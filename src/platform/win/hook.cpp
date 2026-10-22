#include "platform/win/hook.h"

#include "core/placement.h"
#include "core/remote_desktop.h"
#include "platform/win/input.h"
#include "platform/win/logging.h"
#include "platform/win/monitor.h"
#include "platform/win/window.h"

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
    const bool enabled = !m_engine->config()->windowRules.empty() && !m_suspended.load();
    if (!enabled) {
        m_pendingWindows.clear();
    } else if (!m_pendingWindows.empty()) {
        logDebug(QStringLiteral("placement tick: flushing %1 window(s)").arg(m_pendingWindows.size()));
        PlacementEvent event;
        event.kind = PlacementEvent::Kind::WindowsShown;
        event.windows = std::move(m_pendingWindows);
        m_pendingWindows.clear();
        if (m_placementSink) {
            m_placementSink(m_engine->config(), event);
        }
    }

    // 显示器基线无论有没有规则都要跟：否则刚 reload 上一条规则就会把“当前这批
    // 显示器”误报成刚刚接入。
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
    const QStringList added = core::newMonitorDevices(m_monitorDevices, devices);
    m_monitorDevices = devices;
    if (added.isEmpty() || !enabled) {
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
