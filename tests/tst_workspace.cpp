// 工作空间**纯逻辑状态机**（`core/workspace`）的测试。
//
// 这一层不碰 Win32 / GUI，所以每条规则都能在这里直接钉住：显隐顺序（先显后藏、
// 显示按 Z 序从底到顶、隐藏从顶到底）、焦点交接（M0 实测：隐藏掉前台窗口之后系统
// 不会自己把前台交给别人）、每显示器独立性、`count` 变小后的合并、显示器拔插的
// 迁移与复位，以及随机操作序列下的一组不变量。
//
// 真实的 `ShowWindow` / `SetWindowPos` 行为不在这里测（M4 的后端 + `tst_interactive`）。
#include <QtTest>

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include "core/workspace.h"

using namespace flowkeyd;

namespace {

const core::MonitorKey kScreen1 = QStringLiteral("\\\\.\\DISPLAY1");
const core::MonitorKey kScreen2 = QStringLiteral("\\\\.\\DISPLAY2");
const core::DesktopKey kDeskA = QStringLiteral("{11111111-aaaa}");
const core::DesktopKey kDeskB = QStringLiteral("{22222222-bbbb}");

/// 窗口 id 列表 → 便于 QCOMPARE 打印的字符串列表。
QStringList listing(const std::vector<core::WindowId> &windows)
{
    QStringList out;
    for (const core::WindowId id : windows) {
        out.append(QString::number(id));
    }
    return out;
}

QStringList listing(std::initializer_list<core::WindowId> windows)
{
    return listing(std::vector<core::WindowId>(windows));
}

/// 当前虚拟桌面上这块屏的活动空间；这块屏还不认识时返回 0。
std::uint32_t activeOf(const core::WorkspaceState &state, const core::MonitorKey &monitor)
{
    const auto spaces = state.spaces(monitor);
    return spaces.has_value() ? spaces->active : 0;
}

/// 这块屏一共有几个空间；不认识时返回 0。
std::uint32_t countOf(const core::WorkspaceState &state, const core::MonitorKey &monitor)
{
    const auto spaces = state.spaces(monitor);
    return spaces.has_value() ? spaces->count : 0;
}

/// 随机序列测试里用：每个窗口都恰属于一个空间、每个空间里的窗口集合是完整的、
/// Z 序与焦点序不重复、「活动空间」与「应该可见」一致。
void checkInvariants(const core::WorkspaceState &state, const QString &where)
{
    std::vector<core::WindowId> tracked;
    for (core::WindowId id = 1; id <= 12; ++id) {
        if (state.tracks(id)) {
            tracked.push_back(id);
        }
    }

    QSet<qulonglong> zOrders;
    QSet<qulonglong> focusSeqs;
    for (const core::WindowId id : tracked) {
        const auto record = state.record(id);
        QVERIFY2(record.has_value(), qPrintable(where));
        const auto spaces = state.spaces(record->workspace.desktop, record->workspace.monitor);
        QVERIFY2(spaces.has_value(), qPrintable(where));
        QCOMPARE(spaces->count, state.count());
        QVERIFY2(record->workspace.index >= 1, qPrintable(where));
        QVERIFY2(record->workspace.index <= spaces->count, qPrintable(where));
        QVERIFY2(spaces->active >= 1 && spaces->active <= spaces->count, qPrintable(where));
        // 「活动空间」就是「该可见」的唯一判据（M0：不能用 IsWindowVisible）。
        QCOMPARE(state.isVisible(id), record->workspace.index == spaces->active);
        QVERIFY2(!zOrders.contains(record->zOrder), qPrintable(where));
        zOrders.insert(record->zOrder);
        if (record->lastFocusSeq != 0) {
            QVERIFY2(!focusSeqs.contains(record->lastFocusSeq), qPrintable(where));
            focusSeqs.insert(record->lastFocusSeq);
        }
    }

    // 每个（虚拟桌面，显示器）里，所有空间的窗口并起来必须恰好是这一组全部窗口。
    const std::vector<core::DesktopKey> desktops{kDeskA, kDeskB};
    for (const core::DesktopKey &desktop : desktops) {
        for (const core::MonitorKey &monitor : state.monitors()) {
            const auto spaces = state.spaces(desktop, monitor);
            if (!spaces.has_value()) {
                continue;
            }
            std::vector<core::WindowId> collected;
            for (std::uint32_t index = 1; index <= spaces->count; ++index) {
                for (const core::WindowId id : state.windowsInSpace(desktop, monitor, index)) {
                    collected.push_back(id);
                }
            }
            std::vector<core::WindowId> expected;
            for (const core::WindowId id : tracked) {
                const auto record = state.record(id);
                if (record->workspace.desktop == desktop && record->workspace.monitor == monitor) {
                    expected.push_back(id);
                }
            }
            std::sort(collected.begin(), collected.end());
            std::sort(expected.begin(), expected.end());
            QCOMPARE(listing(collected), listing(expected));
        }
    }
}

} // namespace

