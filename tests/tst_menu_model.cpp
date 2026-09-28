// `menu` 选单模型的纯逻辑单测：卡片几何、高亮、单字符选中、`Esc`/`Enter`。
//
// 不碰 QML、不碰任何 Win32：窗口与前台锁那部分在 `app::PopupHost` 里。
// **行几何与命中测试不在这里测**——它们已经不是模型的事了（列表是标准的
// QML `ListView` + `ItemDelegate`，见 AGENTS.md 第 10 节），只剩卡片外框的
// 那几个数字与「列表从卡片顶部的哪里开始」。
#include <QtTest>

#include <QAbstractItemModel>

#include "app/menu_model.h"

using namespace flowkeyd;

namespace {

app::MenuEntry entry(const QString &label,
                     std::optional<QChar> key = std::nullopt,
                     std::optional<QString> hint = std::nullopt)
{
    app::MenuEntry item;
    item.label = label;
    item.key = key;
    item.hint = hint;
    return item;
}

std::vector<app::MenuEntry> powerItems()
{
    return {
        entry(QStringLiteral("睡眠"), QChar(u's'), QStringLiteral("Sleep")),
        entry(QStringLiteral("关机"), QChar(u'p'), QStringLiteral("Shut down")),
        entry(QStringLiteral("重启"), QChar(u'r'), QStringLiteral("Restart")),
        entry(QStringLiteral("取消")),
    };
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

class TestMenuModel : public QObject
{
    Q_OBJECT

private slots:
    void cardSizeFollowsTheItemCountAndTitle();
    void listMetricsMatchTheCardGeometry();
    void highlightWrapsAroundAtBothEnds();
    void acceptReturnsTheHighlightedItem();
    void emptyMenuHasNoHighlightAndIgnoresArrows();
    void characterSelectionIsCaseInsensitive();
    void handleKeyMapsEscapeEnterAndArrows();
    void handleKeyIgnoresUnknownKeysAndTheMenuMaskInjection();
    void hoverOverridesTheKeyboardHighlight();
    void resetClearsTheHighlight();
    void popupPlacementStaysOnScreen();
    void rolesExposeWhatTheDelegateNeeds();
};

void TestMenuModel::cardSizeFollowsTheItemCountAndTitle()
{
    app::MenuModel titled;
    titled.setItems(QStringLiteral("电源"), powerItems());
    QCOMPARE(titled.hasTitle(), true);
    QCOMPARE(titled.title(), QStringLiteral("电源"));
    QCOMPARE(titled.count(), 4);
    QCOMPARE(titled.cardWidth(), 300);
    // 10(内边距) + 30(标题) + 4*40(条目) + 2 + 22(底部提示) + 10
    QCOMPARE(titled.cardHeight(), 234);

    app::MenuModel untitled;
    untitled.setItems(std::nullopt, powerItems());
    QCOMPARE(untitled.hasTitle(), false);
    QCOMPARE(untitled.cardHeight(), 204);

    app::MenuModel empty;
    empty.setItems(std::nullopt, {});
    QCOMPARE(empty.count(), 0);
    QCOMPARE(empty.cardHeight(), 10 + 2 + 22 + 10);
}

void TestMenuModel::listMetricsMatchTheCardGeometry()
{
    app::MenuModel model;
    model.setItems(QStringLiteral("电源"), powerItems());

    // 列表从表头之下开始，一行占「行高 + 行距」一格；这几个常量让 QML 的
    // `ListView` 与卡片几何对得上（`cardHeight` 就是用同一组数字算出来的）。
    QCOMPARE(model.listTop(), 40);   // 内边距 10 + 标题 30
    QCOMPARE(model.rowHeight(), 38);
    QCOMPARE(model.rowSpacing(), 2);
    QCOMPARE(model.rowInset(), 8);
    QCOMPARE(model.badgeSize(), 22);

    checkRect(model.titleRect(), 18, 10, 264, 30);
    checkRect(model.footerRect(), 18, 202, 264, 22);

    // 最后一行必须在底部提示之上。
    const int pitch = model.rowHeight() + model.rowSpacing();
    const int lastRowBottom = model.listTop() + model.count() * pitch - model.rowSpacing();
    QVERIFY(lastRowBottom <= model.footerRect().y);
    // 卡片高度 = 表头 + 列表 + 列表与底部提示之间的空隙 + 底部提示 + 内边距。
    QCOMPARE(model.cardHeight(), model.listTop() + model.count() * pitch + 2 + 22 + 10);
    QVERIFY(model.footerText().contains(QStringLiteral("Esc")));

    // 没写 `title` 时表头整块高度是 0，列表直接从内边距之下开始。
    app::MenuModel untitled;
    untitled.setItems(std::nullopt, powerItems());
    QCOMPARE(untitled.listTop(), 10);
    QCOMPARE(untitled.cardHeight(), 204);
}

void TestMenuModel::highlightWrapsAroundAtBothEnds()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());

