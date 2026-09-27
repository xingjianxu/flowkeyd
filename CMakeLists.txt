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

# 图标生成工具（`tools/icon_gen`）需要 Qt6::Svg 把 logo.svg 光栅化，但**发布的 exe
# 不需要它**（运行时只用已经生成好的 PNG 帧，见 src/app/app_icon.cpp）。所以这里是
# 一次 QUIET 的补充查找：机器上没装 QtSvg 时只是少了那个手工工具，正常构建不受影响。
find_package(Qt6 QUIET COMPONENTS Svg)

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
# 版本号的 git 短修订（见 AGENTS.md 第 2 节第 12 条）：构建时用
# `git rev-parse --short HEAD` 取一次，写进一个只给 src/core/version.cpp 用的
# 生成头文件。`CMAKE_CONFIGURE_DEPENDS` 把 .git 的 HEAD 与当前分支的 ref 登记成
# configure 依赖，所以提交 / 切分支之后重新构建会自动刷新它；而「没提交、没改
# 源文件」的构建仍然是 `ninja: no work to do`（不要改成每次构建都生成：那会逼着
# exe 反复重链接，见 src/core/version.h 里的说明）。
# 拿不到 git（不在仓库里、或机器上没装）时留成 `unknown`。
# ---------------------------------------------------------------------------
set(FLOWKEYD_GIT_REVISION "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _flowkeyd_git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _flowkeyd_git_status
        ERROR_QUIET)
    if(_flowkeyd_git_status EQUAL 0 AND NOT _flowkeyd_git_revision STREQUAL "")
        set(FLOWKEYD_GIT_REVISION "${_flowkeyd_git_revision}")
    endif()
endif()

# .git 在正常克隆里是目录；worktree / submodule 里可能是文件，那就跳过。
set(_flowkeyd_git_meta "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD" _flowkeyd_head)
    if(_flowkeyd_head MATCHES "^ref: *(.+)$")
        string(STRIP "${CMAKE_MATCH_1}" _flowkeyd_head_ref)
        list(APPEND _flowkeyd_git_meta "${CMAKE_CURRENT_SOURCE_DIR}/.git/${_flowkeyd_head_ref}")
    endif()
endif()
foreach(_flowkeyd_dep IN LISTS _flowkeyd_git_meta)
    if(EXISTS "${_flowkeyd_dep}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_flowkeyd_dep}")
    endif()
endforeach()
unset(_flowkeyd_dep)
unset(_flowkeyd_head)
unset(_flowkeyd_head_ref)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_revision.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/flowkeyd_revision.h"
    @ONLY)
message(STATUS "flowkeyd: source revision ${FLOWKEYD_GIT_REVISION}")

