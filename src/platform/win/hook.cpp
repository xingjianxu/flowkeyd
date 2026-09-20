#include "platform/win/hook.h"

#include "platform/win/input.h"
#include "platform/win/logging.h"

#include <QStringList>

#include <future>
#include <utility>

namespace flowkeyd::platform::win {

namespace {

/// 引擎滴答使用的定时器 id。
constexpr UINT_PTR kTimerTick = 1;

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

} // namespace

HookThread::~HookThread()
{
    stop();
}

bool HookThread::start(std::shared_ptr<const core::Compiled> config, TriggerSink sink, QString *error)
{
    if (m_thread.joinable()) {
        if (error != nullptr) {
            *error = QStringLiteral("the hook thread is already running");
        }
        return false;
    }
    m_initialConfig = std::move(config);
    m_sink = std::move(sink);
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

    s_active.store(this);
    SetTimer(nullptr, kTimerTick, m_initialConfig->settings.tickMs, nullptr);
    m_running.store(true);
    ready.set_value(QString());

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.wParam == kTimerTick) {
            handleTick();
        } else if (message.message == kControlMessage) {
            handleControlCommand(static_cast<ControlCmd>(message.wParam));
        }
    }

    m_running.store(false);
    KillTimer(nullptr, kTimerTick);
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
    event.injected = (info->flags & LLKHF_INJECTED) != 0;

    m_keyEvents.fetch_add(1);
    const core::Reaction reaction = m_engine->onKey(event, monotonicMs());
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
