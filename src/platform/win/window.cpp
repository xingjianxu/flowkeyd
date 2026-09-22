#include "platform/win/window.h"

#include "core/window_match.h"
#include "platform/win/dwm.h"
#include "platform/win/logging.h"

#include <QHash>

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
    /// 已还原的窗口优于最小化的窗口。
    HWND restored = nullptr;
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
    // 永远不是用户想要的那个窗口。
    if (IsWindowVisible(hwnd) == 0 || GetWindow(hwnd, GW_OWNER) != nullptr) {
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
    finder.restored = hwnd;
    return FALSE;
}

/// 一次会改变窗口状态的调用，顺带（默认）关掉 DWM 的过渡动画。
///
/// `DWMWA_TRANSITIONS_FORCEDISABLED` 读不回来，所以只能“设 TRUE → ShowWindow
/// → 设回 FALSE”，也就是假定这个窗口本来是带动画的。
class TransitionGuard
{
public:
    TransitionGuard(HWND hwnd, bool animate) : m_hwnd(hwnd)
    {
        if (animate) {
            return;
        }
        QString error;
        if (dwm::forceDisableTransitions(hwnd, true, &error)) {
            m_active = true;
        } else {
            // 没有 DWM（或无头会话）时只是动画多一点，不该让动作失败。
            logDebug(QStringLiteral("keeping the DWM transition of the window: %1").arg(error));
        }
    }

    ~TransitionGuard()
    {
        if (!m_active) {
            return;
        }
        QString error;
        if (!dwm::forceDisableTransitions(m_hwnd, false, &error)) {
            logDebug(QStringLiteral("could not re-enable the DWM transition of the window: %1")
                         .arg(error));
        }
    }

    TransitionGuard(const TransitionGuard &) = delete;
    TransitionGuard &operator=(const TransitionGuard &) = delete;

private:
    HWND m_hwnd = nullptr;
    bool m_active = false;
};

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

HWND find(const core::WindowQuery &query)
{
    if (query.isForeground()) {
        HWND hwnd = GetForegroundWindow();
        return hwnd;
    }
    Finder finder;
    finder.query = &query;
    t_finder = &finder;
    EnumWindows(enumWindowProc, 0);
    t_finder = nullptr;
    if (finder.restored != nullptr) {
        return finder.restored;
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

bool isActive(HWND hwnd)
{
    return GetForegroundWindow() == hwnd && IsIconic(hwnd) == 0;
}

bool raiseWindow(HWND hwnd)
{
    // 除非调用进程拥有前台锁，否则 `SetForegroundWindow` 会被拒绝，
    // 而钩子守护进程不能指望拥有它。三级递进尝试（照抄 oskeyd 的
    // `raise_window`）：
    // 1. `SetForegroundWindow`（常见情况下唯一需要的）。
    // 2. 附着到持有锁的那个线程的输入队列，再从那里重试。
    // 3. `BringWindowToTop` + `SetWindowPos(HWND_TOP)`，然后再重试一次。
    if (GetForegroundWindow() == hwnd && IsIconic(hwnd) == 0) {
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
    return raised || GetForegroundWindow() == hwnd;
}

bool applyTo(HWND hwnd, core::WindowOp op, bool animate, QString *detail, QString *error)
{
    const QString title = windowTitle(hwnd);
    std::optional<TransitionGuard> guard;
    if (core::windowOpHasTransition(op)) {
        guard.emplace(hwnd, animate);
    }

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
        const LONG_PTR updated = topmost ? (style & ~static_cast<LONG_PTR>(WS_EX_TOPMOST))
                                         : (style | static_cast<LONG_PTR>(WS_EX_TOPMOST));
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, updated);
        SetWindowPos(hwnd, topmost ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        break;
    }
    }

    if (detail != nullptr) {
        *detail = QStringLiteral("%1 %2").arg(core::windowOpDebugName(op), core::rustDebug(title));
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
