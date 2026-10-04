// Lua 配置层：DSL 注入、脚本求值，以及「脚本里的表 → core::Config」的转换。
//
// 求值分两步：
//
// 1. 脚本本身报错（语法错、运行时错、`hotkey{}` 传了非表）→ 直接失败，
//    错误里带 `config.lua:行号`；
// 2. 每张表逐条目转换 → 失败不中断，收集成 `hotkey #3 (\`name\`): ...`
//    这样的信息，最后和 core::compile() 的校验错误一起报告。
//
// C++ 没有 serde 的 `deny_unknown_fields`，所以未知字段由本文件显式核对白名单。
#include "lua/lua_config.h"

#include "lua/lua_include.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QStringList>
#include <QVector>

#include <functional>
#include <limits>
#include <memory>
#include <utility>

// 静态库里的 qrc 会被链接器丢掉，除非有一个被引用的符号把它拉进来。
static void flowkeydEnsureLuaPreludeResource()
{
    Q_INIT_RESOURCE(lua_prelude);
}

namespace flowkeyd::lua {

namespace {

namespace core = flowkeyd::core;

constexpr const char *kPreludeResource = ":/lua/lua_prelude.lua";

/// 逐条目转换时抛出的错误；调用方会加上条目名再收集。
struct ConvError
{
    QString message;
};

[[noreturn]] void fail(const QString &message)
{
    throw ConvError{message};
}

// ---------------------------------------------------------------------------
// Lua 小工具
// ---------------------------------------------------------------------------

QString luaTypeName(int type)
{
    switch (type) {
    case LUA_TNIL:
        return QStringLiteral("nil");
    case LUA_TBOOLEAN:
        return QStringLiteral("boolean");
    case LUA_TNUMBER:
        return QStringLiteral("number");
    case LUA_TSTRING:
        return QStringLiteral("string");
    case LUA_TTABLE:
        return QStringLiteral("table");
    case LUA_TFUNCTION:
        return QStringLiteral("function");
    case LUA_TUSERDATA:
        return QStringLiteral("userdata");
    case LUA_TLIGHTUSERDATA:
        return QStringLiteral("userdata");
    case LUA_TTHREAD:
        return QStringLiteral("thread");
    default:
        return QStringLiteral("value");
    }
}

int tracebackHandler(lua_State *L)
{
    const char *message = lua_tostring(L, 1);
    if (message == nullptr) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
            return 1;
        }
        message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, message, 1);
    return 1;
}

/// 取出字段值压栈；字段不存在或是 nil 时返回 false（不压栈）。
bool pushField(lua_State *L, int index, const char *field, int *outIndex)
{
    lua_getfield(L, index, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    *outIndex = lua_gettop(L);
    return true;
}

bool hasField(lua_State *L, int index, const char *field)
{
    lua_getfield(L, index, field);
    const bool present = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return present;
}

QString rawStringAt(lua_State *L, int index)
{
    size_t length = 0;
    const char *data = lua_tolstring(L, index, &length);
    if (data == nullptr) {
        return QString();
    }
    return QString::fromUtf8(data, static_cast<int>(length));
}

QString joinQuoted(const QStringList &names)
{
    QStringList quoted;
    quoted.reserve(names.size());
    for (const QString &name : names) {
        quoted.append(QStringLiteral("`%1`").arg(name));
    }
    return quoted.join(QStringLiteral(", "));
}

QString expectedList(const QStringList &names)
{
    if (names.size() == 1) {
        return QStringLiteral("expected `%1`").arg(names.first());
    }
    return QStringLiteral("expected one of %1").arg(joinQuoted(names));
}

/// 表里出现过的命名键，以及列表条目的数量。
struct TableShape
{
    QStringList named;
    int listCount = 0;
};

TableShape shapeOf(lua_State *L, int index)
{
    TableShape shape;
    const int absolute = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        // 栈上是 key, value
        if (lua_type(L, -2) == LUA_TSTRING) {
            shape.named.append(rawStringAt(L, -2));
        } else if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)
                   && lua_tointeger(L, -2) >= 1) {
            shape.listCount += 1;
        } else {
            shape.named.append(QStringLiteral("<%1 key>").arg(luaTypeName(lua_type(L, -2))));
        }
        lua_pop(L, 1);
    }
    return shape;
}

/// 严格白名单：未知命名键与列表条目都要报错（不变量 6）。
void checkFields(lua_State *L, int index, const QStringList &allowed)
{
    const TableShape shape = shapeOf(L, index);
    for (const QString &name : shape.named) {
        if (!allowed.contains(name)) {
            fail(QStringLiteral("unknown field `%1`, %2").arg(name, expectedList(allowed)));
        }
    }
    if (shape.listCount > 0) {
        fail(QStringLiteral("invalid type: sequence, expected a map with the field(s) %1")
                 .arg(joinQuoted(allowed)));
    }
}

// ---------------------------------------------------------------------------
// 标量字段
// ---------------------------------------------------------------------------

std::optional<QString> optString(lua_State *L, int index, const char *field)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return std::nullopt;
    }
    if (lua_type(L, v) != LUA_TSTRING) {
        const QString type = luaTypeName(lua_type(L, v));
        lua_pop(L, 1);
        fail(QStringLiteral("invalid type: %1, expected a string").arg(type));
    }
    const QString value = rawStringAt(L, v);
    lua_pop(L, 1);
    return value;
}

QString reqString(lua_State *L, int index, const char *field)
{
    const std::optional<QString> value = optString(L, index, field);
    if (!value.has_value()) {
        fail(QStringLiteral("missing field `%1`").arg(QString::fromLatin1(field)));
    }
    return *value;
}

std::optional<bool> optBool(lua_State *L, int index, const char *field)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return std::nullopt;
    }
    if (lua_type(L, v) != LUA_TBOOLEAN) {
        const QString type = luaTypeName(lua_type(L, v));
        lua_pop(L, 1);
        fail(QStringLiteral("invalid type: %1, expected a boolean").arg(type));
    }
    const bool value = lua_toboolean(L, v) != 0;
    lua_pop(L, 1);
    return value;
}

std::optional<qint64> optInteger(lua_State *L, int index, const char *field,
                                 qint64 min, qint64 max, const QString &expected)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return std::nullopt;
    }
    if (lua_type(L, v) != LUA_TNUMBER || !lua_isinteger(L, v)) {
        const QString text = lua_type(L, v) == LUA_TNUMBER
                                 ? QString::number(lua_tonumber(L, v))
                                 : luaTypeName(lua_type(L, v));
        lua_pop(L, 1);
        fail(QStringLiteral("invalid type: %1, expected %2").arg(text, expected));
    }
    const qint64 value = static_cast<qint64>(lua_tointeger(L, v));
    lua_pop(L, 1);
    if (value < min || value > max) {
        fail(QStringLiteral("invalid value: integer `%1`, expected %2").arg(value).arg(expected));
    }
    return value;
}

