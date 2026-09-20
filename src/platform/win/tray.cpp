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
    m_logAction = m_menu->addAction(QStringLiteral("查看日志"));
    m_suspendAction = m_menu->addAction(QStringLiteral("挂起"));
    m_suspendAction->setEnabled(false); // stage 3 接上引擎之后再启用
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QStringLiteral("退出"));

    m_icon->setContextMenu(m_menu);

    connect(m_logAction, &QAction::triggered, this, &Tray::logWindowRequested);
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
    m_suspendAction->setText(suspended ? QStringLiteral("恢复") : QStringLiteral("挂起"));
}

void Tray::showMessage(const QString &title, const QString &body)
{
    m_icon->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

} // namespace flowkeyd::platform::win
