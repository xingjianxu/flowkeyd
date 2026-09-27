#include "app/update_archive.h"

#include "core/update_check.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <QtCore/private/qzipreader_p.h>

namespace flowkeyd::app {

namespace {

/// 低于这个尺寸不可能是我们的 exe（release 里 strip 过的那份也有 1.6 MB）。
constexpr int kMinimumExecutableBytes = 512 * 1024;

} // namespace

bool looksLikeExecutable(const QByteArray &bytes)
{
    if (bytes.size() < kMinimumExecutableBytes) {
        return false;
    }
    // PE 文件以 `MZ` 开头。这一条挡不住「拿了一个任意够大的文件」，但配合
    // 发布资产名的匹配与（有 digest 时的）sha256 校验已经足够：
    // 真正会拦住的错误是「下载被截断」「zip 里放的不是 exe」这类。
    return bytes.startsWith("MZ");
}

bool extractUpdateExecutable(const QByteArray &archive,
                             const QString &destination,
                             QString *error)
{
    QBuffer buffer;
    buffer.setData(archive);
    if (!buffer.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("could not read the downloaded archive from memory");
        }
        return false;
    }

    QZipReader reader(&buffer);
    if (!reader.isReadable()) {
        if (error != nullptr) {
            *error = QStringLiteral("the downloaded file is not a readable zip archive");
        }
        return false;
    }

    QString entryName;
    const QList<QZipReader::FileInfo> entries = reader.fileInfoList();
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.isFile && core::isUpdateArchiveEntry(entry.filePath)) {
            entryName = entry.filePath;
            break;
        }
    }
    if (entryName.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the release archive does not contain `flowkeyd.exe`");
        }
        return false;
    }

    const QByteArray bytes = reader.fileData(entryName);
    reader.close();
    if (!looksLikeExecutable(bytes)) {
        if (error != nullptr) {
            *error = QStringLiteral("`%1` in the archive is not a usable Windows executable "
                                    "(%2 bytes)")
                         .arg(entryName)
                         .arg(bytes.size());
        }
        return false;
    }

    QDir().mkpath(QFileInfo(destination).absolutePath());
    // `QSaveFile` 先写临时文件再原子替换：中途失败不会留下半个 exe 把下次更新搞乱。
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("could not write %1: %2")
                         .arg(QDir::toNativeSeparators(destination), file.errorString());
        }
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("could not write %1: %2")
                         .arg(QDir::toNativeSeparators(destination), file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace flowkeyd::app
