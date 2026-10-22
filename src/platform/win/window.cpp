// `monitor.h` 会经 `core/placement.h` 拉到 `core/keys.h`，必须在任何 `windows.h`
// 之前包含：`keys.h` 里的 `DELETE` / `XBUTTON1` / `XBUTTON2` 与 windows.h 的
// 宏同名。
#include "platform/win/monitor.h"

#include "platform/win/window.h"

#include "core/window_match.h"
#include "platform/win/desktop.h"
#include "platform/win/dwm.h"
#include "platform/win/logging.h"

#include <QHash>

#include <algorithm>
#include <optional>

namespace flowkeyd::platform::win::window {

namespace {

/// 拥有 `pid` 的可执行文件的小写文件名（`wezterm-gui.exe`）。
/// 取不到（进程已退出、访问被拒）时返回空。
std::optional<QString> processImageName(DWORD pid)
{
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr) {
        return std::nullopt;
    }
    wchar_t buffer[1024];
    DWORD length = static_cast<DWORD>(std::size(buffer));
    const BOOL ok = QueryFullProcessImageNameW(handle, 0, buffer, &length);
    CloseHandle(handle);
    if (ok == 0 || length == 0) {
        return std::nullopt;
    }
    return core::executableBaseName(QString::fromWCharArray(buffer, static_cast<int>(length)));
}

/// `EnumWindows` 的搜索状态（回调不能接收闭包，因此用线程局部变量）。
struct Finder
{
    const core::WindowQuery *query = nullptr;
    /// 被工作空间藏起来的窗口（它们的 `IsWindowVisible` 是假的，但仍然是用户自己
    /// 的程序窗口）：没有的话 `window("activate")` 根本找不到藏在别的空间里的
    /// 窗口（M9）。
    const std::vector<HWND> *tracked = nullptr;
    /// 已还原的窗口优于被我们藏起来的，藏起来的优于最小化的。
    HWND restored = nullptr;
    HWND hidden = nullptr;
    HWND minimized = nullptr;
    /// 进程 id -> 小写可执行文件名，使一次枚举中每个进程只需一次 `OpenProcess`。
    QHash<DWORD, std::optional<QString>> names;
};

thread_local Finder *t_finder = nullptr;

std::optional<QString> processNameFor(HWND hwnd, Finder &finder)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return std::nullopt;
    }
    const auto cached = finder.names.constFind(pid);
    if (cached != finder.names.constEnd()) {
        return *cached;
    }
    std::optional<QString> name = processImageName(pid);
    finder.names.insert(pid, name);
    return name;
}

BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM)
{
    Finder &finder = *t_finder;
    if (finder.restored != nullptr) {
        return FALSE;
    }
    // 不可见窗口，以及属于别的窗口的窗口（对话框、工具提示、弹出菜单）
    // 永远不是用户想要的那个窗口。**例外**：被工作空间藏起来的窗口——它们是用户
    // 自己的程序窗口，只是 `WS_VISIBLE` 被我们清掉了。
    const bool visible = IsWindowVisible(hwnd) != 0;
    const bool hiddenByUs =
        !visible && finder.tracked != nullptr
        && std::find(finder.tracked->begin(), finder.tracked->end(), hwnd) != finder.tracked->end();
    if (!visible && !hiddenByUs) {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    const QString title = windowTitle(hwnd);
    std::optional<QString> executableName;
    if (finder.query->process.has_value()) {
        executableName = processNameFor(hwnd, finder);
    }
    if (!core::windowMatchesQuery(*finder.query, title, executableName)) {
        return TRUE;
    }
    if (IsIconic(hwnd) != 0) {
        if (finder.minimized == nullptr) {
            finder.minimized = hwnd;
        }
        // 继续找已还原的窗口。
        return TRUE;
    }
    if (!visible) {
        // 藏起来的窗口排在“看得见的”后面：同名 / 同进程的多个窗口里先给用户
        // 那些就在眼前的。
        if (finder.hidden == nullptr) {
            finder.hidden = hwnd;
        }
        return TRUE;
    }
    finder.restored = hwnd;
    return FALSE;
}

/// 窗口是不是在当前（用户看着的那张）虚拟桌面上。
///
/// 拿不到答案时（虚拟桌面后端不可用）返回 `true`，也就是按老行为处理：
/// 探测失败不该改变动作的结果。
bool isOnCurrentDesktop(HWND hwnd)
{
    QString error;
    const std::optional<bool> onCurrent = desktop::isWindowOnCurrentDesktop(hwnd, &error);
    if (!onCurrent.has_value()) {
        logDebug(QStringLiteral("could not tell whether the window is on the current desktop: %1")
                     .arg(error));
        return true;
    }
    return *onCurrent;
}

/// 窗口是不是“用户现在真的在用的那一个”。
///
/// 只看 `GetForegroundWindow()` 是不够的：`window_rule` 把窗口搬到别的虚拟桌面
/// （未公开的 `MoveViewToDesktop`）之后，**shell 仍然把它当作前台窗口**（本机 24H2
/// 实测），于是它会看起来“已经激活”，而用户根本看不到它。
bool isForegroundOnThisDesktop(HWND hwnd)
{
    if (GetForegroundWindow() != hwnd || IsIconic(hwnd) != 0) {
        return false;
    }
    return isOnCurrentDesktop(hwnd);
}

/// 覆盖层窗口（不是用户真正在用的那个窗口）。
///
/// 范例：**PowerToys 的「快捷键指南」**（`PowerToys.ShortcutGuide.exe`，按住 Win
/// 约一秒就会弹出来）。它是 `WS_EX_TOOLWINDOW | WS_EX_TOPMOST`，弹出后会成为
/// `GetForegroundWindow()`，于是“对前台窗口做动作”的 `window` 快捷键就会去操控这个
/// 覆盖层（实测报 `could not read the desktop id of the window` —— 覆盖层不属于
/// 任何虚拟桌面）。用户看到的现象是“按住 Win 连按下一个和弦就没反应了，松手再按才行”。
///
/// 与 `topLevelWindows()` / `window_rule` 的过滤一致：工具窗口不是用户想要的目标。
bool isOverlayWindow(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
}

/// 句柄的十六进制文本（`0x20602`），日志用。
///
/// **一定要传 `QString`**：Qt 6 里有
/// `QString::arg(const QString &a, int fieldWidth, QChar fillChar)` 这个重载，
/// 所以 `.arg(标题, 句柄)` 会把句柄当成**字段宽度** —— 那次替换变成十几万个空格、
/// 而后面的 `%2` 原样留下（本机实测：`hwnd 0x20602` 让日志行变成 132 KB）。
QString handleText(HWND hwnd)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(hwnd)),
                                      0,
                                      16);
}

/// 沿 Z 序往下找第一个“主窗口”：可见、无属主、非工具窗口、有标题、尺寸非零。
/// 找不到时返回 `nullptr`。
HWND firstMainWindowBelow(HWND hwnd)
{
    for (HWND current = GetWindow(hwnd, GW_HWNDNEXT); current != nullptr;
         current = GetWindow(current, GW_HWNDNEXT)) {
        if (IsWindowVisible(current) == 0 || GetWindow(current, GW_OWNER) != nullptr) {
            continue;
        }
        if ((GetWindowLongPtrW(current, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
            continue;
        }
        if (windowTitle(current) == QStringLiteral("<untitled>")) {
            continue;
        }
        RECT rect{};
        if (GetWindowRect(current, &rect) == 0 || rect.right <= rect.left
            || rect.bottom <= rect.top) {
            continue;
        }
        return current;
    }
    return nullptr;
}

/// 把 Win32 能问到的都问出来，交给 `core::window_match` 的纯函数判断。
///
/// 便宜的检查（可见性 / 属主 / 工具窗口）先做，真正跨进程的调用（读标题、
/// 读矩形）只对活得下来的窗口做；`cloaked` 不在这里读 —— 调用方先确认它
/// “看起来像主窗口”再问 DWM（见 `isSwitchableWindow`）。
core::TopLevelWindowFacts windowFacts(HWND hwnd, bool requireVisible = true)
{
    core::TopLevelWindowFacts facts;
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        return facts;
    }
    if (IsWindowVisible(hwnd) == 0) {
        // `requireVisible = false` 是给工作空间用的：被我们 `SW_HIDE` 藏起来的
        // 窗口也得能判断“它本来是不是一个主窗口”。
        if (requireVisible) {
            return facts;
        }
    } else {
        facts.visible = true;
    }
    // 外壳自己的「Program Manager」（`Progman`）不是用户想切换到的程序窗口。
    if (hwnd == GetShellWindow()) {
        facts.isShellWindow = true;
        return facts;
    }
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    facts.appWindow = (exStyle & WS_EX_APPWINDOW) != 0;
    facts.hasOwner = GetWindow(hwnd, GW_OWNER) != nullptr;
    if (facts.hasOwner && !facts.appWindow) {
        return facts;
    }
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        facts.toolWindow = true;
        return facts;
    }
    if (GetWindowTextLengthW(hwnd) <= 0) {
        return facts;
    }
    facts.hasTitle = true;
    RECT rect{};
    facts.hasArea = GetWindowRect(hwnd, &rect) != 0 && rect.right > rect.left
                    && rect.bottom > rect.top;
    return facts;
}

} // namespace