class TestWorkspace : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndUnknownQueries();
    void appearedWindowJoinsTheActiveSpace();
    void trackedWindowsAreListedForTheEnumerateBackend();
    void rulePlacedWindowIsHiddenWithoutFocus();
    void switchingWithinOneMonitor();
    void switchingIsClampedAndIdempotent();
    void emptySpaceDoesNotStealFocus();
    void minimizedWindowsAreNotFocusTargets();
    void movingForegroundWindowHandsFocusOver();
    void movingBackgroundWindowLeavesFocusAlone();
    void destroyedWindowsForgetTheirRecords();
    void monitorsAreIndependent();
    void reconfigureMergesOutOfRangeSpaces();
    void monitorRemovalRehomesAndReattachRestores();
    void movedToAnotherMonitorJoinsItsActiveSpace();
    void perDesktopSpacesAreIndependent();
    void restoreAllShowsEverything();
    void randomOperationsKeepInvariants();
};

void TestWorkspace::defaultsAndUnknownQueries()
{
    core::WorkspaceState state;
    QCOMPARE(state.count(), core::kDefaultWorkspaceCount);
    QCOMPARE(state.desktop(), QString());
    QVERIFY(state.monitors().isEmpty());

    // 什么都不认识的时候：所有查询都是「没有」，所有命令都无操作。
    QVERIFY(!state.tracks(1));
    QVERIFY(!state.record(1).has_value());
    QVERIFY(!state.isVisible(1));
    QVERIFY(!state.spaces(kScreen1).has_value());
    QVERIFY(!state.spaces(kDeskA, kScreen1).has_value());
    QVERIFY(state.windowsInSpace(kScreen1, 1).empty());

    QVERIFY(state.switchTo(kScreen1, 3).isEmpty());
    QVERIFY(state.moveWindow(1, 3).isEmpty());
    QVERIFY(state.windowDestroyed(1).isEmpty());
    QVERIFY(state.windowMinimized(1, true).isEmpty());
    QVERIFY(state.focusChanged(1).isEmpty());
    QVERIFY(state.windowMovedToMonitor(1, kScreen2).isEmpty());
    QVERIFY(state.restoreAll().isEmpty());

    // 换虚拟桌面本身不产生任何效果（Windows 自己负责那张桌面的显隐）。
    QVERIFY(state.setDesktop(kDeskA).isEmpty());
    QCOMPARE(state.desktop(), kDeskA);
    // 同一个桌面再来一次也无操作。
    QVERIFY(state.setDesktop(kDeskA).isEmpty());
}

