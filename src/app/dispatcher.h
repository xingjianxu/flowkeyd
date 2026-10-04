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
#include "platform/win/hook.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QTimer;

namespace flowkeyd::app {

class Runtime;

/// 跑在 `QThread` 上的执行器。
///
/// 自己不是线程：对象被 `moveToThread()` 到工作线程，`submit()` 从钩子线程
/// 用 `Qt::QueuedConnection` 投递（这是 AGENTS.md 第 6 节选定并写定的方案）。
class Dispatcher : public QObject
{
    Q_OBJECT

public:
    explicit Dispatcher(Runtime *runtime, QObject *parent = nullptr);

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
    ///
    /// 为什么放在这条线程上：`desktop::currentDesktopIndex()` 要走一次性的 STA
    /// 线程 + shell COM，放在 GUI 线程上会白白阻塞 QML 渲染。动作线程本来就在干
    /// 这类事，而且轮询偶尔被一个长动作拖后几百毫秒无伤大雅（托盘上的数字晚一点
    /// 变而已）。
    void startDesktopWatch();

private:
    void execute(const std::shared_ptr<const core::Compiled> &config, const core::Trigger &trigger);
    void runActions(const std::shared_ptr<const core::Compiled> &config,
                    const QString &name,
                    const std::vector<core::Action> &actions);
    void applyPlacementRules(const std::shared_ptr<const core::Compiled> &config,
                             const platform::win::PlacementEvent &event);
    /// `startDesktopWatch()` 与那个 500 ms 的定时器共用的轮询体。
    void pollDesktop();

    /// 重扫开始菜单（只能在动作线程上跑：它会开一条一次性的 STA 线程）。
    void refreshAppCatalog();

    Runtime *m_runtime = nullptr;

    /// 程序启动器的目录（`apps` 动作）：已经在动作线程上经过
    /// `core::prepareAppEntries()` 整理的条目，加上扫描时间戳。
    ///
    /// 为什么要缓存：扫描（递归目录 + `IShellLink` 解析 123 个快捷方式）在真机
    /// 上要 80 ms 左右 —— 每次弹出都现场扫一遍会让卡片等小半秒。**图标不在
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
};

} // namespace flowkeyd::app