QString windowTitle(HWND hwnd)
{
    if (IsWindow(hwnd) == 0) {
        return QStringLiteral("<invalid window>");
    }
    wchar_t buffer[512];
    const int length = GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 0) {
        return QStringLiteral("<untitled>");
    }
    return QString::fromWCharArray(buffer, length);
}

HWND find(const core::WindowQuery &query, const std::vector<HWND> &tracked)
{
    if (query.isForeground()) {
        HWND hwnd = GetForegroundWindow();
        // 前台窗口是个覆盖层时（例如按住 Win 弹出来的 PowerToys「快捷键指南」）
        // 它不是用户真正在用的窗口。它是 topmost 的工具窗口，所以沿 Z 序往下
        // 找第一个“主窗口”。
        if (hwnd != nullptr && isOverlayWindow(hwnd)) {
            if (HWND below = firstMainWindowBelow(hwnd); below != nullptr) {
                logDebug(QStringLiteral("foreground %1 (%2) is an overlay; using %3 (%4) instead")
                             .arg(core::rustDebug(windowTitle(hwnd)), handleText(hwnd))
                             .arg(core::rustDebug(windowTitle(below)), handleText(below)));
                return below;
            }
        }
        return hwnd;
    }
    Finder finder;
    finder.query = &query;
    finder.tracked = &tracked;
    t_finder = &finder;
    EnumWindows(enumWindowProc, 0);
    t_finder = nullptr;
    if (finder.restored != nullptr) {
        return finder.restored;
    }
    if (finder.hidden != nullptr) {
        return finder.hidden;
    }
    return finder.minimized;
}

std::optional<QString> processName(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return std::nullopt;
    }
    return processImageName(pid);
}