std::optional<quint32> optU32(lua_State *L, int index, const char *field)
{
    const auto value = optInteger(L, index, field, 0, 4294967295LL, QStringLiteral("u32"));
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<quint32>(*value);
}

std::optional<quint64> optU64(lua_State *L, int index, const char *field)
{
    const auto value = optInteger(L, index, field, 0, std::numeric_limits<qint64>::max(),
                                  QStringLiteral("u64"));
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<quint64>(*value);
}

std::optional<quint8> optU8(lua_State *L, int index, const char *field)
{
    const auto value = optInteger(L, index, field, 0, 255, QStringLiteral("u8"));
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<quint8>(*value);
}

std::optional<qint32> optI32(lua_State *L, int index, const char *field)
{
    const auto value = optInteger(L, index, field, -2147483648LL, 2147483647LL, QStringLiteral("i32"));
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<qint32>(*value);
}

// ---------------------------------------------------------------------------
// 枚举字段
// ---------------------------------------------------------------------------

QString enumName(lua_State *L, int index, const char *field,
                 const QStringList &allowed, const QString &fallback)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return fallback;
    }
    if (lua_type(L, v) != LUA_TSTRING) {
        const QString type = luaTypeName(lua_type(L, v));
        lua_pop(L, 1);
        fail(QStringLiteral("invalid type: %1, expected one of %2").arg(type, joinQuoted(allowed)));
    }
    const QString value = rawStringAt(L, v);
    lua_pop(L, 1);
    if (!allowed.contains(value)) {
        fail(QStringLiteral("unknown variant `%1`, expected one of %2").arg(value, joinQuoted(allowed)));
    }
    return value;
}

// ---------------------------------------------------------------------------
// 列表 / 映射字段
// ---------------------------------------------------------------------------

/// 一个 Lua 表当作字符串列表读取。空表是合法的空列表。
QStringList stringListValue(lua_State *L, int index)
{
    if (lua_type(L, index) != LUA_TTABLE) {
        fail(QStringLiteral("invalid type: %1, expected a sequence").arg(luaTypeName(lua_type(L, index))));
    }
    const TableShape shape = shapeOf(L, index);
    if (!shape.named.isEmpty()) {
        fail(QStringLiteral("invalid type: map, expected a sequence of strings"));
    }
    QStringList out;
    const lua_Unsigned length = lua_rawlen(L, index);
    for (lua_Unsigned i = 1; i <= length; ++i) {
        lua_rawgeti(L, index, static_cast<lua_Integer>(i));
        if (lua_type(L, -1) != LUA_TSTRING) {
            const QString type = luaTypeName(lua_type(L, -1));
            lua_pop(L, 1);
            fail(QStringLiteral("invalid type: %1, expected a string (list entry %2)").arg(type).arg(i));
        }
        out.append(rawStringAt(L, -1));
        lua_pop(L, 1);
    }
    return out;
}

std::optional<QStringList> optStringList(lua_State *L, int index, const char *field)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return std::nullopt;
    }
    const QStringList value = stringListValue(L, v);
    lua_pop(L, 1);
    return value;
}

/// `OneOrMany<String>`：字符串，或者字符串列表。
QStringList oneOrManyStrings(lua_State *L, int index, const char *field)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        fail(QStringLiteral("missing field `%1`").arg(QString::fromLatin1(field)));
    }
    if (lua_type(L, v) == LUA_TSTRING) {
        const QString value = rawStringAt(L, v);
        lua_pop(L, 1);
        return QStringList{value};
    }
    if (lua_type(L, v) == LUA_TTABLE) {
        const QStringList value = stringListValue(L, v);
        lua_pop(L, 1);
        return value;
    }
    const QString type = luaTypeName(lua_type(L, v));
    lua_pop(L, 1);
    fail(QStringLiteral("invalid type: %1, expected a string or a list of strings").arg(type));
}

/// `BTreeMap<String, String>`。
QMap<QString, QString> stringMapValue(lua_State *L, int index)
{
    if (lua_type(L, index) != LUA_TTABLE) {
        fail(QStringLiteral("invalid type: %1, expected a map").arg(luaTypeName(lua_type(L, index))));
    }
    const TableShape shape = shapeOf(L, index);
    if (shape.listCount > 0) {
        fail(QStringLiteral("invalid type: sequence, expected a map"));
    }
    if (shape.named.isEmpty()) {
        // Lua 里 `{}` 既是空列表也是空表：这里的字段要的是命名键。
        fail(QStringLiteral(
            "invalid type: sequence, expected a map (in Lua an empty `{}` counts as an empty "
            "list; this field wants named keys — fill it in, or leave it out and the default "
            "applies)"));
    }
    QMap<QString, QString> out;
    const int absolute = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        // key, value
        if (lua_type(L, -2) == LUA_TSTRING) {
            const QString key = rawStringAt(L, -2);
            if (lua_type(L, -1) != LUA_TSTRING) {
                const QString type = luaTypeName(lua_type(L, -1));
                lua_pop(L, 2);
                fail(QStringLiteral("invalid type: %1, expected a string for `env.%2`").arg(type, key));
            }
            out.insert(key, rawStringAt(L, -1));
        }
        lua_pop(L, 1);
    }
    return out;
}

std::optional<QMap<QString, QString>> optStringMap(lua_State *L, int index, const char *field)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return std::nullopt;
    }
    const QMap<QString, QString> value = stringMapValue(L, v);
    lua_pop(L, 1);
    return value;
}

// ---------------------------------------------------------------------------
// 复杂字段的前置声明
// ---------------------------------------------------------------------------

core::ActionSpec convertActionSpec(lua_State *L, int index);
core::Action convertAction(lua_State *L, int index);
core::RepeatSpec convertRepeatSpec(lua_State *L, int index);
core::LaunchSpec convertLaunchSpec(lua_State *L, int index, core::LaunchFields *fields);
core::MenuItemDef convertMenuItem(lua_State *L, int index);
core::MonitorRef convertMonitorRef(lua_State *L, int index);

