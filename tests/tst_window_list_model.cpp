// 窗口切换器模型（`app::WindowListModel`）的纯逻辑测试：筛选（进程名前缀，
// 标题不参与）、`可见/总数` 计数、「只剩一个窗口就直接激活」、
// 「一个进程多个窗口」时的数字选择模式、
// `Enter`/`Esc`/方向键、悬停即高亮、几何与角色名。
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
    void cardHasNoTitleRow();
    void filterMatchesProcessPrefixOnly();
    void titlesDoNotParticipateInTheFilter();
    void uniqueMatchAutoChoosesTheItem();
    void emptyFilterNeverAutoChooses();
    void multipleMatchesDoNotAutoChoose();
    void numberedKeysAppearForOneProcessWithSeveralWindows();
    void numberKeysActivateTheMatchingWindow();
    void onlyTheFirstTenWindowsGetAKey();
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
    model.setItems(std::nullopt, sampleItems());

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.totalCount(), 4);
    QCOMPARE(model.visibleCount(), 4);
    QCOMPARE(model.countText(), QStringLiteral("4 个窗口"));
    QCOMPARE(model.cardWidth(), 560);
    QCOMPARE(model.selected(), 0);
    // 卡片里没有标题行了，名字只出现在**窗口标题**里（外面用它断言）。
    QCOMPARE(model.caption(), QStringLiteral("flowkeyd 窗口 — 4 个"));

    const int titleRole = roleOf(model, "windowTitle");
    const int processRole = roleOf(model, "windowProcess");
    const int selectedRole = roleOf(model, "rowSelected");
    const int keyRole = roleOf(model, "rowKey");
    QVERIFY(titleRole != -1);
    QVERIFY(processRole != -1);
    QVERIFY(selectedRole != -1);
    QVERIFY(keyRole != -1);

    QCOMPARE(model.data(model.index(0), titleRole).toString(), QStringLiteral("π - flowkeyd"));
    QCOMPARE(model.data(model.index(0), processRole).toString(), QStringLiteral("wezterm-gui.exe"));
    QCOMPARE(model.data(model.index(0), selectedRole).toBool(), true);
    QCOMPARE(model.data(model.index(1), selectedRole).toBool(), false);
    // 还没筛选（不在数字选择模式）：没有快捷键徽标。
    QVERIFY(!model.numberedMode());
    QCOMPARE(model.data(model.index(0), keyRole).toString(), QString());

    // 卡片高度 = listTop + 行数 * (行高 + 空隙) + listBottom。
    QCOMPARE(model.cardHeight(),
             model.listTop() + 4 * (model.rowHeight() + model.rowSpacing()) + model.listBottom());
}

void TestWindowListModel::cardHasNoTitleRow()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // 卡片里只有筛选框、列表与底部提示：筛选框就是第一行，列表紧贴在它下面。
    const app::PopupRect filter = model.filterRect();
    QCOMPARE(filter.y, 12); // 卡片上内边距
    QCOMPARE(model.listTop(), filter.y + filter.height + 8);
    QCOMPARE(model.cardHeight(),
             model.listTop() + 4 * (model.rowHeight() + model.rowSpacing()) + model.listBottom());
    QCOMPARE(model.footerRect().y + model.footerRect().height + 12, model.cardHeight());

    // 原来在标题行右边的「N / M 个窗口」现在在底部提示里（QML 的 `ListView`
    // 左右与宽度都用 `filterRect`，所以列表比之前窄、与输入框对齐）。
    QVERIFY(model.footerText().startsWith(QStringLiteral("4 个窗口")));
    QCOMPARE(model.listTop(), 50);

    // `windows("切换窗口")` 给的名字只用在窗口标题上。
    model.setItems(std::optional<QString>(QStringLiteral("切换窗口")), sampleItems());
    QCOMPARE(model.caption(), QStringLiteral("切换窗口 — 4 个"));
}

void TestWindowListModel::filterMatchesProcessPrefixOnly()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // 完整进程名（`chrome` 是 `chrome.exe` 的前缀）。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("chrome"))), QStringLiteral("choose"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.selected(), 0);

    // 多个进程共享同一个前缀时都得留下。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("c"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 2);
    QCOMPARE(model.itemIndexForVisible(0), std::optional<int>(1));
    QCOMPARE(model.itemIndexForVisible(1), std::optional<int>(2));

    // 前缀而不是子串：`hrome` 不是任何进程名的开头。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("hrome"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 0);
    QVERIFY(!model.hasMatches());

    // 大小写无关；首尾空白会被 trim 掉。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("CHROME"))), QStringLiteral("choose"));
    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("  code.exe  "))),
             QStringLiteral("choose"));
    QCOMPARE(model.itemIndexForVisible(0), std::optional<int>(2));
}

void TestWindowListModel::titlesDoNotParticipateInTheFilter()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // `visual` 只在条目 2 的标题里，`flowkeyd` 在三条标题里：都不该命中。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("visual"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 0);
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("flowkeyd"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 0);

    // 进程名里的子串也不命中（前缀才行）。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("notepad.exe"))),
             QStringLiteral("choose"));
    QCOMPARE(model.itemIndexForVisible(0), std::optional<int>(3));
}

void TestWindowListModel::uniqueMatchAutoChoosesTheItem()
{
    app::WindowListModel model;
    model.setItems(std::nullopt, sampleItems());

    // 先筛掉一部分（还剩两条），再筛到唯一：返回的必须是**条目**下标，不是行下标。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("c"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 2);
    const QVariantMap chosen = model.setFilter(QStringLiteral("n"));
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

    // 标题不参与筛选，所以同一个程序的多个窗口不能再靠打字区分，
    // 只能 ↑/↓ + Enter、鼠标点选、或者直接按数字键（见下面的数字选择模式）。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("b"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 0);
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("chrome"))), QStringLiteral("none"));
    const QVariantMap chosen = model.activate(1);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 1);

    // 两条 chrome + 一条 code：前缀 `chrome` 落在同一个进程的两个窗口上，
    // 于是进入数字选择模式（不再是“没法区分”）。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("chrome"))), QStringLiteral("none"));
    QVERIFY(model.numberedMode());
}

