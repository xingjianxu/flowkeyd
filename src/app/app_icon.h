// 应用图标：托盘、全部 QML 窗口与 Qt 消息框都用它。
//
// 另外这里也画**托盘上那个「第几号虚拟桌面」的徽标**（`desktopIcon`）：蓝色渐变
// 圆角方块 + 白色粗体数字，由守护进程 500 ms 轮询一次当前桌面后换成对应的那一张
// （见 `app/runtime.*` 与 `platform/win/tray.*`）。
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

/// 托盘上表示「当前是第几号虚拟桌面」的图标：蓝色渐变圆角方块 + 白色粗体数字
/// （配色取自 `logo.svg`，见 `renderDesktopBadge` 的说明）。
///
/// 画什么字由 `core::desktopBadgeText()` 决定：`1..9` 就是数字本身，`10` 以上是
/// `9+`。`number <= 0`（读不到当前桌面）时返回**空** QIcon，调用方据此退回
/// `applicationIcon()` —— 不要把一个 `0` 画到托盘上。
///
/// 与 `applicationIcon()` 一样，每个尺寸单独渲染（不缩放位图），并且只在 GUI
/// 线程上调用（QIcon/QPixmap 的常规要求）。
QIcon desktopIcon(int number);

} // namespace flowkeyd::app