void TestWorkspace::appearedWindowJoinsTheActiveSpace()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);

    // 新窗口默认落在活动空间（= 1）：什么都不用做，它本来就该可见。
    QVERIFY(state.windowAppeared(10, kScreen1).isEmpty());
    QVERIFY(state.tracks(10));
    QVERIFY(state.isVisible(10));
    const auto record = state.record(10);
    QVERIFY(record.has_value());
    QCOMPARE(record->workspace.desktop, kDeskA);
    QCOMPARE(record->workspace.monitor, kScreen1);
    QCOMPARE(record->workspace.index, 1u);
    QCOMPARE(record->lastFocusSeq, 0u); // 还没拿到过焦点
    QVERIFY(!record->minimized);
    QVERIFY(!record->homeMonitor.has_value());

    QCOMPARE(state.monitors(), QStringList{kScreen1});
    QCOMPARE(countOf(state, kScreen1), core::kDefaultWorkspaceCount);
    QCOMPARE(activeOf(state, kScreen1), 1u);
    QCOMPARE(listing(state.windowsInSpace(kScreen1, 1)), listing({10}));

    // 幂等：同一个窗口再「出现」一次不会重复记账、也不产生效果。
    QVERIFY(state.windowAppeared(10, kScreen1).isEmpty());
    const auto again = state.record(10);
    QVERIFY(again.has_value());
    QCOMPARE(again->zOrder, record->zOrder);

    // 非法参数直接忽略（句柄 0、显示器为空）。
    QVERIFY(state.windowAppeared(0, kScreen1).isEmpty());
    QVERIFY(state.windowAppeared(11, QString()).isEmpty());
    QVERIFY(!state.tracks(0));
    QVERIFY(!state.tracks(11));
}

void TestWorkspace::trackedWindowsAreListedForTheEnumerateBackend()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    QVERIFY(state.trackedWindows().empty());

    state.windowAppeared(9, kScreen1);
    state.windowAppeared(3, kScreen2);
    state.windowAppeared(7, kScreen1, 2);
    // 句柄升序（`platform::win::workspace::enumerateManageable()` 拿它当
    // 「已记录的窗口」清单，顺序不重要，但固定了才好断言）。
    QCOMPARE(listing(state.trackedWindows()), listing({3, 7, 9}));

    state.windowDestroyed(7);
    QCOMPARE(listing(state.trackedWindows()), listing({3, 9}));

    // `restoreAll()` 与 `reconfigure()` 都不会把记录弄丢。
    state.restoreAll();
    state.reconfigure(2);
    QCOMPARE(listing(state.trackedWindows()), listing({3, 9}));
}

void TestWorkspace::rulePlacedWindowIsHiddenWithoutFocus()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);

    // `window_rule` / `app` 的 `workspace = 3`：新窗口直接落进那个（非活动的）空间，
    // 出现即隐藏，而且**不抢焦点**。
    const core::WorkspaceEffects effects = state.windowAppeared(2, kScreen1, 3);
    QCOMPARE(listing(effects.show), QStringList{});
    QCOMPARE(listing(effects.hide), listing({2}));
    QVERIFY(!effects.focus.has_value());

    QVERIFY(!state.isVisible(2));
    QVERIFY(state.isVisible(1));
    QCOMPARE(activeOf(state, kScreen1), 1u);
    QCOMPARE(listing(state.windowsInSpace(kScreen1, 3)), listing({2}));

    // 越界的空间号夹到最后一个空间（配置校验在 M2，这里只保证不丢窗口）。
    QCOMPARE(listing(state.windowAppeared(3, kScreen1, 99).hide), listing({3}));
    QCOMPARE(state.record(3)->workspace.index, core::kDefaultWorkspaceCount);
}

void TestWorkspace::switchingWithinOneMonitor()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.windowAppeared(2, kScreen1);
    state.windowAppeared(3, kScreen1, 2);
    state.focusChanged(1);
    state.focusChanged(2); // 前台是 2（最近用过）

    // 切到空间 2：先显示新的（底 → 顶），再隐藏旧的（顶 → 底），最后聚焦。
    core::WorkspaceEffects effects = state.switchTo(kScreen1, 2);
    QCOMPARE(listing(effects.show), listing({3}));
    QCOMPARE(listing(effects.hide), listing({2, 1}));
    QVERIFY(effects.focus.has_value());
    QCOMPARE(*effects.focus, core::WindowId{3});
    QCOMPARE(activeOf(state, kScreen1), 2u);
    QVERIFY(state.isVisible(3));
    QVERIFY(!state.isVisible(1));
    QVERIFY(!state.isVisible(2));

    // 切回空间 1：显示顺序按原来的 Z 序（1 在 2 下面），焦点回到最近用过的 2。
    effects = state.switchTo(kScreen1, 1);
    QCOMPARE(listing(effects.show), listing({1, 2}));
    QCOMPARE(listing(effects.hide), listing({3}));
    QVERIFY(effects.focus.has_value());
    QCOMPARE(*effects.focus, core::WindowId{2});
    QCOMPARE(activeOf(state, kScreen1), 1u);
}

