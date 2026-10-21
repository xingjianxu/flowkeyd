#include "app/runtime.h"

#include "app/dispatcher.h"
#include "lua/lua_config.h"
#include "platform/win/ffi.h"
#include "platform/win/hook.h"
#include "platform/win/logging.h"
#include "platform/win/single_instance.h"

#include <QMetaObject>
#include <QThread>
#include <QVector>
#include <QWinEventNotifier>

#include <utility>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

Runtime::Runtime(QObject *parent)
    : QObject(parent)
{
}

Runtime::~Runtime()
{
    shutdown();
}

bool Runtime::start(const QString &configPath,
                    std::shared_ptr<const core::Compiled> config,
                    QString *error)
{
    if (m_started) {
        if (error != nullptr) {
            *error = QStringLiteral("the runtime is already started");
        }
        return false;
    }
    m_configPath = configPath;

    m_hook = std::make_unique<win::HookThread>();

    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("flowkeyd-actions"));
    m_dispatcher = new Dispatcher(this);
    m_dispatcher->moveToThread(m_workerThread);
    m_workerThread->start();

    const std::shared_ptr<const core::Compiled> startupConfig = config;
    const bool started = m_hook->start(
        std::move(config),
        [this](const std::shared_ptr<const core::Compiled> &current, const core::Trigger &trigger) {
            if (m_dispatcher != nullptr) {
                m_dispatcher->submit(current, trigger);
            }
        },
        [this](const std::shared_ptr<const core::Compiled> &current,
               const win::PlacementEvent &event) {
            if (m_dispatcher != nullptr) {
                m_dispatcher->submitPlacement(current, event);
            }
        },
        [this](const std::shared_ptr<const core::Compiled> &current,
               const core::WorkspaceEvent &event) {
            if (m_dispatcher != nullptr) {
                m_dispatcher->submitWorkspace(current, event);
            }
        },
        m_workspaceEvents,
        error);
    if (!started) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_dispatcher;
        m_dispatcher = nullptr;
        m_hook.reset();
        return false;
    }
    m_started = true;

    // 托盘的虚拟桌面号：动作线程上每 500 ms 查一次，变了就投回 GUI 线程换图标
    // （`startDesktopWatch` 必须在它自己那条线程上建定时器，所以用排队调用）。
    QMetaObject::invokeMethod(m_dispatcher, &Dispatcher::startDesktopWatch, Qt::QueuedConnection);

    // 程序启动器的目录：启动时先扫一遍（工作线程，真机上约 80 ms），这样第一次
    // 按 Win+Space 不必现场等扫描。
    QMetaObject::invokeMethod(m_dispatcher, &Dispatcher::startAppScan, Qt::QueuedConnection);

    // 启动时按 `window_rule` 把已有窗口归位一次（用户要求）。工作线程已经起来，
    // 所以这里只是投一个队列任务。
    if (m_dispatcher != nullptr && m_hook != nullptr) {
        win::PlacementEvent event;
        event.kind = win::PlacementEvent::Kind::Startup;
        m_dispatcher->submitPlacement(startupConfig, event);
        // 工作空间的「收编」排在它后面：规则真的把窗口搬了屏之后，归属才按**实际**
        // 所在的那块屏来算。窗口列表由工作线程自己枚举（钩子线程不枚举窗口）。
        core::WorkspaceEvent workspaceEvent;
        workspaceEvent.kind = core::WorkspaceEvent::Kind::Startup;
        m_dispatcher->submitWorkspace(startupConfig, workspaceEvent);
    }

    // `--quit` 通道：另一个进程（install.ps1）用它请我们走干净退出路径，
    // 而不是 `taskkill /F`。事件在 GUI 线程上监听，所以退出走的还是
    // 托盘「退出」那条 `performShutdown()`。
    QString quitError;
    m_quitEvent = win::createQuitEvent(win::instanceKey(configPath), &quitError);
    if (m_quitEvent == nullptr) {
        win::logWarn(QStringLiteral("no quit channel (%1); `--quit` cannot reach this instance")
                         .arg(quitError));
    } else {
        auto *notifier = new QWinEventNotifier(static_cast<HANDLE>(m_quitEvent), this);
        connect(notifier, &QWinEventNotifier::activated, this, [this]() {
            win::logInfo(QStringLiteral("quit requested by another process"));
            requestShutdownFromAnyThread();
        });
        m_quitNotifier = notifier;
    }

    // `--restore-windows` 通道（M8）：另一个进程请我们把被工作空间藏起来的窗口
    // 全部放回来（没有实例在跑时，那个进程会自己按恢复日志做）。
    QString restoreError;
    m_restoreEvent = win::createRestoreEvent(win::instanceKey(configPath), &restoreError);
    if (m_restoreEvent == nullptr) {
        win::logWarn(QStringLiteral("no restore channel (%1); `--restore-windows` cannot reach "
                                    "this instance")
                         .arg(restoreError));
    } else {
        auto *notifier = new QWinEventNotifier(static_cast<HANDLE>(m_restoreEvent), this);
        connect(notifier, &QWinEventNotifier::activated, this, [this]() {
            win::logInfo(QStringLiteral("restore requested by another process"));
            requestWorkspaceRestore();
        });
        m_restoreNotifier = notifier;
    }
    return true;
}

