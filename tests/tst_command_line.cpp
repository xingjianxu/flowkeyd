// 命令行转发与启动参数的纯逻辑单测（`quoteArg` / `quoteForCmd` / 环境块）。
#include <QtTest>

#include <algorithm>

#include "platform/win/elevate.h"
#include "platform/win/process.h"

using namespace flowkeyd;

class TestCommandLine : public QObject
{
    Q_OBJECT

private slots:
    void quoteArgCases();
    void buildParametersJoinsQuoted();
    void quoteForCmdCases();
    void showCodeCases();
    void environmentBlockIsSortedAndNullTerminated();
};

void TestCommandLine::quoteArgCases()
{
    QCOMPARE(platform::win::quoteArg(QStringLiteral("simple")), QStringLiteral("simple"));
    QCOMPARE(platform::win::quoteArg(QStringLiteral("two words")), QStringLiteral("\"two words\""));
    QCOMPARE(platform::win::quoteArg(QString()), QStringLiteral("\"\""));
    // 内部引号要转义（反斜杠加倍再加一个）。
    QCOMPARE(platform::win::quoteArg(QStringLiteral("has\"quote")),
             QStringLiteral("\"has\\\"quote\""));
    // 结尾的反斜杠要加倍，否则会把收尾的引号吞掉。
    QCOMPARE(platform::win::quoteArg(QStringLiteral("C:\\a b\\")),
             QStringLiteral("\"C:\\a b\\\\\""));
    QCOMPARE(platform::win::quoteArg(QStringLiteral("C:\\plain\\path")),
             QStringLiteral("C:\\plain\\path"));
}

void TestCommandLine::buildParametersJoinsQuoted()
{
    QCOMPARE(platform::win::buildParameters({QStringLiteral("--config"),
                                             QStringLiteral("C:\\my dir\\config.lua")}),
             QStringLiteral("--config \"C:\\my dir\\config.lua\""));
}

void TestCommandLine::quoteForCmdCases()
{
    QCOMPARE(platform::win::quoteForCmd(QStringLiteral("plain")), QStringLiteral("plain"));
    QCOMPARE(platform::win::quoteForCmd(QStringLiteral("a b")), QStringLiteral("\"a b\""));
    QCOMPARE(platform::win::quoteForCmd(QStringLiteral("\"already\"")), QStringLiteral("\"already\""));
}

void TestCommandLine::showCodeCases()
{
    QCOMPARE(platform::win::showCode(core::ShowMode::Hidden), int(SW_HIDE));
    QCOMPARE(platform::win::showCode(core::ShowMode::Minimized), int(SW_SHOWMINIMIZED));
    QCOMPARE(platform::win::showCode(core::ShowMode::Maximized), int(SW_SHOWMAXIMIZED));
    QCOMPARE(platform::win::showCode(core::ShowMode::Normal), int(SW_SHOWNORMAL));
}

void TestCommandLine::environmentBlockIsSortedAndNullTerminated()
{
    QMap<QString, QString> overrides;
    overrides.insert(QStringLiteral("FLOWKEYD_TEST_OVERRIDE"), QStringLiteral("1"));
    // 这对名字在「折成小写」与「转成大写」两种不区分大小写的排序下会给出
    // 相反的答案（第二个字符 `_`(0x5F) vs `A`），所以不论跑测试的机器上有什么
    // 环境变量，这条断言都能钉住“按大写折叠再比码元”这个顺序。
    overrides.insert(QStringLiteral("AAB"), QStringLiteral("1"));
    overrides.insert(QStringLiteral("A_Z"), QStringLiteral("1"));
    const std::vector<wchar_t> block = platform::win::buildEnvironmentBlock(overrides);
    QVERIFY(block.size() >= 2);
    QVERIFY(block[block.size() - 1] == L'\0');
    QVERIFY(block[block.size() - 2] == L'\0');

    QStringList entries;
    const wchar_t *cursor = block.data();
    while (*cursor != L'\0') {
        entries.append(QString::fromWCharArray(cursor));
        cursor += wcslen(cursor) + 1;
    }
    QVERIFY(entries.contains(QStringLiteral("FLOWKEYD_TEST_OVERRIDE=1")));
    QVERIFY(entries.contains(QStringLiteral("AAB=1")));
    QVERIFY(entries.contains(QStringLiteral("A_Z=1")));

    // 环境块必须按名称不区分大小写排序。
    QStringList names;
    for (const QString &entry : entries) {
        names.append(entry.section(QLatin1Char('='), 0, 0).toUpper());
    }
    QStringList sorted = names;
    std::sort(sorted.begin(), sorted.end());
    QCOMPARE(names, sorted);
}

QTEST_MAIN(TestCommandLine)
#include "tst_command_line.moc"
