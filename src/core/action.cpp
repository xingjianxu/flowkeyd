#include "core/action.h"

#include <QRegularExpression>

#include <algorithm>

namespace flowkeyd::core {

QString rustDebug(const QString &text)
{
    QString out;
    out.reserve(text.size() + 2);
    out.append(QLatin1Char('"'));
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'"':
            out.append(QLatin1String("\\\""));
            break;
        case u'\\':
            out.append(QLatin1String("\\\\"));
            break;
        case u'\n':
            out.append(QLatin1String("\\n"));
            break;
        case u'\r':
            out.append(QLatin1String("\\r"));
            break;
        case u'\t':
            out.append(QLatin1String("\\t"));
            break;
        case u'\0':
            out.append(QLatin1String("\\0"));
            break;
        default:
            out.append(c);
            break;
        }
    }
    out.append(QLatin1Char('"'));
    return out;
}

QString showModeDebugName(ShowMode mode)
{
    switch (mode) {
    case ShowMode::Normal:
        return QStringLiteral("Normal");
    case ShowMode::Hidden:
        return QStringLiteral("Hidden");
    case ShowMode::Minimized:
        return QStringLiteral("Minimized");
    case ShowMode::Maximized:
        return QStringLiteral("Maximized");
    }
    return QStringLiteral("Normal");
}

QString volumeOpDebugName(VolumeOp op)
{
    switch (op) {
    case VolumeOp::Up:
        return QStringLiteral("Up");
    case VolumeOp::Down:
        return QStringLiteral("Down");
    case VolumeOp::Set:
        return QStringLiteral("Set");
    case VolumeOp::Mute:
        return QStringLiteral("Mute");
    case VolumeOp::Unmute:
        return QStringLiteral("Unmute");
    case VolumeOp::Toggle:
        return QStringLiteral("Toggle");
    }
    return QStringLiteral("Up");
}

QString mediaOpDebugName(MediaOp op)
{
    switch (op) {
    case MediaOp::PlayPause:
        return QStringLiteral("PlayPause");
    case MediaOp::Next:
        return QStringLiteral("Next");
    case MediaOp::Prev:
        return QStringLiteral("Prev");
    case MediaOp::Stop:
        return QStringLiteral("Stop");
    }
    return QStringLiteral("PlayPause");
}

QString clipboardOpDebugName(ClipboardOp op)
{
    switch (op) {
    case ClipboardOp::Get:
        return QStringLiteral("Get");
    case ClipboardOp::Set:
        return QStringLiteral("Set");
    case ClipboardOp::Append:
        return QStringLiteral("Append");
    case ClipboardOp::Clear:
        return QStringLiteral("Clear");
    }
    return QStringLiteral("Get");
}

QString windowOpDebugName(WindowOp op)
{
    switch (op) {
    case WindowOp::Activate:
        return QStringLiteral("Activate");
    case WindowOp::Minimize:
        return QStringLiteral("Minimize");
    case WindowOp::Maximize:
        return QStringLiteral("Maximize");
    case WindowOp::Restore:
        return QStringLiteral("Restore");
    case WindowOp::Close:
        return QStringLiteral("Close");
    case WindowOp::ToggleTopmost:
        return QStringLiteral("ToggleTopmost");
    }
    return QStringLiteral("Activate");
}

QString toggleStateDebugName(ToggleState state)
{
    switch (state) {
    case ToggleState::On:
        return QStringLiteral("On");
    case ToggleState::Off:
        return QStringLiteral("Off");
    case ToggleState::Toggle:
        return QStringLiteral("Toggle");
    }
    return QStringLiteral("Toggle");
}

QString capsLockStateDebugName(CapsLockState state)
{
    Q_UNUSED(state);
    return QStringLiteral("Off");
}

QString powerOpName(PowerOp op)
{
    switch (op) {
    case PowerOp::Sleep:
        return QStringLiteral("sleep");
    case PowerOp::Hibernate:
        return QStringLiteral("hibernate");
    case PowerOp::Shutdown:
        return QStringLiteral("shutdown");
    case PowerOp::Restart:
        return QStringLiteral("restart");
    case PowerOp::Logoff:
        return QStringLiteral("logoff");
    case PowerOp::Lock:
        return QStringLiteral("lock");
    case PowerOp::ScreenOff:
        return QStringLiteral("screen_off");
    }
    return QStringLiteral("sleep");
}

std::optional<ShowMode> showModeFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("normal")) {
        return ShowMode::Normal;
    }
    if (key == QLatin1String("hidden")) {
        return ShowMode::Hidden;
    }
    if (key == QLatin1String("minimized")) {
        return ShowMode::Minimized;
    }
    if (key == QLatin1String("maximized")) {
        return ShowMode::Maximized;
    }
    return std::nullopt;
}

