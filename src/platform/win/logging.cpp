#include "platform/win/logging.h"

#include "platform/win/console.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTime>

#include <cstdio>

namespace flowkeyd::platform::win {

namespace {

struct LoggerState
{
    QMutex mutex;
    LogLevel level = LogLevel::Info;
    bool color = false;
    QFile file;
    QString path;
};

LoggerState &state()
{
    static LoggerState instance;
    return instance;
}

const char *ansiFor(LogLevel level)
{
    switch (level) {
    case LogLevel::Error:
        return "\x1b[31;1m";
    case LogLevel::Warn:
        return "\x1b[33;1m";
    case LogLevel::Info:
        return "\x1b[36m";
    case LogLevel::Debug:
        return "\x1b[35m";
    case LogLevel::Trace:
        return "\x1b[90m";
    case LogLevel::Off:
        break;
    }
    return "";
}

} // namespace

std::optional<LogLevel> parseLogLevel(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("off") || key == QLatin1String("none")) {
        return LogLevel::Off;
    }
    if (key == QLatin1String("error")) {
        return LogLevel::Error;
    }
    if (key == QLatin1String("warn") || key == QLatin1String("warning")) {
        return LogLevel::Warn;
    }
    if (key == QLatin1String("info")) {
        return LogLevel::Info;
    }
    if (key == QLatin1String("debug")) {
        return LogLevel::Debug;
    }
    if (key == QLatin1String("trace")) {
        return LogLevel::Trace;
    }
    return std::nullopt;
}

QString logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Off:
        return QStringLiteral("OFF");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Warn:
        return QStringLiteral("WARN");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Trace:
        return QStringLiteral("TRACE");
    }
    return QStringLiteral("INFO");
}

bool initLogging(LogLevel level, bool color, const std::optional<QString> &filePath, QString *error)
{
    LoggerState &logger = state();
    QMutexLocker locker(&logger.mutex);
    logger.level = level;
    logger.color = color;
    logger.file.close();
    logger.path.clear();
    if (!filePath.has_value() || filePath->isEmpty()) {
        return true;
    }
    const QFileInfo info(*filePath);
    if (!info.absoluteDir().exists()) {
        if (!QDir().mkpath(info.absolutePath())) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot create the log directory %1").arg(info.absolutePath());
            }
            return false;
        }
    }
    logger.file.setFileName(*filePath);
    if (!logger.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1: %2").arg(*filePath, logger.file.errorString());
        }
        return false;
    }
    logger.path = *filePath;
    return true;
}

void setLogLevel(LogLevel level)
{
    LoggerState &logger = state();
    QMutexLocker locker(&logger.mutex);
    logger.level = level;
}

LogLevel currentLogLevel()
{
    LoggerState &logger = state();
    QMutexLocker locker(&logger.mutex);
    return logger.level;
}

void shutdownLogging()
{
    LoggerState &logger = state();
    QMutexLocker locker(&logger.mutex);
    logger.file.close();
}

QString logFilePath()
{
    LoggerState &logger = state();
    QMutexLocker locker(&logger.mutex);
    return logger.path;
}

void logMessage(LogLevel level, const QString &message)
{
    LoggerState &logger = state();
    QMutexLocker locker(&logger.mutex);
    if (level == LogLevel::Off || static_cast<int>(level) > static_cast<int>(logger.level)) {
        return;
    }
    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString levelText = logLevelName(level).leftJustified(5);
    const QString plain = QStringLiteral("%1 %2 %3\n").arg(stamp, levelText, message);
    if (logger.color) {
        const QString colored = QStringLiteral("\x1b[90m%1\x1b[0m %2%3\x1b[0m %4\r\n")
                                    .arg(stamp, QString::fromLatin1(ansiFor(level)), levelText, message);
        writeStderr(colored);
    } else {
        QString console = plain;
        console.replace(QLatin1String("\n"), QLatin1String("\r\n"));
        writeStderr(console);
    }
    if (logger.file.isOpen()) {
        logger.file.write(plain.toUtf8());
        logger.file.flush();
    }
}

} // namespace flowkeyd::platform::win
