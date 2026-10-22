#include "core/config_map.h"

#include "core/action.h"
#include "core/keys.h"
#include "core/remote_desktop.h"

#include <QMap>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace flowkeyd::core {

namespace {

// ---------------------------------------------------------------------------
// 枚举 <-> 配置里的写法
// ---------------------------------------------------------------------------

template <typename T>
struct EnumName
{
    T value;
    const char *text;
};

template <typename T, std::size_t N>
QString enumName(const EnumName<T> (&table)[N], T value)
{
    for (const EnumName<T> &entry : table) {
        if (entry.value == value) {
            return QString::fromLatin1(entry.text);
        }
    }
    return QString::fromLatin1(table[0].text);
}

constexpr EnumName<ShowMode> kShowModes[] = {
    {ShowMode::Normal, "normal"},
    {ShowMode::Hidden, "hidden"},
    {ShowMode::Minimized, "minimized"},
    {ShowMode::Maximized, "maximized"},
};

constexpr EnumName<VolumeOp> kVolumeOps[] = {
    {VolumeOp::Up, "up"},   {VolumeOp::Down, "down"},     {VolumeOp::Set, "set"},
    {VolumeOp::Mute, "mute"}, {VolumeOp::Unmute, "unmute"}, {VolumeOp::Toggle, "toggle"},
};

constexpr EnumName<MediaOp> kMediaOps[] = {
    {MediaOp::PlayPause, "play_pause"},
    {MediaOp::Next, "next"},
    {MediaOp::Prev, "prev"},
    {MediaOp::Stop, "stop"},
};

constexpr EnumName<ClipboardOp> kClipboardOps[] = {
    {ClipboardOp::Get, "get"},
    {ClipboardOp::Set, "set"},
    {ClipboardOp::Append, "append"},
    {ClipboardOp::Clear, "clear"},
};

constexpr EnumName<WindowOp> kWindowOps[] = {
    {WindowOp::Activate, "activate"},
    {WindowOp::Minimize, "minimize"},
    {WindowOp::Maximize, "maximize"},
    {WindowOp::Restore, "restore"},
    {WindowOp::Close, "close"},
    {WindowOp::ToggleTopmost, "toggle_topmost"},
    {WindowOp::MovePrevDesktop, "move_prev_desktop"},
    {WindowOp::MoveNextDesktop, "move_next_desktop"},
    {WindowOp::MoveLeftMonitor, "move_left_monitor"},
    {WindowOp::MoveRightMonitor, "move_right_monitor"},
};

constexpr EnumName<ToggleState> kToggleStates[] = {
    {ToggleState::On, "on"},
    {ToggleState::Off, "off"},
    {ToggleState::Toggle, "toggle"},
};

constexpr EnumName<WorkspaceOp> kWorkspaceOps[] = {
    {WorkspaceOp::Switch, "switch"},
    {WorkspaceOp::Move, "move"},
    {WorkspaceOp::MovePrev, "move_prev"},
    {WorkspaceOp::MoveNext, "move_next"},
    {WorkspaceOp::Prev, "prev"},
    {WorkspaceOp::Next, "next"},
};

constexpr EnumName<WorkspaceSwitchTarget> kWorkspaceSwitchTargets[] = {
    {WorkspaceSwitchTarget::Cursor, "cursor"},
    {WorkspaceSwitchTarget::Focus, "focus"},
};

constexpr EnumName<WorkspaceSwitchEffect> kWorkspaceSwitchEffects[] = {
    {WorkspaceSwitchEffect::Auto, "auto"},
    {WorkspaceSwitchEffect::Instant, "instant"},
    {WorkspaceSwitchEffect::Freeze, "freeze"},
};

constexpr EnumName<TrayBadgeMode> kTrayBadgeModes[] = {
    {TrayBadgeMode::Both, "both"},
    {TrayBadgeMode::Desktop, "desktop"},
    {TrayBadgeMode::Workspace, "workspace"},
};

constexpr EnumName<RemapMode> kRemapModes[] = {
    {RemapMode::Hold, "hold"},
    {RemapMode::Tap, "tap"},
};

// ---------------------------------------------------------------------------
// 从字典里取值的小工具
// ---------------------------------------------------------------------------

QString key(const char *name)
{
    return QString::fromLatin1(name);
}

bool has(const QVariantMap &map, const char *name)
{
    return map.contains(key(name));
}

std::optional<QString> optionalString(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return std::nullopt;
    }
    return map.value(key(name)).toString();
}

QString stringOr(const QVariantMap &map, const char *name, const QString &fallback = QString())
{
    if (!has(map, name)) {
        return fallback;
    }
    const QVariant value = map.value(key(name));
    return value.isValid() ? value.toString() : fallback;
}

bool boolOr(const QVariantMap &map, const char *name, bool fallback)
{
    if (!has(map, name)) {
        return fallback;
    }
    return map.value(key(name)).toBool();
}

std::optional<bool> optionalBool(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return std::nullopt;
    }
    return map.value(key(name)).toBool();
}

std::uint32_t uintOr(const QVariantMap &map, const char *name, std::uint32_t fallback)
{
    if (!has(map, name)) {
        return fallback;
    }
    bool ok = false;
    const qulonglong value = map.value(key(name)).toULongLong(&ok);
    return ok ? static_cast<std::uint32_t>(value) : fallback;
}

std::optional<std::uint32_t> optionalUint(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong value = map.value(key(name)).toULongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::optional<std::uint64_t> optionalU64(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong value = map.value(key(name)).toULongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

std::optional<std::uint8_t> optionalU8(const QVariantMap &map, const char *name)
{
    const std::optional<std::uint32_t> value = optionalUint(map, name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*value);
}

std::optional<std::int32_t> optionalI32(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return std::nullopt;
    }
    bool ok = false;
    const qlonglong value = map.value(key(name)).toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(value);
}

/// 把 QML 交回来的值读成字符串列表：`QStringList`、`QVariantList` 与裸字符串
/// 都接受（QML 里的 JS 数组与 C++ 里构造的 `QStringList` 到此统一）。
QStringList stringListValue(const QVariant &value)
{
    const QMetaType type = value.metaType();
    if (type.id() == QMetaType::QStringList) {
        return value.toStringList();
    }
    if (type.id() == QMetaType::QString) {
        return QStringList{value.toString()};
    }
    if (value.canConvert<QVariantList>()) {
        QStringList out;
        const QVariantList list = value.toList();
        out.reserve(list.size());
        for (const QVariant &item : list) {
            out.append(item.toString());
        }
        return out;
    }
    return {};
}

QStringList stringListOr(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return {};
    }
    return stringListValue(map.value(key(name)));
}

