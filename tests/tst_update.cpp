// 在线更新的纯逻辑单测：GitHub Release 响应的解析、升级包的挑选、构建版本比较、
// 以及从发布 zip 里取出新的 `flowkeyd.exe`。
//
// **不联网**：喂进去的是固定的 JSON 文本与现造出来的 zip 字节。zip 用 Qt 自己的
// `QZipWriter`（同样是 QtCore 私有头）生成，所以这里验证的是「发布脚本打出来的
// 那种 zip」能被读出来 —— `scripts/release.ps1` 用的就是同一个 zip 实现
// （.NET 的 `ZipArchive`，标准 deflate）。
#include <QtTest>

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>

#include <QtCore/private/qzipwriter_p.h>

#include "app/update_archive.h"
#include "core/update_check.h"

using namespace flowkeyd;

namespace {

/// 一份形状与 GitHub `GET /repos/{owner}/{repo}/releases/latest` 一致的响应
/// （字段取自本仓库真实发布过一次的内容，资产大小与 digest 是真的）。
const char *kReleaseJson = R"({
  "tag_name": "v26-09-28-abcdef0",
  "name": "flowkeyd 26-09-28-abcdef0",
  "body": "### 更新内容\n\n- 修了一个 bug\n- 加了一个功能\n",
  "html_url": "https://github.com/xingjianxu/flowkeyd/releases/tag/v26-09-28-abcdef0",
  "draft": false,
  "prerelease": false,
  "assets": [
    {
      "name": "flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip",
      "browser_download_url": "https://github.com/xingjianxu/flowkeyd/releases/download/v26-09-28-abcdef0/flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip",
      "size": 672254,
      "digest": "sha256:653E3839C450651CB6FEDDC7BB2120B5C7FF8DA7A3DD5FC1CE0C56B8DC1269FD"
    },
    {
      "name": "flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip.sha256",
      "browser_download_url": "https://github.com/xingjianxu/flowkeyd/releases/download/v26-09-28-abcdef0/flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip.sha256",
      "size": 113
    },
    {
      "name": "flowkeyd-26-09-28-abcdef0-windows-x64.zip",
      "browser_download_url": "https://github.com/xingjianxu/flowkeyd/releases/download/v26-09-28-abcdef0/flowkeyd-26-09-28-abcdef0-windows-x64.zip",
      "size": 25171547
    }
  ]
})";

core::ReleaseAsset asset(const QString &name, qint64 size = 1024)
{
    core::ReleaseAsset item;
    item.name = name;
    item.url = QUrl(QStringLiteral("https://example.invalid/%1").arg(name));
    item.size = size;
    return item;
}

/// 用 Qt 自己的 zip 写一个「发布包」的字节（与 scripts/release.ps1 的产物同一种
/// 格式：标准 deflate zip）。
QByteArray makeZip(const QString &entryName, const QByteArray &contents)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return bytes;
    }
    QZipWriter writer(&buffer);
    writer.addFile(entryName, contents);
    writer.close();
    return bytes;
}

/// 一个「像我们的 exe」的字节：`MZ` 开头、够大。
QByteArray fakeExecutable()
{
    return QByteArray("MZ") + QByteArray(600 * 1024, '\0');
}

} // namespace

class TestUpdate : public QObject
{
    Q_OBJECT

private slots:
    void apiUrlPointsAtTheReleasesEndpoint();
    void parsesLatestRelease();
    void rejectsBrokenJson();
    void prefersTheSlimAsset();
    void fallsBackToTheFullAsset();
    void reportsWhenNoAssetCanBeUsed();
    void recognizesOnlyTheExecutableEntry();
    void comparesBuildVersions();
    void parsesTheBuildDate();
    void looksLikeExecutableNeedsThePeMagic();
    void extractsExecutableFromSlimZip();
    void extractsExecutableFromFullZipLayout();
    void reportsMissingEntry();
    void reportsCorruptArchive();
};

void TestUpdate::apiUrlPointsAtTheReleasesEndpoint()
{
    QCOMPARE(core::updateRepository(), QStringLiteral("xingjianxu/flowkeyd"));
    QCOMPARE(core::latestReleaseApiUrl().toString(),
             QStringLiteral("https://api.github.com/repos/xingjianxu/flowkeyd/releases/latest"));
}

