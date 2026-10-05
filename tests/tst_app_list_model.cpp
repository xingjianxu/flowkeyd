// 程序启动器模型（`app::AppListModel`）的纯逻辑测试。
//
// 覆盖：行式视图的三个形态（概览 = 已固定 + 最近使用 + 「全部程序」按钮、
// 筛选 = 扁平网格、全部 = 按首字母分组的一行一个）、角色、拼音 / 首字母筛选、
// 键盘选中项（方向键 / Home / End / PgUp / PgDn，跳过表头、到边界夹住）、
// `Space` 固定、`Enter`/`Esc` 的语义、「最近使用」的记账、筛选之后的**数字快速
// 启动键**（0 起、只给前 10 条），以及卡片高度。
//
// **开始菜单的扫描与图标都不在这里**：前者要真机（`platform/win/apps`，见
// `tst_interactive`），后者是 `app::AppIconProvider` 的异步活儿（由对话框那边
// 的验收脚本从外部观察）。状态文件（`launcher.json`）的读写也不在这里
// （`core/launcher_state` 有自己的测试，落盘在 `app::PopupHost`）。
#include <QtTest>

#include <QAbstractItemModel>
#include <QSignalSpy>
#include <QVariantList>
#include <QVariantMap>

#include "app/app_list_model.h"

using namespace flowkeyd;

namespace {

app::AppListEntry entry(const QString &name, const QString &key)
{
    app::AppListEntry item;
    item.name = name;
    item.iconSource = QStringLiteral("image://flowkeyd-app/") + key;
    item.key = key;
    return item;
}

/// `count` 个名字带序号、**按序号排好序**（`app01`…）的程序：`m_items` 的下标
/// 因此与显示顺序一致，断言里不用再换算。
std::vector<app::AppListEntry> sampleItems(int count)
{
    std::vector<app::AppListEntry> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const QString number = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        items.push_back(entry(QStringLiteral("app%1").arg(number),
                              QStringLiteral("k%1").arg(number)));
    }
    return items;
}

QString keyFor(int index)
{
    return QStringLiteral("k%1").arg(index + 1, 2, 10, QLatin1Char('0'));
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

QString rowKind(const app::AppListModel &model, int row)
{
    return model.data(model.index(row), roleOf(model, "rowKind")).toString();
}

QString rowTitle(const app::AppListModel &model, int row)
{
    return model.data(model.index(row), roleOf(model, "rowTitle")).toString();
}

QVariantList rowItems(const app::AppListModel &model, int row)
{
    return model.data(model.index(row), roleOf(model, "rowItems")).toList();
}

int rowSelectedColumn(const app::AppListModel &model, int row)
{
    return model.data(model.index(row), roleOf(model, "rowSelectedColumn")).toInt();
}

bool rowSelected(const app::AppListModel &model, int row)
{
    return model.data(model.index(row), roleOf(model, "rowSelected")).toBool();
}

int rowHeight(const app::AppListModel &model, int row)
{
    return model.data(model.index(row), roleOf(model, "rowHeight")).toInt();
}

/// 某一行的每一项的条目下标。
std::vector<int> itemIndices(const app::AppListModel &model, int row)
{
    std::vector<int> indices;
    const QVariantList items = rowItems(model, row);
    indices.reserve(static_cast<std::size_t>(items.size()));
    for (const QVariant &value : items) {
        indices.push_back(value.toMap().value(QStringLiteral("index")).toInt());
    }
    return indices;
}

/// 某一格（行, 列）的数字快速启动键（不在那种模式时是空串）。
QString itemKey(const app::AppListModel &model, int row, int column)
{
    const QVariantList items = rowItems(model, row);
    if (column < 0 || column >= items.size()) {
        return QString();
    }
    return items.at(column).toMap().value(QStringLiteral("key")).toString();
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

QStringList kinds(const app::AppListModel &model)
{
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row) {
        out.append(rowKind(model, row));
    }
    return out;
}

} // namespace