QList<QVariantMap> mapListValue(const QVariant &value)
{
    QList<QVariantMap> out;
    if (!value.canConvert<QVariantList>()) {
        return out;
    }
    const QVariantList list = value.toList();
    out.reserve(list.size());
    for (const QVariant &item : list) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }
        out.append(item.toMap());
    }
    return out;
}

QList<QVariantMap> mapListOr(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return {};
    }
    return mapListValue(map.value(key(name)));
}

QVariantMap stringMapOr(const QVariantMap &map, const char *name)
{
    if (!has(map, name)) {
        return {};
    }
    return map.value(key(name)).toMap();
}

QVariant stringListVariant(const QStringList &list)
{
    QVariantList out;
    out.reserve(list.size());
    for (const QString &item : list) {
        out.append(item);
    }
    return out;
}

QVariant envVariant(const QMap<QString, QString> &env)
{
    QVariantMap out;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

QMap<QString, QString> envFromMap(const QVariantMap &map)
{
    QMap<QString, QString> out;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        out.insert(it.key(), it.value().toString());
    }
    return out;
}

QVariantMap mapOf(std::initializer_list<std::pair<const char *, QVariant>> entries)
{
    QVariantMap out;
    for (const auto &entry : entries) {
        out.insert(key(entry.first), entry.second);
    }
    return out;
}

/// 只在 `value` 有值时插入一个键（`std::optional` 的“没写就不在字典里”）。
void putIf(std::optional<QString> value, QVariantMap *map, const char *name)
{
    if (value.has_value()) {
        map->insert(key(name), *value);
    }
}

void putBoolIf(std::optional<bool> value, QVariantMap *map, const char *name)
{
    if (value.has_value()) {
        map->insert(key(name), *value);
    }
}

void putU32If(std::optional<std::uint32_t> value, QVariantMap *map, const char *name)
{
    if (value.has_value()) {
        map->insert(key(name), static_cast<uint>(*value));
    }
}

void putI32If(std::optional<std::int32_t> value, QVariantMap *map, const char *name)
{
    if (value.has_value()) {
        map->insert(key(name), static_cast<int>(*value));
    }
}

void putU64If(std::optional<std::uint64_t> value, QVariantMap *map, const char *name)
{
    if (value.has_value()) {
        map->insert(key(name), static_cast<qulonglong>(*value));
    }
}

/// 空串在界面上等于「这个字段没填」：`help` / `windows` / `apps` 的 `title` 写成
/// 空串会被 `--check` 拒绝（它要么是省略，要么是一个非空标题）。
std::optional<QString> nonEmpty(std::optional<QString> value)
{
    if (value.has_value() && value->trimmed().isEmpty()) {
        return std::nullopt;
    }
    return value;
}

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

/// 动作 -> 字典。`launch` 只写出**显式写过**的字段：那张表里“写没写”本身就是
/// 语义，多写一个 `program = ""` 会让一个本来继承 app 的 launch 变成一条坏配置。
QVariantMap launchToMap(const LaunchSpec &spec, const LaunchFields &fields, bool masked)
{
    QVariantMap out;
    const auto take = [&](bool field) { return !masked || field; };
    if (take(fields.program)) {
        out.insert(QStringLiteral("program"), spec.program);
    }
    if (take(fields.args)) {
        out.insert(QStringLiteral("args"), stringListVariant(spec.args));
    }
    if (take(fields.cwd) && spec.cwd.has_value()) {
        out.insert(QStringLiteral("cwd"), *spec.cwd);
    }
    if (take(fields.show)) {
        out.insert(QStringLiteral("show"), enumName(kShowModes, spec.show));
    }
    if (take(fields.shell)) {
        out.insert(QStringLiteral("shell"), spec.shell);
    }
    if (take(fields.env) && !spec.env.isEmpty()) {
        out.insert(QStringLiteral("env"), envVariant(spec.env));
    }
    if (take(fields.waitMs) && spec.waitMs.has_value()) {
        out.insert(QStringLiteral("wait_ms"), static_cast<qulonglong>(*spec.waitMs));
    }
    // `elevated` 在遮罩模式（动作自己的 `launch`）里同样要能写成 `false`：那是
    // 对 `app{ launch = { elevated = true } }` 的逐字段覆盖。未遮罩时与其它字段
    // 一样整份写出（`elevated = false` 就是默认值，写出来无害）。
    if (take(fields.elevated)) {
        out.insert(QStringLiteral("elevated"), spec.elevated);
    }
    return out;
}

LaunchSpec launchFromMap(const QVariantMap &map, bool masked, LaunchFields *fields)
{
    LaunchSpec spec;
    spec.program = stringOr(map, "program");
    spec.args = stringListOr(map, "args");
    spec.cwd = optionalString(map, "cwd");
    spec.show = showModeFromName(stringOr(map, "show", QStringLiteral("normal")))
                    .value_or(ShowMode::Normal);
    spec.shell = boolOr(map, "shell", false);
    spec.env = envFromMap(stringMapOr(map, "env"));
    spec.waitMs = optionalU64(map, "wait_ms");
    spec.elevated = boolOr(map, "elevated", false);
    if (fields != nullptr) {
        if (masked) {
            // 界面交回来的字典只有写过的键，所以“在不在”就是掩码。
            fields->program = has(map, "program");
            fields->args = has(map, "args");
            fields->cwd = has(map, "cwd");
            fields->show = has(map, "show");
            fields->shell = has(map, "shell");
            fields->env = has(map, "env");
            fields->waitMs = has(map, "wait_ms");
            fields->elevated = has(map, "elevated");
        } else {
            *fields = LaunchFields{};
        }
    }
    return spec;
}

QVariantMap windowQueryToMap(const Action &action)
{
    QVariantMap out;
    putIf(action.target, &out, "target");
    putIf(action.process, &out, "process");
    return out;
}

void windowQueryFromMap(const QVariantMap &map, Action *action)
{
    action->target = optionalString(map, "target");
    action->process = optionalString(map, "process");
}

QVariantList menuItemsToMap(const std::vector<MenuItemDef> &items)
{
    QVariantList out;
    out.reserve(static_cast<qsizetype>(items.size()));
    for (std::size_t i = 0; i < items.size(); ++i) {
        const MenuItemDef &item = items.at(i);
        QVariantMap entry;
        putIf(item.key, &entry, "key");
        entry.insert(QStringLiteral("label"), item.label);
        putIf(item.hint, &entry, "hint");
        if (item.action) {
            entry.insert(QStringLiteral("actions"),
                         actionListToMap(*item.action));
        } else {
            entry.insert(QStringLiteral("actions"), QVariantList());
        }
        out.append(entry);
    }
    return out;
}

