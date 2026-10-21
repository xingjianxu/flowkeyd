#include "platform/win/hook.h"

#include "core/placement.h"
#include "core/remote_desktop.h"
#include "platform/win/input.h"
#include "platform/win/logging.h"
#include "platform/win/monitor.h"
#include "platform/win/window.h"
#include "platform/win/workspace.h"

#include <QStringList>

#include <algorithm>
#include <future>
#include <utility>

namespace flowkeyd::platform::win {

namespace {

/// 引擎滴答使用的定时器 id。
constexpr UINT_PTR kTimerTick = 1;
/// 窗口 / 显示器监听的定时器 id（比 tick 慢得多，没必要每 15 ms 查一次）。
constexpr UINT_PTR kTimerPlacement = 2;
/// 窗口刚出现时标题/位置还没稳定，等一个 tick 再处理。
constexpr UINT kPlacementIntervalMs = 350;

/// 当前活着的钩子线程（回调运行在它上面）。一次只允许一个。
std::atomic<HookThread *> s_active{nullptr};

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    // 对自己合成输入的快速路径：这也覆盖了我们在 SendInput 内部时触发的重入回调，
    // 那时任何状态都还没被触碰。
    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lparam);
    if (info->dwExtraInfo == kSyntheticTag) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    HookThread *self = s_active.load();
    if (self == nullptr || !self->processHookEvent(wparam, info)) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    return 1;
}

/// `SetWinEventHook` 的回调：顶层窗口的“出现”与“销毁”，以及前台窗口的切换。
///
/// `WINEVENT_OUTOFCONTEXT` 的回调运行在装钩子的那个线程（钩子线程）上，
/// 所以这里只把窗口记下来，真正的摆放交给工作线程；而“在不在远程桌面里”
/// 只影响引擎，就地更新就行。
void CALLBACK winEventProc(HWINEVENTHOOK,
                           DWORD event,
                           HWND hwnd,
                           LONG idObject,
                           LONG idChild,
                           DWORD,
                           DWORD)
{
    HookThread *self = s_active.load();
    if (self == nullptr) {
        return;
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        // 没有任何前台窗口时 `hwnd` 是 0：那当然不在远程桌面里。
        self->noteForegroundWindow(hwnd);
        return;
    }
    if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) {
        // 最小化事件的 `hwnd` 就是那个窗口，`idObject` 也是 `OBJID_WINDOW`；
        // 但这两条不在 `EVENT_OBJECT_*` 那一族里，所以单独转一次。
        if (hwnd != nullptr && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
            self->noteWindowEvent(event, hwnd);
        }
        return;
    }
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }
    self->noteWindowEvent(event, hwnd);
}

} // namespace

HookThread::~HookThread()
{
    stop();
}

bool HookThread::start(std::shared_ptr<const core::Compiled> config,
                       TriggerSink sink,
                       PlacementSink placementSink,
                       WorkspaceSink workspaceSink,
                       std::shared_ptr<core::ExpectedWorkspaceEvents> expectedEvents,
                       QString *error)
{
    if (m_thread.joinable()) {
        if (error != nullptr) {
            *error = QStringLiteral("the hook thread is already running");
        }
        return false;
    }
    m_initialConfig = std::move(config);
    m_sink = std::move(sink);
    m_placementSink = std::move(placementSink);
    m_workspaceSink = std::move(workspaceSink);
    m_expectedEvents = std::move(expectedEvents);
    std::promise<QString> ready;
    std::future<QString> future = ready.get_future();
    m_thread = std::thread([this, promise = std::move(ready)]() mutable {
        threadMain(std::move(promise));
    });
    const QString message = future.get();
    if (!message.isEmpty()) {
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }
    return true;
}

void HookThread::stop()
{
    if (!m_thread.joinable()) {
        return;
    }
    const DWORD id = m_threadId.load();
    if (id != 0) {
        PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    m_thread.join();
}

void HookThread::postControl(ControlCmd cmd)
{
    const DWORD id = m_threadId.load();
    if (id != 0) {
        PostThreadMessageW(id, kControlMessage, static_cast<WPARAM>(cmd), 0);
    }
}

void HookThread::replaceConfig(std::shared_ptr<const core::Compiled> config)
{
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_pendingConfig = std::move(config);
    }
    postControl(ControlCmd::Reload);
}

