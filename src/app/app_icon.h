// 应用图标：托盘、全部 QML 窗口与 Qt 消息框都用它。
//
// 另外这里也画**托盘上那个数字徽标**（`trayBadgeIcon`）：圆角渐变底 + 红色粗体数字，
// 由守护进程 500 ms 轮询一次当前虚拟桌面 / 工作空间后换成对应的那一张
// （见 `app/runtime.*` 与 `platform/win/tray.*`）。
//
// 这里的 PNG 帧编在 **exe 自己的 qrc** 里（`qt_add_resources(flowkeyd "app_icons" ...)`，
// 见 CMakeLists.txt）；exe 在资源管理器 / 任务栏里显示的那个图标是另一份东西
// ——`assets/flowkeyd.ico`，由 windres 通过 `assets/flowkeyd.rc.in` 嵌进二进制。
// 两者都由仓库根目录的 `logo.svg` 生成（`cmake --build --preset debug --target icons`，
// 生成工具是 `tools/icon_gen`，见 AGENTS.md 第 10 节）。
#pragma once

#include "core/desktop_badge.h"

#include <QIcon>

namespace flowkeyd::app {

/// 多尺寸的应用图标（16/20/24/32/40/48/64/128/256，够托盘在各种缩放下都取到
/// 原生尺寸的那一帧）。资源缺失时返回一个**空** QIcon，调用方自己决定怎么回退
/// （托盘会退回系统图标，不会留下一个看不见的图标）。
QIcon applicationIcon();

/// 托盘上的数字徽标：工作空间号（深绿）+ 虚拟桌面号（蓝），红色最粗字重。
///
/// 画什么字由 `core::trayBadgeParts()` 决定：`1..9` 就是数字本身，`10` 以上是
/// `9+`；两个号都有时**左上角工作空间号、右下角虚拟桌面号**，底色沿对角线分成
/// 左上绿 / 右下蓝，中间一条 `/` 方向的白线（每个号因此能画得很大）；只有一个号
/// 时它居中，整块用它自己的颜色（虚拟桌面蓝、工作空间绿）。一个号都拿不到时返回
/// **空** QIcon，调用方据此退回 `applicationIcon()` —— 不要把误导性的 `0` 画到
/// 托盘上。
///
/// 与 `applicationIcon()` 一样，每个尺寸单独渲染（不缩放位图），并且只在 GUI
/// 线程上调用（QIcon/QPixmap 的常规要求）。
QIcon trayBadgeIcon(const core::TrayBadge &badge);

} // namespace flowkeyd::app