class TestAppListModel : public QObject
{
    Q_OBJECT

private slots:
    void rowsExposeRolesAndGeometry();
    void firstRunShowsEveryProgramAsAGrid();
    void pinnedAndRecentBecomeSectionsWithAnAllButton();
    void recentIsCappedAndExcludesPinned();
    void filterFlattensToAGrid();
    void filterMatchesNameSubstringOnly();
    void filterMatchesPinyinAndInitials();
    void setFilterNeverAutoLaunches();
    void noMatchesShowsTheEmptyMessage();
    void activateItemReturnsTheItemIndex();
    void spaceTogglesThePinOnlyWhenNotFiltering();
    void togglePinMovesTheItemIntoThePinnedSection();
    void noteLaunchedPutsTheItemFirstAndEmitsStateEdited();
    void stateIsReplacedWholesaleAndManagedBySelection();
    void showAllGroupsByLetterAndEscapeReturns();
    void keyboardNavigationSkipsHeadersAndClamps();
    void hoverMovesTheHighlight();
    void handleKeyChoosesAndCancels();
    void unhandledKeysArePassedThrough();
    void numberedKeysFollowTheFilteredGrid();
    void onlyTheFirstTenMatchesGetAKey();
    void digitKeysLaunchTheNumberedProgram();
    void digitsGoToTheFilterWhenNothingIsFiltered();
    void resetClearsFilterAndAllMode();
    void contextMenuDecidesWhetherToCloseTheCard();
    void itemIndexForVisibleWalksTheRows();
    void qmlEntryPointsAreInvokable();
    void rowsForAvailableHeightIsClamped();
};

void TestAppListModel::rowsExposeRolesAndGeometry()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(7));

    QCOMPARE(model.totalCount(), 7);
    QCOMPARE(model.visibleCount(), 7);
    QCOMPARE(model.columns(), 6);
    QCOMPARE(model.cellWidth(), 126);
    QCOMPARE(model.cellHeight(), 88);
    QCOMPARE(model.iconSize(), 40);
    // 6 列 × 126 + 两边的内边距(12) 与缩进(10) = 800。
    QCOMPARE(model.cardWidth(), 6 * 126 + 2 * 12 + 2 * 10);
    // 标题里带着条数：外面（验收脚本）靠它读「现在列了几个」。
    QCOMPARE(model.caption(), QStringLiteral("flowkeyd 程序 — 7 个"));
    QCOMPARE(model.allButtonText(), QStringLiteral("全部程序（7）"));

    // 7 个 = 两行网格。
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("grid"), QStringLiteral("grid")}));
    QCOMPARE(rowItems(model, 0).size(), qsizetype(6));
    QCOMPARE(rowItems(model, 1).size(), qsizetype(1));
    QCOMPARE(rowHeight(model, 0), 88);
    QCOMPARE(rowItems(model, 0).at(2).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("app03"));
    QCOMPARE(rowItems(model, 0).at(2).toMap().value(QStringLiteral("icon")).toString(),
             QStringLiteral("image://flowkeyd-app/k03"));

    // 第一行第一格选中。
    QCOMPARE(model.selectedRow(), 0);
    QCOMPARE(model.selectedColumn(), 0);
    QVERIFY(rowSelected(model, 0));
    QVERIFY(!rowSelected(model, 1));
    QCOMPARE(rowSelectedColumn(model, 0), 0);
    QCOMPARE(rowSelectedColumn(model, 1), -1);
    QVERIFY(!rowItems(model, 0).at(0).toMap().value(QStringLiteral("pinned")).toBool());

    // 卡片高度 = 列表顶 50 + 两行 176 + 列表底 44。
    QCOMPARE(model.listTop(), 12 + 30 + 8);
    QCOMPARE(model.listBottom(), 8 + 24 + 12);
    QCOMPARE(model.cardHeight(), 50 + 2 * 88 + 44);
}

void TestAppListModel::firstRunShowsEveryProgramAsAGrid()
{
    // 没有固定、也没有最近使用（第一次用）：概览直接就是全部程序的网格，
    // 没有表头、也没有那个按钮。
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(13));

    QVERIFY(!model.hasPinned());
    QVERIFY(!model.hasRecent());
    QVERIFY(!model.allMode());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("grid"), QStringLiteral("grid"),
                                        QStringLiteral("grid")}));
    QCOMPARE(model.visibleCount(), 13);
    QCOMPARE(model.cardHeight(), 50 + 3 * 88 + 44);
}

