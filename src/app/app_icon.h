// 应用图标：托盘、全部 QML 窗口与 Qt 消息框都用它。
//
// 这里的 PNG 帧编在 **exe 自己的 qrc** 里（`qt_add_resources(flowkeyd "app_icons" ...)`，
// 见 CMakeLists.txt）；exe 在资源管理器 / 任务栏里显示的那个图标是另一份东西
// ——`assets/flowkeyd.ico`，由 windres 通过 `assets/flowkeyd.rc.in` 嵌进二进制。
// 两者都由仓库根目录的 `logo.svg` 生成（`cmake --build --preset debug --target icons`，
// 生成工具是 `tools/icon_gen`，见 AGENTS.md 第 10 节）。
#pragma once

#include <QIcon>

namespace flowkeyd::app {

/// 多尺寸的应用图标（16/20/24/32/40/48/64/128/256，够托盘在各种缩放下都取到
/// 原生尺寸的那一帧）。资源缺失时返回一个**空** QIcon，调用方自己决定怎么回退
/// （托盘会退回系统图标，不会留下一个看不见的图标）。
QIcon applicationIcon();

} // namespace flowkeyd::app
