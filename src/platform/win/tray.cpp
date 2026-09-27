#include "platform/win/tray.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

namespace flowkeyd::platform::win {

Tray::Tray(const QIcon &icon, QObject *parent) : QObject(parent)
{
    m_icon = new QSystemTrayIcon(icon, this);
    if (m_icon->icon().isNull()) {
        // 应用图标的资源没能加载（正常不会发生）：退回系统图标，
        // 总比托盘上什么都没有强。
        m_icon->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    }
    m_appIcon = m_icon->icon();
    m_buildVersion = QStringLiteral("unknown");
    updateToolTip();

    m_menu = new QMenu();
    // 菜单文案是界面，用中文；日志与错误信息仍然保持英文（工作约定第 4 条）。
    // 顺序与 oskeyd 一致：日志放最上面（提权/隐藏控制台之后它是唯一能看运行
    // 情况的地方），挂起/重载/打开配置在中间，退出在最下面。
    m_logAction = m_menu->addAction(QStringLiteral("查看日志(&V)"));
    m_menu->addSeparator();
    m_suspendAction = m_menu->addAction(QStringLiteral("挂起快捷键(&S)"));
    m_reloadAction = m_menu->addAction(QStringLiteral("重载配置(&L)"));
    m_openConfigAction = m_menu->addAction(QStringLiteral("打开配置文件(&O)"));
    // 在线更新：查 GitHub Release，有新版本就弹更新窗口（下载 + 替换 + 重启）。
    m_updateAction = m_menu->addAction(QStringLiteral("检查更新(&U)..."));
    m_menu->addSeparator();
    // 构建版本：一个不可点的信息项（真实取值由启动时调 setBuildVersion 传进来，
    // 见 core/version.h）。放在「退出」上面，和系统菜单里的「关于」条目一个位置。
    m_versionAction = m_menu->addAction(QStringLiteral("版本 %1").arg(m_buildVersion));
    m_versionAction->setEnabled(false);
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QStringLiteral("退出(&Q)"));

    m_icon->setContextMenu(m_menu);

    connect(m_logAction, &QAction::triggered, this, &Tray::logWindowRequested);
    connect(m_suspendAction, &QAction::triggered, this, &Tray::suspendToggleRequested);
    connect(m_reloadAction, &QAction::triggered, this, &Tray::reloadRequested);
    connect(m_openConfigAction, &QAction::triggered, this, &Tray::openConfigRequested);
    connect(m_updateAction, &QAction::triggered, this, &Tray::checkUpdateRequested);
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
    m_suspended = suspended;
    m_suspendAction->setText(suspended ? QStringLiteral("恢复快捷键(&R)")
                                       : QStringLiteral("挂起快捷键(&S)"));
    updateToolTip();
}

void Tray::setDesktop(int number, int count, const QIcon &badgeIcon)
{
    m_desktopNumber = number;
    m_desktopCount = count;
    // 读不到当前桌面（或调用方没做徽标）时退回应用图标：宁可看不出是第几号，
    // 也不要画一个误导性的 `0`，更不要留一个空白图标。
    m_icon->setIcon(number > 0 && !badgeIcon.isNull() ? badgeIcon : m_appIcon);
    updateToolTip();
}

void Tray::setBuildVersion(const QString &version)
{
    m_buildVersion = version;
    m_versionAction->setText(QStringLiteral("版本 %1").arg(version));
    updateToolTip();
}

void Tray::updateToolTip()
{
    QString tip = QStringLiteral("flowkeyd %1").arg(m_buildVersion);
    if (m_desktopNumber > 0 && m_desktopCount > 0) {
        tip += QStringLiteral("（桌面 %1/%2）").arg(m_desktopNumber).arg(m_desktopCount);
    }
    if (m_suspended) {
        tip += QStringLiteral("（已挂起）");
    }
    m_icon->setToolTip(tip);
}

void Tray::showMessage(const QString &title, const QString &body)
{
    m_icon->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

} // namespace flowkeyd::platform::win
