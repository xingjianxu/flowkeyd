// 动作工作线程：执行已经校验过的声明式动作。
//
// `run`/`open`/`wait` 都会阻塞几十毫秒到几秒，绝不能在钩子线程上做。
// 钩子线程把「哪个快捷键的哪一半触发了」排到这里，这里再决定做什么。
//
// `menu` / `help` 这种要弹窗的动作也只在这里被**请求**：窗口由 GUI 线程上的
// `app::PopupHost` 创建，用户选完之后再回到这个线程执行（见 `submitActions`）。
#pragma once

#include "core/app_list.h"
#include "core/config.h"
#include "core/engine.h"
#include "core/window_monitor_state.h"
#include "core/workspace.h"
#include "core/workspace_event.h"
#include "core/workspace_log.h"
#include "platform/win/hook.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

#include <cstdint>

class QTimer;

namespace flowkeyd::app {

class Runtime;

/// `monitor = "last"` 那份持久记忆（定义在 `dispatcher.cpp` 里）。
///
/// 它只是一份小状态 + 一个落盘位置，没必要把实现细节掳到头上，所以用一条前置
/// 声明 + `unique_ptr`（析构函数因此在 `.cpp` 里显式定义）。
struct MonitorMemory;

/// 跑在 `QThread` 上的执行器。
///
/// 自己不是线程：对象被 `moveToThread()` 到工作线程，`submit()` 从钩子线程
/// 用 `Qt::QueuedConnection` 投递（这是 AGENTS.md 第 6 节选定并写定的方案）。
class Dispatcher : public QObject
{
    Q_OBJECT

public:
    explicit Dispatcher(Runtime *runtime, QObject *parent = nullptr);
    ~Dispatcher() override;

    /// 线程安全：把一个已触发的快捷键排入工作线程队列。
    void submit(std::shared_ptr<const core::Compiled> config, core::Trigger trigger);

    /// 线程安全：执行一串已经展平的动作。
    ///
    /// `menu` 选单被选中时由 GUI 线程的弹窗回调调用；这里再投一次队列，
    /// 所以窗口侧永不阻塞，动作也始终在工作线程上跑。
    void submitActions(std::shared_ptr<const core::Compiled> config,
                       QString name,
                       std::vector<core::Action> actions);

    /// 线程安全：窗口出现 / 显示器重新接入 / 启动时按 `window_rule` 归位。
    void submitPlacement(std::shared_ptr<const core::Compiled> config,
                         platform::win::PlacementEvent event);

    /// 线程安全：把一个工作空间事件（窗口出现 / 销毁 / 最小化 / 位置变化 / 前台 /
    /// 显示器集合）排入工作线程队列。
    ///
    /// 工作空间状态机的**唯一所有者就是这条动作线程**（见 `plan.md` 2.2），
    /// 所以钩子线程只入队、不碰状态。
    void submitWorkspace(std::shared_ptr<const core::Compiled> config,
                         core::WorkspaceEvent event);

    /// 线程安全：把一段任意工作排入工作线程队列。
    ///
    /// 窗口切换器用它把「激活用户选中的那个窗口」拿回工作线程执行：弹窗的回调在
    /// GUI 线程上，而窗口后端（`window::applyTo`）只在动作线程上跑。
    void submitCall(std::function<void()> work);

    /// **只能在动作线程上调用**（`Runtime::start()` 用排队调用投进来）：先把
    /// 开始菜单扫一遍，这样第一次按启动器的快捷键不必现场等那几十毫秒。
    void startAppScan();

    /// `apps` 动作的程序目录：过期（或从没扫过）就重扫一次，然后返回它。
    /// **只能在动作线程上调用**（扫描会开一条一次性的 STA 线程）。
    const std::vector<core::AppEntry> &appCatalog();

