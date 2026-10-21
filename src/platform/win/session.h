// 「会话即将结束」（关机 / 注销 / 重启）的通知。
//
// 工作空间用 `ShowWindow(SW_HIDE)` 藏窗口。会话结束时如果不先把它们放回来：
//   * 需求里明确写了「正常退出 / 关机前先恢复」；
//   * 应用收不到正常的关闭流程（它们是被我们藏着的），下次登录只能靠恢复日志
//     （`workspace.json`）兜底 —— 那是给崩溃留的后路，不该当常规路径用。
//
// 为什么自己建一个隐藏的顶层窗口：`WM_QUERYENDSESSION` 是发给进程的**顶层窗口**
// 的。挂到 Qt 的 `QAbstractNativeEventFilter` 上要先假设“此刻恰好有一个 Qt 窗口
// 活着”（弹窗预热失败、日志窗口关掉了就没有），而这里必须确定。窗口建在 GUI
// 线程上、消息由 Qt 的消息循环派发，所以不用自己 pump。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <functional>

namespace flowkeyd::platform::win {

/// 会话结束监听器。一个进程一个就够。
class SessionEndWatcher
{
public:
    SessionEndWatcher() = default;
    ~SessionEndWatcher();

    SessionEndWatcher(const SessionEndWatcher &) = delete;
    SessionEndWatcher &operator=(const SessionEndWatcher &) = delete;

    /// 建那个隐藏窗口并开始监听。
    ///
    /// **只在 GUI 线程、且在 `QApplication` 构造之后调用**（窗口创建必须在 DPI
    /// 感知就绪之后，见 `AGENTS.md` 第 7 节第 19 条）。`onSessionEnd` 会在 GUI
    /// 线程上被调用，而且是在 `WM_QUERYENDSESSION` 里 —— 系统只等几秒，所以回调
    /// 里必须用**有上限**的等待（见 `Runtime::restoreWorkspaceNow`）。
    bool start(std::function<void()> onSessionEnd, QString *error);
    /// 销毁窗口并丢掉回调（可以重复调用）。
    void stop();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void notify();

    HWND m_window = nullptr;
    std::function<void()> m_onSessionEnd;
};

} // namespace flowkeyd::platform::win
