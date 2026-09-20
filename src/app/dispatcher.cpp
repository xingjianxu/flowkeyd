#include "app/dispatcher.h"

#include "app/runtime.h"
#include "core/keys.h"
#include "core/template.h"
#include "core/window_match.h"
#include "platform/win/audio.h"
#include "platform/win/clipboard.h"
#include "platform/win/hook.h"
#include "platform/win/input.h"
#include "platform/win/logging.h"
#include "platform/win/process.h"
#include "platform/win/window.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>

#include <optional>
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

/// 日志行里用的截断（保留前 `limit` 个字符，其余用省略号）。
QString truncateText(const QString &text, int limit)
{
    if (text.size() <= limit) {
        return text;
    }
    return text.left(limit) + QChar(0x2026);
}

/// 一次触发的模板展开上下文：把剪贴板/选中文本的读取缓存起来。
///
/// 只有在字符串里真的出现 `{clipboard}` / `{selection}` 时才去读，
/// 而 `{selection}` 会合成一次 Ctrl+C——绝不能因为一条 `run` 动作就顺手做掉。
class ExpandContext
{
public:
    ExpandContext(Runtime *runtime, QString hotkey)
        : m_runtime(runtime), m_hotkey(std::move(hotkey))
    {
    }

    QString expand(const QString &text)
    {
        core::Vars vars = baseVars();
        if (core::needsClipboard(text)) {
            if (core::needsSelection(text)) {
                vars.selection = selectionText();
            }
            vars.clipboard = clipboardText();
        }
        return core::expand(text, vars);
    }

private:
    core::Vars baseVars() const
    {
        core::Vars vars;
        vars.hotkey = m_hotkey;
        vars.configDir = QFileInfo(m_runtime->configPath()).absolutePath();
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

    /// 剪贴板内容，在单次触发期间缓存。
    QString clipboardText()
    {
        if (m_clipboard.has_value()) {
            return *m_clipboard;
        }
        QString value;
        QString error;
        if (!win::clipboard::getText(&value, &error)) {
            win::logWarn(QStringLiteral("clipboard read failed: %1").arg(error));
        }
        m_clipboard = value;
        return value;
    }

    /// 当前选中的文本：合成一次 Ctrl+C，然后读剪贴板。
    QString selectionText()
    {
        if (m_selection.has_value()) {
            return *m_selection;
        }
        QString value;
        QString error;
        if (!win::copySelection(150, &error)) {
            win::logWarn(QStringLiteral("copying the selection failed: %1").arg(error));
        } else if (!win::clipboard::getText(&value, &error)) {
            win::logWarn(QStringLiteral("clipboard read failed: %1").arg(error));
        }
        m_clipboard = value;
        m_selection = value;
        return value;
    }

    Runtime *m_runtime = nullptr;
    QString m_hotkey;
    std::optional<QString> m_clipboard;
    std::optional<QString> m_selection;
};

/// `window` 动作等待刚启动的程序开窗的时间。
constexpr std::uint64_t kDefaultLaunchWaitMs = 3000;
/// 等待该窗口时的轮询间隔。
constexpr unsigned long kPollIntervalMs = 50;

/// 启动 `window` 动作指定的程序，然后等待它的窗口出现并激活它。
///
/// 运行在工作线程上，所以这个轮询循环允许 sleep；钩子回调永远不会走到这里。
///
/// 这里**不**套用 `toggle`：刚启动的窗口往往自己就抢到了前台，在这种时候
/// 把“启动它”变成“立刻收起它”毫无道理。
bool launchThenActivate(ExpandContext &ctx,
                        const core::LaunchSpec &launch,
                        const core::WindowQuery &query,
                        bool animate,
                        QString *detail,
                        QString *error)
{
    win::RunCommandSpec spec;
    spec.program = ctx.expand(launch.program);
    for (const QString &arg : launch.args) {
        spec.args.append(ctx.expand(arg));
    }
    if (launch.cwd.has_value()) {
        spec.cwd = ctx.expand(*launch.cwd);
    }
    spec.show = launch.show;
    spec.shell = launch.shell;
    spec.wait = false;
    for (auto it = launch.env.constBegin(); it != launch.env.constEnd(); ++it) {
        spec.env.insert(ctx.expand(it.key()), ctx.expand(it.value()));
    }

    QString started;
    if (!win::runCommand(spec, &started, error)) {
        return false;
    }

    const std::uint64_t timeout = launch.waitMs.value_or(kDefaultLaunchWaitMs);
    const std::uint64_t deadline = win::monotonicMs() + timeout;
    while (true) {
        HWND hwnd = win::window::find(query);
        if (hwnd != nullptr) {
            QString placement;
            if (!win::window::applyTo(hwnd, core::WindowOp::Activate, animate, &placement, error)) {
                return false;
            }
            if (detail != nullptr) {
                *detail = QStringLiteral("started %1, then %2").arg(started, placement);
            }
            return true;
        }
        if (win::monotonicMs() >= deadline) {
            if (error != nullptr) {
                *error = QStringLiteral("started %1 but no window %2 appeared within %3 ms")
                             .arg(started, query.describe())
                             .arg(timeout);
            }
            return false;
        }
        QThread::msleep(kPollIntervalMs);
    }
}

void executeWindowAction(ExpandContext &ctx,
                         const core::Action &action,
                         const QString &hotkey)
{
    const std::optional<QString> title = action.target.has_value()
                                             ? std::optional<QString>(ctx.expand(*action.target))
                                             : std::nullopt;
    const std::optional<QString> process = action.process.has_value()
                                               ? std::optional<QString>(ctx.expand(*action.process))
                                               : std::nullopt;
    const core::WindowQuery query = core::WindowQuery::make(title, process);
    const bool animate = action.animate.value_or(false);

    if (HWND hwnd = win::window::find(query); hwnd != nullptr) {
        // 已经在前台的窗口再激活一次本来就是空操作。默认的 `toggle` 改成把它
        // 收起，于是同一个快捷键在“唤起”与“收起”之间切换。这里只处理
        // “窗口已经存在”这条路径，见 launchThenActivate。
        const bool alreadyActive = win::window::isActive(hwnd);
        const core::WindowPlan plan =
            core::planWindowAction(action.windowOp, action.toggle, alreadyActive);
        QString detail;
        QString error;
        if (plan == core::WindowPlan::MinimizeBecauseActive) {
            if (!win::window::applyTo(hwnd, core::WindowOp::Minimize, animate, &detail, &error)) {
                win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
            } else {
                win::logInfo(QStringLiteral("`%1` -> %2 (already active)").arg(hotkey, detail));
            }
            return;
        }
        if (!win::window::applyTo(hwnd, action.windowOp, animate, &detail, &error)) {
            win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        }
        return;
    }

    if (action.launch.has_value() && action.windowOp == core::WindowOp::Activate) {
        QString detail;
        QString error;
        if (!launchThenActivate(ctx, *action.launch, query, animate, &detail, &error)) {
            win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        }
        return;
    }
    win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, win::window::missing(query)));
}

