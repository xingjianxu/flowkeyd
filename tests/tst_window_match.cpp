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
    void mainWindowFactsNeedsEveryCondition();
    void switchableWindowsDropHiddenShellWindows();
    void toggleOnlyCollapsesAnAlreadyActiveWindow();
    void onlyStateChangingOpsHaveTransitions();
    void workspaceExcludeListMatchesProcessNames();
    void manageableWindowsFollowTheRecordNotVisibility();
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

void TestWindowMatch::mainWindowFactsNeedsEveryCondition()
{
    core::TopLevelWindowFacts facts;
    facts.visible = true;
    facts.hasTitle = true;
    facts.hasArea = true;
    // 一个普通的顶层窗口：可见、无属主、有标题、尺寸非零。
    QVERIFY(core::isMainWindow(facts));

    // 每一条前置条件都是必需的。
    core::TopLevelWindowFacts hidden = facts;
    hidden.visible = false;
    QVERIFY(!core::isMainWindow(hidden));

    core::TopLevelWindowFacts shell = facts;
    shell.isShellWindow = true;
    QVERIFY(!core::isMainWindow(shell));

    core::TopLevelWindowFacts tool = facts;
    tool.toolWindow = true;
    QVERIFY(!core::isMainWindow(tool));

    core::TopLevelWindowFacts untitled = facts;
    untitled.hasTitle = false;
    QVERIFY(!core::isMainWindow(untitled));

    core::TopLevelWindowFacts empty = facts;
    empty.hasArea = false;
    QVERIFY(!core::isMainWindow(empty));

    // 有属主的是对话框 / 弹出窗口：除非它显式声明了 `WS_EX_APPWINDOW`
    // （任务栏与 Alt+Tab 的规则就是这样）。
    core::TopLevelWindowFacts dialog = facts;
    dialog.hasOwner = true;
    QVERIFY(!core::isMainWindow(dialog));
    dialog.appWindow = true;
    QVERIFY(core::isMainWindow(dialog));
}

