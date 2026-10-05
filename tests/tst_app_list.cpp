// 程序启动器的纯逻辑（`core/app_list`）：图标键、名字匹配、「算不算程序 /
// 算不算卸载程序」的判据，以及扫描结果的去重 / 排序。
//
// **开始菜单的扫描不在里**（那是 `platform/win/apps` 的活儿，要真机：枚举
// `shell:AppsFolder` 得有一个真桌面）；登录之后真正跑一遍扫描的检查在
// `tst_interactive`（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 才跑）。
#include <QtTest>

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

#include "core/app_list.h"
#include "core/pinyin.h"

using namespace flowkeyd;

namespace {

/// 造一条扫描结果：启动名默认按 `shell:AppsFolder\<名字>` 拼，与真机上的形状
/// 一样（`name` 是显示名，命名随便给）。
core::AppEntry entry(const QString &name,
                     const QString &target,
                     const QString &arguments = QString(),
                     const QString &appId = QString())
{
    core::AppEntry item;
    item.name = name;
    item.launch = QStringLiteral("shell:AppsFolder\\")
                  + (appId.isEmpty() ? name : appId);
    item.target = target;
    item.arguments = arguments;
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

QStringList readings(char32_t codePoint)
{
    QStringList out;
    for (const QString &reading : core::pinyinReadings(codePoint)) {
        out.append(reading);
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
    void programTargetsAreExecutablesSystemToolsAndStoreApps();
    void uninstallerNamesAndTargetsAreRecognized();
    void nameMatchingIsACaseInsensitiveSubstring();
    void nameMatchingAcceptsPinyinAndInitials();
    void pinyinReadingsAreToneFreeLowercaseAndCoverTheIdeographs();
    void searchTextCoversPinyinInitialsAndLatinInitials();
    void searchTextFallsBackToPrimaryReadingsWhenVariantsExplode();
    void prepareTrimsNamesAndSorts();
    void prepareDropsEntriesWithoutNameOrLaunch();
    void prepareDeduplicatesSameNameAndLaunch();
    void prepareKeepsSameTargetUnderDifferentNames();
    void sortTextAndGroupLetterCoverPinyinLatinDigitsAndSymbols();
};

void TestAppList::iconKeyIsShortStableAndCaseInsensitive()
{
    const QString launch =
        QStringLiteral("shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
    const QString key = core::appIconKey(launch);
    QCOMPARE(key.size(), 16);
    QVERIFY(key.contains(QRegularExpression(QStringLiteral("^[0-9a-f]{16}$"))));

    // 同一个程序永远同一个键（QML 的图片缓存、`launcher.json` 里的固定/最近
    // 使用的身份都是它）。
    QCOMPARE(core::appIconKey(launch), key);
    // 大小写无关（AUMID 与路径都不区分大小写）。
    QCOMPARE(core::appIconKey(
                 QStringLiteral("SHELL:APPSFOLDER\\Microsoft.WindowsCalculator_8WEKYB3D8BBWE!APP")),
             key);
    // 不同的程序要给出不同的键。
    QVERIFY(core::appIconKey(QStringLiteral("shell:AppsFolder\\Microsoft.Paint_8wekyb3d8bbwe!App"))
            != key);
}

void TestAppList::iconUrlUsesTheKey()
{
    const QString launch = QStringLiteral("shell:AppsFolder\\Chrome");
    QCOMPARE(core::appIconUrl(launch),
             QStringLiteral("image://flowkeyd-app/") + core::appIconKey(launch));
    // id 里只有 `[0-9a-f]`：不用转义，提供者拿到的就是它本身。
    QVERIFY(!core::appIconUrl(launch).contains(QLatin1Char('\\')));
    QVERIFY(!core::appIconUrl(launch).contains(QLatin1Char(' ')));
}

void TestAppList::programTargetsAreExecutablesSystemToolsAndStoreApps()
{
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\Windows\\System32\\notepad.exe"),
                                     QString()));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\Apps\\FOO.EXE"), QString()));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("  C:\\Apps\\foo.exe  "), QString()));
    // 命令脚本 / 管理单元 / 控制面板项都算（开始菜单里的系统工具就是这一类）。
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\Tools\\build.cmd"), QString()));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\Tools\\build.bat"), QString()));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\WINDOWS\\system32\\services.msc"),
                                     QString()));
    QVERIFY(core::appTargetIsProgram(QStringLiteral("C:\\WINDOWS\\system32\\main.cpl"),
                                     QString()));
    // 目标可能是「已知文件夹 GUID + 相对路径」的 shell 写法，看末尾扩展名就行。
    QVERIFY(core::appTargetIsProgram(
        QStringLiteral("{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\WindowsPowerShell\\v1.0\\powershell.exe"),
        QString()));
    // shell 的虚拟项：「文件资源管理器」「控制面板」「运行」。
    QVERIFY(core::appTargetIsProgram(QStringLiteral("::{52205FD8-5DFB-447D-801A-D0B52F2E83E1}"),
                                     QString()));
    // 商店/UWP 应用没有目标路径，只有 `<包家族名>!<AppId>` 形状的 AUMID。
    QVERIFY(core::appTargetIsProgram(QString(),
                                     QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App")));
    QVERIFY(!core::appTargetIsProgram(QString(), QString()));
    QVERIFY(!core::appTargetIsProgram(QString(), QStringLiteral("Chrome")));
    // 文档 / 帮助 / 网址 / 文件夹 / 坏掉的条目都不是程序。
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Program Files"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Docs\\readme.md"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Docs\\readme.txt"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Tools\\7-zip.chm"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Tools\\docs.url"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Tools\\foo.com"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("C:\\Tools\\foo.exe.bak"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("https://example.com"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("http://support.example.com/"), QString()));
    QVERIFY(!core::appTargetIsProgram(QStringLiteral("steam://rungameid/570"), QString()));
}

void TestAppList::uninstallerNamesAndTargetsAreRecognized()
{
    // 名字里有「卸载」（真机上的「卸载微信」「卸载 PixPin」）。
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("卸载微信"),
                                         QStringLiteral("C:\\Weixin.exe")));
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("卸载小狼毫"),
                                         QStringLiteral("C:\\WeaselSetup.exe")));
    // 名字里有独立的 `uninstall` 词（真机上的「Uninstall Qt」）。
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("Uninstall Qt"),
                                         QStringLiteral("C:\\Qt\\MaintenanceTool.exe")));
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("Uninstaller"), QString()));
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("uninstall foo"), QString()));
    // 名字看不出，但目标是安装器生成的反向操作程序。
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("PixPin"),
                                         QStringLiteral("C:\\Apps\\PixPin\\unins000.exe")));
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("Something"),
                                         QStringLiteral("C:\\Apps\\uninstall.exe")));
    QVERIFY(core::appLooksLikeUninstaller(QStringLiteral("Something"),
                                         QStringLiteral("C:\\Apps\\unwise.exe")));

    // 名字里只是恰好含有这几个字母（不是一个词）就不算。
    QVERIFY(!core::appLooksLikeUninstaller(QStringLiteral("MyUninstallerPro"), QString()));
    // 安装器 / 维护工具 / 配置工具都是正常入口，不能猜着丢掉。
    QVERIFY(!core::appLooksLikeUninstaller(QStringLiteral("Visual Studio Installer"),
                                          QStringLiteral("C:\\setup.exe")));
    QVERIFY(!core::appLooksLikeUninstaller(QStringLiteral("Qt Maintenance Tool"),
                                          QStringLiteral("C:\\Qt\\MaintenanceTool.exe")));
    QVERIFY(!core::appLooksLikeUninstaller(QStringLiteral("配置工具"),
                                          QStringLiteral("C:\\ksomisc.exe")));
    QVERIFY(!core::appLooksLikeUninstaller(QStringLiteral("计算器"),
                                          QStringLiteral("C:\\calc.exe")));
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

