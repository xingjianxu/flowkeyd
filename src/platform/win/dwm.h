// 运行时解析的 `dwmapi.dll` 导出。
//
// 这里只需要一个函数：`DwmSetWindowAttribute`，配合
// `DWMWA_TRANSITIONS_FORCEDISABLED` 按**窗口**关掉 DWM 的过渡动画
// （最小化 / 最大化 / 还原时的缩放·淡入效果）。
//
// 为什么不用 `SystemParametersInfo(SPI_SETANIMATION)`：那个是**系统级**开关，
// 会改掉用户在“辅助功能 / 视觉效果”里的设置、影响所有程序。这里要的是
// “只有 flowkeyd 触发的这次状态变化不带动画”，所以用这个 per-window 属性。
//
// `dwmapi` 不在本仓库链接的导入库集合里（见 AGENTS.md 第 2 节），
// 因此和 `win32u` 一样用 `LoadLibraryW` + `GetProcAddress` 解析，绝不静态链接。
//
// 实测（Windows 11 build 26200，与 oskeyd 的 `scripts/repro-anim.ps1` 结论一致）：
// 设置后最小化在一帧内完成、还原从 ~150 ms 降到 ~20 ms；而
// `DwmGetWindowAttribute` 对这个属性返回 `E_INVALIDARG`，**读不回来**，
// 所以调用方只能“设 TRUE → ShowWindow → 设回 FALSE”。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

namespace flowkeyd::platform::win::dwm {

/// `dwmapi!DwmSetWindowAttribute` 在这个进程里可用吗。
bool available();

/// 要求 DWM 跳过（`disabled = true`）或恢复（`false`）`hwnd` 的过渡动画。
///
/// 失败时 `error` 里是英文原因；拿不到 `dwmapi` 时也算失败，但调用方
/// 只记一行 debug：动画多一点不是动作失败。
bool forceDisableTransitions(HWND hwnd, bool disabled, QString *error);

} // namespace flowkeyd::platform::win::dwm