void TestWindowMatch::switchableWindowsDropHiddenShellWindows()
{
    core::TopLevelWindowFacts facts;
    facts.visible = true;
    facts.hasTitle = true;
    facts.hasArea = true;
    QVERIFY(core::isSwitchableWindow(facts));

    // 被 cloaked 掉、又留在当前桌面上的窗口是 shell 藏起来的假窗口
    // （本机实测：`TextInputHost.exe` 的「Windows 输入体验」），不进切换器。
    core::TopLevelWindowFacts hidden = facts;
    hidden.cloaked = true;
    hidden.onCurrentDesktop = true;
    QVERIFY(!core::isSwitchableWindow(hidden));

    // 被 cloaked 但在**别的**虚拟桌面上的窗口要保留：切换器会切到那张桌面去
    // （这正是它与 Alt+Tab 不同的地方）。
    core::TopLevelWindowFacts elsewhere = facts;
    elsewhere.cloaked = true;
    elsewhere.onCurrentDesktop = false;
    QVERIFY(core::isSwitchableWindow(elsewhere));

    // 「主窗口」判据不理会 cloaking：`window_rule` 走的是那一条。
    QVERIFY(core::isMainWindow(hidden));

    // 被**工作空间**藏起来的窗口（`hiddenByUs`）：`IsWindowVisible` 是假的，
    // 但它仍然是用户自己的程序窗口，切换器必须能列出来（选中它先切到它那个
    // 工作空间再激活）。判据只能看状态机的记录（M0），不能看可见性。
    core::TopLevelWindowFacts byUs = facts;
    byUs.visible = false;
    byUs.hiddenByUs = true;
    QVERIFY(core::isSwitchableWindow(byUs));
    // 藏起来的窗口自己不是「主窗口」时（工具窗口 / 无标题）照样不进切换器。
    core::TopLevelWindowFacts byUsTool = byUs;
    byUsTool.toolWindow = true;
    QVERIFY(!core::isSwitchableWindow(byUsTool));
    // 「被我们藏着」+「shell 也 cloaked 掉、又在当前桌面」：两个排除条件互不影响，
    // 后者仍然赢。
    core::TopLevelWindowFacts byUsCloaked = byUs;
    byUsCloaked.cloaked = true;
    byUsCloaked.onCurrentDesktop = true;
    QVERIFY(!core::isSwitchableWindow(byUsCloaked));

    // 连「主窗口」都不算的窗口，当然也不进切换器。
    core::TopLevelWindowFacts tool = hidden;
    tool.toolWindow = true;
    QVERIFY(!core::isSwitchableWindow(tool));
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

    // 被工作空间藏起来的窗口**永不**收起：`SW_HIDE` 之后 `GetForegroundWindow()`
    // 仍然指着它（M0 实测），否则「唤起藏在别的工作空间里的窗口」会变成「最小化它」。
    QVERIFY(core::planWindowAction(WindowOp::Activate, std::nullopt, true, true)
            == WindowPlan::ApplyOp);
    QVERIFY(core::planWindowAction(WindowOp::Activate, std::optional<bool>(true), true, true)
            == WindowPlan::ApplyOp);
    // 只影响 activate：别的 op 本来就照做。
    QVERIFY(core::planWindowAction(WindowOp::Minimize, std::nullopt, true, true)
            == WindowPlan::ApplyOp);
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

/// `settings.workspace.exclude`：进程名子串、大小写无关；拿不到进程名时不排除。
void TestWindowMatch::workspaceExcludeListMatchesProcessNames()
{
    const QStringList exclude{QStringLiteral("game"), QStringLiteral("OBS")};
    QVERIFY(core::workspaceExcluded(std::optional<QString>(QStringLiteral("game.exe")), exclude));
    QVERIFY(core::workspaceExcluded(std::optional<QString>(QStringLiteral("obs64.exe")), exclude));
    QVERIFY(!core::workspaceExcluded(std::optional<QString>(QStringLiteral("code.exe")), exclude));
    // 空名单什么都不排除；拿不到属主进程名时也不能凭空排除。
    QVERIFY(!core::workspaceExcluded(std::optional<QString>(QStringLiteral("game.exe")), {}));
    QVERIFY(!core::workspaceExcluded(std::nullopt, exclude));
    QVERIFY(!core::workspaceExcluded(std::optional<QString>(QString()), exclude));
    // 空 needle 在 `--check` 阶段就会被拒；这里再兜一层（它不能把全部窗口排除掉）。
    QVERIFY(!core::workspaceExcluded(std::optional<QString>(QStringLiteral("game.exe")),
                                     {QString(), QStringLiteral("  ")}));
}

/// 「归工作空间管」的判据：记录（而不是 `IsWindowVisible`）决定一个窗口算不算。
void TestWindowMatch::manageableWindowsFollowTheRecordNotVisibility()
{
    using core::ManageableWindowFacts;
    const std::optional<QString> wezterm = QStringLiteral("wezterm-gui.exe");
    const QStringList noExclude;

    ManageableWindowFacts facts;
    facts.exists = true;
    facts.mainWindow = true;
    QVERIFY(core::isManageableWindow(facts, wezterm, noExclude));

    // 已被记录（可能正被我们 `SW_HIDE` 藏着）的窗口照旧归它管：
    // 平台层列的 `mainWindow` 本来就不要求可见（`isMainWindowIgnoringVisibility`）。
    ManageableWindowFacts tracked = facts;
    tracked.tracked = true;
    QVERIFY(core::isManageableWindow(tracked, wezterm, noExclude));

    // 已经没了的窗口 / flowkeyd 自己的窗口 / 任务栏与桌面 / 工具窗口般的窗口
    // 都不归它管。
    ManageableWindowFacts gone = facts;
    gone.exists = false;
    QVERIFY(!core::isManageableWindow(gone, wezterm, noExclude));

    ManageableWindowFacts own = facts;
    own.ownProcess = true;
    QVERIFY(!core::isManageableWindow(own, wezterm, noExclude));

    ManageableWindowFacts shell = facts;
    shell.shellWindow = true;
    QVERIFY(!core::isManageableWindow(shell, wezterm, noExclude));

    ManageableWindowFacts dialog = facts;
    dialog.mainWindow = false;
    QVERIFY(!core::isManageableWindow(dialog, wezterm, noExclude));

    // 排除名单命中就不管（与 `settings.workspace.exclude` 一致）。
    QVERIFY(!core::isManageableWindow(facts, wezterm,
                                      {QStringLiteral("wezterm")}));
}

QTEST_MAIN(TestWindowMatch)
#include "tst_window_match.moc"
