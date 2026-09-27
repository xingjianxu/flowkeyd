// 「在线更新」窗口的**纯逻辑**模型（只依赖 QtCore，可直接单测）。
//
// 与 `help_model` / `menu_model` 一样，这里只管状态与文案，不碰网络、不碰文件、
// 不碰窗口：`app::Updater` 负责真的去查 GitHub、下载、解压，然后把这些事实
// 一条条写进来（`startChecking` → `showAvailable` → `startDownloading` →
// `setProgress` → `startExtracting` → `showReady` / `fail`）。QML 只读属性，
// 并且在按钮上调用 `PopupHost` 的 `Q_INVOKABLE`（那些再把活儿交给 `Updater`）。
//
// 状态机：
//
//     idle ──startChecking──► checking ──┬─ showUpToDate ─► uptodate
//                                        ├─ showAvailable ─► available
//                                        └─ fail ──────────► failed
//     available ──startDownloading──► downloading ──startExtracting──► extracting
//                                                    ── showReady ──► ready
//     任意下载 / 解压阶段 ── fail ──► failed；`reset()` 从任何状态回到 idle。
//
// 文案：界面上给用户看的都是中文（与弹窗的其它文案一致），而 `errorDetail`
// 里保留英文的技术原因（网络错误、解压错误），方便搜日志、贴给作者
// ——「日志与错误信息保持英文」那条约定管的是日志，界面上给用户看的一句
// 人话仍然是中文（见 AGENTS.md 工作约定第 4 条）。
#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

namespace flowkeyd::app {

class UpdateModel : public QObject
{
    Q_OBJECT

    /// 当前阶段名（QML 用来决定画什么）：`idle` / `checking` / `uptodate` /
    /// `available` / `downloading` / `extracting` / `ready` / `failed`。
    Q_PROPERTY(QString phase READ phaseName NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString caption READ caption NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion NOTIFY stateChanged)
    Q_PROPERTY(QString newVersion READ newVersion NOTIFY stateChanged)
    Q_PROPERTY(bool hasNewVersion READ hasNewVersion NOTIFY stateChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY stateChanged)
    Q_PROPERTY(bool hasNotes READ hasNotes NOTIFY stateChanged)
    Q_PROPERTY(QString notesPlaceholder READ notesPlaceholder CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY stateChanged)
    Q_PROPERTY(bool hasErrorDetail READ hasErrorDetail NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    /// 下载进度有没有总长度（没有时进度条画成不确定态）。
    Q_PROPERTY(bool hasProgressTotal READ hasProgressTotal NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged)
    Q_PROPERTY(bool canDismiss READ canDismiss NOTIFY stateChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY stateChanged)
    Q_PROPERTY(bool canOpenRelease READ canOpenRelease NOTIFY stateChanged)
    /// 唯一的关闭按钮的文案：下载中叫「取消下载」，其余时候叫「关闭」。
    Q_PROPERTY(QString closeLabel READ closeLabel NOTIFY stateChanged)
    Q_PROPERTY(int cardWidth READ cardWidth CONSTANT)
    Q_PROPERTY(int cardHeight READ cardHeight CONSTANT)
    Q_PROPERTY(int cardRadius READ cardRadius CONSTANT)

public:
    enum class Phase {
        Idle,
        Checking,
        UpToDate,
        Available,
        Downloading,
        Extracting,
        Ready,
        Failed,
    };
    Q_ENUM(Phase)

    explicit UpdateModel(QObject *parent = nullptr);

    Phase phase() const { return m_phase; }
    QString phaseName() const;

    QString title() const { return tr("在线更新"); }
    QString caption() const;
    QString currentVersion() const { return m_currentVersion; }
    QString newVersion() const { return m_newVersion; }
    bool hasNewVersion() const { return !m_newVersion.isEmpty(); }
    QString notes() const { return m_notes; }
    bool hasNotes() const { return !m_notes.trimmed().isEmpty(); }
    QString notesPlaceholder() const;
    QString statusText() const { return m_statusText; }
    QString errorDetail() const { return m_errorDetail; }
    bool hasErrorDetail() const { return !m_errorDetail.isEmpty(); }
    double progress() const { return m_progress; }
    QString progressText() const;
    bool hasProgressTotal() const { return m_total > 0; }
    bool busy() const;
    bool canInstall() const { return m_phase == Phase::Available; }
    /// 关掉窗口。只有「正在重启」那一下不允许关（关了就看不到重启了，
    /// 而且那一刻请求已经结束、没什么可取消的）。
    bool canDismiss() const { return m_phase != Phase::Ready; }
    bool canRetry() const { return m_phase == Phase::Failed; }
    bool canOpenRelease() const;
    QString closeLabel() const;
    QString releaseUrl() const { return m_releaseUrl; }

    // ---- 卡片几何（逻辑像素；`PopupHost` 用它摆位置） ----
    int cardWidth() const { return 520; }
    int cardHeight() const { return 360; }
    int cardRadius() const { return 12; }

    // ---- 状态迁移（由 `app::Updater` 推动；每一步都发一次信号） ----

    /// 回到「还没开始」的样子（再次点「检查更新」时先调它）。
    void reset();
    /// 开始检查更新；`currentVersion` 是正在运行的这个构建的版本号。
    void startChecking(const QString &currentVersion);
    /// 检查结果：已经是最新的。
    void showUpToDate();
    /// 检查结果：有新版本。`version` 是新版本号，`notes` 是发布说明，
    /// `releaseUrl` 是发布页地址（打不开下载时给用户一个手动入口）。
    void showAvailable(const QString &version, const QString &notes, const QString &releaseUrl);
    /// 用户确认了，开始下载。
    void startDownloading();
    /// 下载进度（`total <= 0` 表示服务器没给 `Content-Length`）。
    void setProgress(qint64 received, qint64 total);
    /// 下载完了，正在解压 / 校验。
    void startExtracting();
    /// 更新已经落盘并通过校验，马上就要重启。
    void showReady();
    /// 出错了：`summary` 是一句中文人话（显示在状态行），`detail` 是英文的技术
    /// 原因（小字显示，也写日志）。`detail` 可以为空。
    void fail(const QString &summary, const QString &detail);

signals:
    void stateChanged();
    void progressChanged();

private:
    /// 把「正在运行的构建版本」记下来（`reset()` 之后由 `startChecking` 重新给）。
    void setCurrentVersion(const QString &version);
    /// 换阶段 + 发信号（顺带把「下载完之后残留的进度」清掉）。
    void setPhase(Phase phase);
    void clearProgress();
    /// 由阶段推导出来的状态行文案。
    QString statusForPhase() const;

    Phase m_phase = Phase::Idle;
    QString m_currentVersion;
    QString m_newVersion;
    QString m_notes;
    QString m_releaseUrl;
    QString m_statusText;
    QString m_errorDetail;
    qint64 m_received = 0;
    qint64 m_total = 0;
    double m_progress = 0.0;
};

} // namespace flowkeyd::app
