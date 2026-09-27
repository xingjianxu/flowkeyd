#include "app/updater.h"

#include "app/update_archive.h"
#include "platform/win/logging.h"
#include "platform/win/process.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTime>

#include <optional>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

namespace {

/// 落盘的新 exe 的文件名（就在当前 exe 旁边 —— 同一个卷才能用改名瞬间替换）。
QString stagedExecutablePath(const QString &executablePath)
{
    const QFileInfo info(executablePath);
    return info.absolutePath() + QStringLiteral("/flowkeyd.exe.new");
}

QString userAgent(const QString &version)
{
    return QStringLiteral("flowkeyd/%1").arg(version);
}

} // namespace

Updater::Updater(QString currentVersion, QString executablePath, QObject *parent)
    : QObject(parent),
      m_currentVersion(std::move(currentVersion)),
      m_executablePath(std::move(executablePath))
{
    m_model = new UpdateModel(this);
}

void Updater::ensureNetwork()
{
    if (m_network != nullptr) {
        return;
    }
    m_network = new QNetworkAccessManager(this);
}

void Updater::abortInFlight()
{
    if (m_reply == nullptr) {
        return;
    }
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    // 先断开所有回到 `this` 的信号，再 abort：`abort()` 会发一次 finished，
    // 而那时我们已经不想处理它了（否则一次「取消」会被当成一次失败）。
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void Updater::checkForUpdates()
{
    abortInFlight();
    m_release = core::ReleaseInfo{};
    m_asset = core::ReleaseAsset{};
    m_archiveBytes.clear();
    m_stagedPath.clear();
    m_newVersion.clear();

    m_model->startChecking(m_currentVersion);
    emit windowRequested();
    startReleaseRequest();
}

void Updater::retry()
{
    checkForUpdates();
}

void Updater::startReleaseRequest()
{
    ensureNetwork();
    QNetworkRequest request(core::latestReleaseApiUrl());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent(m_currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // 检查更新永远要的是「现在」的结果，不走任何缓存。
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReleaseFinished(reply); });
}

void Updater::onReleaseFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return; // 过期 / 被取消
    }
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("无法访问 GitHub 检查更新"), reply->errorString());
        return;
    }
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (status != 200) {
        // 404 = 仓库还没有任何 release；403/429 = 匿名请求被 GitHub 限流。
        fail(tr("GitHub 返回了 HTTP %1").arg(status),
             QStringLiteral("GET %1 -> HTTP %2: %3")
                 .arg(core::latestReleaseApiUrl().toString())
                 .arg(status)
                 .arg(QString::fromUtf8(body).trimmed().left(300)));
        return;
    }

    QString parseError;
    const std::optional<core::ReleaseInfo> info = core::parseReleaseJson(body, &parseError);
    if (!info.has_value()) {
        fail(tr("无法解析 GitHub 的发布信息"), parseError);
        return;
    }
    m_release = *info;
    win::logInfo(QStringLiteral("update check: latest release is %1 (current %2)")
                     .arg(m_release.version, m_currentVersion));

    if (core::compareBuildVersions(m_currentVersion, m_release.version) <= 0) {
        win::logInfo(QStringLiteral("update check: already up to date"));
        m_model->showUpToDate();
        return;
    }

    QString assetError;
    const std::optional<core::ReleaseAsset> asset = core::pickUpdateAsset(m_release.assets, &assetError);
    if (!asset.has_value()) {
        fail(tr("这次发布没有可下载的升级包"), assetError);
        return;
    }
    m_asset = *asset;
    if (m_asset.sha256.isEmpty()) {
        // 老版本 API 没有 `digest` 字段：解压时 zip 自己的 CRC 仍然会挡下损坏的
        // 数据，另外还有 PE 魔数与尺寸检查，所以这里只记一条日志。
        win::logDebug(QStringLiteral("update check: the release asset has no sha256 digest; "
                                     "relying on the zip checksum"));
    }
    win::logInfo(QStringLiteral("update check: %1 is available (%2, %3 bytes)")
                     .arg(m_release.version, m_asset.name)
                     .arg(m_asset.size));
    m_model->showAvailable(m_release.version, m_release.notes, m_release.pageUrl.toString());
}