    /// **只能在动作线程上调用**（`Runtime::start()` 用排队调用投进来）：起一个
    /// 定时器轮询「当前是第几号虚拟桌面 + 第几个工作空间」，变化时通知 GUI 线程去换
    /// 托盘图标。
    /// 为什么放在这条线程上：`desktop::currentDesktopIndex()` 要走一次性的 STA
    /// 线程 + shell COM，放在 GUI 线程上会白白阻塞 QML 渲染；工作空间状态机的
    /// 所有者也是这条线程（不能从别的线程读）。动作线程本来就在干这类事，而且
    /// 轮询偶尔被一个长动作拖后几百毫秒无伤大雅（托盘上的数字晚一点变而已）。
    void startDesktopWatch();

    /// **只能在动作线程上调用**：执行 `workspace("switch"|"move", N)`。
    ///
    /// 目标显示器 = 光标所在（`switch_target = "cursor"`）或前台窗口所在
    /// （`"focus"`）；`move` 的目标窗口 = 当前前台窗口。效果（先显后藏 → 聚焦）
    /// 由 `core::WorkspaceState` 算出，这里只负责翻译成 Win32 调用。
    void executeWorkspaceAction(const core::Compiled &config,
                                const core::Action &action,
                                const QString &hotkey);

    /// 线程安全：请动作线程把**全部被工作空间藏起来的窗口**放回可见
    /// （托盘菜单「恢复全部窗口」）。
    ///
    /// 异步：托盘要立刻消失，不能等动作线程。真正干活的是
    /// `restoreWorkspaceWindows()`。
    void submitWorkspaceRestore();

    // --- 工作空间与其它功能的联动（M9）---------------------------------------
    //
    // 下面三个都**只能在动作线程上调用**（它们碰状态机，而状态机只归这条线程）。

    /// 工作空间状态机记着的窗口句柄（`windows()` 切换器用它把被我们藏起来的窗口
    /// 也列出来）。
    std::vector<HWND> trackedWorkspaceWindows() const;

    /// 这个窗口此刻是不是**被工作空间藏着**（记录说它在非活动空间里）。
    ///
    /// `window("activate")` 靠它把「唤起」与「收起」分开：被藏着的窗口就算
    /// `GetForegroundWindow()` 还指着它（M0 实测），这一下也该是唤起。
    bool workspaceSaysHidden(const core::Compiled &config, HWND hwnd) const;

    /// 唤起前的准备：目标窗口可能正被藏在别的工作空间里，先把**它那块屏**切到它
    /// 所在的空间（窗口因此真的可见），再去激活它。
    ///
    /// 返回非空字符串 = 真的切了空间（一行日志用的说明）。**顺序不能反**：对一个
    /// `SW_HIDE` 着的窗口调 `raiseWindow()` 是没用的。
    QString revealWindowWorkspace(const core::Compiled &config, HWND hwnd);

    /// 窗口被挪到别的显示器之后（四个「挪窗口」op / `window("activate")` 的
    /// `monitor` / `window_rule` 摆屏），让它在我这儿的归属跟着走（M9）。
    ///
    /// 返回非空字符串 = 归属真的变了（一行日志用的说明）。
    QString followWindowMonitor(const core::Compiled &config, HWND hwnd);

    /// 线程安全：现在就恢复，**最多等 `timeoutMs`**（退出 / 注销 / 关机用）。
    ///
    /// 为什么要有上限：这些都在系统的关键路径上（注销时 Windows 只等几秒），
    /// 而动作线程可能正卡在一个 `run` 动作里。超时了就返回 false 并继续 —— 恢复
    /// 日志（`workspace.json`）里还留着记录，下次启动会再恢复一次。
    bool restoreWorkspaceNow(int timeoutMs);

private:
    void execute(const std::shared_ptr<const core::Compiled> &config, const core::Trigger &trigger);
    void runActions(const std::shared_ptr<const core::Compiled> &config,
                    const QString &name,
                    const std::vector<core::Action> &actions);
    void applyPlacementRules(const std::shared_ptr<const core::Compiled> &config,
                             const platform::win::PlacementEvent &event);

    // --- 工作空间（状态机 + 平台后端） --------------------------------------