std::optional<core::ActionSpec> optActionSpec(lua_State *L, int index, const char *field)
{
    int v = 0;
    if (!pushField(L, index, field, &v)) {
        return std::nullopt;
    }
    core::ActionSpec spec = convertActionSpec(L, v);
    lua_pop(L, 1);
    return spec;
}

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

core::Action convertAction(lua_State *L, int index)
{
    int v = 0;
    if (!pushField(L, index, "type", &v)) {
        fail(QStringLiteral("missing field `type` in an action table"));
    }
    if (lua_type(L, v) != LUA_TSTRING) {
        const QString type = luaTypeName(lua_type(L, v));
        lua_pop(L, 1);
        fail(QStringLiteral("invalid type: %1, expected a string for `type`").arg(type));
    }
    const QString type = rawStringAt(L, v);
    lua_pop(L, 1);

    core::Action action;
    if (type == QLatin1String("run")) {
        action.kind = core::Action::Kind::Run;
        action.program = reqString(L, index, "program");
        action.args = optStringList(L, index, "args").value_or(QStringList());
        action.cwd = optString(L, index, "cwd");
        action.show = *core::showModeFromName(
            enumName(L, index, "show", {"normal", "hidden", "minimized", "maximized"}, QStringLiteral("normal")));
        action.shell = optBool(L, index, "shell").value_or(false);
        action.wait = optBool(L, index, "wait").value_or(false);
        action.env = optStringMap(L, index, "env").value_or(QMap<QString, QString>());
    } else if (type == QLatin1String("send")) {
        action.kind = core::Action::Kind::Send;
        action.keys = reqString(L, index, "keys");
        action.delayMs = optU64(L, index, "delay_ms");
        action.releaseModifiers = optBool(L, index, "release_modifiers");
    } else if (type == QLatin1String("caps_lock")) {
        action.kind = core::Action::Kind::CapsLock;
        enumName(L, index, "state", {"off"}, QStringLiteral("off"));
        action.capsState = core::CapsLockState::Off;
    } else if (type == QLatin1String("type")) {
        action.kind = core::Action::Kind::Type;
        action.text = reqString(L, index, "text");
        action.delayMs = optU64(L, index, "delay_ms");
        action.releaseModifiers = optBool(L, index, "release_modifiers");
    } else if (type == QLatin1String("open")) {
        action.kind = core::Action::Kind::Open;
        action.target = reqString(L, index, "target");
        action.openArgs = optString(L, index, "args");
        action.cwd = optString(L, index, "cwd");
        action.show = *core::showModeFromName(
            enumName(L, index, "show", {"normal", "hidden", "minimized", "maximized"}, QStringLiteral("normal")));
    } else if (type == QLatin1String("volume")) {
        action.kind = core::Action::Kind::Volume;
        action.volumeOp = *core::volumeOpFromName(
            enumName(L, index, "op", {"up", "down", "set", "mute", "unmute", "toggle"}, QStringLiteral("up")));
        action.level = optU8(L, index, "level");
        action.step = optU8(L, index, "step");
    } else if (type == QLatin1String("media")) {
        action.kind = core::Action::Kind::Media;
        action.mediaOp = *core::mediaOpFromName(
            enumName(L, index, "op", {"play_pause", "next", "prev", "stop"}, QStringLiteral("play_pause")));
    } else if (type == QLatin1String("clipboard")) {
        action.kind = core::Action::Kind::Clipboard;
        action.clipboardOp = *core::clipboardOpFromName(
            enumName(L, index, "op", {"get", "set", "append", "clear"}, QStringLiteral("get")));
        action.clipboardText = optString(L, index, "text");
    } else if (type == QLatin1String("window")) {
        action.kind = core::Action::Kind::Window;
        action.windowOp = *core::windowOpFromName(enumName(
            L, index, "op",
            {"activate", "minimize", "maximize", "restore", "close", "toggle_topmost",
             "move_prev_desktop", "move_next_desktop", "move_left_monitor", "move_right_monitor"},
            QStringLiteral("activate")));
        action.target = optString(L, index, "target");
        action.process = optString(L, index, "process");
        action.toggle = optBool(L, index, "toggle");
        action.animate = optBool(L, index, "animate");
        action.follow = optBool(L, index, "follow");
        int launch = 0;
        if (pushField(L, index, "launch", &launch)) {
            if (lua_type(L, launch) != LUA_TTABLE) {
                const QString launchType = luaTypeName(lua_type(L, launch));
                lua_pop(L, 1);
                fail(QStringLiteral("invalid type: %1, expected a table with the launch fields "
                                    "(`program`, `args`, ...)")
                         .arg(launchType));
            }
            action.launch = convertLaunchSpec(L, launch, &action.launchFields);
            lua_pop(L, 1);
        }
        // `wait_ms` 是 `launch.wait_ms` 的简写（与 app 的 launch 合并后生效）。
        action.waitMs = optU64(L, index, "wait_ms");
    } else if (type == QLatin1String("notify")) {
        action.kind = core::Action::Kind::Notify;
        action.title = reqString(L, index, "title");
        action.body = optString(L, index, "body");
    } else if (type == QLatin1String("desktop")) {
        action.kind = core::Action::Kind::Desktop;
        const auto number = optInteger(L, index, "switch", 0, 4294967295LL, QStringLiteral("u32"));
        if (!number.has_value()) {
            fail(QStringLiteral("missing field `switch`"));
        }
        action.desktopSwitch = static_cast<quint32>(*number);
    } else if (type == QLatin1String("menu")) {
        action.kind = core::Action::Kind::Menu;
        action.menuTitle = optString(L, index, "title");
        int items = 0;
        if (!pushField(L, index, "items", &items)) {
            fail(QStringLiteral("missing field `items`"));
        }
        if (lua_type(L, items) != LUA_TTABLE) {
            const QString itemType = luaTypeName(lua_type(L, items));
            lua_pop(L, 1);
            fail(QStringLiteral("invalid type: %1, expected a list of menu items").arg(itemType));
        }
        const lua_Unsigned length = lua_rawlen(L, items);
        for (lua_Unsigned i = 1; i <= length; ++i) {
            lua_rawgeti(L, items, static_cast<lua_Integer>(i));
            if (lua_type(L, -1) != LUA_TTABLE) {
                const QString itemType = luaTypeName(lua_type(L, -1));
                lua_pop(L, 1);
                fail(QStringLiteral("invalid type: %1, expected a menu item table (entry %2)")
                         .arg(itemType)
                         .arg(i));
            }
            action.items.push_back(convertMenuItem(L, lua_gettop(L)));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    } else if (type == QLatin1String("help")) {
        action.kind = core::Action::Kind::Help;
        action.helpTitle = optString(L, index, "title");
    } else if (type == QLatin1String("windows")) {
        action.kind = core::Action::Kind::Windows;
        action.windowsTitle = optString(L, index, "title");
    } else if (type == QLatin1String("power")) {
        action.kind = core::Action::Kind::Power;
        action.powerOp = *core::powerOpFromName(
            enumName(L, index, "op",
                     {"sleep", "hibernate", "shutdown", "restart", "logoff", "lock", "screen_off",
                      "monitor_off", "display_off"},
                     QStringLiteral("sleep")));
    } else if (type == QLatin1String("suspend")) {
        action.kind = core::Action::Kind::Suspend;
        action.toggleState = *core::toggleStateFromName(
            enumName(L, index, "state", {"on", "off", "toggle"}, QStringLiteral("toggle")));
    } else if (type == QLatin1String("reload")) {
        action.kind = core::Action::Kind::Reload;
    } else if (type == QLatin1String("quit")) {
        action.kind = core::Action::Kind::Quit;
    } else if (type == QLatin1String("none")) {
        action.kind = core::Action::Kind::Noop;
    } else {
        fail(QStringLiteral("unknown variant `%1`, expected one of %2")
                 .arg(type,
                      joinQuoted({QStringLiteral("run"), QStringLiteral("send"),
                                  QStringLiteral("caps_lock"), QStringLiteral("type"),
                                  QStringLiteral("open"), QStringLiteral("volume"),
                                  QStringLiteral("media"), QStringLiteral("clipboard"),
                                  QStringLiteral("window"), QStringLiteral("notify"),
                                  QStringLiteral("desktop"), QStringLiteral("menu"),
                                  QStringLiteral("help"), QStringLiteral("windows"),
                                  QStringLiteral("power"),
                                  QStringLiteral("suspend"), QStringLiteral("reload"),
                                  QStringLiteral("quit"), QStringLiteral("none")})));
    }
    return action;
}

core::LaunchSpec convertLaunchSpec(lua_State *L, int index, core::LaunchFields *fields)
{
    checkFields(L, index,
                {"program", "args", "cwd", "show", "shell", "env", "wait_ms"});
    if (fields != nullptr) {
        // 记下显式写了哪些字段：app 的 `launch` 与动作的 `launch` 要逐字段合并，
        // 而 `show` / `shell` / `args` 的默认值与“没写”在值上分不开。
        fields->program = hasField(L, index, "program");
        fields->args = hasField(L, index, "args");
        fields->cwd = hasField(L, index, "cwd");
        fields->show = hasField(L, index, "show");
        fields->shell = hasField(L, index, "shell");
        fields->env = hasField(L, index, "env");
        fields->waitMs = hasField(L, index, "wait_ms");
    }
    core::LaunchSpec spec;
    spec.program = reqString(L, index, "program");
    spec.args = optStringList(L, index, "args").value_or(QStringList());
    spec.cwd = optString(L, index, "cwd");
    spec.show = *core::showModeFromName(
        enumName(L, index, "show", {"normal", "hidden", "minimized", "maximized"}, QStringLiteral("normal")));
    spec.shell = optBool(L, index, "shell").value_or(false);
    spec.env = optStringMap(L, index, "env").value_or(QMap<QString, QString>());
    spec.waitMs = optU64(L, index, "wait_ms");
    return spec;
}

core::MenuItemDef convertMenuItem(lua_State *L, int index)
{
    checkFields(L, index, {"key", "label", "hint", "action"});
    core::MenuItemDef item;
    item.key = optString(L, index, "key");
    item.label = reqString(L, index, "label");
    item.hint = optString(L, index, "hint");
    if (auto action = optActionSpec(L, index, "action")) {
        item.action = std::make_shared<core::ActionSpec>(std::move(*action));
    }
    return item;
}

core::ActionSpec convertActionSpec(lua_State *L, int index)
{
    core::ActionSpec spec;
    const int type = lua_type(L, index);
    if (type == LUA_TSTRING) {
        spec.kind = core::ActionSpec::Kind::Short;
        spec.shorthand = rawStringAt(L, index);
        return spec;
    }
    if (type == LUA_TFUNCTION) {
        fail(QStringLiteral("an action cannot be a Lua function; actions are declarative "
                            "(use send(\"^{c}\"), run(\"notepad.exe\"), notify(\"done\"), ...), "
                            "see README.md"));
    }
    if (type != LUA_TTABLE) {
        fail(QStringLiteral("invalid type: %1, expected a table, a list of tables or a shorthand string")
                 .arg(luaTypeName(type)));
    }

    const TableShape shape = shapeOf(L, index);
    const bool hasType = hasField(L, index, "type");
    if (hasType) {
        spec.kind = core::ActionSpec::Kind::One;
        spec.action = convertAction(L, index);
        return spec;
    }
    if (shape.listCount > 0) {
        spec.kind = core::ActionSpec::Kind::List;
        const lua_Unsigned length = lua_rawlen(L, index);
        for (lua_Unsigned i = 1; i <= length; ++i) {
            lua_rawgeti(L, index, static_cast<lua_Integer>(i));
            spec.list.push_back(convertActionSpec(L, lua_gettop(L)));
            lua_pop(L, 1);
        }
        return spec;
    }
    if (shape.named.isEmpty()) {
        // Lua 里 `{}` 是空列表：留到 flatten() 里报 “an empty action list does nothing”。
        spec.kind = core::ActionSpec::Kind::List;
        return spec;
    }
    fail(QStringLiteral(
        "data did not match any action form: a table needs a `type` field, or it must be a list "
        "of actions"));
}

core::RepeatSpec convertRepeatSpec(lua_State *L, int index)
{
    if (lua_type(L, index) == LUA_TBOOLEAN) {
        return core::RepeatSpec::makeFlag(lua_toboolean(L, index) != 0);
    }
    if (lua_type(L, index) == LUA_TTABLE) {
        checkFields(L, index, {"interval_ms", "delay_ms"});
        return core::RepeatSpec::makeConfig(optU32(L, index, "interval_ms"),
                                            optU32(L, index, "delay_ms"));
    }
    fail(QStringLiteral(
        "invalid type: %1, expected `true`, `false` or a table with `interval_ms` / `delay_ms`")
             .arg(luaTypeName(lua_type(L, index))));
}

// ---------------------------------------------------------------------------
// 条目
// ---------------------------------------------------------------------------

core::Settings convertSettings(lua_State *L, int index)
{
    checkFields(L, index,
                {"log_level", "swallow", "exact_modifiers", "release_modifiers",
                 "repeat_interval_ms", "repeat_delay_ms", "tick_ms", "input_backend",
                 "single_instance", "elevate", "remote_desktop"});
    core::Settings settings;
    if (auto value = optString(L, index, "log_level")) {
        settings.logLevel = *value;
    }
    if (auto value = optBool(L, index, "swallow")) {
        settings.swallow = *value;
    }
    if (auto value = optBool(L, index, "exact_modifiers")) {
        settings.exactModifiers = *value;
    }
    if (auto value = optBool(L, index, "release_modifiers")) {
        settings.releaseModifiers = *value;
    }
    if (auto value = optU32(L, index, "repeat_interval_ms")) {
        settings.repeatIntervalMs = *value;
    }
    if (auto value = optU32(L, index, "repeat_delay_ms")) {
        settings.repeatDelayMs = *value;
    }
    if (auto value = optU32(L, index, "tick_ms")) {
        settings.tickMs = *value;
    }
    if (auto value = optString(L, index, "input_backend")) {
        settings.inputBackend = *value;
    }
    if (auto value = optBool(L, index, "single_instance")) {
        settings.singleInstance = *value;
    }
    if (auto value = optBool(L, index, "elevate")) {
        settings.elevate = *value;
    }
    // `remote_desktop = true|false`，或者一张表：
    //
    //   remote_desktop = { enabled = true, processes = { "mstsc.exe" } }
    //
    // 后者才能改进程名单（写了就整体替掉内置名单，空表 = 谁都不算）。
    if (hasField(L, index, "remote_desktop")) {
        int v = 0;
        pushField(L, index, "remote_desktop", &v);
        if (lua_type(L, v) == LUA_TBOOLEAN) {
            settings.remoteDesktop = lua_toboolean(L, v) != 0;
        } else if (lua_type(L, v) == LUA_TTABLE) {
            checkFields(L, v, {"enabled", "processes"});
            if (auto value = optBool(L, v, "enabled")) {
                settings.remoteDesktop = *value;
            }
            if (hasField(L, v, "processes")) {
                settings.remoteDesktopProcesses = oneOrManyStrings(L, v, "processes");
            }
        } else {
            const QString type = luaTypeName(lua_type(L, v));
            lua_pop(L, 1);
            fail(QStringLiteral("remote_desktop: invalid type: %1, expected `true`, `false` or a "
                                "table with `enabled` / `processes`")
                     .arg(type));
        }
        lua_pop(L, 1);
    }
    return settings;
}

core::HotkeyDef convertHotkey(lua_State *L, int index)
{
    checkFields(L, index,
                {"name", "keys", "trigger", "swallow", "action", "press", "on_press",
                 "on_release", "repeat", "repeatable", "enabled", "comment", "remote_desktop"});
    core::HotkeyDef hotkey;
    hotkey.name = optString(L, index, "name");
    hotkey.keys = oneOrManyStrings(L, index, "keys");
    if (auto trigger = optString(L, index, "trigger")) {
        const auto mode = core::triggerModeFromName(*trigger);
        if (!mode.has_value()) {
            fail(QStringLiteral("unknown variant `%1`, expected one of %2")
                     .arg(*trigger, joinQuoted({QStringLiteral("press"), QStringLiteral("release"),
                                                QStringLiteral("repeat")})));
        }
        hotkey.trigger = *mode;
    }
    hotkey.swallow = optBool(L, index, "swallow");

    const bool hasAction = hasField(L, index, "action");
    const bool hasPress = hasField(L, index, "press");
    const bool hasOnPress = hasField(L, index, "on_press");
    if (static_cast<int>(hasAction) + static_cast<int>(hasPress) + static_cast<int>(hasOnPress) > 1) {
        fail(QStringLiteral("duplicate field `action` (it was also given as `press` or `on_press`)"));
    }
    if (hasAction) {
        hotkey.action = optActionSpec(L, index, "action");
    } else if (hasPress) {
        hotkey.action = optActionSpec(L, index, "press");
    } else if (hasOnPress) {
        hotkey.action = optActionSpec(L, index, "on_press");
    }
    if (hasField(L, index, "on_release")) {
        hotkey.onRelease = optActionSpec(L, index, "on_release");
    }

    const bool hasRepeat = hasField(L, index, "repeat");
    const bool hasRepeatable = hasField(L, index, "repeatable");
    if (hasRepeat && hasRepeatable) {
        fail(QStringLiteral("duplicate field `repeat` (it was also given as `repeatable`)"));
    }
    const char *repeatField = hasRepeat ? "repeat" : (hasRepeatable ? "repeatable" : nullptr);
    if (repeatField != nullptr) {
        int v = 0;
        pushField(L, index, repeatField, &v);
        hotkey.repeat = convertRepeatSpec(L, v);
        lua_pop(L, 1);
    }

    hotkey.enabled = optBool(L, index, "enabled").value_or(true);
    hotkey.comment = optString(L, index, "comment");
    hotkey.remoteDesktop = optBool(L, index, "remote_desktop");
    return hotkey;
}

core::RemapDef convertRemap(lua_State *L, int index)
{
    checkFields(L, index, {"name", "from", "to", "mode", "swallow", "enabled", "remote_desktop"});
    core::RemapDef remap;
    remap.name = optString(L, index, "name");
    remap.from = reqString(L, index, "from");
    remap.to = reqString(L, index, "to");
    const QString mode = enumName(L, index, "mode", {"hold", "tap"}, QStringLiteral("hold"));
    remap.mode = mode == QLatin1String("tap") ? core::RemapMode::Tap : core::RemapMode::Hold;
    remap.swallow = optBool(L, index, "swallow");
    remap.enabled = optBool(L, index, "enabled").value_or(true);
    remap.remoteDesktop = optBool(L, index, "remote_desktop");
    return remap;
}

core::MonitorRef convertMonitorRef(lua_State *L, int index)
{
    core::MonitorRef ref;
    if (lua_type(L, index) == LUA_TNUMBER && lua_isinteger(L, index)) {
        const qint64 value = static_cast<qint64>(lua_tointeger(L, index));
        if (value < 1 || value > 4294967295LL) {
            fail(QStringLiteral("invalid value: integer `%1`, expected a monitor number >= 1")
                     .arg(value));
        }
        ref.kind = core::MonitorRef::Kind::Index;
        ref.index = static_cast<quint32>(value);
        return ref;
    }
    if (lua_type(L, index) == LUA_TSTRING) {
        const QString text = rawStringAt(L, index).trimmed();
        if (text.isEmpty()) {
            fail(QStringLiteral("`monitor` must not be empty"));
        }
        if (text.compare(QLatin1String("primary"), Qt::CaseInsensitive) == 0) {
            ref.kind = core::MonitorRef::Kind::Primary;
            return ref;
        }
        ref.kind = core::MonitorRef::Kind::Device;
        ref.device = text;
        return ref;
    }
    fail(QStringLiteral("invalid type: %1, expected a monitor number, \"primary\" or a device "
                        "name like \"DISPLAY2\"")
             .arg(luaTypeName(lua_type(L, index))));
}

core::WindowRuleDef convertWindowRule(lua_State *L, int index)
{
    checkFields(L, index,
                {"name", "title", "process", "desktop", "all_desktops", "monitor", "maximize",
                 "topmost", "x", "y", "width", "height", "enabled"});
    core::WindowRuleDef rule;
    rule.name = optString(L, index, "name");
    rule.title = optString(L, index, "title");
    rule.process = optString(L, index, "process");
    rule.desktop = optU32(L, index, "desktop");
    rule.allDesktops = optBool(L, index, "all_desktops");
    rule.maximize = optBool(L, index, "maximize");
    rule.topmost = optBool(L, index, "topmost");
    rule.x = optI32(L, index, "x");
    rule.y = optI32(L, index, "y");
    rule.width = optU32(L, index, "width");
    rule.height = optU32(L, index, "height");
    rule.enabled = optBool(L, index, "enabled").value_or(true);
    int monitor = 0;
    if (pushField(L, index, "monitor", &monitor)) {
        rule.monitor = convertMonitorRef(L, monitor);
        lua_pop(L, 1);
    }
    return rule;
}

// ---------------------------------------------------------------------------
// 混合表检查
// ---------------------------------------------------------------------------

/// Lua 的表既是数组也是哈希表，而「读成列表」会**静默忽略**命名键。
/// TOML 时代这种写法根本写不出来，所以这里把混合表显式报错。
void checkMixedTables(lua_State *L, int index, const QString &path,
                      QVector<const void *> *seen, QStringList *errors)
{
    if (lua_type(L, index) != LUA_TTABLE) {
        return;
    }
    const void *pointer = lua_topointer(L, index);
    if (seen->contains(pointer)) {
        return;
    }
    seen->append(pointer);

    const int absolute = lua_absindex(L, index);
    QStringList named;
    int listEntries = 0;

    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        // 栈上是 key, value；在 value 上递归不会打乱 lua_next 的游标。
        QString childPath;
        if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2) && lua_tointeger(L, -2) >= 1) {
            listEntries += 1;
            childPath = QStringLiteral("%1[%2]").arg(path).arg(lua_tointeger(L, -2));
        } else {
            QString name;
            if (lua_type(L, -2) == LUA_TSTRING) {
                name = rawStringAt(L, -2);
            } else {
                name = QStringLiteral("<%1 key>").arg(luaTypeName(lua_type(L, -2)));
            }
            named.append(name);
            childPath = QStringLiteral("%1.%2").arg(path, name);
        }
        checkMixedTables(L, lua_gettop(L), childPath, seen, errors);
        lua_pop(L, 1);
    }

    if (!named.isEmpty() && listEntries > 0) {
        errors->append(QStringLiteral(
                           "%1: this table mixes list entries with the named key(s) %2 — the named "
                           "ones are ignored when the field is read as a list, which is almost "
                           "always a typo")
                           .arg(path, named.join(QStringLiteral(", "))));
    }
}