void TestAppListModel::pinnedAndRecentBecomeSectionsWithAnAllButton()
{
    app::AppListModel model;
    core::LauncherState state;
    state.pinned = {keyFor(0)};
    state.recent = {keyFor(1), keyFor(2)};
    model.setState(state);
    model.setItems(std::nullopt, sampleItems(20));

    QVERIFY(model.hasPinned());
    QVERIFY(model.hasRecent());
    QCOMPARE(model.pinnedCount(), 1);
    QCOMPARE(model.recentCount(), 2);
    QCOMPARE(model.visibleCount(), 3);

    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("button")}));
    QCOMPARE(rowTitle(model, 0), QStringLiteral("已固定"));
    QCOMPARE(rowTitle(model, 2), QStringLiteral("最近使用"));
    QCOMPARE(rowTitle(model, 4), QStringLiteral("全部程序（20）"));
    QVERIFY(itemIndices(model, 1) == std::vector<int>({0}));
    QVERIFY(itemIndices(model, 3) == std::vector<int>({1, 2}));
    QCOMPARE(rowHeight(model, 0), 26);
    QCOMPARE(rowHeight(model, 4), 40);

    // 选中项落在第一个**可选**行上（表头不能选）。
    QCOMPARE(model.selectedRow(), 1);
    QCOMPARE(model.selectedColumn(), 0);
    QCOMPARE(model.selectedItem(), 0);

    // 卡片高度 = 50 + (26 + 88 + 26 + 88 + 40) + 44。
    QCOMPARE(model.cardHeight(), 50 + 268 + 44);
}

void TestAppListModel::recentIsCappedAndExcludesPinned()
{
    app::AppListModel model;
    core::LauncherState state;
    state.pinned = {keyFor(0)};
    // 最近使用里有 20 条（第一条与固定重复）。
    state.recent = {keyFor(0)};
    for (int i = 1; i < 20; ++i) {
        state.recent.append(keyFor(i));
    }
    model.setState(state);
    model.setItems(std::nullopt, sampleItems(30));

    QCOMPARE(model.pinnedCount(), 1);
    // 最多 12 条（两行网格），而且已经固定住的那一条不再重复出现。
    QCOMPARE(model.recentCount(), 12);
    QVERIFY(itemIndices(model, 1) == std::vector<int>({0}));
    for (int index : itemIndices(model, 3)) {
        QVERIFY(index != 0);
    }
}

void TestAppListModel::filterFlattensToAGrid()
{
    app::AppListModel model;
    core::LauncherState state;
    state.pinned = {keyFor(0)};
    state.recent = {keyFor(1)};
    model.setState(state);
    model.setItems(std::nullopt, sampleItems(10));
    QVERIFY(model.rowCount() > 3);

    const QVariantMap decision = model.setFilter(QStringLiteral("app01"));
    QCOMPARE(decisionOf(decision), QStringLiteral("none"));
    QVERIFY(handledOf(decision));
    // 筛选之后只剩扁平网格：没有表头、也没有「全部程序」按钮。
    QCOMPARE(kinds(model), QStringList({QStringLiteral("grid")}));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 0);

    model.clearFilter();
    QCOMPARE(kinds(model), QStringList({QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("button")}));
}

