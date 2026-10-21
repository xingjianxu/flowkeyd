// 托盘数字徽标的纯逻辑单测：`core::desktopBadgeText` / `desktopBadgeFontPixels` /
// `trayBadgeParts` / `trayBadgeText` / 显示模式的解析。
//
// 真正画图的是 `app/app_icon.cpp`（QtGui，需要 QGuiApplication），所以这里只钉住
// 那几件事：画什么字、两位号码怎么拼、字号怎么随尺寸与位数变（见 AGENTS.md 第 2 节
// 第 20 条）。
#include <QtTest>

#include "core/desktop_badge.h"

using namespace flowkeyd;

class TestDesktopBadge : public QObject
{
    Q_OBJECT

private slots:
    void textIsTheDesktopNumber();
    void textIsNinePlusBeyondNine();
    void textIsEmptyWhenThereIsNoDesktop();
    void modeNamesRoundTrip();
    void bothModeWritesWorkspaceSlashDesktop();
    void bothModeFallsBackWhenOneNumberIsMissing();
    void desktopOnlyModeIgnoresTheWorkspace();
    void workspaceOnlyModeIgnoresTheDesktop();
    void partsSayWhenToSplitTheBackground();
    void fontShrinksForTwoCharacters();
    void fontShrinksAgainForThreeCharacters();
    void cornerFontIsMuchLargerThanTheOneLineFont();
    void cornerFontShrinksForTwoCharacters();
    void fontGrowsWithTheIcon();
    void fontStaysLegibleAndInsideTheIcon();
};

namespace {

core::TrayBadge badgeOf(core::TrayBadgeMode mode, int workspace, int desktop)
{
    core::TrayBadge badge;
    badge.mode = mode;
    badge.workspace = workspace;
    badge.workspaceCount = workspace > 0 ? 9 : 0;
    badge.desktop = desktop;
    badge.desktopCount = desktop > 0 ? 4 : 0;
    return badge;
}

} // namespace

void TestDesktopBadge::textIsTheDesktopNumber()
{
    QCOMPARE(core::desktopBadgeText(1), QStringLiteral("1"));
    QCOMPARE(core::desktopBadgeText(3), QStringLiteral("3"));
    QCOMPARE(core::desktopBadgeText(9), QStringLiteral("9"));
}

void TestDesktopBadge::textIsNinePlusBeyondNine()
{
    // 项目所有者 2026-09 拍板：两位数在 16 px 图标上挤，统一画 `9+`。
    QCOMPARE(core::desktopBadgeText(10), QStringLiteral("9+"));
    QCOMPARE(core::desktopBadgeText(42), QStringLiteral("9+"));
}

void TestDesktopBadge::textIsEmptyWhenThereIsNoDesktop()
{
    // `0` 是「读不到当前桌面」（probe 里 current 为 0 的那种情况），
    // 调用方看到空串就退回应用图标，而不是画一个 `0`。
    QCOMPARE(core::desktopBadgeText(0), QString());
    QCOMPARE(core::desktopBadgeText(-1), QString());
}

void TestDesktopBadge::modeNamesRoundTrip()
{
    // `settings.tray_badge` 的取值是公开接口：名字与解析必须一一对上。
    QCOMPARE(core::trayBadgeModeName(core::TrayBadgeMode::Both), QStringLiteral("both"));
    QCOMPARE(core::trayBadgeModeName(core::TrayBadgeMode::Desktop), QStringLiteral("desktop"));
    QCOMPARE(core::trayBadgeModeName(core::TrayBadgeMode::Workspace),
             QStringLiteral("workspace"));
    for (const QString &name : core::trayBadgeModeNames()) {
        const std::optional<core::TrayBadgeMode> parsed = core::trayBadgeModeFromName(name);
        QVERIFY2(parsed.has_value(), qPrintable(name));
        QCOMPARE(core::trayBadgeModeName(*parsed), name);
    }
    // 大小写与空白照旧宽容（与其他枚举一致）。
    QCOMPARE(core::trayBadgeModeFromName(QStringLiteral(" Both ")),
             std::optional<core::TrayBadgeMode>(core::TrayBadgeMode::Both));
    QCOMPARE(core::trayBadgeModeFromName(QStringLiteral("nope")), std::nullopt);
}

void TestDesktopBadge::bothModeWritesWorkspaceSlashDesktop()
{
    // 默认样式（项目所有者 2026-10 要求）：左边工作空间号、右边虚拟桌面号。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Both, 1, 2)), QStringLiteral("1/2"));
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Both, 9, 3)), QStringLiteral("9/3"));
    // 两位数照旧压成 `9+`（两侧各自裁）。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Both, 10, 12)),
             QStringLiteral("9+/9+"));
}

void TestDesktopBadge::bothModeFallsBackWhenOneNumberIsMissing()
{
    // 工作空间关着 / 问不出来：只画虚拟桌面号（整块蓝底，见 `trayBadgeParts`）。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Both, 0, 2)), QStringLiteral("2"));
    // 读不到虚拟桌面（锁屏）：只画工作空间号（绿底）。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Both, 3, 0)), QStringLiteral("3"));
    // 两个都没有 → 空串（调用方退回应用图标，不画 `0`）。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Both, 0, 0)), QString());
}

void TestDesktopBadge::desktopOnlyModeIgnoresTheWorkspace()
{
    // 本特性出现之前的样子：只看虚拟桌面号，蓝底。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Desktop, 5, 2)),
             QStringLiteral("2"));
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Desktop, 5, 0)), QString());
}

