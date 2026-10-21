// `window_rule` 的**纯逻辑**：显示器选择与窗口摆放几何。
//
// `platform/win/monitor.cpp` 只负责 `EnumDisplayMonitors` / `SetWindowPlacement`
// 这类 Win32 调用；「哪块显示器是第 2 块」「窗口该摆到哪个矩形里」这些判断放在
// 这里，于是它们可以被 Qt Test 直接覆盖（见 AGENTS.md 第 4 节的「分层铁律」）。
#pragma once

#include "core/config.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace flowkeyd::core {

/// 虚拟屏幕坐标里的一个矩形（像素）。
struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// 一块显示器的描述。`platform/win/monitor` 从 `MONITORINFOEXW` 转过来。
struct MonitorDescription
{
    /// 设备名，例如 `\\.\DISPLAY2`。
    QString device;
    /// 完整显示器矩形。
    Rect bounds;
    /// 工作区（扣掉任务栏）。
    Rect work;
    bool primary = false;
};

/// 按**排列顺序**排序：先比较 `x`，再比较 `y`，最后用规范化后的设备名兜底
/// （保证同样的输入总是得到同样的顺序）。
///
/// `window_rule` 里 `monitor = 1` 的“1”就是排序后的第一个。
std::vector<MonitorDescription> sortedMonitors(std::vector<MonitorDescription> monitors);

/// 在**已排序**的列表里挑出 `ref` 指向的显示器。找不到返回 `std::nullopt`
/// （例如显示器还没接上、序号越界、设备名拼错）。
std::optional<std::size_t> selectMonitor(const std::vector<MonitorDescription> &sorted,
                                         const MonitorRef &ref);

/// 设备名的规范化：剥掉 `\\.\` 前缀、去掉首尾空白、转成大写。
/// 比较与「显示器重新接入」检测都用它。
QString normalizeDeviceName(const QString &device);

/// 相对 `previous`，`current` 里**新出现**的显示器设备名。
///
/// 这就是「之前断开的显示器重新接上」的判据：设备名从无到有。纯粹的
/// 分辨率/排列变化（设备名集合不变）不算。
QStringList newMonitorDevices(const QStringList &previous, const QStringList &current);

/// 在 `[0, count)` 里从下标 `current` 走 `delta` 步。
///
/// `wrap` 为真时首尾相接（在最前/最后一步越界就回另一端），为假时越界返回
/// `std::nullopt`。`count == 0` 或 `current >= count` 同样返回 `std::nullopt`。
///
/// `window` 动作的 `move_prev_desktop` / `move_next_desktop`（循环）与
/// `move_left_monitor` / `move_right_monitor`（不循环）共用它，于是这两套语义
/// 都能被单测直接覆盖。
std::optional<std::size_t> stepIndex(std::size_t count,
                                     std::size_t current,
                                     int delta,
                                     bool wrap);

/// 计算窗口应该被摆到的矩形（虚拟屏幕坐标）。
///
/// `current` 是窗口当前的矩形（拿不到时调用方给一个合理的默认值）。
///   * `maximize` 为真：返回目标显示器的工作区。
///   * 否则：保持窗口大小（`width`/`height` 给了就覆盖），位置按 `x`/`y`
///     （相对工作区左上角）或居中，最后夹进工作区；窗口比工作区还大时对齐
///     工作区左上角，保证标题栏可见。
Rect placementRect(const Rect &current,
                   const MonitorDescription &target,
                   bool maximize,
                   std::optional<std::int32_t> x,
                   std::optional<std::int32_t> y,
                   std::optional<std::uint32_t> width,
                   std::optional<std::uint32_t> height);

