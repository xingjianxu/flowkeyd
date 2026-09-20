// 提权：`ShellExecuteW("runas")` 自重启 + 降级 + 命令行转发。
//
// 提权只发生在守护进程模式（不变量 11）；`--check`/`--list`/`--list-keys`
// 在调用到这里之前就已经返回了。
#pragma once

#include "platform/win/ffi.h"

#include <QString>
#include <QStringList>

namespace flowkeyd::platform::win {

/// 本进程是否已经以管理员权限运行。
bool isElevated();

/// 按 `CommandLineToArgvW` 的规则给一个参数加引号（含转义内部引号）。
QString quoteArg(const QString &arg);

/// 把一组参数拼成 `lpParameters`。
QString buildParameters(const QStringList &args);

/// 本进程的完整命令行（含程序名），来自 `GetCommandLineW`。
QStringList commandLineArguments();

/// 以管理员身份重启本进程，并转发完整命令行（补上 `--elevated`）与工作目录。
///
/// 用户点“否”时返回 false 并填 `error`。**本机实测返回码是 5（拒绝访问）而不是
/// 1223**，所以调用方只判返回值，不猜具体错误码。
bool relaunchElevated(QString *error);

} // namespace flowkeyd::platform::win
