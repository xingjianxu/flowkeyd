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

    HookThread() = default;
    ~HookThread();

    HookThread(const HookThread &) = delete;
    HookThread &operator=(const HookThread &) = delete;

    /// 起线程、建消息队列、装钩子与窗口/显示器监听、开定时器。
    /// 失败时 `error` 里是英文原因。
    bool start(std::shared_ptr<const core::Compiled> config,
               TriggerSink sink,
               PlacementSink placementSink,
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
    /// 只在钩子线程上调用：`SetWinEventHook` 的回调（窗口出现 / 销毁）。
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

    std::thread m_thread;
    std::atomic<DWORD> m_threadId{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_suspended{false};
    std::atomic<std::uint64_t> m_keyEvents{0};
    std::atomic<std::uint64_t> m_actions{0};

    std::shared_ptr<const core::Compiled> m_initialConfig;
    TriggerSink m_sink;
    PlacementSink m_placementSink;

    // 只在钩子线程上碰：
    std::unique_ptr<core::Engine> m_engine;
    HHOOK m_hook = nullptr;
    HWINEVENTHOOK m_winEventHook = nullptr;
    /// 前台窗口监听（`EVENT_SYSTEM_FOREGROUND`）：远程桌面放行的判据就是它。
    HWINEVENTHOOK m_foregroundHook = nullptr;
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
