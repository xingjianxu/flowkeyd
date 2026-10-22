#include "core/switch_cover.h"

#include <QtTest>

using namespace flowkeyd::core;

class TestSwitchCover : public QObject
{
    Q_OBJECT

private slots:
    void chromiumClassFlickers();
    void electronProcessesFlicker();
    void plainWin32DoesNotFlicker();
    void emptyTraitsDoNotFlicker();
    void coverOnlyWhenSomethingFlickers();
    void coverIsNeededForAnySingleFlashyWindow();
};

void TestSwitchCover::chromiumClassFlickers()
{
    // Chrome / Edge / 全系 Electron 都是这个类名，大写小写都要认。
    QVERIFY(flickersWhenRevealed(RevealTraits{QStringLiteral("Chrome_WidgetWin_1"), QString()}));
    QVERIFY(flickersWhenRevealed(
        RevealTraits{QStringLiteral("chrome_widgetwin_1"), QStringLiteral("whatever.exe")}));
    // 前后空白不该让它漏。
    QVERIFY(flickersWhenRevealed(
        RevealTraits{QStringLiteral("  Chrome_WidgetWin_1  "), QString()}));
}

void TestSwitchCover::electronProcessesFlicker()
{
    // 类名认不出来时还有进程名这一层（WezTerm 这类不是 Chrome 窗口类）。
    QVERIFY(flickersWhenRevealed(RevealTraits{QStringLiteral("org.wezfurlong.wezterm"),
                                               QStringLiteral("wezterm-gui.exe")}));
    QVERIFY(flickersWhenRevealed(RevealTraits{QStringLiteral("SunAwtFrame"),
                                               QStringLiteral("Code.exe")}));
    QVERIFY(flickersWhenRevealed(
        RevealTraits{QString(), QStringLiteral("MSEDGE.EXE")}));
}

void TestSwitchCover::plainWin32DoesNotFlicker()
{
    // M11 / M12 实测：纯 GDI 程序（记事本）从 `SW_HIDE` 回来不闪。
    QVERIFY(!flickersWhenRevealed(
        RevealTraits{QStringLiteral("Notepad"), QStringLiteral("notepad.exe")}));
    QVERIFY(!flickersWhenRevealed(
        RevealTraits{QStringLiteral("CabinetWClass"), QStringLiteral("explorer.exe")}));
    QVERIFY(!flickersWhenRevealed(
        RevealTraits{QStringLiteral("WeaselUIWindow"), QStringLiteral("WeaselServer.exe")}));
}

void TestSwitchCover::emptyTraitsDoNotFlicker()
{
    QVERIFY(!flickersWhenRevealed(RevealTraits{}));
    QVERIFY(!flickersWhenRevealed(RevealTraits{QStringLiteral("   "), QStringLiteral("  ")}));
    // 空串不能被当成“不限制”（决策 26 同一条坑：空 needle 会匹配所有）。
    QVERIFY(!flickersWhenRevealed(RevealTraits{QString(), QStringLiteral(".exe")}));
}

void TestSwitchCover::coverOnlyWhenSomethingFlickers()
{
    QVERIFY(!needsCoverForReveal({}));
    QVERIFY(!needsCoverForReveal({
        RevealTraits{QStringLiteral("Notepad"), QStringLiteral("notepad.exe")},
        RevealTraits{QStringLiteral("CabinetWClass"), QStringLiteral("explorer.exe")},
    }));
    QVERIFY(needsCoverForReveal({
        RevealTraits{QStringLiteral("Notepad"), QStringLiteral("notepad.exe")},
        RevealTraits{QStringLiteral("Chrome_WidgetWin_1"), QStringLiteral("chrome.exe")},
    }));
}

void TestSwitchCover::coverIsNeededForAnySingleFlashyWindow()
{
    // 会闪的那个在末尾也一样要盖（判据是“任何一个”，不是“第一个”）。
    std::vector<RevealTraits> windows(8, RevealTraits{QStringLiteral("Notepad"),
                                                      QStringLiteral("notepad.exe")});
    QVERIFY(!needsCoverForReveal(windows));
    windows.back() = RevealTraits{QStringLiteral("Chrome_WidgetWin_1"), QStringLiteral("msedge.exe")};
    QVERIFY(needsCoverForReveal(windows));
}

QTEST_MAIN(TestSwitchCover)
#include "tst_switch_cover.moc"