void Updater::startDownload()
{
    if (m_asset.url.isEmpty()) {
        fail(tr("没有可下载的升级包"), QStringLiteral("no update asset has been selected"));
        return;
    }
    abortInFlight();
    m_archiveBytes.clear();
    m_stagedPath.clear();
    m_model->startDownloading();

    ensureNetwork();
    QNetworkRequest request(m_asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent(m_currentVersion));
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // 下载是「有数据就不算超时」的：25 MB 的完整包在慢网下也不该被掐。
    request.setTransferTimeout(120000);

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64 total) {
                if (reply != m_reply) {
                    return;
                }
                m_model->setProgress(received, total);
            });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (reply != m_reply) {
            reply->readAll();
            return;
        }
        m_archiveBytes.append(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onArchiveFinished(reply); });
}

void Updater::onArchiveFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    m_reply = nullptr;
    // `readyRead` 可能赶不上最后一次数据。
    m_archiveBytes.append(reply->readAll());

    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("下载升级包失败"), reply->errorString());
        return;
    }
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
        fail(tr("下载升级包失败：HTTP %1").arg(status),
             QStringLiteral("GET %1 -> HTTP %2").arg(m_asset.url.toString()).arg(status));
        return;
    }
    if (m_archiveBytes.isEmpty()) {
        fail(tr("下载到的升级包是空的"), QStringLiteral("the download produced 0 bytes"));
        return;
    }

    if (!m_asset.sha256.isEmpty()) {
        const QString actual =
            QString::fromLatin1(QCryptographicHash::hash(m_archiveBytes, QCryptographicHash::Sha256).toHex());
        if (actual != m_asset.sha256) {
            fail(tr("下载的升级包校验失败（sha256 不一致）"),
                 QStringLiteral("sha256 mismatch for %1: expected %2, got %3")
                     .arg(m_asset.name, m_asset.sha256, actual));
            return;
        }
        win::logDebug(QStringLiteral("update download: sha256 verified (%1)").arg(actual));
    }

    m_model->startExtracting();
    const QString staged = stagedExecutablePath(m_executablePath);
    QString extractError;
    if (!extractUpdateExecutable(m_archiveBytes, staged, &extractError)) {
        fail(tr("无法解压升级包"), extractError);
        return;
    }

    // 把新 exe 的最后写入时间对齐到**发布那一天**：构建版本号里的日期段取自
    // exe 的最后写入时间（见 core/version.h），不对齐的话更新完重启，版本号会
    // 变成「下载那一天」，和更新窗口里显示的版本号对不上。
    if (const std::optional<QDate> date = core::buildVersionDate(m_release.version);
        date.has_value()) {
        QFile stagedFile(staged);
        if (stagedFile.open(QIODevice::ReadWrite)) {
            // 正午的本地时间：跨时区也是同一天。
            stagedFile.setFileTime(QDateTime(*date, QTime(12, 0)),
                                   QFileDevice::FileModificationTime);
            stagedFile.close();
        }
    }

    m_stagedPath = staged;
    m_newVersion = m_release.version;
    m_archiveBytes.clear();
    win::logInfo(QStringLiteral("update download: %1 is ready at %2")
                     .arg(m_newVersion, QDir::toNativeSeparators(m_stagedPath)));
    m_model->showReady();
    emit stagedReady();
}

void Updater::dismiss()
{
    abortInFlight();
    // 状态故意留着：用户再打开窗口时还看得到上一次的结果；下一次「检查更新」
    // 会把它复位。
}

void Updater::openReleasePage()
{
    const QString url = m_release.pageUrl.isValid()
        ? m_release.pageUrl.toString()
        : QStringLiteral("https://github.com/%1/releases").arg(core::updateRepository());
    QString error;
    if (!win::openTarget(url, std::nullopt, std::nullopt, core::ShowMode::Normal, &error)) {
        win::logError(QStringLiteral("could not open %1: %2").arg(url, error));
        return;
    }
    win::logInfo(QStringLiteral("opened the release page: %1").arg(url));
}

void Updater::fail(const QString &summary, const QString &detail)
{
    win::logWarn(QStringLiteral("update failed: %1 (%2)").arg(summary, detail));
    m_model->fail(summary, detail);
}

} // namespace flowkeyd::app