void TestWorkspace::switchingIsClampedAndIdempotent()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);

    // 目标就是当前活动空间：无操作（绝不重复显隐，否则自触发事件会打成环）。
    QVERIFY(state.switchTo(kScreen1, 1).isEmpty());
    // 空间号 0 / 越界都夹进合法范围。
    QVERIFY(state.switchTo(kScreen1, 0).isEmpty()); // 0 → 1，还是当前空间
    QCOMPARE(activeOf(state, kScreen1), 1u);
    const core::WorkspaceEffects effects = state.switchTo(kScreen1, 99);
    QCOMPARE(activeOf(state, kScreen1), core::kDefaultWorkspaceCount);
    QCOMPARE(listing(effects.hide), listing({1}));
    QCOMPARE(listing(effects.show), QStringList{});
    // 显示器名为空 = 不知道怎么切，什么都不做。
    QVERIFY(state.switchTo(QString(), 2).isEmpty());
    QCOMPARE(activeOf(state, kScreen1), core::kDefaultWorkspaceCount);
}

void TestWorkspace::emptySpaceDoesNotStealFocus()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.focusChanged(1);

    const core::WorkspaceEffects effects = state.switchTo(kScreen1, 4);
    QCOMPARE(listing(effects.hide), listing({1}));
    QCOMPARE(listing(effects.show), QStringList{});
    QVERIFY(!effects.focus.has_value()); // 空空间：谁都不抢
    QVERIFY(!state.isVisible(1));

    // 切回来：没有焦点记录时取 Z 序最上面的那个（= 最近出现的）。
    const core::WorkspaceEffects back = state.switchTo(kScreen1, 1);
    QCOMPARE(listing(back.show), listing({1}));
    QVERIFY(back.focus.has_value());
    QCOMPARE(*back.focus, core::WindowId{1});
}

void TestWorkspace::minimizedWindowsAreNotFocusTargets()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.windowAppeared(2, kScreen1);
    state.focusChanged(1);
    state.focusChanged(2);
    state.switchTo(kScreen1, 2);

    // 最近用过的 2 被最小化了：焦点交给次近的 1，而不是把 2 弹出来。
    state.windowMinimized(2, true);
    QVERIFY(state.record(2)->minimized);
    core::WorkspaceEffects effects = state.switchTo(kScreen1, 1);
    QVERIFY(effects.focus.has_value());
    QCOMPARE(*effects.focus, core::WindowId{1});

    // 整个空间都最小化：宁可不抢焦点。
    state.windowMinimized(1, true);
    state.switchTo(kScreen1, 2);
    effects = state.switchTo(kScreen1, 1);
    QVERIFY(!effects.focus.has_value());

    // 拿到焦点说明已经还原了（最小化的窗口不可能拿到焦点）。
    state.focusChanged(1);
    QVERIFY(!state.record(1)->minimized);
    // 不认识的窗口 / 值没变：都无操作。
    QVERIFY(state.windowMinimized(99, true).isEmpty());
    QVERIFY(state.windowMinimized(1, false).isEmpty());
}