std::vector<MenuItemDef> menuItemsFromMap(const QVariantList &list, QStringList *errors,
                                          const QString &label)
{
    std::vector<MenuItemDef> out;
    int index = 0;
    for (const QVariantMap &entry : mapListValue(list)) {
        ++index;
        MenuItemDef item;
        item.key = optionalString(entry, "key");
        item.label = stringOr(entry, "label");
        item.hint = optionalString(entry, "hint");
        if (auto spec = actionListFromMap(
                entry.value(QStringLiteral("actions")).toList(), errors,
                QStringLiteral("%1 menu item #%2").arg(label).arg(index))) {
            item.action = std::make_shared<ActionSpec>(std::move(*spec));
        }
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

/// 写回 Lua / 交给 Lua 层时用的值：序号必须是**数字**，写成字符串 `"1"` 会被
/// `convertMonitorRef` 当成设备名（那是完全不同的意思）。定义在下面。
QVariant monitorRefValue(const MonitorRef &ref);

QVariantMap settingsToMap(const Settings &settings)
{
    QVariantMap out;
    out.insert(QStringLiteral("log_level"), settings.logLevel);
    out.insert(QStringLiteral("swallow"), settings.swallow);
    out.insert(QStringLiteral("exact_modifiers"), settings.exactModifiers);
    out.insert(QStringLiteral("release_modifiers"), settings.releaseModifiers);
    out.insert(QStringLiteral("repeat_interval_ms"), settings.repeatIntervalMs);
    out.insert(QStringLiteral("repeat_delay_ms"), settings.repeatDelayMs);
    out.insert(QStringLiteral("tick_ms"), settings.tickMs);
    out.insert(QStringLiteral("input_backend"), settings.inputBackend);
    out.insert(QStringLiteral("single_instance"), settings.singleInstance);
    out.insert(QStringLiteral("elevate"), settings.elevate);
    out.insert(QStringLiteral("remote_desktop"), settings.remoteDesktop);
    // 名单与内置值相同时**不写**：否则界面保存一次就把当前版本的默认名单钉死在
    // 配置文件里，以后新增一个 RDP 客户端反而不会生效了。
    if (settings.remoteDesktopProcesses != builtinRemoteDesktopProcesses()) {
        out.insert(QStringLiteral("remote_desktop_processes"),
                   stringListVariant(settings.remoteDesktopProcesses));
    }
    out.insert(QStringLiteral("window_monitor"), monitorRefValue(settings.windowMonitor));
    // 托盘徽标显示哪几个号（`1/2` / 只有虚拟桌面号 / 只有工作空间号）。
    out.insert(QStringLiteral("tray_badge"), enumName(kTrayBadgeModes, settings.trayBadge));
    // `settings.workspace`：嵌套一层表（与配置文件里的形状一致）。
    // `exclude` 为空时不写（与 remote_desktop_processes 一样：赘余的默认值写进文件
    // 只会让以后改默认值时不生效）。
    QVariantMap workspace;
    workspace.insert(QStringLiteral("enabled"), settings.workspace.enabled);
    workspace.insert(QStringLiteral("count"), settings.workspace.count);
    workspace.insert(QStringLiteral("switch_target"),
                     enumName(kWorkspaceSwitchTargets, settings.workspace.switchTarget));
    workspace.insert(QStringLiteral("switch_effect"),
                     enumName(kWorkspaceSwitchEffects, settings.workspace.switchEffect));
    workspace.insert(QStringLiteral("new_window"),
                     settings.workspace.newWindow == 0
                         ? QVariant(QStringLiteral("active"))
                         : QVariant(static_cast<uint>(settings.workspace.newWindow)));
    if (!settings.workspace.exclude.isEmpty()) {
        workspace.insert(QStringLiteral("exclude"),
                         stringListVariant(settings.workspace.exclude));
    }
    out.insert(QStringLiteral("workspace"), workspace);
    return out;
}

Settings settingsFromMap(const QVariantMap &map)
{
    Settings settings;
    if (has(map, "log_level")) {
        settings.logLevel = stringOr(map, "log_level", settings.logLevel);
    }
    settings.swallow = boolOr(map, "swallow", settings.swallow);
    settings.exactModifiers = boolOr(map, "exact_modifiers", settings.exactModifiers);
    settings.releaseModifiers = boolOr(map, "release_modifiers", settings.releaseModifiers);
    settings.repeatIntervalMs = uintOr(map, "repeat_interval_ms", settings.repeatIntervalMs);
    settings.repeatDelayMs = uintOr(map, "repeat_delay_ms", settings.repeatDelayMs);
    settings.tickMs = uintOr(map, "tick_ms", settings.tickMs);
    if (has(map, "input_backend")) {
        settings.inputBackend = stringOr(map, "input_backend", settings.inputBackend);
    }
    settings.singleInstance = boolOr(map, "single_instance", settings.singleInstance);
    settings.elevate = boolOr(map, "elevate", settings.elevate);
    settings.remoteDesktop = boolOr(map, "remote_desktop", settings.remoteDesktop);
    if (has(map, "remote_desktop_processes")) {
        settings.remoteDesktopProcesses = stringListOr(map, "remote_desktop_processes");
    }
    if (auto ref = monitorRefFromText(stringOr(map, "window_monitor"))) {
        settings.windowMonitor = *ref;
    }
    if (has(map, "tray_badge")) {
        settings.trayBadge = trayBadgeModeFromName(stringOr(map, "tray_badge"))
                                 .value_or(settings.trayBadge);
    }
    if (has(map, "workspace")) {
        const QVariant value = map.value(QStringLiteral("workspace"));
        if (value.metaType().id() == QMetaType::Bool) {
            settings.workspace.enabled = value.toBool();
        } else {
            const QVariantMap workspace = value.toMap();
            settings.workspace.enabled = boolOr(workspace, "enabled", settings.workspace.enabled);
            settings.workspace.count = uintOr(workspace, "count", settings.workspace.count);
            if (has(workspace, "switch_target")) {
                settings.workspace.switchTarget =
                    workspaceSwitchTargetFromName(stringOr(workspace, "switch_target"))
                        .value_or(settings.workspace.switchTarget);
            }
            if (has(workspace, "switch_effect")) {
                settings.workspace.switchEffect =
                    workspaceSwitchEffectFromName(stringOr(workspace, "switch_effect"))
                        .value_or(settings.workspace.switchEffect);
            }
            if (has(workspace, "new_window")) {
                // `"active"` 与数字都能读：`QVariant` 的 `toUInt` 对字符串 `"active"`
                // 会失败，所以先试数字。
                bool ok = false;
                const uint number = workspace.value(QStringLiteral("new_window")).toUInt(&ok);
                settings.workspace.newWindow = ok ? number : 0;
            }
            if (has(workspace, "exclude")) {
                settings.workspace.exclude = stringListOr(workspace, "exclude");
            }
        }
    }
    return settings;
}

// ---------------------------------------------------------------------------
// 显示器目标
// ---------------------------------------------------------------------------

QString monitorRefText(const MonitorRef &ref)
{
    switch (ref.kind) {
    case MonitorRef::Kind::Index:
        return QString::number(ref.index);
    case MonitorRef::Kind::Primary:
        return QStringLiteral("primary");
    case MonitorRef::Kind::Device:
        return ref.device;
    case MonitorRef::Kind::Cursor:
        return QStringLiteral("cursor");
    case MonitorRef::Kind::Last:
        return QStringLiteral("last");
    case MonitorRef::Kind::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("primary");
}

/// 写回 Lua / 交给 Lua 层时用的值：序号必须是**数字**，写成字符串 `"1"` 会被
/// `convertMonitorRef` 当成设备名（那是完全不同的意思）。
QVariant monitorRefValue(const MonitorRef &ref)
{
    if (ref.kind == MonitorRef::Kind::Index) {
        return static_cast<uint>(ref.index);
    }
    return monitorRefText(ref);
}

std::optional<MonitorRef> monitorRefFromText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    MonitorRef ref;
    if (trimmed.compare(QLatin1String("primary"), Qt::CaseInsensitive) == 0) {
        ref.kind = MonitorRef::Kind::Primary;
        return ref;
    }
    if (trimmed.compare(QLatin1String("cursor"), Qt::CaseInsensitive) == 0) {
        ref.kind = MonitorRef::Kind::Cursor;
        return ref;
    }
    if (trimmed.compare(QLatin1String("last"), Qt::CaseInsensitive) == 0) {
        ref.kind = MonitorRef::Kind::Last;
        return ref;
    }
    if (trimmed.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
        ref.kind = MonitorRef::Kind::None;
        return ref;
    }
    bool ok = false;
    const qulonglong number = trimmed.toULongLong(&ok);
    if (ok && number >= 1) {
        ref.kind = MonitorRef::Kind::Index;
        ref.index = static_cast<std::uint32_t>(number);
        return ref;
    }
    ref.kind = MonitorRef::Kind::Device;
    ref.device = trimmed;
    return ref;
}

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

QVariantMap actionToMap(const Action &action)
{
    QVariantMap out;
    switch (action.kind) {
    case Action::Kind::Run:
        out.insert(QStringLiteral("type"), QStringLiteral("run"));
        out.insert(QStringLiteral("program"), action.program);
        out.insert(QStringLiteral("args"), stringListVariant(action.args));
        putIf(action.cwd, &out, "cwd");
        out.insert(QStringLiteral("show"), enumName(kShowModes, action.show));
        out.insert(QStringLiteral("shell"), action.shell);
        out.insert(QStringLiteral("wait"), action.wait);
        out.insert(QStringLiteral("elevated"), action.elevated);
        if (!action.env.isEmpty()) {
            out.insert(QStringLiteral("env"), envVariant(action.env));
        }
        break;
    case Action::Kind::Send:
        out.insert(QStringLiteral("type"), QStringLiteral("send"));
        out.insert(QStringLiteral("keys"), action.keys);
        putU64If(action.delayMs, &out, "delay_ms");
        putBoolIf(action.releaseModifiers, &out, "release_modifiers");
        break;
    case Action::Kind::CapsLock:
        out.insert(QStringLiteral("type"), QStringLiteral("caps_lock"));
        break;
    case Action::Kind::Type:
        out.insert(QStringLiteral("type"), QStringLiteral("type"));
        out.insert(QStringLiteral("text"), action.text);
        putU64If(action.delayMs, &out, "delay_ms");
        putBoolIf(action.releaseModifiers, &out, "release_modifiers");
        break;
    case Action::Kind::Open:
        out.insert(QStringLiteral("type"), QStringLiteral("open"));
        putIf(action.target, &out, "target");
        putIf(action.openArgs, &out, "args");
        putIf(action.cwd, &out, "cwd");
        out.insert(QStringLiteral("show"), enumName(kShowModes, action.show));
        break;
    case Action::Kind::Volume:
        out.insert(QStringLiteral("type"), QStringLiteral("volume"));
        out.insert(QStringLiteral("op"), enumName(kVolumeOps, action.volumeOp));
        if (action.level.has_value()) {
            out.insert(QStringLiteral("level"), *action.level);
        }
        if (action.step.has_value()) {
            out.insert(QStringLiteral("step"), *action.step);
        }
        break;
    case Action::Kind::Media:
        out.insert(QStringLiteral("type"), QStringLiteral("media"));
        out.insert(QStringLiteral("op"), enumName(kMediaOps, action.mediaOp));
        break;
    case Action::Kind::Clipboard:
        out.insert(QStringLiteral("type"), QStringLiteral("clipboard"));
        out.insert(QStringLiteral("op"), enumName(kClipboardOps, action.clipboardOp));
        putIf(action.clipboardText, &out, "text");
        break;
    case Action::Kind::Window:
        out.insert(QStringLiteral("type"), QStringLiteral("window"));
        out.insert(QStringLiteral("op"), enumName(kWindowOps, action.windowOp));
        out.insert(windowQueryToMap(action));
        putBoolIf(action.toggle, &out, "toggle");
        putBoolIf(action.animate, &out, "animate");
        putBoolIf(action.follow, &out, "follow");
        if (action.monitor.has_value()) {
            out.insert(QStringLiteral("monitor"), monitorRefValue(*action.monitor));
        }
        if (action.launch.has_value()) {
            out.insert(QStringLiteral("launch"),
                       launchToMap(*action.launch, action.launchFields, true));
        }
        putU64If(action.waitMs, &out, "wait_ms");
        break;
    case Action::Kind::Notify:
        out.insert(QStringLiteral("type"), QStringLiteral("notify"));
        out.insert(QStringLiteral("title"), action.title);
        putIf(action.body, &out, "body");
        break;
    case Action::Kind::Desktop:
        out.insert(QStringLiteral("type"), QStringLiteral("desktop"));
        out.insert(QStringLiteral("switch"), action.desktopSwitch);
        break;
    case Action::Kind::Menu:
        out.insert(QStringLiteral("type"), QStringLiteral("menu"));
        putIf(action.menuTitle, &out, "title");
        out.insert(QStringLiteral("items"), menuItemsToMap(action.items));
        break;
    case Action::Kind::Help:
        out.insert(QStringLiteral("type"), QStringLiteral("help"));
        putIf(action.helpTitle, &out, "title");
        break;
    case Action::Kind::Windows:
        out.insert(QStringLiteral("type"), QStringLiteral("windows"));
        putIf(action.windowsTitle, &out, "title");
        break;
    case Action::Kind::Apps:
        out.insert(QStringLiteral("type"), QStringLiteral("apps"));
        putIf(action.appsTitle, &out, "title");
        break;
    case Action::Kind::Workspace:
        out.insert(QStringLiteral("type"), QStringLiteral("workspace"));
        out.insert(QStringLiteral("op"), enumName(kWorkspaceOps, action.workspaceOp));
        // `prev` / `next` / `move_prev` / `move_next` 是相对的：没有空间号可写（写了也没用）。
        if (action.workspaceOp == WorkspaceOp::Switch || action.workspaceOp == WorkspaceOp::Move) {
            out.insert(QStringLiteral("index"), action.workspaceIndex);
        }
        putBoolIf(action.follow, &out, "follow");
        break;
    case Action::Kind::Power:
        out.insert(QStringLiteral("type"), QStringLiteral("power"));
        out.insert(QStringLiteral("op"), powerOpName(action.powerOp));
        break;
    case Action::Kind::Suspend:
        out.insert(QStringLiteral("type"), QStringLiteral("suspend"));
        out.insert(QStringLiteral("state"), enumName(kToggleStates, action.toggleState));
        break;
    case Action::Kind::Reload:
        out.insert(QStringLiteral("type"), QStringLiteral("reload"));
        break;
    case Action::Kind::Quit:
        out.insert(QStringLiteral("type"), QStringLiteral("quit"));
        break;
    case Action::Kind::Noop:
        out.insert(QStringLiteral("type"), QStringLiteral("none"));
        break;
    }
    return out;
}

Action actionFromMap(const QVariantMap &map, QStringList *errors, const QString &label)
{
    Action action;
    const QString type = stringOr(map, "type");
    const auto note = [&](const QString &message) {
        if (errors != nullptr) {
            errors->append(QStringLiteral("%1: %2").arg(label, message));
        }
    };
    if (type == QLatin1String("run")) {
        action.kind = Action::Kind::Run;
        action.program = stringOr(map, "program");
        action.args = stringListOr(map, "args");
        action.cwd = optionalString(map, "cwd");
        action.show = showModeFromName(stringOr(map, "show", QStringLiteral("normal")))
                          .value_or(ShowMode::Normal);
        action.shell = boolOr(map, "shell", false);
        action.wait = boolOr(map, "wait", false);
        action.elevated = boolOr(map, "elevated", false);
        action.env = envFromMap(stringMapOr(map, "env"));
    } else if (type == QLatin1String("send")) {
        action.kind = Action::Kind::Send;
        action.keys = stringOr(map, "keys");
        action.delayMs = optionalU64(map, "delay_ms");
        action.releaseModifiers = optionalBool(map, "release_modifiers");
    } else if (type == QLatin1String("caps_lock")) {
        action.kind = Action::Kind::CapsLock;
        action.capsState = CapsLockState::Off;
    } else if (type == QLatin1String("type")) {
        action.kind = Action::Kind::Type;
        action.text = stringOr(map, "text");
        action.delayMs = optionalU64(map, "delay_ms");
        action.releaseModifiers = optionalBool(map, "release_modifiers");
    } else if (type == QLatin1String("open")) {
        action.kind = Action::Kind::Open;
        action.target = optionalString(map, "target");
        action.openArgs = optionalString(map, "args");
        action.cwd = optionalString(map, "cwd");
        action.show = showModeFromName(stringOr(map, "show", QStringLiteral("normal")))
                          .value_or(ShowMode::Normal);
    } else if (type == QLatin1String("volume")) {
        action.kind = Action::Kind::Volume;
        action.volumeOp = volumeOpFromName(stringOr(map, "op", QStringLiteral("up")))
                              .value_or(VolumeOp::Up);
        action.level = optionalU8(map, "level");
        action.step = optionalU8(map, "step");
    } else if (type == QLatin1String("media")) {
        action.kind = Action::Kind::Media;
        action.mediaOp = mediaOpFromName(stringOr(map, "op", QStringLiteral("play_pause")))
                             .value_or(MediaOp::PlayPause);
    } else if (type == QLatin1String("clipboard")) {
        action.kind = Action::Kind::Clipboard;
        action.clipboardOp = clipboardOpFromName(stringOr(map, "op", QStringLiteral("get")))
                                 .value_or(ClipboardOp::Get);
        action.clipboardText = optionalString(map, "text");
    } else if (type == QLatin1String("window")) {
        action.kind = Action::Kind::Window;
        action.windowOp = windowOpFromName(stringOr(map, "op", QStringLiteral("activate")))
                              .value_or(WindowOp::Activate);
        windowQueryFromMap(map, &action);
        action.toggle = optionalBool(map, "toggle");
        action.animate = optionalBool(map, "animate");
        action.follow = optionalBool(map, "follow");
        if (auto ref = monitorRefFromText(stringOr(map, "monitor"))) {
            action.monitor = *ref;
        }
        if (has(map, "launch")) {
            action.launch = launchFromMap(map.value(QStringLiteral("launch")).toMap(), true,
                                          &action.launchFields);
        }
        action.waitMs = optionalU64(map, "wait_ms");
    } else if (type == QLatin1String("notify")) {
        action.kind = Action::Kind::Notify;
        action.title = stringOr(map, "title");
        action.body = optionalString(map, "body");
    } else if (type == QLatin1String("desktop")) {
        action.kind = Action::Kind::Desktop;
        action.desktopSwitch = uintOr(map, "switch", 1);
    } else if (type == QLatin1String("menu")) {
        action.kind = Action::Kind::Menu;
        action.menuTitle = optionalString(map, "title");
        action.items = menuItemsFromMap(map.value(QStringLiteral("items")).toList(), errors, label);
    } else if (type == QLatin1String("help")) {
        action.kind = Action::Kind::Help;
        action.helpTitle = nonEmpty(optionalString(map, "title"));
    } else if (type == QLatin1String("windows")) {
        action.kind = Action::Kind::Windows;
        action.windowsTitle = nonEmpty(optionalString(map, "title"));
    } else if (type == QLatin1String("apps")) {
        action.kind = Action::Kind::Apps;
        action.appsTitle = nonEmpty(optionalString(map, "title"));
    } else if (type == QLatin1String("workspace")) {
        action.kind = Action::Kind::Workspace;
        action.workspaceOp = workspaceOpFromName(stringOr(map, "op", QStringLiteral("switch")))
                                 .value_or(WorkspaceOp::Switch);
        action.workspaceIndex = uintOr(map, "index", 1);
        action.follow = optionalBool(map, "follow");
    } else if (type == QLatin1String("power")) {
        action.kind = Action::Kind::Power;
        action.powerOp = powerOpFromName(stringOr(map, "op", QStringLiteral("sleep")))
                             .value_or(PowerOp::Sleep);
    } else if (type == QLatin1String("suspend")) {
        action.kind = Action::Kind::Suspend;
        action.toggleState = toggleStateFromName(stringOr(map, "state", QStringLiteral("toggle")))
                                 .value_or(ToggleState::Toggle);
    } else if (type == QLatin1String("reload")) {
        action.kind = Action::Kind::Reload;
    } else if (type == QLatin1String("quit")) {
        action.kind = Action::Kind::Quit;
    } else if (type == QLatin1String("none")) {
        action.kind = Action::Kind::Noop;
    } else {
        note(QStringLiteral("unknown action type `%1`").arg(type));
        action.kind = Action::Kind::Noop;
    }
    return action;
}

QStringList actionKindNames()
{
    return {QStringLiteral("run"),    QStringLiteral("send"),      QStringLiteral("type"),
            QStringLiteral("open"),   QStringLiteral("window"),    QStringLiteral("menu"),
            QStringLiteral("notify"), QStringLiteral("volume"),    QStringLiteral("media"),
            QStringLiteral("clipboard"), QStringLiteral("desktop"), QStringLiteral("caps_lock"),
            QStringLiteral("power"),  QStringLiteral("suspend"),   QStringLiteral("help"),
            QStringLiteral("windows"), QStringLiteral("apps"),      QStringLiteral("workspace"),
            QStringLiteral("reload"), QStringLiteral("quit"),     QStringLiteral("none")};
}

QVariantMap defaultActionMap(const QString &type)
{
    if (type == QLatin1String("run")) {
        return mapOf({{"type", QStringLiteral("run")},
                      {"program", QString()},
                      {"args", QVariantList()},
                      {"show", QStringLiteral("normal")},
                      {"shell", false},
                      {"wait", false},
                      {"elevated", false}});
    }
    if (type == QLatin1String("send")) {
        return mapOf({{"type", QStringLiteral("send")}, {"keys", QString()}});
    }
    if (type == QLatin1String("type")) {
        return mapOf({{"type", QStringLiteral("type")}, {"text", QString()}});
    }
    if (type == QLatin1String("open")) {
        return mapOf({{"type", QStringLiteral("open")},
                      {"target", QString()},
                      {"show", QStringLiteral("normal")}});
    }
    if (type == QLatin1String("window")) {
        return mapOf({{"type", QStringLiteral("window")},
                      {"op", QStringLiteral("activate")},
                      {"toggle", true}});
    }
    if (type == QLatin1String("menu")) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), QString());
        item.insert(QStringLiteral("actions"), QVariantList());
        QVariantList items;
        items.append(item);
        QVariantMap out;
        out.insert(QStringLiteral("type"), QStringLiteral("menu"));
        out.insert(QStringLiteral("items"), items);
        return out;
    }
    if (type == QLatin1String("notify")) {
        return mapOf({{"type", QStringLiteral("notify")}, {"title", QString()}});
    }
    if (type == QLatin1String("volume")) {
        return mapOf({{"type", QStringLiteral("volume")}, {"op", QStringLiteral("up")}});
    }
    if (type == QLatin1String("media")) {
        return mapOf({{"type", QStringLiteral("media")}, {"op", QStringLiteral("play_pause")}});
    }
    if (type == QLatin1String("clipboard")) {
        return mapOf({{"type", QStringLiteral("clipboard")}, {"op", QStringLiteral("get")}});
    }
    if (type == QLatin1String("desktop")) {
        return mapOf({{"type", QStringLiteral("desktop")}, {"switch", 1}});
    }
    if (type == QLatin1String("workspace")) {
        return mapOf({{"type", QStringLiteral("workspace")},
                      {"op", QStringLiteral("switch")},
                      {"index", 1}});
    }
    if (type == QLatin1String("power")) {
        return mapOf({{"type", QStringLiteral("power")}, {"op", QStringLiteral("sleep")}});
    }
    if (type == QLatin1String("suspend")) {
        return mapOf({{"type", QStringLiteral("suspend")}, {"state", QStringLiteral("toggle")}});
    }
    return mapOf({{"type", type}});
}