void HookThread::threadMain(std::promise<QString> ready)
{
    m_threadId.store(GetCurrentThreadId());
    // `PostThreadMessage` 在目标线程还没消息队列时会静默失败，
    // 所以先 `PeekMessage` 建出队列，再告诉启动方“队列已就绪”。
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    m_engine = std::make_unique<core::Engine>(m_initialConfig);
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, moduleHandle(), 0);
    if (m_hook == nullptr) {
        const QString error = lastErrorMessage("SetWindowsHookExW(WH_KEYBOARD_LL)");
        m_engine.reset();
        m_threadId.store(0);
        ready.set_value(error);
        return;
    }
    logInfo(QStringLiteral("keyboard hook installed (WH_KEYBOARD_LL)"));
    // 窗口监听：`window_rule` 靠它发现“某个程序刚启动”。
    m_winEventHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW,
                                     nullptr, winEventProc, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (m_winEventHook == nullptr) {
        logWarn(QStringLiteral("could not watch for new windows (%1); window rules will only "
                               "run at startup and when a monitor reconnects")
                    .arg(lastErrorMessage("SetWinEventHook")));
    } else {
        logDebug(QStringLiteral("window watcher installed (EVENT_OBJECT_SHOW)"));
    }
    // 前台窗口监听：远程桌面放行靠它立刻生效（不然要等下一次 placement tick）。
    // **故意不带** `WINEVENT_SKIPOWNPROCESS`：flowkeyd 自己的弹窗拿到前台时就应该
    // 算“不在远程桌面里”（那时按的键是给我们自己的）。
    m_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                       nullptr, winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (m_foregroundHook == nullptr) {
        logWarn(QStringLiteral("could not watch foreground changes (%1); the remote desktop "
                               "check will only run when a window appears")
                    .arg(lastErrorMessage("SetWinEventHook")));
    } else {
        logDebug(QStringLiteral("foreground watcher installed (EVENT_SYSTEM_FOREGROUND)"));
    }
    if (acceptInjectedInput()) {
        logWarn(QStringLiteral(
            "FLOWKEYD_ACCEPT_INJECTED is set: input synthesized by other programs will "
            "trigger hotkeys (test mode only)"));
    }

    s_active.store(this);
    // 工作空间的两个监听（位置变化 / 最小化）只在启用时装：默认开启但没有绑定时
    // 也只是两个空回调，不隐藏任何窗口。
    updateWorkspaceHooks(workspaceEnabled());
    // `SetTimer(nullptr, …)` 会**忽略** `nIDEvent` 并返回一个新 id，`WM_TIMER`
    // 的 `wParam` 就是那个新 id；必须存返回值，否则比较永远不成立。
    m_tickTimerId = SetTimer(nullptr, kTimerTick, m_initialConfig->settings.tickMs, nullptr);
    m_placementTimerId =
        SetTimer(nullptr, kTimerPlacement, kPlacementIntervalMs, nullptr);
    logDebug(QStringLiteral("timers: tick=%1 (%2 ms), placement=%3 (%4 ms)")
                 .arg(static_cast<qulonglong>(m_tickTimerId))
                 .arg(m_initialConfig->settings.tickMs)
                 .arg(static_cast<qulonglong>(m_placementTimerId))
                 .arg(kPlacementIntervalMs));
    m_running.store(true);
    // 启动时先看一眼前台窗口：如果键盘一启动就在远程桌面客户端里，第一个按键
    // 就该放行。
    noteForegroundWindow(GetForegroundWindow());
    ready.set_value(QString());

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.wParam == m_tickTimerId) {
            handleTick();
        } else if (message.message == WM_TIMER && message.wParam == m_placementTimerId) {
            handlePlacementTick();
        } else if (message.message == kControlMessage) {
            handleControlCommand(static_cast<ControlCmd>(message.wParam));
        }
    }

    m_running.store(false);
    if (m_tickTimerId != 0) {
        KillTimer(nullptr, m_tickTimerId);
        m_tickTimerId = 0;
    }
    if (m_placementTimerId != 0) {
        KillTimer(nullptr, m_placementTimerId);
        m_placementTimerId = 0;
    }
    if (m_winEventHook != nullptr) {
        UnhookWinEvent(m_winEventHook);
        m_winEventHook = nullptr;
    }
    if (m_foregroundHook != nullptr) {
        UnhookWinEvent(m_foregroundHook);
        m_foregroundHook = nullptr;
    }
    updateWorkspaceHooks(false);
    m_pendingMoves.clear();
    if (m_hook != nullptr) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
        logInfo(QStringLiteral("keyboard hook removed"));
    }
    s_active.store(nullptr);

    // 消失之前把重映射还按着的键释放掉。
    if (m_engine) {
        injectOps(m_engine->setSuspended(true));
        m_engine.reset();
    }
    m_threadId.store(0);
}