void TestAppList::nameMatchingAcceptsPinyinAndInitials()
{
    // 全拼、首字母（大小写无关）都算命中。
    QVERIFY(core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("jishiben")));
    QVERIFY(core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("jsb")));
    QVERIFY(core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("  JIS  ")));
    QVERIFY(core::appNameMatches(QStringLiteral("Visual Studio Code"),
                                 QStringLiteral("vsc")));
    QVERIFY(!core::appNameMatches(QStringLiteral("网易云音乐"),
                                  QStringLiteral("wangyiyunyue")));
    // 多音字的每一种读音都能用。
    QVERIFY(core::appNameMatches(QStringLiteral("网易云音乐"),
                                 QStringLiteral("yinyue")));
    QVERIFY(core::appNameMatches(QStringLiteral("网易云音乐"),
                                 QStringLiteral("yinle")));
    // 拼音不是模糊匹配：跨音节乱拼、多打一个字母都不该命中。
    QVERIFY(!core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("jishibenx")));
    QVERIFY(!core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("jb")));
    QVERIFY(!core::appNameMatches(QStringLiteral("记事本"), QStringLiteral("benji")));
}

void TestAppList::pinyinReadingsAreToneFreeLowercaseAndCoverTheIdeographs()
{
    // 单个字：去声调、小写 ASCII；`ü` 记作 `v` 之外也认写成 `u` 的那一条。
    QCOMPARE(readings(0x8BB0), QStringList({QStringLiteral("ji")})); // 记
    QCOMPARE(readings(0x4E50),
             QStringList({QStringLiteral("le"), QStringLiteral("yue")})); // 乐
    QVERIFY(readings(0x5973).contains(QStringLiteral("nv")));                  // 女
    QVERIFY(readings(0x5973).contains(QStringLiteral("nu")));
    QVERIFY(readings(0x884C).contains(QStringLiteral("hang")));                // 行
    // 表外：非汉字、Ext A、以及没进表的码点都没有读音。
    QVERIFY(readings(0x41).isEmpty());
    QVERIFY(readings(0x3400).isEmpty());
    QVERIFY(readings(0x2F800).isEmpty());

    // 整张表跑一遍：读音必须是 `[a-z]+`、同一个字不能给重复读音、覆盖率不能塌
    // （真机数据：U+4E00–U+9FFF 的 20992 个码点里有 20924 个有读音）。
    const QRegularExpression shape(QStringLiteral("^[a-z]+$"));
    int covered = 0;
    for (char32_t codePoint = 0x4E00; codePoint <= 0x9FFFu; ++codePoint) {
        const QStringList list = readings(codePoint);
        if (list.isEmpty()) {
            continue;
        }
        ++covered;
        QSet<QString> unique;
        for (const QString &reading : list) {
            QVERIFY2(shape.match(reading).hasMatch(),
                     qPrintable(QStringLiteral("U+%1: %2")
                                    .arg(static_cast<uint>(codePoint), 4, 16, QLatin1Char('0'))
                                    .arg(reading)));
            QVERIFY(!unique.contains(reading));
            unique.insert(reading);
        }
    }
    QVERIFY(covered >= 20000);
}

