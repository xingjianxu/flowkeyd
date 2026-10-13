#include "core/config_writer.h"

#include "core/action.h"
#include "core/config_map.h"
#include "core/remote_desktop.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace flowkeyd::core {

namespace {

// ---------------------------------------------------------------------------
// Lua 值的排版
// ---------------------------------------------------------------------------

/// Lua 的字符串字面量（双引号 + 转义）。
///
/// 刻意不用 `[[...]]` 长字符串：它不能包含 `]]`，而且会原样保留换行 —— 反过来
/// 用短字符串把每个反斜杠都转义掉，读回来一定与写出去的一致（Windows 路径因此
/// 不需要用户再操心）。
QString luaString(const QString &text)
{
    QString out;
    out.reserve(text.size() + 2);
    out.append(QLatin1Char('"'));
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case u'\\':
            out += QLatin1String("\\\\");
            break;
        case u'"':
            out += QLatin1String("\\\"");
            break;
        case u'\n':
            out += QLatin1String("\\n");
            break;
        case u'\r':
            out += QLatin1String("\\r");
            break;
        case u'\t':
            out += QLatin1String("\\t");
            break;
        default:
            if (ch.unicode() < 0x20 || ch.unicode() == 0x7F) {
                // Lua 的十进制转义最多读三位；补零到三位，后面紧跟一个数字字符
                // 时才不会被连读（例如控制字符后面正好是一个 '5'）。
                out += QStringLiteral("\\%1")
                           .arg(QString::number(static_cast<int>(ch.unicode()))
                                    .rightJustified(3, QLatin1Char('0')));
            } else {
                out.append(ch);
            }
            break;
        }
    }
    out.append(QLatin1Char('"'));
    return out;
}

QString luaNumber(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Double:
    case QMetaType::Float: {
        const double number = value.toDouble();
        if (number == static_cast<double>(static_cast<qlonglong>(number))) {
            return QString::number(static_cast<qlonglong>(number));
        }
        return QString::number(number, 'g', 17);
    }
    default:
        return QString::number(value.toLongLong());
    }
}

QString luaBody(const QVariant &value, int indent);

QString luaListBody(const QVariantList &list, int indent)
{
    if (list.isEmpty()) {
        return QStringLiteral("{}");
    }
    const QString pad(indent + 2, QLatin1Char(' '));
    QString out = QStringLiteral("{\n");
    for (const QVariant &item : list) {
        out += pad;
        out += luaBody(item, indent + 2);
        out += QStringLiteral(",\n");
    }
    out += QString(indent, QLatin1Char(' '));
    out += QLatin1Char('}');
    return out;
}

QString luaMapBody(const QVariantMap &map, int indent)
{
    if (map.isEmpty()) {
        return QStringLiteral("{}");
    }
    const QString pad(indent + 2, QLatin1Char(' '));
    QString out = QStringLiteral("{\n");
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        out += pad;
        out += it.key();
        out += QLatin1String(" = ");
        out += luaBody(it.value(), indent + 2);
        out += QStringLiteral(",\n");
    }
    out += QString(indent, QLatin1Char(' '));
    out += QLatin1Char('}');
    return out;
}

QString luaBody(const QVariant &value, int indent)
{
    const QMetaType type = value.metaType();
    switch (type.id()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return luaNumber(value);
    case QMetaType::QString:
        return luaString(value.toString());
    case QMetaType::QStringList: {
        QVariantList list;
        const QStringList strings = value.toStringList();
        for (const QString &item : strings) {
            list.append(item);
        }
        return luaListBody(list, indent);
    }
    case QMetaType::QVariantList:
        return luaListBody(value.toList(), indent);
    case QMetaType::QVariantMap:
        return luaMapBody(value.toMap(), indent);
    default:
        break;
    }
    if (value.canConvert<QVariantMap>()) {
        return luaMapBody(value.toMap(), indent);
    }
    if (value.canConvert<QVariantList>()) {
        return luaListBody(value.toList(), indent);
    }
    return luaString(value.toString());
}

/// 空的表要不要写出去？界面内部会给「可选的一整块」留一个空表，而空表在 Lua 里
/// 常常是有语义的（`window = {}` 会真的变成一条什么都不做的摆放规则）。所以只在
/// 真的填了东西时才写。
void dropEmpty(QVariantMap *map, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QVariant value = map->value(key);
        if (value.canConvert<QVariantMap>() && value.toMap().isEmpty()) {
            map->remove(key);
        } else if (value.canConvert<QVariantList>() && value.toList().isEmpty()) {
            map->remove(key);
        }
    }
}

/// `action` 字段：一个动作写一张表，多个动作写一列。
QVariant actionField(const QVariantList &actions)
{
    if (actions.size() == 1) {
        return actions.first();
    }
    return actions;
}

