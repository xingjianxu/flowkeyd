// 托盘图标与气泡通知。
//
// 这是 `QSystemTrayIcon`（QtWidgets）而不是 QML：托盘菜单要直接触发
// `ControlCmd`，走 C++ 更直接（见 AGENTS.md 第 10 节）。
//
// 菜单项：查看日志 / 挂起·恢复 / 恢复全部窗口 / 重载配置 / **配置...** / 打开配置文件 /
// 检查更新 / 版本 / 退出，
// 悬停提示显示构建版本、当前工作空间 / 虚拟桌面与挂起状态，气泡提示用于 `notify` 动作与错误报告。
// 图标本身平时是应用图标，但守护进程读到当前桌面 / 工作空间后会换成**数字徽标**
// （默认 `1/2` 的写法：工作空间号 / 虚拟桌面号，项目所有者 2026-09 与 2026-10 要求），
// 见 `setDesktop` 与 `app::trayBadgeIcon`。
#pragma once

#include "core/desktop_badge.h"

#include <QIcon>
#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace flowkeyd::platform::win {

class Tray : public QObject
{
    Q_OBJECT

public:
    /// `icon` 是托盘图标（应用图标，见 `app/app_icon.h`）；传空 QIcon 时退回
    /// 系统图标，不会留下一个看不见的托盘图标。
    explicit Tray(const QIcon &icon, QObject *parent = nullptr);

    void show();
    /// 悬停提示与「挂起/恢复」菜单项跟着引擎状态走。
    void setSuspended(bool suspended);
    /// 托盘图标改成带数字的徽标（第几号虚拟桌面 / 第几个工作空间，项目所有者
    /// 2026-09 与 2026-10 的要求）。
    ///
    /// `badge` 里带着**该显示哪几个号**（`settings.tray_badge`）与两个号本身；
    /// 图标由调用方用 `app::trayBadgeIcon()` 现画好传进来（GUI 线程上画）。
    /// 悬停提示里多一段 `（工作空间 W/N，桌面 D/M）`；没有号可画（`badge` 里两个
    /// 号都是 `0`）或 `badgeIcon` 为空时退回启动时传进来的应用图标。
    ///
    /// **只在 GUI 线程调用**（它改 `QSystemTrayIcon`）。
    void setDesktop(const core::TrayBadge &badge, const QIcon &badgeIcon);
    /// 设置菜单与悬停提示里显示的构建版本（`core::buildVersion()` 的结果）。
    void setBuildVersion(const QString &version);
    /// 弹一个气泡提示（`notify` 动作与错误报告都用它）。
    void showMessage(const QString &title, const QString &body);

signals:
    /// 左键单击托盘图标，或菜单里的「查看日志」。
    void logWindowRequested();
    /// 「挂起/恢复」被点击（当前状态由 `setSuspended` 维护）。
    void suspendToggleRequested();
    /// 「重载配置」被点击。
    void reloadRequested();
    /// 「配置...」被点击（图形化配置界面）。
    void configureRequested();
    /// 「打开配置文件」被点击。
    void openConfigRequested();
    /// 「检查更新」被点击（在线更新：查 GitHub Release、下载、替换并重启）。
    void checkUpdateRequested();
    /// 「恢复全部窗口」被点击（工作空间藏起来的窗口立刻全部放回来）。
    ///
    /// 这是安全兜底（M8）：正常退出 / 注销 / 断电重启都会自己恢复，但万一用户
    /// 觉得「窗口不见了」，这一条菜单项不用改配置、不用重载就能把窗口全部放回来。
    void restoreWindowsRequested();
    void quitRequested();

private:
    /// 把悬停提示重刷成当前构建版本 + 当前虚拟桌面 + 挂起状态。
    void updateToolTip();

    QSystemTrayIcon *m_icon = nullptr;
    /// 启动时传进来的应用图标：读不到虚拟桌面时用它。
    QIcon m_appIcon;
    /// 当前徽标的内容（显示模式 + 两个号与它们的总数），悬停提示用。
    core::TrayBadge m_badge;
    QMenu *m_menu = nullptr;
    QAction *m_logAction = nullptr;
    QAction *m_suspendAction = nullptr;
    QAction *m_restoreAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_configureAction = nullptr;
    QAction *m_openConfigAction = nullptr;
    QAction *m_updateAction = nullptr;
    QAction *m_versionAction = nullptr;
    QAction *m_quitAction = nullptr;
    QString m_buildVersion;
    bool m_suspended = false;
};

} // namespace flowkeyd::platform::win
