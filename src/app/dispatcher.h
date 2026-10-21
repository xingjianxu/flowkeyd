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
    /// 定时器轮询「当前是第几号虚拟桌面」，变化时通知 GUI 线程去换托盘图标。
    /// 为什么放在这条线程上：`desktop::currentDesktopIndex()` 要走一次性的 STA
    /// 线程 + shell COM，放在 GUI 线程上会白白阻塞 QML 渲染。动作线程本来就在干
    /// 这类事，而且轮询偶尔被一个长动作拖后几百毫秒无伤大雅（托盘上的数字晚一点
    /// 变而已）。
    void startDesktopWatch();

    /// **只能在动作线程上调用**：执行 `workspace("switch"|"move", N)`。
    ///
    /// 目标显示器 = 光标所在（`switch_target = "cursor"`）或前台窗口所在
    /// （`"focus"`）；`move` 的目标窗口 = 当前前台窗口。效果（先显后藏 → 聚焦）
    /// 由 `core::WorkspaceState` 算出，这里只负责翻译成 Win32 调用。
    void executeWorkspaceAction(const core::Compiled &config,
                                const core::Action &action,
                                const QString &hotkey);

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
    void pollDesktop();
    /// `workspace("switch")` 要作用在哪块显示器上（设备名，已规范化；空串 = 问不出来）。
    ///
    /// `switch_target = "cursor"` 看光标，`"focus"` 看前台窗口（前台是覆盖层时
    /// 跳过它，与 `window` 动作同一个找法，见决策 19）；`"focus"` 问不出来时退回
    /// 光标 —— 用户按了快捷键总得有点反应。
    QString workspaceSwitchMonitor(const core::Compiled &config);
    /// 把一组窗口过滤成「在当前虚拟桌面上的」（一次批量 COM 查询，见决策 13）。
    ///
    /// 问不出来（被我们藏起来的窗口、打包应用宿主、COM 失败）的一律**保留**：
    /// 「不知道」不等于「在别的桌面上」。
    std::vector<core::WindowId> windowsOnThisDesktop(const std::vector<core::WindowId> &windows);

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

    /// 上一条记忆的访问器（第一次调用时建出来，并记住配置目录旁边的落盘位置）。
    MonitorMemory *monitorMemory();
};

} // namespace flowkeyd::app
