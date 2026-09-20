// 日志尾随的纯逻辑单测：`completeUtf8PrefixLen` + `core::LogTailer`。
//
// 不碰控制台、不碰 QML，也不碰任何 Win32：读写的都是临时文件。
#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/log_tail.h"

using namespace flowkeyd;

namespace {

void appendBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
}

} // namespace

class TestLogTail : public QObject
{
    Q_OBJECT

private slots:
    void completePrefixAcceptsAsciiAndCompleteMultibyte();
    void completePrefixTruncatesPartialSequences();
    void completePrefixLeavesInvalidStartBytesAlone();
    void pollReturnsOnlyCompleteLines();
    void pollWaitsForATruncatedMultibyteCharacter();
    void pollDropsOldLinesBeyondTheBacklog();
    void pollRestartsAfterAFileRotation();
    void pollOfAMissingFileIsNotAnError();
};

void TestLogTail::completePrefixAcceptsAsciiAndCompleteMultibyte()
{
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray("hello\n")), 6);
    QCOMPARE(core::completeUtf8PrefixLen(QStringLiteral("中文").toUtf8()), 6);
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray()), 0);
    // 尾部有一个完整的多字节字符（3 字节）时不能少算。
    QCOMPARE(core::completeUtf8PrefixLen(QStringLiteral("a你").toUtf8()), 4);
}

void TestLogTail::completePrefixTruncatesPartialSequences()
{
    // “你” = E4 BD A0。只写前 1、2 个字节时都不该消费。
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("e4")), 0);
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("e4bd")), 0);
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("e4bda0")), 3);
    // ASCII 之后跟一个被截断的 3 字节序列：只消费 ASCII。
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("41e4bd")), 1);
    // 4 字节序列（emoji）同理。
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("f09f")), 0);
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("f09f9880")), 4);
}

void TestLogTail::completePrefixLeavesInvalidStartBytesAlone()
{
    // 0xF8..0xFF 不是合法的 UTF-8 首字节：不插手，交给 fromUtf8 的替换字符。
    QCOMPARE(core::completeUtf8PrefixLen(QByteArray::fromHex("41f8")), 2);
}

void TestLogTail::pollReturnsOnlyCompleteLines()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.log"));

    core::LogTailer tailer(path);
    // 文件还不存在：不算错误。
    QCOMPARE(tailer.poll(), QStringList());

    appendBytes(path, QByteArray("one\ntwo\n"));
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("one"), QStringLiteral("two")}));
    // 没有新内容时是空的。
    QCOMPARE(tailer.poll(), QStringList());

    // 半行不输出，等换行。
    appendBytes(path, QByteArray("thr"));
    QCOMPARE(tailer.poll(), QStringList());
    appendBytes(path, QByteArray("ee\n"));
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("three")}));

    // CRLF 也要把 \r 去掉。
    appendBytes(path, QByteArray("four\r\n"));
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("four")}));
}

void TestLogTail::pollWaitsForATruncatedMultibyteCharacter()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.log"));

    core::LogTailer tailer(path);
    appendBytes(path, QByteArray::fromHex("e4bd")); // “你”的前两个字节
    QCOMPARE(tailer.poll(), QStringList());

    // 补完第三个字节与换行：整行一次出现。
    appendBytes(path, QByteArray::fromHex("a0") + QByteArray("\n"));
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("你")}));
}

void TestLogTail::pollDropsOldLinesBeyondTheBacklog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.log"));

    QByteArray payload;
    const int total = core::LogTailer::kMaxBacklogLines + 200;
    for (int i = 0; i < total; ++i) {
        payload += QByteArray("line ") + QByteArray::number(i) + QByteArray("\n");
    }
    appendBytes(path, payload);

    core::LogTailer tailer(path);
    const QStringList lines = tailer.poll();
    QCOMPARE(lines.size(), core::LogTailer::kMaxBacklogLines);
    QVERIFY(tailer.truncated());
    // 丢掉的是最前面的 200 行，因此第一行是 line 200。
    QCOMPARE(lines.first(), QStringLiteral("line 200"));
    QCOMPARE(lines.last(), QStringLiteral("line %1").arg(total - 1));
}

void TestLogTail::pollRestartsAfterAFileRotation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("flowkeyd.log"));

    core::LogTailer tailer(path);
    appendBytes(path, QByteArray("first\n"));
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("first")}));

    // 轮转：新文件比已经读过的偏移短，于是从头再读。
    // （只按文件大小判断轮转，与 oskeyd 的 `Tailer` 一致：flowkeyd 自己的日志
    // 只会被追加，永远不会在“偏移之后”换成另一份更长的内容。）
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QByteArray("new\n")), qint64(4));
    file.close();
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("new")}));

    // 轮转之后继续追加也照常工作。
    appendBytes(path, QByteArray("more\n"));
    QCOMPARE(tailer.poll(), QStringList({QStringLiteral("more")}));
}

void TestLogTail::pollOfAMissingFileIsNotAnError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    core::LogTailer tailer(dir.filePath(QStringLiteral("nope.log")));
    QString error;
    QCOMPARE(tailer.poll(&error), QStringList());
    QVERIFY(error.isEmpty());
    QVERIFY(!tailer.truncated());
}

QTEST_MAIN(TestLogTail)
#include "tst_log_tail.moc"