void TestAppListModel::filterMatchesNameSubstringOnly()
{
    app::AppListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("Visual Studio Code"), QStringLiteral("a")),
                    entry(QStringLiteral("Calibre"), QStringLiteral("b")),
                    entry(QStringLiteral("Windows 终端"), QStringLiteral("c"))});

    model.setFilter(QStringLiteral("studio"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 0);

    // 子串，不是前缀。
    model.setFilter(QStringLiteral("ode"));
    QCOMPARE(model.visibleCount(), 1);

    model.setFilter(QStringLiteral("CAL"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 1);

    model.setFilter(QStringLiteral("终端"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 2);

    // 首尾空白先去掉。
    model.setFilter(QStringLiteral("  calibre  "));
    QCOMPARE(model.visibleCount(), 1);
}

void TestAppListModel::filterMatchesPinyinAndInitials()
{
    app::AppListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("记事本"), QStringLiteral("a")),
                    entry(QStringLiteral("微信"), QStringLiteral("b")),
                    entry(QStringLiteral("Visual Studio Code"), QStringLiteral("c"))});

    model.setFilter(QStringLiteral("jishiben"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 0);

    model.setFilter(QStringLiteral("jsb"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 0);

    model.setFilter(QStringLiteral("wx"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 1);

    // 拉丁名字的词首字母。
    model.setFilter(QStringLiteral("vsc"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selectedItem(), 2);

    // 字面匹配不受影响。
    model.setFilter(QStringLiteral("记事"));
    QCOMPARE(model.visibleCount(), 1);
    model.setFilter(QStringLiteral("studio"));
    QCOMPARE(model.visibleCount(), 1);

    // 拼音不是模糊匹配：`jb` 跨了两个音节，不是 `jsb` 的子串。
    model.setFilter(QStringLiteral("jb"));
    QCOMPARE(model.visibleCount(), 0);
}

void TestAppListModel::setFilterNeverAutoLaunches()
{
    app::AppListModel model;
    model.setItems(std::nullopt, {entry(QStringLiteral("Chrome"), QStringLiteral("a")),
                                  entry(QStringLiteral("Code"), QStringLiteral("b"))});

    // 筛到只剩一个也**不**自动启动（那是「切窗口」的语义；这里会真的拉起进程）。
    const QVariantMap decision = model.setFilter(QStringLiteral("Chr"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(decisionOf(decision), QStringLiteral("none"));
    QCOMPARE(indexOf(decision), -1);
}

void TestAppListModel::noMatchesShowsTheEmptyMessage()
{
    app::AppListModel model;
    QCOMPARE(model.emptyMessage(), QStringLiteral("没有找到程序"));
    model.setItems(std::nullopt, sampleItems(3));
    QCOMPARE(model.emptyMessage(), QStringLiteral("没有匹配的程序"));

    model.setFilter(QStringLiteral("nothing-matches"));
    QCOMPARE(model.visibleCount(), 0);
    QVERIFY(!model.hasMatches());
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.selectedRow(), -1);
    QCOMPARE(model.selectedItem(), -1);
    // 一条都没有时给的决定是「什么都不做」。
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Return)), QStringLiteral("none"));
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Down)), QStringLiteral("none"));
    // 卡片至少留一格的空白来画那句提示。
    QCOMPARE(model.cardHeight(), 50 + 88 + 44);
}

void TestAppListModel::activateItemReturnsTheItemIndex()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(4));

    const QVariantMap decision = model.activateItem(2);
    QCOMPARE(decisionOf(decision), QStringLiteral("choose"));
    QCOMPARE(indexOf(decision), 2);
    // 点第 3 格也把高亮挪过去。
    QCOMPARE(model.selectedItem(), 2);

    // 越界的下标什么都不做。
    QCOMPARE(decisionOf(model.activateItem(99)), QStringLiteral("none"));
    QCOMPARE(model.selectedItem(), 2);
}

void TestAppListModel::spaceTogglesThePinOnlyWhenNotFiltering()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(4));
    QSignalSpy edited(&model, &app::AppListModel::stateEdited);

    // 概览里 `Space` = 固定（第一次用是扁平网格，选中的是第 1 个）。
    const QVariantMap pinned = model.handleKey(Qt::Key_Space);
    QVERIFY(handledOf(pinned));
    QCOMPARE(decisionOf(pinned), QStringLiteral("none"));
    QCOMPARE(edited.count(), qsizetype(1));
    QCOMPARE(model.pinnedCount(), 1);
    QCOMPARE(model.selectedItem(), 0);
    // 固定之后概览多出「已固定」表头与「全部程序」按钮。
    QCOMPARE(kinds(model), QStringList({QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("button")}));

    // 再按一次 = 取消固定。
    model.handleKey(Qt::Key_Space);
    QCOMPARE(edited.count(), qsizetype(2));
    QCOMPARE(model.pinnedCount(), 0);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("grid")}));

    // **筛选框非空时 `Space` 放行**给标准 `TextInput`（否则打不出
    // `visual studio` 这种带空格的名字）。
    model.setFilter(QStringLiteral("app"));
    const QVariantMap passed = model.handleKey(Qt::Key_Space);
    QVERIFY(!handledOf(passed));
    QCOMPARE(edited.count(), qsizetype(2));
    QCOMPARE(model.pinnedCount(), 0);
}

void TestAppListModel::togglePinMovesTheItemIntoThePinnedSection()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(10));

    // 直接固定第 5 个（不论它在哪一行）。
    model.togglePinItem(4);
    QCOMPARE(model.launcherState().pinned, QStringList({keyFor(4)}));
    QCOMPARE(model.selectedItem(), 4);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("button")}));
    QVERIFY(itemIndices(model, 1) == std::vector<int>({4}));
    // 网格里那一格的 `pinned` 标记立起来了。
    QVERIFY(rowItems(model, 1).at(0).toMap().value(QStringLiteral("pinned")).toBool());

    // 再切一次就回到全部程序那一组，选中项跟着它。
    model.togglePinItem(4);
    QVERIFY(model.launcherState().pinned.isEmpty());
    QCOMPARE(model.selectedItem(), 4);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("grid"), QStringLiteral("grid")}));

    // 没有稳定键的行（预热用的假数据）固定不了。
    app::AppListModel fake;
    fake.setItems(std::nullopt, {app::AppListEntry{QStringLiteral("preload"), QString(), QString()}});
    fake.togglePinItem(0);
    QVERIFY(fake.launcherState().pinned.isEmpty());
}

