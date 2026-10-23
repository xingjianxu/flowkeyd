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
///
/// 最小化的窗口按它的**还原位置**判断（`MonitorFromWindow()` 对它们不可靠，
/// 见实现）。
std::optional<std::size_t> indexForWindow(const std::vector<core::MonitorDescription> &sorted,
                                          HWND hwnd);

/// **光标**所在显示器在已排序列表里的下标（`monitor = "cursor"` 用）。
/// 拿不到光标位置或那块显示器不在列表里时返回 `std::nullopt`。
std::optional<std::size_t> indexForCursor(const std::vector<core::MonitorDescription> &sorted);

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

/// 把窗口搬到 `sorted[target]` 那一块显示器上（`monitor = "cursor"` / `"last"`
/// 与四个 `move_*` op 走的都是这一条）。
///
/// **保留最大化状态**：最大化窗口在新显示器上仍然最大化；普通窗口保持原有
/// 大小并居中到目标显示器的工作区；最小化的窗口只更新它的还原位置（不会被弹
/// 出来）。全程不激活窗口。
///
/// 成功时 `detail` 是 `"placed 1280x800 at 1920,320 on monitor 2/2"` 这样一行。
bool moveToMonitor(HWND hwnd,
                   const std::vector<core::MonitorDescription> &sorted,
                   std::size_t target,
                   QString *detail,
                   QString *error);

/// 把窗口移到相邻的显示器（`delta`：`-1` 左边，`+1` 右边）。
///
/// “左/右”按 `core::sortedMonitors` 的排列顺序（先左后右、再上后下）。
/// 虚拟桌面完全不变（移动的是显示器，不是桌面）。摆放细节见 `moveToMonitor`。
///
/// 与虚拟桌面不同，这里**不循环**：没有更左/更右的显示器时失败（`error` 里
/// 说明原因）。成功时 `detail` 是 `"maximized 1920x1040 at 1920,0 on monitor 2/2"`
/// 这样一行。
bool moveToAdjacentMonitor(HWND hwnd, int delta, QString *detail, QString *error);

} // namespace flowkeyd::platform::win::monitor
