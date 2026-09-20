// `help` 帮助窗口模型的纯逻辑单测：筛选、滚动钳位、`可见/总数`、`Enter` 复制、
// 两级 `Esc`、鼠标命中与滚轮。
//
// 不碰 QML、不碰剪贴板：`onCopy` 的回调在 `app::PopupHost` 里，
// 真实剪贴板由 `tst_interactive` / 手工冒烟覆盖。
#include <QtTest>

#include <QAbstractItemModel>

#include "app/help_model.h"

using namespace flowkeyd;

namespace {

app::HelpEntry entry(const QStringList &chords,
                     const QString &label,
                     std::optional<QString> detail = std::nullopt)
{
    app::HelpEntry item;
    item.chords = chords;
    item.label = label;
    item.detail = detail;
    return item;
}

std::vector<app::HelpEntry> sampleItems()
{
    return {
        entry({QStringLiteral("Ctrl+Alt+F12")}, QStringLiteral("睡眠"),
              QStringLiteral("power sleep")),
        entry({QStringLiteral("Win+X"), QStringLiteral("Ctrl+Alt+X")}, QStringLiteral("电源选单"),
              QStringLiteral("menu \"电源\" (4 item(s))")),
        entry({QStringLiteral("CapsLock")}, QStringLiteral("大写锁定关闭")),
    };
}

/// `n` 条可滚动的条目（用来验证视口钳位与滚动条几何）。
std::vector<app::HelpEntry> manyItems(int n)
{
    std::vector<app::HelpEntry> items;
    for (int i = 0; i < n; ++i) {
        items.push_back(entry({QStringLiteral("Ctrl+F%1").arg(i)},
                              QStringLiteral("item %1").arg(i),
                              QStringLiteral("run thing%1").arg(i)));
    }
    return items;
}

void checkRect(const app::PopupRect &actual, int x, int y, int width, int height)
{
    QCOMPARE(actual.x, x);
    QCOMPARE(actual.y, y);
    QCOMPARE(actual.width, width);
    QCOMPARE(actual.height, height);
}

int roleOf(const QAbstractItemModel &model, const char *name)
{
    const QHash<int, QByteArray> names = model.roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (it.value() == name) {
            return it.key();
        }
    }
    return -1;
}

QString decisionOf(const QVariantMap &map)
{
    return map.value(QStringLiteral("decision")).toString();
}

int indexOf(const QVariantMap &map)
{
    return map.value(QStringLiteral("index")).toInt();
}

bool handledOf(const QVariantMap &map)
{
    return map.value(QStringLiteral("handled")).toBool();
}

} // namespace

class TestHelpModel : public QObject
{
    Q_OBJECT

private slots:
    void layoutKeepsRowsInsideTheCard();
    void filterMatchesChordsLabelsAndDetails();
    void filterTrimsWhitespaceAndIsCaseInsensitive();
    void countsAndCaptionFollowTheFilter();
    void enterCopiesTheActiveRow();
    void escapeClearsTheFilterFirstThenCancels();
    void backspaceRemovesOneCharacterAndRefilters();
    void arrowKeysClampAndScroll();
    void pageKeysHomeEndAndWheel();
    void clickRowSelectsAndReportsTheVisibleIndex();
    void scrollbarAppearsOnlyWhenTheContentOverflows();
    void maxRowsLimitsTheVisibleRows();
    void badgesSplitChordsAndInsertSeparators();
    void emptyResultShowsTheRightMessage();
    void rowsForAvailableHeightIsClamped();
    void controlCharactersAreNotFilterInput();
    void rolesExposeTheGeometryForQml();
};

void TestHelpModel::layoutKeepsRowsInsideTheCard()
{
    app::HelpModel model;
    model.setItems(QStringLiteral("快捷键"), sampleItems());

    QCOMPARE(model.totalCount(), 3);
    QCOMPARE(model.visibleCount(), 3);
    QCOMPARE(model.visibleRows(), 3);
    QCOMPARE(model.cardWidth(), 500);
    // 88(表头+筛选框) + 3*48 + 8 + 24(底部提示) + 12
    QCOMPARE(model.cardHeight(), 276);

    checkRect(model.titleRect(), 22, 12, 273, 30);
    checkRect(model.countRect(), 22, 12, 456, 30);
    checkRect(model.filterRect(), 22, 50, 456, 30);
    checkRect(model.footerRect(), 22, 240, 456, 24);

    checkRect(model.rowRect(0), 22, 88, 456, 46);
    checkRect(model.rowRect(2), 22, 184, 456, 46);
    QVERIFY(model.rowRect(2).y + model.rowRect(2).height <= model.footerRect().y);
    checkRect(model.keysRect(0), 32, 88, 150, 46);
    checkRect(model.textRect(0), 192, 88, 276, 46);
    QVERIFY(model.footerText().contains(QStringLiteral("Esc")));
}

