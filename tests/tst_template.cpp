// `{placeholder}` 模板展开。
#include "core/template.h"

#include <QtTest>

using namespace flowkeyd::core;

namespace {

Vars sampleVars()
{
    Vars vars;
    vars.hotkey = QStringLiteral("Ctrl+Alt+T");
    vars.clipboard = QStringLiteral("clip text");
    vars.selection = QStringLiteral("selected");
    vars.configDir = QStringLiteral("C:\\cfg");
    vars.exeDir = QStringLiteral("C:\\bin");
    vars.userProfile = QStringLiteral("C:\\Users\\me");
    vars.localTime = LocalTime{2026, 9, 18, 13, 46, 7};
    return vars;
}

} // namespace

class TestTemplate : public QObject
{
    Q_OBJECT

private slots:
    void expandsKnownPlaceholders();
    void unknownPlaceholdersSurvive();
    void doubleBracesEscape();
    void missingDynamicValuesExpandEmpty();
    void envLookup();
    void needsClipboardDetectsUsage();
    void civilConversionRoundTrip();
};

void TestTemplate::expandsKnownPlaceholders()
{
    const Vars vars = sampleVars();
    QCOMPARE(expand(QStringLiteral("{clipboard}"), vars), QStringLiteral("clip text"));
    QCOMPARE(expand(QStringLiteral("{selection}"), vars), QStringLiteral("selected"));
    QCOMPARE(expand(QStringLiteral("name={hotkey}"), vars), QStringLiteral("name=Ctrl+Alt+T"));
    QCOMPARE(expand(QStringLiteral("{date} {time}"), vars), QStringLiteral("2026-09-18 13:46:07"));
    QCOMPARE(expand(QStringLiteral("{datetime}"), vars), QStringLiteral("2026-09-18 13:46:07"));
    QCOMPARE(expand(QStringLiteral("{timestamp}"), vars), QStringLiteral("20260918-134607"));
    QCOMPARE(expand(QStringLiteral("{config_dir}\\x"), vars), QStringLiteral("C:\\cfg\\x"));
    QCOMPARE(expand(QStringLiteral("{USERPROFILE}"), vars), QStringLiteral("C:\\Users\\me"));
}

void TestTemplate::unknownPlaceholdersSurvive()
{
    const Vars vars = sampleVars();
    QCOMPARE(expand(QStringLiteral("{notavar}"), vars), QStringLiteral("{notavar}"));
    QCOMPARE(expand(QStringLiteral("a {b} c"), vars), QStringLiteral("a {b} c"));
    QCOMPARE(expand(QStringLiteral("unterminated {clipboard"), vars),
             QStringLiteral("unterminated {clipboard"));
}

void TestTemplate::doubleBracesEscape()
{
    const Vars vars = sampleVars();
    QCOMPARE(expand(QStringLiteral("{{clipboard}}"), vars), QStringLiteral("{clipboard}"));
    QCOMPARE(expand(QStringLiteral("{{}}"), vars), QStringLiteral("{}"));
}

void TestTemplate::missingDynamicValuesExpandEmpty()
{
    Vars vars = sampleVars();
    vars.clipboard.reset();
    QCOMPARE(expand(QStringLiteral("[{clipboard}]"), vars), QStringLiteral("[]"));
}

void TestTemplate::envLookup()
{
    // USERPROFILE 在 Windows 上总是存在。
    const Vars vars = sampleVars();
    const QString expanded = expand(QStringLiteral("{env:USERPROFILE}"), vars);
    QVERIFY(!expanded.contains(QLatin1Char('{')));
    // 大小写与空白不应当影响查找。
    QCOMPARE(expand(QStringLiteral("{ENV:USERPROFILE}"), vars), expanded);
    QCOMPARE(expand(QStringLiteral("{env: USERPROFILE }"), vars), expanded);
}

void TestTemplate::needsClipboardDetectsUsage()
{
    QVERIFY(needsClipboard(QStringLiteral("echo {clipboard}")));
    QVERIFY(needsClipboard(QStringLiteral("echo {SELECTION}")));
    QVERIFY(!needsClipboard(QStringLiteral("echo {hotkey}")));
    QVERIFY(needsSelection(QStringLiteral("echo {Selection}")));
    QVERIFY(!needsSelection(QStringLiteral("echo {clipboard}")));
}

void TestTemplate::civilConversionRoundTrip()
{
    qint64 year = 0;
    std::uint32_t month = 0;
    std::uint32_t day = 0;

    civilFromDays(0, &year, &month, &day);
    QCOMPARE(year, qint64(1970));
    QCOMPARE(month, 1u);
    QCOMPARE(day, 1u);

    civilFromDays(1, &year, &month, &day);
    QCOMPARE(year, qint64(1970));
    QCOMPARE(day, 2u);

    civilFromDays(-1, &year, &month, &day);
    QCOMPARE(year, qint64(1969));
    QCOMPARE(month, 12u);
    QCOMPARE(day, 31u);

    // 11016 是 2000-02-29：世纪闰年规则。
    civilFromDays(11016, &year, &month, &day);
    QCOMPARE(year, qint64(2000));
    QCOMPARE(month, 2u);
    QCOMPARE(day, 29u);

    // 20714 是 2026-09-18。
    civilFromDays(20714, &year, &month, &day);
    QCOMPARE(year, qint64(2026));
    QCOMPARE(month, 9u);
    QCOMPARE(day, 18u);

    const LocalTime time = LocalTime::fromUnix(20714LL * 86400 + 13 * 3600 + 46 * 60 + 7);
    QCOMPARE(time.date(), QStringLiteral("2026-09-18"));
    QCOMPARE(time.time(), QStringLiteral("13:46:07"));
}

QTEST_MAIN(TestTemplate)
#include "tst_template.moc"