QVariantList menuItemsForLua(const std::vector<MenuItemDef> &items);
/// `ActionSpec` -> 动作表列表（简写展开成等价的动作表，嵌套列表拍平）。
QVariantList actionSpecForLua(const ActionSpec &spec);

/// 动作 -> Lua 表。
///
/// 与 `actionToMap()` 的区别只有两处，都是「界面内部形状」与「配置形状」的差异：
///   * 空的 `launch` 不写（写成 `launch = {}` 会因为缺 `program` 而报错）；
///   * `menu` 的条目里那个字段叫 `action`（界面内部叫 `actions`）。
QVariantMap actionForLua(const Action &action)
{
    QVariantMap out = actionToMap(action);
    dropEmpty(&out, {QStringLiteral("launch")});
    if (action.kind == Action::Kind::Menu) {
        out.insert(QStringLiteral("items"), menuItemsForLua(action.items));
    }
    return out;
}

QVariantList menuItemsForLua(const std::vector<MenuItemDef> &items)
{
    QVariantList out;
    out.reserve(static_cast<qsizetype>(items.size()));
    for (const MenuItemDef &item : items) {
        QVariantMap entry;
        if (item.key.has_value()) {
            entry.insert(QStringLiteral("key"), *item.key);
        }
        entry.insert(QStringLiteral("label"), item.label);
        if (item.hint.has_value()) {
            entry.insert(QStringLiteral("hint"), *item.hint);
        }
        if (item.action) {
            entry.insert(QStringLiteral("action"), actionField(actionSpecForLua(*item.action)));
        }
        out.append(entry);
    }
    return out;
}

/// `ActionSpec` -> 动作表列表（简写展开成等价的动作表，嵌套列表拍平）。
QVariantList actionSpecForLua(const ActionSpec &spec)
{
    QVariantList out;
    switch (spec.kind) {
    case ActionSpec::Kind::One:
        out.append(actionForLua(spec.action));
        break;
    case ActionSpec::Kind::Short: {
        Action parsed;
        if (!parseActionShorthand(spec.shorthand, &parsed).has_value()) {
            out.append(actionForLua(parsed));
        }
        break;
    }
    case ActionSpec::Kind::List:
        for (const ActionSpec &item : spec.list) {
            const QVariantList nested = actionSpecForLua(item);
            for (const QVariant &value : nested) {
                out.append(value);
            }
        }
        break;
    }
    return out;
}

QVariantMap hotkeyForLua(const HotkeyDef &def)
{
    QVariantMap out = hotkeyToMap(def);
    // 界面内部的键名 -> 配置文件里的字段名。
    out.remove(QStringLiteral("actions"));
    out.remove(QStringLiteral("release_actions"));
    out.remove(QStringLiteral("repeat_interval_ms"));
    out.remove(QStringLiteral("repeat_delay_ms"));
    if (def.action.has_value()) {
        const QVariantList actions = actionSpecForLua(*def.action);
        if (!actions.isEmpty()) {
            out.insert(QStringLiteral("action"), actionField(actions));
        }
    }
    if (def.onRelease.has_value()) {
        const QVariantList actions = actionSpecForLua(*def.onRelease);
        if (!actions.isEmpty()) {
            out.insert(QStringLiteral("on_release"), actionField(actions));
        }
    }
    // `repeatable` 在配置里有三种形状：`true` / `false` / `{ interval_ms, delay_ms }`。
    if (def.repeat.has_value()) {
        const bool hasNumbers =
            def.repeat->intervalMs.has_value() || def.repeat->delayMs.has_value();
        if (def.repeat->kind == RepeatSpec::Kind::Config && hasNumbers) {
            QVariantMap repeat;
            if (def.repeat->intervalMs.has_value()) {
                repeat.insert(QStringLiteral("interval_ms"),
                              static_cast<uint>(*def.repeat->intervalMs));
            }
            if (def.repeat->delayMs.has_value()) {
                repeat.insert(QStringLiteral("delay_ms"), static_cast<uint>(*def.repeat->delayMs));
            }
            out.insert(QStringLiteral("repeatable"), repeat);
        } else if (def.repeat->kind == RepeatSpec::Kind::Flag) {
            out.insert(QStringLiteral("repeatable"), def.repeat->flag);
        } else {
            out.insert(QStringLiteral("repeatable"), true);
        }
    } else {
        out.remove(QStringLiteral("repeatable"));
    }
    return out;
}

QVariantList hotkeysForLua(const std::vector<HotkeyDef> &defs)
{
    QVariantList out;
    out.reserve(static_cast<qsizetype>(defs.size()));
    for (const HotkeyDef &def : defs) {
        out.append(hotkeyForLua(def));
    }
    return out;
}