    /// 处理一条工作空间事件（只在动作线程上跑）。
    void applyWorkspaceEvent(const std::shared_ptr<const core::Compiled> &config,
                             const core::WorkspaceEvent &event);
    /// 启动时把已经有窗口的窗口收编进各自的**活动**空间。
    void adoptExistingWindows(const core::Compiled &config);
    /// `settings.workspace.count` 变了（`reload`）时重建空间划分，并执行随之而来的
    /// 显隐（被合并进活动空间的窗口必须露出来）。
    void syncWorkspaceLayout(const core::Compiled &config);
    /// 刷新「当前虚拟桌面」这个上下文（按它分开口记，见 `plan.md` 的 Q1）。
    ///
    /// 每次都会开一条一次性的 STA 线程（虚拟桌面后端的要求），所以只在真的需要
    /// 时叫它：启动收编与新窗口出现。
    void refreshWorkspaceDesktop();
    /// 把状态机给出的效果交给平台后端执行（只在动作线程上跑）。
    void applyWorkspaceEffects(const core::WorkspaceEffects &effects, const QString &reason);
    /// `startDesktopWatch()` 与那个 500 ms 的定时器共用的轮询体。
    ///
    /// 一次算出**整份徽标内容**（`settings.tray_badge` + 工作空间号 + 虚拟桌面号），
    /// 有变化才投给 GUI 线程（见 `Runtime::reportDesktop`）。
    void pollDesktop();
    /// 记住最新的一份配置（只在动作线程上读写）。
    ///
    /// 为什么需要：500 ms 的轮询要读 `settings.tray_badge` 与
    /// `settings.workspace`，而它是自发的（不带着配置指针）；每个进来的任务
    /// （`submit*`）顺手把指针换成最新的一份就够了。
    void rememberConfig(const std::shared_ptr<const core::Compiled> &config);
    /// 徽标上的工作空间号该是多少（`std::nullopt` = 没有可显示的号）。
    ///
    /// 目标显示器与 `workspace("switch")` 完全一致（`settings.workspace.
    /// switch_target`：光标或前台窗口所在的那块屏）。只在动作线程上跑。
    std::optional<core::MonitorSpaces> workspaceSpacesForBadge();
    /// `workspace("switch")` 要作用在哪块显示器上（设备名，已规范化；空串 = 问不出来）。
    ///
    /// `switch_target = "cursor"` 看光标，`"focus"` 看前台窗口（前台是覆盖层时
    /// 跳过它，与 `window` 动作同一个找法，见决策 19）；`"focus"` 问不出来时退回
    /// 光标 —— 用户按了快捷键总得有点反应。
    ///
    /// `logFailures` 为 false 时**不**打日志：500 ms 轮询用它（否则光标 / 前台
    /// 一时问不出来就会每半秒刷一行）。
    QString workspaceSwitchMonitor(const core::Compiled &config, bool logFailures = true);
    /// 把一组窗口过滤成「在当前虚拟桌面上的」（一次批量 COM 查询，见决策 13）。
    ///
    /// 问不出来（被我们藏起来的窗口、打包应用宿主、COM 失败）的一律**保留**：
    /// 「不知道」不等于「在别的桌面上」。
    std::vector<core::WindowId> windowsOnThisDesktop(const std::vector<core::WindowId> &windows);

    /// 显示器拔插之后，把「记录说它在某块屏上、实际却不在那块屏上」的窗口物理上
    /// 搬回去（M7）。只在动作线程上跑。
    ///
    /// 两头的纷歧都由这一条规则盖住：
    ///   * 拔出：Windows 把窗口留在已经不存在的坐标上 → 搬到剩下的那块屏；
    ///   * 重新接入：状态机已经物归原主，窗口却不在那儿 → 搬回去。
    void relocateDisplacedWindows();

    /// 按 `window_rule` / `app` 的 `workspace` 字段给一批窗口定归属（M9）。
    ///
    /// 与 `window_rule` 的时机完全一致（窗口首次出现 / 显示器重新接入 / 启动），
    /// 因为就是在 `applyPlacementRules()` 的同一遍里调的：**只在这三个时机生效**，
    /// 用户后来自己 `workspace("move", N)` 搬走的窗口不会被规则拉回去。
    void applyWorkspaceRules(const core::Compiled &config, const std::vector<HWND> &windows);