    QCOMPARE(model.highlight(), 0);
    // 选单与帮助窗口不同：到边界是回绕，不是夹住。
    model.moveHighlight(-1);
    QCOMPARE(model.highlight(), 3);
    model.moveHighlight(1);
    QCOMPARE(model.highlight(), 0);
    model.moveHighlight(1);
    QCOMPARE(model.highlight(), 1);
    model.moveHighlight(0);
    QCOMPARE(model.highlight(), 1);
}

void TestMenuModel::acceptReturnsTheHighlightedItem()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());
    QCOMPARE(model.accept(), 0);
    model.moveHighlight(1);
    QCOMPARE(model.accept(), 1);
}

void TestMenuModel::emptyMenuHasNoHighlightAndIgnoresArrows()
{
    app::MenuModel model;
    model.setItems(std::nullopt, {});
    QCOMPARE(model.accept(), -1);
    model.moveHighlight(1);
    QCOMPARE(model.highlight(), 0);
    QCOMPARE(model.accept(), -1);
    QCOMPARE(indexOf(model.handleKey(Qt::Key_Return, QString())), -1);
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Return, QString())), QStringLiteral("none"));
}

void TestMenuModel::characterSelectionIsCaseInsensitive()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());
    QCOMPARE(model.indexForChar(QStringLiteral("p")), 1);
    QCOMPARE(model.indexForChar(QStringLiteral("P")), 1);
    QCOMPARE(model.indexForChar(QStringLiteral("x")), -1);
    // 多字符的文本不是「单字符选中」。
    QCOMPARE(model.indexForChar(QStringLiteral("pp")), -1);
    QCOMPARE(model.indexForChar(QString()), -1);
}

void TestMenuModel::handleKeyMapsEscapeEnterAndArrows()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());

    // `Esc` 只关窗，什么也不选。
    const QVariantMap escape = model.handleKey(Qt::Key_Escape, QString());
    QCOMPARE(decisionOf(escape), QStringLiteral("cancel"));
    QCOMPARE(handledOf(escape), true);

    // `Enter` 选中的是当前高亮那一条。
    model.moveHighlight(1);
    const QVariantMap enter = model.handleKey(Qt::Key_Return, QString());
    QCOMPARE(decisionOf(enter), QStringLiteral("choose"));
    QCOMPARE(indexOf(enter), 1);
    // 小键盘的 `Enter` 与主键盘的 `Return` 走同一条路。
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Enter, QString())), QStringLiteral("choose"));

    // 方向键只是移动高亮。
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Down, QString())), QStringLiteral("none"));
    QCOMPARE(model.highlight(), 2);
    QCOMPARE(decisionOf(model.handleKey(Qt::Key_Up, QString())), QStringLiteral("none"));
    QCOMPARE(model.highlight(), 1);

    // 字符键直接选中（并报告它的下标）。
    const QVariantMap key = model.handleKey(Qt::Key_R, QStringLiteral("r"));
    QCOMPARE(decisionOf(key), QStringLiteral("choose"));
    QCOMPARE(indexOf(key), 2);
}

void TestMenuModel::handleKeyIgnoresUnknownKeysAndTheMenuMaskInjection()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());

    // 没绑定的字符：吃掉，但不选任何东西（否则它就会漏给下面的窗口）。
    const QVariantMap unknown = model.handleKey(Qt::Key_X, QStringLiteral("x"));
    QCOMPARE(decisionOf(unknown), QStringLiteral("none"));
    QCOMPARE(handledOf(unknown), true);

    // 引擎吞掉 `Win+…` 和弦时会注入 `VK_UNASSIGNED`(0xE8) 来遮断外壳，
    // 而选单窗口正拿着焦点，于是它会送到这里。它没有文本，所以既不能选中
    // 任何条目，也不该被当成“按了一个没绑定的字符”。
    const QVariantMap mask = model.handleKey(0xE8, QString());
    QCOMPARE(decisionOf(mask), QStringLiteral("none"));
    QCOMPARE(handledOf(mask), false);

    // 真正的功能键也一样：不处理，交给系统。
    QCOMPARE(handledOf(model.handleKey(Qt::Key_F5, QString())), false);
}

void TestMenuModel::hoverOverridesTheKeyboardHighlight()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());
    model.moveHighlight(1);
    QCOMPARE(model.accept(), 1);

    model.setHover(3);
    QCOMPARE(model.hover(), 3);
    QCOMPARE(model.accept(), 3);
    // `Enter` 选的是鼠标悬停那一条。
    QCOMPARE(indexOf(model.handleKey(Qt::Key_Return, QString())), 3);

    // 悬停到一个不存在的行就是“不在任何条目上”。
    model.setHover(99);
    QCOMPARE(model.hover(), -1);
    QCOMPARE(model.accept(), 1);

    // 方向键会清掉悬停，键盘高亮重新生效。
    model.setHover(2);
    model.moveHighlight(1);
    QCOMPARE(model.hover(), -1);
    QCOMPARE(model.accept(), 2);
}