// ---------------------------------------------------------------------------
// 条目
// ---------------------------------------------------------------------------

QVariantList actionListToMap(const ActionSpec &spec)
{
    QVariantList out;
    switch (spec.kind) {
    case ActionSpec::Kind::One:
        out.append(actionToMap(spec.action));
        break;
    case ActionSpec::Kind::Short: {
        // 简写是给人写配置文件用的糖：界面里统一展开成等价的动作表（行为一字不差，
        // 只是写回文件时不再是简写）。
        Action parsed;
        if (!parseActionShorthand(spec.shorthand, &parsed).has_value()) {
            out.append(actionToMap(parsed));
        }
        break;
    }
    case ActionSpec::Kind::List:
        // 嵌套列表在界面里没有对应写法，直接拍平（行为完全一样）。
        for (const ActionSpec &item : spec.list) {
            const QVariantList nested = actionListToMap(item);
            for (const QVariant &value : nested) {
                out.append(value);
            }
        }
        break;
    }
    return out;
}

std::optional<ActionSpec> actionListFromMap(const QVariantList &list, QStringList *errors,
                                            const QString &label)
{
    const QList<QVariantMap> maps = mapListValue(list);
    if (maps.isEmpty()) {
        return std::nullopt;
    }
    if (maps.size() == 1) {
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = actionFromMap(maps.first(), errors, label);
        return spec;
    }
    ActionSpec spec;
    spec.kind = ActionSpec::Kind::List;
    int index = 0;
    for (const QVariantMap &map : maps) {
        ++index;
        ActionSpec item;
        item.kind = ActionSpec::Kind::One;
        item.action = actionFromMap(map, errors,
                                    QStringLiteral("%1 action #%2").arg(label).arg(index));
        spec.list.push_back(std::move(item));
    }
    return spec;
}

