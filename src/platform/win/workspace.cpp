// 工作空间的平台后端。隐藏方式的选型与 M0 实测结论见头文件里的说明。
#include "platform/win/workspace.h"

#include "core/window_match.h"
#include "platform/win/logging.h"
#include "platform/win/monitor.h"
#include "platform/win/window.h"

#include <QHash>

#include <algorithm>
#include <map>
#include <optional>

namespace flowkeyd::platform::win::workspace {

namespace {

QString toString(const wchar_t *buffer, int length)
{
    if (length <= 0) {
        return QString();
    }
    return QString::fromWCharArray(buffer, length);
}

/// 属主进程的创建时间（`FILETIME`）。拿不到时返回 0。
std::uint64_t processStartedAt(DWORD pid)
{
    if (pid == 0) {
        return 0;
    }
    // `PROCESS_QUERY_LIMITED_INFORMATION` 对同权限级别的进程就够（比
    // `PROCESS_QUERY_INFORMATION` 更容易拿到，尤其是提权进程）。
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    }
    if (process == nullptr) {
        return 0;
    }
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const BOOL ok = GetProcessTimes(process, &creation, &exit, &kernel, &user);
    CloseHandle(process);
    if (ok == 0) {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return static_cast<std::uint64_t>(value.QuadPart);
}

/// 外壳自己的窗口类名：桌面（`Progman` / `WorkerW`）与任务栏（两个都算）。
bool isShellClass(const QString &className)
{
    return className == QLatin1String("Progman")
        || className == QLatin1String("WorkerW")
        || className == QLatin1String("Shell_TrayWnd")
        || className == QLatin1String("Shell_SecondaryTrayWnd");
}

/// `hide()` 记下来的位置信息（给 `show()` 兜底用）。
///
/// **不加锁**：这些函数只允许在动作工作线程上调用（见头文件的约束 1），
/// 而那条线程是工作空间状态的唯一所有者（`AGENTS.md` 第 7 节不变量 5 的同一条
/// 理由：不要在可能阻塞的 Win32 调用外面套锁）。
std::map<HWND, WINDOWPLACEMENT> &savedPlacements()
{
    static std::map<HWND, WINDOWPLACEMENT> map;
    return map;
}

/// 隐藏前的几何与最大化状态。位置/大小由 Windows 自己保持（M0 实测），
/// 存下来只是给 `show()` 一个“万一 `showCmd` 丢了就摆回去”的兜底。
WINDOWPLACEMENT placementOf(HWND hwnd)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement) == 0) {
        placement.length = 0;
    }
    return placement;
}

} // namespace

QString classNameOf(HWND hwnd)
{
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        return QString();
    }
    wchar_t buffer[256];
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    return toString(buffer, length);
}

Identity identityOf(HWND hwnd)
{
    Identity identity;
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        return identity;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    identity.className = classNameOf(hwnd);
    if (pid == 0 || identity.className.isEmpty()) {
        return identity;
    }
    identity.pid = pid;
    identity.processStarted = processStartedAt(pid);
    identity.valid = true;
    return identity;
}

bool stillSame(HWND hwnd, const Identity &identity)
{
    if (!identity.valid || hwnd == nullptr || IsWindow(hwnd) == 0) {
        return false;
    }
    // 类名与 PID 都很便宜，先比它们，对不上就不用再开进程了。
    if (classNameOf(hwnd) != identity.className) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != identity.pid) {
        return false;
    }
    if (identity.processStarted == 0) {
        // 当年就没读到创建时间（权限不足）：PID + 类名已经是能拿到的最好判据。
        return true;
    }
    return processStartedAt(pid) == identity.processStarted;
}

bool isAlive(HWND hwnd)
{
    return hwnd != nullptr && IsWindow(hwnd) != 0;
}

HWND manageTarget(HWND hwnd)
{
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        return nullptr;
    }
    // UWP：真正要藏的是 `ApplicationFrameHost.exe` 的 `ApplicationFrameWindow`。
    // 传进来的如果是它内部那个 `Windows.UI.Core.CoreWindow`（子窗口），
    // 沿根窗口往上找一次就够了。
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root == nullptr) {
        return hwnd;
    }
    if (root != hwnd && classNameOf(root) == QLatin1String("ApplicationFrameWindow")) {
        return root;
    }
    return hwnd;
}

bool hide(HWND hwnd, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!isAlive(hwnd)) {
        return fail(QStringLiteral("the window is gone"));
    }
    // `SW_HIDE` 会 `SendMessage` 到目标线程（0.6–2.2 ms/窗口）。目标进程卡死时
    // 那一下可能把**我们这条线程**挂住，而恢复手段是不依赖目标响应的日志重放
    // （M8），所以这里宁可直接失败。`IsHungAppWindow()` 是唯一的廉价护航。
    if (IsHungAppWindow(hwnd) != 0) {
        return fail(QStringLiteral("the window is not responding"));
    }
    const WINDOWPLACEMENT placement = placementOf(hwnd);
    ShowWindow(hwnd, SW_HIDE);
    // `ShowWindow` 的返回值是“之前的可见状态”，0 是正常情况，所以看后置条件。
    if (IsWindowVisible(hwnd) != 0) {
        return fail(lastErrorMessage("ShowWindow(SW_HIDE)"));
    }
    if (placement.length != 0) {
        savedPlacements()[hwnd] = placement;
    }
    return true;
}