# ---------------------------------------------------------------------------
# 应用图标（见 AGENTS.md 第 10 节）：
#   * exe 的 Windows 图标资源 = assets/flowkeyd.ico，由 windres 通过一个在配置时
#     生成的 .rc 嵌进二进制（任务栏 / 资源管理器 / 快捷方式看到的就是它）。
#     .rc 里写**绝对路径**是因为 windres 解析 `ICON` 的相对路径不是相对 .rc 文件；
#     生成的 .rc 落在 build/<preset>/generated/ 里，不进版本库（`*.rc` 也在
#     .gitignore 里）。
#   * 运行时图标 = assets/icons/*.png，编在 exe 自己的 qrc 里（src/app/app_icon.cpp
#     把它们拼成一个多尺寸 QIcon，托盘与弹窗都用它）。
# 两份产物都由 `logo.svg` 生成：`cmake --build --preset debug --target icons`。
# ---------------------------------------------------------------------------
if(WIN32)
    enable_language(RC)
    set(FLOWKEYD_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/flowkeyd.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/flowkeyd.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/flowkeyd.rc"
        @ONLY)
    set(FLOWKEYD_APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/flowkeyd.rc")
else()
    set(FLOWKEYD_APP_ICON_RC "")
endif()

# ---------------------------------------------------------------------------
# 纯逻辑层：不碰 Win32、不碰 Qt GUI，只用 QtCore。
# 因此它可以被 Qt Test 直接链接，也能在没有桌面会话时跑 --check/--list-keys。
# ---------------------------------------------------------------------------
add_library(flowkeyd_core STATIC
    src/core/action.cpp
    src/core/action.h
    src/core/config.cpp
    src/core/config.h
    src/core/desktop_badge.cpp
    src/core/desktop_badge.h
    src/core/engine.cpp
    src/core/engine.h
    src/core/keys.cpp
    src/core/keys.h
    src/core/log_tail.cpp
    src/core/log_tail.h
    src/core/placement.cpp
    src/core/placement.h
    src/core/template.cpp
    src/core/template.h
    src/core/version.cpp
    src/core/version.h
    src/core/window_match.cpp
    src/core/window_match.h
)

target_include_directories(flowkeyd_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# 生成头 `flowkeyd_revision.h` 只有 core/version.cpp 用，所以是 PRIVATE。
target_include_directories(flowkeyd_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
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
    src/app/window_list_model.cpp
    src/app/window_list_model.h
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
    src/platform/win/ime.cpp
    src/platform/win/ime.h
    src/platform/win/input.cpp
    src/platform/win/input.h
    src/platform/win/logging.cpp
    src/platform/win/logging.h
    src/platform/win/monitor.cpp
    src/platform/win/monitor.h
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
    src/qml/SwitchPopup.qml
)
set_source_files_properties(src/qml/LogWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS LogWindow.qml)
set_source_files_properties(src/qml/MenuPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS MenuPopup.qml)
set_source_files_properties(src/qml/HelpPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS HelpPopup.qml)
set_source_files_properties(src/qml/SwitchPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS SwitchPopup.qml)

qt_add_executable(flowkeyd
    WIN32
    src/main.cpp
    src/cli.cpp
    src/cli.h
    src/app/app_icon.cpp
    src/app/app_icon.h
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
    # Windows 图标资源（配置时生成，见上面那一节；非 Windows 上是空串）。
    ${FLOWKEYD_APP_ICON_RC}
)

qt_add_resources(flowkeyd "app_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    FILES
        assets/icons/flowkeyd-16.png
        assets/icons/flowkeyd-20.png
        assets/icons/flowkeyd-24.png
        assets/icons/flowkeyd-32.png
        assets/icons/flowkeyd-40.png
        assets/icons/flowkeyd-48.png
        assets/icons/flowkeyd-64.png
        assets/icons/flowkeyd-128.png
        assets/icons/flowkeyd-256.png
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

# 精简清单（cmake/PruneRuntime.cmake）变了之后必须重新链接一次 exe，而下面的
# POST_BUILD 只在链接时跑；不然改完清单直接 `cmake --build` 会得到
# `ninja: no work to do`，部署目录里还是旧内容（LINK_DEPENDS 正是为此存在的）。
if(WIN32 AND TARGET Qt6::windeployqt)
    set_property(TARGET flowkeyd APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PruneRuntime.cmake")
endif()

# ---------------------------------------------------------------------------
# 让构建产物自包含：`windeployqt` 把 Qt 与 MinGW 运行时的 DLL（以及 exe 用到的
# QML 模块）拷到 exe 同目录，于是双击 build/<preset>/flowkeyd.exe 就能启动，
# 不必再把 Qt 的 bin 加进 PATH（见 AGENTS.md 第 5 节）。
# 只给 flowkeyd 做；测试可执行文件仍然靠 ctest 注入 PATH。
#
# 紧接着 `cmake/PruneRuntime.cmake` 把 windeployqt 多拷的东西（没用到的 Quick
# Controls 样式、qmltooling、软件 OpenGL 回退……）删掉，release 还顺手 strip 掉
# exe 的调试符号。清单与理由见 AGENTS.md 第 10 节的“发布包精简”。
# debug profile 保留 qmltooling 与符号（Qt Creator 的 QML 调试、gdb 要用）。
# ---------------------------------------------------------------------------
if(WIN32 AND TARGET Qt6::windeployqt)
    if(FLOWKEYD_IS_DEBUG_PROFILE)
        set(FLOWKEYD_PRUNE_PROFILE debug)
    else()
        set(FLOWKEYD_PRUNE_PROFILE release)
    endif()
    add_custom_command(TARGET flowkeyd POST_BUILD
        COMMAND Qt6::windeployqt
            --no-translations
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/qml"
            "$<TARGET_FILE:flowkeyd>"
        COMMAND ${CMAKE_COMMAND}
            "-DPRUNE_DIR=$<TARGET_FILE_DIR:flowkeyd>"
            "-DPROFILE=${FLOWKEYD_PRUNE_PROFILE}"
            "-DSTRIP_EXE=$<TARGET_FILE:flowkeyd>"
            "-DOBJCOPY=${CMAKE_OBJCOPY}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PruneRuntime.cmake"
        COMMENT "windeployqt + prune: deploying the Qt runtime next to flowkeyd.exe"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# release profile 额外产出一个**干净的发布目录** build/dist-release：
# 里面只有 flowkeyd.exe 与它需要的 Qt / MinGW 运行时（DLL、QML 模块、插件），
# 没有构建系统文件、没有单测，可以直接拷贝到别的机器上运行。
# 注意这是“额外”产出：build/windows-release 自己仍然部署了一份运行时，方便
# 开发期直接跑；发布一律以 build/dist-release 为准（见 AGENTS.md 第 5 节）。
# 上面那条 POST_BUILD 已经把 build/windows-release 里的 exe 与运行时精简过，
# 所以这里拷过去的 exe 与 dist 里的是同一个二进制（SHA-256 相同）。
# 下面紧接着的这一次 prune **不再 strip**：源 exe 已经 strip 过了，而 objcopy
# **不是幂等的**（每跑一次都会重写 PE 头的 TimeDateStamp），再 strip 一次会让
# dist 的 exe 与 build/windows-release 的不再逐字节相同。
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
        COMMAND ${CMAKE_COMMAND}
            "-DPRUNE_DIR=${FLOWKEYD_DIST_DIR}"
            "-DPROFILE=release"
            "-DSTRIP_EXE="
            "-DOBJCOPY=${CMAKE_OBJCOPY}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PruneRuntime.cmake"
        COMMENT "windeployqt + prune: staging the clean release into build/dist-release"
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
# 图标生成工具：把 logo.svg 光栅化成 assets/flowkeyd.ico（exe 资源）与
# assets/icons/*.png（运行时 QIcon）。**EXCLUDE_FROM_ALL**，所以两条 profile 的
# 正常构建根本不碰它，也不会把它需要的 Qt6::Svg 拖进任何产物；改了 logo.svg 之后
# 手工跑一次：
#
#     cmake --build --preset debug --target icons
#
# 生成结果在 assets/ 下，是**要提交**的（别把 logo.svg 当成构建时输入：构建时
# 光栅化会把 Qt6::Svg 与 windeployqt 的插件都变成必需项，见 AGENTS.md 第 10 节）。
# ---------------------------------------------------------------------------
if(WIN32 AND TARGET Qt6::Svg)
    add_executable(flowkeyd_icon_gen EXCLUDE_FROM_ALL
        tools/icon_gen/main.cpp
    )
    # 纯 C++，没有 Q_OBJECT / .ui：关掉 AUTOMOC/AUTOUIC，免得白编一个空的
    # mocs_compilation.cpp（与 lua_static 上那一条同理，见 AGENTS.md 第 10 节）。
    set_target_properties(flowkeyd_icon_gen PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    target_link_libraries(flowkeyd_icon_gen PRIVATE Qt6::Gui Qt6::Svg)
    flowkeyd_strict_warnings(flowkeyd_icon_gen)
    add_custom_target(icons
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${FLOWKEYD_QT_BIN_DIR};${FLOWKEYD_MINGW_BIN_DIR};$ENV{PATH}"
            "$<TARGET_FILE:flowkeyd_icon_gen>"
            "${CMAKE_CURRENT_SOURCE_DIR}/logo.svg"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        DEPENDS flowkeyd_icon_gen
        COMMENT "icon_gen: rendering logo.svg into assets/flowkeyd.ico + assets/icons/*.png"
        VERBATIM)
else()
    message(STATUS "flowkeyd: Qt6::Svg not found; the `icons` regeneration target is unavailable")
endif()

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
flowkeyd_add_test(tst_version)
flowkeyd_add_test(tst_desktop_badge)
flowkeyd_add_test(tst_lua LIBS flowkeyd_lua)
flowkeyd_add_test(tst_autostart LIBS flowkeyd_platform)
flowkeyd_add_test(tst_layout LIBS flowkeyd_platform)
flowkeyd_add_test(tst_input LIBS flowkeyd_platform)
flowkeyd_add_test(tst_command_line LIBS flowkeyd_platform)
flowkeyd_add_test(tst_instance LIBS flowkeyd_platform)
flowkeyd_add_test(tst_log_tail)
flowkeyd_add_test(tst_window_match)
flowkeyd_add_test(tst_placement)
flowkeyd_add_test(tst_audio LIBS flowkeyd_platform)
flowkeyd_add_test(tst_interactive LIBS flowkeyd_platform)
flowkeyd_add_test(tst_menu_model LIBS flowkeyd_models)
flowkeyd_add_test(tst_help_model LIBS flowkeyd_models)
flowkeyd_add_test(tst_window_list_model LIBS flowkeyd_models)
flowkeyd_add_test(tst_desktop_table LIBS flowkeyd_platform)
flowkeyd_add_test(tst_power_table LIBS flowkeyd_platform)

# 冒烟测试要真的链接 Lua，才能证明 vendor/lua 编得进来。
target_link_libraries(tst_smoke PRIVATE flowkeyd::lua)