void TestAppListModel::noteLaunchedPutsTheItemFirstAndEmitsStateEdited()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(10));
    QSignalSpy edited(&model, &app::AppListModel::stateEdited);

    model.noteLaunched(3);
    model.noteLaunched(7);
    model.noteLaunched(3);
    QCOMPARE(edited.count(), qsizetype(3));
    // 最近的在前，重复启动只是把它提前（不会出现两次）。
    QCOMPARE(model.launcherState().recent,
             QStringList({keyFor(3), keyFor(7)}));
    QCOMPARE(model.recentCount(), 2);
    QCOMPARE(model.selectedItem(), 3);
    // 行：0 表头 / 1 网格 / 2 「全部程序」按钮。
    QCOMPARE(kinds(model), QStringList({QStringLiteral("header"), QStringLiteral("grid"),
                                        QStringLiteral("button")}));
    QVERIFY(itemIndices(model, 1) == std::vector<int>({3, 7}));

    // 越界 / 没有键的都被忽略。
    model.noteLaunched(99);
    QCOMPARE(edited.count(), qsizetype(3));
}

void TestAppListModel::stateIsReplacedWholesaleAndManagedBySelection()
{
    app::AppListModel model;
    core::LauncherState state;
    state.pinned = {keyFor(2), keyFor(0)};
    state.recent = {keyFor(5)};
    // 与 `PopupHost::showApps()` 一样的顺序：先读状态，再换程序列表。
    model.setState(state);
    model.setItems(std::nullopt, sampleItems(10));
    QCOMPARE(model.launcherState().pinned, state.pinned);
    // 固定顺序 = 显示顺序（后固定的排在后面）。
    QVERIFY(itemIndices(model, 1) == std::vector<int>({2, 0}));
    QCOMPARE(model.selectedItem(), 2);

    // 目录里已经没有的键（卸载了）显示时跳过，但状态里留着。
    core::LauncherState stale;
    stale.pinned = {QStringLiteral("gone"), keyFor(1)};
    model.setState(stale);
    QCOMPARE(model.pinnedCount(), 1);
    QCOMPARE(model.launcherState().pinned, stale.pinned);
    QVERIFY(itemIndices(model, 1) == std::vector<int>({1}));
}

void TestAppListModel::showAllGroupsByLetterAndEscapeReturns()
{
    app::AppListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("记事本"), QStringLiteral("a")),
                    entry(QStringLiteral("Word"), QStringLiteral("b")),
                    entry(QStringLiteral("7-Zip"), QStringLiteral("c")),
                    entry(QStringLiteral("微信"), QStringLiteral("d"))});
    core::LauncherState state;
    state.pinned = {QStringLiteral("a")};
    model.setState(state);

    // 「全部程序」按钮是最后一行；`Enter` 落在它上面就打开列表。
    const QVariantMap opened = model.handleKey(Qt::Key_End);
    QVERIFY(handledOf(opened));
    QCOMPARE(model.selectedRow(), model.rowCount() - 1);
    QCOMPARE(rowKind(model, model.selectedRow()), QStringLiteral("button"));
    model.handleKey(Qt::Key_Return);
    QVERIFY(model.allMode());

    // 「← 返回」+ 按首字母分的组（数字 / 符号开头的一组在最前面）。
    QCOMPARE(kinds(model), QStringList({QStringLiteral("button"), QStringLiteral("header"),
                                        QStringLiteral("list"), QStringLiteral("header"),
                                        QStringLiteral("list"), QStringLiteral("header"),
                                        QStringLiteral("list"), QStringLiteral("list")}));
    QCOMPARE(rowTitle(model, 1), QStringLiteral("#"));
    QCOMPARE(rowTitle(model, 3), QStringLiteral("J"));
    QCOMPARE(rowTitle(model, 5), QStringLiteral("W"));
    // 组内按拼音 / 字母排：微信（weixin）在 Word 前面。
    QCOMPARE(rowItems(model, 6).at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("微信"));
    QCOMPARE(rowItems(model, 7).at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Word"));
    QCOMPARE(rowHeight(model, 2), 44);

    // 列表里 `Esc` 是「返回概览」，不是关卡片。
    const QVariantMap back = model.handleKey(Qt::Key_Escape);
    QVERIFY(handledOf(back));
    QCOMPARE(decisionOf(back), QStringLiteral("none"));
    QVERIFY(!model.allMode());

    // 再回概览之后 `Esc` 才是关卡片。
    const QVariantMap cancel = model.handleKey(Qt::Key_Escape);
    QCOMPARE(decisionOf(cancel), QStringLiteral("cancel"));

    // 「← 返回」按钮本身也能用（鼠标点它走的是 QML 那边）。
    model.showAll();
    QVERIFY(model.allMode());
    model.showOverview();
    QVERIFY(!model.allMode());
}

