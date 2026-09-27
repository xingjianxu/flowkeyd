// `platform/win/update.*` 的单测：把新的 exe 换上、重启、失败回滚、清理备份。
//
// 这里**不启动 flowkeyd**：`applyExecutableUpdate()` 会把 `restartArgs` 交给目标
// 路径去执行，所以测试拿系统自带的 `cmd.exe` / `ping.exe` 当「旧版本 / 新版本」——
// 它们不需要桌面、不会装键盘钩子，`ping -n 4` 也刚好能活过 2.5 秒的启动观察期。
// 这样做的好处是：真正会被测到的是**改名 + 启动 + 回滚**这套逻辑本身，
// 而不是某个被测程序的配合。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "platform/win/update.h"

using namespace flowkeyd;

namespace {

QString systemExecutable(const QString &name)
{
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    return QDir::toNativeSeparators(root + QStringLiteral("/System32/") + name);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

/// 一个「新版本 exe」：真实的 PE，而且会自己活过启动观察期。
QString pingPath()
{
    return systemExecutable(QStringLiteral("ping.exe"));
}

} // namespace

class TestUpdateInstall : public QObject
{
    Q_OBJECT

private slots:
    void appliesTheStagedExecutableAndRestarts();
    void rollsBackWhenTheNewExecutableCannotStart();
    void rollsBackWhenTheNewExecutableExitsImmediately();
    void removingAMissingBackupSucceeds();
};

void TestUpdateInstall::appliesTheStagedExecutableAndRestarts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("flowkeyd.exe"));
    const QString staged = dir.filePath(QStringLiteral("flowkeyd.exe.new"));

    // 「旧版本」= cmd.exe，「新版本」= ping.exe：两者字节不同，所以替换之后
    // 能一眼看出换上的是哪一个。
    QVERIFY(QFile::copy(systemExecutable(QStringLiteral("cmd.exe")), target));
    QVERIFY(QFile::copy(pingPath(), staged));
    const QByteArray oldBytes = readFile(target);
    const QByteArray newBytes = readFile(staged);
    QVERIFY(!oldBytes.isEmpty());
    QVERIFY(!newBytes.isEmpty());
    QVERIFY(oldBytes != newBytes);

    platform::win::UpdateInstallPlan plan;
    plan.targetExecutable = target;
    plan.stagedExecutable = staged;
    // `ping -n 4 127.0.0.1` 要 3 秒，长过 2.5 秒的启动观察期。
    plan.restartArgs = QStringList{QStringLiteral("-n"), QStringLiteral("4"),
                                   QStringLiteral("127.0.0.1")};
    // 版本号留空：`--updated-from` 是给 flowkeyd 自己看的，这里不需要。
    plan.currentVersion.clear();

    QString error;
    QVERIFY2(platform::win::applyExecutableUpdate(plan, &error), qPrintable(error));

    // 目标路径现在是「新版本」的字节；旧的那份在 `<exe>.old` 里。
    QCOMPARE(readFile(target), newBytes);
    const QString backup = target + QStringLiteral(".old");
    QVERIFY(QFileInfo::exists(backup));
    QCOMPARE(readFile(backup), oldBytes);
    // 已经落位的 staged 路径不再存在。
    QVERIFY(!QFileInfo::exists(staged));

    // 清理：新实例启动时会调它。
    QVERIFY(platform::win::removeExecutableBackup(target));
    QVERIFY(!QFileInfo::exists(backup));
    // 幂等。
    QVERIFY(platform::win::removeExecutableBackup(target));
}

void TestUpdateInstall::rollsBackWhenTheNewExecutableCannotStart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("flowkeyd.exe"));
    const QString staged = dir.filePath(QStringLiteral("flowkeyd.exe.new"));
    QVERIFY(QFile::copy(systemExecutable(QStringLiteral("cmd.exe")), target));
    const QByteArray oldBytes = readFile(target);
    // 一个「不像 exe」的新版本：`CreateProcess` 会失败。
    const QByteArray broken("this is not a portable executable");
    QVERIFY(writeFile(staged, broken));

    platform::win::UpdateInstallPlan plan;
    plan.targetExecutable = target;
    plan.stagedExecutable = staged;
    plan.restartArgs.clear();

    QString error;
    QVERIFY(!platform::win::applyExecutableUpdate(plan, &error));
    QVERIFY(!error.isEmpty());
    // 回到更新前的样子：目标还是旧的、没有留下 `.old`、坏文件还回 staged。
    QCOMPARE(readFile(target), oldBytes);
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".old")));
    QCOMPARE(readFile(staged), broken);
}

void TestUpdateInstall::rollsBackWhenTheNewExecutableExitsImmediately()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("flowkeyd.exe"));
    const QString staged = dir.filePath(QStringLiteral("flowkeyd.exe.new"));
    QVERIFY(QFile::copy(systemExecutable(QStringLiteral("cmd.exe")), target));
    const QByteArray oldBytes = readFile(target);
    QVERIFY(QFile::copy(pingPath(), staged));

    platform::win::UpdateInstallPlan plan;
    plan.targetExecutable = target;
    plan.stagedExecutable = staged;
    // 立刻退出的进程 = “新版本起不来”（缺 DLL 时就是这个样子）。
    plan.restartArgs = QStringList{QStringLiteral("-n"), QStringLiteral("1"),
                                   QStringLiteral("127.0.0.1")};

    QString error;
    QVERIFY(!platform::win::applyExecutableUpdate(plan, &error));
    QVERIFY(error.contains(QStringLiteral("immediately")));
    QCOMPARE(readFile(target), oldBytes);
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".old")));
}

void TestUpdateInstall::removingAMissingBackupSucceeds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(platform::win::removeExecutableBackup(dir.filePath(QStringLiteral("flowkeyd.exe"))));
}

QTEST_MAIN(TestUpdateInstall)
#include "tst_update_install.moc"
