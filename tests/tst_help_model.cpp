// `help` 帮助窗口模型的纯逻辑单测：筛选、`可见/总数`、`Enter` 复制、两级 `Esc`、
// 键盘与鼠标选中行。
//
// **滚动不在这一层**：列表是 QML 里的真 `ListView` + 自带 `ScrollBar`，所以这里
// 只验证「卡片几何与 `ListView` 的内容高度能不能对齐」以及「选中项变了要通知视图」
// 这类契约，不碰任何滚动位置（见 `app/help_model.h` 顶部的说明）。
// **输入也不在这一层**：筛选框是标准的 `TextField`，字符 / 退格 / `Home` / `End`
// 都归它，模型只接 `setFilter()`；这里盯的是「模型不接编辑键」(handled == false)。
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

/// `n` 条条目（用来验证视口钳位）。
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
    void contentHeightMatchesTheCardUnlessItOverflows();
    void filterMatchesChordsLabelsAndDetails();
    void filterTrimsWhitespaceAndIsCaseInsensitive();
    void countsAndCaptionFollowTheFilter();
    void enterCopiesTheActiveRow();
    void escapeClearsTheFilterFirstThenCancels();
    void arrowKeysClampAtTheEnds();
    void pageKeysMoveTheSelection();
    void mouseClickSelectsTheRow();
    void selectionChangedTellsTheViewToFollow();
    void maxRowsLimitsTheVisibleRows();
    void badgesSplitChordsAndInsertSeparators();
    void emptyResultShowsTheRightMessage();
    void rowsForAvailableHeightIsClamped();
    void editingKeysAreLeftToTheTextField();
    void rolesExposeWhatTheDelegateNeeds();
};

void TestHelpModel::layoutKeepsRowsInsideTheCard()
{
    app::HelpModel model;
    model.setItems(QStringLiteral("快捷键"), sampleItems());

    QCOMPARE(model.totalCount(), 3);
    QCOMPARE(model.visibleCount(), 3);
    QCOMPARE(model.visibleRows(), 3);
    QCOMPARE(model.cardWidth(), 500);
    // 88(表头+筛选框) + 3*48 + 44(底部提示+内边距)
    QCOMPARE(model.cardHeight(), 276);

    checkRect(model.titleRect(), 22, 12, 273, 30);
    checkRect(model.countRect(), 22, 12, 456, 30);
    checkRect(model.filterRect(), 22, 50, 456, 30);
    checkRect(model.footerRect(), 22, 240, 456, 24);

    // 列表区就是夹在表头与底部提示之间的那一段；行高 + 空隙由 QML 的
    // `ListView` 直接用，所以这里要保证它们能拼出卡片高度。
    QCOMPARE(model.listTop(), 88);
    QCOMPARE(model.rowHeight(), 46);
    QCOMPARE(model.rowSpacing(), 2);
    QCOMPARE(model.cardHeight(), model.listTop() + 3 * (model.rowHeight() + model.rowSpacing())
                                     + model.listBottom());
    // 行不会盖住底部提示
    QVERIFY(model.listTop() + 3 * (model.rowHeight() + model.rowSpacing())
            <= model.footerRect().y);
    QVERIFY(model.footerText().contains(QStringLiteral("Esc")));
}

// `ListView` 的内容高度 = header(`listTop`) + 行数*(行高+空隙) + footer(`listBottom`)。
// 只要卡片高度与它相等，Qt 自带的滚动条就恰好在「放不下」时出现。
void TestHelpModel::contentHeightMatchesTheCardUnlessItOverflows()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());
    QCOMPARE(model.visibleCount(), model.visibleRows());
    QCOMPARE(model.cardHeight(), 276);

    model.setItems(std::nullopt, manyItems(5));
    model.setMaxRows(2);
    QCOMPARE(model.visibleCount(), 5);
    QCOMPARE(model.visibleRows(), 2);
    // 卡片只放得下 2 行：内容比卡片高 (5 - 2) * 48
    QCOMPARE(model.cardHeight(), 88 + 2 * 48 + 44);
    QCOMPARE(model.listTop() + model.visibleCount() * (model.rowHeight() + model.rowSpacing())
                 + model.listBottom(),
             model.cardHeight() + (5 - 2) * 48);
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
    const QVariantMap first = model.handleKey(Qt::Key_Return);
    QCOMPARE(decisionOf(first), QStringLiteral("copy"));
    QCOMPARE(indexOf(first), 0);

    // 多个和弦用 ` / ` 连起来（与 oskeyd 的 `copy_active` 一致）。
    model.moveSelection(1);
    QCOMPARE(model.copyText(), QStringLiteral("Win+X / Ctrl+Alt+X"));
    QCOMPARE(indexOf(model.handleKey(Qt::Key_Enter)), 1);

    // 筛选之后复制的是筛选结果里的那一条。
    model.setFilter(QStringLiteral("大写"));
    QCOMPARE(model.copyText(), QStringLiteral("CapsLock"));

    // 一条都没有时没有东西可复制。
    model.setFilter(QStringLiteral("zzz"));
    QCOMPARE(model.copyText(), QString());
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Return)), QStringLiteral("none"));
}