void TestAppListModel::keyboardNavigationSkipsHeadersAndClamps()
{
    app::AppListModel model;
    core::LauncherState state;
    state.pinned = {keyFor(0), keyFor(1)};
    state.recent = {keyFor(2)};
    model.setState(state);
    model.setItems(std::nullopt, sampleItems(10));

    // 行：0 表头 / 1 网格(2 个) / 2 表头 / 3 网格(1 个) / 4 按钮。
    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.selectedRow(), 1);
    QCOMPARE(model.selectedColumn(), 0);

    model.moveSelection(1, 0);
    QCOMPARE(model.selectedColumn(), 1);
    // 右边到头了就夹住。
    model.moveSelection(1, 0);
    QCOMPARE(model.selectedColumn(), 1);

    // 向下跳过表头（行 2）。
    model.moveSelection(0, 1);
    QCOMPARE(model.selectedRow(), 3);
    QCOMPARE(model.selectedColumn(), 0);
    // 再向下是按钮（列夹到 0）。
    model.moveSelection(0, 1);
    QCOMPARE(model.selectedRow(), 4);
    QCOMPARE(model.selectedColumn(), 0);
    QCOMPARE(model.selectedItem(), -1);
    // 到底了就不动。
    model.moveSelection(0, 1);
    QCOMPARE(model.selectedRow(), 4);
    model.moveSelection(0, 1);
    QCOMPARE(model.selectedRow(), 4);

    model.moveSelection(0, -1);
    QCOMPARE(model.selectedRow(), 3);

    model.handleKey(Qt::Key_End);
    QCOMPARE(model.selectedRow(), 4);
    model.handleKey(Qt::Key_Home);
    QCOMPARE(model.selectedRow(), 1);
    QCOMPARE(model.selectedColumn(), 0);

    // PgDn 一页（默认 6 行）走到最后一行的按钮上。
    model.handleKey(Qt::Key_PageDown);
    QCOMPARE(model.selectedRow(), 4);
    model.handleKey(Qt::Key_PageUp);
    QCOMPARE(model.selectedRow(), 1);
}

void TestAppListModel::hoverMovesTheHighlight()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(10));

    // 条目 7 在第 2 行第 2 格。
    model.hoverItem(7);
    QCOMPARE(model.selectedRow(), 1);
    QCOMPARE(model.selectedColumn(), 1);
    QCOMPARE(model.selectedItem(), 7);

    // 找不到的条目忽略（不夹到边界）。
    model.hoverItem(-1);
    QCOMPARE(model.selectedItem(), 7);
    model.hoverItem(999);
    QCOMPARE(model.selectedItem(), 7);
}

void TestAppListModel::handleKeyChoosesAndCancels()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(4));
    model.hoverItem(3);

    const QVariantMap choose = model.handleKey(Qt::Key_Return);
    QCOMPARE(decisionOf(choose), QStringLiteral("choose"));
    QCOMPARE(indexOf(choose), 3);

    const QVariantMap cancel = model.handleKey(Qt::Key_Escape);
    QCOMPARE(decisionOf(cancel), QStringLiteral("cancel"));
    QVERIFY(handledOf(cancel));
}

void TestAppListModel::unhandledKeysArePassedThrough()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(4));
    // 字符、退格、Tab 都归筛选框那个标准 `TextField`。
    for (int key : {Qt::Key_A, Qt::Key_Backspace, Qt::Key_Tab, Qt::Key_Delete}) {
        const QVariantMap decision = model.handleKey(key);
        QVERIFY(!handledOf(decision));
        QCOMPARE(decisionOf(decision), QStringLiteral("none"));
    }
    QCOMPARE(model.selectedItem(), 0);
}