    // --- 恢复日志（M8，只在动作线程上碰） ----------------------------------

    /// 第一次真的需要读写日志时把 `<配置目录>/workspace.json` 读进来。
    ///
    /// 启动恢复（`main.cpp`）已经把它清过一遍了；**这里读到的东西是它没能恢复
    /// 的那些**（进程卡住之类），所以要接着用，不能直接覆盖。
    void ensureWorkspaceLogLoaded();
    /// 把当前内存里的日志写回磁盘（空日志 = 删文件）。
    bool persistWorkspaceLog(const QString &reason);
    /// 把全部被藏起来的窗口放回可见（状态机 + 日志里剩下的那些）。只在动作线程上跑。
    void restoreWorkspaceWindows(const QString &reason);

    /// 重扫开始菜单（只能在动作线程上跑：它会开一条一次性的 STA 线程）。
    void refreshAppCatalog();

    Runtime *m_runtime = nullptr;

    /// 程序启动器的目录（`apps` 动作）：已经在动作线程上经过
    /// `core::prepareAppEntries()` 整理的条目，加上扫描时间戳。
    ///
    /// 为什么要缓存：扫描（枚举 `shell:AppsFolder` + 读每条的属性 + 过滤 + 去重排序）
    /// 在真机上大约 80 ms —— 每次弹出都现场扫一遍会让卡片等小半秒。**图标不在
    /// 这里**（每个约 3 ms，那是 `AppIconProvider` 的分内事）。
    std::vector<core::AppEntry> m_appCatalog;
    /// 上一次扫描的 `monotonicMs()`；`m_appScanned` 为 false 时无意义。
    std::uint64_t m_appScannedAt = 0;
    bool m_appScanned = false;
    /// 托盘数字图标的轮询定时器（在动作线程上创建与运行，见 `startDesktopWatch`）。
    QTimer *m_desktopTimer = nullptr;
    int m_desktopNumber = 0;
    int m_desktopCount = 0;
    /// 上一次轮询是不是失败（只在状态翻转时打一条 debug 日志，不要每 500 ms 写一行）。
    bool m_desktopWatchFailed = false;
    /// 最近一次投给托盘的徽标内容（变了才投）。
    core::TrayBadge m_badge;
    /// 最新的一份配置（`rememberConfig()` 换掉；轮询用它读设置）。
    std::shared_ptr<const core::Compiled> m_config;

    /// `monitor = "last"` 那份记忆（`<配置目录>/windows.json`）：**第一次真的
    /// 用到 `"last"` 时才建**（`"cursor"` / `"none"` 完全不碰这个文件）。
    std::unique_ptr<MonitorMemory> m_monitorMemory;

    // --- 工作空间（只在动作线程上碰） ---------------------------------------
    //
    // 状态机的**唯一所有者**就是这条线程，所以这里不需要任何锁（与钩子线程共享的
    // 只有 `Runtime::workspaceEvents()` 那张带锁的期望事件表）。
    core::WorkspaceState m_workspace;
    /// 状态机是不是已经按配置里的 `count` 建过了（`WorkspaceState` 的构造参数）。
    bool m_workspaceConfigured = false;
    /// 当前虚拟桌面键（`desktop::currentDesktopId()` 的缓存）。空串 = 还不知道。
    QString m_workspaceDesktop;
    /// 恢复日志（M8）：此刻被我们藏起来的窗口。
    ///
    /// **先写日志再隐藏**（写不进去就不藏），显示成功之后才把条目去掉。
    core::WorkspaceLog m_workspaceLog;
    /// 配置文件路径（日志就放在它旁边：`workspace.json`）；空串表示没地方可写
    /// （没有配置路径 = 测试环境）。
    QString m_workspaceConfigPath;
    bool m_workspaceLogLoaded = false;

    /// 上一条记忆的访问器（第一次调用时建出来，并记住配置目录旁边的落盘位置）。
    MonitorMemory *monitorMemory();
};

} // namespace flowkeyd::app