bool show(HWND hwnd, HWND insertAfter, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!isAlive(hwnd)) {
        savedPlacements().erase(hwnd);
        return fail(QStringLiteral("the window is gone"));
    }
    // 最小化的窗口（`IsIconic`）显示回来时仍是 `SW_SHOWMINNOACTIVE`
    // （M0 实测 `IsIconic` 保持为 1、几何不变），这里照抄那个状态。
    const bool minimized = IsIconic(hwnd) != 0;
    ShowWindow(hwnd, minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
    if (IsWindowVisible(hwnd) == 0) {
        return fail(lastErrorMessage("ShowWindow(SW_SHOWNOACTIVATE)"));
    }
    // 兜底：`showCmd` 变了（最大化 / 最小化丢了）就拿隐藏前的记录摆回去。
    // 只修“状态”，不碰位置 —— 位置 Windows 自己保持得比我们好。
    const auto it = savedPlacements().find(hwnd);
    if (it != savedPlacements().end()) {
        const WINDOWPLACEMENT &saved = it->second;
        WINDOWPLACEMENT current{};
        current.length = sizeof(current);
        if (GetWindowPlacement(hwnd, &current) != 0 && current.showCmd != saved.showCmd
            && (saved.showCmd == SW_SHOWMAXIMIZED || saved.showCmd == SW_SHOWMINIMIZED)) {
            WINDOWPLACEMENT restore = saved;
            restore.length = sizeof(restore);
            if (SetWindowPlacement(hwnd, &restore) == 0) {
                logWarn(QStringLiteral("could not restore the placement of %1: %2")
                            .arg(core::rustDebug(window::windowTitle(hwnd)),
                                 winErrorMessage(GetLastError())));
            }
        }
        savedPlacements().erase(it);
    }
    if (insertAfter != nullptr && IsWindow(insertAfter) != 0) {
        SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return true;
}

bool restack(const std::vector<HWND> &bottomToTop, QString *error)
{
    if (bottomToTop.size() < 2) {
        // 0 / 1 个窗口时没有“顺序”可言。
        return true;
    }
    // `SetWindowPos(hwnd, insertAfter, …)` 的 `insertAfter` 是**排在它前面**
    // （Z 序更高）的那个窗口，所以底 → 顶逐对排过去正好。
    for (std::size_t i = 0; i + 1 < bottomToTop.size(); ++i) {
        HWND lower = bottomToTop.at(i);
        HWND upper = bottomToTop.at(i + 1);
        if (!isAlive(lower) || !isAlive(upper)) {
            continue;
        }
        if (SetWindowPos(lower, upper, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == 0) {
            if (error != nullptr) {
                *error = lastErrorMessage("SetWindowPos(restack)");
            }
            return false;
        }
    }
    return true;
}

bool relocateToMonitor(HWND hwnd, const core::MonitorDescription &destination, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!isAlive(hwnd)) {
        return fail(QStringLiteral("the window is gone"));
    }
    // 与 `hide()` 同一个顾虑：`SetWindowPlacement` / `SetWindowPos` 都是跨线程
    // `SendMessage` 语义，目标进程卡死时会把我们这条线程（= 动作线程，也是工作
    // 空间状态的唯一所有者）挂住。
    if (IsHungAppWindow(hwnd) != 0) {
        return fail(QStringLiteral("the window is not responding"));
    }

    const bool visible = IsWindowVisible(hwnd) != 0;
    const bool minimized = IsIconic(hwnd) != 0;
    const bool maximized = !minimized && IsZoomed(hwnd) != 0;
    // `windowRect()` 给的是**还原矩形**（`rcNormalPosition`）：最大化 / 最小化 /
    // 被我们藏起来的窗口都能拿到有意义的值，正好是要摆的目标。
    const std::optional<core::Rect> current = monitor::windowRect(hwnd);
    const core::Rect rect = core::placementRect(current.value_or(core::Rect{0, 0, 800, 600}),
                                                destination, maximized, std::nullopt,
                                                std::nullopt, std::nullopt, std::nullopt);

    // 最大化窗口只改 `rcNormalPosition` 不会重新最大化到新屏上（它会留着原来的
    // 最大化矩形）：先还原，摆好位置再重新最大化（与 `monitor::applyPlacement()`
    // 同一条路）。**藏着的窗口不能这么做** —— `SW_RESTORE` 会把它露出来。
    if (visible && maximized) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    WINDOWPLACEMENT placement = placementOf(hwnd);
    if (placement.length == 0) {
        return fail(QStringLiteral("could not read the window placement"));
    }
    placement.rcNormalPosition =
        RECT{static_cast<LONG>(rect.x),
             static_cast<LONG>(rect.y),
             static_cast<LONG>(rect.x + rect.width),
             static_cast<LONG>(rect.y + rect.height)};
    // **显隐照原样**：藏着就用 `SW_HIDE`（`showCmd` 直接决定显隐状态，写成
    // `SW_SHOWNORMAL` 会把这个窗口弹出来 —— 那正是不能用 `moveToMonitor()` 的原因）。
    if (!visible) {
        placement.showCmd = SW_HIDE;
    } else if (minimized) {
        placement.showCmd = SW_SHOWMINNOACTIVE;
    } else if (maximized) {
        placement.showCmd = SW_SHOWMAXIMIZED;
    } else {
        placement.showCmd = SW_SHOWNOACTIVATE;
    }
    if (SetWindowPlacement(hwnd, &placement) == 0) {
        return fail(lastErrorMessage("SetWindowPlacement(relocate)"));
    }
    // `SetWindowPlacement` 的坐标历史上有过“工作区坐标”的差异（见
    // `monitor::applyPlacement()`），非最大化时再用 `SetWindowPos` 钉一次；
    // 最大化没生效的窗口退化成“铺满目标工作区”，至少位置是对的。
    if (!minimized && !maximized) {
        SetWindowPos(hwnd, nullptr, rect.x, rect.y, rect.width, rect.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (visible && maximized && IsZoomed(hwnd) == 0) {
        SetWindowPos(hwnd, nullptr, rect.x, rect.y, rect.width, rect.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // 藏着的窗口要做两件事收尾：
    //  1. `show()` 的兜底用的是 `hide()` 记下的那份 `WINDOWPLACEMENT`，它的
    //     `rcNormalPosition` 也得跟着挪，否则重新露出来时会又跑回旧屏；
    //  2. `SetWindowPlacement` 可能按 `showCmd` 把藏着的窗口露出来。
    if (!visible) {
        const auto saved = savedPlacements().find(hwnd);
        if (saved != savedPlacements().end()) {
            saved->second.rcNormalPosition = placement.rcNormalPosition;
        }
        if (IsWindowVisible(hwnd) != 0) {
            ShowWindow(hwnd, SW_HIDE);
            if (IsWindowVisible(hwnd) != 0) {
                return fail(lastErrorMessage("ShowWindow(SW_HIDE) after moving"));
            }
        }
    }
    return true;
}

namespace {

struct EnumerateContext
{
    const std::vector<HWND> *tracked = nullptr;
    const QStringList *excludedProcesses = nullptr;
    std::vector<HWND> *out = nullptr;
    DWORD self = 0;
};

/// 把一个窗口的「原始观察值」收集好，交给 `core::isManageableWindow()` 判断。
/// 单窗口与枚举两条路径共用（判据只有一份）。
bool manageable(HWND hwnd, bool tracked, const QStringList &excludedProcesses, DWORD self)
{
    core::ManageableWindowFacts facts;
    facts.exists = isAlive(hwnd);
    facts.tracked = tracked;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    facts.ownProcess = pid == self;
    facts.shellWindow = isShellClass(classNameOf(hwnd));
    // 被我们藏起来的窗口仍然算「主窗口」（判据里不要求可见）。
    facts.mainWindow = tracked ? window::isMainWindowIgnoringVisibility(hwnd)
                              : window::isMainWindow(hwnd);

    const std::optional<QString> process = window::processName(hwnd);
    return core::isManageableWindow(facts, process, excludedProcesses);
}

BOOL CALLBACK enumerateProc(HWND hwnd, LPARAM param)
{
    auto *context = reinterpret_cast<EnumerateContext *>(param);
    const bool tracked = std::find(context->tracked->begin(), context->tracked->end(), hwnd)
        != context->tracked->end();
    if (!manageable(hwnd, tracked, *context->excludedProcesses, context->self)) {
        return TRUE;
    }
    context->out->push_back(hwnd);
    return TRUE;
}

} // namespace

bool isManageable(HWND hwnd, bool tracked, const QStringList &excludedProcesses)
{
    if (hwnd == nullptr) {
        return false;
    }
    return manageable(hwnd, tracked, excludedProcesses, GetCurrentProcessId());
}

std::vector<HWND> enumerateManageable(const std::vector<HWND> &tracked,
                                      const QStringList &excludedProcesses)
{
    std::vector<HWND> out;
    EnumerateContext context{&tracked, &excludedProcesses, &out, GetCurrentProcessId()};
    EnumWindows(enumerateProc, reinterpret_cast<LPARAM>(&context));
    return out;
}

} // namespace flowkeyd::platform::win::workspace