void TestAppListModel::numberedKeysFollowTheFilteredGrid()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(12));

    // 概览（没有筛选）：不编号，每一格都没有号码。
    QVERIFY(!model.numberedMode());
    QCOMPARE(itemKey(model, 0, 0), QString());
    QVERIFY(!model.footerText().contains(QStringLiteral("直接启动")));

    // `app0` 命中 app01..app09 九条：第 1 条 `0`、第 2 条 `1`……第 9 条 `8`。
    model.setFilter(QStringLiteral("app0"));
    QVERIFY(model.numberedMode());
    QCOMPARE(model.visibleCount(), 9);
    QCOMPARE(itemKey(model, 0, 0), QStringLiteral("0"));
    QCOMPARE(itemKey(model, 0, 1), QStringLiteral("1"));
    QCOMPARE(itemKey(model, 1, 0), QStringLiteral("6"));
    QCOMPARE(itemKey(model, 1, 2), QStringLiteral("8"));
    QVERIFY(model.footerText().contains(QStringLiteral("直接启动")));

    // 清掉筛选：号码消失、底部提示回到普通那一句。
    model.clearFilter();
    QVERIFY(!model.numberedMode());
    QCOMPARE(itemKey(model, 0, 0), QString());
    QVERIFY(!model.footerText().contains(QStringLiteral("直接启动")));
}

void TestAppListModel::onlyTheFirstTenMatchesGetAKey()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(12));

    model.setFilter(QStringLiteral("app")); // 12 条全中
    QVERIFY(model.numberedMode());
    QCOMPARE(model.visibleCount(), 12);
    QCOMPARE(itemKey(model, 0, 0), QStringLiteral("0"));
    QCOMPARE(itemKey(model, 0, 5), QStringLiteral("5"));
    QCOMPARE(itemKey(model, 1, 0), QStringLiteral("6"));
    QCOMPARE(itemKey(model, 1, 3), QStringLiteral("9"));
    // 第 11、12 条（app11、app12）不分配号码。
    QCOMPARE(itemKey(model, 1, 4), QString());
    QCOMPARE(itemKey(model, 1, 5), QString());
}

void TestAppListModel::digitKeysLaunchTheNumberedProgram()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(12));
    model.setFilter(QStringLiteral("app")); // 前 10 条依次是 app01..app10

    // `0` = 第 1 个（条目 0），`3` = 第 4 个（条目 3）。
    QVariantMap chosen = model.handleKey(Qt::Key_0);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QVERIFY(handledOf(chosen));
    QCOMPARE(indexOf(chosen), 0);

    chosen = model.handleKey(Qt::Key_3);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 3);

    // `9` = 第 10 个（条目 9）。
    chosen = model.handleKey(Qt::Key_9);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 9);

    // 没有对应条目的号码被吃掉但什么都不做（不能漏给筛选框，否则会把列表筛空）。
    model.setFilter(QStringLiteral("app1")); // app10..app12 三条
    QCOMPARE(model.visibleCount(), 3);
    const QVariantMap extra = model.handleKey(Qt::Key_5);
    QCOMPARE(decisionOf(extra), QStringLiteral("none"));
    QVERIFY(handledOf(extra));
    QCOMPARE(model.visibleCount(), 3);
}

void TestAppListModel::digitsGoToTheFilterWhenNothingIsFiltered()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(5));

    // 没有匹配（筛空）时当然不编号，数字键要放行给筛选框；筛选框为空时同理
    // （`7-Zip` 这类名字得能用数字筛）。
    model.setFilter(QStringLiteral("zzq"));
    QCOMPARE(model.visibleCount(), 0);
    QVERIFY(!model.numberedMode());
    QVERIFY(!handledOf(model.handleKey(Qt::Key_7)));

    model.clearFilter();
    QVERIFY(!model.numberedMode());
    QVERIFY(!handledOf(model.handleKey(Qt::Key_7)));
}

