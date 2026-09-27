# =============================================================================
# 发布运行时精简：**以脚本方式运行**（`cmake -P`），由 CMakeLists.txt 在
# `windeployqt` 之后调用。
#
# windeployqt 会把 Qt 的插件、QML 模块与**全部** Quick Controls 样式都拷进来，
# 另外 release 的 exe（RelWithDebInfo）还带着 40 MB 以上的调试符号。flowkeyd 只用
# FluentWinUI3 这一个样式，所以这里把确定用不到的东西删掉：
#
#     实测 build/dist-release：1378 个文件 / 149.8 MB → 211 个文件 / 62.9 MB
#
# 用法（全部参数都由 CMakeLists.txt 传进来；想手工试一遍就照下面这样跑）：
#
#     cmake -DPRUNE_DIR=build/dist-release -DPROFILE=release \
#           -DSTRIP_EXE=build/dist-release/flowkeyd.exe \
#           -DOBJCOPY=C:/Qt/Tools/mingw1310_64/bin/objcopy.exe \
#           -P cmake/PruneRuntime.cmake
#
# 参数：
#   PRUNE_DIR   要精简的目录（Qt 运行时旁边，即 exe 所在目录）—— 必填
#   PROFILE     `debug` / `release`。debug 保留 `qmltooling`（Qt Creator 的 QML
#               调试要用）并且**不** strip 符号（要在 gdb 里调试那个构建）
#   STRIP_EXE   要 strip 调试符号的 exe；留空表示不 strip
#   OBJCOPY     objcopy 的路径（strip 用）
#
# 清单的依据、为什么不用 windeployqt 的 `--no-*` 开关、为什么 Basic/Fusion 删不得、
# 为什么 FluentWinUI3 的 .qml/.png 可以从磁盘删掉，见 AGENTS.md 第 10 节的
# “发布包精简”。**不要往这里加“顺手”的删除项**：每一条都要能说出理由。
# =============================================================================

if(NOT PRUNE_DIR OR NOT IS_DIRECTORY "${PRUNE_DIR}")
    message(FATAL_ERROR "prune_runtime: PRUNE_DIR must be an existing directory (got '${PRUNE_DIR}')")
endif()

if(NOT PROFILE)
    set(PROFILE release)
endif()

function(_prune_stats out_count out_bytes)
    file(GLOB_RECURSE _files LIST_DIRECTORIES false "${PRUNE_DIR}/*")
    list(LENGTH _files _count)
    set(_bytes 0)
    foreach(_f ${_files})
        file(SIZE "${_f}" _size)
        math(EXPR _bytes "${_bytes} + ${_size}")
    endforeach()
    set(${out_count} ${_count} PARENT_SCOPE)
    set(${out_bytes} ${_bytes} PARENT_SCOPE)
endfunction()

_prune_stats(_files_before _bytes_before)

# --- 1) 用不到的 Quick Controls 样式 ----------------------------------------
# 只有 FluentWinUI3 在用（src/qml/*.qml 里写的就是它），其余样式是
# `QtQuick.Controls` 的 qmldir 里那批 `optional import ... auto` 被
# qmlimportscanner 扫进来的一整套。
#
# **Basic 与 Fusion 必须留着**：`QtQuick.Controls` 的 qmldir 里有
# `default import QtQuick.Controls.Basic auto`，FluentWinUI3 的 qmldir 里有
# `import QtQuick.Controls.Fusion auto`；删掉之后三个弹窗直接加载不了
# （实测报 `module "QtQuick.Controls.Basic" is not installed` /
# `module "QtQuick.Controls.Fusion" is not installed`）。
set(_prune_files
    Qt6QuickControls2Imagine.dll
    Qt6QuickControls2Material.dll
    Qt6QuickControls2Universal.dll
    Qt6QuickControls2ImagineStyleImpl.dll
    Qt6QuickControls2MaterialStyleImpl.dll
    Qt6QuickControls2UniversalStyleImpl.dll
    Qt6QuickControls2WindowsStyleImpl.dll
)

# --- 2) 不用的库与插件 ------------------------------------------------------
# Qt6Svg / qsvg / qsvgicon / qgif / qjpeg：图标与 QML 里的图一律是 PNG
# （assets/icons/*.png 编进 qrc，FluentWinUI3 的样式图也是 PNG），
# 刻意不依赖 Qt6Svg（见 AGENTS.md 第 10 节“应用图标”）。
# Qt6Quick3DUtils：只有 `qmltooling/qmldbg_quick3dprofiler.dll` 需要它。
# opengl32sw：软件 OpenGL 回退。Qt Quick 在 Windows 上走 D3D11 RHI
# （没有显卡驱动时 Windows 自带的 WARP 也能画），用不到它；**它必须与
# D3Dcompiler_47 同进同退**：少了 D3D 编译器 Qt 会退回 OpenGL，那时才需要软件回退。
# D3Dcompiler_47：Windows 10 起系统自带（本机 `C:\Windows\System32\` 里就有）。
list(APPEND _prune_files
    imageformats/qgif.dll
    imageformats/qjpeg.dll
    imageformats/qsvg.dll
    iconengines/qsvgicon.dll
    Qt6Svg.dll
    Qt6Quick3DUtils.dll
    opengl32sw.dll
    D3Dcompiler_47.dll
)
foreach(_f ${_prune_files})
    file(REMOVE "${PRUNE_DIR}/${_f}")
endforeach()

