// 工作空间的**事件**：钩子线程观察到的东西，以及「我们自己引起的事件」的抑制表。
//
// 数据流（`plan.md` M5）：钩子线程只把事件入队 → 动作工作线程上的
// `core::WorkspaceState` 算出效果 → `platform/win/workspace` 执行。回调里绝不碰
// 状态、绝不做慢事（`AGENTS.md` 第 7 节不变量 1）。
//
// 为什么抑制表也是纯逻辑：M0 实测 `ShowWindow(SW_HIDE)` 会**同时**产生一条
// `EVENT_OBJECT_HIDE` 和一条 `EVENT_OBJECT_SHOW`，而且分不出是谁干的。我们自己
// 的显隐如果不吞掉，状态机就会把自己的操作当成用户操作，和它自己打成一个环。
// 表本身要在**两条线程**上使用（工作线程登记、钩子线程命中），所以它自带一把锁：
// 锁里只有几次 map 操作，钩子回调的 300 ms 预算完全够（可比的是 `SendInput`
// 那种绝不能在持锁时做的事，见不变量 5）。
#pragma once

#include "core/workspace.h"

#include <QStringList>

#include <cstdint>
#include <map>
#include <mutex>
#include <utility>

namespace flowkeyd::core {

/// 一条要在动作工作线程上交给 `WorkspaceState` 的事件。
///
/// 只带「发生了什么」，不带「该怎么做」：显示器归属、虚拟桌面归属、能不能管这个
/// 窗口这类判断在平台层/分发层完成（钩子线程上做它们会拖慢回调）。
struct WorkspaceEvent
{
    enum class Kind {
        /// flowkeyd 启动：把已经在屏幕上的窗口收编进各自的**活动**空间。
        /// 由 `app::Runtime` 投递（钩子线程不枚举窗口）。
        Startup,
        /// 一个顶层窗口出现了（`EVENT_OBJECT_SHOW`，已按 350 ms 防抖）。
        WindowAppeared,
        /// 窗口销毁了：记录必须清掉，否则 `HWND` 复用会把别人的窗口当成它。
        WindowDestroyed,
        /// 窗口最小化（`EVENT_SYSTEM_MINIMIZESTART`）。
        WindowMinimized,
        /// 窗口从最小化还原（`EVENT_SYSTEM_MINIMIZEEND`）。
        WindowRestored,
        /// 窗口位置变了（`EVENT_OBJECT_LOCATIONCHANGE`，已按 350 ms 防抖）。
        /// 显示器可能因此改变 —— 归属的调整在 M7 做。
        WindowMoved,
        /// 前台窗口变了（`EVENT_SYSTEM_FOREGROUND`）。
        FocusChanged,
        /// 显示器集合变了：拔出与重新接入都在这一条里（`monitors` 是**现存**的
        /// 设备名，已排序、已规范化）。一个事件装两件事是刻意的 —— 状态机的
        /// `monitorsChanged(present)` 就是这么收的，拆成两条反而要它自己记基线。
        MonitorsChanged,
    };

    Kind kind = Kind::Startup;
    /// 单窗口事件的窗口（`Startup` / `MonitorsChanged` 为 0）。
    WindowId window = 0;
    /// `MonitorsChanged`：现存显示器的设备名（已排序、已规范化）。
    QStringList monitors;
};

/// 事件种类的英文名（日志用）。
const char *workspaceEventName(WorkspaceEvent::Kind kind);

/// 「我们自己引起的事件」的抑制表：执行效果**之前**登记，事件到达时命中即吞。
///
/// 键是（种类，窗口），值是过期时间。同一条事件被登记一次只吞一次 ——
/// `SW_HIDE` 产生的那一对 HIDE/SHOW 里我们只关心 SHOW（它看起来像“窗口出现了”）。
class ExpectedWorkspaceEvents
{
public:
    /// 登记的存活时间。要够长（目标进程忙时事件会晚到几百毫秒），也要够短
    /// （窗口真的在那一刻出现时不能被我们误吞）。
    static constexpr std::uint64_t kDefaultLifetimeMs = 1500;

    /// 登记一条即将由我们自己引起的事件。
    void expect(WorkspaceEvent::Kind kind,
                WindowId window,
                std::uint64_t nowMs,
                std::uint64_t lifetimeMs = kDefaultLifetimeMs);

    /// 命中就吞掉（并返回 true）。没登记过 / 已经过期返回 false。
    bool consume(WorkspaceEvent::Kind kind, WindowId window, std::uint64_t nowMs);

    /// 还登记着的条数（单测与诊断用）。
    std::size_t size() const;

    /// 全部忘掉（`reload` / 退出时用）。
    void clear();

private:
    using Key = std::pair<WorkspaceEvent::Kind, WindowId>;

    /// 清掉过期的条目。**调用方必须已经持有 `m_mutex`**。
    void pruneLocked(std::uint64_t nowMs);

    mutable std::mutex m_mutex;
    std::map<Key, std::uint64_t> m_expiry;
};

} // namespace flowkeyd::core
