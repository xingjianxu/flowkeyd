// 程序启动器模型（`app::AppListModel`）的纯逻辑测试：网格几何、「图标 + 名字」
// 角色、按名字 / 拼音 / 首字母子串筛选、键盘选中项（方向键 / Home / End /
// PgUp / PgDn）、`Enter`/`Esc` 的语义，以及「筛选之后 `Enter` 启动的仍然是刚选
// 中的那一个」。
//
// **开始菜单的扫描与图标都不在这里**：前者要真机（`platform/win/apps`，见
// `tst_interactive`），后者是 `app::AppIconProvider` 的异步活儿（由对话框那边
// 的验收脚本从外部观察）。
#include <QtTest>

#include <QAbstractItemModel>

#include "app/app_list_model.h"

using namespace flowkeyd;

namespace {

app::AppListEntry entry(const QString &name)
{
    app::AppListEntry item;
    item.name = name;
    item.iconSource = QStringLiteral("image://flowkeyd-app/") + name;
    return item;
}

std::vector<app::AppListEntry> sampleItems(int count)
{
    std::vector<app::AppListEntry> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        // 名字带序号：排序不参与，只看下标。
        items.push_back(entry(QStringLiteral("app%1").arg(i + 1, 2, 10, QLatin1Char('0'))));
    }
    return items;
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

class TestAppListModel : public QObject
{
    Q_OBJECT

private slots:
    void itemsExposeRolesAndGeometry();
    void gridGeometryGrowsWithTheRowCount();
    void filterMatchesNameSubstringOnly();
    void filterMatchesPinyinAndInitials();
    void filterResetsTheSelectionToTheFirstTile();
    void noMatchesShowsTheEmptyMessage();
    void activateReturnsTheItemIndex();
    void activateAfterFilteringReturnsTheOriginalItem();
    void handleKeyNavigatesTheGrid();
    void handleKeyChoosesAndCancels();
    void unhandledKeysArePassedThrough();
    void selectionStopsAtTheEdges();
    void hoverMovesTheHighlight();
    void emptyListIsHandled();
    void setFilterNeverAutoLaunches();
    void contextMenuDecidesWhetherToCloseTheCard();
    void qmlEntryPointsAreInvokable();
    void resetClearsTheFilter();
    void rowsForAvailableHeightIsClamped();
};

void TestAppListModel::itemsExposeRolesAndGeometry()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(7));

    QCOMPARE(model.rowCount(), 7);
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
    QCOMPARE(model.selected(), 0);

    const int nameRole = roleOf(model, "appName");
    const int iconRole = roleOf(model, "appIcon");
    const int selectedRole = roleOf(model, "rowSelected");
    QVERIFY(nameRole > 0 && iconRole > 0 && selectedRole > 0);
    QCOMPARE(model.data(model.index(2), nameRole).toString(), QStringLiteral("app03"));
    QCOMPARE(model.data(model.index(2), iconRole).toString(),
             QStringLiteral("image://flowkeyd-app/app03"));
    QVERIFY(model.data(model.index(0), selectedRole).toBool());
    QVERIFY(!model.data(model.index(1), selectedRole).toBool());
}

void TestAppListModel::gridGeometryGrowsWithTheRowCount()
{
    app::AppListModel model;
    model.setMaxRows(6);

    // 并行数不满一行时也要占一行。
    model.setItems(std::nullopt, sampleItems(1));
    const int oneRow = model.cardHeight();
    QCOMPARE(model.visibleRows(), 1);

    // 正好一格列数（6）仍然是一行。
    model.setItems(std::nullopt, sampleItems(6));
    QCOMPARE(model.visibleRows(), 1);
    QCOMPARE(model.cardHeight(), oneRow);

    // 7 个 = 两行。
    model.setItems(std::nullopt, sampleItems(7));
    QCOMPARE(model.visibleRows(), 2);
    QCOMPARE(model.cardHeight(), oneRow + 88);

    // 行数上限管着卡片高度（超过就靠滚动条）；一屏 6 行 × 88 + 上下留白 = 622。
    model.setItems(std::nullopt, sampleItems(40));
    QCOMPARE(model.visibleRows(), 6);
    QCOMPARE(model.cardHeight(), oneRow + 5 * 88);
    QCOMPARE(model.cardHeight(), 622);

    // 网格区顶边 / 底边与卡片高度自洽。
    QCOMPARE(model.listTop(), 12 + 30 + 8);
    QCOMPARE(model.cardHeight(), model.listTop() + 6 * 88 + model.listBottom());
}

