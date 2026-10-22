// 工作空间的平台后端。隐藏方式的选型与 M0 实测结论见头文件里的说明。
#include "platform/win/workspace.h"

#include "core/action.h"
#include "core/window_match.h"
#include "platform/win/dwm.h"
#include "platform/win/logging.h"
#include "platform/win/monitor.h"
#include "platform/win/window.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <algorithm>
#include <atomic>
#include <map>
#include <optional>
#include <thread>

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

/// 并行跑完 `items` 里的每一项（每项只能碰它自己的那个窗口）。
///
/// 用在哪：`hideMany()` / `showMany()` 的 `ShowWindow`。这些调用是**跨线程
/// `SendMessage` 语义**，耗时就是目标 App 的响应时间；一个一个做的话，一屏窗口
/// 会“依次”消失 / 出现（真机实测：UI 线程睡 300 ms 的 App，串行 3 个窗口要 900 ms）。
///
/// `items` 里只应有**属于别的线程**的窗口：属于调用线程的要先就在调用线程上做掉
/// （`ownedByCallingThread()`），否则就是自己等自己的死锁。
///
/// 线程数上限 8：一次切换开一次（每个线程只干几毫秒），不值得为它维护一个常驻池；
/// 多出来的窗口排在后面，拿的是最早空出来的那条线程。
template <typename Work>
void runConcurrently(const std::vector<std::size_t> &items, Work work)
{
    if (items.empty()) {
        return;
    }
    const std::size_t threads = std::min<std::size_t>(items.size(), 8);
    if (threads == 1) {
        work(items.front());
        return;
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::size_t index = 0; index < threads; ++index) {
        workers.emplace_back([&next, &items, &work]() {
            for (;;) {
                const std::size_t slot = next.fetch_add(1);
                if (slot >= items.size()) {
                    return;
                }
                work(items.at(slot));
            }
        });
    }
    for (std::thread &worker : workers) {
        worker.join();
    }
}

/// 这个窗口的窗口过程就在**调用线程**上吗。
///
/// 是的话绝不能把它丢给临时线程去 `ShowWindow`：那会 `SendMessage` 给窗口所在线程，
/// 而那条线程正在等 `join` —— 自己等自己就是死锁（测试里自己进程的窗口就是这个
/// 情形；真机上目标永远是别的进程，但接口不能有这种隐含前提）。同一线程的窗口
/// 直接同步调即可，不耗时。
bool ownedByCallingThread(HWND hwnd)
{
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(hwnd, &pid);
    return thread != 0 && thread == GetCurrentThreadId();
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

std::vector<BatchResult> hideMany(const std::vector<HWND> &windows)
{
    std::vector<BatchResult> results(windows.size());
    // 1) 调用线程：挑出真能藏的窗口、记 `WINDOWPLACEMENT`（`savedPlacements()` 只由
    //    这条线程读写）。`ShowWindow` 之前的判断全在这里做，工作线程里只剩两句。
    std::vector<HWND> targets;
    // 注意变量名不能叫 `slots`：`rpcndr.h` / MIDL 把一个同名的宏塞进了 `windows.h`
    // （`#define slots`），写出来就是 “declaration does not declare anything”。
    std::vector<std::size_t> indexes;
    targets.reserve(windows.size());
    indexes.reserve(windows.size());
    for (std::size_t index = 0; index < windows.size(); ++index) {
        const HWND hwnd = windows.at(index);
        if (!isAlive(hwnd)) {
            results.at(index) = BatchResult{false, QStringLiteral("the window is gone")};
            continue;
        }
        // 目标进程卡死时 `ShowWindow` 会把调用线程挂住；恢复手段是不依赖目标响应的
        // 日志重放（M8），所以这里宁可直接失败。
        if (IsHungAppWindow(hwnd) != 0) {
            results.at(index) =
                BatchResult{false, QStringLiteral("the window is not responding")};
            continue;
        }
        const WINDOWPLACEMENT placement = placementOf(hwnd);
        if (placement.length != 0) {
            savedPlacements()[hwnd] = placement;
        }
        targets.push_back(hwnd);
        indexes.push_back(index);
    }
    if (targets.empty()) {
        return results;
    }

    // 2) 整批关过渡 + 并行 `ShowWindow(SW_HIDE)`：一屏窗口必须是**一个**视觉动作。
    dwm::setTransitionsDisabled(targets, true);
    std::vector<char> stillVisible(targets.size(), 0);
    std::vector<DWORD> failures(targets.size(), 0);
    const auto hideOne = [&](std::size_t item) {
        const HWND hwnd = targets.at(item);
        ShowWindow(hwnd, SW_HIDE);
        // `ShowWindow` 的返回值是“之前的可见状态”，0 是正常情况，所以只看后置条件。
        if (IsWindowVisible(hwnd) != 0) {
            stillVisible.at(item) = 1;
            failures.at(item) = GetLastError();
        }
    };
    std::vector<std::size_t> parallel;
    if (targets.size() > 1) {
        parallel.reserve(targets.size());
    }
    for (std::size_t item = 0; item < targets.size(); ++item) {
        if (ownedByCallingThread(targets.at(item))) {
            hideOne(item);
        } else {
            parallel.push_back(item);
        }
    }
    runConcurrently(parallel, hideOne);
    dwm::setTransitionsDisabled(targets, false);

    // 3) 调用线程：收拾失败的那几条（错误码是工作线程当场取的 `GetLastError()`）。
    for (std::size_t item = 0; item < targets.size(); ++item) {
        const std::size_t slot = indexes.at(item);
        if (stillVisible.at(item) != 0) {
            savedPlacements().erase(targets.at(item));
            results.at(slot) = BatchResult{false,
                                           QStringLiteral("ShowWindow(SW_HIDE) failed: %1")
                                               .arg(winErrorMessage(failures.at(item)))};
            continue;
        }
        results.at(slot) = BatchResult{true, QString()};
    }
    return results;
}

bool hide(HWND hwnd, QString *error)
{
    const std::vector<BatchResult> results = hideMany({hwnd});
    const BatchResult &result = results.front();
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }
    return true;
}

