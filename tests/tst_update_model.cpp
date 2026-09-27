// 「在线更新」窗口模型的单测：阶段迁移、状态行、版本号、进度、按钮可见性。
//
// 这一层是纯逻辑（只依赖 QtCore）：网络、解压、替换 exe、重启都不在这里，
// 所以测试只要按顺序调那些「状态迁移」方法，看属性对不对就行。
#include <QtTest>

#include "app/update_model.h"

using namespace flowkeyd;

class TestUpdateModel : public QObject
{
    Q_OBJECT

private slots:
    void startsIdle();
    void checkingIsBusyButStillClosable();
    void upToDateKeepsTheCurrentVersion();
    void availableOffersTheNewVersion();
    void downloadReportsProgress();
    void downloadWithoutTotalIsIndeterminate();
    void extractingThenReady();
    void failureExposesTheDetailAndRetry();
    void closeLabelFollowsThePhase();
    void resetReturnsToIdle();
};

void TestUpdateModel::startsIdle()
{
    app::UpdateModel model;
    QCOMPARE(model.phaseName(), QStringLiteral("idle"));
    QCOMPARE(model.canInstall(), false);
    QCOMPARE(model.canRetry(), false);
    QCOMPARE(model.canDismiss(), true);
    QCOMPARE(model.hasNewVersion(), false);
    QVERIFY(!model.statusText().isEmpty());
    QVERIFY(model.progressText().isEmpty());
}

void TestUpdateModel::checkingIsBusyButStillClosable()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    QCOMPARE(model.phaseName(), QStringLiteral("checking"));
    QCOMPARE(model.currentVersion(), QStringLiteral("26-09-27-95f40ac"));
    QCOMPARE(model.busy(), true);
    // 检查更新也要能关掉窗口（网络卡住时用户不该被关在卡片里）。
    QCOMPARE(model.canDismiss(), true);
    QCOMPARE(model.canInstall(), false);
}

void TestUpdateModel::upToDateKeepsTheCurrentVersion()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showUpToDate();
    QCOMPARE(model.phaseName(), QStringLiteral("uptodate"));
    QCOMPARE(model.busy(), false);
    QCOMPARE(model.hasNewVersion(), false);
    QVERIFY(model.statusText().contains(QStringLiteral("26-09-27-95f40ac")));
}

void TestUpdateModel::availableOffersTheNewVersion()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"),
                        QStringLiteral("### 更新内容\n\n- 修了一个 bug"),
                        QStringLiteral("https://github.com/xingjianxu/flowkeyd/releases/tag/v1"));
    QCOMPARE(model.phaseName(), QStringLiteral("available"));
    QCOMPARE(model.newVersion(), QStringLiteral("26-09-28-abcdef0"));
    QCOMPARE(model.hasNewVersion(), true);
    QCOMPARE(model.hasNotes(), true);
    QVERIFY(model.notes().contains(QStringLiteral("修了一个 bug")));
    QCOMPARE(model.canInstall(), true);
    QCOMPARE(model.canDismiss(), true);
    QCOMPARE(model.canOpenRelease(), true);
    QVERIFY(model.caption().contains(QStringLiteral("26-09-28-abcdef0")));

    // 没有说明时给一句占位文案，而不是留一块空白。
    app::UpdateModel bare;
    bare.startChecking(QStringLiteral("26-09-27-95f40ac"));
    bare.showAvailable(QStringLiteral("26-09-28-abcdef0"), QString(), QString());
    QCOMPARE(bare.hasNotes(), false);
    QVERIFY(!bare.notesPlaceholder().isEmpty());
    QCOMPARE(bare.canOpenRelease(), false);
}

void TestUpdateModel::downloadReportsProgress()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"), QString(), QString());
    model.startDownloading();
    QCOMPARE(model.phaseName(), QStringLiteral("downloading"));
    QCOMPARE(model.canInstall(), false);
    QCOMPARE(model.busy(), true);

    model.setProgress(50, 200);
    QCOMPARE(model.hasProgressTotal(), true);
    QCOMPARE(model.progress(), 0.25);
    QVERIFY(model.progressText().contains(QStringLiteral("25%")));

    // 超出总长的进度要被夹到 1（HTTP 层的数字不总是可信）。
    model.setProgress(400, 200);
    QCOMPARE(model.progress(), 1.0);
}

void TestUpdateModel::downloadWithoutTotalIsIndeterminate()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"), QString(), QString());
    model.startDownloading();
    model.setProgress(4096, 0);
    QCOMPARE(model.hasProgressTotal(), false);
    QCOMPARE(model.progress(), 0.0);
    QCOMPARE(model.progressText(), QStringLiteral("4.0 KB"));
}

void TestUpdateModel::extractingThenReady()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"), QString(), QString());
    model.startDownloading();
    model.setProgress(100, 100);
    model.startExtracting();
    QCOMPARE(model.phaseName(), QStringLiteral("extracting"));
    QCOMPARE(model.busy(), true);

    model.showReady();
    QCOMPARE(model.phaseName(), QStringLiteral("ready"));
    QCOMPARE(model.busy(), false);
    QCOMPARE(model.progress(), 1.0);
    // 「正在重启」这一下不允许关窗：关了用户就看不到重启了。
    QCOMPARE(model.canDismiss(), false);
}

void TestUpdateModel::failureExposesTheDetailAndRetry()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"),
                        QString(),
                        QStringLiteral("https://example.invalid/release"));
    model.startDownloading();
    model.setProgress(10, 100);
    model.fail(QStringLiteral("下载升级包失败"), QStringLiteral("Connection timed out"));

    QCOMPARE(model.phaseName(), QStringLiteral("failed"));
    QCOMPARE(model.statusText(), QStringLiteral("下载升级包失败"));
    QCOMPARE(model.errorDetail(), QStringLiteral("Connection timed out"));
    QCOMPARE(model.hasErrorDetail(), true);
    QCOMPARE(model.canRetry(), true);
    QCOMPARE(model.canDismiss(), true);
    QCOMPARE(model.canOpenRelease(), true);
    QCOMPARE(model.canInstall(), false);
}

void TestUpdateModel::closeLabelFollowsThePhase()
{
    app::UpdateModel model;
    QCOMPARE(model.closeLabel(), QStringLiteral("关闭"));
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"), QString(), QString());
    model.startDownloading();
    QCOMPARE(model.closeLabel(), QStringLiteral("取消下载"));
}

void TestUpdateModel::resetReturnsToIdle()
{
    app::UpdateModel model;
    model.startChecking(QStringLiteral("26-09-27-95f40ac"));
    model.showAvailable(QStringLiteral("26-09-28-abcdef0"), QStringLiteral("notes"), QString());
    model.reset();
    QCOMPARE(model.phaseName(), QStringLiteral("idle"));
    QCOMPARE(model.hasNewVersion(), false);
    QCOMPARE(model.hasNotes(), false);
    QCOMPARE(model.canInstall(), false);
}

QTEST_MAIN(TestUpdateModel)
#include "tst_update_model.moc"
