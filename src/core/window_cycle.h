// 「在当前虚拟桌面 + 当前显示器的窗口之间轮转」的纯逻辑（`window` 动作的
// `cycle_next` / `cycle_prev`）。
//
// 为什么需要一点状态：激活一个窗口就会把它推到 Z 序最前面，所以“下一个窗口”
// 如果每次都由当前的 Z 序算，第一下跳到 A、第二下就会跳回刚才那个窗口，在两
// 个窗口之间反复横跳。这里的做法是**第一次按下时把顺序排定**（最近用过的在前），
// 之后一直沿用它，直到用户自己切到别的窗口（前台窗口不再是上次激活的那个）
// 或者那一批窗口变了（锚点不在了）才重新排一次。
//
// 这一层不认识 `HWND`：窗口身份由调用方给成一串不透明的 id（平台层用句柄的
// 十六进制文本），所以它只用 QtCore、可以被 Qt Test 直接测。
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace flowkeyd::core {

/// 轮转的状态（由 `Dispatcher` 在动作线程上持有，只有那条线程读写）。
struct WindowCycleState
{
    /// 上一次算出来的环形顺序（窗口 id，最近用过的在前）。
    QStringList ring;
    /// 上一次真正激活的那个窗口（一定是 `ring` 里的一环）。
    QString anchor;
};

/// `planWindowCycle()` 的结果。
struct WindowCyclePlan
{
    /// 这一次该激活的窗口 id。
    QString target;
    /// 下一次用的状态（`ring` 不变、`anchor` 换成这次的目标）。
    WindowCycleState state;
    /// 这次是不是重新排了一次环（只为了日志）。
    bool restarted = false;
};

/// 决定 `step`（`+1` 下一个 / `-1` 上一个）该激活哪一个窗口。
///
/// * `candidates`：这条快捷键看得见的那批窗口，按 Z 序（最近用过的在前），并且
///   调用方**已经**筛掉了别的虚拟桌面 / 别的显示器上的窗口、以及自己的窗口；
/// * `foreground`：当前前台窗口的 id（可能就是 `candidates` 的第一个）；
/// * 满足「上一次的锚点还在、它就是当前前台窗口、环里至少两个窗口」时沿用
///   上次的顺序（`restarted = false`），否则按 `candidates` 的顺序重排；
/// * 首尾相接：`+1` 从最后一个绕回第一个，`-1` 反过来。
///
/// 返回空 = 没什么可轮转的（候选为空，或者只有一个窗口而且它就是前台窗口）。
std::optional<WindowCyclePlan> planWindowCycle(const WindowCycleState &state,
                                               const QStringList &candidates,
                                               const QString &foreground,
                                               int step);

/// 轮转只在哪一块显示器上进行（`std::nullopt` = 不按显示器筛，转整张桌面）。
///
/// 次序是三步：**光标悬停**的那一块 → **前台窗口**所在的那一块 → 当前桌面上**压在最
/// 上面**的那个窗口所在的那一块。
///
/// 第一顺位是光标（2026-10 项目所有者拍板，见决策 38）：`Win+Ctrl+←/→` 想换的是
/// “鼠标这一边的窗口”。后两步只是 `GetCursorPos()` 问不出来时的兜底（前台窗口偶尔
/// 会空空如也，比如远程桌面窗口关掉的那一瞬），它们以前就是首选，现在让位给光标。
std::optional<std::size_t> chooseCycleMonitor(const std::optional<std::size_t> &cursorMonitor,
                                              const std::optional<std::size_t> &foregroundMonitor,
                                              const std::optional<std::size_t> &topmostMonitor);

} // namespace flowkeyd::core