bool HookThread::processHookEvent(WPARAM message, KBDLLHOOKSTRUCT *info)
{
    if (!m_running.load() || !m_engine) {
        return false;
    }
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    core::KeyEvent event;
    // 小键盘的 Enter 与主键盘的 Enter 共用 `VK_RETURN`，只有扩展标志能分开它们。
    event.vk = core::keyFromHook(static_cast<core::Vk>(info->vkCode),
                                 (info->flags & LLKHF_EXTENDED) != 0);
    event.down = down;
    event.injected = (info->flags & LLKHF_INJECTED) != 0 && !acceptInjectedInput();

    m_keyEvents.fetch_add(1);
    const core::Reaction reaction = m_engine->onKey(event, monotonicMs());
    if (currentLogLevel() >= LogLevel::Trace) {
        QStringList triggers;
        for (const core::Trigger &trigger : reaction.triggers) {
            triggers.append(QStringLiteral("%1:%2")
                                .arg(trigger.index)
                                .arg(trigger.phase == core::Phase::Press ? QStringLiteral("press")
                                                                          : QStringLiteral("release")));
        }
        logTrace(QStringLiteral("key %1 %2 swallow=%3 inject=%4 triggers=[%5] | %6")
                     .arg(core::nameFromKey(event.vk),
                          event.down ? QStringLiteral("down") : QStringLiteral("up"),
                          reaction.swallow ? QStringLiteral("yes") : QStringLiteral("no"))
                     .arg(reaction.inject.size())
                     .arg(triggers.join(QLatin1Char(',')))
                     .arg(m_engine->stateSummary()));
    }
    applyReaction(reaction);
    return reaction.swallow;
}

void HookThread::handleTick()
{
    if (!m_engine) {
        return;
    }
    applyReaction(m_engine->tick(monotonicMs()));
}

void HookThread::noteWindowEvent(DWORD event, HWND hwnd)
{
    if (!m_running.load() || m_suspended.load()) {
        return;
    }
    if (event == EVENT_OBJECT_DESTROY) {
        // 句柄会被系统复用，销毁时一定要忘掉它。
        m_knownWindows.erase(hwnd);
        m_pendingWindows.erase(std::remove(m_pendingWindows.begin(), m_pendingWindows.end(), hwnd),
                               m_pendingWindows.end());
        m_pendingMoves.erase(hwnd);
        emitWorkspaceEvent(core::WorkspaceEvent::Kind::WindowDestroyed, hwnd);
        return;
    }
    if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) {
        emitWorkspaceEvent(event == EVENT_SYSTEM_MINIMIZESTART
                               ? core::WorkspaceEvent::Kind::WindowMinimized
                               : core::WorkspaceEvent::Kind::WindowRestored,
                           hwnd);
        return;
    }
    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
        // 只有打开工作空间时才跟踪位置：窗口切换器与 `window_rule` 都不关心它。
        // 这里是回调，所以只做**最便宜**的过滤；真正的判断放到 placement tick 里
        // （那里会再过一遍 `isMainWindow`）。一次拖动会产生几百条这类事件，
        // 防抖靠这个集合去重，否则会把状态机淹死。
        if (!workspaceEnabled()) {
            return;
        }
        if (IsWindowVisible(hwnd) == 0 || GetWindow(hwnd, GW_OWNER) != nullptr) {
            return;
        }
        if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
            return;
        }
        m_pendingMoves.insert(hwnd);
        return;
    }
    if (event == EVENT_OBJECT_HIDE) {
        // 显隐由工作空间状态机自己记账（M0：不能拿 `IsWindowVisible` 当“在不在”），
        // 隐藏事件没有要处理的东西。
        return;
    }
    if (m_knownWindows.contains(hwnd)) {
        // 同一个窗口再次显示（从托盘还原、最小化后还原）不算“程序启动”。
        return;
    }
    // 与 `window::topLevelWindows()` 同一套前置过滤：不可见的、有属主的窗口
    // （对话框 / 工具提示 / 弹出菜单）以及工具窗口都不是 `window_rule` 想摆放的
    // “主窗口”。
    if (IsWindowVisible(hwnd) == 0 || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return;
    }
    if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return;
    }
    m_knownWindows.insert(hwnd);
    m_pendingWindows.push_back(hwnd);
    logDebug(QStringLiteral("window appeared: hwnd %1").arg(reinterpret_cast<quintptr>(hwnd)));
}

