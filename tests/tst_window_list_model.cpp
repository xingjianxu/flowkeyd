// 窗口切换器模型（`app::WindowListModel`）的纯逻辑测试：筛选（进程名 + 标题）、
// `可见/总数` 计数、「只剩一个窗口就直接激活」、`Enter`/`Esc`/方向键、
// 悬停即高亮、几何与角色名。
//
// **窗口枚举与真正的激活不在这里**：那是 `platform/win/window` 与
// `app::Dispatcher` 的活儿，需要真实桌面。
#include <QtTest>

#include <QAbstractItemModel>

#include "app/window_list_model.h"

using namespace flowkeyd;

namespace {

app::WindowListEntry entry(const QString &title, const QString &process)
{
    app::WindowListEntry item;
    item.title = title;
    item.process = process;
    return item;
}

std::vector<app::WindowListEntry> sampleItems()
{
    return {
        entry(QStringLiteral("π - flowkeyd"), QStringLiteral("wezterm-gui.exe")),
        entry(QStringLiteral("flowkeyd - Google Chrome"), QStringLiteral("chrome.exe")),
        entry(QStringLiteral("README.md - Visual Studio Code"), QStringLiteral("code.exe")),
        entry(QStringLiteral("flowkeyd.lua.example - Notepad"), QStringLiteral("notepad.exe")),
    };
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

} // namespace

class TestWindowListModel : public QObject
{
    Q_OBJECT

private slots:
    void itemsExposeRolesAndGeometry();
    void filterMatchesProcessAndTitle();
    void uniqueMatchAutoChoosesTheItem();
    void emptyFilterNeverAutoChooses();
    void multipleMatchesDoNotAutoChoose();
    void noMatchesShowsTheEmptyMessage();
    void activateReturnsTheItemIndex();
    void handleKeyNavigatesChoosesAndCancels();
    void hoverMovesTheHighlight();
    void resetClearsTheFilter();
    void rowsForAvailableHeightIsClamped();
};

void TestWindowListModel::itemsExposeRolesAndGeometry()
{
    app::WindowListModel model;
    model.setItems(std::optional<QString>(QStringLiteral("窗口")), sampleItems());

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.totalCount(), 4);
    QCOMPARE(model.visibleCount(), 4);
    QCOMPARE(model.title(), QStringLiteral("窗口"));
    QCOMPARE(model.countText(), QStringLiteral("4 个窗口"));
    QCOMPARE(model.cardWidth(), 560);
    QCOMPARE(model.selected(), 0);

    const int titleRole = roleOf(model, "windowTitle");
    const int processRole = roleOf(model, "windowProcess");
    const int selectedRole = roleOf(model, "rowSelected");
    QVERIFY(titleRole != -1);
    QVERIFY(processRole != -1);
    QVERIFY(selectedRole != -1);

    QCOMPARE(model.data(model.index(0), titleRole).toString(), QStringLiteral("π - flowkeyd"));
    QCOMPARE(model.data(model.index(0), processRole).toString(), QStringLiteral("wezterm-gui.exe"));
    QCOMPARE(model.data(model.index(0), selectedRole).toBool(), true);
    QCOMPARE(model.data(model.index(1), selectedRole).toBool(), false);

    // 卡片高度 = listTop + 行数 * (行高 + 空隙) + listBottom。
    QCOMPARE(model.cardHeight(),
             model.listTop() + 4 * (model.rowHeight() + model.rowSpacing()) + model.listBottom());
}

void TestWindowListModel::filterMatchesProcessAndTitle()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // 进程名（`chrome` 命中 `chrome.exe`）。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("chrome"))), QStringLiteral("choose"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selected(), 0);

    // 标题也参与匹配（`visual` 命中条目 3 的标题）。
    model.setFilter(QStringLiteral("visual"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(indexOf(model.setFilter(QStringLiteral("visual"))), -1);
    QCOMPARE(model.itemIndexForVisible(0), std::optional<int>(2));

    // 大小写无关。
    model.setFilter(QStringLiteral("CHROME"));
    QCOMPARE(model.visibleCount(), 1);
}

void TestWindowListModel::uniqueMatchAutoChoosesTheItem()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // 先筛掉一部分（还剩两条），再筛到唯一：返回的必须是**条目**下标，不是行下标。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("flowkeyd"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 3);
    const QVariantMap chosen = model.setFilter(QStringLiteral("notepad"));
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 3);
}

void TestWindowListModel::emptyFilterNeverAutoChooses()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, {entry(QStringLiteral("only"), QStringLiteral("one.exe"))});

    // 只有一条窗口也不该一打开就被切走。
    const QVariantMap decision = model.setFilter(QString());
    QCOMPARE(decisionOf(decision), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 1);

    // 打一个字把它筛到唯一才自动激活。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("o"))), QStringLiteral("choose"));
}

