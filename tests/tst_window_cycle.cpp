// 「在当前虚拟桌面 + 当前显示器的窗口之间轮转」的纯逻辑测试
// （`core/window_cycle`）。
//
// 真正枚举窗口 / 激活窗口在 `platform/win/window` 与 `app::Dispatcher` 里，
// 这里只钉住「这一次该去哪一个」这条判断：环怎么排、什么时候重排、首尾相接。
#include <QtTest>

#include "core/action.h"
#include "core/window_cycle.h"

using namespace flowkeyd;

namespace {

QStringList ids(std::initializer_list<const char *> list)
{
    QStringList out;
    for (const char *id : list) {
        out.append(QString::fromLatin1(id));
    }
    return out;
}

} // namespace

class TestWindowCycle : public QObject
{
    Q_OBJECT

private slots:
    void nothingToCycleWithoutCandidates();
    void firstPressGoesToTheSecondMostRecentWindow();
    void repeatedPressesKeepSteppingThroughTheSameRing();
    void previousWrapsAroundBackwards();
    void switchingToAWindowByHandStartsOver();
    void closedWindowsDropOutOfTheRingButKeepTheOrder();
    void loneWindowIsNotActivatedAgain();
    void loneWindowOnAnotherMonitorIsActivated();
    void foregroundThatIsNotACandidateStartsFromTheTop();
    void duplicatesAreCollapsed();
    void theMonitorComesFromTheCursor();
    void shorthandParsesBothOps();
};

void TestWindowCycle::nothingToCycleWithoutCandidates()
{
    const core::WindowCycleState state;
    QVERIFY(!core::planWindowCycle(state, QStringList(), QStringLiteral("a"), 1).has_value());
    // `step = 0` 什么都不做（调用方不会这么传，但别让它去激活一个随机窗口）。
    QVERIFY(!core::planWindowCycle(state, ids({"a", "b"}), QStringLiteral("a"), 0).has_value());
}

void TestWindowCycle::firstPressGoesToTheSecondMostRecentWindow()
{
    // 第一次按下：候选按 Z 序给（前台窗口在最前面），去第二个。
    const auto plan = core::planWindowCycle(
        core::WindowCycleState(), ids({"fg", "second", "third"}), QStringLiteral("fg"), 1);
    QVERIFY(plan.has_value());
    QCOMPARE(plan->target, QStringLiteral("second"));
    QVERIFY(plan->restarted);
    QCOMPARE(plan->state.anchor, QStringLiteral("second"));
    QCOMPARE(plan->state.ring, ids({"fg", "second", "third"}));
}

void TestWindowCycle::repeatedPressesKeepSteppingThroughTheSameRing()
{
    // 关键行为：激活会把窗口推到 Z 序最前面，所以第二次的候选顺序已经变成
    // `[second, fg, third]`。沿用第一次的环，于是去 `third`，而不是跳回 `fg`。
    const auto first = core::planWindowCycle(
        core::WindowCycleState(), ids({"fg", "second", "third"}), QStringLiteral("fg"), 1);
    QVERIFY(first.has_value());

    const auto second = core::planWindowCycle(
        first->state, ids({"second", "fg", "third"}), QStringLiteral("second"), 1);
    QVERIFY(second.has_value());
    QCOMPARE(second->target, QStringLiteral("third"));
    QVERIFY(!second->restarted);

    // 转一圈回到原地。
    const auto third = core::planWindowCycle(
        second->state, ids({"third", "second", "fg"}), QStringLiteral("third"), 1);
    QVERIFY(third.has_value());
    QCOMPARE(third->target, QStringLiteral("fg"));

    const auto fourth = core::planWindowCycle(
        third->state, ids({"fg", "third", "second"}), QStringLiteral("fg"), 1);
    QVERIFY(fourth.has_value());
    QCOMPARE(fourth->target, QStringLiteral("second"));
}

void TestWindowCycle::previousWrapsAroundBackwards()
{
    // 第一次按“上一个”：从环尾开始（最久没用的那个），首尾相接。
    const auto plan = core::planWindowCycle(
        core::WindowCycleState(), ids({"fg", "second", "third"}), QStringLiteral("fg"), -1);
    QVERIFY(plan.has_value());
    QCOMPARE(plan->target, QStringLiteral("third"));

    const auto back = core::planWindowCycle(
        plan->state, ids({"third", "fg", "second"}), QStringLiteral("third"), -1);
    QVERIFY(back.has_value());
    QCOMPARE(back->target, QStringLiteral("second"));

    // `next` 与 `prev` 共用同一个环，可以来回走。
    const auto forward = core::planWindowCycle(
        back->state, ids({"second", "third", "fg"}), QStringLiteral("second"), 1);
    QVERIFY(forward.has_value());
    QCOMPARE(forward->target, QStringLiteral("third"));
}

void TestWindowCycle::switchingToAWindowByHandStartsOver()
{
    const auto first = core::planWindowCycle(
        core::WindowCycleState(), ids({"fg", "second", "third"}), QStringLiteral("fg"), 1);
    QVERIFY(first.has_value());
    // 用户自己切到了 `third`（不是我们上次激活的 `second`）：按新的 Z 序重排。
    const auto next = core::planWindowCycle(
        first->state, ids({"third", "fg", "second"}), QStringLiteral("third"), 1);
    QVERIFY(next.has_value());
    QCOMPARE(next->target, QStringLiteral("fg"));
    QVERIFY(next->restarted);
    QCOMPARE(next->state.ring, ids({"third", "fg", "second"}));
}