QVariantMap hotkeyToMap(const HotkeyDef &def)
{
    QVariantMap out;
    putIf(def.name, &out, "name");
    out.insert(QStringLiteral("keys"), stringListVariant(def.keys));
    if (def.trigger.has_value()) {
        out.insert(QStringLiteral("trigger"), triggerModeName(*def.trigger));
    }
    putBoolIf(def.swallow, &out, "swallow");
    if (def.action.has_value()) {
        out.insert(QStringLiteral("actions"), actionListToMap(*def.action));
    } else {
        out.insert(QStringLiteral("actions"), QVariantList());
    }
    if (def.onRelease.has_value()) {
        out.insert(QStringLiteral("release_actions"), actionListToMap(*def.onRelease));
    } else {
        out.insert(QStringLiteral("release_actions"), QVariantList());
    }
    if (def.repeat.has_value()) {
        if (def.repeat->kind == RepeatSpec::Kind::Flag) {
            out.insert(QStringLiteral("repeatable"), def.repeat->flag);
        } else {
            out.insert(QStringLiteral("repeatable"), true);
            putU32If(def.repeat->intervalMs, &out, "repeat_interval_ms");
            putU32If(def.repeat->delayMs, &out, "repeat_delay_ms");
        }
    } else {
        out.insert(QStringLiteral("repeatable"), false);
    }
    out.insert(QStringLiteral("enabled"), def.enabled);
    putIf(def.comment, &out, "comment");
    putBoolIf(def.remoteDesktop, &out, "remote_desktop");
    return out;
}

