// 总装层：把 钩子线程 + 动作工作线程 + 配置生命周期 串起来。
//
// 这里拥有 `HookThread` 与 `QThread`，只做“路由”，绝不执行动作。
// 跨线程的方法都标了「线程安全」；Qt 对象（托盘、窗口）只在 GUI 线程上碰。
#pragma once

#include "app/popup_host.h"
#include "core/config.h"

#include <QObject>
#include <QString>

#include <memory>

class QThread;
class QWinEventNotifier;

namespace flowkeyd::platform::win {
class HookThread;
enum class ControlCmd : unsigned;
} // namespace flowkeyd::platform::win

namespace flowkeyd::app {

class Dispatcher;

class Runtime : public QObject
{
    Q_OBJECT

public:
    explicit Runtime(QObject *parent = nullptr);
    ~Runtime() override;

    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    /// 起钩子线程与动作线程。失败时 `error` 里是英文原因。
    bool start(const QString &configPath,
               std::shared_ptr<const core::Compiled> config,
               QString *error);

    /// 停动作线程与钩子线程（幂等）。只在 GUI 线程调用。
    void shutdown();

    const QString &configPath() const { return m_configPath; }
    platform::win::HookThread *hook() const { return m_hook.get(); }
    bool isSuspended() const;

    /// 把弹窗宿主交给运行时（GUI 线程、启动时调一次）。
    ///
    /// 弹窗只能在 GUI 线程上创建，而动作在工作线程上执行，所以 `menu`/`help`
    /// 必须经过这里中转；退出时 `shutdown()` 会先把弹窗关掉，
    /// 免得留下会碰到已销毁 `Dispatcher` 的回调。
    void setPopupHost(PopupHost *host) { m_popupHost = host; }

    // ---- 以下可从任意线程调用 ----

    /// 给钩子线程投一个控制命令。
    void postControl(platform::win::ControlCmd cmd);
    /// 重新读取配置并整体替换给钩子线程（失败时保留旧配置）。
    void reloadFromAnyThread();
    /// 请求干净退出（投递到 GUI 线程执行）。
    void requestShutdownFromAnyThread();
    /// 弹一个托盘气泡（投递到 GUI 线程）。
    void notifyFromAnyThread(const QString &title, const QString &body);
    /// 工作线程调用：把动作触发的挂起状态变化广播给 GUI 线程（托盘提示用）。
    void reportSuspended(bool suspended);
    /// 工作线程调用：在 GUI 线程上弹出选单。
    void showMenuFromAnyThread(MenuRequest request);
    /// 工作线程调用：在 GUI 线程上弹出帮助窗口。
    void showHelpFromAnyThread(HelpRequest request);

signals:
    void notificationRequested(const QString &title, const QString &body);
    void reloadFinished(const QString &summary);
    void suspendedChanged(bool suspended);
    /// 退出流程已经走完（钩子卸掉、动作线程停下）。
    void finished();

private:
    void performShutdown();

    QString m_configPath;
    std::unique_ptr<platform::win::HookThread> m_hook;
    PopupHost *m_popupHost = nullptr;
    QThread *m_workerThread = nullptr;
    Dispatcher *m_dispatcher = nullptr;
    /// `--quit` 的退出事件（`HANDLE`；这里不引入 `windows.h`，见 runtime.cpp）。
    void *m_quitEvent = nullptr;
    QWinEventNotifier *m_quitNotifier = nullptr;
    bool m_shuttingDown = false;
    bool m_started = false;
};

} // namespace flowkeyd::app
