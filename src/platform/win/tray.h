// 托盘图标与气泡通知。
//
// 这是 `QSystemTrayIcon`（QtWidgets）而不是 QML：托盘菜单要直接触发
// `ControlCmd`，走 C++ 更直接（见 AGENTS.md 第 10 节）。
//
// 菜单项与 oskeyd 一致：查看日志 / 挂起·恢复 / 重载配置 / 打开配置文件 / 版本 / 退出，
// 悬停提示显示构建版本、当前虚拟桌面与挂起状态，气泡提示用于 `notify` 动作与错误报告。
// 图标本身平时是应用图标，但守护进程读到当前桌面后会换成**数字徽标**（第几号桌面，
// 项目所有者 2026-09 要求），见 `setDesktop` 与 `app::desktopIcon`。
#pragma once

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
    /// 托盘图标改成「当前是第几号虚拟桌面」（项目所有者 2026-09 要求）。
    ///
    /// `number >= 1` 时用 `badgeIcon`（由 `app::desktopIcon()` 画出来的数字徽标），
    /// 悬停提示里多一段 `（桌面 N/M）`；`number <= 0`（读不到当前桌面）时退回
    /// 启动时传进来的应用图标。`badgeIcon` 为空也退回应用图标（调用方没做徽标）。
    ///
    /// **只在 GUI 线程调用**（它改 `QSystemTrayIcon`）。
    void setDesktop(int number, int count, const QIcon &badgeIcon);
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
    /// 「打开配置文件」被点击。
    void openConfigRequested();
    void quitRequested();

private:
    /// 把悬停提示重刷成当前构建版本 + 当前虚拟桌面 + 挂起状态。
    void updateToolTip();

    QSystemTrayIcon *m_icon = nullptr;
    /// 启动时传进来的应用图标：读不到虚拟桌面时用它。
    QIcon m_appIcon;
    /// 当前是第几号虚拟桌面（`0` = 不知道）与桌面的总数，只用于悬停提示。
    int m_desktopNumber = 0;
    int m_desktopCount = 0;
    QMenu *m_menu = nullptr;
    QAction *m_logAction = nullptr;
    QAction *m_suspendAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_openConfigAction = nullptr;
    QAction *m_versionAction = nullptr;
    QAction *m_quitAction = nullptr;
    QString m_buildVersion;
    bool m_suspended = false;
};

} // namespace flowkeyd::platform::win
