// 「窗口该摆到哪块显示器上」的表示。
//
// 独立成一个头（而不是留在 `core/config.h` 里）是因为 `core/action.h` 也要用它
// —— `window` 动作自己可以写 `monitor`，而 `config.h` 本来就包含 `action.h`。
//
// 三种**写死**的目标（序号 / 主显示器 / 设备名）之外，还有三种**运行时才知道**
// 的目标：
//   * `"cursor"` —— 光标现在所在的那一块（默认值，见 `settings.window_monitor`）；
//   * `"last"`   —— 这个程序上一次被摆到过的那一块（记在 `<配置目录>/windows.json`）；
//   * `"none"`   —— 不碰窗口的显示器（`window_rule` / `window` 动作上的显式退出）。
// 前两种由 `monitorRefIsDynamic()` 认出来、`"none"` 表示「这次不摆」。
#pragma once

#include <QString>

#include <cstdint>

namespace flowkeyd::core {

/// `window_rule` / `window` 动作 / `settings.window_monitor` 里的目标显示器。
///
/// 写法：
///   * `monitor = 2`          —— 1 起，按「先左后右、再上后下」的排列顺序；
///   * `monitor = "primary"`  —— 主显示器；
///   * `monitor = "DISPLAY2"` —— `EnumDisplayMonitors` 的设备名（可写全名
///     `\\.\DISPLAY2`，比较时会剥掉 `\\.\` 前缀并忽略大小写）；
///   * `monitor = "cursor"`   —— 光标所在的那一块（摆放时现查）；
///   * `monitor = "last"`     —— 这个程序上一次被摆到过的那一块；
///   * `monitor = "none"`     —— 不碰窗口的显示器。
struct MonitorRef
{
    enum class Kind { Index, Primary, Device, Cursor, Last, None };

    Kind kind = Kind::Index;
    /// `Kind::Index`：1 起的序号。
    std::uint32_t index = 1;
    /// `Kind::Device`：设备名。
    QString device;

    /// 供日志、摘要与校验错误使用的可读形式。
    QString describe() const;
};

/// 配置里**写死**了是哪一块显示器吗（序号 / 主显示器 / 设备名）？
///
/// 只有写死的那种才会让 `window_rule` 默认最大化（见 `core/config.cpp`）：
/// `"cursor"` / `"last"` 是动态的，一个“跟着鼠标走”的窗口不该因为目标变了就
/// 突然铺满屏幕。
bool monitorRefIsConcrete(const MonitorRef &ref);

/// 目标要等到运行时才知道吗（光标所在的那一块 / 上次记住的那一块）？
bool monitorRefIsDynamic(const MonitorRef &ref);

} // namespace flowkeyd::core