void TestHelpModel::filterMatchesChordsLabelsAndDetails()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    model.setFilter(QStringLiteral("F12"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.visibleIndices(), std::vector<int>{0});

    model.setFilter(QStringLiteral("power"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{0});

    model.setFilter(QStringLiteral("大写"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{2});

    model.setFilter(QStringLiteral("Win"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{1});

    model.clearFilter();
    QCOMPARE(model.visibleCount(), 3);
}

void TestHelpModel::filterTrimsWhitespaceAndIsCaseInsensitive()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    model.setFilter(QStringLiteral("  CAPSLOCK  "));
    QCOMPARE(model.visibleIndices(), std::vector<int>{2});
    // 空白串等于没有筛选。
    model.setFilter(QStringLiteral("   "));
    QCOMPARE(model.visibleCount(), 3);
    // 筛选串本身保留大小写（窗口上显示的是用户打进去的东西）。
    model.setFilter(QStringLiteral("WiN"));
    QCOMPARE(model.filter(), QStringLiteral("WiN"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{1});
}

void TestHelpModel::countsAndCaptionFollowTheFilter()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    QCOMPARE(model.countText(), QStringLiteral("3 项"));
    QCOMPARE(model.caption(), QStringLiteral("flowkeyd 快捷键 — 3/3 项"));

    model.setFilter(QStringLiteral("F12"));
    QCOMPARE(model.countText(), QStringLiteral("1 / 3 项"));
    QCOMPARE(model.caption(), QStringLiteral("flowkeyd 快捷键 — 1/3 项"));

    // 再次弹出时筛选被清空（`reset`）。
    model.reset();
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 3);
    QCOMPARE(model.caption(), QStringLiteral("flowkeyd 快捷键 — 3/3 项"));
}

void TestHelpModel::enterCopiesTheActiveRow()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    QCOMPARE(model.copyText(), QStringLiteral("Ctrl+Alt+F12"));
    const QVariantMap first = model.handleKey(Qt::Key_Return, QString());
    QCOMPARE(decisionOf(first), QStringLiteral("copy"));
    QCOMPARE(indexOf(first), 0);

    // 多个和弦用 ` / ` 连起来（与 oskeyd 的 `copy_active` 一致）。
    model.moveSelection(1);
    QCOMPARE(model.copyText(), QStringLiteral("Win+X / Ctrl+Alt+X"));
    QCOMPARE(indexOf(model.handleKey(Qt::Key_Enter, QString())), 1);

    // 筛选之后复制的是筛选结果里的那一条。
    model.setFilter(QStringLiteral("大写"));
    QCOMPARE(model.copyText(), QStringLiteral("CapsLock"));

    // 一条都没有时没有东西可复制。
    model.setFilter(QStringLiteral("zzz"));
    QCOMPARE(model.copyText(), QString());
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Return, QString())), QStringLiteral("none"));
}

void TestHelpModel::escapeClearsTheFilterFirstThenCancels()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());
    model.setFilter(QStringLiteral("F12"));

    // 第一下 `Esc` 只清筛选（否则删错一个字就得重开）。
    const QVariantMap first = model.handleKey(Qt::Key_Escape, QString());
    QCOMPARE(decisionOf(first), QStringLiteral("none"));
    QCOMPARE(handledOf(first), true);
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 3);

    // 筛选本来就是空的：第二下才关窗。
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Escape, QString())), QStringLiteral("cancel"));
}

void TestHelpModel::backspaceRemovesOneCharacterAndRefilters()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());
    model.setFilter(QStringLiteral("CapsX"));

    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Backspace, QString())), QStringLiteral("none"));
    QCOMPARE(model.filter(), QStringLiteral("Caps"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{2});

    // 筛选已经空了：退格什么也不做，也不关窗。
    model.clearFilter();
    model.handleKey(Qt::Key_Backspace, QString());
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 3);
}

void TestHelpModel::arrowKeysClampAndScroll()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(5));
    model.setMaxRows(2);

    QCOMPARE(model.visibleRows(), 2);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.scroll(), 0);

    // 到边界是夹住（与选单的回绕不同）。
    model.moveSelection(-1);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.scroll(), 0);

    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Down, QString())), QStringLiteral("none"));
    QCOMPARE(model.selected(), 1);
    QCOMPARE(model.scroll(), 0);
    model.handleKey(Qt::Key_Down, QString());
    QCOMPARE(model.selected(), 2);
    QCOMPARE(model.scroll(), 1);

    // 一直往下：停在最后一条，视口跟着到底。
    for (int i = 0; i < 10; ++i) {
        model.handleKey(Qt::Key_Down, QString());
    }
    QCOMPARE(model.selected(), 4);
    QCOMPARE(model.scroll(), 3);
    QCOMPARE(model.visibleRows(), 2);

    // 往上：选中项还在视口里就不动滚动位置，再往上才把它拉回视野。
    model.handleKey(Qt::Key_Up, QString());
    QCOMPARE(model.selected(), 3);
    QCOMPARE(model.scroll(), 3);
    model.handleKey(Qt::Key_Up, QString());
    QCOMPARE(model.selected(), 2);
    QCOMPARE(model.scroll(), 2);
}

