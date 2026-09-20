#include "platform/win/tray.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

namespace flowkeyd::platform::win {

Tray::Tray(QObject *parent) : QObject(parent)
{
    m_icon = new QSystemTrayIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon), this);
    m_icon->setToolTip(QStringLiteral("flowkeyd"));

    m_menu = new QMenu();
    // 菜单文案是界面，用中文；日志与错误信息仍然保持英文（工作约定第 4 条）。
    // 顺序与 oskeyd 一致：日志放最上面（提权/隐藏控制台之后它是唯一能看运行
    // 情况的地方），挂起/重载/打开配置在中间，退出在最下面。
    m_logAction = m_menu->addAction(QStringLiteral("查看日志(&V)"));
    m_menu->addSeparator();
    m_suspendAction = m_menu->addAction(QStringLiteral("挂起快捷键(&S)"));
    m_reloadAction = m_menu->addAction(QStringLiteral("重载配置(&L)"));
    m_openConfigAction = m_menu->addAction(QStringLiteral("打开配置文件(&O)"));
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QStringLiteral("退出(&Q)"));

    m_icon->setContextMenu(m_menu);

    connect(m_logAction, &QAction::triggered, this, &Tray::logWindowRequested);
    connect(m_suspendAction, &QAction::triggered, this, &Tray::suspendToggleRequested);
    connect(m_reloadAction, &QAction::triggered, this, &Tray::reloadRequested);
    connect(m_openConfigAction, &QAction::triggered, this, &Tray::openConfigRequested);
    connect(m_quitAction, &QAction::triggered, this, &Tray::quitRequested);
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            emit logWindowRequested();
        }
    });
}

void Tray::show()
{
    m_icon->show();
}

void Tray::setSuspended(bool suspended)
{
    m_icon->setToolTip(suspended ? QStringLiteral("flowkeyd（已挂起）") : QStringLiteral("flowkeyd"));
    m_suspendAction->setText(suspended ? QStringLiteral("恢复快捷键(&R)")
                                       : QStringLiteral("挂起快捷键(&S)"));
}

void Tray::showMessage(const QString &title, const QString &body)
{
    m_icon->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

} // namespace flowkeyd::platform::win