// ---------------------------------------------------------------------------
// 表收集
// ---------------------------------------------------------------------------

/// 把注册表 / 返回表里的一个列表读成按顺序排列的表引用（栈上的绝对索引）。
QVector<int> readTableList(lua_State *L, int index, const QString &what)
{
    if (lua_type(L, index) != LUA_TTABLE) {
        fail(QStringLiteral("%1 must be a list of tables").arg(what));
    }
    const TableShape shape = shapeOf(L, index);
    if (!shape.named.isEmpty()) {
        fail(QStringLiteral("%1 must be a list of tables without named keys, but it has the key(s) %2")
                 .arg(what, joinQuoted(shape.named)));
    }
    QVector<int> out;
    const lua_Unsigned length = lua_rawlen(L, index);
    // 每个条目都会压在栈上（调用方负责清），而条目数可能远多于 API 保证的
    // `LUA_MINSTACK` 个槽。不先扩容的话，`api_incr_top` 会直接写到栈数组之外
    // —— 以前一直是靠 `EXTRA_STACK` 的 5 个余量在硬撑，条目一多（例如
    // 一个 app 里的 hotkeys）就变成堆损坏。
    if (length > 100000) {
        fail(QStringLiteral("%1 has too many entries").arg(what));
    }
    if (!lua_checkstack(L, static_cast<int>(length) + LUA_MINSTACK)) {
        fail(QStringLiteral("%1 is too large to load (out of stack space)").arg(what));
    }
    for (lua_Unsigned i = 1; i <= length; ++i) {
        lua_rawgeti(L, index, static_cast<lua_Integer>(i));
        if (lua_type(L, -1) != LUA_TTABLE) {
            const QString type = luaTypeName(lua_type(L, -1));
            lua_pop(L, 1);
            fail(QStringLiteral("%1 must be a list of tables, but entry %2 is a %3")
                     .arg(what)
                     .arg(i)
                     .arg(type));
        }
        out.append(lua_gettop(L));
    }
    return out;
}

