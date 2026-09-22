// 构建版本号的纯逻辑单测：`core` 里的时间戳推导与格式。
//
// 不碰 Win32、不碰 QML：只对一个临时文件设置「最后写入时间」再读回来。
#include <QtTest>

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "core/version.h"

using namespace flowkeyd;

class TestVersion : public QObject
{
    Q_OBJECT

private slots:
    void unknownTimestampIsStable();
    void timestampOfAMissingFileIsUnknown();
    void timestampFollowsTheFileModificationTime();
    void buildVersionIsTheBuildTimestamp();
};

void TestVersion::unknownTimestampIsStable()
{
    // 这个占位词会被写进日志与托盘菜单，所以钉住它。
    QCOMPARE(core::unknownTimestamp(), QStringLiteral("unknown"));
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

    // 钉一个本地时间的整秒；版本号只到分钟，所以秒不进结果。
    const QDateTime stamp(QDate(2021, 3, 4), QTime(5, 6, 7));
    QVERIFY(stamp.isValid());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));
    file.close();

    QCOMPARE(core::buildTimestampFromFile(path), QStringLiteral("202103040506"));
}

void TestVersion::buildVersionIsTheBuildTimestamp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.exe"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("x") >= 0);
    file.close();

    // 版本号就是构建的时间戳本身，没有再拼别的前缀。
    QCOMPARE(core::buildVersion(path), core::buildTimestampFromFile(path));
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9]{12}$"))
                .match(core::buildVersion(path))
                .hasMatch());
    // 读不到 exe 时退化成占位词，而不是空串。
    QCOMPARE(core::buildVersion(QString()), core::unknownTimestamp());
}

QTEST_MAIN(TestVersion)
#include "tst_version.moc"
