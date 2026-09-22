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

/// 窗口是否命中这条规则：`title` 与 `process` 都要满足（与 `window` 动作同一套
/// 子串匹配；`process` 拿不到时不算匹配）。
bool windowMatchesRule(const WindowRule &rule,
                       const QString &title,
                       const std::optional<QString> &executableName);

} // namespace flowkeyd::core
