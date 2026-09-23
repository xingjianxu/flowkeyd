// 托盘虚拟桌面徽标的纯逻辑单测：`core::desktopBadgeText` / `desktopBadgeFontPixels`。
//
// 真正画图的是 `app/app_icon.cpp`（QtGui，需要 QGuiApplication），所以这里只钉住
// 那两件事：画什么字、字号怎么随尺寸与位数变（见 AGENTS.md 第 2 节第 20 条）。
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
    void fontShrinksForTwoCharacters();
    void fontGrowsWithTheIcon();
    void fontStaysLegibleAndInsideTheIcon();
};

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

void TestDesktopBadge::fontShrinksForTwoCharacters()
{
    QVERIFY(core::desktopBadgeFontPixels(64, 1) > core::desktopBadgeFontPixels(64, 2));
    QVERIFY(core::desktopBadgeFontPixels(16, 1) > core::desktopBadgeFontPixels(16, 2));
    // 没有字要画时是 0，不是某个最小值。
    QCOMPARE(core::desktopBadgeFontPixels(64, 0), 0);
}

void TestDesktopBadge::fontGrowsWithTheIcon()
{
    QVERIFY(core::desktopBadgeFontPixels(32, 1) > core::desktopBadgeFontPixels(16, 1));
    QVERIFY(core::desktopBadgeFontPixels(256, 2) > core::desktopBadgeFontPixels(64, 2));
}

void TestDesktopBadge::fontStaysLegibleAndInsideTheIcon()
{
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        for (const int characters : {1, 2}) {
            const int pixels = core::desktopBadgeFontPixels(size, characters);
            QVERIFY2(pixels >= 6, qPrintable(QStringLiteral("size %1: font %2").arg(size).arg(pixels)));
            QVERIFY2(pixels <= size, qPrintable(QStringLiteral("size %1: font %2").arg(size).arg(pixels)));
        }
    }
}

QTEST_MAIN(TestDesktopBadge)
#include "tst_desktop_badge.moc"