void TestMenuModel::resetClearsTheHighlight()
{
    app::MenuModel model;
    model.setItems(std::nullopt, powerItems());
    model.moveHighlight(2);
    model.setHover(3);
    model.reset();
    QCOMPARE(model.highlight(), 0);
    QCOMPARE(model.hover(), -1);
    QCOMPARE(model.accept(), 0);
}

void TestMenuModel::popupPlacementStaysOnScreen()
{
    // 正常情况：在工作区里居中。
    const app::PopupPoint centred =
        app::centrePopup(app::PopupRect{0, 0, 1920, 1040}, app::PopupRect{0, 0, 1920, 1080}, 300, 274);
    QCOMPARE(centred.x, 810);
    QCOMPARE(centred.y, 383);

    // 卡片比工作区还宽：贴工作区的左边。
    const app::PopupPoint narrow =
        app::centrePopup(app::PopupRect{0, 0, 200, 600}, app::PopupRect{0, 0, 300, 600}, 300, 274);
    QCOMPARE(narrow.x, 0);

    // 本机实测过的那个坑：225% 缩放下 Qt 报出的工作区比屏幕还宽
    // （工作区从 x=108 开始、宽 485，而屏幕只有 533 宽），
    // 不夹进屏幕的话 500 宽的帮助卡片会被放到屏幕外面去。
    const app::PopupPoint real = app::centrePopup(app::PopupRect{108, 0, 485, 1095},
                                                  app::PopupRect{0, 0, 533, 1095},
                                                  500,
                                                  708);
    // 居中算出来是 x=101，卡片会从 101 伸到 601，超过 533 的屏幕右边界。
    QCOMPARE(real.x, 33);
    QVERIFY(real.x + 500 <= 533);
    QCOMPARE(real.y, 193);

    // 屏幕在副显示器上、坐标是负数时也要能算对。
    const app::PopupPoint left =
        app::centrePopup(app::PopupRect{-1920, 0, 1920, 1040}, app::PopupRect{-1920, 0, 1920, 1080}, 300, 274);
    QCOMPARE(left.x, -1110);
    QCOMPARE(left.y, 383);
}

void TestMenuModel::rolesExposeWhatTheDelegateNeeds()
{
    app::MenuModel model;
    model.setItems(QStringLiteral("电源"), powerItems());

    const int labelRole = roleOf(model, "label");
    const int hintRole = roleOf(model, "hint");
    const int keyRole = roleOf(model, "keyText");
    const int selectedRole = roleOf(model, "rowSelected");
    QVERIFY(labelRole > 0);
    QVERIFY(hintRole > 0);
    QVERIFY(keyRole > 0);
    QVERIFY(selectedRole > 0);

    // 行几何与「鼠标悬停」不再由模型给：每一行是标准的 `ItemDelegate`，它自己
    // 就有 `hovered` 与 `highlighted` 两个属性（角色名撞上就声明不出来），
    // 而行几何与命中测试归 `ListView`。这条断言盯着别把它们再加回来。
    QCOMPARE(roleOf(model, "highlighted"), -1);
    QCOMPARE(roleOf(model, "hovered"), -1);
    QCOMPARE(roleOf(model, "rowRect"), -1);

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(0, 0), labelRole).toString(), QStringLiteral("睡眠"));
    QCOMPARE(model.data(model.index(0, 0), hintRole).toString(), QStringLiteral("Sleep"));
    // 徽标显示大写。
    QCOMPARE(model.data(model.index(0, 0), keyRole).toString(), QStringLiteral("S"));
    QCOMPARE(model.data(model.index(3, 0), keyRole).toString(), QString());
    QCOMPARE(model.data(model.index(0, 0), selectedRole).toBool(), true);
    QCOMPARE(model.data(model.index(1, 0), selectedRole).toBool(), false);

    // 鼠标悬停的那一行就是高亮那一行（`Enter` 选的也是它）。
    model.setHover(1);
    QCOMPARE(model.data(model.index(1, 0), selectedRole).toBool(), true);
    QCOMPARE(model.data(model.index(0, 0), selectedRole).toBool(), false);

    // 越界/无效下标不能崩。
    QCOMPARE(model.data(QModelIndex(), labelRole).isValid(), false);
    QCOMPARE(model.data(model.index(9, 0), labelRole).isValid(), false);
}

QTEST_MAIN(TestMenuModel)
#include "tst_menu_model.moc"