void TestHelpModel::pageKeysHomeEndAndWheel()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(10));
    model.setMaxRows(3);

    model.handleKey(Qt::Key_PageDown, QString());
    QCOMPARE(model.selected(), 3);
    QCOMPARE(model.scroll(), 1);
    model.handleKey(Qt::Key_PageUp, QString());
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.scroll(), 0);

    model.handleKey(Qt::Key_End, QString());
    QCOMPARE(model.selected(), 9);
    QCOMPARE(model.scroll(), 7);
    model.handleKey(Qt::Key_Home, QString());
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.scroll(), 0);

    // 滚轮：一格（±120）跳过三行，与 oskeyd 的 `WM_MOUSEWHEEL` 逐字一致。
    model.wheel(120);
    QCOMPARE(model.selected(), 3);
    QCOMPARE(model.scroll(), 1);
    model.wheel(-120);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.scroll(), 0);
    // 小于一格的增量什么也不做（与 oskeyd 相同）。
    model.wheel(60);
    QCOMPARE(model.selected(), 0);
    // 内容放得下时滚不动。
    app::HelpModel shortModel;
    shortModel.setItems(std::nullopt, sampleItems());
    shortModel.wheel(-120);
    QCOMPARE(shortModel.selected(), 0);
    QCOMPARE(shortModel.scroll(), 0);
}

void TestHelpModel::clickRowSelectsAndReportsTheVisibleIndex()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(5));
    model.setMaxRows(2);

    // 第二行（可见下标 1）在画出来的第一行位置上。
    QCOMPARE(model.hitTest(100, 100), 0);
    QCOMPARE(model.hitTest(100, 150), 1);
    // 表头/筛选框/内边距都不是条目。
    QCOMPARE(model.hitTest(100, 20), -1);
    QCOMPARE(model.hitTest(100, 60), -1);
    QCOMPARE(model.hitTest(5, 100), -1);

    QCOMPARE(model.clickRow(100, 150), 1);
    QCOMPARE(model.selected(), 1);
    QCOMPARE(model.hover(), 1);
    QCOMPARE(model.copyText(), QStringLiteral("Ctrl+F1"));

    // 滚过一行之后，画出来的第一行是可见下标 1。
    model.handleKey(Qt::Key_Down, QString());
    QCOMPARE(model.selected(), 2);
    QCOMPARE(model.scroll(), 1);
    QCOMPARE(model.hitTest(100, 100), 1);
    QCOMPARE(model.hitTest(100, 150), 2);

    // 点到空白处什么也不做。
    QCOMPARE(model.clickRow(100, 20), -1);
    QCOMPARE(model.selected(), 2);
}

void TestHelpModel::scrollbarAppearsOnlyWhenTheContentOverflows()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());
    QCOMPARE(model.hasScrollbar(), false);
    QCOMPARE(model.scrollTrack().height, 3 * 48 - 2);
    checkRect(model.scrollThumb(), 0, 0, 0, 0);

    model.setItems(std::nullopt, manyItems(5));
    model.setMaxRows(2);
    QCOMPARE(model.hasScrollbar(), true);
    checkRect(model.scrollTrack(), 483, 88, 5, 94);
    checkRect(model.scrollThumb(), 483, 88, 5, 37);

    // 滚到底：滑块贴着滑槽底部。
    model.handleKey(Qt::Key_End, QString());
    QCOMPARE(model.scroll(), 3);
    checkRect(model.scrollThumb(), 483, 145, 5, 37);
}

void TestHelpModel::maxRowsLimitsTheVisibleRows()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(5));
    QCOMPARE(model.visibleRows(), 5);

    model.setMaxRows(2);
    QCOMPARE(model.visibleRows(), 2);
    QCOMPARE(model.maxRows(), 2);
    QCOMPARE(model.cardHeight(), 88 + 2 * 48 + 8 + 24 + 12);
    QCOMPARE(model.rowCount(), 2);

    // 一条都没有时也留一行的高度（否则卡片会缩成一条线）。
    model.setItems(std::nullopt, {});
    QCOMPARE(model.visibleRows(), 1);
    QCOMPARE(model.rowCount(), 0);

    model.setMaxRows(0);
    QCOMPARE(model.maxRows(), 1);
}