core::AppDef convertApp(lua_State *L, int index)
{
    checkFields(L, index, {"name", "process", "title", "window", "launch", "hotkeys", "enabled"});
    core::AppDef app;
    app.name = optString(L, index, "name");
    app.process = optString(L, index, "process");
    app.title = optString(L, index, "title");
    app.enabled = optBool(L, index, "enabled").value_or(true);

    int launch = 0;
    if (pushField(L, index, "launch", &launch)) {
        if (lua_type(L, launch) != LUA_TTABLE) {
            const QString launchType = luaTypeName(lua_type(L, launch));
            lua_pop(L, 1);
            fail(QStringLiteral("invalid type: %1, expected a table with the launch fields "
                                "(`program`, `args`, ...)")
                     .arg(launchType));
        }
        app.launch = convertLaunchSpec(L, launch, nullptr);
        lua_pop(L, 1);
    }

    int window = 0;
    if (pushField(L, index, "window", &window)) {
        if (lua_type(L, window) != LUA_TTABLE) {
            const QString type = luaTypeName(lua_type(L, window));
            lua_pop(L, 1);
            fail(QStringLiteral("invalid type: %1, expected a table with the window_rule fields "
                                "(`desktop`, `monitor`, ...)")
                     .arg(type));
        }
        app.window = convertWindowRule(L, window);
        lua_pop(L, 1);
    }

    int hotkeys = 0;
    if (pushField(L, index, "hotkeys", &hotkeys)) {
        // `readTableList` 把每个条目都压上栈；转换完要把它们连同列表一起清掉，
        // 否则会把调用方的栈顶弄乱（下面的 `lua_pop` 就弹错东西了）。
        const int listBase = lua_gettop(L);
        const QVector<int> tables = readTableList(L, hotkeys, QStringLiteral("`hotkeys`"));
        for (int tableIndex : tables) {
            app.hotkeys.push_back(convertHotkey(L, tableIndex));
        }
        lua_settop(L, listBase);
        lua_pop(L, 1);
    }
    return app;
}

