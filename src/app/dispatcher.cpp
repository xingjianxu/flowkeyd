#include "app/dispatcher.h"

#include "app/runtime.h"
#include "core/keys.h"
#include "core/template.h"
#include "platform/win/input.h"
#include "platform/win/hook.h"
#include "platform/win/logging.h"
#include "platform/win/process.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>

#include <utility>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

namespace {

/// 模板里 `{date}` / `{time}` 用的是本地墙上时钟。
core::LocalTime localTimeNow()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    core::LocalTime time;
    time.year = now.wYear;
    time.month = now.wMonth;
    time.day = now.wDay;
    time.hour = now.wHour;
    time.minute = now.wMinute;
    time.second = now.wSecond;
    return time;
}

core::Vars makeVars(Runtime *runtime, const QString &hotkey)
{
    core::Vars vars;
    vars.hotkey = hotkey;
    vars.configDir = QFileInfo(runtime->configPath()).absolutePath();
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (!exeDir.isEmpty()) {
        vars.exeDir = exeDir;
    }
    const QString profile = qEnvironmentVariable("USERPROFILE");
    if (!profile.isEmpty()) {
        vars.userProfile = profile;
    }
    vars.localTime = localTimeNow();
    return vars;
}

} // namespace

Dispatcher::Dispatcher(Runtime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

void Dispatcher::submit(std::shared_ptr<const core::Compiled> config, core::Trigger trigger)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), trigger]() { execute(config, trigger); },
        Qt::QueuedConnection);
}

void Dispatcher::execute(const std::shared_ptr<const core::Compiled> &config,
                         const core::Trigger &trigger)
{
    if (!config || trigger.index >= config->bindings.size()) {
        return;
    }
    const core::Binding &binding = config->bindings[trigger.index];
    const std::vector<core::Action> &actions =
        trigger.phase == core::Phase::Press ? binding.press : binding.release;
    if (actions.empty()) {
        return;
    }
    for (const core::Action &action : actions) {
        executeAction(*config, binding.name, action);
    }
}

