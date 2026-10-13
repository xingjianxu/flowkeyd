// 配置结构体 -> Lua 源码，以及配置文件本身（含备份）的读写。
//
// 图形化配置界面（`app::ConfigEditor`）保存时会把**整份**配置重新写成
// `config.lua`：一份由界面生成的、字段与文档逐字对应的 Lua 文件。代价是原文件
// 里的注释与手写的 Lua 代码（循环、变量）会消失，所以：
//
//   * 生成的文件第一行是一个固定的标记（`kEditorGeneratedMarker`），界面用它
//     判断「这份文件本来就是我们生成的」；
//   * 覆盖一个**不是**界面生成的文件之前，`app::ConfigEditor` 会先让用户确认，
//     而这里在写盘时总会把原文件改名成 `<path>.bak` 留一份底。
//
// 这一层只用 QtCore（`QFile` / `QSaveFile`），不碰 Win32、不碰 QML，所以能被
// 单元测试直接驱动：`tst_config_writer` 会「写出来 -> 用真正的 Lua 层读回来」
// 地跑一遍，字段名写错会在那里立刻暴露。
#pragma once

#include "core/config.h"

#include <QString>

namespace flowkeyd::core {

/// 界面生成的配置文件在开头写下的标记。
///
/// **纯 ASCII**：它是被断言与程序读取的东西，不能跟着代码页走。
inline constexpr char kEditorGeneratedMarker[] = "flowkeyd-config-editor:generated";

/// 这段文本是界面生成的吗（开头几行里有标记）？
bool isEditorGenerated(const QString &text);

/// 把配置序列化成一份完整的 Lua 脚本（UTF-8 文本，含标记与说明注释）。
QString configToLua(const Config &config);

/// 把一个 Lua 值序列化成 Lua 源码（`luaValue` 是公开的，测试与调试都用得到）。
///
/// 接受：`bool`、各种整数 / 浮点、`QString`、`QStringList` / `QVariantList`、
/// `QVariantMap`。列表与映射都会跨行排版（缩进两个空格），空表写成 `{}`。
QString luaValue(const QVariant &value, int indent = 0);

/// 写配置文件：目录会在需要时创建，已存在的文件先改名成 `<path>.bak`。
///
/// 用 `QSaveFile` 落盘，所以中途失败不会留下半截文件。`error` 是英文原因。
bool writeConfigLuaFile(const QString &path, const QString &text, QString *error);

} // namespace flowkeyd::core
