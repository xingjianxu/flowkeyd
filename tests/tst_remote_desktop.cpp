// 「这个前台进程算不算远程桌面客户端」的纯逻辑（`core/remote_desktop`）。
//
// 判定本身只是一次大小写无关的子串匹配，真正值得钉住的是两个边界：
//   * 内置名单的形状（小写、只列微软 RDP 客户端）；
//   * 拿不到属主进程名 / 名单为空时的答案必须是 false —— 否则一次误判就会把
//     所有快捷键一起放行。
#include "core/remote_desktop.h"

#include <QtTest>

using namespace flowkeyd::core;

namespace {

std::optional<QString> exe(const QString &name)
{
    return std::optional<QString>(name);
}

} // namespace

class TestRemoteDesktop : public QObject
{
    Q_OBJECT

private slots:
    void builtinListCoversTheRdpClients();
    void matchingIsCaseInsensitiveAndSubstring();
    void customListReplacesTheBuiltinOne();
    void unknownOwnerNeverCounts();
    void emptyEntriesNeverMatchEverything();
};

void TestRemoteDesktop::builtinListCoversTheRdpClients()
{
    const QStringList &builtin = builtinRemoteDesktopProcesses();
    QVERIFY(!builtin.isEmpty());
    for (const QString &name : builtin) {
        // 进程名统一小写（AGENTS.md 第 2 节第 17 条的同一套风格）。
        QCOMPARE(name, name.toLower());
        QVERIFY(!name.trimmed().isEmpty());
    }
    QVERIFY(builtin.contains(QStringLiteral("mstsc.exe")));
    QVERIFY(builtin.contains(QStringLiteral("msrdc.exe")));
    // 第三方远程控制软件**刻意**不在默认名单里：它们在前台就整片放行是另一种口味。
    QVERIFY(!builtin.contains(QStringLiteral("anydesk.exe")));
}

void TestRemoteDesktop::matchingIsCaseInsensitiveAndSubstring()
{
    const QStringList &builtin = builtinRemoteDesktopProcesses();
    QVERIFY(isRemoteDesktopProcess(exe(QStringLiteral("mstsc.exe")), builtin));
    QVERIFY(isRemoteDesktopProcess(exe(QStringLiteral("MSTSC.EXE")), builtin));
    // 与 `window_rule` / `window` 动作的 `process` 一样是子串匹配。
    QVERIFY(isRemoteDesktopProcess(exe(QStringLiteral("RdClient.Windows.exe")), builtin));
    QVERIFY(!isRemoteDesktopProcess(exe(QStringLiteral("chrome.exe")), builtin));
    QVERIFY(!isRemoteDesktopProcess(exe(QStringLiteral("explorer.exe")), builtin));
}

void TestRemoteDesktop::customListReplacesTheBuiltinOne()
{
    const QStringList custom{QStringLiteral("ToDesk.exe"), QStringLiteral("SunloginClient.exe")};
    QVERIFY(isRemoteDesktopProcess(exe(QStringLiteral("ToDesk.exe")), custom));
    QVERIFY(isRemoteDesktopProcess(exe(QStringLiteral("sunloginclient.exe")), custom));
    // 自定义名单是**整体替换**：内置的 mstsc 就不再算数了。
    QVERIFY(!isRemoteDesktopProcess(exe(QStringLiteral("mstsc.exe")), custom));
    // 空名单 = 谁都不算（等于关掉这项检测）。
    QVERIFY(!isRemoteDesktopProcess(exe(QStringLiteral("mstsc.exe")), QStringList{}));
}

void TestRemoteDesktop::unknownOwnerNeverCounts()
{
    QVERIFY(!isRemoteDesktopProcess(std::nullopt, builtinRemoteDesktopProcesses()));
    QVERIFY(!isRemoteDesktopProcess(std::nullopt, QStringList{QStringLiteral("mstsc.exe")}));
}

void TestRemoteDesktop::emptyEntriesNeverMatchEverything()
{
    // 空串在 `windowProcessMatches()` 里是「不限制」的意思，那会让任何前台窗口
    // 都变成远程桌面；`isRemoteDesktopProcess()` 必须跳过它（加载时也会报错）。
    const QStringList withEmpty{QStringLiteral("mstsc.exe"), QString()};
    QVERIFY(!isRemoteDesktopProcess(exe(QStringLiteral("chrome.exe")), withEmpty));
    QVERIFY(isRemoteDesktopProcess(exe(QStringLiteral("mstsc.exe")), withEmpty));
    QVERIFY(!isRemoteDesktopProcess(exe(QStringLiteral("chrome.exe")), QStringList{QString()}));
}

QTEST_MAIN(TestRemoteDesktop)
#include "tst_remote_desktop.moc"
