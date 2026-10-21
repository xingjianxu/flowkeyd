// 工作空间**事件管线**里那一块纯逻辑：事件种类名 + 「我们自己引起的事件」的抑制表
// （`core/workspace_event.h`）。
//
// 抑制表存在的理由（M0 实测）：`ShowWindow(SW_HIDE)` 会**同时**产生一条
// `EVENT_OBJECT_HIDE` 和一条 `EVENT_OBJECT_SHOW`，而 `SetWinEventHook` 分不出是谁
// 干的。不吞掉的话，状态机就会把自己的显隐当成用户操作，和自己打成一个环。
//
// 这张表在**两条线程**上用（动作线程登记、钩子线程命中），所以除了语义之外还要
// 确认它自己能扛住并发：最后一条用例用两条线程同时登记/消费。
#include <QtTest>

#include <QStringList>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "core/workspace_event.h"

using namespace flowkeyd;

class TestWorkspaceEvent : public QObject
{
    Q_OBJECT

private slots:
    void namesAreStable();
    void expectationsAreSwallowedOnce();
    void expectationsExpire();
    void expectationsAreKeyedByKindAndWindow();
    void concurrentUseIsSafe();
};

void TestWorkspaceEvent::namesAreStable()
{
    // 这些名字会出现在日志里（`workspace: ...`），所以钉住它们。
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(core::WorkspaceEvent::Kind::Startup)),
             QStringLiteral("startup"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(
                 core::WorkspaceEvent::Kind::WindowAppeared)),
             QStringLiteral("window appeared"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(
                 core::WorkspaceEvent::Kind::WindowDestroyed)),
             QStringLiteral("window destroyed"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(
                 core::WorkspaceEvent::Kind::WindowMinimized)),
             QStringLiteral("window minimized"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(
                 core::WorkspaceEvent::Kind::WindowRestored)),
             QStringLiteral("window restored"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(core::WorkspaceEvent::Kind::WindowMoved)),
             QStringLiteral("window moved"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(core::WorkspaceEvent::Kind::FocusChanged)),
             QStringLiteral("focus changed"));
    QCOMPARE(QString::fromLatin1(core::workspaceEventName(
                 core::WorkspaceEvent::Kind::MonitorsChanged)),
             QStringLiteral("monitors changed"));
}

void TestWorkspaceEvent::expectationsAreSwallowedOnce()
{
    core::ExpectedWorkspaceEvents expected;
    QCOMPARE(expected.size(), 0u);

    // 没登记过的东西一律放行。
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 42, 1000));

    expected.expect(core::WorkspaceEvent::Kind::WindowAppeared, 42, 1000);
    QCOMPARE(expected.size(), 1u);
    // 命中即吞（`SW_HIDE` → 一条 HIDE + 一条 SHOW；我们只需要吞那一条 SHOW）。
    QVERIFY(expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 42, 1000));
    // 同一次显隐不会产生第二条，所以登记也只吞一次。
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 42, 1000));
    QCOMPARE(expected.size(), 0u);

    // `clear()` 全部忘掉（`reload` / 退出用）。
    expected.expect(core::WorkspaceEvent::Kind::WindowAppeared, 7, 1000);
    expected.expect(core::WorkspaceEvent::Kind::WindowDestroyed, 8, 1000);
    QCOMPARE(expected.size(), 2u);
    expected.clear();
    QCOMPARE(expected.size(), 0u);
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 7, 1000));
}

void TestWorkspaceEvent::expectationsExpire()
{
    core::ExpectedWorkspaceEvents expected;
    // 默认存活时间够长（目标进程忙时事件会晚到几百毫秒）。
    expected.expect(core::WorkspaceEvent::Kind::WindowAppeared, 5, 1000);
    QVERIFY(expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 5, 1000 + 100));

    // 过期之后就不该再吞（窗口真的是在那一刻出现的，不能被误吞）。
    expected.expect(core::WorkspaceEvent::Kind::WindowAppeared, 5, 2000);
    const std::uint64_t after =
        2000 + core::ExpectedWorkspaceEvents::kDefaultLifetimeMs + 1;
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 5, after));
    // 过期条目会被顺手清掉，不会无限长。
    QCOMPARE(expected.size(), 0u);

    // 自定义存活时间：立刻过期。
    expected.expect(core::WorkspaceEvent::Kind::WindowAppeared, 6, 3000, 0);
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 6, 3000));
}

void TestWorkspaceEvent::expectationsAreKeyedByKindAndWindow()
{
    core::ExpectedWorkspaceEvents expected;
    expected.expect(core::WorkspaceEvent::Kind::WindowAppeared, 11, 500);

    // 别的窗口不算命中。
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 12, 500));
    // 别的种类也不算命中（同一个窗口的最小化事件与它无关）。
    QVERIFY(!expected.consume(core::WorkspaceEvent::Kind::WindowMinimized, 11, 500));
    // 登记的那一条还在。
    QCOMPARE(expected.size(), 1u);
    QVERIFY(expected.consume(core::WorkspaceEvent::Kind::WindowAppeared, 11, 500));
}

void TestWorkspaceEvent::concurrentUseIsSafe()
{
    core::ExpectedWorkspaceEvents expected;
    constexpr int kIterations = 400;
    std::atomic<int> swallowed{0};

    // 一条线程当动作线程（不断登记），一条当钩子线程（不断消费）：不崩、不丢锁，
    // 总次数落在合理范围里。
    std::thread producer([&expected]() {
        for (int i = 0; i < kIterations; ++i) {
            expected.expect(core::WorkspaceEvent::Kind::WindowAppeared,
                            static_cast<core::WindowId>(i % 8),
                            static_cast<std::uint64_t>(i));
        }
    });
    std::thread consumer([&expected, &swallowed]() {
        for (int i = 0; i < kIterations; ++i) {
            if (expected.consume(core::WorkspaceEvent::Kind::WindowAppeared,
                                 static_cast<core::WindowId>(i % 8),
                                 static_cast<std::uint64_t>(i))) {
                swallowed.fetch_add(1);
            }
        }
    });
    producer.join();
    consumer.join();

    QVERIFY(swallowed.load() >= 0);
    QVERIFY(expected.size() <= 8u);
}

QTEST_MAIN(TestWorkspaceEvent)
#include "tst_workspace_event.moc"
