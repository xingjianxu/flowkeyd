#include "core/version.h"

#include <QDateTime>
#include <QFileInfo>

// 单测与直接链接 flowkeyd_core 的工具（没有经过 flowkeyd 可执行文件）也要能编过，
// 所以这里保留一个兜底；正式构建由 CMakeLists.txt 把 PROJECT_VERSION 传进来。
#ifndef FLOWKEYD_VERSION
#define FLOWKEYD_VERSION "0.0.0"
#endif

namespace flowkeyd::core {

namespace {

QString unknown()
{
    return QStringLiteral("unknown");
}

} // namespace

QString unknownTimestamp()
{
    return unknown();
}

QString projectVersion()
{
    return QStringLiteral(FLOWKEYD_VERSION);
}

QString buildTimestampFromFile(const QString &path)
{
    if (path.isEmpty()) {
        return unknown();
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return unknown();
    }
    const QDateTime modified = info.lastModified();
    if (!modified.isValid()) {
        return unknown();
    }
    return modified.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString formatBuildVersion(const QString &version, const QString &timestamp)
{
    if (timestamp.isEmpty() || timestamp == unknown()) {
        return version;
    }
    return QStringLiteral("%1 (build %2)").arg(version, timestamp);
}

QString buildVersion(const QString &executablePath)
{
    return formatBuildVersion(projectVersion(), buildTimestampFromFile(executablePath));
}

} // namespace flowkeyd::core