void HookThread::noteForegroundWindow(HWND hwnd)
{
    if (!m_running.load() || !m_engine) {
        return;
    }
    updateRemoteDesktop(hwnd != nullptr ? window::processName(hwnd) : std::nullopt);
    // 前台变化同时是工作空间的“最近用过”信号（状态机靠它决定切回去时聚焦谁）。
    if (hwnd != nullptr) {
        emitWorkspaceEvent(core::WorkspaceEvent::Kind::FocusChanged, hwnd);
    }
}

bool HookThread::workspaceEnabled() const
{
    return m_engine != nullptr && m_engine->config()->settings.workspace.enabled;
}

void HookThread::updateWorkspaceHooks(bool enabled)
{
    if (!enabled) {
        if (m_locationHook != nullptr) {
            UnhookWinEvent(m_locationHook);
            m_locationHook = nullptr;
        }
        if (m_minimizeHook != nullptr) {
            UnhookWinEvent(m_minimizeHook);
            m_minimizeHook = nullptr;
        }
        m_pendingMoves.clear();
        return;
    }
    if (m_locationHook == nullptr) {
        // **窄范围**：只要位置变化这一条。合并成 `HIDE..LOCATIONCHANGE` 那种大范围会
        // 把 `SELECTION` / `STATECHANGE` 之类每次 UI 变动全算进来。
        m_locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                         nullptr, winEventProc, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (m_locationHook == nullptr) {
            logWarn(QStringLiteral("could not watch window moves (%1); windows dragged to "
                                   "another monitor will keep their workspace")
                        .arg(lastErrorMessage("SetWinEventHook")));
        }
    }
    if (m_minimizeHook == nullptr) {
        m_minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
                                        nullptr, winEventProc, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (m_minimizeHook == nullptr) {
            logWarn(QStringLiteral("could not watch minimize events (%1); a minimized window "
                                   "may be focused when its workspace comes back")
                        .arg(lastErrorMessage("SetWinEventHook")));
        }
    }
    if (m_locationHook != nullptr && m_minimizeHook != nullptr) {
        logDebug(QStringLiteral("workspace watchers installed (location, minimize)"));
    }
}

void HookThread::emitWorkspaceEvent(core::WorkspaceEvent::Kind kind, HWND hwnd)
{
    if (!m_workspaceSink || !m_engine || !workspaceEnabled()) {
        return;
    }
    core::WorkspaceEvent event;
    event.kind = kind;
    event.window = workspace::idOf(hwnd);
    if (hwnd != nullptr && m_expectedEvents != nullptr
        && m_expectedEvents->consume(kind, event.window, monotonicMs())) {
        // 我们自己刚把这个窗口显出来 / 藏起来（M0：`SW_HIDE` 会额外产生一条 SHOW，
        // 分不出是谁干的）。
        return;
    }
    m_workspaceSink(m_engine->config(), event);
}

void HookThread::emitMonitorsChanged()
{
    if (!m_workspaceSink || !m_engine || !workspaceEnabled()) {
        return;
    }
    core::WorkspaceEvent event;
    event.kind = core::WorkspaceEvent::Kind::MonitorsChanged;
    event.monitors = m_monitorDevices;
    m_workspaceSink(m_engine->config(), event);
}

