// Lua 与 C++ 的**唯一**边界：建 `lua_State`、注入 DSL 预置环境、
// 求值配置脚本，并把脚本里的表逐条目转换成 `core::Config`。
//
// 求值完就把 `lua_State` 关掉：`core::Compiled` 与 Lua 无关，
// 钩子回调与工作线程永远碰不到它（见 AGENTS.md 第 7 节第 14 条）。
#pragma once

#include "core/config.h"

namespace flowkeyd::lua {

/// 构造一个真正的 Lua 5.5 求值器，交给 `core::loadConfig()` 使用。
///
/// 预置环境（`hotkey{}` / `remap{}` / `settings{}` 与全部动作构造器）
/// 来自 `src/lua/lua_prelude.lua`，编进 qrc 后在运行时读取。
core::Evaluator makeLuaEvaluator();

/// 所链接的 Lua 版本串（`LUA_RELEASE` 的内容），`--version`/`--check` 会用它。
QString luaVersion();

} // namespace flowkeyd::lua
