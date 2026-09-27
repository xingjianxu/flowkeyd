#include "app/update_model.h"

#include <QtGlobal>

namespace flowkeyd::app {

namespace {

QString humanBytes(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kilobytes = static_cast<double>(bytes) / 1024.0;
    if (kilobytes < 1024.0) {
        return QStringLiteral("%1 KB").arg(QString::number(kilobytes, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(kilobytes / 1024.0, 'f', 1));
}

} // namespace

UpdateModel::UpdateModel(QObject *parent) : QObject(parent)
{
    m_statusText = statusForPhase();
}

QString UpdateModel::phaseName() const
{
    switch (m_phase) {
    case Phase::Idle:
        return QStringLiteral("idle");
    case Phase::Checking:
        return QStringLiteral("checking");
    case Phase::UpToDate:
        return QStringLiteral("uptodate");
    case Phase::Available:
        return QStringLiteral("available");
    case Phase::Downloading:
        return QStringLiteral("downloading");
    case Phase::Extracting:
        return QStringLiteral("extracting");
    case Phase::Ready:
        return QStringLiteral("ready");
    case Phase::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

QString UpdateModel::caption() const
{
    if (hasNewVersion()) {
        return QStringLiteral("flowkeyd 在线更新 — %1").arg(m_newVersion);
    }
    if (!m_currentVersion.isEmpty()) {
        return QStringLiteral("flowkeyd 在线更新 — 当前 %1").arg(m_currentVersion);
    }
    return QStringLiteral("flowkeyd 在线更新");
}

QString UpdateModel::notesPlaceholder() const
{
    return tr("（这次发布没有写更新说明）");
}

QString UpdateModel::statusForPhase() const
{
    switch (m_phase) {
    case Phase::Idle:
        return tr("准备就绪");
    case Phase::Checking:
        return tr("正在检查更新…");
    case Phase::UpToDate:
        return m_currentVersion.isEmpty() ? tr("已经是最新版本")
                                          : tr("已经是最新版本（%1）").arg(m_currentVersion);
    case Phase::Available:
        return tr("发现新版本 %1（当前 %2）").arg(m_newVersion, m_currentVersion);
    case Phase::Downloading:
        return tr("正在下载 %1…").arg(m_newVersion);
    case Phase::Extracting:
        return tr("正在解压并校验更新…");
    case Phase::Ready:
        return tr("更新已就绪，正在重启 flowkeyd…");
    case Phase::Failed:
        // 失败时的文案由 `fail()` 自己给（要区分「检查失败」/「下载失败」），
        // 所以这里只是兜底。
        return tr("更新失败");
    }
    return QString();
}

bool UpdateModel::busy() const
{
    return m_phase == Phase::Checking || m_phase == Phase::Downloading
        || m_phase == Phase::Extracting;
}

bool UpdateModel::canOpenRelease() const
{
    if (m_releaseUrl.isEmpty()) {
        return false;
    }
    return m_phase == Phase::Available || m_phase == Phase::Failed
        || m_phase == Phase::UpToDate;
}

QString UpdateModel::closeLabel() const
{
    return m_phase == Phase::Downloading ? tr("取消下载") : tr("关闭");
}

QString UpdateModel::progressText() const
{
    if (m_phase != Phase::Downloading && m_phase != Phase::Extracting) {
        return QString();
    }
    if (m_total > 0) {
        const int percent = qBound(0, static_cast<int>(m_progress * 100.0 + 0.5), 100);
        return QStringLiteral("%1%  ·  %2 / %3")
            .arg(percent)
            .arg(humanBytes(m_received), humanBytes(m_total));
    }
    return m_received > 0 ? humanBytes(m_received) : QString();
}

void UpdateModel::setPhase(Phase phase)
{
    m_phase = phase;
    if (phase != Phase::Failed) {
        m_statusText = statusForPhase();
    }
    emit stateChanged();
}

void UpdateModel::clearProgress()
{
    m_received = 0;
    m_total = 0;
    m_progress = 0.0;
    emit progressChanged();
}

void UpdateModel::setCurrentVersion(const QString &version)
{
    m_currentVersion = version;
}

void UpdateModel::reset()
{
    m_newVersion.clear();
    m_notes.clear();
    m_releaseUrl.clear();
    m_errorDetail.clear();
    clearProgress();
    setPhase(Phase::Idle);
}

void UpdateModel::startChecking(const QString &currentVersion)
{
    setCurrentVersion(currentVersion);
    m_newVersion.clear();
    m_notes.clear();
    m_releaseUrl.clear();
    m_errorDetail.clear();
    clearProgress();
    setPhase(Phase::Checking);
}

void UpdateModel::showUpToDate()
{
    m_newVersion.clear();
    m_errorDetail.clear();
    clearProgress();
    setPhase(Phase::UpToDate);
}

void UpdateModel::showAvailable(const QString &version,
                                const QString &notes,
                                const QString &releaseUrl)
{
    m_newVersion = version;
    m_notes = notes;
    m_releaseUrl = releaseUrl;
    m_errorDetail.clear();
    clearProgress();
    setPhase(Phase::Available);
}

void UpdateModel::startDownloading()
{
    clearProgress();
    setPhase(Phase::Downloading);
}

void UpdateModel::setProgress(qint64 received, qint64 total)
{
    m_received = received;
    m_total = total;
    m_progress = total > 0
        ? qBound(0.0, static_cast<double>(received) / static_cast<double>(total), 1.0)
        : 0.0;
    emit progressChanged();
}

void UpdateModel::startExtracting()
{
    m_progress = 1.0;
    emit progressChanged();
    setPhase(Phase::Extracting);
}

void UpdateModel::showReady()
{
    m_progress = 1.0;
    emit progressChanged();
    setPhase(Phase::Ready);
}

void UpdateModel::fail(const QString &summary, const QString &detail)
{
    m_statusText = summary;
    m_errorDetail = detail;
    m_phase = Phase::Failed;
    // 失败时进度条要收起来（下载到一半才失败时尤其明显）。
    emit progressChanged();
    emit stateChanged();
}

} // namespace flowkeyd::app
