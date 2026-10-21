// 低级键盘钩子、它自己的 Win32 消息循环，以及拥有它们的钩子线程。
//
// ### 为什么回调必须快
//
// `WH_KEYBOARD_LL` 回调运行在装钩子的那个线程上，而 Windows 会强制
// `LowLevelHooksTimeout`（Windows 10+ 默认 300 ms）：超时的钩子会被**静默移除**，
// 于是守护进程看起来还活着，却什么都不做。所以回调只做
// 「解码事件 → 问引擎要决定 → 注入（很少、已预先拆好的）重映射按键 →
// 把动作排给工作线程」，然后立刻 `CallNextHookEx`。
//
// ### 为什么不装在 Qt 主线程上
//
// QML 渲染的一次慢帧、以及 `QSystemTrayIcon` 的模态菜单都可能让主线程
// 几十毫秒不回来；300 ms 的预算经不起这种抖动。独立线程还让钩子的生命周期
// 与 Qt 事件循环解耦，`quit` 时能确定地先卸钩子再退循环。
#pragma once

#include "core/config.h"
#include "core/engine.h"
#include "core/workspace_event.h"
#include "platform/win/ffi.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
namespace flowkeyd::platform::win {

enum class ControlCmd : unsigned {
    Quit = 1,
    Reload = 2,
    Suspend = 3,
    Resume = 4,
    ToggleSuspend = 5,
    Status = 6,
};

/// 投递给钩子线程的自定义消息（`wParam` 是 `ControlCmd`）。
inline constexpr UINT kControlMessage = WM_APP + 1;

/// 钩子线程观察到的、需要在工作线程上处理的「归位」事件（`window_rule`）。
struct PlacementEvent
{
    enum class Kind {
        /// 一批刚刚出现的顶层窗口（窗口句柄）。
        WindowsShown,
        /// 之前断开的显示器重新接上了：对当前所有窗口重跑一遍规则。
        MonitorConnected,
        /// flowkeyd 启动：对已有窗口跑一遍规则。
        Startup,
    };

    Kind kind = Kind::Startup;
    std::vector<HWND> windows;
};

class HookThread
{
public:
    /// 钩子线程在动作线程之前收到的通知：`config` 是那一刻生效的配置。
    using TriggerSink = std::function<void(const std::shared_ptr<const core::Compiled> &,
                                           const core::Trigger &)>;
    /// 窗口出现 / 显示器重新接入时，钩子线程把事件交给工作线程。
    using PlacementSink = std::function<void(const std::shared_ptr<const core::Compiled> &,
                                             const PlacementEvent &)>;
    /// 工作空间事件（出现 / 销毁 / 最小化 / 位置变化 / 前台 / 显示器集合）。
    ///
    /// 与 `PlacementSink` 分开：两者关心的东西不同（一个要摆位置，一个要维护
    /// 「哪个窗口在哪块屏的哪个空间」），而且工作空间默认**开启**却没有绑定时，
    /// 前者与后者各自可能都没事做。
    using WorkspaceSink = std::function<void(const std::shared_ptr<const core::Compiled> &,
                                             const core::WorkspaceEvent &)>;

    HookThread() = default;
    ~HookThread();

    HookThread(const HookThread &) = delete;
    HookThread &operator=(const HookThread &) = delete;

    /// 起线程、建消息队列、装钩子与窗口/显示器监听、开定时器。
    ///
    /// `expectedEvents` 是「我们自己引起的显隐事件」的抑制表，与动作工作线程共享
    /// （那边在执行效果前登记，这里在事件到达时命中即吞）。可以为空（测试）。
    /// 失败时 `error` 里是英文原因。
    bool start(std::shared_ptr<const core::Compiled> config,
               TriggerSink sink,
               PlacementSink placementSink,
               WorkspaceSink workspaceSink,
               std::shared_ptr<core::ExpectedWorkspaceEvents> expectedEvents,
               QString *error);

    /// 卸钩子、退消息循环并 `join`。可重复调用。
    void stop();

    DWORD threadId() const { return m_threadId.load(); }
    bool isRunning() const { return m_running.load(); }

    /// 线程安全：投一个控制命令给钩子线程。
    void postControl(ControlCmd cmd);

    /// 线程安全：换配置并请求重载（整体替换指针，绝不就地改）。
    void replaceConfig(std::shared_ptr<const core::Compiled> config);

    bool isSuspended() const { return m_suspended.load(); }
    std::uint64_t keyEvents() const { return m_keyEvents.load(); }
    std::uint64_t actions() const { return m_actions.load(); }
    void noteAction() { m_actions.fetch_add(1); }