HotkeyDef hotkeyFromMap(const QVariantMap &map, QStringList *errors, const QString &label)
{
    HotkeyDef def;
    def.name = optionalString(map, "name");
    def.keys = stringListOr(map, "keys");
    if (has(map, "trigger")) {
        def.trigger = triggerModeFromName(stringOr(map, "trigger"));
    }
    def.swallow = optionalBool(map, "swallow");
    if (has(map, "actions")) {
        def.action = actionListFromMap(map.value(QStringLiteral("actions")).toList(), errors, label);
    }
    if (has(map, "release_actions")) {
        def.onRelease =
            actionListFromMap(map.value(QStringLiteral("release_actions")).toList(), errors,
                              QStringLiteral("%1 (on_release)").arg(label));
    }
    const bool repeatable = boolOr(map, "repeatable", false);
    if (repeatable) {
        const std::optional<std::uint32_t> interval = optionalUint(map, "repeat_interval_ms");
        const std::optional<std::uint32_t> delay = optionalUint(map, "repeat_delay_ms");
        if (interval.has_value() || delay.has_value()) {
            def.repeat = RepeatSpec::makeConfig(interval, delay);
        } else {
            def.repeat = RepeatSpec::makeFlag(true);
        }
    }
    def.enabled = boolOr(map, "enabled", true);
    def.comment = optionalString(map, "comment");
    def.remoteDesktop = optionalBool(map, "remote_desktop");
    return def;
}

