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

# ---------------------------------------------------------------------------
# 两条 profile 的分工（见 AGENTS.md 第 5 节）：
#   * windows-debug   —— 开发用：构建单测（含 tst_interactive），构建完自动把
#                        Qt/MinGW 运行时部署到构建目录，可以直接双击/命令行跑。
#   * windows-release —— 发布用：**不构建任何测试目标**，除了把运行时部署到
#                        构建目录，还会额外产出一个干净的发布目录
#                        build/dist-release（只有 exe 与它需要的运行时）。
# 这样 build/windows-release 里不会出现 tst_*.exe，发布永远以
# build/dist-release 为准（可以直接拷到别的机器上运行）。
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FLOWKEYD_IS_DEBUG_PROFILE ON)
else()
    set(FLOWKEYD_IS_DEBUG_PROFILE OFF)
endif()

option(FLOWKEYD_BUILD_TESTS
    "Build the Qt Test unit test targets (debug profile only; see AGENTS.md 5)"
    ${FLOWKEYD_IS_DEBUG_PROFILE})

if(FLOWKEYD_BUILD_TESTS)
    enable_testing()
endif()

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
    src/core/log_tail.cpp
    src/core/log_tail.h
    src/core/template.cpp
    src/core/template.h
    src/core/window_match.cpp
    src/core/window_match.h
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
# 弹窗模型层：`menu` / `help` 的纯逻辑（几何、筛选、滚动、命中测试）。
# 与 core 一样只用 QtCore，所以能被 Qt Test 直接链接。
# ---------------------------------------------------------------------------
add_library(flowkeyd_models STATIC
    src/app/help_model.cpp
    src/app/help_model.h
    src/app/menu_model.cpp
    src/app/menu_model.h
    src/app/popup_layout.cpp
    src/app/popup_layout.h
)
target_include_directories(flowkeyd_models PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(flowkeyd_models PUBLIC Qt6::Core)
flowkeyd_strict_warnings(flowkeyd_models)

# ---------------------------------------------------------------------------
# Win32 后端。不碰 Qt GUI（托盘/窗口在 app 层），因此单测也能直接链接。
# 系统库写成 PUBLIC，静态库的使用者自己不用再列一遍。
# ---------------------------------------------------------------------------
add_library(flowkeyd_platform STATIC
    src/platform/win/audio.cpp
    src/platform/win/audio.h
    src/platform/win/autostart.cpp
    src/platform/win/autostart.h
    src/platform/win/clipboard.cpp
    src/platform/win/clipboard.h
    src/platform/win/console.cpp
    src/platform/win/console.h
    src/platform/win/desktop.cpp
    src/platform/win/desktop.h
    src/platform/win/dwm.cpp
    src/platform/win/dwm.h
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
    src/platform/win/power.cpp
    src/platform/win/power.h
    src/platform/win/single_instance.cpp
    src/platform/win/single_instance.h
    src/platform/win/window.cpp
    src/platform/win/window.h
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
    src/qml/MenuPopup.qml
    src/qml/HelpPopup.qml
)
set_source_files_properties(src/qml/LogWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS LogWindow.qml)
set_source_files_properties(src/qml/MenuPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS MenuPopup.qml)
set_source_files_properties(src/qml/HelpPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS HelpPopup.qml)

qt_add_executable(flowkeyd
    WIN32
    src/main.cpp
    src/cli.cpp
    src/cli.h
    src/app/dispatcher.cpp
    src/app/dispatcher.h
    src/app/log_model.cpp
    src/app/log_model.h
    src/app/log_window.cpp
    src/app/log_window.h
    src/app/popup_host.cpp
    src/app/popup_host.h
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
        flowkeyd_models
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

# ---------------------------------------------------------------------------
# 让构建产物自包含：`windeployqt` 把 Qt 与 MinGW 运行时的 DLL（以及 exe 用到的
# QML 模块）拷到 exe 同目录，于是双击 build/<preset>/flowkeyd.exe 就能启动，
# 不必再把 Qt 的 bin 加进 PATH（见 AGENTS.md 第 5 节）。
# 只给 flowkeyd 做；测试可执行文件仍然靠 ctest 注入 PATH。
# ---------------------------------------------------------------------------
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET flowkeyd POST_BUILD
        COMMAND Qt6::windeployqt
            --no-translations
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/qml"
            "$<TARGET_FILE:flowkeyd>"
        COMMENT "windeployqt: deploying the Qt runtime next to flowkeyd.exe"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# release profile 额外产出一个**干净的发布目录** build/dist-release：
# 里面只有 flowkeyd.exe 与它需要的 Qt / MinGW 运行时（DLL、QML 模块、插件），
# 没有构建系统文件、没有单测，可以直接拷贝到别的机器上运行。
# 注意这是“额外”产出：build/windows-release 自己仍然部署了一份运行时，方便
# 开发期直接跑；发布一律以 build/dist-release 为准（见 AGENTS.md 第 5 节）。
# ---------------------------------------------------------------------------
if(WIN32 AND TARGET Qt6::windeployqt AND NOT FLOWKEYD_IS_DEBUG_PROFILE)
    set(FLOWKEYD_DIST_DIR "${CMAKE_SOURCE_DIR}/build/dist-release")
    add_custom_command(TARGET flowkeyd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FLOWKEYD_DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:flowkeyd>" "${FLOWKEYD_DIST_DIR}/flowkeyd.exe"
        COMMAND Qt6::windeployqt
            --no-translations
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/qml"
            "${FLOWKEYD_DIST_DIR}/flowkeyd.exe"
        COMMENT "windeployqt: staging the clean release into build/dist-release"
        VERBATIM)
endif()

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
# 单元测试（只在 debug profile 里构建与运行，见 AGENTS.md 第 5 节）
# ---------------------------------------------------------------------------
if(NOT FLOWKEYD_BUILD_TESTS)
    message(STATUS "flowkeyd: unit tests are disabled in this profile (FLOWKEYD_BUILD_TESTS=OFF)")
    return()
endif()

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
flowkeyd_add_test(tst_autostart LIBS flowkeyd_platform)
flowkeyd_add_test(tst_layout LIBS flowkeyd_platform)
flowkeyd_add_test(tst_input LIBS flowkeyd_platform)
flowkeyd_add_test(tst_command_line LIBS flowkeyd_platform)
flowkeyd_add_test(tst_instance LIBS flowkeyd_platform)
flowkeyd_add_test(tst_log_tail)
flowkeyd_add_test(tst_window_match)
flowkeyd_add_test(tst_audio LIBS flowkeyd_platform)
flowkeyd_add_test(tst_interactive LIBS flowkeyd_platform)
flowkeyd_add_test(tst_menu_model LIBS flowkeyd_models)
flowkeyd_add_test(tst_help_model LIBS flowkeyd_models)
flowkeyd_add_test(tst_desktop_table LIBS flowkeyd_platform)
flowkeyd_add_test(tst_power_table LIBS flowkeyd_platform)

# 冒烟测试要真的链接 Lua，才能证明 vendor/lua 编得进来。
target_link_libraries(tst_smoke PRIVATE flowkeyd::lua)
