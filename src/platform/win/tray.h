// 托盘图标（stage 4 会把右键菜单补全：查看日志 / 挂起·恢复 / 重载配置 /
// 打开配置文件 / 退出）。stage 0 只需要「图标可见 + 左键打开日志窗口 + 退出」。
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
    /// 弹一个气泡提示（`notify` 动作与错误报告都用它）。
    void showMessage(const QString &title, const QString &body);

signals:
    /// 左键单击托盘图标。
    void logWindowRequested();
    void quitRequested();

private:
    QSystemTrayIcon *m_icon = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_logAction = nullptr;
    QAction *m_suspendAction = nullptr;
    QAction *m_quitAction = nullptr;
};

} // namespace flowkeyd::platform::win