namespace {

struct Collector
{
    std::vector<HWND> *out = nullptr;
};

BOOL CALLBACK collectWindowProc(HWND hwnd, LPARAM param)
{
    auto *collector = reinterpret_cast<Collector *>(param);
    // 与 `find` 同一套前置过滤：不可见的、有属主的（对话框/工具提示/弹出菜单）
    // 都不是 `window_rule` 想摆放的“主窗口”。
    if (IsWindowVisible(hwnd) == 0 || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    collector->out->push_back(hwnd);
    return TRUE;
}

} // namespace

std::vector<HWND> topLevelWindows()
{
    std::vector<HWND> out;
    Collector collector{&out};
    EnumWindows(collectWindowProc, reinterpret_cast<LPARAM>(&collector));
    return out;
}

bool isMainWindow(HWND hwnd)
{
    return core::isMainWindow(windowFacts(hwnd));
}

bool isMainWindowIgnoringVisibility(HWND hwnd)
{
    core::TopLevelWindowFacts facts = windowFacts(hwnd, false);
    // 不要求可见，但其余判据（无属主 / 非工具窗口 / 有标题 / 有尺寸）一个不少。
    facts.visible = true;
    return core::isMainWindow(facts);
}

bool isSwitchableWindow(HWND hwnd, bool hiddenByUs)
{
    // 被工作空间藏起来的窗口 `IsWindowVisible` 是假的：判据只看它的**其他**
    // 主窗口特征（无属主 / 非工具窗口 / 有标题 / 有尺寸），可见性交给
    // `hiddenByUs` 这个参数（它来自状态机的记录，不是 Win32）。
    core::TopLevelWindowFacts facts = windowFacts(hwnd, !hiddenByUs);
    facts.hiddenByUs = hiddenByUs;
    // 用 `core::isSwitchableWindow()` 而不是 `isMainWindow()`：前者把「被我们
    // 藏起来的窗口」也算成可切换（它会把 `hiddenByUs` 折成可见），后者只认
    // 当下的可见性。这里 `facts.cloaked` 还没填，所以两种写法差别只在那一句。
    if (!core::isSwitchableWindow(facts)) {
        return false;
    }
    // 只有“看起来像主窗口”的窗口才值得问 DWM；被 cloaked 的更是少数，
    // 那才需要再问一次虚拟桌面（COM，每次一条一次性 STA 线程）。
    facts.cloaked = dwm::isCloaked(hwnd);
    if (!facts.cloaked) {
        return true;
    }
    QString error;
    const std::optional<bool> onCurrent = desktop::isWindowOnCurrentDesktop(hwnd, &error);
    if (!onCurrent.has_value()) {
        // 问不出来时按“它在别的桌面上”处理：宁可多列一条，也不要把用户的
        // 窗口藏起来（与 `isOnCurrentDesktop()` 同一个原则）。
        logDebug(QStringLiteral("could not tell whether cloaked %1 is on the current "
                                "desktop (%2); listing it anyway")
                     .arg(core::rustDebug(windowTitle(hwnd)), error));
        return true;
    }
    facts.onCurrentDesktop = *onCurrent;
    if (!core::isSwitchableWindow(facts)) {
        logDebug(QStringLiteral("skipping hidden shell window %1 (%2, cloaked on this "
                                "desktop)")
                     .arg(core::rustDebug(windowTitle(hwnd)), handleText(hwnd)));
        return false;
    }
    return true;
}

namespace {

struct WindowCollector
{
    std::vector<OpenWindow> *out = nullptr;
    const std::vector<HWND> *tracked = nullptr;
    DWORD self = 0;
};

BOOL CALLBACK collectOpenWindowProc(HWND hwnd, LPARAM param)
{
    auto *collector = reinterpret_cast<WindowCollector *>(param);
    const bool hiddenByUs =
        collector->tracked != nullptr
        && std::find(collector->tracked->begin(), collector->tracked->end(), hwnd)
               != collector->tracked->end();
    if (!isSwitchableWindow(hwnd, hiddenByUs)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    // 跳过 flowkeyd 自己的窗口（切换器 / 帮助 / 选单 / 日志）。
    if (pid == 0 || pid == collector->self) {
        return TRUE;
    }
    OpenWindow entry;
    entry.hwnd = hwnd;
    entry.title = windowTitle(hwnd);
    entry.process = processImageName(pid).value_or(QString());
    collector->out->push_back(std::move(entry));
    return TRUE;
}

} // namespace

std::vector<OpenWindow> listOpenWindows(const std::vector<HWND> &tracked)
{
    std::vector<OpenWindow> out;
    WindowCollector collector{&out, &tracked, GetCurrentProcessId()};
    EnumWindows(collectOpenWindowProc, reinterpret_cast<LPARAM>(&collector));
    return out;
}

bool isActive(HWND hwnd)
{
    return isForegroundOnThisDesktop(hwnd);
}

bool raiseWindow(HWND hwnd)
{
    // 窗口在别的虚拟桌面上时，`SetForegroundWindow` 是**不够**的：如果它正好是
    // shell 记着的前台窗口（被 `MoveViewToDesktop` 搬走之后就是这样，本机实测），
    // `SetForegroundWindow` 会返回 TRUE 却什么都不做。所以先把视图切到它那一张桌面。
    if (!isOnCurrentDesktop(hwnd)) {
        QString detail;
        QString error;
        if (!desktop::switchToWindowDesktop(hwnd, &detail, &error)) {
            logWarn(QStringLiteral("could not switch to the desktop of %1: %2")
                        .arg(core::rustDebug(windowTitle(hwnd)), error));
        } else {
            logDebug(QStringLiteral("switched to the desktop of the window: %1").arg(detail));
        }
    }
    // 除非调用进程拥有前台锁，否则 `SetForegroundWindow` 会被拒绝，
    // 而钩子守护进程不能指望拥有它。三级递进尝试：
    // 1. `SetForegroundWindow`（常见情况下唯一需要的）。
    // 2. 附着到持有锁的那个线程的输入队列，再从那里重试。
    // 3. `BringWindowToTop` + `SetWindowPos(HWND_TOP)`，然后再重试一次。
    if (isActive(hwnd)) {
        return true;
    }
    if (SetForegroundWindow(hwnd) != 0) {
        return true;
    }

    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread =
        foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD thisThread = GetCurrentThreadId();
    // 每条路径上 `AttachThreadInput` 都必须配平。
    const bool attached = foregroundThread != 0 && foregroundThread != thisThread
                          && AttachThreadInput(foregroundThread, thisThread, TRUE) != 0;

    bool raised = SetForegroundWindow(hwnd) != 0;
    if (!raised) {
        BringWindowToTop(hwnd);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        raised = SetForegroundWindow(hwnd) != 0;
    }
    if (attached) {
        AttachThreadInput(foregroundThread, thisThread, FALSE);
    }
    return raised || isActive(hwnd);
}

bool setTopmost(HWND hwnd, bool topmost, QString *error)
{
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("the window is gone");
        }
        return false;
    }
    // `WS_EX_TOPMOST` 是窗口管理器维护的“伪样式”：不能用 `SetWindowLongPtr`
    // 设置，只能用 `SetWindowPos` 的 `HWND_TOPMOST` / `HWND_NOTOPMOST`
    // （手动写样式位会造成“返回 TRUE 却没生效”的怪状态，实测会遇到）。
    if (SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)
        == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("could not %1 %2")
                         .arg(topmost ? QStringLiteral("raise") : QStringLiteral("lower"),
                              core::rustDebug(windowTitle(hwnd)));
        }
        return false;
    }
    return true;
}