void TestAppList::searchTextCoversPinyinInitialsAndLatinInitials()
{
    // 第 1 段还是名字本身：原来的字面匹配一个字都不变。
    const QString jishiben = core::appSearchText(QStringLiteral("记事本"));
    QVERIFY(jishiben.startsWith(QStringLiteral("记事本")));
    QVERIFY(jishiben.contains(QStringLiteral("jishiben")));
    QVERIFY(jishiben.contains(QStringLiteral("jsb")));

    // 多音字：每一种读音都在搜索串里。
    const QString music = core::appSearchText(QStringLiteral("网易云音乐"));
    QVERIFY(music.contains(QStringLiteral("wangyiyunyinyue")));
    QVERIFY(music.contains(QStringLiteral("wangyiyunyinle")));
    QVERIFY(music.contains(QStringLiteral("wyyy")));

    // 拉丁名字：整段保留 + 词首字母缩写。
    const QString code = core::appSearchText(QStringLiteral("Visual Studio Code"));
    QVERIFY(code.contains(QStringLiteral("vsc")));
    QVERIFY(code.contains(QStringLiteral("studio")));

    // 中文 + 拉丁混排：汉字进拼音，拉丁段整段保留、首字母取那一段的首字符。
    const QString mixed = core::appSearchText(QStringLiteral("QQ音乐"));
    QVERIFY(mixed.contains(QStringLiteral("qqyinyue")));
    QVERIFY(mixed.contains(QStringLiteral("qyy")));

    // 空格与标点：全拼里按字面留着，首字母里丢掉。
    const QString spaced = core::appSearchText(QStringLiteral("记事 本（Pro）"));
    QVERIFY(spaced.contains(QStringLiteral("jishi ben")));
    QVERIFY(spaced.contains(QStringLiteral("jsbp")));

    // 表外的汉字（Ext A）没有拼音：它按字面留在全拼里，不贡献首字母。
    const QString rare = core::appSearchText(QString(QStringLiteral("记")) + QChar(0x3400)
                                             + QStringLiteral("本"));
    QVERIFY(rare.contains(QStringLiteral("ji\u3400ben")));
    QVERIFY(rare.contains(QStringLiteral("jb")));
}

