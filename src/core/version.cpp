#include "core/version.h"

#include <QDateTime>
#include <QFileInfo>

#include "flowkeyd_revision.h"

// 生成头文件缺失时（例如有人绕开了 CMake 的配置步骤）也要能编过。
#ifndef FLOWKEYD_GIT_REVISION
#define FLOWKEYD_GIT_REVISION "unknown"
#endif

namespace flowkeyd::core {

namespace {

QString unknown()
{
    return QStringLiteral("unknown");
}

} // namespace

QString unknownValue()
{
    return unknown();
}

QString buildDateFromFile(const QString &path)
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
    return modified.toString(QStringLiteral("yy-MM-dd"));
}

QString sourceRevision()
{
    const QString revision = QStringLiteral(FLOWKEYD_GIT_REVISION);
    return revision.isEmpty() ? unknown() : revision;
}

QString buildVersion(const QString &executablePath)
{
    return buildDateFromFile(executablePath) + QLatin1Char('-') + sourceRevision();
}

} // namespace flowkeyd::core
