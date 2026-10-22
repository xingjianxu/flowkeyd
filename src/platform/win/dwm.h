// 运行时解析的 `dwmapi.dll` 导出。
//
// 这里只需要两个函数：`DwmSetWindowAttribute`（配合
// `DWMWA_TRANSITIONS_FORCEDISABLED` 按**窗口**关掉 DWM 的过渡动画：最小化 /
// 最大化 / 还原时的缩放·淡入效果）与 `DwmGetWindowAttribute`（读
// `DWMWA_CLOAKED`：这个窗口到底有没有真的显示出来）。
//
// 为什么不用 `SystemParametersInfo(SPI_SETANIMATION)`：那个是**系统级**开关，
// 会改掉用户在“辅助功能 / 视觉效果”里的设置、影响所有程序。这里要的是
// “只有 flowkeyd 触发的这次状态变化不带动画”，所以用这个 per-window 属性。
//
// `dwmapi` 不在本仓库链接的导入库集合里（见 AGENTS.md 第 2 节），
// 因此和 `win32u` 一样用 `LoadLibraryW` + `GetProcAddress` 解析，绝不静态链接。
//
// 实测（Windows 11 build 26200）：
// 设置后最小化在一帧内完成、还原从 ~150 ms 降到 ~20 ms；而
// `DwmGetWindowAttribute` 对这个属性返回 `E_INVALIDARG`，**读不回来**，
// 所以调用方只能“设 TRUE → ShowWindow → 设回 FALSE”。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <vector>

namespace flowkeyd::platform::win::dwm {

/// `dwmapi!DwmSetWindowAttribute` 在这个进程里可用吗。
bool available();

/// 要求 DWM 跳过（`disabled = true`）或恢复（`false`）`hwnd` 的过渡动画。
///
/// 失败时 `error` 里是英文原因；拿不到 `dwmapi` 时也算失败，但调用方
/// 只记一行 debug：动画多一点不是动作失败。
bool forceDisableTransitions(HWND hwnd, bool disabled, QString *error);

/// 把**一批**窗口的过渡动画一起关掉（`disabled = true`）或一起恢复（`false`）。
///
/// 与 `TransitionGuard` 是同一个属性（`DWMWA_TRANSITIONS_FORCEDISABLED`），区别只在
/// 作用域：切一次工作空间涉及一屏窗口，这一屏的消失 / 出现必须是**一个**视觉动作，
/// 所以整批之前设 `TRUE`、整批之后才设回 `FALSE` —— 不许“设一个、`ShowWindow` 一个、
/// 复原一个”（DWM 可能已经按复原后的状态开始画那一批的过渡了）。
///
/// 返回成功设上的窗口数；拿不到 `dwmapi` 时是 0，只记一行 debug（动画多一点不是
/// 动作失败）。
std::size_t setTransitionsDisabled(const std::vector<HWND> &windows, bool disabled);

/// 一次会改变窗口可见性 / 状态的调用，顺带（默认）关掉 DWM 过渡动画的 RAII 守卫。
///
/// `DWMWA_TRANSITIONS_FORCEDISABLED` **读不回来**，所以只能“设 TRUE → 调用 →
/// 设回 FALSE”，也就是假定这个窗口本来是带动画的。DWM 在过渡**开始**时读它，
/// 因此作用域只要盖住那一次调用就够。
///
/// `animate = true` 什么都不做（保留系统动画）。设不上时只记一行 debug：没有
/// 合成器（或无头会话）时动画多一点，不该让这次调用失败。
///
/// 两个用户：`window::applyTo()`（`window` 动作的 `animate` 参数）与
/// `workspace::hide() / show()`（切工作空间的显隐一律不要动画，见
/// `docs/notes/pitfalls.md`「`SW_HIDE` 本身会播 DWM 淡出」）。工作空间的**批量**
/// 显隐走的是下面的 `setTransitionsDisabled()`：那一批窗口的作用域要一样长。
class TransitionGuard
{
public:
    TransitionGuard(HWND hwnd, bool animate);
    ~TransitionGuard();

    TransitionGuard(const TransitionGuard &) = delete;
    TransitionGuard &operator=(const TransitionGuard &) = delete;

private:
    HWND m_hwnd = nullptr;
    bool m_active = false;
};

/// `DWMWA_CLOAKED`：这个窗口被 shell 或应用**藏起来了**吗。
///
/// 这是「窗口现在到底有没有显示出来」唯一可靠的判据：被 cloaked 掉的窗口
/// `IsWindowVisible` 仍然是 `TRUE`、坐标也还在屏幕里、照样收 `WM_PAINT`，
/// 只有这个属性说实话（微软自己的说法见 “How can I detect that my window has
/// been suppressed from the screen by the shell?”）。常见来源：
/// * shell 把窗口搬到别的虚拟桌面；
/// * shell 把一个 UWP / 输入法宿主窗口收起来（本机实测：`TextInputHost.exe`
///   的「Windows 输入体验」）；
/// * 应用自己用 `DWMWA_CLOAK` 把自己的窗口藏起来。
///
/// **三种情况在值上分不开**，调用方要自己区分（窗口切换器因此还要问一句
/// 「它在别的虚拟桌面上吗」，见 `window::isSwitchableWindow`）。
///
/// 读不到时（拿不到 `dwmapi`、窗口正在销毁）返回 `false`，也就是按“它在显示”
/// 处理 —— 宁可在列表里多一条，也不要把用户的好窗口藏起来。
bool isCloaked(HWND hwnd);

} // namespace flowkeyd::platform::win::dwm