/// `app` 那一张表：`window` / `launch` / `hotkeys` 留空时不写（理由见 `dropEmpty`），
/// 里面的快捷键也要换成配置里的形状。
QVariantMap appForLua(const AppDef &app)
{
    QVariantMap out = appToMap(app);
    out.remove(QStringLiteral("hotkeys"));
    dropEmpty(&out, {QStringLiteral("window"), QStringLiteral("launch")});
    if (!app.hotkeys.empty()) {
        out.insert(QStringLiteral("hotkeys"), hotkeysForLua(app.hotkeys));
    }
    return out;
}

/// `settings` 那一张表。
///
/// 只有它需要手工拼：`remote_desktop` 在配置里有**两种形状**（布尔，或者
/// `{ enabled = …, processes = { … } }`），而界面内部用的是「一个开关 + 一份名单」
/// 两个字段 —— 名单没被改过时应该继续让内置名单跟着版本走，所以只在用户改过时
/// 才写成表。
QVariantMap settingsForLua(const Settings &settings)
{
    QVariantMap out = settingsToMap(settings);
    out.remove(QStringLiteral("remote_desktop"));
    out.remove(QStringLiteral("remote_desktop_processes"));
    if (settings.remoteDesktopProcesses == builtinRemoteDesktopProcesses()) {
        out.insert(QStringLiteral("remote_desktop"), settings.remoteDesktop);
    } else {
        QVariantMap table;
        table.insert(QStringLiteral("enabled"), settings.remoteDesktop);
        QVariantList processes;
        for (const QString &process : settings.remoteDesktopProcesses) {
            processes.append(process);
        }
        table.insert(QStringLiteral("processes"), processes);
        out.insert(QStringLiteral("remote_desktop"), table);
    }
    return out;
}

} // namespace

bool isEditorGenerated(const QString &text)
{
    // 只看开头：标记是生成文件的第一行，用户在自己写的文件里提到这个名字不算数。
    return stripUtf8Bom(text).left(160).contains(QLatin1String(kEditorGeneratedMarker));
}

QString luaValue(const QVariant &value, int indent)
{
    return luaBody(value, indent);
}

QString configToLua(const Config &config)
{
    QVariantMap root;
    root.insert(QStringLiteral("settings"), settingsForLua(config.settings));

    QVariantList remaps;
    remaps.reserve(static_cast<qsizetype>(config.remaps.size()));
    for (const RemapDef &remap : config.remaps) {
        remaps.append(remapToMap(remap));
    }

    QVariantList windowRules;
    windowRules.reserve(static_cast<qsizetype>(config.windowRules.size()));
    for (const WindowRuleDef &rule : config.windowRules) {
        windowRules.append(windowRuleToMap(rule));
    }

    QVariantList apps;
    apps.reserve(static_cast<qsizetype>(config.apps.size()));
    for (const AppDef &app : config.apps) {
        apps.append(appForLua(app));
    }

    root.insert(QStringLiteral("hotkeys"), hotkeysForLua(config.hotkeys));
    root.insert(QStringLiteral("remaps"), remaps);
    root.insert(QStringLiteral("window_rules"), windowRules);
    root.insert(QStringLiteral("apps"), apps);

    QString out;
    out += QStringLiteral("-- %1\n").arg(QLatin1String(kEditorGeneratedMarker));
    out += QStringLiteral("-- 本文件由 flowkeyd 的设置界面生成：在界面里保存一次，"
                          "这里的内容就会被整体重写。\n");
    out += QStringLiteral("-- 想继续手写 Lua（注释、循环、变量）请把上面这行标记删掉，"
                          "并且不要在界面里重新保存。\n");
    out += QStringLiteral("-- 覆盖之前原文件都会留一份 `.bak`。字段含义见 README.md 的「配置」一章。\n");
    out += QStringLiteral("\nreturn ");
    out += luaMapBody(root, 0);
    out += QStringLiteral("\n");
    return out;
}

bool writeConfigLuaFile(const QString &path, const QString &text, QString *error)
{
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    if (!dir.isEmpty() && !QDir().mkpath(dir)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create the directory %1")
                         .arg(QDir::toNativeSeparators(dir));
        }
        return false;
    }
    if (QFileInfo::exists(path)) {
        // 先留底：界面保存是整体重写，用户的注释与手写代码只会在这里留下。
        const QString backup = path + QStringLiteral(".bak");
        QFile::remove(backup);
        if (!QFile::rename(path, backup)) {
            if (error != nullptr) {
                *error =
                    QStringLiteral("cannot back up %1").arg(QDir::toNativeSeparators(path));
            }
            return false;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace flowkeyd::core