bool applyTo(HWND hwnd, core::WindowOp op, bool animate, QString *detail, QString *error, bool follow)
{
    const QString title = windowTitle(hwnd);
    std::optional<dwm::TransitionGuard> guard;
    if (core::windowOpHasTransition(op)) {
        guard.emplace(hwnd, animate);
    }

    // 需要额外一行说明的 op（例如“搬到了 2/4 号桌面”）把说明放在这里，
    // 拼进最后那条统一的 detail。
    QString extra;

    switch (op) {
    case core::WindowOp::Activate:
        if (IsIconic(hwnd) != 0) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        if (!raiseWindow(hwnd)) {
            if (error != nullptr) {
                *error = QStringLiteral("could not bring %1 to the foreground")
                             .arg(core::rustDebug(title));
            }
            return false;
        }
        break;
    case core::WindowOp::Minimize:
        ShowWindow(hwnd, SW_MINIMIZE);
        break;
    case core::WindowOp::Maximize:
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        break;
    case core::WindowOp::Restore:
        ShowWindow(hwnd, SW_RESTORE);
        break;
    case core::WindowOp::Close:
        if (PostMessageW(hwnd, WM_CLOSE, 0, 0) == 0) {
            if (error != nullptr) {
                *error = QStringLiteral("could not post WM_CLOSE to %1").arg(core::rustDebug(title));
            }
            return false;
        }
        break;
    case core::WindowOp::ToggleTopmost: {
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const bool topmost = (style & WS_EX_TOPMOST) != 0;
        if (!setTopmost(hwnd, !topmost, error)) {
            return false;
        }
        break;
    }
    case core::WindowOp::MovePrevDesktop:
    case core::WindowOp::MoveNextDesktop: {
        const int delta = op == core::WindowOp::MovePrevDesktop ? -1 : 1;
        QString note;
        if (!desktop::moveWindowToAdjacentDesktop(hwnd, delta, &note, error, follow)) {
            return false;
        }
        extra = note;
        // `follow` 的语义是“用户跟着窗口到那张桌面”，所以搬完还要让窗口重新拿到
        // 前台。失败不算动作失败：窗口确实已经搬过去、视图也确实跟着走了。
        if (follow && !raiseWindow(hwnd)) {
            logWarn(QStringLiteral("moved %1 to its desktop but could not activate it")
                        .arg(core::rustDebug(title)));
            extra += QStringLiteral(", could not activate");
        }
        break;
    }
    case core::WindowOp::MoveLeftMonitor:
    case core::WindowOp::MoveRightMonitor: {
        const int delta = op == core::WindowOp::MoveLeftMonitor ? -1 : 1;
        QString note;
        if (!monitor::moveToAdjacentMonitor(hwnd, delta, &note, error)) {
            return false;
        }
        extra = note;
        break;
    }
    }

    if (detail != nullptr) {
        QString text = QStringLiteral("%1 %2").arg(core::windowOpDebugName(op), core::rustDebug(title));
        if (!extra.isEmpty()) {
            text += QStringLiteral(" (%1)").arg(extra);
        }
        *detail = text;
    }
    return true;
}

QString missing(const core::WindowQuery &query)
{
    if (query.isForeground()) {
        return QStringLiteral("there is no foreground window");
    }
    return QStringLiteral("no visible window %1").arg(query.describe());
}

QString foregroundTitle()
{
    const HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) {
        return QStringLiteral("<none>");
    }
    wchar_t buffer[256];
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    const QString className =
        length > 0 ? QString::fromWCharArray(buffer, length) : QStringLiteral("<unknown>");
    return QStringLiteral("%1 [%2]").arg(windowTitle(hwnd), className);
}

} // namespace flowkeyd::platform::win::window