/// 窗口是否**不在** `monitor` 这块屏上（显示器拔插之后要不要把它物理上搬回去）。
///
/// `normalPosition` 是窗口的**还原矩形**（`WINDOWPLACEMENT::rcNormalPosition`，
/// 最大化 / 最小化 / 被我们藏起来的窗口都看它）：
///   * 显示器被拔出：Windows 会把窗口留在已经不存在的坐标上（窗口的属主那块屏
///     从 `EnumDisplayMonitors` 里消失了，`rcNormalPosition` 还指着它）→ 必须搬到
///     剩下的屏上，否则用户再也看不见它；
///   * 显示器重新接入：`WorkspaceState` 已经把窗口物归原主，窗口本人却还留在
///     刚才那块屏上 → 搬回去；
///   * 本来就好端端地压在那块屏上（含跨屏的情况）→ 不动它。
///
/// 「在这块屏上」= 与它的**边界**有非空重叠。用 `bounds` 而不是 `work`：任务栏
/// 挡住的窗口不需要重新居中。
bool windowIsOffMonitor(const Rect &normalPosition, const MonitorDescription &monitor);

/// 窗口是否命中这条规则：`title` 与 `process` 都要满足（与 `window` 动作同一套
/// 子串匹配；`process` 拿不到时不算匹配）。
bool windowMatchesRule(const WindowRule &rule,
                       const QString &title,
                       const std::optional<QString> &executableName);

/// 命中这个窗口的**第一条**规则（规则按书写顺序匹配，先写的赢）。
///
/// 一条都没命中时返回 `nullptr`。`window_rule` 的摆放与 `window` 动作的
/// 「跨虚拟桌面怎么走」共用这一条查找。
const WindowRule *firstWindowRule(const std::vector<WindowRule> &rules,
                                  const QString &title,
                                  const std::optional<QString> &executableName);

/// 这个窗口所在的程序在配置里**声明了固定的虚拟桌面**吗？
///
/// 判据：命中这个窗口的第一条 `window_rule` 写了 `desktop`。`window` 动作的
/// `activate` 靠它决定窗口在别的虚拟桌面上时往哪边走：
///   * 声明了固定桌面 —— 用户按快捷键就是想过去，把**视图**切到它那张桌面
///     （`window_rule` 的语义：这个程序属于那张桌面）；
///   * 没声明 —— 窗口跟着用户走，把它**搬到当前桌面**再激活。不这样做的话
///     Windows 会把视图切到窗口上一次所在的那张桌面上。
bool windowRulePinsDesktop(const std::vector<WindowRule> &rules,
                           const QString &title,
                           const std::optional<QString> &executableName);

/// 新出现的窗口应该归入第几个工作空间。
///
/// 优先级（与 `window_rule` 的其它字段一致，先写的规则赢）：
///   1. 命中这个窗口的第一条 `window_rule` 写了 `workspace`（`app.workspace` 展开成它）
///      → 用规则里的空间号，**规则赢**；
///   2. 否则用 `settings.workspace.new_window`（`0` = `"active"`）；
///   3. 都没写 → `std::nullopt`，也就是那块显示器**此刻的活动空间**（默认行为）。
///
/// 命中规则但规则没写 `workspace` 时**不**去看后面的规则：规则按书写顺序匹配，
/// 先命中的那条就是这个窗口的规则（与 `firstWindowRule()` 的口径一致），它没提
/// 工作空间就退到全局默认。
///
/// 注意它只决定**归属**，不碰几何，也不负责显隐 —— 空间号与目标显示器当前活动
/// 空间不同时，`core::WorkspaceState::windowAppeared()` 会立刻把窗口藏起来
/// （`settings.workspace.new_window = N` 与 `window_rule.workspace` 同一个机制）。
std::optional<std::uint32_t> workspaceForNewWindow(const std::vector<WindowRule> &rules,
                                                   const QString &title,
                                                   const std::optional<QString> &executableName,
                                                   const Settings &settings);

/// `window` 动作的 `op = "activate"` 该把窗口摆到哪一块显示器上。
///
/// 优先级（显式写的赢）：
///   1. 动作自己的 `monitor`（`window("activate", { monitor = 2 })`）；
///   2. 命中这个窗口的第一条 `window_rule` 的 `monitor`；
///   3. 都没有时用 `settings.windowMonitor`（默认 = 光标所在的那一块）。
///
/// 返回 `Kind::None` 表示这次不碰窗口的显示器（`monitor = "none"`）。
MonitorRef windowActionMonitor(const std::optional<MonitorRef> &actionMonitor,
                               const WindowRule *rule,
                               const Settings &settings);

} // namespace flowkeyd::core