void Runtime::shutdown()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    // 先把弹窗关掉：`onChoose` 会回投到 `Dispatcher`，留着未完成的回调就是
    // 在给“点了已销毁的对象”找机会（见 popup_host.h）。
    if (m_popupHost != nullptr) {
        m_popupHost->closeAll();
    }
    if (m_hook) {
        // 先卸钩子，之后不会再有任何动作被排进队列。
        m_hook->stop();
        m_hook.reset();
    }
    if (m_dispatcher != nullptr) {
        // 退出前把被工作空间藏起来的窗口**全部放回来**（M8）：不先停钩子的话，
        // 一次“切换工作空间”会跟在恢复后面再次藏掉窗口。有上限 —— 动作线程可能
        // 卡在某个长动作里，那时日志会先留着，下次启动再恢复。
        m_dispatcher->restoreWorkspaceNow(3000);
    }
    if (m_workerThread != nullptr) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    if (m_dispatcher != nullptr) {
        delete m_dispatcher;
        m_dispatcher = nullptr;
    }
    // 事件句柄先关：`quitEventExists()` 把「对象消失」当作「这个实例退干净了」。
    if (m_quitNotifier != nullptr) {
        m_quitNotifier->setEnabled(false);
        delete m_quitNotifier;
        m_quitNotifier = nullptr;
    }
    if (m_quitEvent != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_quitEvent));
        m_quitEvent = nullptr;
    }
    if (m_restoreNotifier != nullptr) {
        m_restoreNotifier->setEnabled(false);
        delete m_restoreNotifier;
        m_restoreNotifier = nullptr;
    }
    if (m_restoreEvent != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_restoreEvent));
        m_restoreEvent = nullptr;
    }
}

bool Runtime::isSuspended() const
{
    return m_hook != nullptr && m_hook->isSuspended();
}

void Runtime::postControl(win::ControlCmd cmd)
{
    if (m_hook != nullptr) {
        m_hook->postControl(cmd);
    }
}

void Runtime::reloadFromAnyThread()
{
    if (m_hook == nullptr) {
        return;
    }
    core::Compiled compiled;
    if (const auto error = core::loadConfig(lua::makeLuaEvaluator(), m_configPath, &compiled);
        error.has_value()) {
        win::logError(QStringLiteral("reload failed, keeping the previous configuration: %1")
                          .arg(error->oneLine()));
        return;
    }
    for (const QString &warning : compiled.warnings) {
        win::logWarn(warning);
    }
    if (const auto level = win::parseLogLevel(compiled.settings.logLevel); level.has_value()) {
        win::setLogLevel(*level);
    }
    const QString summary = QStringLiteral("%1 hotkey(s), %2 remap(s)")
                                .arg(compiled.bindings.size())
                                .arg(compiled.remaps.size());
    auto shared = std::make_shared<core::Compiled>(std::move(compiled));
    m_hook->replaceConfig(shared);
    win::logInfo(QStringLiteral("reloaded %1 (%2)").arg(m_configPath, summary));
    emit reloadFinished(summary);
}

void Runtime::requestShutdownFromAnyThread()
{
    QMetaObject::invokeMethod(this, &Runtime::performShutdown, Qt::QueuedConnection);
}

void Runtime::requestWorkspaceRestore()
{
    if (m_dispatcher != nullptr) {
        m_dispatcher->submitWorkspaceRestore();
    }
}

bool Runtime::restoreWorkspaceNow(int timeoutMs)
{
    if (m_dispatcher == nullptr) {
        return true;
    }
    return m_dispatcher->restoreWorkspaceNow(timeoutMs);
}

void Runtime::notifyFromAnyThread(const QString &title, const QString &body)
{
    emit notificationRequested(title, body);
}

void Runtime::reportSuspended(bool suspended)
{
    // 这个方法是工作线程调用的；把 emit 挪到 GUI 线程，托盘那条直接连接
    // 就会在正确的线程上跑。
    QMetaObject::invokeMethod(
        this, [this, suspended]() { emit suspendedChanged(suspended); }, Qt::QueuedConnection);
}

void Runtime::reportDesktop(int number, int count)
{
    // 与 `reportSuspended` 同一个道理：工作线程只负责“说话”，换图标由 GUI 线程做。
    QMetaObject::invokeMethod(
        this,
        [this, number, count]() { emit desktopChanged(number, count); },
        Qt::QueuedConnection);
}

void Runtime::showMenuFromAnyThread(MenuRequest request)
{
    if (m_popupHost != nullptr) {
        m_popupHost->requestMenu(std::move(request));
    }
}

void Runtime::showHelpFromAnyThread(HelpRequest request)
{
    if (m_popupHost != nullptr) {
        m_popupHost->requestHelp(std::move(request));
    }
}

void Runtime::showSwitchFromAnyThread(SwitchRequest request)
{
    if (m_popupHost != nullptr) {
        m_popupHost->requestSwitch(std::move(request));
    }
}

void Runtime::showAppsFromAnyThread(AppRequest request)
{
    if (m_popupHost != nullptr) {
        m_popupHost->requestApps(std::move(request));
    }
}

void Runtime::performShutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    shutdown();
    emit finished();
}

} // namespace flowkeyd::app