void TestWorkspace::movingForegroundWindowHandsFocusOver()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.windowAppeared(2, kScreen1);
    state.focusChanged(1);
    state.focusChanged(2); // 用户正在用 2

    core::WorkspaceEffects effects = state.moveWindow(2, 2);
    QCOMPARE(listing(effects.hide), listing({2}));
    QCOMPARE(listing(effects.show), QStringList{});
    // 藏掉前台窗口之后系统不会自己移交前台（M0），必须显式交给次近的 1。
    QVERIFY(effects.focus.has_value());
    QCOMPARE(*effects.focus, core::WindowId{1});
    QVERIFY(!state.isVisible(2));
    QCOMPARE(activeOf(state, kScreen1), 1u); // 活动空间没变：用户留在原地
    QCOMPARE(state.record(2)->workspace.index, 2u);
    QCOMPARE(listing(state.windowsInSpace(kScreen1, 2)), listing({2}));

    // 同一个空间：无操作。
    QVERIFY(state.moveWindow(2, 2).isEmpty());

    // 从藏着的空间叫回活动空间：显示并聚焦。
    effects = state.moveWindow(2, 1);
    QCOMPARE(listing(effects.show), listing({2}));
    QCOMPARE(listing(effects.hide), QStringList{});
    QVERIFY(effects.focus.has_value());
    QCOMPARE(*effects.focus, core::WindowId{2});
    QVERIFY(state.isVisible(2));
    QCOMPARE(state.record(2)->workspace.index, 1u);
}

void TestWorkspace::movingBackgroundWindowLeavesFocusAlone()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.windowAppeared(2, kScreen1);
    state.focusChanged(1); // 前台是 1，2 在后台

    const core::WorkspaceEffects effects = state.moveWindow(2, 5);
    QCOMPARE(listing(effects.hide), listing({2}));
    // 搬走的不是前台窗口：不该顺手把焦点抢给别人。
    QVERIFY(!effects.focus.has_value());
    QVERIFY(state.isVisible(1));
    QVERIFY(!state.isVisible(2));
}

void TestWorkspace::destroyedWindowsForgetTheirRecords()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.windowAppeared(2, kScreen1, 2);
    state.focusChanged(1);

    QVERIFY(state.windowDestroyed(2).isEmpty());
    QVERIFY(!state.tracks(2));
    QVERIFY(!state.record(2).has_value());
    QVERIFY(!state.isVisible(2));
    QVERIFY(state.windowsInSpace(kScreen1, 2).empty());

    // 前台窗口没了：记录与「最近焦点」一起清掉（`HWND` 可能马上被别人复用）。
    QVERIFY(state.windowDestroyed(1).isEmpty());
    QVERIFY(!state.tracks(1));
    // 同一个 id 再出现算新窗口：Z 序与焦点序都是新的。
    QVERIFY(state.windowAppeared(1, kScreen1).isEmpty());
    QCOMPARE(state.record(1)->lastFocusSeq, 0u);
    QVERIFY(state.record(1)->zOrder > 0);
    // 销毁不认识的窗口同样安全。
    QVERIFY(state.windowDestroyed(99).isEmpty());
}

void TestWorkspace::monitorsAreIndependent()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.windowAppeared(2, kScreen1);
    state.windowAppeared(3, kScreen2);

    // 切 A 屏：只有 A 屏的窗口动，B 屏的 3 完全不受影响。
    const core::WorkspaceEffects effects = state.switchTo(kScreen1, 2);
    QCOMPARE(listing(effects.hide), listing({2, 1}));
    QVERIFY(state.isVisible(3));
    QCOMPARE(activeOf(state, kScreen1), 2u);
    QCOMPARE(activeOf(state, kScreen2), 1u);

    // 再切 B 屏：A 屏的活动空间与窗口都不动。
    const core::WorkspaceEffects other = state.switchTo(kScreen2, 3);
    QCOMPARE(listing(other.hide), listing({3}));
    QCOMPARE(activeOf(state, kScreen2), 3u);
    QCOMPARE(activeOf(state, kScreen1), 2u);
    QVERIFY(!state.isVisible(3));

    // 每块屏的窗口只在自己的空间里；切到空间 3 之后 3 号窗口（在空间 1 里）就藏
    // 起来了，而不是跟着跑过去。
    QCOMPARE(listing(state.windowsInSpace(kScreen1, 1)), listing({1, 2}));
    QCOMPARE(listing(state.windowsInSpace(kScreen2, 1)), listing({3}));
    QVERIFY(state.windowsInSpace(kScreen2, 3).empty());
}

