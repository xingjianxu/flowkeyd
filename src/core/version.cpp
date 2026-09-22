#include "core/version.h"

#include <QDateTime>
#include <QFileInfo>

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
    // 版本号就是它：紧凑、可直接当字符串比较，`yyyyMMddHHmm` 例如 202609221718。
    return modified.toString(QStringLiteral("yyyyMMddHHmm"));
}

QString buildVersion(const QString &executablePath)
{
    return buildTimestampFromFile(executablePath);
}

} // namespace flowkeyd::core
