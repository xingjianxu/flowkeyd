// 构建版本号的纯逻辑单测：`core` 里的日期推导、git 修订与拼装。
//
// 不碰 Win32、不碰 QML：日期那部分只对一个临时文件设置「最后写入时间」再读回来；
// git 修订是编译进来的常量（由 CMake 生成），只断言它的形状。
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
    void unknownValueIsStable();
    void dateOfAMissingFileIsUnknown();
    void dateFollowsTheFileModificationTime();
    void sourceRevisionLooksLikeAShortHash();
    void buildVersionIsDateDashRevision();
};

void TestVersion::unknownValueIsStable()
{
    // 这个占位词会被写进日志与托盘菜单，所以钉住它。
    QCOMPARE(core::unknownValue(), QStringLiteral("unknown"));
}

void TestVersion::dateOfAMissingFileIsUnknown()
{
    QCOMPARE(core::buildDateFromFile(QString()), core::unknownValue());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(core::buildDateFromFile(dir.filePath(QStringLiteral("nope.exe"))),
             core::unknownValue());
}

void TestVersion::dateFollowsTheFileModificationTime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.exe"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("x"), qint64(1));
    file.close();

    // 钉一个本地时间；版本号里只保留 `yy-MM-dd`。
    const QDateTime stamp(QDate(2021, 3, 4), QTime(5, 6, 7));
    QVERIFY(stamp.isValid());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));
    file.close();

    QCOMPARE(core::buildDateFromFile(path), QStringLiteral("21-03-04"));
}

void TestVersion::sourceRevisionLooksLikeAShortHash()
{
    const QString revision = core::sourceRevision();
    QVERIFY(!revision.isEmpty());
    QVERIFY(!revision.contains(QLatin1Char(' ')));
    // 正常构建（在 git 仓库里跑 CMake）应当是 `git rev-parse --short HEAD`
    // 那种小写十六进制；拿不到 git 时才允许是占位词。
    if (revision != core::unknownValue()) {
        QVERIFY2(QRegularExpression(QStringLiteral("^[0-9a-f]{4,}$")).match(revision).hasMatch(),
                 qPrintable(QStringLiteral("unexpected revision: %1").arg(revision)));
    }
}

void TestVersion::buildVersionIsDateDashRevision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.exe"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("x") >= 0);
    file.close();

    // 版本号 = `<yy-MM-dd>-<git 短修订>`，没有再拼别的前缀。
    QCOMPARE(core::buildVersion(path),
             core::buildDateFromFile(path) + QLatin1Char('-') + core::sourceRevision());
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9]{2}-[0-9]{2}-[0-9]{2}-"))
                .match(core::buildVersion(path))
                .hasMatch());
    // 读不到 exe 时日期段退化成占位词，而不是空串。
    QCOMPARE(core::buildVersion(QString()),
             core::unknownValue() + QLatin1Char('-') + core::sourceRevision());
}

QTEST_MAIN(TestVersion)
#include "tst_version.moc"
