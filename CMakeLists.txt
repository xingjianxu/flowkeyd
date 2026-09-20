cmake_minimum_required(VERSION 3.24)

project(flowkeyd VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Widgets
    Test
)

qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

# 跑 exe / 单测时需要的 DLL 目录（Qt 自己的，以及 MinGW 的 libstdc++/libgcc）。
get_target_property(_flowkeyd_qt_core_location Qt6::Core IMPORTED_LOCATION)
get_filename_component(FLOWKEYD_QT_BIN_DIR "${_flowkeyd_qt_core_location}" DIRECTORY)
get_filename_component(FLOWKEYD_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

include(VendorLua)

# ---------------------------------------------------------------------------
# 告警策略：本仓库的代码把警告当错误（见 AGENTS.md 第 11 节）。
# 第三方代码（vendor/lua）不套用这条。
# ---------------------------------------------------------------------------
function(flowkeyd_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 纯逻辑层：不碰 Win32、不碰 Qt GUI，只用 QtCore。
# 因此它可以被 Qt Test 直接链接，也能在没有桌面会话时跑 --check/--list-keys。
# ---------------------------------------------------------------------------
add_library(flowkeyd_core STATIC
    src/core/action.cpp
    src/core/action.h
    src/core/config.cpp
    src/core/config.h
    src/core/engine.cpp
    src/core/engine.h
    src/core/keys.cpp
    src/core/keys.h
    src/core/template.cpp
    src/core/template.h
)

target_include_directories(flowkeyd_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(flowkeyd_core PUBLIC Qt6::Core)
flowkeyd_strict_warnings(flowkeyd_core)

# ---------------------------------------------------------------------------
# Lua 配置层：Lua 与 C++ 的唯一边界（预置 DSL 编在 qrc 里）。
# 与 core 一样不碰 Win32、不碰 Qt GUI，因此也能被单元测试直接链接。
# ---------------------------------------------------------------------------
add_library(flowkeyd_lua STATIC
    src/lua/lua_config.cpp
    src/lua/lua_config.h
    src/lua/lua_include.h
)
target_include_directories(flowkeyd_lua PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(flowkeyd_lua PUBLIC flowkeyd_core flowkeyd::lua Qt6::Core)
flowkeyd_strict_warnings(flowkeyd_lua)
qt_add_resources(flowkeyd_lua "lua_prelude"
    PREFIX "/lua"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/lua"
    FILES src/lua/lua_prelude.lua
)

# ---------------------------------------------------------------------------
# Win32 后端。不碰 Qt GUI（托盘/窗口在 app 层），因此单测也能直接链接。
# 系统库写成 PUBLIC，静态库的使用者自己不用再列一遍。
# ---------------------------------------------------------------------------
add_library(flowkeyd_platform STATIC
    src/platform/win/console.cpp
    src/platform/win/console.h
    src/platform/win/elevate.cpp
    src/platform/win/elevate.h
    src/platform/win/ffi.cpp
    src/platform/win/ffi.h
    src/platform/win/hook.cpp
    src/platform/win/hook.h
    src/platform/win/input.cpp
    src/platform/win/input.h
    src/platform/win/logging.cpp
    src/platform/win/logging.h
    src/platform/win/nt.cpp
    src/platform/win/nt.h
    src/platform/win/process.cpp
    src/platform/win/process.h
    src/platform/win/single_instance.cpp
    src/platform/win/single_instance.h
)
target_include_directories(flowkeyd_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(flowkeyd_platform PUBLIC
    flowkeyd_core
    Qt6::Core
    user32
    kernel32
    shell32
    ole32
    advapi32
    powrprof
)
flowkeyd_strict_warnings(flowkeyd_platform)

# ---------------------------------------------------------------------------
# 守护进程本体
# ---------------------------------------------------------------------------
set(FLOWKEYD_QML_FILES
    src/qml/LogWindow.qml
)
set_source_files_properties(src/qml/LogWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS LogWindow.qml)

qt_add_executable(flowkeyd
    WIN32
    src/main.cpp
    src/cli.cpp
    src/cli.h
    src/app/dispatcher.cpp
    src/app/dispatcher.h
    src/app/log_window.cpp
    src/app/log_window.h
    src/app/runtime.cpp
    src/app/runtime.h
    src/platform/win/tray.cpp
    src/platform/win/tray.h
)

qt_add_qml_module(flowkeyd
    URI Flowkeyd
    VERSION 1.0
    QML_FILES ${FLOWKEYD_QML_FILES}
)

target_link_libraries(flowkeyd
    PRIVATE
        flowkeyd_core
        flowkeyd_lua
        flowkeyd_platform
        flowkeyd::lua
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        ntdll
        uxtheme
)

flowkeyd_strict_warnings(flowkeyd)

target_compile_definitions(flowkeyd PRIVATE FLOWKEYD_VERSION="${PROJECT_VERSION}")

install(TARGETS flowkeyd
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET flowkeyd
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
function(flowkeyd_add_test name)
    cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
    qt_add_executable(${name} tests/${name}.cpp)
    set_target_properties(${name} PROPERTIES
        WIN32_EXECUTABLE FALSE
        MACOSX_BUNDLE FALSE
    )
    target_link_libraries(${name} PRIVATE flowkeyd_core Qt6::Test ${ARG_LIBS})
    flowkeyd_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND}
        -DTEST_EXE=$<TARGET_FILE:${name}>
        -DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/${name}.log
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunQTest.cmake)
    # 测试进程要能找到 Qt 与 MinGW 运行时的 DLL。
    set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${FLOWKEYD_QT_BIN_DIR};PATH=path_list_prepend:${FLOWKEYD_MINGW_BIN_DIR}")
endfunction()

flowkeyd_add_test(tst_smoke)
flowkeyd_add_test(tst_keys)
flowkeyd_add_test(tst_send_script)
flowkeyd_add_test(tst_config)
flowkeyd_add_test(tst_engine)
flowkeyd_add_test(tst_template)
flowkeyd_add_test(tst_lua LIBS flowkeyd_lua)
flowkeyd_add_test(tst_layout LIBS flowkeyd_platform)
flowkeyd_add_test(tst_input LIBS flowkeyd_platform)
flowkeyd_add_test(tst_command_line LIBS flowkeyd_platform)
flowkeyd_add_test(tst_instance LIBS flowkeyd_platform)

# 冒烟测试要真的链接 Lua，才能证明 vendor/lua 编得进来。
target_link_libraries(tst_smoke PRIVATE flowkeyd::lua)