QVariantMap remapToMap(const RemapDef &def)
{
    QVariantMap out;
    putIf(def.name, &out, "name");
    out.insert(QStringLiteral("from"), def.from);
    out.insert(QStringLiteral("to"), def.to);
    out.insert(QStringLiteral("mode"), enumName(kRemapModes, def.mode));
    putBoolIf(def.swallow, &out, "swallow");
    out.insert(QStringLiteral("enabled"), def.enabled);
    putBoolIf(def.remoteDesktop, &out, "remote_desktop");
    return out;
}

RemapDef remapFromMap(const QVariantMap &map)
{
    RemapDef def;
    def.name = optionalString(map, "name");
    def.from = stringOr(map, "from");
    def.to = stringOr(map, "to");
    def.mode = stringOr(map, "mode", QStringLiteral("hold")) == QLatin1String("tap")
                   ? RemapMode::Tap
                   : RemapMode::Hold;
    def.swallow = optionalBool(map, "swallow");
    def.enabled = boolOr(map, "enabled", true);
    def.remoteDesktop = optionalBool(map, "remote_desktop");
    return def;
}

QVariantMap windowRuleToMap(const WindowRuleDef &def)
{
    QVariantMap out;
    putIf(def.name, &out, "name");
    putIf(def.title, &out, "title");
    putIf(def.process, &out, "process");
    putU32If(def.desktop, &out, "desktop");
    putU32If(def.workspace, &out, "workspace");
    putBoolIf(def.allDesktops, &out, "all_desktops");
    putBoolIf(def.topmost, &out, "topmost");
    if (def.monitor.has_value()) {
        out.insert(QStringLiteral("monitor"), monitorRefValue(*def.monitor));
    }
    putBoolIf(def.maximize, &out, "maximize");
    putI32If(def.x, &out, "x");
    putI32If(def.y, &out, "y");
    putU32If(def.width, &out, "width");
    putU32If(def.height, &out, "height");
    out.insert(QStringLiteral("enabled"), def.enabled);
    return out;
}

WindowRuleDef windowRuleFromMap(const QVariantMap &map)
{
    WindowRuleDef def;
    def.name = optionalString(map, "name");
    def.title = optionalString(map, "title");
    def.process = optionalString(map, "process");
    def.desktop = optionalUint(map, "desktop");
    def.workspace = optionalUint(map, "workspace");
    def.allDesktops = optionalBool(map, "all_desktops");
    def.topmost = optionalBool(map, "topmost");
    if (auto ref = monitorRefFromText(stringOr(map, "monitor"))) {
        def.monitor = *ref;
    }
    def.maximize = optionalBool(map, "maximize");
    def.x = optionalI32(map, "x");
    def.y = optionalI32(map, "y");
    def.width = optionalUint(map, "width");
    def.height = optionalUint(map, "height");
    def.enabled = boolOr(map, "enabled", true);
    return def;
}

