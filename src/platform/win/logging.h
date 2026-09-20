// 控制台 / 文件日志器。
//
// 纯文本、分级别、可选 ANSI 颜色；同时写 stderr 与一个追加的文件。
// **日志一律英文**（见工作约定第 4 条）：它们是机器可断言的字符串，
// 中英混排的日志在终端里很难读。
#pragma once

#include <QString>

#include <optional>

namespace flowkeyd::platform::win {

enum class LogLevel { Off = 0, Error = 1, Warn = 2, Info = 3, Debug = 4, Trace = 5 };

/// 解析 `off|error|warn|info|debug|trace`（`warning`/`none` 作为别名）。
std::optional<LogLevel> parseLogLevel(const QString &name);

/// 日志行里显示的级别词（`ERROR`、`WARN`……）。
QString logLevelName(LogLevel level);

/// 初始化日志器。`filePath` 为空表示只写 stderr。
///
/// 目录不存在时会创建。返回 false 时 `error` 里是英文原因（用户显式给出
/// `--log-file` 时应视为致命错误；默认日志文件打不开则继续跑）。
bool initLogging(LogLevel level, bool color, const std::optional<QString> &filePath, QString *error);

/// 运行时改级别（`reload` 之后跟着配置走）。
void setLogLevel(LogLevel level);
LogLevel currentLogLevel();

/// 关闭文件句柄（退出时用；不调用也能靠进程退出收尾）。
void shutdownLogging();

/// 当前日志文件路径（`--log-window` 与托盘菜单要用）。没有文件时返回空。
QString logFilePath();

void logMessage(LogLevel level, const QString &message);

inline void logTrace(const QString &message) { logMessage(LogLevel::Trace, message); }
inline void logDebug(const QString &message) { logMessage(LogLevel::Debug, message); }
inline void logInfo(const QString &message) { logMessage(LogLevel::Info, message); }
inline void logWarn(const QString &message) { logMessage(LogLevel::Warn, message); }
inline void logError(const QString &message) { logMessage(LogLevel::Error, message); }

} // namespace flowkeyd::platform::win
