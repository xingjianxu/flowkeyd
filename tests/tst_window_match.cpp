// 窗口匹配纯逻辑的单测：可执行文件名提取、标题/进程名子串匹配。
#include <QtTest>

#include "core/action.h"
#include "core/window_match.h"

using namespace flowkeyd;

class TestWindowMatch : public QObject
{
    Q_OBJECT

private slots:
    void executableBaseNameHandlesBothSeparators();
    void titleMatchingIsCaseInsensitiveSubstring();
    void processMatchingCoversTheGuiSuffix();
    void unknownProcessOwnerNeverMatches();
    void queryRequiresEveryCondition();
    void toggleOnlyCollapsesAnAlreadyActiveWindow();
    void onlyStateChangingOpsHaveTransitions();
};

void TestWindowMatch::executableBaseNameHandlesBothSeparators()
{
    QCOMPARE(core::executableBaseName(QStringLiteral("C:\\Program Files\\WezTerm\\wezterm-gui.exe")),
             QStringLiteral("wezterm-gui.exe"));
    QCOMPARE(core::executableBaseName(QStringLiteral("C:/Program Files/Code/Code.exe")),
             QStringLiteral("code.exe"));
    QCOMPARE(core::executableBaseName(QStringLiteral("chrome.exe")), QStringLiteral("chrome.exe"));
    QCOMPARE(core::executableBaseName(QString()), QString());
    // 大小写无关：取出来的一律是小写，便于直接比较。
    QCOMPARE(core::executableBaseName(QStringLiteral("C:\\Windows\\NOTEPAD.EXE")),
             QStringLiteral("notepad.exe"));
}

void TestWindowMatch::titleMatchingIsCaseInsensitiveSubstring()
{
    const std::optional<QString> needle = QStringLiteral("wez");
    QVERIFY(core::windowTitleMatches(QStringLiteral("WezTerm - bash"), needle));
    QVERIFY(core::windowTitleMatches(QStringLiteral("wezterm"), needle));
    QVERIFY(!core::windowTitleMatches(QStringLiteral("Notepad"), needle));
    // 没有标题条件时一律算匹配。
    QVERIFY(core::windowTitleMatches(QStringLiteral("anything"), std::nullopt));
}

void TestWindowMatch::processMatchingCoversTheGuiSuffix()
{
    const std::optional<QString> name = QStringLiteral("wezterm-gui.exe");
    QVERIFY(core::windowProcessMatches(name, std::optional<QString>(QStringLiteral("wezterm"))));
    QVERIFY(core::windowProcessMatches(name, std::optional<QString>(QStringLiteral("WEZTERM-GUI"))));
    QVERIFY(!core::windowProcessMatches(name, std::optional<QString>(QStringLiteral("chrome"))));
    // 没有进程条件时一律算匹配。
    QVERIFY(core::windowProcessMatches(name, std::nullopt));
}

void TestWindowMatch::unknownProcessOwnerNeverMatches()
{
    // 拿不到进程名（访问被拒）时，即使查询是空的也算匹配；
    // 但只要查询要求进程名，就必须返回 false，不能假定未知属主匹配。
    QVERIFY(core::windowProcessMatches(std::nullopt, std::nullopt));
    QVERIFY(!core::windowProcessMatches(std::nullopt, std::optional<QString>(QStringLiteral("wezterm"))));
}