QString entryLabel(lua_State *L, int index, std::size_t position, const char *kind)
{
    const QString kindName = QString::fromLatin1(kind);
    lua_getfield(L, index, "name");
    QString name;
    if (lua_type(L, -1) == LUA_TSTRING) {
        name = rawStringAt(L, -1);
    }
    lua_pop(L, 1);
    // `app` 不写 `name` 时用 `process`（再退到 `title`）当标签，
    // 这样错误信息里能直接看出是哪个程序。
    if (name.isEmpty() && kindName == QLatin1String("app")) {
        for (const char *field : {"process", "title"}) {
            lua_getfield(L, index, field);
            if (lua_type(L, -1) == LUA_TSTRING) {
                name = rawStringAt(L, -1);
            }
            lua_pop(L, 1);
            if (!name.isEmpty()) {
                break;
            }
        }
    }
    if (!name.isEmpty()) {
        return QStringLiteral("%1 #%2 (`%3`)").arg(kindName).arg(position + 1).arg(name);
    }
    return QStringLiteral("%1 #%2").arg(kindName).arg(position + 1);
}

} // namespace

// ---------------------------------------------------------------------------
// 求值器
// ---------------------------------------------------------------------------

namespace {

/// 求值一段配置脚本。`text` 已经剥掉 UTF-8 BOM（`core::loadConfig` 负责）。
std::optional<core::ConfigError> evaluate(const QString &text, const QString &path,
                                          core::EvalResult *out)
{
    QFile preludeFile(QString::fromLatin1(kPreludeResource));
    flowkeydEnsureLuaPreludeResource();
    if (!preludeFile.open(QIODevice::ReadOnly)) {
        return core::ConfigError::makeParse(
            path, QStringLiteral("cannot read the embedded Lua DSL prelude (%1)")
                      .arg(QString::fromLatin1(kPreludeResource)));
    }
    const QByteArray prelude = preludeFile.readAll();
    preludeFile.close();

    lua_State *L = luaL_newstate();
    if (L == nullptr) {
        return core::ConfigError::makeParse(path, QStringLiteral("cannot create a Lua state"));
    }
    struct StateGuard
    {
        lua_State *state;
        ~StateGuard() { lua_close(state); }
    } guard{L};
    luaL_openlibs(L);

    // DSL 注册表：settings / hotkeys / remaps / window_rules / apps 五个列表，
    // 只作为参数传给预置脚本。
    lua_createtable(L, 0, 5);
    for (const char *name : {"settings", "hotkeys", "remaps", "window_rules", "apps"}) {
        lua_newtable(L);
        lua_setfield(L, -2, name);
    }
    const int registryIndex = lua_absindex(L, -1);

    const int msghIndex = lua_gettop(L) + 1;
    lua_pushcfunction(L, tracebackHandler);

    const auto load = [&](const QByteArray &code, const QByteArray &chunkName) -> std::optional<QString> {
        if (luaL_loadbufferx(L, code.constData(), static_cast<size_t>(code.size()),
                             chunkName.constData(), "t")
            != LUA_OK) {
            const QString message = QString::fromUtf8(lua_tostring(L, -1));
            lua_pop(L, 1);
            return QStringLiteral("syntax error: %1").arg(message);
        }
        return std::nullopt;
    };
    const auto takeError = [&]() -> QString {
        QString message = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        return message;
    };

    if (const auto error = load(prelude, QByteArrayLiteral("@flowkeyd-dsl"))) {
        return core::ConfigError::makeParse(path, *error);
    }
    lua_pushvalue(L, registryIndex);
    if (lua_pcall(L, 1, 0, msghIndex) != LUA_OK) {
        return core::ConfigError::makeParse(path, takeError());
    }

    const QString nativePath = QDir::toNativeSeparators(path);
    const QByteArray chunkName = QByteArrayLiteral("@") + nativePath.toUtf8();
    const QByteArray code = text.toUtf8();
    if (const auto error = load(code, chunkName)) {
        return core::ConfigError::makeParse(path, *error);
    }
    if (lua_pcall(L, 0, 1, msghIndex) != LUA_OK) {
        return core::ConfigError::makeParse(path, takeError());
    }
    const int returnedIndex = lua_absindex(L, -1);

    core::Config config;
    QStringList errors;
    QStringList warnings;

    // ---- settings（可以出现多次，后写的键覆盖先写的） ----
    lua_getfield(L, registryIndex, "settings");
    QVector<int> settingsTables =
        readTableList(L, lua_gettop(L), QStringLiteral("settings"));
    // 上面那张 registry 表留在栈上（readTableList 把条目压在了它上面）。
    if (returnedIndex != 0 && lua_type(L, returnedIndex) == LUA_TTABLE) {
        lua_getfield(L, returnedIndex, "settings");
        if (lua_type(L, -1) == LUA_TTABLE) {
            settingsTables.append(lua_gettop(L));
        } else if (!lua_isnil(L, -1)) {
            errors.append(QStringLiteral("the returned `settings` must be a table, got a %1")
                              .arg(luaTypeName(lua_type(L, -1))));
            lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
        }
    }

    if (settingsTables.size() == 1) {
        try {
            config.settings = convertSettings(L, settingsTables.first());
        } catch (const ConvError &error) {
            errors.append(QStringLiteral("settings: %1").arg(error.message));
        }
    } else if (settingsTables.size() > 1) {
        warnings.append(QStringLiteral(
                            "`settings` was given %1 times (script calls plus a returned table); "
                            "later values win")
                            .arg(settingsTables.size()));
        lua_createtable(L, 0, 0);
        const int merged = lua_absindex(L, -1);
        for (int source : settingsTables) {
            lua_pushnil(L);
            while (lua_next(L, source) != 0) {
                // 栈上是 key, value；复制到 merged 后 key 留在栈顶供 lua_next 使用。
                lua_pushvalue(L, -2);
                lua_insert(L, -2);
                lua_settable(L, merged);
            }
        }
        try {
            config.settings = convertSettings(L, merged);
        } catch (const ConvError &error) {
            errors.append(QStringLiteral("settings: %1").arg(error.message));
        }
        lua_pop(L, 1);
    }

    // ---- hotkeys / remaps ----
    const auto convertEntries = [&](const char *registryField, const char *kind,
                                    const std::function<void(int, std::size_t, const QString &)> &handle) {
        lua_getfield(L, registryIndex, registryField);
        const QVector<int> tables =
            readTableList(L, lua_gettop(L), QString::fromLatin1(registryField));
        for (std::size_t i = 0; i < static_cast<std::size_t>(tables.size()); ++i) {
            const int tableIndex = tables.at(static_cast<int>(i));
            const QString label = entryLabel(L, tableIndex, i, kind);
            QVector<const void *> seen;
            checkMixedTables(L, tableIndex, label, &seen, &errors);
            handle(tableIndex, i, label);
        }
    };

    convertEntries("hotkeys", "hotkey", [&](int tableIndex, std::size_t, const QString &label) {
        try {
            config.hotkeys.push_back(convertHotkey(L, tableIndex));
        } catch (const ConvError &error) {
            errors.append(QStringLiteral("%1: %2").arg(label, error.message));
        }
    });
    convertEntries("remaps", "remap", [&](int tableIndex, std::size_t, const QString &label) {
        try {
            config.remaps.push_back(convertRemap(L, tableIndex));
        } catch (const ConvError &error) {
            errors.append(QStringLiteral("%1: %2").arg(label, error.message));
        }
    });
    convertEntries("window_rules", "window_rule",
                   [&](int tableIndex, std::size_t, const QString &label) {
                       try {
                           config.windowRules.push_back(convertWindowRule(L, tableIndex));
                       } catch (const ConvError &error) {
                           errors.append(QStringLiteral("%1: %2").arg(label, error.message));
                       }
                   });
    convertEntries("apps", "app", [&](int tableIndex, std::size_t, const QString &label) {
        try {
            config.apps.push_back(convertApp(L, tableIndex));
        } catch (const ConvError &error) {
            errors.append(QStringLiteral("%1: %2").arg(label, error.message));
        }
    });

    // ---- 脚本 `return` 的那张表（排在脚本体注册的条目之后） ----
    if (returnedIndex != 0 && !lua_isnil(L, returnedIndex)) {
        if (lua_type(L, returnedIndex) != LUA_TTABLE) {
            errors.append(QStringLiteral(
                              "the config script must return a table (`return { hotkeys = { ... } }`) "
                              "or nothing, got a %1")
                              .arg(luaTypeName(lua_type(L, returnedIndex))));
        } else {
            const int loopBase = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, returnedIndex) != 0) {
                // 栈上是 key, value；处理时可能压入更多东西，
                // 所以每轮结束时用 lua_settop 回到「key 在栈顶」。
                if (lua_type(L, -2) != LUA_TSTRING) {
                    errors.append(QStringLiteral(
                                      "the returned table only takes the named fields `settings`, "
                                      "`hotkeys`, `remaps` and `window_rules`, found a %1 key")
                                      .arg(luaTypeName(lua_type(L, -2))));
                    lua_settop(L, loopBase + 1);
                    continue;
                }
                const QString key = rawStringAt(L, -2);
                const int valueIndex = lua_gettop(L);
                if (key == QLatin1String("settings")) {
                    // 已经在上面处理过了。
                } else if (key == QLatin1String("hotkeys") || key == QLatin1String("remaps")
                           || key == QLatin1String("window_rules") || key == QLatin1String("apps")) {
                    const bool isHotkeys = key == QLatin1String("hotkeys");
                    const bool isRemaps = key == QLatin1String("remaps");
                    const bool isApps = key == QLatin1String("apps");
                    const char *kind = isHotkeys ? "hotkey"
                                                 : (isRemaps ? "remap"
                                                             : (isApps ? "app" : "window_rule"));
                    const QString what =
                        QStringLiteral("the returned `%1`").arg(key);
                    try {
                        const QVector<int> tables = readTableList(L, valueIndex, what);
                        for (int tableIndex : tables) {
                            const std::size_t position =
                                isHotkeys ? config.hotkeys.size()
                                          : (isRemaps ? config.remaps.size()
                                                      : (isApps ? config.apps.size()
                                                                : config.windowRules.size()));
                            const QString label = entryLabel(L, tableIndex, position, kind);
                            QVector<const void *> seen;
                            checkMixedTables(L, tableIndex, label, &seen, &errors);
                            try {
                                if (isHotkeys) {
                                    config.hotkeys.push_back(convertHotkey(L, tableIndex));
                                } else if (isRemaps) {
                                    config.remaps.push_back(convertRemap(L, tableIndex));
                                } else if (isApps) {
                                    config.apps.push_back(convertApp(L, tableIndex));
                                } else {
                                    config.windowRules.push_back(convertWindowRule(L, tableIndex));
                                }
                            } catch (const ConvError &error) {
                                errors.append(QStringLiteral("%1: %2").arg(label, error.message));
                            }
                        }
                    } catch (const ConvError &error) {
                        errors.append(error.message);
                    }
                } else {
                    errors.append(QStringLiteral(
                                      "unknown field `%1` in the returned table, expected `settings`, "
                                      "`hotkeys`, `remaps`, `window_rules` or `apps`")
                                      .arg(key));
                }
                lua_settop(L, loopBase + 1);
            }
        }
    }

    out->config = std::move(config);
    out->errors = std::move(errors);
    out->warnings = std::move(warnings);
    return std::nullopt;
}

} // namespace

core::Evaluator makeLuaEvaluator()
{
    return [](const QString &text, const QString &path, core::EvalResult *out) {
        try {
            return evaluate(text, path, out);
        } catch (const ConvError &error) {
            // 理论上不该到达：每个条目转换都各自捕过了。
            return std::optional<core::ConfigError>(
                core::ConfigError::makeParse(path, error.message));
        }
    };
}

QString luaVersion()
{
    return QString::fromLatin1(LUA_RELEASE);
}

} // namespace flowkeyd::lua
