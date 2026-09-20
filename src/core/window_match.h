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
