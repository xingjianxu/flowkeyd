// 构建版本的纯逻辑单测：`core` 里的时间戳推导与字符串组装。
//
// 不碰 Win32、不碰 QML：只对一个临时文件设置「最后写入时间」再读回来。
#include <QtTest>

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

#include "core/version.h"

using namespace flowkeyd;

class TestVersion : public QObject
{
    Q_OBJECT

private slots:
    void unknownTimestampIsStable();
    void formatBuildVersionIncludesTheTimestamp();
    void formatBuildVersionDropsAMissingTimestamp();
    void timestampOfAMissingFileIsUnknown();
    void timestampFollowsTheFileModificationTime();
    void buildVersionCombinesProjectVersionAndTimestamp();
};

void TestVersion::unknownTimestampIsStable()
{
    // 这个占位词会被写进日志、也会被 formatBuildVersion 识别，所以钉住它。
    QCOMPARE(core::unknownTimestamp(), QStringLiteral("unknown"));
}

void TestVersion::formatBuildVersionIncludesTheTimestamp()
{
    QCOMPARE(core::formatBuildVersion(QStringLiteral("0.1.0"),
                                      QStringLiteral("2026-09-22 17:02:55")),
             QStringLiteral("0.1.0 (build 2026-09-22 17:02:55)"));
}

void TestVersion::formatBuildVersionDropsAMissingTimestamp()
{
    // 拿不到构建时间时不能留下半个括号。
    QCOMPARE(core::formatBuildVersion(QStringLiteral("0.1.0"), QString()), QStringLiteral("0.1.0"));
    QCOMPARE(core::formatBuildVersion(QStringLiteral("0.1.0"), core::unknownTimestamp()),
             QStringLiteral("0.1.0"));
}

void TestVersion::timestampOfAMissingFileIsUnknown()
{
    QCOMPARE(core::buildTimestampFromFile(QString()), core::unknownTimestamp());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(core::buildTimestampFromFile(dir.filePath(QStringLiteral("nope.exe"))),
             core::unknownTimestamp());
}

void TestVersion::timestampFollowsTheFileModificationTime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.exe"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("x"), qint64(1));
    file.close();

    // 钉一个本地时间的整秒；`buildTimestampFromFile` 取的就是文件的最后写入时间。
    const QDateTime stamp(QDate(2021, 3, 4), QTime(5, 6, 7));
    QVERIFY(stamp.isValid());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));
    file.close();

    QCOMPARE(core::buildTimestampFromFile(path), QStringLiteral("2021-03-04 05:06:07"));
}

void TestVersion::buildVersionCombinesProjectVersionAndTimestamp()
{
    QVERIFY(!core::projectVersion().isEmpty());

    // 没有可执行文件路径时退化成纯版本号，不留下半个括号。
    QCOMPARE(core::buildVersion(QString()), core::projectVersion());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.exe"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("x") >= 0);
    file.close();

    QCOMPARE(core::buildVersion(path),
             core::formatBuildVersion(core::projectVersion(), core::buildTimestampFromFile(path)));
    QVERIFY(core::buildVersion(path).startsWith(core::projectVersion()));
    QVERIFY(core::buildVersion(path).contains(QStringLiteral("(build ")));
}

QTEST_MAIN(TestVersion)
#include "tst_version.moc"