void executeClipboardAction(ExpandContext &ctx,
                            const core::Action &action,
                            const QString &hotkey)
{
    QString error;
    switch (action.clipboardOp) {
    case core::ClipboardOp::Get: {
        QString value;
        if (!win::clipboard::getText(&value, &error)) {
            win::logError(QStringLiteral("`%1` clipboard get failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard %2 char(s): %3")
                         .arg(hotkey)
                         .arg(value.size())
                         .arg(core::rustDebug(truncateText(value, 60))));
        return;
    }
    case core::ClipboardOp::Set: {
        const QString value = ctx.expand(action.clipboardText.value_or(QString()));
        if (!win::clipboard::setText(value, &error)) {
            win::logError(QStringLiteral("`%1` clipboard set failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard set (%2 char(s))").arg(hotkey).arg(value.size()));
        return;
    }
    case core::ClipboardOp::Append: {
        const QString value = ctx.expand(action.clipboardText.value_or(QString()));
        if (!win::clipboard::appendText(value, &error)) {
            win::logError(QStringLiteral("`%1` clipboard append failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard append (%2 char(s))").arg(hotkey).arg(value.size()));
        return;
    }
    case core::ClipboardOp::Clear:
        if (!win::clipboard::clear(&error)) {
            win::logError(QStringLiteral("`%1` clipboard clear failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard cleared").arg(hotkey));
        return;
    }
}

/// 执行一个动作。`ctx` 在单次触发期间共享，所以剪贴板只读一次。
void executeAction(Runtime *runtime,
                   const core::Compiled &config,
                   const QString &hotkey,
                   const core::Action &action,
                   ExpandContext &ctx)
{
    switch (action.kind) {
    case core::Action::Kind::Noop:
        break;
    case core::Action::Kind::Send: {
        const QString script = ctx.expand(action.keys);
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
        const QString text = ctx.expand(action.text);
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
        const QString target = ctx.expand(action.target.value_or(QString()));
        const std::optional<QString> args =
            action.openArgs.has_value() ? std::optional<QString>(ctx.expand(*action.openArgs))
                                        : std::nullopt;
        const std::optional<QString> cwd =
            action.cwd.has_value() ? std::optional<QString>(ctx.expand(*action.cwd))
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
        spec.program = ctx.expand(action.program);
        for (const QString &arg : action.args) {
            spec.args.append(ctx.expand(arg));
        }
        if (action.cwd.has_value()) {
            spec.cwd = ctx.expand(*action.cwd);
        }
        spec.show = action.show;
        spec.shell = action.shell;
        spec.wait = action.wait;
        for (auto it = action.env.constBegin(); it != action.env.constEnd(); ++it) {
            spec.env.insert(ctx.expand(it.key()), ctx.expand(it.value()));
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
    case core::Action::Kind::Volume: {
        QString detail;
        QString error;
        if (!win::audio::apply(action.volumeOp, action.level, action.step, &detail, &error)) {
            win::logError(QStringLiteral("`%1` volume: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> volume %2").arg(hotkey, detail));
        }
        break;
    }
    case core::Action::Kind::Media: {
        core::Vk vk = core::vk::MEDIA_PLAY_PAUSE;
        switch (action.mediaOp) {
        case core::MediaOp::PlayPause:
            vk = core::vk::MEDIA_PLAY_PAUSE;
            break;
        case core::MediaOp::Next:
            vk = core::vk::MEDIA_NEXT_TRACK;
            break;
        case core::MediaOp::Prev:
            vk = core::vk::MEDIA_PREV_TRACK;
            break;
        case core::MediaOp::Stop:
            vk = core::vk::MEDIA_STOP;
            break;
        }
        QString error;
        if (!win::tapKey(vk, &error)) {
            win::logError(QStringLiteral("`%1` media failed: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> media %2")
                             .arg(hotkey, core::mediaOpDebugName(action.mediaOp)));
        }
        break;
    }
    case core::Action::Kind::Clipboard:
        executeClipboardAction(ctx, action, hotkey);
        break;
    case core::Action::Kind::Window:
        executeWindowAction(ctx, action, hotkey);
        break;
    case core::Action::Kind::Notify: {
        const QString title = ctx.expand(action.title);
        const QString body = ctx.expand(action.body.value_or(QString()));
        win::logInfo(QStringLiteral("`%1` -> notify %2").arg(hotkey, core::rustDebug(title)));
        runtime->notifyFromAnyThread(title, body);
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
            target = !runtime->isSuspended();
            break;
        }
        runtime->postControl(cmd);
        runtime->reportSuspended(target);
        win::logInfo(QStringLiteral("`%1` -> suspend (%2)").arg(hotkey).arg(target ? "on" : "off"));
        break;
    }
    case core::Action::Kind::Reload:
        win::logInfo(QStringLiteral("`%1` -> reload").arg(hotkey));
        runtime->reloadFromAnyThread();
        break;
    case core::Action::Kind::Quit:
        win::logInfo(QStringLiteral("`%1` -> quit").arg(hotkey));
        runtime->requestShutdownFromAnyThread();
        break;
    default:
        // desktop / menu / help / power 是后续阶段的后端，这里先明确说一声，
        // 而不是静默地什么都不做。
        win::logWarn(QStringLiteral("`%1`: action `%2` is not implemented yet (later stage)")
                         .arg(hotkey, action.summary()));
        break;
    }
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
    ExpandContext ctx(m_runtime, binding.name);
    for (const core::Action &action : actions) {
        executeAction(m_runtime, *config, binding.name, action, ctx);
    }
}

} // namespace flowkeyd::app
