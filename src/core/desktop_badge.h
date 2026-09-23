// 托盘图标上的「当前是第几号虚拟桌面」徽标文字（纯逻辑，可单测）。
//
// 图标本身（蓝色渐变圆角底 + 白色粗体数字）画在 `app/app_icon.cpp`，用的是 QtGui；
// 这里只决定**画什么字**、**用多大字号**，所以能在没有桌面会话时被 Qt Test
// 直接验证（与 `core/log_tail`、`core/version` 同一个理由）。
//
// 它服务于一个很小的目标：托盘通知区域里一眼看出现在是第几号虚拟桌面
// （项目所有者 2026-09 要求，见 AGENTS.md 第 2 节第 20 条）。
#pragma once

#include <QString>

namespace flowkeyd::core {

/// 徽标上要画的字符。
///
/// * `number` 在 `1..9` 时就是它本身（`3`）；
/// * `number >= 10` 时是 `9+`（项目所有者 2026-09 拍板：两位数在 16 逻辑像素的
///   托盘图标上已经挤成一团，宁可表达“还有更多”——所以 10 号桌面之后看不出
///   具体是第几张，这是刻意的取舍）；
/// * `number <= 0` 返回**空串**，表示“读不到当前桌面”（锁屏、非交互会话、
///   虚拟桌面接口对不上版本表……）。调用方据此退回应用图标，而不是画个 `0`。
QString desktopBadgeText(int number);

/// 在这个图标尺寸（逻辑像素边长）上画 `characters` 个字符所需的字体像素大小。
///
/// 一位数占满画面的大半，两位数要把字号收下来；结果不小于 6 像素，
/// 免得在高 DPI 的小尺寸上糊成一团。`characters <= 0` 时返回 0（没有字要画）。
int desktopBadgeFontPixels(int iconSize, int characters);

} // namespace flowkeyd::core