void TestAppListModel::filterMatchesNameSubstringOnly()
{
    app::AppListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("Visual Studio Code")), entry(QStringLiteral("Calibre")),
                    entry(QStringLiteral("Windows 终端"))});

    model.setFilter(QStringLiteral("studio"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 0);

    // 子串，不是前缀。
    model.setFilter(QStringLiteral("ode"));
    QCOMPARE(model.visibleCount(), 1);

    model.setFilter(QStringLiteral("CAL"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 1);

    model.setFilter(QStringLiteral("终端"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 2);

    // 首尾空白先去掉。
    model.setFilter(QStringLiteral("  calibre  "));
    QCOMPARE(model.visibleCount(), 1);
}

void TestAppListModel::filterMatchesPinyinAndInitials()
{
    app::AppListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("记事本")), entry(QStringLiteral("微信")),
                    entry(QStringLiteral("Visual Studio Code"))});

    // 汉字全拼。
    model.setFilter(QStringLiteral("jishiben"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 0);

    // 汉字首字母。
    model.setFilter(QStringLiteral("jsb"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 0);

    model.setFilter(QStringLiteral("wx"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 1);

    // 拉丁名字的词首字母。
    model.setFilter(QStringLiteral("vsc"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.itemIndexForVisible(0).value(), 2);

    // 字面匹配不受影响。
    model.setFilter(QStringLiteral("记事"));
    QCOMPARE(model.visibleCount(), 1);
    model.setFilter(QStringLiteral("studio"));
    QCOMPARE(model.visibleCount(), 1);

    // 拼音不是模糊匹配：`jb` 跨了两个音节，不是 `jsb` 的子串。
    model.setFilter(QStringLiteral("jb"));
    QCOMPARE(model.visibleCount(), 0);
}

void TestAppListModel::filterResetsTheSelectionToTheFirstTile()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(20));
    model.moveSelection(7);
    QCOMPARE(model.selected(), 7);

    model.setFilter(QStringLiteral("app1"));
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.visibleCount(), 10); // app10..app19
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
    QCOMPARE(model.selected(), 0);
    // 一条都没有时给的决定是「什么都不做」。
    QCOMPARE(decisionOf(model.activate(0)), QStringLiteral("none"));
}

void TestAppListModel::activateReturnsTheItemIndex()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(4));

    const QVariantMap decision = model.activate(2);
    QCOMPARE(decisionOf(decision), QStringLiteral("choose"));
    QCOMPARE(indexOf(decision), 2);
    // 点第 3 格也把高亮挪过去。
    QCOMPARE(model.selected(), 2);

    // 越界夹到最后一个。
    QCOMPARE(indexOf(model.activate(99)), 3);
}

void TestAppListModel::activateAfterFilteringReturnsTheOriginalItem()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(20));
    model.setFilter(QStringLiteral("app1"));
    // 可见行 0 其实是原始条目 9（app10）。
    QCOMPARE(model.itemIndexForVisible(0).value(), 9);
    QCOMPARE(indexOf(model.activate(0)), 9);
    QCOMPARE(indexOf(model.activate(9)), 18);
}