void Dispatcher::executeAction(const core::Compiled &config, const QString &hotkey,
                               const core::Action &action)
{
    core::Vars vars = makeVars(m_runtime, hotkey);
    switch (action.kind) {
    case core::Action::Kind::Noop:
        break;
    case core::Action::Kind::Send: {
        const QString script = core::expand(action.keys, vars);
        if (action.delayMs.has_value()) {
            QThread::msleep(static_cast<unsigned long>(*action.delayMs));
        }
        QVector<core::SendOp> ops;
        if (const auto error = core::parseKeyOrScript(script, &ops); error.has_value()) {
            win::logError(QStringLiteral("`%1` send %2: %3")
                              .arg(hotkey, core::rustDebug(action.keys), error->message()));
            return;
        }
        const bool release =
            action.releaseModifiers.value_or(config.settings.releaseModifiers);
        win::ModifierGuard guard =
            release ? win::ModifierGuard::release() : win::ModifierGuard::none();
        QString error;
        const bool ok = win::sendOps(ops, true, &error);
        guard.restore();
        if (!ok) {
            win::logError(QStringLiteral("`%1` send failed: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2 (%3 step(s))")
                             .arg(hotkey, core::rustDebug(script))
                             .arg(ops.size()));
        }
        break;
    }
    case core::Action::Kind::Type: {
        if (action.delayMs.has_value()) {
            QThread::msleep(static_cast<unsigned long>(*action.delayMs));
        }
        const QString text = core::expand(action.text, vars);
        QVector<core::SendOp> ops;
        ops.reserve(text.size());
        for (int i = 0; i < text.size(); ++i) {
            ops.append(core::SendOp::unicode(text.at(i).unicode()));
        }
        const bool release =
            action.releaseModifiers.value_or(config.settings.releaseModifiers);
        win::ModifierGuard guard =
            release ? win::ModifierGuard::release() : win::ModifierGuard::none();
        QString error;
        const bool ok = win::sendOps(ops, true, &error);
        guard.restore();
        if (!ok) {
            win::logError(QStringLiteral("`%1` type failed: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> typed %2 char(s)").arg(hotkey).arg(text.size()));
        }
        break;
    }
    case core::Action::Kind::CapsLock: {
        if (win::capsLockOn()) {
            QString error;
            if (!win::tapKey(core::vk::CAPITAL, &error)) {
                win::logWarn(QStringLiteral("`%1` caps_lock failed: %2").arg(hotkey, error));
            } else {
                win::logInfo(QStringLiteral("`%1` -> caps lock off").arg(hotkey));
            }
        } else {
            win::logDebug(QStringLiteral("`%1` caps lock already off").arg(hotkey));
        }
        break;
    }
    case core::Action::Kind::Open: {
        const QString target = core::expand(action.target.value_or(QString()), vars);
        const std::optional<QString> args =
            action.openArgs.has_value() ? std::optional<QString>(core::expand(*action.openArgs, vars))
                                        : std::nullopt;
        const std::optional<QString> cwd =
            action.cwd.has_value() ? std::optional<QString>(core::expand(*action.cwd, vars))
                                   : std::nullopt;
        QString error;
        if (!win::openTarget(target, args, cwd, action.show, &error)) {
            win::logError(QStringLiteral("`%1` open: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> opened %2").arg(hotkey, target));
        }
        break;
    }
    case core::Action::Kind::Run: {
        win::RunCommandSpec spec;
        spec.program = core::expand(action.program, vars);
        for (const QString &arg : action.args) {
            spec.args.append(core::expand(arg, vars));
        }
        if (action.cwd.has_value()) {
            spec.cwd = core::expand(*action.cwd, vars);
        }
        spec.show = action.show;
        spec.shell = action.shell;
        spec.wait = action.wait;
        for (auto it = action.env.constBegin(); it != action.env.constEnd(); ++it) {
            spec.env.insert(core::expand(it.key(), vars), core::expand(it.value(), vars));
        }
        QString detail;
        QString error;
        if (!win::runCommand(spec, &detail, &error)) {
            win::logError(QStringLiteral("`%1` run: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> run %2 (%3)").arg(hotkey, spec.program, detail));
        }
        break;
    }
    case core::Action::Kind::Notify: {
        const QString title = core::expand(action.title, vars);
        const QString body = core::expand(action.body.value_or(QString()), vars);
        win::logInfo(QStringLiteral("`%1` -> notify %2").arg(hotkey, core::rustDebug(title)));
        m_runtime->notifyFromAnyThread(title, body);
        break;
    }
    case core::Action::Kind::Suspend: {
        win::ControlCmd cmd = win::ControlCmd::ToggleSuspend;
        bool target = false;
        switch (action.toggleState) {
        case core::ToggleState::On:
            cmd = win::ControlCmd::Suspend;
            target = true;
            break;
        case core::ToggleState::Off:
            cmd = win::ControlCmd::Resume;
            target = false;
            break;
        case core::ToggleState::Toggle:
            target = !m_runtime->isSuspended();
            break;
        }
        m_runtime->postControl(cmd);
        emit suspendedChanged(target);
        win::logInfo(QStringLiteral("`%1` -> suspend (%2)").arg(hotkey).arg(target ? "on" : "off"));
        break;
    }
    case core::Action::Kind::Reload:
        win::logInfo(QStringLiteral("`%1` -> reload").arg(hotkey));
        m_runtime->reloadFromAnyThread();
        break;
    case core::Action::Kind::Quit:
        win::logInfo(QStringLiteral("`%1` -> quit").arg(hotkey));
        m_runtime->requestShutdownFromAnyThread();
        break;
    default:
        // volume / media / clipboard / window / desktop / menu / help / power
        // 是后续阶段的后端，这里先明确说一声，而不是静默地什么都不做。
        win::logWarn(QStringLiteral("`%1`: action `%2` is not implemented yet (later stage)")
                         .arg(hotkey, action.summary()));
        break;
    }
}

} // namespace flowkeyd::app
