// 单实例互斥体：散列的稳定性与「第二个实例能看见第一个」。
#include <QtTest>

#include "platform/win/single_instance.h"

using namespace flowkeyd;

class TestInstance : public QObject
{
    Q_OBJECT

private slots:
    void fnv1aIsStable();
    void instanceKeyNormalizesThePath();
    void secondAcquireSeesTheFirst();
};

void TestInstance::fnv1aIsStable()
{
    const std::uint64_t empty = platform::win::fnv1a64(QByteArray());
    QCOMPARE(empty, std::uint64_t(0xcbf29ce484222325ULL));
    QCOMPARE(platform::win::fnv1a64(QByteArray("flowkeyd")),
             platform::win::fnv1a64(QByteArray("flowkeyd")));
    QVERIFY(platform::win::fnv1a64(QByteArray("flowkeyd"))
            != platform::win::fnv1a64(QByteArray("flowkeyD")));
}

void TestInstance::instanceKeyNormalizesThePath()
{
    const QString a = platform::win::instanceKey(QStringLiteral("C:\\Tools\\Config.lua"));
    const QString b = platform::win::instanceKey(QStringLiteral("c:/tools/config.lua"));
    QCOMPARE(a, b);
    QVERIFY(a.startsWith(QStringLiteral("Local\\flowkeyd-")));
}

void TestInstance::secondAcquireSeesTheFirst()
{
    // 用一个只属于本测试的 key，避免撞上真实的守护进程。
    const QString key = platform::win::instanceKey(QStringLiteral("\\\\.\\pipe\\flowkeyd-test"));
    bool firstAlready = true;
    QString error;
    auto first = platform::win::SingleInstance::acquire(key, &firstAlready, &error);
    QVERIFY2(first.has_value(), qPrintable(error));
    // 同一个进程里再拿一次同名互斥体：`CreateMutexW` 会成功，但会告诉我们已存在。
    bool secondAlready = false;
    auto second = platform::win::SingleInstance::acquire(key, &secondAlready, &error);
    QVERIFY2(second.has_value(), qPrintable(error));
    QVERIFY(secondAlready);
    // 释放之后不应该再看见它。
    second.reset();
    first.reset();
    bool thirdAlready = true;
    auto third = platform::win::SingleInstance::acquire(key, &thirdAlready, &error);
    QVERIFY2(third.has_value(), qPrintable(error));
    QVERIFY(!thirdAlready);
}

QTEST_MAIN(TestInstance)
#include "tst_instance.moc"