void TestHelpModel::escapeClearsTheFilterFirstThenCancels()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());
    model.setFilter(QStringLiteral("F12"));

    // 第一下 `Esc` 只清筛选（否则删错一个字就得重开），而且要告诉 QML 把
    // 输入框里的文本也清掉（`clear`；输入框自己持有它显示的文本）。
    const QVariantMap first = model.handleKey(Qt::Key_Escape);
    QCOMPARE(decisionOf(first), QStringLiteral("clear"));
    QCOMPARE(handledOf(first), true);
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 3);

    // 筛选本来就是空的：第二下才关窗。
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Escape)), QStringLiteral("cancel"));
}

void TestHelpModel::arrowKeysClampAtTheEnds()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(5));
    model.setMaxRows(2);

    QCOMPARE(model.visibleRows(), 2);
    QCOMPARE(model.selected(), 0);

    // 到边界是夹住（与选单的回绕不同）。
    model.moveSelection(-1);
    QCOMPARE(model.selected(), 0);

    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Down)), QStringLiteral("none"));
    QCOMPARE(model.selected(), 1);
    model.handleKey(Qt::Key_Down);
    QCOMPARE(model.selected(), 2);

    // 一直往下：停在最后一条。
    for (int i = 0; i < 10; ++i) {
        model.handleKey(Qt::Key_Down);
    }
    QCOMPARE(model.selected(), 4);
    QCOMPARE(model.visibleRows(), 2);

    model.handleKey(Qt::Key_Up);
    QCOMPARE(model.selected(), 3);
}

void TestHelpModel::pageKeysMoveTheSelection()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(10));
    model.setMaxRows(3);

    model.handleKey(Qt::Key_PageDown);
    QCOMPARE(model.selected(), 3);
    model.handleKey(Qt::Key_PageUp);
    QCOMPARE(model.selected(), 0);

    // `Home`/`End` 归筛选框那个标准 `TextField`（它们在那里是光标移动），
    // 模型一概不接。
    QCOMPARE(handledOf(model.handleKey(Qt::Key_End)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Home)), false);
    QCOMPARE(model.selected(), 0);

    // 内容放得下时翻页照样能选，而且会被夹在两端。
    app::HelpModel shortModel;
    shortModel.setItems(std::nullopt, sampleItems());
    shortModel.handleKey(Qt::Key_PageDown);
    QCOMPARE(shortModel.selected(), 2);
    shortModel.handleKey(Qt::Key_PageDown);
    QCOMPARE(shortModel.selected(), 2);
    shortModel.handleKey(Qt::Key_PageUp);
    QCOMPARE(shortModel.selected(), 0);
}

// 鼠标点选走的是 `setSelected()`（`ItemDelegate.onClicked` 调它），它与键盘的
// `moveSelection()` 只共用「键盘选中项」这一个状态：高亮跟着它走。
void TestHelpModel::mouseClickSelectsTheRow()
{
    app::HelpModel model;
    QSignalSpy spy(&model, &app::HelpModel::selectedChanged);
    model.setItems(std::nullopt, manyItems(5));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.selected(), 0);

    model.setSelected(3);
    QCOMPARE(model.selected(), 3);
    QCOMPARE(spy.count(), 2);

    // 点同一行不重复发信号（QML 不必白白重算一遍）。
    model.setSelected(3);
    QCOMPARE(spy.count(), 2);

    // 越界夹住（与键盘导航一致，不回绕）。
    model.setSelected(99);
    QCOMPARE(model.selected(), 4);
    model.setSelected(-7);
    QCOMPARE(model.selected(), 0);

    // 一条都没有时什么也不做。
    app::HelpModel empty;
    empty.setSelected(2);
    QCOMPARE(empty.selected(), 0);
}

