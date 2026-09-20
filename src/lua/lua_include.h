// 在 C++ 里包含 Lua 头文件的唯一正确姿势。
//
// **Lua 的头文件没有 `extern "C"` 保护**（这是 Lua 官方的一贯做法，见
// `luaconf.h` 里只有 `LUAMOD_API` 带 `extern "C"`）。如果直接
// `#include <lua.h>`，所有函数声明都会按 C++ 规则改写成
// `_Z13luaL_newstatev` 之类的名字，链接时找不到静态库里的 C 版实现，
// 报一屏 `undefined reference to luaL_newstate()`。
//
// 所有需要 Lua API 的 C++ 文件都必须改包含这个头，而不是 `<lua.h>`。
#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