void TestWindowCycle::closedWindowsDropOutOfTheRingButKeepTheOrder()
{
    const auto first = core::planWindowCycle(
        core::WindowCycleState(), ids({"fg", "b", "c", "d"}), QStringLiteral("fg"), 1);
    QVERIFY(first.has_value());
    QCOMPARE(first->target, QStringLiteral("b"));

    // `c` 被关掉了：环里剩下的顺序仍是 fg → b → d，所以下一个是 `d`。
    const auto second =
        core::planWindowCycle(first->state, ids({"b", "fg", "d"}), QStringLiteral("b"), 1);
    QVERIFY(second.has_value());
    QCOMPARE(second->target, QStringLiteral("d"));
    QVERIFY(!second->restarted);
    QCOMPARE(second->state.ring, ids({"fg", "b", "d"}));

    // 上一轮激活的那个被关掉了（前台不再是它）：重排。
    const auto third =
        core::planWindowCycle(second->state, ids({"foo", "fg", "b"}), QStringLiteral("foo"), 1);
    QVERIFY(third.has_value());
    QVERIFY(third->restarted);
    QCOMPARE(third->target, QStringLiteral("fg"));
}

void TestWindowCycle::loneWindowIsNotActivatedAgain()
{
    // 这一块屏上只剩用户正在用的那个窗口：按下去什么都不该发生。
    QVERIFY(!core::planWindowCycle(core::WindowCycleState(), ids({"fg"}),
                                   QStringLiteral("fg"), 1)
                 .has_value());
    QVERIFY(!core::planWindowCycle(core::WindowCycleState(), ids({"fg"}),
                                   QStringLiteral("fg"), -1)
                 .has_value());
}

void TestWindowCycle::loneWindowOnAnotherMonitorIsActivated()
{
    // 前台窗口不在这批候选里（例如它是覆盖层）：只有一个候选也要能过去。
    const auto plan = core::planWindowCycle(
        core::WindowCycleState(), ids({"only"}), QStringLiteral("overlay"), 1);
    QVERIFY(plan.has_value());
    QCOMPARE(plan->target, QStringLiteral("only"));
    QVERIFY(plan->restarted);
}

void TestWindowCycle::foregroundThatIsNotACandidateStartsFromTheTop()
{
    // 前台窗口不属于这批候选：`next` 从第一个开始，`prev` 从最后一个开始。
    const auto next = core::planWindowCycle(
        core::WindowCycleState(), ids({"a", "b", "c"}), QStringLiteral("overlay"), 1);
    QVERIFY(next.has_value());
    QCOMPARE(next->target, QStringLiteral("a"));

    const auto prev = core::planWindowCycle(
        core::WindowCycleState(), ids({"a", "b", "c"}), QStringLiteral("overlay"), -1);
    QVERIFY(prev.has_value());
    QCOMPARE(prev->target, QStringLiteral("c"));
}

void TestWindowCycle::duplicatesAreCollapsed()
{
    // 同一个窗口出现两次会让环算错（第二次按下去可能还留在原地）。
    const auto plan = core::planWindowCycle(
        core::WindowCycleState(), ids({"fg", "b", "b", "c", ""}), QStringLiteral("fg"), 1);
    QVERIFY(plan.has_value());
    QCOMPARE(plan->target, QStringLiteral("b"));
    QCOMPARE(plan->state.ring, ids({"fg", "b", "c"}));
}

void TestWindowCycle::theMonitorComesFromTheCursor()
{
    // 首选**光标悬停的那一块**（项目所有者 2026-10 改的）：哪怕前台窗口在别的屏上也转
    // 光标这一边的。
    QCOMPARE(core::chooseCycleMonitor(std::optional<std::size_t>(1),
                                      std::optional<std::size_t>(0),
                                      std::optional<std::size_t>(0)),
             std::optional<std::size_t>(1));
    QCOMPARE(core::chooseCycleMonitor(std::optional<std::size_t>(0),
                                      std::optional<std::size_t>(1),
                                      std::optional<std::size_t>(1)),
             std::optional<std::size_t>(0));
    // 光标问不出来（`GetCursorPos()` 失败）才退回前台窗口所在的那一块。
    QCOMPARE(core::chooseCycleMonitor(std::nullopt, std::optional<std::size_t>(1),
                                      std::optional<std::size_t>(0)),
             std::optional<std::size_t>(1));
    // 前台窗口也问不出来（`GetForegroundWindow()` 返回空、或它不在当前虚拟桌面上）
    // 时，退回当前桌面上压在最上面的那个窗口所在的那一块。
    QCOMPARE(core::chooseCycleMonitor(std::nullopt, std::nullopt, std::optional<std::size_t>(1)),
             std::optional<std::size_t>(1));
    // 三个都问不出来：不按显示器筛（转整张桌面）。
    QVERIFY(!core::chooseCycleMonitor(std::nullopt, std::nullopt, std::nullopt).has_value());
}

void TestWindowCycle::shorthandParsesBothOps()
{
    // `--list` 的摘要与简写解析都要认得这两个新 op。
    for (const QString &name : {QStringLiteral("cycle_next"), QStringLiteral("cycle_prev")}) {
        core::Action action;
        const std::optional<QString> error =
            core::parseActionShorthand(QStringLiteral("window:") + name, &action);
        QVERIFY2(!error.has_value(), qPrintable(error.value_or(QString())));
        QCOMPARE(action.kind, core::Action::Kind::Window);
        QCOMPARE(action.windowOp, name == QStringLiteral("cycle_next") ? core::WindowOp::CycleNext
                                                                      : core::WindowOp::CyclePrev);
        // 摘要里没有“目标查询”这一段，只写清楚范围。
        QCOMPARE(action.summary(),
                 name == QStringLiteral("cycle_next")
                     ? QStringLiteral("window CycleNext (current desktop + monitor)")
                     : QStringLiteral("window CyclePrev (current desktop + monitor)"));
    }
}

QTEST_MAIN(TestWindowCycle)
#include "tst_window_cycle.moc"
