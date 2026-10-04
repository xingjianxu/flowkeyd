// 程序启动器的纯逻辑（`core/app_list`）：图标键、名字匹配、「算不算程序」的
// 判据，以及扫描结果的去重 / 排序。
//
// **开始菜单的扫描不在里**（那是 `platform/win/apps` 的活儿，要真机：真机上
// 不存在某个目录、某个 `.lnk` 是坏的都正常）；登录之后真正跑一遍扫描的检查在
// `tst_interactive`（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 才跑）。
#include <QtTest>

#include <QRegularExpression>

#include "core/app_list.h"

using namespace flowkeyd;

namespace {

core::AppEntry entry(const QString &name,
                     const QString &target,
                     const QString &arguments = QString(),
                     const QString &group = QString())
{
    core::AppEntry item;
    item.name = name;
    item.shortcut = QStringLiteral("C:\\Start Menu\\Programs\\") + name;
    item.target = target;
    item.arguments = arguments;
    item.group = group;
    return item;
}

QStringList names(const std::vector<core::AppEntry> &entries)
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(entries.size()));
    for (const core::AppEntry &item : entries) {
        out.append(item.name);
    }
    return out;
}

} // namespace

class TestAppList : public QObject
{
    Q_OBJECT

private slots:
    void iconKeyIsShortStableAndCaseInsensitive();
    void iconUrlUsesTheKey();
    void programTargetsAreExecutablesOrIdListOnly();
    void nameMatchingIsACaseInsensitiveSubstring();
    void prepareStripsTheLnkSuffixAndSorts();
    void prepareDropsEntriesWithoutNameOrShortcut();
    void prepareDeduplicatesSameNameAndTarget();
    void prepareKeepsSameTargetUnderDifferentNames();
};

void TestAppList::iconKeyIsShortStableAndCaseInsensitive()
{
    const QString key = core::appIconKey(QStringLiteral("C:\\Start Menu\\Programs\\Notepad.lnk"));
    QCOMPARE(key.size(), 16);
    QVERIFY(key.contains(QRegularExpression(QStringLiteral("^[0-9a-f]{16}$"))));

    // 同一个快捷方式永远同一个键（QML 的图片缓存靠它）。
    QCOMPARE(core::appIconKey(QStringLiteral("C:\\Start Menu\\Programs\\Notepad.lnk")), key);
    // 大小写无关、分隔符两种写法等价（Windows 的路径本来就不区分大小写）。
    QCOMPARE(core::appIconKey(QStringLiteral("c:/start menu/programs/NOTEPAD.LNK")), key);
    // 不同的快捷方式要给出不同的键。
    QVERIFY(core::appIconKey(QStringLiteral("C:\\Start Menu\\Programs\\Calc.lnk")) != key);
}

void TestAppList::iconUrlUsesTheKey()
{
    const QString path = QStringLiteral("C:\\Start Menu\\Programs\\Notepad.lnk");
    QCOMPARE(core::appIconUrl(path),
             QStringLiteral("image://flowkeyd-app/") + core::appIconKey(path));
    // id 里只有 `[0-9a-f]`：不用转义，提供者拿到的就是它本身。
    QVERIFY(!core::appIconUrl(path).contains(QLatin1Char('\\')));
    QVERIFY(!core::appIconUrl(path).contains(QLatin1Char(' ')));
}

void TestAppList::programTargetsAreExecutablesOrIdListOnly()
{
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\Windows\\System32\\notepad.exe"), false));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\Apps\\FOO.EXE"), false));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("  C:\\Apps\\foo.exe  "), false));
    // 商店 / UWP 应用的快捷方式可能只有 IDList（没有目标路径）。
    QVERIFY(core::appTargetIsProgram(QString(), true));
    QVERIFY(!core::appTargetIsProgram(QString(), false));
    // 文件夹、文档、命令脚本、坏掉的快捷方式都不是程序。
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Program Files"), false));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Docs\\readme.md"), false));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Tools\\foo.com"), false));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Tools\\foo.exe.bak"), false));
}