// QML 用 `selectedChanged` 把选中项带进视野（`positionViewAtIndex(..., Contain)`）。
// 筛选之后列表短了、选中项回到第 0 条，也必须通知一次，否则视图会停在旧位置上。
void TestHelpModel::selectionChangedTellsTheViewToFollow()
{
    app::HelpModel model;
    QSignalSpy spy(&model, &app::HelpModel::selectedChanged);

    model.setItems(std::nullopt, manyItems(10));
    QCOMPARE(spy.count(), 1);

    model.moveSelection(1);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(model.selected(), 1);
    // 夹在边界上、数值没变时不发信号。
    model.moveSelection(-5);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(spy.count(), 3);
    model.moveSelection(-1);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(spy.count(), 3);

    model.setFilter(QStringLiteral("F1"));
    QVERIFY(spy.count() > 3);
    QCOMPARE(model.selected(), 0);
}

void TestHelpModel::maxRowsLimitsTheVisibleRows()
{
    app::HelpModel model;
    model.setItems(std::nullopt, manyItems(5));
    QCOMPARE(model.visibleRows(), 5);
    // 全部条目都在模型里，由 `ListView` 决定画面里放得下几条。
    QCOMPARE(model.rowCount(), 5);

    model.setMaxRows(2);
    QCOMPARE(model.visibleRows(), 2);
    QCOMPARE(model.maxRows(), 2);
    QCOMPARE(model.cardHeight(), 88 + 2 * 48 + 44);
    QCOMPARE(model.rowCount(), 5);

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

void TestHelpModel::editingKeysAreLeftToTheTextField()
{
    app::HelpModel model;
    model.setItems(std::nullopt, sampleItems());

    // 字符、退格、`Home`/`End`、左右箭头、`Tab` 全部放行给筛选框那个标准
    // `TextField`：模型一概不接，也不许自己拼筛选串。
    QCOMPARE(handledOf(model.handleKey(Qt::Key_C)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Backspace)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Delete)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Left)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Right)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Home)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_End)), false);
    QCOMPARE(handledOf(model.handleKey(Qt::Key_Tab)), false);
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.selected(), 0);

    // 筛选串只从 `setFilter()` 进来（QML 在 `onTextEdited` 里调它）。
    model.setFilter(QStringLiteral("cap"));
    QCOMPARE(model.visibleIndices(), std::vector<int>{2});
}

void TestHelpModel::rolesExposeWhatTheDelegateNeeds()
{
    app::HelpModel model;
    model.setItems(QStringLiteral("快捷键"), sampleItems());

    const int badgesRole = roleOf(model, "badges");
    const int labelRole = roleOf(model, "label");
    const int detailRole = roleOf(model, "detail");
    // 角色名是 `rowSelected`，不是 `highlighted`：委托是标准 `ItemDelegate`，
    // 它自己就有 `highlighted`（撞名就声明不了必需属性）。
    const int rowSelectedRole = roleOf(model, "rowSelected");
    QVERIFY(badgesRole > 0);
    QVERIFY(labelRole > 0);
    QVERIFY(detailRole > 0);
    QVERIFY(rowSelectedRole > 0);

    // 行下标就是 `ListView` 的下标，几何由委托自己用锚点拼，模型不再给矩形。
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0, 0), labelRole).toString(), QStringLiteral("睡眠"));
    QCOMPARE(model.data(model.index(0, 0), detailRole).toString(), QStringLiteral("power sleep"));
    QCOMPARE(model.data(model.index(2, 0), detailRole).toString(), QString());
    QCOMPARE(model.data(model.index(0, 0), rowSelectedRole).toBool(), true);
    QCOMPARE(model.data(model.index(1, 0), rowSelectedRole).toBool(), false);
    QCOMPARE(model.data(model.index(0, 0), badgesRole).toList().size(), 3);

    // 高亮就是键盘选中项（鼠标悬停不参与：拖动滚动条时高亮会跟着指针乱跳，
    // 2026-09 已取消；鼠标**点选**改的是同一个状态，走 `setSelected`）。
    model.setSelected(2);
    QCOMPARE(model.selected(), 2);
    QCOMPARE(model.data(model.index(0, 0), rowSelectedRole).toBool(), false);
    QCOMPARE(model.data(model.index(2, 0), rowSelectedRole).toBool(), true);
    QCOMPARE(model.copyText(), QStringLiteral("CapsLock"));
    model.moveSelection(-2);
    QCOMPARE(model.data(model.index(0, 0), rowSelectedRole).toBool(), true);
    QCOMPARE(model.data(model.index(2, 0), rowSelectedRole).toBool(), false);

    // 越界/无效下标不能崩。
    QCOMPARE(model.data(QModelIndex(), labelRole).isValid(), false);
    QCOMPARE(model.data(model.index(9, 0), labelRole).isValid(), false);
}

QTEST_MAIN(TestHelpModel)
#include "tst_help_model.moc"