void TestAppListModel::resetClearsFilterAndAllMode()
{
    app::AppListModel model;
    core::LauncherState state;
    state.pinned = {keyFor(0)};
    model.setState(state);
    model.setItems(std::nullopt, sampleItems(10));

    model.setFilter(QStringLiteral("app"));
    QCOMPARE(model.visibleCount(), 10);
    model.showAll();
    QVERIFY(model.allMode());

    model.reset();
    QCOMPARE(model.filter(), QString());
    QVERIFY(!model.allMode());
    QCOMPARE(model.visibleCount(), 1); // 只有那一条固定的
    QCOMPARE(model.selectedItem(), 0);
    // 固定本身不会被 `reset()` 清掉（那是持久状态，只有 `Space` 才动它）。
    QCOMPARE(model.pinnedCount(), 1);
}

void TestAppListModel::contextMenuDecidesWhetherToCloseTheCard()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(3));

    // 取消（`Esc` / 点了菜单外面）：卡片留着，用户接着选下一格。
    const QVariantMap kept = model.afterContextMenu(false);
    QCOMPARE(decisionOf(kept), QStringLiteral("none"));
    QVERIFY(handledOf(kept));
    QCOMPARE(indexOf(kept), -1);

    // 真的选了某条命令：收卡片（`cancel` 在 QML 里就是 `host.appDismiss()`）。
    // 命令本身由 shell 执行，模型不需要知道是哪一条。
    const QVariantMap closed = model.afterContextMenu(true);
    QCOMPARE(decisionOf(closed), QStringLiteral("cancel"));
    QVERIFY(handledOf(closed));
    QCOMPARE(indexOf(closed), -1);
}

void TestAppListModel::itemIndexForVisibleWalksTheRows()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(10));
    QCOMPARE(model.itemIndexForVisible(0).value(), 0);
    QCOMPARE(model.itemIndexForVisible(9).value(), 9);
    QVERIFY(!model.itemIndexForVisible(10).has_value());
    QVERIFY(!model.itemIndexForVisible(-1).has_value());

    // 有分区时：表头与「全部程序」按钮不占号。
    core::LauncherState state;
    state.pinned = {keyFor(5)};
    state.recent = {keyFor(7), keyFor(2)};
    model.setState(state);
    QCOMPARE(model.itemIndexForVisible(0).value(), 5);
    QCOMPARE(model.itemIndexForVisible(1).value(), 7);
    QCOMPARE(model.itemIndexForVisible(2).value(), 2);
    QVERIFY(!model.itemIndexForVisible(3).has_value());

    // 筛选之后的顺序与显示顺序一致。
    model.setFilter(QStringLiteral("app0"));
    QCOMPARE(model.itemIndexForVisible(0).value(), 0);
    QVERIFY(!model.itemIndexForVisible(model.visibleCount()).has_value());
}

void TestAppListModel::qmlEntryPointsAreInvokable()
{
    // QML 只认 `Q_INVOKABLE`（或者 slot）：漏了会抛 `TypeError`，而且**处理器里
    // 后面的语句会被静默跳过**（AGENTS.md 第 10 节，表现得很像“处理器没跑”）。
    app::AppListModel model;
    const QMetaObject *meta = model.metaObject();
    for (const char *signature : {"setFilter(QString)", "clearFilter()", "handleKey(int)",
                                  "activateItem(int)", "hoverItem(int)", "moveSelection(int,int)",
                                  "togglePinItem(int)", "showAll()", "showOverview()",
                                  "afterContextMenu(bool)"}) {
        QVERIFY2(meta->indexOfMethod(signature) >= 0, signature);
    }
}

void TestAppListModel::rowsForAvailableHeightIsClamped()
{
    // 很矮的屏幕也至少给一行。
    QCOMPARE(app::AppListModel::rowsForAvailableHeight(120), 1);
    // 很高的屏幕也不会超过上限（6 行）。
    QCOMPARE(app::AppListModel::rowsForAvailableHeight(4000), 6);
    // 6 行 × 88 + 上下留白（50 + 44）+ 屏幕边缘的 48 = 670，比 670 更矮的屏幕就收行。
    QCOMPARE(app::AppListModel::rowsForAvailableHeight(670), 6);
    QCOMPARE(app::AppListModel::rowsForAvailableHeight(669), 5);

    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(40));
    model.setMaxRows(2);
    QCOMPARE(model.maxRows(), 2);
    // 内容比上限高：卡片停在 2 行（滚动条负责其余的）。
    QCOMPARE(model.rowCount(), 7);
    QCOMPARE(model.cardHeight(), 50 + 2 * 88 + 44);
}

QTEST_MAIN(TestAppListModel)
#include "tst_app_list_model.moc"