QVariantMap appToMap(const AppDef &def)
{
    QVariantMap out;
    putIf(def.name, &out, "name");
    putIf(def.process, &out, "process");
    putIf(def.title, &out, "title");
    out.insert(QStringLiteral("enabled"), def.enabled);
    putU32If(def.workspace, &out, "workspace");
    if (def.window.has_value()) {
        out.insert(QStringLiteral("window"), windowRuleToMap(*def.window));
    } else {
        out.insert(QStringLiteral("window"), QVariantMap());
    }
    if (def.launch.has_value()) {
        out.insert(QStringLiteral("launch"), launchToMap(*def.launch, LaunchFields{}, false));
    } else {
        out.insert(QStringLiteral("launch"), QVariantMap());
    }
    QVariantList hotkeys;
    for (const HotkeyDef &hotkey : def.hotkeys) {
        hotkeys.append(hotkeyToMap(hotkey));
    }
    out.insert(QStringLiteral("hotkeys"), hotkeys);
    return out;
}

AppDef appFromMap(const QVariantMap &map, QStringList *errors, const QString &label)
{
    AppDef def;
    def.name = optionalString(map, "name");
    def.process = optionalString(map, "process");
    def.title = optionalString(map, "title");
    def.enabled = boolOr(map, "enabled", true);
    def.workspace = optionalUint(map, "workspace");
    const QVariantMap window = map.value(QStringLiteral("window")).toMap();
    if (!window.isEmpty()) {
        def.window = windowRuleFromMap(window);
    }
    const QVariantMap launch = map.value(QStringLiteral("launch")).toMap();
    if (!launch.isEmpty()) {
        LaunchFields fields;
        def.launch = launchFromMap(launch, false, &fields);
    }
    const QList<QVariantMap> hotkeys = mapListOr(map, "hotkeys");
    int index = 0;
    for (const QVariantMap &hotkey : hotkeys) {
        ++index;
        def.hotkeys.push_back(hotkeyFromMap(
            hotkey, errors, QStringLiteral("%1 hotkey #%2").arg(label).arg(index)));
    }
    return def;
}

// ---------------------------------------------------------------------------
// 整份配置
// ---------------------------------------------------------------------------

QVariantMap configToMap(const Config &config)
{
    QVariantMap out;
    out.insert(QStringLiteral("settings"), settingsToMap(config.settings));
    QVariantList hotkeys;
    for (const HotkeyDef &hotkey : config.hotkeys) {
        hotkeys.append(hotkeyToMap(hotkey));
    }
    out.insert(QStringLiteral("hotkeys"), hotkeys);
    QVariantList remaps;
    for (const RemapDef &remap : config.remaps) {
        remaps.append(remapToMap(remap));
    }
    out.insert(QStringLiteral("remaps"), remaps);
    QVariantList windowRules;
    for (const WindowRuleDef &rule : config.windowRules) {
        windowRules.append(windowRuleToMap(rule));
    }
    out.insert(QStringLiteral("window_rules"), windowRules);
    QVariantList apps;
    for (const AppDef &app : config.apps) {
        apps.append(appToMap(app));
    }
    out.insert(QStringLiteral("apps"), apps);
    return out;
}

Config configFromMap(const QVariantMap &map, QStringList *errors)
{
    Config config;
    config.settings = settingsFromMap(map.value(QStringLiteral("settings")).toMap());

    const QList<QVariantMap> hotkeys = mapListOr(map, "hotkeys");
    int index = 0;
    for (const QVariantMap &hotkey : hotkeys) {
        ++index;
        config.hotkeys.push_back(
            hotkeyFromMap(hotkey, errors, QStringLiteral("hotkey #%1").arg(index)));
    }
    for (const QVariantMap &remap : mapListOr(map, "remaps")) {
        config.remaps.push_back(remapFromMap(remap));
    }
    for (const QVariantMap &rule : mapListOr(map, "window_rules")) {
        config.windowRules.push_back(windowRuleFromMap(rule));
    }
    index = 0;
    for (const QVariantMap &app : mapListOr(map, "apps")) {
        ++index;
        config.apps.push_back(
            appFromMap(app, errors, QStringLiteral("app #%1").arg(index)));
    }
    return config;
}

// ---------------------------------------------------------------------------
// 下拉框取值
// ---------------------------------------------------------------------------

QStringList logLevelNames()
{
    return {QStringLiteral("trace"), QStringLiteral("debug"), QStringLiteral("info"),
            QStringLiteral("warn"),  QStringLiteral("error"), QStringLiteral("off")};
}

QStringList inputBackendNames()
{
    return {QStringLiteral("auto"), QStringLiteral("user32"), QStringLiteral("ntuser")};
}

QStringList triggerModeNames()
{
    return {triggerModeName(TriggerMode::Press), triggerModeName(TriggerMode::Release),
            triggerModeName(TriggerMode::Repeat)};
}

QStringList remapModeNames()
{
    return {enumName(kRemapModes, RemapMode::Hold), enumName(kRemapModes, RemapMode::Tap)};
}

QStringList showModeNames()
{
    QStringList out;
    for (const EnumName<ShowMode> &entry : kShowModes) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList volumeOpNames()
{
    QStringList out;
    for (const EnumName<VolumeOp> &entry : kVolumeOps) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList mediaOpNames()
{
    QStringList out;
    for (const EnumName<MediaOp> &entry : kMediaOps) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList clipboardOpNames()
{
    QStringList out;
    for (const EnumName<ClipboardOp> &entry : kClipboardOps) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList windowOpNames()
{
    QStringList out;
    for (const EnumName<WindowOp> &entry : kWindowOps) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList powerOpNames()
{
    return {powerOpName(PowerOp::Sleep),     powerOpName(PowerOp::Hibernate),
            powerOpName(PowerOp::Shutdown),  powerOpName(PowerOp::Restart),
            powerOpName(PowerOp::Logoff),    powerOpName(PowerOp::Lock),
            powerOpName(PowerOp::ScreenOff)};
}

QStringList toggleStateNames()
{
    QStringList out;
    for (const EnumName<ToggleState> &entry : kToggleStates) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList workspaceOpNames()
{
    QStringList out;
    for (const EnumName<WorkspaceOp> &entry : kWorkspaceOps) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList workspaceSwitchTargetNames()
{
    QStringList out;
    for (const EnumName<WorkspaceSwitchTarget> &entry : kWorkspaceSwitchTargets) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

QStringList workspaceSwitchEffectNames()
{
    QStringList out;
    for (const EnumName<WorkspaceSwitchEffect> &entry : kWorkspaceSwitchEffects) {
        out.append(QString::fromLatin1(entry.text));
    }
    return out;
}

} // namespace flowkeyd::core