# --- 3) 用不到的插件目录 ----------------------------------------------------
# networkinformation：Qt Network 的可选插件（拿不到只是不能判断「网络是否可用」，
# 在线更新自己会报网络错误）。generic/qtuiotouch：TUIO 协议触摸输入。
#
# **`tls/` 不能整个删掉**（2026-09 起）：在线更新要走 https，Qt 在 Windows 上默认
# 用系统自带的 Schannel 后端，那是一个插件（`qschannelbackend.dll`）；没有它
# `QNetworkAccessManager` 会直接报 TLS 后端缺失。OpenSSL 与 cert-only 两个后端
# 用不到（本机有 Schannel），删掉。
# `Qt6Network.dll` 本身必须留：Qt6Qml/Qt6Quick 直接依赖它。
file(REMOVE_RECURSE
    "${PRUNE_DIR}/networkinformation"
    "${PRUNE_DIR}/generic"
)
file(REMOVE
    "${PRUNE_DIR}/tls/qopensslbackend.dll"
    "${PRUNE_DIR}/tls/qcertonlybackend.dll"
)
# qmltooling：13 个 QML 调试/剖析插件，只有 `-qmljsdebugger` 那条路会加载它们。
if(NOT PROFILE STREQUAL "debug")
    file(REMOVE_RECURSE "${PRUNE_DIR}/qmltooling")
endif()

# --- 4) 用不到的样式模块目录（QML 侧） --------------------------------------
file(REMOVE_RECURSE
    "${PRUNE_DIR}/qml/QtQuick/NativeStyle"          # 只有 Controls.Windows 用
    "${PRUNE_DIR}/qml/QtQuick/Controls/Imagine"
    "${PRUNE_DIR}/qml/QtQuick/Controls/Material"
    "${PRUNE_DIR}/qml/QtQuick/Controls/Universal"
    "${PRUNE_DIR}/qml/QtQuick/Controls/Windows"     # 里面的大头是 780 KB 的样式插件
)

# --- 5) 磁盘上与插件内嵌资源重复的 QML 文件 ---------------------------------
# FluentWinUI3（含 impl）的样式插件把整套 .qml 与 .png **内嵌在自己的 qrc 里**，
# qmldir 里的 `prefer :/qt-project.org/imports/QtQuick/Controls/FluentWinUI3/`
# 就是指向那里；磁盘上这一份只是给 Qt Creator / qmllint 看的（官方 Qt 两处都装）。
# 删掉之后弹窗、日志窗口照常（实测零 QML 警告），省掉 848 个文件。
file(GLOB_RECURSE _prune_glob
    "${PRUNE_DIR}/qml/QtQuick/Controls/FluentWinUI3/*.qml"
    "${PRUNE_DIR}/qml/QtQuick/Controls/FluentWinUI3/*.png"
)

# --- 6) 只给工具用的类型信息 ------------------------------------------------
# plugins.qmltypes 是给 Qt Creator / qmllint 的类型补全用的，运行时不需要。
file(GLOB_RECURSE _prune_glob2 "${PRUNE_DIR}/plugins.qmltypes")
if(_prune_glob OR _prune_glob2)
    file(REMOVE ${_prune_glob} ${_prune_glob2})
endif()

# --- 7) windeployqt 造出来的空目录 ------------------------------------------
# 例如 `qml/QtQuick/Dialogs`（模块目录在、里面一个文件都没有）。逐层删到没有了
# 为止 —— 空目录多一个少一个都不影响运行，所以这里不需要维护具体名单。
set(_removed_empty TRUE)
while(_removed_empty)
    set(_removed_empty FALSE)
    file(GLOB_RECURSE _dirs LIST_DIRECTORIES true "${PRUNE_DIR}/*")
    foreach(_d ${_dirs})
        if(IS_DIRECTORY "${_d}")
            file(GLOB _children "${_d}/*")
            if(NOT _children)
                file(REMOVE_RECURSE "${_d}")
                set(_removed_empty TRUE)
            endif()
        endif()
    endforeach()
endwhile()

# --- 8) strip 掉 exe 的调试符号（只 release） --------------------------------
# RelWithDebInfo 带 `-g`：实测 47.4 MB 的 exe 里 43 MB 是 `.debug_*` 段。
# 符号只留在 debug profile（build/windows-debug），要调试崩溃就用它。
if(PROFILE STREQUAL "debug")
    message(STATUS "prune_runtime: debug profile — keeping qmltooling and the debug symbols")
elseif(STRIP_EXE AND EXISTS "${STRIP_EXE}")
    if(NOT OBJCOPY OR NOT EXISTS "${OBJCOPY}")
        message(WARNING "prune_runtime: objcopy not found — '${STRIP_EXE}' keeps its debug symbols")
    else()
        execute_process(
            COMMAND "${OBJCOPY}" --strip-all "${STRIP_EXE}"
            RESULT_VARIABLE _strip_rc
            ERROR_VARIABLE _strip_err)
        if(NOT _strip_rc EQUAL 0)
            message(FATAL_ERROR "prune_runtime: objcopy --strip-all failed for '${STRIP_EXE}': ${_strip_err}")
        endif()
    endif()
endif()

# --- 汇总（构建日志里能一眼看到精简成果） -----------------------------------
_prune_stats(_files_after _bytes_after)
math(EXPR _kb_before "${_bytes_before} / 1024")
math(EXPR _kb_after "${_bytes_after} / 1024")
message(STATUS "prune_runtime: ${PRUNE_DIR} -> ${_files_after} file(s), ${_kb_after} KB (was ${_files_before} file(s), ${_kb_before} KB)")