void TestWindowListModel::numberedKeysAppearForOneProcessWithSeveralWindows()
{
    app::WindowListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("a"), QStringLiteral("chrome.exe")),
                    entry(QStringLiteral("b"), QStringLiteral("chrome.exe")),
                    entry(QStringLiteral("c"), QStringLiteral("code.exe"))});

    // 空筛选（刚打开）：不编号 —— 要求是「根据**用户的输入**」能匹配到一个进程名。
    QCOMPARE(decisionOf(model.setFilter(QString())), QStringLiteral("none"));
    QVERIFY(!model.numberedMode());

    // 前缀命中两个进程名（chrome.exe 两个 + code.exe 一个）：不编号。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("c"))), QStringLiteral("none"));
    QCOMPARE(model.visibleCount(), 3);
    QVERIFY(!model.numberedMode());
    QVERIFY(!model.footerText().contains(QStringLiteral("数字键")));

    // 命中一个进程名的两个窗口：进入数字选择模式，前 10 行依次拿到 1..9、0。
    QCOMPARE(decisionOf(model.setFilter(QStringLiteral("chrome"))), QStringLiteral("none"));
    QVERIFY(model.numberedMode());
    QCOMPARE(model.visibleCount(), 2);
    const int keyRole = roleOf(model, "rowKey");
    QCOMPARE(model.data(model.index(0), keyRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(model.index(1), keyRole).toString(), QStringLiteral("2"));

    // 底部提示换成数字键那一句；清掉筛选后回到普通提示、编号消失。
    QVERIFY(model.footerText().contains(QStringLiteral("数字键")));
    model.clearFilter();
    QVERIFY(!model.numberedMode());
    QCOMPARE(model.data(model.index(0), keyRole).toString(), QString());
    QVERIFY(!model.footerText().contains(QStringLiteral("数字键")));
}

void TestWindowListModel::numberKeysActivateTheMatchingWindow()
{
    app::WindowListModel model;
    model.setItems(std::nullopt,
                   {entry(QStringLiteral("first"), QStringLiteral("chrome.exe")),
                    entry(QStringLiteral("second"), QStringLiteral("chrome.exe")),
                    entry(QStringLiteral("third"), QStringLiteral("chrome.exe"))});
    model.setFilter(QStringLiteral("chrome"));
    QCOMPARE(model.visibleCount(), 3);
    QVERIFY(model.numberedMode());

    // `1` -> 第 1 行（条目 0）。
    QVariantMap chosen = model.handleKey(Qt::Key_1);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 0);

    // `2` -> 第 2 行（条目 1）。
    chosen = model.handleKey(Qt::Key_2);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 1);

    // 没有对应行的数字（只有 3 个窗口）被吃掉但什么都不做：
    // 不能漏给筛选框，否则「5」会把列表筛空。
    const QVariantMap extra = model.handleKey(Qt::Key_5);
    QCOMPARE(decisionOf(extra), QStringLiteral("none"));
    QVERIFY(extra.value(QStringLiteral("handled")).toBool());
    QCOMPARE(model.visibleCount(), 3);

    // 不在数字选择模式时数字键放行（用户可能要按它筛 `7zip` 这类进程名）。
    model.clearFilter();
    const QVariantMap plain = model.handleKey(Qt::Key_1);
    QVERIFY(!plain.value(QStringLiteral("handled")).toBool());
}

void TestWindowListModel::onlyTheFirstTenWindowsGetAKey()
{
    std::vector<app::WindowListEntry> items;
    for (int index = 0; index < 12; ++index) {
        items.push_back(entry(QStringLiteral("window %1").arg(index),
                              QStringLiteral("chrome.exe")));
    }
    app::WindowListModel model;
    model.setItems(std::nullopt, items);
    model.setFilter(QStringLiteral("chrome"));
    QVERIFY(model.numberedMode());
    QCOMPARE(model.visibleCount(), 12);

    const int keyRole = roleOf(model, "rowKey");
    QCOMPARE(model.data(model.index(0), keyRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(model.index(8), keyRole).toString(), QStringLiteral("9"));
    QCOMPARE(model.data(model.index(9), keyRole).toString(), QStringLiteral("0"));
    // 第 11、12 个窗口不分配按键。
    QCOMPARE(model.data(model.index(10), keyRole).toString(), QString());
    QCOMPARE(model.data(model.index(11), keyRole).toString(), QString());

    // `0` 是第 10 个窗口（条目 9）。
    const QVariantMap tenth = model.handleKey(Qt::Key_0);
    QCOMPARE(decisionOf(tenth), QStringLiteral("choose"));
    QCOMPARE(indexOf(tenth), 9);
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
    model.setFilter(QStringLiteral("c")); // 命中 chrome.exe 与 code.exe 两条

    QCOMPARE(model.visibleCount(), 2);
    QCOMPARE(model.itemIndexForVisible(0), std::optional<int>(1));
    QCOMPARE(model.itemIndexForVisible(1), std::optional<int>(2));

    const QVariantMap chosen = model.activate(1);
    QCOMPARE(decisionOf(chosen), QStringLiteral("choose"));
    QCOMPARE(indexOf(chosen), 2);
    QCOMPARE(model.selected(), 1);

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
