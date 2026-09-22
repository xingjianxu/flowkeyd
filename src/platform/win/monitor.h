// 显示器枚举与窗口摆放（`window_rule` 的 Win32 后端）。
//
// 「哪块显示器是第几块」「窗口该摆到哪个矩形」这些判断在 `core/placement`
// （纯逻辑、可单测）；这里只剩 `EnumDisplayMonitors` 与 `SetWindowPlacement`。
#pragma once

#include "core/placement.h"
#include "platform/win/ffi.h"

#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace flowkeyd::platform::win::monitor {

/// 枚举当前所有显示器（顺序未定义；`core::sortedMonitors` 之后才有“第几块”）。
std::vector<core::MonitorDescription> list();

/// 窗口当前所在的显示器在**已排序**列表里的下标。
/// 窗口无效或枚举不到时返回 `std::nullopt`。
std::optional<std::size_t> indexForWindow(const std::vector<core::MonitorDescription> &sorted,
                                          HWND hwnd);

/// 窗口当前的矩形（屏幕坐标）。
///
/// 最小化 / 最大化的窗口返回它的**还原**矩形：`GetWindowRect` 在这两种状态下
/// 给的是无意义的值（最小化是 -32000，最大化是整块屏幕）。
std::optional<core::Rect> windowRect(HWND hwnd);

/// 把窗口摆到 `rect`；`maximize` 为真时进入最大化状态。
///
/// 最小化的窗口只更新它的“还原位置”，不会被弹出来（不打扰用户）。
/// 全程不激活窗口：`SetWindowPos` 带 `SWP_NOACTIVATE`，`SetWindowPlacement`
/// 本身也不激活；万一焦点还是被抢走了，会把它还给原来的前台窗口。
bool applyPlacement(HWND hwnd, const core::Rect &rect, bool maximize, QString *error);

} // namespace flowkeyd::platform::win::monitor