void TestAppList::nameMatchingIsACaseInsensitiveSubstring()
{
    QVERIFY(core::appNameMatches(QStringLiteral("Visual Studio Code"), QString()));
    QVERIFY(core::appNameMatches(QStringLiteral("Visual Studio Code"), QStringLiteral("  ")));
    // 子串（不是前缀）：记得名字中间一段也能找到。
    QVERIFY(core::appNameMatches(QStringLiteral("Visual Studio Code"), QStringLiteral("studio")));
    QVERIFY(core::appNameMatches(QStringLiteral("Google Chrome"), QStringLiteral("  CHROME ")));
    // 中文名字照样匹配。
    QVERIFY(core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("记事")));
    QVERIFY(!core::appNameMatches(QStringLiteral("Google Chrome"), QStringLiteral("firefox")));
}

void TestAppList::prepareStripsTheLnkSuffixAndSorts()
{
    std::vector<core::AppEntry> entries{
        entry(QStringLiteral("zeta.lnk"), QStringLiteral("C:\\z.exe")),
        entry(QStringLiteral("Alpha.lnk"), QStringLiteral("C:\\a.exe")),
        entry(QStringLiteral("beta"), QStringLiteral("C:\\b.exe")),
    };
    const std::vector<core::AppEntry> kept = core::prepareAppEntries(std::move(entries));
    // `--list` / 卡片里的名字不带 `.lnk`，而且按名字（大小写无关）排序。
    QCOMPARE(names(kept), QStringList({QStringLiteral("Alpha"), QStringLiteral("beta"),
                                       QStringLiteral("zeta")}));
}

void TestAppList::prepareDropsEntriesWithoutNameOrShortcut()
{
    core::AppEntry noName = entry(QString(), QStringLiteral("C:\\x.exe"));
    core::AppEntry noShortcut = entry(QStringLiteral("no-shortcut"), QStringLiteral("C:\\y.exe"));
    noShortcut.shortcut.clear();
    // 只有空白字符的名字在去掉首尾空白之后也等于没有名字。
    core::AppEntry blankName = entry(QStringLiteral("   "), QStringLiteral("C:\\z.exe"));

    const std::vector<core::AppEntry> kept =
        core::prepareAppEntries({noName, noShortcut, blankName});
    QVERIFY(kept.empty());
}

void TestAppList::prepareDeduplicatesSameNameAndTarget()
{
    // 同一个程序在「全局开始菜单」与「当前用户开始菜单」里各一份是常态。
    std::vector<core::AppEntry> entries{
        entry(QStringLiteral("Terminal.lnk"), QStringLiteral("C:\\wt.exe"),
              QStringLiteral("--foo"), QStringLiteral("Windows Terminal")),
        entry(QStringLiteral("Terminal.lnk"), QStringLiteral("C:\\wt.exe"),
              QStringLiteral("--foo")),
    };
    entries[0].shortcut = QStringLiteral("C:\\ProgramData\\Start Menu\\Programs\\Windows "
                                         "Terminal\\Terminal.lnk");
    entries[1].shortcut = QStringLiteral("C:\\Users\\me\\AppData\\Roaming\\Start "
                                         "Menu\\Programs\\Terminal.lnk");
    const QString expected = entries[1].shortcut;
    const std::vector<core::AppEntry> kept = core::prepareAppEntries(std::move(entries));
    QCOMPARE(kept.size(), std::size_t(1));
    // 层级更浅的那一份留下（它更靠近开始菜单的顶层）。
    QVERIFY(kept.front().group.isEmpty());
    QCOMPARE(kept.front().shortcut, expected);
}

void TestAppList::prepareKeepsSameTargetUnderDifferentNames()
{
    // 名字不同、却指向同一个 exe 的条目是两条独立的入口（真机例子：
    // `Developer PowerShell for VS` 与 `Debuggable Package Manager` 都是
    // powershell.exe）。
    std::vector<core::AppEntry> entries{
        entry(QStringLiteral("Developer PowerShell for VS.lnk"),
              QStringLiteral("C:\\powershell.exe")),
        entry(QStringLiteral("Debuggable Package Manager.lnk"),
              QStringLiteral("C:\\powershell.exe")),
        // 名字一样但参数不同：也是两条（快捷方式里写死了不同的启动参数）。
        entry(QStringLiteral("Chrome.lnk"), QStringLiteral("C:\\chrome.exe"),
              QStringLiteral("--profile-directory=Work")),
        entry(QStringLiteral("Chrome.lnk"), QStringLiteral("C:\\chrome.exe")),
    };
    const std::vector<core::AppEntry> kept = core::prepareAppEntries(std::move(entries));
    QCOMPARE(kept.size(), std::size_t(4));
}

QTEST_MAIN(TestAppList)
#include "tst_app_list.moc"
