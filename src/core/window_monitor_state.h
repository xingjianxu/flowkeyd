// 窗口摆放的**持久记忆**：某个程序上一次被摆到过哪一块显示器。
//
// 只服务 `monitor = "last"`（以及 `settings.window_monitor = "last"`）这一种
// 取值：`"cursor"` 每次现查光标，`"none"` 根本不摆，都不碰这个文件。
//
// 条目的身份是**可执行文件名的小写**（`chrome.exe`）：`window_rule` 与 `window`
// 动作的 `process` 匹配的正是它，而且大小写在不同来源（`EnumWindows` 给的
// `szExeFile`、`window_rule` 里手写的 `Code`）里并不一致。
//
// 存的是**设备名**（`\\.\DISPLAY2`）而不是序号：序号会随排列顺序变化（把显示器
// 拖到左边、主显示器改了都会让 `2` 变成别的意义），设备名跟着那块屏幕走。设备名
// 找不到（那块屏拔了、换了接口）时这次就什么都不做，并写一条 debug 日志。
//
// 状态文件与配置文件同目录（`windows.json`）。**它不进仓库、也不用用户手写**：
// flowkeyd 自己在摆放窗口时维护它。
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

#include <optional>

namespace flowkeyd::core {

/// 一个程序 → 它上一次被摆到的那块显示器（设备名）。
struct WindowMonitorState
{
    /// 可执行文件名（小写）→ 显示器设备名（`\\.\DISPLAY2`）。
    QMap<QString, QString> monitors;
};

/// 状态文件的路径：与配置文件同一个目录下的 `windows.json`。
///
/// `configPath` 是相对路径时按当前目录解析（与 `--config` 的行为一致）。
QString windowMonitorStatePath(const QString &configPath);

/// 程序身份键：可执行文件名去掉首尾空白、转成小写。拿不到进程名时返回空串
/// （空串表示“记不了”，调用方应当跳过）。
QString windowMonitorKey(const std::optional<QString> &executableName);

/// 解析一份状态文件。**容错**：坏 JSON、字段类型不对、条目不是非空字符串……
/// 一律当作「那部分为空」，`error` 里（非空时）给一句英文说明。
WindowMonitorState parseWindowMonitorState(const QByteArray &json, QString *error = nullptr);

/// 序列化成稳定的、人能看懂的 JSON（两个空格缩进、字段顺序固定、带版本号）。
QByteArray serializeWindowMonitorState(const WindowMonitorState &state);

/// 记住一个程序落在了哪块显示器上。返回是否**真的**变了（没变就不必落盘）。
bool rememberWindowMonitor(WindowMonitorState *state,
                           const QString &programKey,
                           const QString &device);

/// 查一个程序记住的显示器设备名；没有记住（或 `programKey` 为空）时返回
/// `std::nullopt`。
std::optional<QString> rememberedWindowMonitor(const WindowMonitorState &state,
                                               const QString &programKey);

} // namespace flowkeyd::core