std::vector<BatchResult> showMany(const std::vector<HWND> &windows)
{
    std::vector<BatchResult> results(windows.size());
    /// 每个窗口要露出来时需要的决定（`showCmd` 与 `hide()` 记下的兜底记录）。
    struct Planned
    {
        HWND hwnd = nullptr;
        bool minimized = false;
        WINDOWPLACEMENT saved{};
        bool hasSaved = false;
    };
    std::vector<Planned> targets;
    std::vector<HWND> targetHandles;
    std::vector<std::size_t> indexes;
    targets.reserve(windows.size());
    targetHandles.reserve(windows.size());
    indexes.reserve(windows.size());
    for (std::size_t index = 0; index < windows.size(); ++index) {
        const HWND hwnd = windows.at(index);
        if (!isAlive(hwnd)) {
            savedPlacements().erase(hwnd);
            results.at(index) = BatchResult{false, QStringLiteral("the window is gone")};
            continue;
        }
        if (IsHungAppWindow(hwnd) != 0) {
            results.at(index) =
                BatchResult{false, QStringLiteral("the window is not responding")};
            continue;
        }
        // 最小化的窗口（`IsIconic`）显示回来时仍是 `SW_SHOWMINNOACTIVE`
        // （M0 实测 `IsIconic` 保持为 1、几何不变），这里照抄那个状态。
        Planned planned;
        planned.hwnd = hwnd;
        planned.minimized = IsIconic(hwnd) != 0;
        if (const auto it = savedPlacements().find(hwnd); it != savedPlacements().end()) {
            planned.saved = it->second;
            planned.hasSaved = true;
        }
        targets.push_back(planned);
        targetHandles.push_back(hwnd);
        indexes.push_back(index);
    }
    if (targets.empty()) {
        return results;
    }

    dwm::setTransitionsDisabled(targetHandles, true);
    std::vector<char> stillHidden(targets.size(), 0);
    std::vector<DWORD> showFailures(targets.size(), 0);
    std::vector<DWORD> placementFailures(targets.size(), 0);
    const auto showOne = [&](std::size_t item) {
        const Planned &planned = targets.at(item);
        const HWND hwnd = planned.hwnd;
        ShowWindow(hwnd, planned.minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
        if (IsWindowVisible(hwnd) == 0) {
            stillHidden.at(item) = 1;
            showFailures.at(item) = GetLastError();
            return;
        }
        // 兜底：`showCmd` 变了（最大化 / 最小化丢了）就拿隐藏前的记录摆回去。
        // 只修“状态”，不碰位置 —— 位置 Windows 自己保持得比我们好。
        if (!planned.hasSaved
            || (planned.saved.showCmd != SW_SHOWMAXIMIZED
                && planned.saved.showCmd != SW_SHOWMINIMIZED)) {
            return;
        }
        WINDOWPLACEMENT current{};
        current.length = sizeof(current);
        if (GetWindowPlacement(hwnd, &current) == 0 || current.showCmd == planned.saved.showCmd) {
            return;
        }
        WINDOWPLACEMENT restore = planned.saved;
        restore.length = sizeof(restore);
        if (SetWindowPlacement(hwnd, &restore) == 0) {
            placementFailures.at(item) = GetLastError();
        }
    };
    std::vector<std::size_t> parallel;
    if (targets.size() > 1) {
        parallel.reserve(targets.size());
    }
    for (std::size_t item = 0; item < targets.size(); ++item) {
        if (ownedByCallingThread(targets.at(item).hwnd)) {
            showOne(item);
        } else {
            parallel.push_back(item);
        }
    }
    runConcurrently(parallel, showOne);
    dwm::setTransitionsDisabled(targetHandles, false);

    for (std::size_t item = 0; item < targets.size(); ++item) {
        const std::size_t slot = indexes.at(item);
        const HWND hwnd = targets.at(item).hwnd;
        if (stillHidden.at(item) != 0) {
            // 失败的那条**留着**兜底记录：下次 / 手动恢复再试。
            results.at(slot) = BatchResult{false,
                                           QStringLiteral("ShowWindow(SW_SHOWNOACTIVATE) "
                                                          "failed: %1")
                                               .arg(winErrorMessage(showFailures.at(item)))};
            continue;
        }
        savedPlacements().erase(hwnd);
        if (placementFailures.at(item) != 0) {
            logWarn(QStringLiteral("could not restore the placement of %1: %2")
                        .arg(core::rustDebug(window::windowTitle(hwnd)),
                             winErrorMessage(placementFailures.at(item))));
        }
        results.at(slot) = BatchResult{true, QString()};
    }
    return results;
}

bool show(HWND hwnd, HWND insertAfter, QString *error)
{
    const std::vector<BatchResult> results = showMany({hwnd});
    const BatchResult &result = results.front();
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
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
    // 搬完之后的收尾可能改显隐（藏着的窗口被 `SetWindowPlacement` 露出来再
    // `SW_HIDE` 回去；最大化的窗口先 `SW_RESTORE` 再重新最大化）——都不要动画：
    // 工作空间后端碰显隐一律“直接一下就到位”。
    dwm::TransitionGuard guard(hwnd, false);

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

// ---------------------------------------------------------------------------
// 恢复日志（M8）：读 / 写 / 把窗口放回来
// ---------------------------------------------------------------------------

core::WorkspaceLog loadWorkspaceLog(const QString &configPath, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    const QString path = core::workspaceLogPath(configPath);
    QFile file(path);
    if (!file.exists()) {
        // 没有文件 = 没有窗口被藏着（正常情况下我们恢复完就把它删掉）。
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        logWarn(QStringLiteral("workspace: cannot read the recovery log %1: %2")
                    .arg(QDir::toNativeSeparators(path), file.errorString()));
        return {};
    }
    const QByteArray json = file.readAll();
    file.close();
    QString parseError;
    const core::WorkspaceLog log = core::parseWorkspaceLog(json, &parseError);
    if (!parseError.isEmpty()) {
        // 坏文件只记一条 warning，能读出来的条目照旧用：这个文件坏掉时最合理的
        // 反应是尽量把窗口放回来，而不是让它们永远藏着。
        logWarn(QStringLiteral("workspace: the recovery log %1 is damaged (%2); using what could be read")
                    .arg(QDir::toNativeSeparators(path), parseError));
        if (error != nullptr) {
            *error = parseError;
        }
    }
    return log;
}

bool saveWorkspaceLog(const QString &configPath, const core::WorkspaceLog &log, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    const QString path = core::workspaceLogPath(configPath);
    if (log.isEmpty()) {
        // 没有藏着任何窗口：删掉文件，不留一个空壳。删不掉（被占用）就往下走，
        // 写一份空日志覆盖旧内容 —— 留着过时的条目比多一个文件危险得多。
        if (!QFile::exists(path) || QFile::remove(path)) {
            return true;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray json = core::serializeWorkspaceLog(log);
    if (file.write(json) != json.size()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

RestoreResult restoreHiddenWindows(const core::WorkspaceLog &log)
{
    RestoreResult result;
    result.report.entries = static_cast<int>(log.windows.size());
    // 先认一遍窗口，再一次**并行**全部露出来（`showMany()`）：恢复常常跑在注销 /
    // 关机的关键路径上，逐个 `ShowWindow` 会按 App 的响应速度排队（同 `hideMany()`）。
    std::vector<std::size_t> indexes;
    std::vector<HWND> targets;
    indexes.reserve(log.windows.size());
    targets.reserve(log.windows.size());
    for (std::size_t index = 0; index < log.windows.size(); ++index) {
        const core::HiddenWindow &entry = log.windows.at(index);
        const HWND hwnd = handleOf(entry.window);
        if (!isAlive(hwnd)) {
            ++result.report.dropped;
            logDebug(QStringLiteral("workspace: dropping %1 from the recovery log: the window "
                                    "is gone")
                         .arg(core::rustDebug(entry.title)));
            continue;
        }
        // `HWND` 会被复用：身份对不上就说明这个句柄现在是别人的窗口，不能去动它。
        if (entry.identity.valid && !stillSame(hwnd, entry.identity)) {
            ++result.report.dropped;
            logWarn(QStringLiteral("workspace: dropping %1 from the recovery log: the handle "
                                   "now belongs to another window")
                        .arg(core::rustDebug(entry.title)));
            continue;
        }
        const HWND target = manageTarget(hwnd);
        if (target == nullptr) {
            ++result.report.dropped;
            continue;
        }
        // 目标进程卡死时不碰它（`showMany()` 会给出“the window is not responding”）。
        indexes.push_back(index);
        targets.push_back(target);
    }
    const std::vector<BatchResult> results = showMany(targets);
    for (std::size_t item = 0; item < targets.size(); ++item) {
        const core::HiddenWindow &entry = log.windows.at(indexes.at(item));
        if (!results.at(item).ok) {
            ++result.report.failed;
            result.remaining.windows.push_back(entry);
            logWarn(QStringLiteral("workspace: cannot restore %1: %2")
                        .arg(core::rustDebug(entry.title), results.at(item).error));
            continue;
        }
        ++result.report.restored;
        logDebug(QStringLiteral("workspace: restored %1 from the recovery log")
                     .arg(core::rustDebug(entry.title)));
    }
    return result;
}

RestoreResult restoreFromLog(const QString &configPath)
{
    const QString path = core::workspaceLogPath(configPath);
    const core::WorkspaceLog log = loadWorkspaceLog(configPath);
    if (log.isEmpty()) {
        // 没有文件 / 空文件 / 坏文件：没有可恢复的东西。留下来只会让每次启动都
        // 报一条 warning，所以顺手清掉（这个文件是我们自己写的，内容已经没用了）。
        if (QFileInfo::exists(path)) {
            QFile::remove(path);
        }
        return {};
    }
    RestoreResult result = restoreHiddenWindows(log);
    // 剩下的写回：恢复成功的 / 已经没了的条目都要出日志，否则下次启动会白试。
    const bool changed = result.remaining.windows.size() != log.windows.size();
    if (changed) {
        QString error;
        if (!saveWorkspaceLog(configPath, result.remaining, &error)) {
            logWarn(QStringLiteral("workspace: cannot update the recovery log: %1").arg(error));
        }
    }
    if (result.report.restored > 0 || result.report.dropped > 0) {
        logInfo(QStringLiteral("workspace: restored %1 window(s) from the recovery log "
                               "(%2 gone, %3 still failing)")
                    .arg(result.report.restored)
                    .arg(result.report.dropped)
                    .arg(result.report.failed));
    }
    return result;
}

} // namespace flowkeyd::platform::win::workspace