void TestWindowListModel::multipleMatchesDoNotAutoChoose()
{
    app::WindowListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("a"), QStringLiteral("chrome.exe")),
                    entry(QStringLiteral("b"), QStringLiteral("chrome.exe")),
                    entry(QStringLiteral("c"), QStringLiteral("code.exe"))});

    // 两条 chrome 都命中「只剩一个进程」，但窗口不是一个：不能自动切。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("chrome"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 2);

    // 补一个标题片段才唯一。
    const QVariantMap chosen = model.setFilter(QStringLiteral("b"));
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 1);
}

void TestWindowListModel::noMatchesShowsTheEmptyMessage()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());
    QCOMPARE(model.emptyMessage(), QStringLiteral("没有匹配的窗口"));

    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("zzz"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 0);
    QVERIFY(!model.hasMatches());
    QCOMPARE(model.countText(), QStringLiteral("0 / 4 个窗口"));

    // 一条窗口都没有时给的是另一句人话。
    model.setItems(std::nullopt, {});
    QCOMPARE(model.emptyMessage(), QStringLiteral("没有打开的窗口"));
    QCOMPARE(model.visibleRows(), 1);
}

void TestWindowListModel::activateReturnsTheItemIndex()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());
    model.setFilter(QStringLiteral("flowkeyd")); // 命中 wezterm / chrome / notepad 三条

    QCOMPARE(model.visibleCount(), 3);
    QCOMPARE(model.itemIndexForVisible(0), std::optional<int>(0));
    QCOMPARE(model.itemIndexForVisible(1), std::optional<int>(1));
    QCOMPARE(model.itemIndexForVisible(2), std::optional<int>(3));

    const QVariantMap chosen = model.activate(2);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 3);
    QCOMPARE(model.selected(), 2);

    // 空结果时没有可激活的。
    model.setFilter(QStringLiteral("zzz"));
    QCOMPARE(decisionOf(model.activate(0)), QStringLiteral("none"));
}

void TestWindowListModel::handleKeyNavigatesChoosesAndCancels()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // 向上夹在第一条（不回绕）。
    model.handleKey(Qt::Key_Up);
    QCOMPARE(model.selected(), 0);

    model.handleKey(Qt::Key_Down);
    QCOMPARE(model.selected(), 1);
    model.handleKey(Qt::Key_PageDown);
    QCOMPARE(model.selected(), 3);
    model.handleKey(Qt::Key_Down);
    QCOMPARE(model.selected(), 3);
    model.handleKey(Qt::Key_PageUp);
    QCOMPARE(model.selected(), 0);

    const QVariantMap chosen = model.handleKey(Qt::Key_Return);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 0);

    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Enter)), QStringLiteral("choose"));
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Escape)), QStringLiteral("cancel"));

    // 字符与编辑键放行给筛选框。
    const QVariantMap letter = model.handleKey(Qt::Key_A);
    QVERIFY(!letter.value(QStringLiteral("handled")).toBool());
}

void TestWindowListModel::hoverMovesTheHighlight()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    QSignalSpy spy(&model, &app::WindowListModel::selectedChanged);
    model.setHover(2);
    QCOMPARE(model.selected(), 2);
    QCOMPARE(spy.count(), 1);

    // 同一条不重复发信号；负数忽略（指针移出列表不改选中项）。
    model.setHover(2);
    QCOMPARE(spy.count(), 1);
    model.setHover(-1);
    QCOMPARE(model.selected(), 2);
    QCOMPARE(spy.count(), 1);
}

void TestWindowListModel::resetClearsTheFilter()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());
    model.setFilter(QStringLiteral("chrome"));
    QCOMPARE(model.visibleCount(), 1);

    model.reset();
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 4);
    QCOMPARE(model.selected(), 0);

    // 重新给一批窗口同样会复位。
    model.setFilter(QStringLiteral("chrome"));
    model.setItems(std::nullopt, sampleItems());
    QCOMPARE(model.filter(), QString());
    QCOMPARE(model.visibleCount(), 4);
}

void TestWindowListModel::rowsForAvailableHeightIsClamped()
{
    // 很小的工作区至少给一行。
    QCOMPARE(app::WindowListModel::rowsForAvailableHeight(100), 1);
    // 很大的工作区封顶在 12 行。
    QCOMPARE(app::WindowListModel::rowsForAvailableHeight(5000), 12);
}

QTEST_MAIN(TestWindowListModel)
#include "tst_window_list_model.moc"
