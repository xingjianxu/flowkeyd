# 把 vendor/lua（lua/lua @ v5.5.1）编成一个静态库 `lua_static`。
#
# 排除四个带 `main()` 或测试用的文件：lua.c（解释器）、luac.c（编译器）、
# onelua.c（单文件版）、ltests.c（Lua 自己的测试套件）。
# `LUA_USE_WINDOWS` 是 Lua 官方在 Windows 上推荐的编译开关。
add_library(lua_static STATIC)
add_library(flowkeyd::lua ALIAS lua_static)

set(_lua_dir "${CMAKE_CURRENT_LIST_DIR}/../vendor/lua")

file(GLOB _lua_sources CONFIGURE_DEPENDS "${_lua_dir}/*.c")
list(REMOVE_ITEM _lua_sources
    "${_lua_dir}/lua.c"
    "${_lua_dir}/luac.c"
    "${_lua_dir}/onelua.c"
    "${_lua_dir}/ltests.c"
)
if(NOT _lua_sources)
    message(FATAL_ERROR "vendor/lua 里没有找到任何 .c：请确认 submodule 已检出")
endif()

target_sources(lua_static PRIVATE ${_lua_sources})
target_include_directories(lua_static PUBLIC "${_lua_dir}")
target_compile_definitions(lua_static PRIVATE LUA_USE_WINDOWS)

# Lua 源码不是我们的代码，不做 -Werror；但仍然打开基础告警便于注意。
target_compile_options(lua_static PRIVATE -Wall -Wno-unused-parameter)

set_target_properties(lua_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTOUIC OFF
)

list(LENGTH _lua_sources _lua_count)
message(STATUS "vendor/lua: 编译 ${_lua_dir} 下的 Lua 5.5.1（${_lua_count} 个源文件）")