std::optional<VolumeOp> volumeOpFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("up")) {
        return VolumeOp::Up;
    }
    if (key == QLatin1String("down")) {
        return VolumeOp::Down;
    }
    if (key == QLatin1String("set")) {
        return VolumeOp::Set;
    }
    if (key == QLatin1String("mute")) {
        return VolumeOp::Mute;
    }
    if (key == QLatin1String("unmute")) {
        return VolumeOp::Unmute;
    }
    if (key == QLatin1String("toggle")) {
        return VolumeOp::Toggle;
    }
    return std::nullopt;
}

std::optional<MediaOp> mediaOpFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("play_pause") || key == QLatin1String("playpause")
        || key == QLatin1String("play")) {
        return MediaOp::PlayPause;
    }
    if (key == QLatin1String("next")) {
        return MediaOp::Next;
    }
    if (key == QLatin1String("prev")) {
        return MediaOp::Prev;
    }
    if (key == QLatin1String("stop")) {
        return MediaOp::Stop;
    }
    return std::nullopt;
}

std::optional<ClipboardOp> clipboardOpFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("get")) {
        return ClipboardOp::Get;
    }
    if (key == QLatin1String("set")) {
        return ClipboardOp::Set;
    }
    if (key == QLatin1String("append")) {
        return ClipboardOp::Append;
    }
    if (key == QLatin1String("clear")) {
        return ClipboardOp::Clear;
    }
    return std::nullopt;
}

std::optional<WindowOp> windowOpFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("activate")) {
        return WindowOp::Activate;
    }
    if (key == QLatin1String("minimize")) {
        return WindowOp::Minimize;
    }
    if (key == QLatin1String("maximize")) {
        return WindowOp::Maximize;
    }
    if (key == QLatin1String("restore")) {
        return WindowOp::Restore;
    }
    if (key == QLatin1String("close")) {
        return WindowOp::Close;
    }
    if (key == QLatin1String("toggle_topmost") || key == QLatin1String("topmost")) {
        return WindowOp::ToggleTopmost;
    }
    return std::nullopt;
}

std::optional<ToggleState> toggleStateFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("on")) {
        return ToggleState::On;
    }
    if (key == QLatin1String("off")) {
        return ToggleState::Off;
    }
    if (key == QLatin1String("toggle")) {
        return ToggleState::Toggle;
    }
    return std::nullopt;
}