void TestWorkspace::reconfigureMergesOutOfRangeSpaces()
{
    core::WorkspaceState state; // 默认 9
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);      // 空间 1
    state.windowAppeared(2, kScreen1, 5);   // 空间 5（藏着）
    state.windowAppeared(3, kScreen1, 9);   // 空间 9（藏着）
    state.switchTo(kScreen1, 9);            // 活动空间 9：只有 3 可见
    QVERIFY(state.isVisible(3));
    QVERIFY(!state.isVisible(1));
    QVERIFY(!state.isVisible(2));

    // 9 → 3：活动空间被夹成 3，落在 5 / 9 的窗口并进最后一个空间。3 本来就在显示，
    // 不用动；2 本来藏着、现在落进了活动空间 → 必须显式显示（否则它会留在「活动
    // 空间却不可见」的坏状态，而切到活动空间是幂等无操作）。
    const core::WorkspaceEffects effects = state.reconfigure(3);
    QCOMPARE(state.count(), 3u);
    QCOMPARE(countOf(state, kScreen1), 3u);
    QCOMPARE(activeOf(state, kScreen1), 3u);
    QCOMPARE(listing(effects.show), listing({2}));
    QCOMPARE(listing(effects.hide), QStringList{});
    QCOMPARE(state.record(2)->workspace.index, 3u);
    QCOMPARE(state.record(3)->workspace.index, 3u);
    QVERIFY(state.isVisible(2));
    QVERIFY(state.isVisible(3));
    QVERIFY(!state.isVisible(1));
    QCOMPARE(listing(state.windowsInSpace(kScreen1, 3)), listing({2, 3}));

    // 取值范围夹在 1–10；压到 1 时所有窗口都并进唯一的活动空间 → 全部露出来。
    const core::WorkspaceEffects all = state.reconfigure(0);
    QCOMPARE(state.count(), core::kMinWorkspaceCount);
    QCOMPARE(listing(all.show), listing({1}));
    for (const core::WindowId id : {1, 2, 3}) {
        QVERIFY(state.isVisible(id));
        QCOMPARE(state.record(id)->workspace.index, 1u);
    }
    state.reconfigure(100);
    QCOMPARE(state.count(), core::kMaxWorkspaceCount);
    // 数量没变 → 什么都不做。
    QVERIFY(state.reconfigure(core::kMaxWorkspaceCount).isEmpty());
}

