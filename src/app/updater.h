// 在线更新：查 GitHub Release、下载升级包、解压校验、把新的 exe 落在
// `<exe 同目录>/flowkeyd.exe.new`。
//
// **它不替换 exe、也不重启**（那是 `platform/win/update.*` 与 `main` 收尾做的事）：
// 这里只把更新的字节准备到位，然后 `stagedReady()`。这样分工的好处是
// 「下载」可以在程序正常跑着的时候做，而真正会打断运行的那一下（改名 + 重启）
// 只在收尾做一次。
//
// 全程跑在 Qt GUI 线程上：`QNetworkAccessManager` 是异步的，等待期间事件循环
// 照常转，所以不需要工作线程。会阻塞的只有最后「写一个 1.6 MB 的文件」那一下。
#pragma once

#include "app/update_model.h"
#include "core/update_check.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace flowkeyd::app {

class Updater : public QObject
{
    Q_OBJECT

public:
    /// `currentVersion` 是正在运行的这个构建的版本号（`core::buildVersion()`）；
    /// `executablePath` 是当前 exe，下载回来的新 exe 就落在它旁边。
    Updater(QString currentVersion, QString executablePath, QObject *parent = nullptr);

    UpdateModel *model() const { return m_model; }

    /// 有没有已经落盘、只等重启就位的更新。
    bool hasStagedUpdate() const { return !m_stagedPath.isEmpty(); }
    QString stagedExecutable() const { return m_stagedPath; }
    /// 新版本号（重启之后用来告诉用户更新到了哪一版）。
    QString newVersion() const { return m_newVersion; }

public slots:
    /// 托盘菜单「检查更新」：复位状态、把更新窗口要显示的东西准备好，然后去查。
    void checkForUpdates();
    /// 用户点了「立即更新」。
    void startDownload();
    /// 用户关掉了窗口（或在下载中点「取消下载」）：放弃请求，状态留着。
    void dismiss();
    /// 用户点了「重试」：从头再查一遍。
    void retry();
    /// 用户点了「打开发布页」。
    void openReleasePage();

signals:
    /// 需要把更新窗口显示出来（GUI 线程）。
    void windowRequested();
    /// 新的 exe 已经落盘、校验通过 —— 可以停掉自己、替换并重启了。
    void stagedReady();

private:
    void ensureNetwork();
    /// 放弃当前在飞的请求（断掉回调再 abort，所以不会有迟到的 finished 进来）。
    void abortInFlight();
    void startReleaseRequest();
    void onReleaseFinished(QNetworkReply *reply);
    void startArchiveRequest();
    void onArchiveFinished(QNetworkReply *reply);
    /// `summary` 是给用户看的中文人话，`detail` 是英文的技术原因（写日志）。
    void fail(const QString &summary, const QString &detail);

    QString m_currentVersion;
    QString m_executablePath;

    UpdateModel *m_model = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;

    /// 最近一次检查到的发布与选中的升级包。
    core::ReleaseInfo m_release;
    core::ReleaseAsset m_asset;
    /// 下载中的 zip 字节（边到边攒，最后一次性解压）。
    QByteArray m_archiveBytes;
    /// 已经落盘的新 exe（空表示还没有）。
    QString m_stagedPath;
    QString m_newVersion;
};

} // namespace flowkeyd::app