void TestAppList::searchTextFallsBackToPrimaryReadingsWhenVariantsExplode()
{
    // 长(zhang, chang) 行(xing, hang, heng)：2×3×2×3 = 36 种组合，超过上限，
    // 于是只用主读音（长 zhang、行 xing）。
    const QString text = core::appSearchText(QStringLiteral("长行长行"));
    QVERIFY(text.contains(QStringLiteral("zhangxingzhangxing")));
    QVERIFY(text.contains(QStringLiteral("zxzx")));
    // 只有主读音那一种组合：次读音的组合一个都不该在。
    QVERIFY(!text.contains(QStringLiteral("changxing")));
    QVERIFY(!text.contains(QStringLiteral("zhanghang")));
    QVERIFY(!text.contains(QStringLiteral("chchang")));
    QVERIFY(!text.contains(QStringLiteral("zxhx")));
}

void TestAppList::prepareTrimsNamesAndSorts()
{
    std::vector<core::AppEntry> entries{
        entry(QStringLiteral("  zeta  "), QStringLiteral("C:\\z.exe")),
        entry(QStringLiteral("Alpha"), QStringLiteral("C:\\a.exe")),
        entry(QStringLiteral("beta"), QStringLiteral("C:\\b.exe")),
    };
    const std::vector<core::AppEntry> kept = core::prepareAppEntries(std::move(entries));
    // 名字两边的空白去掉（免得同一个程序因为看不见的字符被当成两条），
    // 然后按名字（大小写无关）排序。
    QCOMPARE(names(kept), QStringList({QStringLiteral("Alpha"), QStringLiteral("beta"),
                                       QStringLiteral("zeta")}));
}

void TestAppList::prepareDropsEntriesWithoutNameOrLaunch()
{
    core::AppEntry noName = entry(QString(), QStringLiteral("C:\\x.exe"));
    core::AppEntry noLaunch = entry(QStringLiteral("no-launch"), QStringLiteral("C:\\y.exe"));
    noLaunch.launch.clear();
    // 只有空白字符的名字在去掉首尾空白之后也等于没有名字。
    core::AppEntry blankName = entry(QStringLiteral("   "), QStringLiteral("C:\\z.exe"));

    const std::vector<core::AppEntry> kept =
        core::prepareAppEntries({noName, noLaunch, blankName});
    QVERIFY(kept.empty());
}

void TestAppList::prepareDeduplicatesSameNameAndLaunch()
{
    // 同一个名字 + 同一个启动名只留一条（第二层保险，真机上 AppsFolder 本来就
    // 不会列两遍）。
    std::vector<core::AppEntry> entries{
        entry(QStringLiteral("Terminal"), QStringLiteral("C:\\wt.exe"), QStringLiteral("--foo"),
              QStringLiteral("Microsoft.WindowsTerminal_8wekyb3d8bbwe!App")),
        entry(QStringLiteral("terminal"), QStringLiteral("C:\\wt.exe"), QStringLiteral("--foo"),
              QStringLiteral("microsoft.windowsterminal_8wekyb3d8bbwe!app")),
    };
    const std::vector<core::AppEntry> kept = core::prepareAppEntries(std::move(entries));
    QCOMPARE(kept.size(), std::size_t(1));
    QCOMPARE(kept.front().name, QStringLiteral("Terminal"));
}