void HookThread::updateRemoteDesktop(const std::optional<QString> &executableName)
{
    if (!m_engine) {
        return;
    }
    const core::Settings &settings = m_engine->config()->settings;
    const bool active = settings.remoteDesktop
        && core::isRemoteDesktopProcess(executableName, settings.remoteDesktopProcesses);
    if (active == m_remoteDesktop) {
        return;
    }
    m_remoteDesktop = active;
    // 进入这个状态时要松开已经被放行的重映射按住的键（`setRemoteDesktop()`
    // 会告诉我们该注入什么），否则那些键会留在按下状态。
    injectOps(m_engine->setRemoteDesktop(active));
    if (active) {
        logInfo(QStringLiteral("remote desktop detected (%1): hotkeys and remaps pass through")
                    .arg(executableName.value_or(QStringLiteral("unknown window"))));
    } else {
        logInfo(QStringLiteral("left the remote desktop (%1): hotkeys and remaps are active again")
                    .arg(executableName.value_or(QStringLiteral("no foreground window"))));
    }
}

void HookThread::handlePlacementTick()
{
    if (!m_engine) {
        return;
    }
    // 兜底：万一 `EVENT_SYSTEM_FOREGROUND` 的监听没装上（或者漏了一次），
    // 这个 tick 也会把状态纠回来。每 350 ms 一次 `OpenProcess`，不值得省。
    noteForegroundWindow(GetForegroundWindow());
    const bool live = !m_suspended.load();
    const bool workspaceOn = live && m_engine->config()->settings.workspace.enabled;
    const bool placementOn = live && !m_engine->config()->windowRules.empty();

    // 防抖后的「位置变了」（`EVENT_OBJECT_LOCATIONCHANGE`）：拖动一次会有几百条
    // 事件，这里每个窗口每个周期只报一条。拿不到主窗口判据的直接丢掉（拖不动
    // 也就没什么可归的）。
    if (!workspaceOn) {
        m_pendingMoves.clear();
    } else if (!m_pendingMoves.empty()) {
        std::unordered_set<HWND> moves;
        moves.swap(m_pendingMoves);
        for (HWND hwnd : moves) {
            if (window::isMainWindow(hwnd)) {
                emitWorkspaceEvent(core::WorkspaceEvent::Kind::WindowMoved, hwnd);
            }
        }
    }

    if (!placementOn && !workspaceOn) {
        m_pendingWindows.clear();
    } else if (!m_pendingWindows.empty()) {
        std::vector<HWND> shown;
        shown.swap(m_pendingWindows);
        // 先让工作空间把它收编（归入那块屏的活动空间），再交给 `window_rule`
        // 摆位置 —— 规则真的把窗口搬了屏时，M10 会把归属改过去。
        if (workspaceOn) {
            for (HWND hwnd : shown) {
                emitWorkspaceEvent(core::WorkspaceEvent::Kind::WindowAppeared, hwnd);
            }
        }
        if (placementOn) {
            logDebug(QStringLiteral("placement tick: flushing %1 window(s)")
                         .arg(shown.size()));
            PlacementEvent event;
            event.kind = PlacementEvent::Kind::WindowsShown;
            event.windows = std::move(shown);
            if (m_placementSink) {
                m_placementSink(m_engine->config(), event);
            }
        }
    }

    // 显示器基线无论有没有规则都要跟：否则刚 reload 上一条规则就会把“当前这批
    // 显示器”误报成刚刚接入。
    //
    // 比的是**规范化后的设备名集合**，不是几何：分辘率 / 缩放 / 排列位置变化都只改
    // `bounds`/`work`，设备名不变 → 不会误报成「拔出 / 接入」。工作空间的归属键也
    // 就是设备名（`core::MonitorKey`），所以那些变化不会动到空间划分。
    const std::vector<core::MonitorDescription> monitors = core::sortedMonitors(monitor::list());
    QStringList devices;
    devices.reserve(static_cast<int>(monitors.size()));
    for (const core::MonitorDescription &description : monitors) {
        devices.append(core::normalizeDeviceName(description.device));
    }
    if (!m_monitorBaseline) {
        m_monitorBaseline = true;
        m_monitorDevices = devices;
        return;
    }
    if (devices == m_monitorDevices) {
        return;
    }
    const QStringList added = core::newMonitorDevices(m_monitorDevices, devices);
    QStringList removed;
    for (const QString &device : m_monitorDevices) {
        if (!devices.contains(device)) {
            removed.append(device);
        }
    }
    m_monitorDevices = devices;
    // 拔出与接入合成一条事件（状态机的 `monitorsChanged(present)` 就是这么收的：
    // 它自己记着上一次的集合，拆成两条反而要它自己再记一份基线）。
    if (workspaceOn) {
        if (!removed.isEmpty()) {
            logInfo(QStringLiteral("monitor disconnected: %1")
                        .arg(removed.join(QStringLiteral(", "))));
        }
        emitMonitorsChanged();
    }
    if (added.isEmpty() || !placementOn) {
        return;
    }
    logInfo(QStringLiteral("monitor connected: %1; re-applying window rules")
                .arg(added.join(QStringLiteral(", "))));
    PlacementEvent event;
    event.kind = PlacementEvent::Kind::MonitorConnected;
    if (m_placementSink) {
        m_placementSink(m_engine->config(), event);
    }
}

