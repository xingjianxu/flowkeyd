// 托盘图标与气泡通知。
//
// 这是 `QSystemTrayIcon`（QtWidgets）而不是 QML：托盘菜单要直接触发
// `ControlCmd`，走 C++ 更直接（见 AGENTS.md 第 10 节）。
//
// 菜单项与 oskeyd 一致：查看日志 / 挂起·恢复 / 重载配置 / 打开配置文件 / 版本 / 退出，
// 悬停提示显示构建版本与挂起状态，气泡提示用于 `notify` 动作与错误报告。
#pragma once

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
    explicit Tray(QObject *parent = nullptr);

    void show();
    /// 悬停提示与「挂起/恢复」菜单项跟着引擎状态走。
    void setSuspended(bool suspended);
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
    /// 把悬停提示重刷成当前构建版本 + 挂起状态。
    void updateToolTip();

    QSystemTrayIcon *m_icon = nullptr;
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