void TestAppList::prepareKeepsSameTargetUnderDifferentNames()
{
    // 名字不同、却指向同一个 exe 的条目是两条独立的入口（真机例子：
    // `Developer PowerShell for VS` 与 `Debuggable Package Manager` 都是
    // powershell.exe，只是 AUMID 不同）。
    std::vector<core::AppEntry> entries{
        entry(QStringLiteral("Developer PowerShell for VS"),
              QStringLiteral("C:\\powershell.exe"), QString(),
              QStringLiteral("Microsoft.AutoGenerated.{9736CA63-…}")),
        entry(QStringLiteral("Debuggable Package Manager"),
              QStringLiteral("C:\\powershell.exe"), QString(),
              QStringLiteral("Microsoft.AutoGenerated.{96690F80-…}")),
        // 名字一样、但启动名不同：也是两条。
        entry(QStringLiteral("Chrome"), QStringLiteral("C:\\chrome.exe"), QString(),
              QStringLiteral("Chrome")),
        entry(QStringLiteral("Chrome"), QStringLiteral("C:\\chrome.exe"), QString(),
              QStringLiteral("Chrome.Beta")),
    };
    const std::vector<core::AppEntry> kept = core::prepareAppEntries(std::move(entries));
    QCOMPARE(kept.size(), std::size_t(4));
}

void TestAppList::sortTextAndGroupLetterCoverPinyinLatinDigitsAndSymbols()
{
    // 中文名字：排序键是主读音全拼，分组表头是它的首字母（大写）。
    const core::AppSortInfo notepad = core::appSortInfo(QStringLiteral("记事本"));
    QCOMPARE(notepad.sortText, QStringLiteral("1jishiben"));
    QCOMPARE(notepad.letter, QStringLiteral("J"));

    // 拉丁名字：排序键就是它自己（小写），空格保留。
    const core::AppSortInfo code = core::appSortInfo(QStringLiteral("Visual Studio Code"));
    QCOMPARE(code.sortText, QStringLiteral("1visual studio code"));
    QCOMPARE(code.letter, QStringLiteral("V"));

    // 数字开头的：`0` 前缀（排在字母组前面），归「#」。
    const core::AppSortInfo zip = core::appSortInfo(QStringLiteral("7-Zip"));
    QVERIFY(zip.sortText.startsWith(QLatin1Char('0')));
    QCOMPARE(zip.letter, QStringLiteral("#"));

    // 符号开头（中文全角括号）：也归「#」，而且与数字开头一样带 `0` 前缀
    // —— 没有这个前缀，`【` 的码点比 `z` 大，会把「#」那一组撕成两段。
    const core::AppSortInfo bracket =
        core::appSortInfo(QStringLiteral("【小狼毫】输入法设定"));
    QVERIFY(bracket.sortText.startsWith(QLatin1Char('0')));
    QCOMPARE(bracket.letter, QStringLiteral("#"));

    // 前导空白不参与分组。
    QCOMPARE(core::appGroupLetter(QStringLiteral("  zoom")), QStringLiteral("Z"));

    // 空名字：没有首字母，归「#」。
    QCOMPARE(core::appGroupLetter(QString()), QStringLiteral("#"));

    // `appSortText()` / `appGroupLetter()` 就是 `appSortInfo()` 的两个字段。
    QCOMPARE(core::appSortText(QStringLiteral("微信")), QStringLiteral("1weixin"));
    QCOMPARE(core::appGroupLetter(QStringLiteral("微信")), QStringLiteral("W"));

    // 同一组（同一个表头）的条目都排在一起：按 sortText 排一遍，表头序列里
    // 不会出现同一个字母两次。
    const QStringList names{QStringLiteral("微信"), QStringLiteral("Visual Studio Code"),
                            QStringLiteral("记事本"), QStringLiteral("7-Zip"),
                            QStringLiteral("【小狼毫】输入法设定"), QStringLiteral("Word")};
    QStringList keys;
    for (const QString &name : names) {
        keys.append(core::appSortText(name));
    }
    std::sort(keys.begin(), keys.end());
    QStringList letters;
    for (const QString &key : keys) {
        const QString letter = key.at(1).isLetter()
            ? QString(key.at(1).toUpper())
            : QStringLiteral("#");
        if (letters.isEmpty() || letters.constLast() != letter) {
            letters.append(letter);
        }
    }
    QCOMPARE(letters, QStringList({QStringLiteral("#"), QStringLiteral("J"),
                                  QStringLiteral("V"), QStringLiteral("W")}));
}

QTEST_MAIN(TestAppList)
#include "tst_app_list.moc"