std::optional<PowerOp> powerOpFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("sleep") || key == QLatin1String("suspend")) {
        return PowerOp::Sleep;
    }
    if (key == QLatin1String("hibernate")) {
        return PowerOp::Hibernate;
    }
    if (key == QLatin1String("shutdown") || key == QLatin1String("poweroff")) {
        return PowerOp::Shutdown;
    }
    if (key == QLatin1String("restart") || key == QLatin1String("reboot")) {
        return PowerOp::Restart;
    }
    if (key == QLatin1String("logoff") || key == QLatin1String("logout")) {
        return PowerOp::Logoff;
    }
    if (key == QLatin1String("lock")) {
        return PowerOp::Lock;
    }
    if (key == QLatin1String("screen_off") || key == QLatin1String("monitor_off")
        || key == QLatin1String("display_off")) {
        return PowerOp::ScreenOff;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// window 辅助结构
// ---------------------------------------------------------------------------

namespace {

std::optional<QString> cleanQueryValue(const std::optional<QString> &value)
{
    if (!value.has_value()) {
        return std::nullopt;
    }
    const QString trimmed = value->trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    if (trimmed.compare(QLatin1String("foreground"), Qt::CaseInsensitive) == 0
        || trimmed.compare(QLatin1String("active"), Qt::CaseInsensitive) == 0) {
        return std::nullopt;
    }
    return trimmed;
}

} // namespace

WindowQuery WindowQuery::make(const std::optional<QString> &title, const std::optional<QString> &process)
{
    WindowQuery query;
    query.title = cleanQueryValue(title);
    query.process = cleanQueryValue(process);
    return query;
}

QString WindowQuery::describe() const
{
    if (!title.has_value() && !process.has_value()) {
        return QStringLiteral("foreground");
    }
    if (title.has_value() && !process.has_value()) {
        return QStringLiteral("with a title containing %1").arg(rustDebug(*title));
    }
    if (!title.has_value() && process.has_value()) {
        return QStringLiteral("belonging to the process %1").arg(rustDebug(*process));
    }
    return QStringLiteral("with a title containing %1 and the process %2")
        .arg(rustDebug(*title), rustDebug(*process));
}

std::optional<QChar> MenuItemDef::keyChar() const
{
    if (!key.has_value()) {
        return std::nullopt;
    }
    const QString trimmed = key->trimmed();
    if (trimmed.size() != 1) {
        return std::nullopt;
    }
    return trimmed.at(0).toLower();
}

// ---------------------------------------------------------------------------
// 摘要
// ---------------------------------------------------------------------------

QString Action::summary() const
{
    switch (kind) {
    case Kind::Run: {
        QString text = QStringLiteral("run %1").arg(program);
        if (!args.isEmpty()) {
            text += QLatin1Char(' ') + args.join(QLatin1Char(' '));
        }
        return text;
    }
    case Kind::Send:
        return QStringLiteral("send %1").arg(keys);
    case Kind::CapsLock:
        return QStringLiteral("caps lock %1").arg(capsLockStateDebugName(capsState));
    case Kind::Type:
        return QStringLiteral("type %1").arg(rustDebug(text));
    case Kind::Open:
        return QStringLiteral("open %1").arg(target.value_or(QString()));
    case Kind::Volume: {
        if (level.has_value()) {
            return QStringLiteral("volume %1 %2").arg(volumeOpDebugName(volumeOp)).arg(*level);
        }
        return QStringLiteral("volume %1").arg(volumeOpDebugName(volumeOp));
    }
    case Kind::Media:
        return QStringLiteral("media %1").arg(mediaOpDebugName(mediaOp));
    case Kind::Clipboard:
        return QStringLiteral("clipboard %1").arg(clipboardOpDebugName(clipboardOp));
    case Kind::Window: {
        const WindowQuery query = WindowQuery::make(target, process);
        QString text = QStringLiteral("window %1 %2").arg(windowOpDebugName(windowOp), query.describe());
        if (launch.has_value()) {
            text += QLatin1String(" (launch if missing)");
        }
        if (toggle.has_value() && !*toggle) {
            text += QLatin1String(" (no toggle)");
        }
        if (animate.has_value() && *animate) {
            text += QLatin1String(" (animated)");
        }
        return text;
    }
    case Kind::Notify:
        return QStringLiteral("notify %1").arg(rustDebug(title));
    case Kind::Desktop:
        return QStringLiteral("desktop %1").arg(desktopSwitch);
    case Kind::Menu: {
        const QString count = QStringLiteral("(%1 item(s))").arg(items.size());
        if (menuTitle.has_value()) {
            return QStringLiteral("menu %1 %2").arg(rustDebug(*menuTitle), count);
        }
        return QStringLiteral("menu %1").arg(count);
    }
    case Kind::Help:
        if (helpTitle.has_value()) {
            return QStringLiteral("help %1").arg(rustDebug(*helpTitle));
        }
        return QStringLiteral("help");
    case Kind::Power:
        return QStringLiteral("power %1").arg(powerOpName(powerOp));
    case Kind::Suspend:
        return QStringLiteral("suspend %1").arg(toggleStateDebugName(toggleState));
    case Kind::Reload:
        return QStringLiteral("reload");
    case Kind::Quit:
        return QStringLiteral("quit");
    case Kind::Noop:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

// ---------------------------------------------------------------------------
// 「危险动作」
// ---------------------------------------------------------------------------

bool isDestructive(const Action &action)
{
    switch (action.kind) {
    case Action::Kind::Quit:
    case Action::Kind::Suspend:
    case Action::Kind::Power:
        return true;
    default:
        break;
    }
    return false;
}

bool isDestructive(const std::vector<Action> &actions)
{
    return std::any_of(actions.begin(), actions.end(),
                       [](const Action &action) { return isDestructive(action); });
}

// ---------------------------------------------------------------------------
// 简写
// ---------------------------------------------------------------------------

std::optional<QString> parseActionShorthand(const QString &input, Action *out)
{
    const QString text = input.trimmed();
    QString prefix;
    QString rest;
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        prefix = text.left(colon).trimmed().toLower();
        rest = text.mid(colon + 1).trimmed();
    } else {
        prefix = text.toLower();
    }

    auto bad = [](const QString &message) -> std::optional<QString> { return message; };

    if (prefix == QLatin1String("run")) {
        if (rest.isEmpty()) {
            return bad(QStringLiteral("`run:` shorthand needs a command"));
        }
        // 把命令按空白拆开，使 `run:notepad.exe foo.txt` 能工作。
        QStringList parts = rest.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            return bad(QStringLiteral("`run:` shorthand needs a command"));
        }
        out->kind = Action::Kind::Run;
        out->program = parts.takeFirst();
        out->args = parts;
        return std::nullopt;
    }
    if (prefix == QLatin1String("send")) {
        out->kind = Action::Kind::Send;
        out->keys = rest;
        return std::nullopt;
    }
    if (prefix == QLatin1String("type")) {
        out->kind = Action::Kind::Type;
        out->text = rest;
        return std::nullopt;
    }
    if (prefix == QLatin1String("open")) {
        out->kind = Action::Kind::Open;
        out->target = rest;
        return std::nullopt;
    }
    if (prefix == QLatin1String("notify")) {
        out->kind = Action::Kind::Notify;
        const qsizetype bar = rest.indexOf(QLatin1Char('|'));
        if (bar >= 0) {
            out->title = rest.left(bar).trimmed();
            out->body = rest.mid(bar + 1).trimmed();
        } else {
            out->title = rest;
        }
        return std::nullopt;
    }
    if (prefix == QLatin1String("desktop")) {
        bool ok = false;
        const uint number = rest.toUInt(&ok);
        if (!ok || number < 1) {
            return bad(QStringLiteral("`desktop:` expects a desktop number of 1 or greater, got `%1`")
                           .arg(rest));
        }
        out->kind = Action::Kind::Desktop;
        out->desktopSwitch = number;
        return std::nullopt;
    }
    if (prefix == QLatin1String("volume")) {
        const auto op = volumeOpFromName(rest);
        if (!op.has_value() || rest.trimmed().toLower() == QLatin1String("set")) {
            return bad(QStringLiteral("`volume:` expects up|down|mute|unmute|toggle, got `%1`").arg(rest));
        }
        out->kind = Action::Kind::Volume;
        out->volumeOp = *op;
        return std::nullopt;
    }
    if (prefix == QLatin1String("media")) {
        const auto op = mediaOpFromName(rest);
        if (!op.has_value()) {
            return bad(QStringLiteral("`media:` expects play_pause|next|prev|stop, got `%1`").arg(rest));
        }
        out->kind = Action::Kind::Media;
        out->mediaOp = *op;
        return std::nullopt;
    }
    if (prefix == QLatin1String("clipboard")) {
        const QString op = rest.toLower();
        if (op == QLatin1String("get")) {
            out->kind = Action::Kind::Clipboard;
            out->clipboardOp = ClipboardOp::Get;
            return std::nullopt;
        }
        if (op == QLatin1String("clear")) {
            out->kind = Action::Kind::Clipboard;
            out->clipboardOp = ClipboardOp::Clear;
            return std::nullopt;
        }
        return bad(QStringLiteral("`clipboard:` expects get|clear, got `%1`").arg(rest));
    }
    if (prefix == QLatin1String("window")) {
        const auto op = windowOpFromName(rest);
        if (!op.has_value()) {
            return bad(QStringLiteral("`window:` expects activate|minimize|maximize|restore|close|"
                                      "toggle_topmost, got `%1`")
                           .arg(rest));
        }
        out->kind = Action::Kind::Window;
        out->windowOp = *op;
        return std::nullopt;
    }
    if (prefix == QLatin1String("power")) {
        const auto op = powerOpFromName(rest);
        if (!op.has_value()) {
            return bad(QStringLiteral("`power:` expects sleep|hibernate|shutdown|restart|logoff|lock|"
                                      "screen_off, got `%1`")
                           .arg(rest));
        }
        out->kind = Action::Kind::Power;
        out->powerOp = *op;
        return std::nullopt;
    }
    if (prefix == QLatin1String("reload")) {
        out->kind = Action::Kind::Reload;
        return std::nullopt;
    }
    if (prefix == QLatin1String("quit") || prefix == QLatin1String("exit")) {
        out->kind = Action::Kind::Quit;
        return std::nullopt;
    }
    // `help` 与 `reload` 一样是裸关键字：帮助列表来自配置本身，不需要参数。
    if (prefix == QLatin1String("help")) {
        out->kind = Action::Kind::Help;
        return std::nullopt;
    }
    if (prefix == QLatin1String("none") || prefix == QLatin1String("noop")
        || prefix == QLatin1String("block")) {
        out->kind = Action::Kind::Noop;
        return std::nullopt;
    }
    return bad(QStringLiteral("unknown action shorthand `%1:` (expected run:, send:, type:, open:, "
                              "notify:, desktop:, volume:, media:, clipboard:, window:, power:, "
                              "reload, quit, help, none)")
                   .arg(prefix));
}

} // namespace flowkeyd::core