void TestWorkspace::monitorRemovalRehomesAndReattachRestores()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    QVERIFY(state.monitorsChanged({kScreen1, kScreen2}).isEmpty());
    QCOMPARE(state.monitors(), (QStringList{kScreen1, kScreen2}));
    QCOMPARE(countOf(state, kScreen2), core::kDefaultWorkspaceCount);

    state.windowAppeared(10, kScreen1);    // 可见
    state.windowAppeared(11, kScreen1, 2); // 藏着
    state.windowAppeared(12, kScreen2);    // 可见

    // 拔掉 1 号屏：它的窗口并入 2 号屏的活动空间。本来藏着的 11 必须显示出来 ——
    // 否则它会落进「活动空间却不可见」的坏状态，而切到活动空间是幂等无操作。
    const core::WorkspaceEffects removed = state.monitorsChanged({kScreen2});
    QCOMPARE(listing(removed.show), listing({11}));
    QCOMPARE(listing(removed.hide), QStringList{});
    QVERIFY(!removed.focus.has_value()); // 拔显示器不该把前台拽走
    QCOMPARE(state.monitors(), QStringList{kScreen2});
    QVERIFY(!state.spaces(kDeskA, kScreen1).has_value());
    for (const core::WindowId id : {10, 11}) {
        QVERIFY(state.isVisible(id));
        QCOMPARE(state.record(id)->workspace.monitor, kScreen2);
        QCOMPARE(state.record(id)->homeMonitor.value_or(QString()), kScreen1);
    }
    // 本来就在 2 号屏上的窗口不受影响（也不该被记成「有原归属」）。
    QVERIFY(state.isVisible(12));
    QCOMPARE(state.record(12)->workspace.monitor, kScreen2);
    QVERIFY(!state.record(12)->homeMonitor.has_value());
    QCOMPARE(state.record(11)->homeIndex.value_or(0), 2u);

    // 接回来：物归原主。原空间 2 不是 1 号屏的活动空间 → 11 又藏起来。
    const core::WorkspaceEffects attached = state.monitorsChanged({kScreen1, kScreen2});
    QCOMPARE(listing(attached.hide), listing({11}));
    QCOMPARE(listing(attached.show), QStringList{});
    QVERIFY(!attached.focus.has_value());
    QCOMPARE(state.record(10)->workspace.monitor, kScreen1);
    QCOMPARE(state.record(10)->workspace.index, 1u);
    QCOMPARE(state.record(11)->workspace.monitor, kScreen1);
    QCOMPARE(state.record(11)->workspace.index, 2u);
    QVERIFY(!state.record(11)->homeMonitor.has_value());
    QVERIFY(!state.isVisible(11));
    QCOMPARE(state.record(12)->workspace.monitor, kScreen2);

    // 空列表（一块屏都没有）：什么都不动，宁可不动窗口也不猜归属。
    QVERIFY(state.monitorsChanged({}).isEmpty());
    QCOMPARE(state.monitors(), (QStringList{kScreen1, kScreen2}));
    QVERIFY(!state.isVisible(11));
}

void TestWorkspace::movedToAnotherMonitorJoinsItsActiveSpace()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.monitorsChanged({kScreen1, kScreen2});
    state.switchTo(kScreen2, 2); // 2 号屏的活动空间是 2
    state.windowAppeared(1, kScreen1);
    state.focusChanged(1);

    // 拖到 2 号屏：改归那边的活动空间，窗口仍然是可见的。
    QVERIFY(state.windowMovedToMonitor(1, kScreen2).isEmpty());
    QCOMPARE(state.record(1)->workspace.monitor, kScreen2);
    QCOMPARE(state.record(1)->workspace.index, 2u);
    QVERIFY(state.isVisible(1));
    QCOMPARE(listing(state.windowsInSpace(kScreen2, 2)), listing({1}));

    // 同一个显示器：无操作（Z 序也不动）。
    const std::uint64_t zBefore = state.record(1)->zOrder;
    QVERIFY(state.windowMovedToMonitor(1, kScreen2).isEmpty());
    QCOMPARE(state.record(1)->zOrder, zBefore);

    // 藏着的窗口被搬到另一块屏（理论上拖不动，隐藏失败时才会发生）：让它出现。
    state.windowAppeared(2, kScreen1, 3);
    const core::WorkspaceEffects effects = state.windowMovedToMonitor(2, kScreen2);
    QCOMPARE(listing(effects.show), listing({2}));
    QVERIFY(state.isVisible(2));
    // 不认识的窗口 / 空显示器名：无操作。
    QVERIFY(state.windowMovedToMonitor(99, kScreen2).isEmpty());
    QVERIFY(state.windowMovedToMonitor(1, QString()).isEmpty());
}

void TestWorkspace::perDesktopSpacesAreIndependent()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);
    state.switchTo(kScreen1, 2);
    QCOMPARE(activeOf(state, kScreen1), 2u);
    QVERIFY(!state.isVisible(1));

    // 换到桌面 B：同一块屏有自己的一套活动空间（默认 1），窗口 1 的状态不变。
    state.setDesktop(kDeskB);
    QCOMPARE(activeOf(state, kScreen1), 1u);
    QCOMPARE(state.desktop(), kDeskB);
    QVERIFY(state.switchTo(kScreen1, 4).isEmpty());
    QCOMPARE(activeOf(state, kScreen1), 4u);
    // 在 B 上新出现的窗口记在 B 名下。
    state.windowAppeared(2, kScreen1);
    QCOMPARE(state.record(2)->workspace.desktop, kDeskB);
    QVERIFY(state.isVisible(2));

    // 回到 A：A 的活动空间还是 2，窗口 1 仍然藏着。
    state.setDesktop(kDeskA);
    QCOMPARE(activeOf(state, kScreen1), 2u);
    QVERIFY(!state.isVisible(1));
    QVERIFY(state.isVisible(2)); // 窗口 1 与 2 分属两张桌面的空间
    QCOMPARE(state.record(1)->workspace.desktop, kDeskA);
}