void HookThread::applyReaction(const core::Reaction &reaction)
{
    if (!reaction.inject.empty()) {
        logDebug(QStringLiteral("injecting %1 keystroke(s) inline").arg(reaction.inject.size()));
        QString error;
        if (!sendOps(reaction.inject, false, &error)) {
            logWarn(QStringLiteral("inline injection failed: %1").arg(error));
        }
    }
    for (const core::Trigger &trigger : reaction.triggers) {
        m_actions.fetch_add(1);
        if (m_sink && m_engine) {
            m_sink(m_engine->config(), trigger);
        }
    }
}

void HookThread::injectOps(std::vector<core::SendOp> ops)
{
    if (ops.empty()) {
        return;
    }
    QString error;
    if (!sendOps(QVector<core::SendOp>(ops.begin(), ops.end()), true, &error)) {
        logWarn(QStringLiteral("cleanup injection failed: %1").arg(error));
    }
}

void HookThread::handleControlCommand(ControlCmd cmd)
{
    if (!m_engine) {
        return;
    }
    switch (cmd) {
    case ControlCmd::Quit:
        logInfo(QStringLiteral("shutting down after %1 key event(s) and %2 action(s)")
                    .arg(m_keyEvents.load())
                    .arg(m_actions.load()));
        PostQuitMessage(0);
        break;
    case ControlCmd::Reload: {
        std::shared_ptr<const core::Compiled> pending;
        {
            std::lock_guard<std::mutex> lock(m_configMutex);
            pending = std::move(m_pendingConfig);
            m_pendingConfig.reset();
        }
        if (!pending) {
            return;
        }
        for (const QString &warning : pending->warnings) {
            logWarn(warning);
        }
        injectOps(m_engine->setConfig(pending));
        m_suspended.store(m_engine->isSuspended());
        // 新配置可能改了工作空间的开关：监听跟着装/卸。
        updateWorkspaceHooks(workspaceEnabled());
        if (workspaceEnabled()) {
            // 刚被打开（或换了 `count`）：让工作线程重新收编一遍已有窗口 —— 收编是
            // 幂等的（已记录的窗口跳过），而换 `count` 时工作线程自己会 `reconfigure`。
            emitWorkspaceEvent(core::WorkspaceEvent::Kind::Startup);
        }
        // 新配置可能改了远程桌面的开关或进程名单，重新判一次。
        noteForegroundWindow(GetForegroundWindow());
        logInfo(QStringLiteral("reloaded %1 (%2 hotkey(s), %3 remap(s))")
                    .arg(pending->source)
                    .arg(pending->bindings.size())
                    .arg(pending->remaps.size()));
        break;
    }
    case ControlCmd::Suspend: {
        injectOps(m_engine->setSuspended(true));
        m_suspended.store(true);
        logWarn(QStringLiteral("hotkeys suspended (the suspend hotkey still works)"));
        break;
    }
    case ControlCmd::Resume:
        m_engine->setSuspended(false);
        m_suspended.store(false);
        logInfo(QStringLiteral("hotkeys resumed"));
        break;
    case ControlCmd::ToggleSuspend: {
        const bool target = !m_engine->isSuspended();
        injectOps(m_engine->setSuspended(target));
        m_suspended.store(target);
        if (target) {
            logWarn(QStringLiteral("hotkeys suspended"));
        } else {
            logInfo(QStringLiteral("hotkeys resumed"));
        }
        break;
    }
    case ControlCmd::Status:
        logInfo(QStringLiteral("status: %1").arg(m_engine->stateSummary()));
        break;
    }
}

} // namespace flowkeyd::platform::win