void TestHelpModel::badgesSplitChordsAndInsertSeparators()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    const QVariantList badges = model.badgesForItem(1);
    QCOMPARE(badges.size(), 6);
    QCOMPARE(badges.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Win"));
    QCOMPARE(badges.at(0).toMap().value(QStringLiteral("badge")).toBool(), true);
    QCOMPARE(badges.at(2).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("·"));
    QCOMPARE(badges.at(2).toMap().value(QStringLiteral("badge")).toBool(), false);
    QCOMPARE(badges.at(3).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Ctrl"));
    QCOMPARE(badges.at(5).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("X"));

    // 单个和弦就是几个牌子，没有分隔点。
    const QVariantList single = model.badgesForVisible(0);
    QCOMPARE(single.size(), 3);
    QCOMPARE(single.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Alt"));

    // 越界不崩。
    QCOMPARE(model.badgesForItem(99).size(), 0);
    QCOMPARE(model.badgesForVisible(99).size(), 0);
}

void TestHelpModel::emptyResultShowsTheRightMessage()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());
    QCOMPARE(model.hasMatches(), true);

    model.setFilter(QStringLiteral("zzz"));
    QCOMPARE(model.hasMatches(), false);
    QCOMPARE(model.visibleRows(), 1);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.emptyMessage(), QStringLiteral("没有匹配的快捷键"));

    app::HelpModel empty;
    empty.setItems(std::nullopt, {});
    QCOMPARE(empty.emptyMessage(), QStringLiteral("配置里还没有快捷键"));
    QCOMPARE(empty.hasMatches(), false);
}

void TestHelpModel::rowsForAvailableHeightIsClamped()
{
    // 很高的显示器也最多 12 行。
    QCOMPARE(app::HelpModel::rowsForAvailableHeight(2000), 12);
    // 1080p：12 行的卡片放得下。
    QCOMPARE(app::HelpModel::rowsForAvailableHeight(1080), 12);
    // 小屏：只放得下几行。
    QCOMPARE(app::HelpModel::rowsForAvailableHeight(400), 4);
    // 再小也至少一行。
    QCOMPARE(app::HelpModel::rowsForAvailableHeight(100), 1);
}

void TestHelpModel::controlCharactersAreNotFilterInput()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    // `Tab`/`Ctrl+字母` 这类控制字符不是输入内容（退格另有处理）。
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Tab, QString(QChar(0x0009)))), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_A, QString(QChar(0x0001)))), false);
    QCOMPARE(model.filter(), QString());

    // 普通字符追加到筛选串后面。
    QCOMPARE(handledOf(model.handleKey(Qt::Key_C, QStringLiteral("c"))), true);
    model.handleKey(Qt::Key_A, QStringLiteral("a"));
    model.handleKey(Qt::Key_P, QStringLiteral("p"));
    QCOMPARE(model.filter(), QStringLiteral("cap"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{2});
}

void TestHelpModel::rolesExposeTheGeometryForQml()
{
    app::HelpModel model;
    model.setItems(QStringLiteral("快捷键"), sampleItems());

    const int badgesRole = roleOf(model, "badges");
    const int labelRole = roleOf(model, "label");
    const int detailRole = roleOf(model, "detail");
    const int highlightedRole = roleOf(model, "highlighted");
    const int rowRole = roleOf(model, "rowRect");
    const int textRole = roleOf(model, "textRect");
    QVERIFY(badgesRole > 0);
    QVERIFY(labelRole > 0);
    QVERIFY(detailRole > 0);
    QVERIFY(highlightedRole > 0);
    QVERIFY(rowRole > 0);
    QVERIFY(textRole > 0);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0, 0), labelRole).toString(), QStringLiteral("睡眠"));
    QCOMPARE(model.data(model.index(0, 0), detailRole).toString(), QStringLiteral("power sleep"));
    QCOMPARE(model.data(model.index(2, 0), detailRole).toString(), QString());
    QCOMPARE(model.data(model.index(0, 0), highlightedRole).toBool(), true);
    QCOMPARE(model.data(model.index(1, 0), highlightedRole).toBool(), false);
    QCOMPARE(model.data(model.index(0, 0), badgesRole).toList().size(), 3);
    QCOMPARE(model.data(model.index(1, 0), rowRole).toRect(), QRect(22, 136, 456, 46));
    QCOMPARE(model.data(model.index(1, 0), textRole).toRect(), QRect(192, 136, 276, 46));

    // 越界/无效下标不能崩。
    QCOMPARE(model.data(QModelIndex(), labelRole).isValid(), false);
    QCOMPARE(model.data(model.index(9, 0), labelRole).isValid(), false);
}

QTEST_MAIN(TestHelpModel)
#include "tst_help_model.moc"