void TestWorkspace::restoreAllShowsEverything()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.windowAppeared(1, kScreen1);      // 可见
    state.windowAppeared(2, kScreen1, 2);   // 藏着
    state.setDesktop(kDeskB);
    state.windowAppeared(3, kScreen1, 3);   // 另一张桌面上的，也藏着

    // 恢复全部：按 Z 序从底到顶显示，不抢焦点；另一张桌面上的窗口也要恢复
    // （我们藏它们的时候 WS_VISIBLE 已经清掉了，Windows 不会替我们恢复）。
    const core::WorkspaceEffects effects = state.restoreAll();
    QCOMPARE(listing(effects.show), listing({2, 3}));
    QCOMPARE(listing(effects.hide), QStringList{});
    QVERIFY(!effects.focus.has_value());
    for (const core::WindowId id : {1, 2, 3}) {
        QVERIFY(state.isVisible(id));
        QCOMPARE(state.record(id)->workspace.index, 1u);
        QVERIFY(!state.record(id)->homeMonitor.has_value());
    }
    // 已经全部可见：再恢复一次无操作。
    QVERIFY(state.restoreAll().isEmpty());
}

void TestWorkspace::randomOperationsKeepInvariants()
{
    core::WorkspaceState state;
    state.setDesktop(kDeskA);
    state.monitorsChanged({kScreen1, kScreen2});

    QRandomGenerator rng(0x5EED5EEDu);
    for (int step = 0; step < 3000; ++step) {
        const core::WindowId window = rng.bounded(12) + 1; // 1..12
        const core::MonitorKey monitor = rng.bounded(2) == 0 ? kScreen1 : kScreen2;
        switch (rng.bounded(9)) {
        case 0:
        case 1: {
            std::optional<std::uint32_t> index;
            if (rng.bounded(2) == 0) {
                index = rng.bounded(12); // 含 0 与越界值，顺便测夹取
            }
            state.windowAppeared(window, monitor, index);
            break;
        }
        case 2:
            state.windowDestroyed(window);
            break;
        case 3:
            state.switchTo(monitor, rng.bounded(12));
            break;
        case 4:
            state.moveWindow(window, rng.bounded(12));
            break;
        case 5:
            state.focusChanged(window);
            break;
        case 6:
            state.windowMinimized(window, rng.bounded(2) == 0);
            break;
        case 7: {
            QStringList present;
            if (rng.bounded(3) != 0) {
                present.append(kScreen1);
            }
            if (rng.bounded(3) != 0) {
                present.append(kScreen2);
            }
            state.monitorsChanged(present);
            break;
        }
        default: {
            switch (rng.bounded(4)) {
            case 0:
                state.reconfigure(rng.bounded(13)); // 0..12，两侧都越界
                break;
            case 1:
                state.setDesktop(rng.bounded(2) == 0 ? kDeskA : kDeskB);
                break;
            case 2:
                state.restoreAll();
                break;
            default:
                state.windowMovedToMonitor(window, monitor);
                break;
            }
            break;
        }
        }
        checkInvariants(state, QStringLiteral("step %1").arg(step));
    }

    // 跑到最后总该有点东西被跟踪，否则上面那些不变量等于没测。
    int tracked = 0;
    for (core::WindowId id = 1; id <= 12; ++id) {
        if (state.tracks(id)) {
            ++tracked;
        }
    }
    QVERIFY(tracked > 0);
    QVERIFY(!state.monitors().isEmpty());
}

QTEST_MAIN(TestWorkspace)
#include "tst_workspace.moc"