void TestDesktopBadge::workspaceOnlyModeIgnoresTheDesktop()
{
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Workspace, 5, 2)),
             QStringLiteral("5"));
    // 工作空间关着时这一模式没有可画的号：退回应用图标（README 里写明了）。
    QCOMPARE(core::trayBadgeText(badgeOf(core::TrayBadgeMode::Workspace, 0, 2)), QString());
}

void TestDesktopBadge::partsSayWhenToSplitTheBackground()
{
    const core::TrayBadgeParts both = core::trayBadgeParts(badgeOf(core::TrayBadgeMode::Both, 1, 2));
    QCOMPARE(both.workspace, QStringLiteral("1"));
    QCOMPARE(both.desktop, QStringLiteral("2"));
    QVERIFY(both.isSplit());
    QVERIFY(!both.isEmpty());

    // 只有一个号时不切色（整块用它自己的颜色）。
    const core::TrayBadgeParts desktopOnly =
        core::trayBadgeParts(badgeOf(core::TrayBadgeMode::Both, 0, 2));
    QVERIFY(!desktopOnly.isSplit());
    QCOMPARE(desktopOnly.desktop, QStringLiteral("2"));

    const core::TrayBadgeParts workspaceOnly =
        core::trayBadgeParts(badgeOf(core::TrayBadgeMode::Workspace, 4, 2));
    QVERIFY(!workspaceOnly.isSplit());
    QCOMPARE(workspaceOnly.workspace, QStringLiteral("4"));
    QVERIFY(workspaceOnly.desktop.isEmpty());
    QVERIFY(!workspaceOnly.isEmpty());

    QVERIFY(core::trayBadgeParts(badgeOf(core::TrayBadgeMode::Both, 0, 0)).isEmpty());
}

void TestDesktopBadge::fontShrinksForTwoCharacters()
{
    QVERIFY(core::desktopBadgeFontPixels(64, 1) > core::desktopBadgeFontPixels(64, 2));
    QVERIFY(core::desktopBadgeFontPixels(16, 1) > core::desktopBadgeFontPixels(16, 2));
    // 没有字要画时是 0，不是某个最小值。
    QCOMPARE(core::desktopBadgeFontPixels(64, 0), 0);
}

void TestDesktopBadge::fontShrinksAgainForThreeCharacters()
{
    // `1/2` 是三个字符：字号必须比 `9+` 再收一点，否则会顶到圆角外面。
    QVERIFY(core::desktopBadgeFontPixels(64, 2) > core::desktopBadgeFontPixels(64, 3));
    QVERIFY(core::desktopBadgeFontPixels(16, 2) >= core::desktopBadgeFontPixels(16, 3));
}

void TestDesktopBadge::cornerFontIsMuchLargerThanTheOneLineFont()
{
    // 项目所有者 2026-10：两个号各占一个角（左上 / 右下），不再把 `1/2` 挤成一行
    // —— 每个号的字号必须明显变大，否则还是看不清。
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        const int corner = core::desktopBadgeCornerFontPixels(size, 1);
        const int oneLine = core::desktopBadgeFontPixels(size, 3);
        QVERIFY2(corner > oneLine,
                 qPrintable(QStringLiteral("size %1: corner %2 vs one-line %3")
                                .arg(size)
                                .arg(corner)
                                .arg(oneLine)));
    }
    // 16 px 上一个是 8 px（一行写 `1/2` 时只有 6 px）。
    QCOMPARE(core::desktopBadgeCornerFontPixels(16, 1), 8);
    QCOMPARE(core::desktopBadgeCornerFontPixels(32, 1), 16);
}

void TestDesktopBadge::cornerFontShrinksForTwoCharacters()
{
    // `9+` 要缩一档，否则会碰到对角线；但没有字要画时仍然是 0。
    for (const int size : {16, 32, 64, 256}) {
        QVERIFY(core::desktopBadgeCornerFontPixels(size, 1)
                > core::desktopBadgeCornerFontPixels(size, 2));
    }
    QCOMPARE(core::desktopBadgeCornerFontPixels(64, 0), 0);
}

void TestDesktopBadge::fontGrowsWithTheIcon()
{
    QVERIFY(core::desktopBadgeFontPixels(32, 1) > core::desktopBadgeFontPixels(16, 1));
    QVERIFY(core::desktopBadgeFontPixels(256, 2) > core::desktopBadgeFontPixels(64, 2));
    QVERIFY(core::desktopBadgeFontPixels(256, 3) > core::desktopBadgeFontPixels(64, 3));
}

void TestDesktopBadge::fontStaysLegibleAndInsideTheIcon()
{
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        for (const int characters : {1, 2, 3}) {
            const int pixels = core::desktopBadgeFontPixels(size, characters);
            QVERIFY2(pixels >= 6, qPrintable(QStringLiteral("size %1: font %2").arg(size).arg(pixels)));
            QVERIFY2(pixels <= size, qPrintable(QStringLiteral("size %1: font %2").arg(size).arg(pixels)));
        }
        for (const int characters : {1, 2}) {
            const int pixels = core::desktopBadgeCornerFontPixels(size, characters);
            QVERIFY2(pixels >= 6, qPrintable(QStringLiteral("size %1: corner %2").arg(size).arg(pixels)));
            QVERIFY2(pixels <= size / 2,
                     qPrintable(QStringLiteral("size %1: corner %2").arg(size).arg(pixels)));
        }
    }
}

QTEST_MAIN(TestDesktopBadge)
#include "tst_desktop_badge.moc"
