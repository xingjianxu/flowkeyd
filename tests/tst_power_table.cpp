// 电源动作的纯逻辑单测。
//
// 刻意**不**调用 `power::execute()`：睡眠 / 关机 / 重启会打断用户手上的事情
// （见 AGENTS.md 第 9 阶段）。这里只覆盖两张表与解析：规范名、
// `powerOpFromName` 的别名、`power:` 简写，以及
// “哪些 op 必须启用 SeShutdownPrivilege”——`screen_off` / `lock` / `sleep` /
// `hibernate` 都**不**走那条权限路径。
#include <QtTest>

#include "core/action.h"
#include "platform/win/power.h"

using namespace flowkeyd;

class TestPowerTable : public QObject
{
    Q_OBJECT

private slots:
    void tableCoversEveryOpWithItsCanonicalName();
    void privilegeFlagsMatchOskeyd();
    void namesRoundTripThroughPowerOpFromName();
    void aliasesMapToTheSameOp();
    void unknownNamesAreRejected();
    void shorthandParsesEveryOp();
    void shorthandRejectsUnknownOps();
};

void TestPowerTable::tableCoversEveryOpWithItsCanonicalName()
{
    const std::vector<platform::win::power::PowerOpInfo> &table =
        platform::win::power::powerOpTable();
    QCOMPARE(table.size(), std::size_t(7));
    for (const platform::win::power::PowerOpInfo &info : table) {
        // 表里的规范名必须与 `--list` / 日志用的那一个一致。
        QCOMPARE(QString::fromLatin1(info.name), core::powerOpName(info.op));
        QCOMPARE(platform::win::power::requiresShutdownPrivilege(info.op),
                 info.needsShutdownPrivilege);
    }
}

/// 只有关机 / 重启 / 注销需要管理员权限；其余四个都不需要。
void TestPowerTable::privilegeFlagsMatchOskeyd()
{
    using platform::win::power::requiresShutdownPrivilege;
    QVERIFY(!requiresShutdownPrivilege(core::PowerOp::Sleep));
    QVERIFY(!requiresShutdownPrivilege(core::PowerOp::Hibernate));
    QVERIFY(!requiresShutdownPrivilege(core::PowerOp::Lock));
    QVERIFY(!requiresShutdownPrivilege(core::PowerOp::ScreenOff));
    QVERIFY(requiresShutdownPrivilege(core::PowerOp::Shutdown));
    QVERIFY(requiresShutdownPrivilege(core::PowerOp::Restart));
    QVERIFY(requiresShutdownPrivilege(core::PowerOp::Logoff));
}

void TestPowerTable::namesRoundTripThroughPowerOpFromName()
{
    for (const platform::win::power::PowerOpInfo &info : platform::win::power::powerOpTable()) {
        const std::optional<core::PowerOp> parsed =
            core::powerOpFromName(QString::fromLatin1(info.name));
        QVERIFY2(parsed.has_value(), info.name);
        QVERIFY(*parsed == info.op);
    }
}

void TestPowerTable::aliasesMapToTheSameOp()
{
    QCOMPARE(core::powerOpFromName(QStringLiteral("suspend")), std::optional<core::PowerOp>(core::PowerOp::Sleep));
    QCOMPARE(core::powerOpFromName(QStringLiteral("poweroff")), std::optional<core::PowerOp>(core::PowerOp::Shutdown));
    QCOMPARE(core::powerOpFromName(QStringLiteral("reboot")), std::optional<core::PowerOp>(core::PowerOp::Restart));
    QCOMPARE(core::powerOpFromName(QStringLiteral("logout")), std::optional<core::PowerOp>(core::PowerOp::Logoff));
    QCOMPARE(core::powerOpFromName(QStringLiteral("monitor_off")), std::optional<core::PowerOp>(core::PowerOp::ScreenOff));
    QCOMPARE(core::powerOpFromName(QStringLiteral("display_off")), std::optional<core::PowerOp>(core::PowerOp::ScreenOff));
    // 大小写与首尾空白都不敏感。
    QCOMPARE(core::powerOpFromName(QStringLiteral("  SCREEN_OFF ")), std::optional<core::PowerOp>(core::PowerOp::ScreenOff));
}

void TestPowerTable::unknownNamesAreRejected()
{
    QVERIFY(!core::powerOpFromName(QStringLiteral("shutdown_now")).has_value());
    QVERIFY(!core::powerOpFromName(QString()).has_value());
}

void TestPowerTable::shorthandParsesEveryOp()
{
    for (const platform::win::power::PowerOpInfo &info : platform::win::power::powerOpTable()) {
        core::Action action;
        const QString shorthand = QStringLiteral("power:%1").arg(QString::fromLatin1(info.name));
        const std::optional<QString> error = core::parseActionShorthand(shorthand, &action);
        QVERIFY2(!error.has_value(), qPrintable(error.value_or(QString())));
        QVERIFY(action.kind == core::Action::Kind::Power);
        QVERIFY(action.powerOp == info.op);
        QCOMPARE(action.summary(), QStringLiteral("power %1").arg(QString::fromLatin1(info.name)));
    }
}

void TestPowerTable::shorthandRejectsUnknownOps()
{
    core::Action action;
    const std::optional<QString> error =
        core::parseActionShorthand(QStringLiteral("power:teleport"), &action);
    QVERIFY(error.has_value());
    // 报错文本要列出全部合法取值，用户才知道能写什么。
    QVERIFY(error->contains(QStringLiteral("sleep")));
    QVERIFY(error->contains(QStringLiteral("screen_off")));
}

QTEST_MAIN(TestPowerTable)
#include "tst_power_table.moc"