void TestAppListModel::handleKeyNavigatesTheGrid()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(20));

    model.handleKey(Qt::Key_Right);
    QCOMPARE(model.selected(), 1);
    model.handleKey(Qt::Key_Left);
    QCOMPARE(model.selected(), 0);
    // 上下走一整行（6 列）。
    model.handleKey(Qt::Key_Down);
    QCOMPARE(model.selected(), 6);
    model.handleKey(Qt::Key_Down);
    QCOMPARE(model.selected(), 12);
    model.handleKey(Qt::Key_Up);
    QCOMPARE(model.selected(), 6);

    model.handleKey(Qt::Key_End);
    QCOMPARE(model.selected(), 19);
    model.handleKey(Qt::Key_Home);
    QCOMPARE(model.selected(), 0);

    // PgUp/PgDn 按卡片当前的行数走（默认最多 6 行 → 6 × 6 = 36，被夹到末尾）。
    model.handleKey(Qt::Key_PageDown);
    QCOMPARE(model.selected(), 19);
    model.handleKey(Qt::Key_PageUp);
    QCOMPARE(model.selected(), 0);
}

void TestAppListModel::handleKeyChoosesAndCancels()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(4));
    model.setHover(3);

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
    QCOMPARE(model.selected(), 0);
}

void TestAppListModel::selectionStopsAtTheEdges()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(3));

    model.moveSelection(-1);
    QCOMPARE(model.selected(), 0);
    model.moveSelection(100);
    QCOMPARE(model.selected(), 2);
    model.moveSelection(1);
    QCOMPARE(model.selected(), 2);
    // 空列表也不越界。
    model.setFilter(QStringLiteral("nothing"));
    model.moveSelection(1);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.visibleRows(), 1);
}

void TestAppListModel::hoverMovesTheHighlight()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(10));

    model.setHover(4);
    QCOMPARE(model.selected(), 4);
    // 负数 / 越界忽略（不是夹到边界）。
    model.setHover(-1);
    QCOMPARE(model.selected(), 4);
    model.setHover(999);
    QCOMPARE(model.selected(), 9);
}

void TestAppListModel::emptyListIsHandled()
{
    app::AppListModel model;
    model.setItems(std::nullopt, {});
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.visibleRows(), 1);
    QCOMPARE(model.selected(), 0);
    QVERIFY(!model.hasMatches());
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Return)), QStringLiteral("none"));
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Down)), QStringLiteral("none"));
    QCOMPARE(model.caption(), QStringLiteral("flowkeyd 程序 — 0 个"));
}

void TestAppListModel::setFilterNeverAutoLaunches()
{
    app::AppListModel model;
    model.setItems(std::nullopt, {entry(QStringLiteral("Chrome")), entry(QStringLiteral("Code"))});

    // 筛到只剩一个也**不**自动启动（那是「切窗口」的语义；这里会真的拉起进程）。
    const QVariantMap decision = model.setFilter(QStringLiteral("Chr"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(decisionOf(decision), QStringLiteral("none"));
    QCOMPARE(indexOf(decision), -1);
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

void TestAppListModel::qmlEntryPointsAreInvokable()
{
    // QML 只认 `Q_INVOKABLE`（或者 slot）：漏了会抛 `TypeError`，而且**处理器里
    // 后面的语句会被静默跳过**（AGENTS.md 第 10 节，表现得很像“处理器没跑”）。
    // 右键菜单那条新路就是靠 `afterContextMenu()` 接的，所以这里把它连同相邻的
    // 几个入口一起断言一下形状。
    app::AppListModel model;
    const QMetaObject *meta = model.metaObject();
    for (const char *signature : {"setFilter(QString)", "activate(int)", "handleKey(int)",
                                  "setHover(int)", "moveSelection(int)",
                                  "afterContextMenu(bool)"}) {
        QVERIFY2(meta->indexOfMethod(signature) >= 0, signature);
    }
}

void TestAppListModel::resetClearsTheFilter()
{
    app::AppListModel model;
    model.setItems(std::nullopt, sampleItems(10));
    model.setFilter(QStringLiteral("app0"));
    QCOMPARE(model.visibleCount(), 9);

    model.reset();
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 10);
    QCOMPARE(model.selected(), 0);
    QCOMPARE(model.itemIndexForVisible(0).value(), 0);
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
    QCOMPARE(model.visibleRows(), 2);
}

QTEST_MAIN(TestAppListModel)
#include "tst_app_list_model.moc"
