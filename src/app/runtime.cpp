#include "app/runtime.h"

#include "app/dispatcher.h"
#include "lua/lua_config.h"
#include "platform/win/hook.h"
#include "platform/win/logging.h"

#include <QMetaObject>
#include <QThread>
#include <QVector>

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
    connect(m_dispatcher, &Dispatcher::suspendedChanged, this, &Runtime::suspendedChanged);
    m_workerThread->start();

    const bool started = m_hook->start(
        std::move(config),
        [this](const std::shared_ptr<const core::Compiled> &current, const core::Trigger &trigger) {
            if (m_dispatcher != nullptr) {
                m_dispatcher->submit(current, trigger);
            }
        },
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
    return true;
}

void Runtime::shutdown()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    if (m_hook) {
        // 先卸钩子，之后不会再有任何动作被排进队列。
        m_hook->stop();
        m_hook.reset();
    }
    if (m_workerThread != nullptr) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    if (m_dispatcher != nullptr) {
        delete m_dispatcher;
        m_dispatcher = nullptr;
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

void Runtime::notifyFromAnyThread(const QString &title, const QString &body)
{
    emit notificationRequested(title, body);
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