    /// 只在钩子线程上调用。
    bool processHookEvent(WPARAM message, KBDLLHOOKSTRUCT *info);
    /// 只在钩子线程上调用：`SetWinEventHook` 的回调（窗口出现 / 销毁 / 显隐 / 位置 / 最小化）。
    void noteWindowEvent(DWORD event, HWND hwnd);
    /// 只在钩子线程上调用：前台窗口变了（`EVENT_SYSTEM_FOREGROUND`）。
    /// 这决定了「键盘现在在不在远程桌面里」（`hwnd` 为 `nullptr` = 没有前台窗口）。
    void noteForegroundWindow(HWND hwnd);

private:
    void threadMain(std::promise<QString> ready);
    void applyReaction(const core::Reaction &reaction);
    void handleTick();
    void handlePlacementTick();
    void handleControlCommand(ControlCmd cmd);
    void injectOps(std::vector<core::SendOp> ops);
    /// 按当前配置重新判定“在不在远程桌面里”，变化时通知引擎并写一条日志。
    void updateRemoteDesktop(const std::optional<QString> &executableName);

    // --- 工作空间事件管线 ---------------------------------------------------

    /// `settings.workspace.enabled`（钩子线程自己的缓存：事件回调里不碰锁、不碰配置指针）。
    bool workspaceEnabled() const;
    /// 按新配置装上 / 卸下工作空间专用的事件监听。只在钩子线程上调用。
    void updateWorkspaceHooks(bool enabled);
    /// 把一个工作空间事件交给工作线程。
    ///
    /// 三件事都在这里做：（a）关掉时直接丢；（b）命中期望事件表就吞掉（我们自己
    /// 显隐引起的 `EVENT_OBJECT_SHOW`）；（c）没装 sink 时直接丢。
    void emitWorkspaceEvent(core::WorkspaceEvent::Kind kind, HWND hwnd = nullptr);
    /// 显示器集合变化（拔出 / 接入都由这一个事件表达）。
    void emitMonitorsChanged();

    std::thread m_thread;
    std::atomic<DWORD> m_threadId{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_suspended{false};
    std::atomic<std::uint64_t> m_keyEvents{0};
    std::atomic<std::uint64_t> m_actions{0};

    std::shared_ptr<const core::Compiled> m_initialConfig;
    TriggerSink m_sink;
    PlacementSink m_placementSink;
    WorkspaceSink m_workspaceSink;
    /// 与动作工作线程共享的期望事件表（见 `core/workspace_event.h`）。
    std::shared_ptr<core::ExpectedWorkspaceEvents> m_expectedEvents;

    // 只在钩子线程上碰：
    std::unique_ptr<core::Engine> m_engine;
    HHOOK m_hook = nullptr;
    HWINEVENTHOOK m_winEventHook = nullptr;
    /// 前台窗口监听（`EVENT_SYSTEM_FOREGROUND`）：远程桌面放行的判据就是它。
    HWINEVENTHOOK m_foregroundHook = nullptr;
    /// 工作空间用的两个**窄范围**监听：位置变化、最小化开始/结束。
    ///
    /// 只在 `settings.workspace.enabled` 时装（默认开着但没有绑定时也只是两个
    /// 空回调）；**不要**合并成一个 `HIDE..LOCATIONCHANGE` 的大范围——那会把
    /// `SELECTION` / `STATECHANGE` 之类每次 UI 变动都算进去。
    HWINEVENTHOOK m_locationHook = nullptr;
    HWINEVENTHOOK m_minimizeHook = nullptr;
    /// 等你一个防抖周期再报的「位置变了」窗口（拖动一次会有几百条）。
    std::unordered_set<HWND> m_pendingMoves;
    /// 键盘现在是不是在远程桌面客户端里（钩子线程自己的缓存，见
    /// `updateRemoteDesktop()`）。
    bool m_remoteDesktop = false;
    /// 已经交给过工作线程的顶层窗口；`EVENT_OBJECT_DESTROY` 时移除，
    /// 这样句柄被系统复用时不会误判成“已经处理过”。
    std::unordered_set<HWND> m_knownWindows;
    /// 等待下一次 placement tick 处理的窗口。
    std::vector<HWND> m_pendingWindows;
    /// 上一次看到的显示器设备名（已规范化），用来发现“重新接入”。
    QStringList m_monitorDevices;
    bool m_monitorBaseline = false;
    /// `SetTimer(nullptr, …)` **返回的**定时器 id。
    ///
    /// 必须用返回值，不能用传进去的 `nIDEvent`：`hWnd` 为 `NULL` 时 Windows
    /// 会忽略 `nIDEvent` 并分配一个新 id，`WM_TIMER` 的 `wParam` 是那个新 id
    /// （实测 30661/30662 这种）。**这是原先漏掉的 bug**：引擎滴答定时器
    /// 一直没被认出来，长按重复从来不会发生。
    UINT_PTR m_tickTimerId = 0;
    UINT_PTR m_placementTimerId = 0;

    // 配置指针与工作线程共享时的保护：
    std::mutex m_configMutex;
    std::shared_ptr<const core::Compiled> m_pendingConfig;
};

} // namespace flowkeyd::platform::win
