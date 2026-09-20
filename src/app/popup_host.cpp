#include "app/popup_host.h"

#include "platform/win/logging.h"
#include "platform/win/window.h"

#include <QCursor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QVariant>

#include <utility>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

namespace {

/// 真的把窗口画出来、抢到键盘焦点。
///
/// 钩子守护进程通常**不持有前台锁**，只调 `requestActivate()` 会被拒绝，于是
/// 窗口弹出来了却收不到键盘——这正是 AGENTS.md 第 7 节第 17 条要解决的问题。
/// `win::window::raiseWindow` 就是那条“递进式绕行 + `AttachThreadInput` 配平”
/// 的实现，与 `window` 动作共用。
void activateWindow(QQuickWindow *window)
{
    window->raise();
    window->requestActivate();
    if (window->isActive()) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (win::window::raiseWindow(hwnd)) {
        window->requestActivate();
    } else {
        win::logDebug(QStringLiteral("the popup could not take the foreground; it can still be clicked"));
    }
}

/// 把窗口居中放到鼠标所在的那块显示器上（拿不到就是主显示器）。
void centreOnCursorScreen(QQuickWindow *window, int width, int height)
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return;
    }
    const QRect work = screen->availableGeometry();
    const QRect bounds = screen->geometry();
    const PopupPoint point = centrePopup(PopupRect{work.x(), work.y(), work.width(), work.height()},
                                         PopupRect{bounds.x(), bounds.y(), bounds.width(), bounds.height()},
                                         width,
                                         height);
    window->setPosition(point.x, point.y);
}

} // namespace

PopupHost::PopupHost(QQmlEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
}

void PopupHost::requestMenu(MenuRequest request)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, request]() { showMenu(request); }, Qt::QueuedConnection);
        return;
    }
    showMenu(std::move(request));
}

void PopupHost::requestHelp(HelpRequest request)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, request]() { showHelp(request); }, Qt::QueuedConnection);
        return;
    }
    showHelp(std::move(request));
}

void PopupHost::closeAll()
{
    if (m_menuWindow != nullptr) {
        m_menuWindow->setProperty("visible", false);
    }
    m_menuRequest = MenuRequest{};
    if (m_helpWindow != nullptr) {
        m_helpWindow->setProperty("visible", false);
    }
    m_helpRequest = HelpRequest{};
}

bool PopupHost::menuVisible() const
{
    return m_menuWindow != nullptr && m_menuWindow->isVisible();
}

bool PopupHost::helpVisible() const
{
    return m_helpWindow != nullptr && m_helpWindow->isVisible();
}

void PopupHost::menuChoose(int index)
{
    if (m_menuWindow == nullptr) {
        return;
    }
    // 先把窗口藏起来再交出去：回调可能立刻触发睡眠/关机之类的动作，
    // 用户不该还看着残影（与 oskeyd 的 `State::choose` 一致）。
    m_menuWindow->setProperty("visible", false);
    MenuRequest request = std::move(m_menuRequest);
    m_menuRequest = MenuRequest{};
    if (request.onChoose && index >= 0 && index < static_cast<int>(request.items.size())) {
        request.onChoose(index);
    }
}

void PopupHost::menuDismiss()
{
    if (m_menuWindow != nullptr) {
        m_menuWindow->setProperty("visible", false);
    }
    m_menuRequest = MenuRequest{};
}

void PopupHost::helpCopy(int index)
{
    if (m_helpModel == nullptr) {
        return;
    }
    // 帮助窗口复制完不关：用户可能还要抄下一条（与 oskeyd 一致）。
    const QString text = m_helpModel->copyTextForVisible(index);
    if (text.isEmpty()) {
        return;
    }
    if (m_helpRequest.onCopy) {
        m_helpRequest.onCopy(text);
    }
}

void PopupHost::helpDismiss()
{
    if (m_helpWindow != nullptr) {
        m_helpWindow->setProperty("visible", false);
    }
    m_helpRequest = HelpRequest{};
}

void PopupHost::showMenu(MenuRequest request)
{
    QQuickWindow *window = ensureMenuWindow();
    if (window == nullptr) {
        return;
    }
    const bool wasVisible = window->isVisible();
    m_menuRequest = std::move(request);
    m_menuModel->setItems(m_menuRequest.title, m_menuRequest.items);
    window->setProperty("visible", true);
    if (!wasVisible) {
        // 位置只在第一次算：再按一次快捷键只是把选单前置，不该让它跳来跳去。
        centreOnCursorScreen(window, m_menuModel->cardWidth(), m_menuModel->cardHeight());
    }
    activateWindow(window);
}

void PopupHost::showHelp(HelpRequest request)
{
    QQuickWindow *window = ensureHelpWindow();
    if (window == nullptr) {
        return;
    }
    const bool wasVisible = window->isVisible();
    m_helpRequest = std::move(request);
    m_helpModel->setItems(m_helpRequest.title, m_helpRequest.items);
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    m_helpModel->setMaxRows(screen != nullptr
                                ? HelpModel::rowsForAvailableHeight(screen->availableGeometry().height())
                                : 12);
    window->setProperty("visible", true);
    if (!wasVisible) {
        centreOnCursorScreen(window, m_helpModel->cardWidth(), m_helpModel->cardHeight());
    }
    activateWindow(window);
}

QQuickWindow *PopupHost::ensureMenuWindow()
{
    if (m_menuWindow != nullptr) {
        return m_menuWindow;
    }
    QQmlComponent component(m_engine);
    component.loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("MenuPopup"));
    if (component.isError()) {
        win::logError(QStringLiteral("could not load MenuPopup.qml: %1").arg(component.errorString()));
        return nullptr;
    }
    QObject *object = component.create();
    m_menuWindow = qobject_cast<QQuickWindow *>(object);
    if (m_menuWindow == nullptr) {
        delete object;
        win::logError(QStringLiteral("MenuPopup.qml did not create a window"));
        return nullptr;
    }
    m_menuModel = new MenuModel(this);
    m_menuWindow->setProperty("menuModel", QVariant::fromValue(static_cast<QObject *>(m_menuModel)));
    m_menuWindow->setProperty("host", QVariant::fromValue(static_cast<QObject *>(this)));
    return m_menuWindow;
}

QQuickWindow *PopupHost::ensureHelpWindow()
{
    if (m_helpWindow != nullptr) {
        return m_helpWindow;
    }
    QQmlComponent component(m_engine);
    component.loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("HelpPopup"));
    if (component.isError()) {
        win::logError(QStringLiteral("could not load HelpPopup.qml: %1").arg(component.errorString()));
        return nullptr;
    }
    QObject *object = component.create();
    m_helpWindow = qobject_cast<QQuickWindow *>(object);
    if (m_helpWindow == nullptr) {
        delete object;
        win::logError(QStringLiteral("HelpPopup.qml did not create a window"));
        return nullptr;
    }
    m_helpModel = new HelpModel(this);
    m_helpWindow->setProperty("helpModel", QVariant::fromValue(static_cast<QObject *>(m_helpModel)));
    m_helpWindow->setProperty("host", QVariant::fromValue(static_cast<QObject *>(this)));
    return m_helpWindow;
}

} // namespace flowkeyd::app
