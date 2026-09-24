// 窗口匹配的**纯逻辑**：从枚举层拿到的标题/可执行文件名，判断它是否满足查询。
//
// `platform/win/window.cpp` 只负责 `EnumWindows`、取进程名与调用 Win32；
// 「这个窗口算不算匹配」这条判断放在这里，于是它可以被 Qt Test 直接覆盖
// （见 AGENTS.md 第 4 节的「分层铁律」）。
#pragma once

#include "core/action.h"

#include <QString>

#include <optional>

namespace flowkeyd::core {

/// 从完整路径里取出**小写**的可执行文件名：
/// `C:\Program Files\WezTerm\wezterm-gui.exe` -> `wezterm-gui.exe`。
/// 正斜杠与反斜杠都认；空路径返回空串。
QString executableBaseName(const QString &fullPath);

/// 窗口标题是否匹配查询（大小写无关子串）。查询为空表示不限制。
bool windowTitleMatches(const QString &title, const std::optional<QString> &needle);

/// 可执行文件名是否匹配查询（大小写无关子串）。
///
/// `executableName` 为空表示拿不到属主进程名（访问被拒）：那时返回 false，
/// 因为未知属主不能假定它匹配。
bool windowProcessMatches(const std::optional<QString> &executableName,
                          const std::optional<QString> &needle);

/// 一个候选窗口是否满足查询的**全部**条件。
///
/// 不含「可见」「没有属主」这类前置过滤，那些由枚举层先做掉
/// （见 `platform/win/window.cpp`）。
bool windowMatchesQuery(const WindowQuery &query,
                        const QString &title,
                        const std::optional<QString> &executableName);

// ---------------------------------------------------------------------------
// 顶层窗口的取舍（纯函数，可单测）
// ---------------------------------------------------------------------------

/// 一个顶层窗口的原始观察值：`platform/win/window.cpp` 把 Win32 能问到的都取好，
/// 判断交给这里的纯函数。这样「什么算一个程序窗口」这条规则可以在没有桌面的
/// 情况下被单测钉住。
struct TopLevelWindowFacts
{
    bool visible = false;
    /// 外壳自己的「Program Manager」（`Progman`）。
    bool isShellWindow = false;
    bool hasOwner = false;
    /// `WS_EX_TOOLWINDOW`：浮动工具栏式的辅助窗口，任务栏与 Alt+Tab 都不列它。
    bool toolWindow = false;
    /// `WS_EX_APPWINDOW`：应用显式要求这个窗口出现在任务栏 / Alt+Tab 里
    /// （即使它有属主）。
    bool appWindow = false;
    bool hasTitle = false;
    /// 窗口矩形非空。
    bool hasArea = false;
    /// `DWMWA_CLOAKED`：shell 或应用把这个窗口藏起来了。
    ///
    /// **它不等于“在别的虚拟桌面上”**：被搬到别的桌面的窗口也是 cloaked，
    /// 那种窗口切换器要留着（激活时会切过去），所以还要看 `onCurrentDesktop`。
    bool cloaked = false;
    /// 窗口就在**当前**虚拟桌面上（只在 `cloaked` 为真时才有意义）。
    bool onCurrentDesktop = true;
};

/// 「主窗口」判据：可见、不是 Program Manager、无属主（或带 `WS_EX_APPWINDOW`）、
/// 非工具窗口、有标题、尺寸非零。
///
/// 很多程序会拿无标题 / 工具窗口当消息汇或渲染宿主（拿 `process` 匹配时会一次
/// 命中一堆），真正的主窗口至少会写个标题。`window_rule` 的 `isPlaceableWindow`
/// 与窗口切换器共用这一条（`platform/win/window.h` 的 `isMainWindow`）。
bool isMainWindow(const TopLevelWindowFacts &window);

/// 窗口切换器（`windows` 动作）要不要列出这个窗口。
///
/// 判据是「主窗口」再加上一句「**shell 真的会显示它**」：被 cloaked 掉、又留在
/// 当前虚拟桌面上的窗口是 shell 藏起来的假窗口（本机实测：Windows 输入法的宿主
/// `TextInputHost.exe` 的「Windows 输入体验」——`IsWindowVisible` 为真、坐标与尺寸
/// 也正常，用户却切不过去），**不进切换器**。
///
/// 被 cloaked 但确实在**别的**虚拟桌面上的窗口要保留 —— 这正是切换器与 Alt+Tab
/// 不同的地方（`windows` 动作会切到那张桌面去），所以判据里必须带上
/// `onCurrentDesktop`。
bool isSwitchableWindow(const TopLevelWindowFacts &window);

// ---------------------------------------------------------------------------
// window 动作的决策（纯函数，可单测）
// ---------------------------------------------------------------------------

/// “窗口已经存在”时到底做什么。
enum class WindowPlan {
    /// 执行动作原本的 op。
    ApplyOp,
    /// `op = "activate"` 且 `toggle` 未关、目标又已经在前台：收起它。
    MinimizeBecauseActive,
};

/// `window` 动作的 `toggle` 边界。
///
/// 只有 `op = "activate"`、`toggle` 没被显式写成 `false`、且窗口已经在前台
/// 时才收起；其余情况一律执行原本的 op。`launch` 那条路径不走这里
/// （它不套用 `toggle`）。
WindowPlan planWindowAction(WindowOp op, std::optional<bool> toggle, bool alreadyActive);

/// 这个 op 会不会产生窗口过渡（也就是 `animate` 对它有没有意义）。
///
/// `close` 与 `toggle_topmost` 不产生过渡，`--check` 会拒绝写在这两个 op 上的
/// `animate`，而 `TransitionGuard` 也只在返回 true 时才去关 DWM 动画。
bool windowOpHasTransition(WindowOp op);

} // namespace flowkeyd::core
