// 配置结构体 <-> QVariant（QML 友好的字典 / 列表）的转换。
//
// 图形化配置界面（`app::ConfigEditor`）不能把 C++ 结构体直接交给 QML：`core::Config`
// 里满是 `std::optional` 与强类型枚举，QML 里既读不出也写不进。所以这里把每个条目
// 摊成一张**与 Lua schema 逐字对应**的字典：
//
//   * 键名就是配置文件里的字段名（`log_level` / `keys` / `on_release` …），于是界面上
//     的每一格都能一眼对回 README 的配置表；
//   * `std::optional` **没写就不在字典里**（QML 读到 `undefined`），显式写了的才会
//     出现。这条规则对 `window` 动作的 `launch` 特别重要：那张表里「写没写某个字段」
//     本身就是语义（见 `core::LaunchFields`），所以它按掩码只写出写过的键；
//   * 枚举写成配置里能写的那种字符串（`"activate"` / `"play_pause"`）；
//   * **动作统一是「一串动作」**（`actions = [ … ]`）：配置文件里的单动作、动作列表
//     与 `"send:^{c}"` 简写都归一到同一个形状，界面只需要一种编辑器。
//     `short` 简写会在加载时展开成等价的动作表（行为完全一样，只是写回文件时不再是
//     简写形式）。
//
// 这一层只依赖 QtCore：不碰 Win32、不碰 QML、不碰磁盘。磁盘与 Lua 源码在
// `core/config_writer.*`。
#pragma once

#include "core/config.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace flowkeyd::core {

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

/// `Settings` -> 字典。`remote_desktop_processes` 只在**用户改过**名单时才出现
/// （没改过就写 `remote_desktop = true/false`，让内置名单继续跟着版本走）。
QVariantMap settingsToMap(const Settings &settings);
Settings settingsFromMap(const QVariantMap &map);

// ---------------------------------------------------------------------------
// 显示器目标
// ---------------------------------------------------------------------------

/// `MonitorRef` -> 界面里那一段文本（`"1"` / `"primary"` / `"DISPLAY2"` /
/// `"cursor"` / `"last"` / `"none"`）。
QString monitorRefText(const MonitorRef &ref);

/// 反过来解析；空串返回 `std::nullopt`（= 这个字段没写）。
///
/// 认不出来的一律当成设备名（与 Lua 层一致）：`--check` 时才会有机会发现它不对。
std::optional<MonitorRef> monitorRefFromText(const QString &text);

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

/// 一个动作 -> 字典（键名与 Lua 表完全一致，含 `type`）。
QVariantMap actionToMap(const Action &action);

/// 反过来。`label` 只用于错误信息（例如 `hotkey #2`）。
Action actionFromMap(const QVariantMap &map, QStringList *errors, const QString &label);

/// 动作种类名列表（`Action::Kind` 的配置写法），供界面的下拉框使用。
QStringList actionKindNames();

/// 一种动作的空字典（界面里「新增动作」的起点，含必要的必填字段）。
QVariantMap defaultActionMap(const QString &type);

// ---------------------------------------------------------------------------
// 条目
// ---------------------------------------------------------------------------

/// `ActionSpec` -> 动作字典列表（0 / 1 / n 个）。
QVariantList actionListToMap(const ActionSpec &spec);
/// 反过来；空列表返回 `std::nullopt`（= 没有动作）。
std::optional<ActionSpec> actionListFromMap(const QVariantList &list,
                                            QStringList *errors,
                                            const QString &label);

QVariantMap hotkeyToMap(const HotkeyDef &def);
HotkeyDef hotkeyFromMap(const QVariantMap &map, QStringList *errors, const QString &label);

QVariantMap remapToMap(const RemapDef &def);
RemapDef remapFromMap(const QVariantMap &map);

QVariantMap windowRuleToMap(const WindowRuleDef &def);
WindowRuleDef windowRuleFromMap(const QVariantMap &map);

QVariantMap appToMap(const AppDef &def);
AppDef appFromMap(const QVariantMap &map, QStringList *errors, const QString &label);

// ---------------------------------------------------------------------------
// 整份配置
// ---------------------------------------------------------------------------

QVariantMap configToMap(const Config &config);
Config configFromMap(const QVariantMap &map, QStringList *errors);

/// 界面里各种下拉框能选的取值（与 `flowkeyd.lua.example` 一致）。
QStringList logLevelNames();
QStringList inputBackendNames();
QStringList triggerModeNames();
QStringList remapModeNames();
QStringList showModeNames();
QStringList volumeOpNames();
QStringList mediaOpNames();
QStringList clipboardOpNames();
QStringList windowOpNames();
QStringList powerOpNames();
QStringList toggleStateNames();
/// `workspace` 动作的 op（`switch` / `move`）。
QStringList workspaceOpNames();
/// `settings.workspace.switch_target` 的取值（`cursor` / `focus`）。
QStringList workspaceSwitchTargetNames();
/// `settings.workspace.switch_effect` 的取值（`instant` / `freeze`）。
QStringList workspaceSwitchEffectNames();

} // namespace flowkeyd::core