void TestUpdate::parsesLatestRelease()
{
    QString error;
    const std::optional<core::ReleaseInfo> info =
        core::parseReleaseJson(QByteArray(kReleaseJson), &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->tag, QStringLiteral("v26-09-28-abcdef0"));
    // 资产名里用的是去掉前导 `v` 的版本。
    QCOMPARE(info->version, QStringLiteral("26-09-28-abcdef0"));
    QCOMPARE(info->title, QStringLiteral("flowkeyd 26-09-28-abcdef0"));
    QVERIFY(info->notes.contains(QStringLiteral("修了一个 bug")));
    QCOMPARE(info->pageUrl.toString(),
             QStringLiteral("https://github.com/xingjianxu/flowkeyd/releases/tag/v26-09-28-abcdef0"));
    QCOMPARE(info->assets.size(), 3);

    QString assetError;
    const std::optional<core::ReleaseAsset> picked = core::pickUpdateAsset(info->assets, &assetError);
    QVERIFY2(picked.has_value(), qPrintable(assetError));
    QCOMPARE(picked->name, QStringLiteral("flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip"));
    QCOMPARE(picked->size, qint64(672254));
    // digest 的 `sha256:` 前缀要去掉，并且统一成小写。
    QCOMPARE(picked->sha256,
             QStringLiteral("653e3839c450651cb6feddc7bb2120b5c7ff8da7a3dd5fc1ce0c56b8dc1269fd"));
}

void TestUpdate::rejectsBrokenJson()
{
    QString error;
    QVERIFY(!core::parseReleaseJson(QByteArray("{ not json"), &error).has_value());
    QVERIFY(!error.isEmpty());

    // 合法 JSON 但没有 tag_name：这不是一个发布，不能拿来当更新源。
    QString missingTag;
    QVERIFY(!core::parseReleaseJson(QByteArray(R"({"name":"x"})"), &missingTag).has_value());
    QVERIFY(!missingTag.isEmpty());

    // 顶层不是对象。
    QString notObject;
    QVERIFY(!core::parseReleaseJson(QByteArray("[1,2,3]"), &notObject).has_value());
}

