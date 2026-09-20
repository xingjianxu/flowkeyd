// 虚拟桌面接口版本表的纯逻辑单测。
//
// 这里**不**调用 `desktop::switchTo()` / `probe()`：那会真的碰 shell 的 COM
// 接口（切换桌面会打断用户手上的事情）。覆盖的是 `apiFor()` 的选表规则、
// 已知 IID 与布局名——它们正是“换 Windows 版本后该改哪一行”的文档。
#include <QtTest>

#include "platform/win/desktop.h"

using namespace flowkeyd;

class TestDesktopTable : public QObject
{
    Q_OBJECT

private slots:
    void tableSelectionFollowsWindowsReleases();
    void twentyFourHTwoUsesTheKnownIids();
    void tableIsSortedByBuildAscending();
    void layoutNamesAreStable();
    void systemVersionIsReadable();
};

/// 选表规则：每个条目的 `build`/`revision` 是“从这里开始换成这套接口”。
void TestDesktopTable::tableSelectionFollowsWindowsReleases()
{
    struct Case
    {
        std::uint32_t build;
        std::uint32_t revision;
        std::uint32_t expectedBuild;
        const char *expectedLayout;
    };
    const Case cases[] = {
        // 比表里最旧的一条还旧：沿用第一条（Windows 10 19044+ / Server 2022）。
        {19045, 5000, 20348, "plain"},
        {20348, 0, 20348, "plain"},
        {22000, 194, 22000, "hmonitor"},
        {22483, 1000, 22483, "hmonitor"},
        // 22H2 在修订号 2215 上换了 vtable。
        {22621, 2214, 22483, "hmonitor"},
        {22621, 2215, 22621, "hmonitor-shifted"},
        // 23H2 的正式版（22631.2428）仍然是 22H2 的接口。
        {22631, 2428, 22621, "hmonitor-shifted"},
        {22631, 3085, 22631, "plain"},
        {26100, 1, 26100, "plain"},
        {26200, 9457, 26100, "plain"},
        // 表里没有的、更新的系统：兜底条目。
        {99999, 0, 99999, "plain"},
    };
    for (const Case &c : cases) {
        const platform::win::desktop::ApiEntry &api =
            platform::win::desktop::apiFor(c.build, c.revision);
        QVERIFY2(api.build == c.expectedBuild,
                 qPrintable(QStringLiteral("build %1.%2 picked %3, expected %4")
                                .arg(c.build)
                                .arg(c.revision)
                                .arg(api.build)
                                .arg(c.expectedBuild)));
        QCOMPARE(QString::fromLatin1(platform::win::desktop::layoutName(api.layout)),
                 QString::fromLatin1(c.expectedLayout));
    }
}

/// 24H2/25H2 的 IID 必须与 MScholtes/VirtualDesktop 的 24H2 版本一致，
/// 否则 `QueryService` 只会返回 `E_NOINTERFACE`。
void TestDesktopTable::twentyFourHTwoUsesTheKnownIids()
{
    const platform::win::desktop::ApiEntry &api = platform::win::desktop::apiFor(26100, 1);
    QCOMPARE(platform::win::desktop::guidText(api.manager),
             QStringLiteral("{53f5ca0b-158f-4124-900c-057158060b27}"));
    QCOMPARE(platform::win::desktop::guidText(api.desktop),
             QStringLiteral("{3f07f4be-b107-441a-af0f-39d82529072c}"));
}

/// `apiFor` 假设表按 `build` 升序；破坏这个前提会让选表静默出错。
void TestDesktopTable::tableIsSortedByBuildAscending()
{
    const std::vector<platform::win::desktop::ApiEntry> &table =
        platform::win::desktop::versionTable();
    QVERIFY(!table.empty());
    for (std::size_t index = 1; index < table.size(); ++index) {
        const std::uint32_t previous = table[index - 1].build;
        const std::uint32_t current = table[index].build;
        QVERIFY2(previous < current,
                 qPrintable(QStringLiteral("table[%1].build %2 >= table[%3].build %4")
                                .arg(index - 1)
                                .arg(previous)
                                .arg(index)
                                .arg(current)));
    }
}

void TestDesktopTable::layoutNamesAreStable()
{
    using platform::win::desktop::Layout;
    QCOMPARE(QString::fromLatin1(platform::win::desktop::layoutName(Layout::Plain)),
             QStringLiteral("plain"));
    QCOMPARE(QString::fromLatin1(platform::win::desktop::layoutName(Layout::Monitor)),
             QStringLiteral("hmonitor"));
    QCOMPARE(QString::fromLatin1(platform::win::desktop::layoutName(Layout::MonitorShifted)),
             QStringLiteral("hmonitor-shifted"));
}

/// 本机的 build 只可能来自 `RtlGetVersion`；读不到时退化成 0，绝不能崩。
void TestDesktopTable::systemVersionIsReadable()
{
    const platform::win::desktop::WindowsVersion version = platform::win::desktop::windowsVersion();
    QVERIFY2(version.build > 1000,
             qPrintable(QStringLiteral("unexpected build %1").arg(version.build)));
}

QTEST_MAIN(TestDesktopTable)
#include "tst_desktop_table.moc"