void TestWindowMatch::queryRequiresEveryCondition()
{
    const core::WindowQuery both = core::WindowQuery::make(
        std::optional<QString>(QStringLiteral("bash")),
        std::optional<QString>(QStringLiteral("wezterm")));
    QVERIFY(core::windowMatchesQuery(both, QStringLiteral("WezTerm - bash"),
                                     std::optional<QString>(QStringLiteral("wezterm-gui.exe"))));
    // 标题对但进程不对。
    QVERIFY(!core::windowMatchesQuery(both, QStringLiteral("WezTerm - bash"),
                                      std::optional<QString>(QStringLiteral("chrome.exe"))));
    // 进程对但标题不对。
    QVERIFY(!core::windowMatchesQuery(both, QStringLiteral("Notepad"),
                                      std::optional<QString>(QStringLiteral("wezterm-gui.exe"))));

    // 只有进程条件时，标题随便。
    const core::WindowQuery byProcess =
        core::WindowQuery::make(std::nullopt, std::optional<QString>(QStringLiteral("code")));
    QVERIFY(core::windowMatchesQuery(byProcess, QStringLiteral("whatever"),
                                     std::optional<QString>(QStringLiteral("Code.exe"))));

    // `"foreground"` / `"active"` 被 `WindowQuery::make` 清成“不限制”。
    const core::WindowQuery foreground = core::WindowQuery::make(
        std::optional<QString>(QStringLiteral("foreground")), std::nullopt);
    QVERIFY(foreground.isForeground());
    QVERIFY(core::windowMatchesQuery(foreground, QStringLiteral("anything"), std::nullopt));
}

void TestWindowMatch::toggleOnlyCollapsesAnAlreadyActiveWindow()
{
    using core::WindowOp;
    using core::WindowPlan;

    // 默认（`toggle` 未写）且目标已在前台：收起。
    QVERIFY(core::planWindowAction(WindowOp::Activate, std::nullopt, true)
            == WindowPlan::MinimizeBecauseActive);
    // 默认但目标不在前台：正常激活。
    QVERIFY(core::planWindowAction(WindowOp::Activate, std::nullopt, false) == WindowPlan::ApplyOp);
    // 显式 `toggle = false`：即使已在前台也不收起。
    QVERIFY(core::planWindowAction(WindowOp::Activate, std::optional<bool>(false), true)
            == WindowPlan::ApplyOp);
    // 显式 `toggle = true`：照旧收起。
    QVERIFY(core::planWindowAction(WindowOp::Activate, std::optional<bool>(true), true)
            == WindowPlan::MinimizeBecauseActive);
    // `toggle` 只对 activate 有意义：别的 op 一律照做。
    QVERIFY(core::planWindowAction(WindowOp::Minimize, std::nullopt, true) == WindowPlan::ApplyOp);
    QVERIFY(core::planWindowAction(WindowOp::Maximize, std::nullopt, true) == WindowPlan::ApplyOp);
    QVERIFY(core::planWindowAction(WindowOp::Close, std::nullopt, true) == WindowPlan::ApplyOp);
    // 四个「挪窗口」的 op 也不套用 toggle（即使目标已经在前台也照做）。
    for (const WindowOp op : {WindowOp::MovePrevDesktop, WindowOp::MoveNextDesktop,
                             WindowOp::MoveLeftMonitor, WindowOp::MoveRightMonitor}) {
        QVERIFY(core::planWindowAction(op, std::nullopt, true) == WindowPlan::ApplyOp);
    }
}

void TestWindowMatch::onlyStateChangingOpsHaveTransitions()
{
    using core::WindowOp;
    QVERIFY(core::windowOpHasTransition(WindowOp::Activate));
    QVERIFY(core::windowOpHasTransition(WindowOp::Minimize));
    QVERIFY(core::windowOpHasTransition(WindowOp::Maximize));
    QVERIFY(core::windowOpHasTransition(WindowOp::Restore));
    // `close` 与 `toggle_topmost` 不产生过渡，`animate` 对它们无意义；
    // 跨虚拟桌面移动也不产生过渡，而跨显示器移动会改变几何、有过渡。
    QVERIFY(!core::windowOpHasTransition(WindowOp::Close));
    QVERIFY(!core::windowOpHasTransition(WindowOp::ToggleTopmost));
    QVERIFY(!core::windowOpHasTransition(WindowOp::MovePrevDesktop));
    QVERIFY(!core::windowOpHasTransition(WindowOp::MoveNextDesktop));
    QVERIFY(core::windowOpHasTransition(WindowOp::MoveLeftMonitor));
    QVERIFY(core::windowOpHasTransition(WindowOp::MoveRightMonitor));
}

QTEST_MAIN(TestWindowMatch)
#include "tst_window_match.moc"
