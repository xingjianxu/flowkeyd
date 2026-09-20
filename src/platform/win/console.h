// 控制台工具：把 GUI 子系统进程的输出接回启动它的终端。
//
// 为什么需要：`qt_add_executable(... WIN32 ...)` 意味着 GUI 子系统，进程默认
// **没有**控制台。于是一个从终端启动的 `flowkeyd --help` 会什么都不打印
// （看起来像“命令什么都没做”）。见 AGENTS.md 第 10 节。
#pragma once

#include <QString>

namespace flowkeyd::platform::win {

/// 尝试接上父进程的控制台（双击启动时会失败，那就忽略）。
///
/// 只有当本进程还没有可用的标准输出句柄时才去开 `CONOUT$`，
/// 因此 `flowkeyd --help > out.txt` 这样的重定向不会被抢走。
bool attachParentConsole();

/// 原样写出一段文本（换行由调用方自己带上，必须是 `\r\n`）。
void writeStdout(const QString &text);

/// 同上，写标准错误。
void writeStderr(const QString &text);

/// 当前控制台窗口是否只属于本进程（从终端启动时不属于）。
uint consoleIsOwned();

/// 隐藏属于本进程的控制台窗口（托盘模式下这么做）。
bool hideConsoleWindow();

} // namespace flowkeyd::platform::win