void TestUpdate::prefersTheSlimAsset()
{
    const QVector<core::ReleaseAsset> assets{
        asset(QStringLiteral("flowkeyd-26-09-28-abcdef0-windows-x64.zip")),
        asset(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip.sha256")),
        asset(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip")),
    };
    QString error;
    const std::optional<core::ReleaseAsset> picked = core::pickUpdateAsset(assets, &error);
    QVERIFY2(picked.has_value(), qPrintable(error));
    QCOMPARE(picked->name, QStringLiteral("flowkeyd-26-09-28-abcdef0-slim-windows-x64.zip"));
}

void TestUpdate::fallsBackToTheFullAsset()
{
    // 老版本发布脚本可能只传过完整包：仍然要能更新（只是多下 25 MB）。
    const QVector<core::ReleaseAsset> assets{
        asset(QStringLiteral("flowkeyd-26-09-28-abcdef0-windows-x64.zip")),
        asset(QStringLiteral("flowkeyd-26-09-28-abcdef0-windows-x64.zip.sha256")),
    };
    QString error;
    const std::optional<core::ReleaseAsset> picked = core::pickUpdateAsset(assets, &error);
    QVERIFY2(picked.has_value(), qPrintable(error));
    QCOMPARE(picked->name, QStringLiteral("flowkeyd-26-09-28-abcdef0-windows-x64.zip"));
}

void TestUpdate::reportsWhenNoAssetCanBeUsed()
{
    QString error;
    QVERIFY(!core::pickUpdateAsset({}, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("asset")));

    QString sourceOnly;
    QVERIFY(!core::pickUpdateAsset({asset(QStringLiteral("source.tar.gz"))}, &sourceOnly).has_value());
    QVERIFY(!sourceOnly.isEmpty());
}

void TestUpdate::recognizesOnlyTheExecutableEntry()
{
    QVERIFY(core::isUpdateArchiveEntry(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim/flowkeyd.exe")));
    QVERIFY(core::isUpdateArchiveEntry(QStringLiteral("flowkeyd.exe")));
    QVERIFY(core::isUpdateArchiveEntry(QStringLiteral("X/FLOWKEYD.EXE")));
    QVERIFY(!core::isUpdateArchiveEntry(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim/README.txt")));
    QVERIFY(!core::isUpdateArchiveEntry(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim/flowkeyd.exe.old")));
    QVERIFY(!core::isUpdateArchiveEntry(QStringLiteral("notflowkeyd.exe")));
}

void TestUpdate::comparesBuildVersions()
{
    // 日期不同按日期算。
    QCOMPARE(core::compareBuildVersions(QStringLiteral("26-09-27-95f40ac"),
                                        QStringLiteral("26-09-28-abcdef0")),
             1);
    QCOMPARE(core::compareBuildVersions(QStringLiteral("26-09-28-abcdef0"),
                                        QStringLiteral("26-09-27-95f40ac")),
             -1);
    // 完全相同。
    QCOMPARE(core::compareBuildVersions(QStringLiteral("26-09-28-abcdef0"),
                                        QStringLiteral("26-09-28-abcdef0")),
             0);
    // 同一天的不同构建（重打 / 修补）也算一次更新。
    QCOMPARE(core::compareBuildVersions(QStringLiteral("26-09-28-abcdef0"),
                                        QStringLiteral("26-09-28-1234567")),
             1);
    // 解析不出来的版本串：只在字符串完全相同时算「没有更新」。
    QCOMPARE(core::compareBuildVersions(QStringLiteral("unknown-abcdef0"),
                                        QStringLiteral("unknown-abcdef0")),
             0);
    QCOMPARE(core::compareBuildVersions(QStringLiteral("unknown-abcdef0"),
                                        QStringLiteral("26-09-28-abcdef0")),
             1);
}

void TestUpdate::parsesTheBuildDate()
{
    const std::optional<QDate> date = core::buildVersionDate(QStringLiteral("26-09-28-abcdef0"));
    QVERIFY(date.has_value());
    QCOMPARE(*date, QDate(2026, 9, 28));

    QVERIFY(!core::buildVersionDate(QString()).has_value());
    QVERIFY(!core::buildVersionDate(QStringLiteral("nonsense")).has_value());
    // 月日不合法。
    QVERIFY(!core::buildVersionDate(QStringLiteral("26-13-40-abcdef0")).has_value());
}

void TestUpdate::looksLikeExecutableNeedsThePeMagic()
{
    QVERIFY(!app::looksLikeExecutable(QByteArray()));
    // 尺寸不够：就算魔数对也不行。
    QVERIFY(!app::looksLikeExecutable(QByteArray("MZ")));
    // 够大但不是 PE。
    QVERIFY(!app::looksLikeExecutable(QByteArray(600 * 1024, 'x')));
    QVERIFY(app::looksLikeExecutable(fakeExecutable()));
}

void TestUpdate::extractsExecutableFromSlimZip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray exeBytes = fakeExecutable();
    const QByteArray zip =
        makeZip(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim/flowkeyd.exe"), exeBytes);
    QVERIFY(!zip.isEmpty());

    const QString destination = dir.filePath(QStringLiteral("flowkeyd.exe.new"));
    QString error;
    QVERIFY2(app::extractUpdateExecutable(zip, destination, &error), qPrintable(error));

    QFile file(destination);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray written = file.readAll();
    QCOMPARE(written.size(), exeBytes.size());
    QCOMPARE(written.left(2), QByteArray("MZ"));
}

void TestUpdate::extractsExecutableFromFullZipLayout()
{
    // 完整包里的目录层是 `flowkeyd-<版本>/`（没有 `-slim`），也要认得。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray zip =
        makeZip(QStringLiteral("flowkeyd-26-09-28-abcdef0/flowkeyd.exe"), fakeExecutable());
    QVERIFY(!zip.isEmpty());
    QString error;
    QVERIFY2(app::extractUpdateExecutable(zip, dir.filePath(QStringLiteral("new.exe")), &error),
             qPrintable(error));
}

void TestUpdate::reportsMissingEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray zip = makeZip(QStringLiteral("flowkeyd-26-09-28-abcdef0-slim/README.txt"),
                                   QByteArray("hello"));
    QString error;
    QVERIFY(!app::extractUpdateExecutable(zip, dir.filePath(QStringLiteral("new.exe")), &error));
    QVERIFY(error.contains(QStringLiteral("flowkeyd.exe")));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("new.exe"))));
}

void TestUpdate::reportsCorruptArchive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY(!app::extractUpdateExecutable(QByteArray("this is not a zip at all"),
                                          dir.filePath(QStringLiteral("new.exe")),
                                          &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(TestUpdate)
#include "tst_update.moc"
