#include "app/popup_host.h"

#include "platform/win/ime.h"
#include "platform/win/logging.h"
#include "platform/win/window.h"

#include <QCursor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <memory>
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
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const bool qtActive = window->isActive();
    const bool foreground = GetForegroundWindow() == hwnd;
    win::logDebug(QStringLiteral("popup activation: qtActive=%1 foreground=%2")
                      .arg(qtActive)
                      .arg(foreground));
    if (qtActive && foreground) {
        return;
    }
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

void PopupHost::requestSwitch(SwitchRequest request)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, request]() { showSwitch(request); }, Qt::QueuedConnection);
        return;
    }
    showSwitch(std::move(request));
}

void PopupHost::requestUpdate(UpdateRequest request)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, request]() { requestUpdate(request); }, Qt::QueuedConnection);
        return;
    }
    m_updateRequest = std::move(request);
    showUpdate();
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
    if (m_switchWindow != nullptr) {
        m_switchWindow->setProperty("visible", false);
    }
    m_switchRequest = SwitchRequest{};
    // 「在线更新」窗口也归这里管：退出时它必须消失，而它里面的回调会碰
    // `Updater`（与 `onChoose` 碰 `Dispatcher` 同理）。
    if (m_updateWindow != nullptr) {
        m_updateWindow->setProperty("visible", false);
    }
    m_updateRequest = UpdateRequest{};
    restoreSwitchInputMode();
}

bool PopupHost::menuVisible() const
{
    return m_menuWindow != nullptr && m_menuWindow->isVisible();
}

bool PopupHost::helpVisible() const
{
    return m_helpWindow != nullptr && m_helpWindow->isVisible();
}

bool PopupHost::switchVisible() const
{
    return m_switchWindow != nullptr && m_switchWindow->isVisible();
}

bool PopupHost::updateVisible() const
{
    return m_updateWindow != nullptr && m_updateWindow->isVisible();
}

void PopupHost::hideUpdate()
{
    if (m_updateWindow != nullptr) {
        m_updateWindow->setProperty("visible", false);
    }
}

void PopupHost::menuChoose(int index)
{
    if (m_menuWindow == nullptr) {
        return;
    }
    // 先把窗口藏起来再交出去：回调可能立刻触发睡眠/关机之类的动作，
    // 用户不该还看着残影。
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
    // 帮助窗口复制完不关：用户可能还要抄下一条。
    const QString text = m_helpModel->copyTextForVisible(index);
    if (text.isEmpty()) {
        return;
    }
    if (m_helpRequest.onCopy) {
        m_helpRequest.onCopy(text);
    }
}

void PopupHost::helpRun(int index)
{
    if (m_helpWindow == nullptr || m_helpModel == nullptr) {
        return;
    }
    // `index` 是**可见行**下标（筛选之后「第几行」与「第几个条目」不是一回事），
    // 而回调那边拿到的是原始条目列表 —— 这里先换算，否则筛选状态下执行的就是
    // 另一条的动作（这个 bug 是验收脚本的「点选之后 Enter 执行的就是刚点中的
    // 那一行」与「第二次 Enter 真的执行」两条检查查出来的）。
    const std::optional<int> item = m_helpModel->itemIndexForVisible(index);
    // 先把窗口藏起来再交出去（与 `menuChoose` 一致，理由也一样）：
    //   * `send`/`type`/`window` 这类动作作用在**前台窗口**上，而窗口刚才是前台；
    //   * 可能立刻触发睡眠/关机之类的动作，用户不该还看着残影。
    // 隐藏之后 Windows 会把前台交还给下一个窗口（通常就是用户原来那个应用）。
    m_helpWindow->setProperty("visible", false);
    HelpRequest request = std::move(m_helpRequest);
    m_helpRequest = HelpRequest{};
    if (request.onRun && item.has_value()) {
        request.onRun(*item);
    }
}

void PopupHost::helpDismiss()
{
    if (m_helpWindow != nullptr) {
        m_helpWindow->setProperty("visible", false);
    }
    m_helpRequest = HelpRequest{};
}

void PopupHost::switchChoose(int index)
{
    if (m_switchWindow == nullptr) {
        return;
    }
    // 先把窗口藏起来再交出去（与 `menuChoose` / `helpRun` 一致）：激活动作作用在
    // **前台窗口**上，不先关窗就会把前台算成切换器自己。
    m_switchWindow->setProperty("visible", false);
    // 关掉卡片就把输入法还回打开前的样子（用户不希望它留下痕迹）。
    restoreSwitchInputMode();
    SwitchRequest request = std::move(m_switchRequest);
    m_switchRequest = SwitchRequest{};
    if (request.onChoose && index >= 0 && index < static_cast<int>(request.items.size())) {
        request.onChoose(index);
    }
}

void PopupHost::switchDismiss()
{
    if (m_switchWindow != nullptr) {
        m_switchWindow->setProperty("visible", false);
    }
    m_switchRequest = SwitchRequest{};
    restoreSwitchInputMode();
}

void PopupHost::updateInstall()
{
    if (m_updateRequest.onInstall) {
        m_updateRequest.onInstall();
    }
}

void PopupHost::updateDismiss()
{
    if (m_updateWindow != nullptr) {
        m_updateWindow->setProperty("visible", false);
    }
    // 与 `menuDismiss()` / `switchDismiss()` 一致：窗口收了就把未完成的回调丢掉。
    UpdateRequest request = std::move(m_updateRequest);
    m_updateRequest = UpdateRequest{};
    if (request.onDismiss) {
        request.onDismiss();
    }
}

void PopupHost::updateRetry()
{
    if (m_updateRequest.onRetry) {
        m_updateRequest.onRetry();
    }
}

void PopupHost::updateOpenRelease()
{
    if (m_updateRequest.onOpenRelease) {
        m_updateRequest.onOpenRelease();
    }
}

void PopupHost::switchUseEnglishInput()
{
    if (m_switchWindow == nullptr) {
        return;
    }
    // 预热期间（窗口在屏幕外、全透明地画第一帧）不要去动输入法：那时用户并没有
    // 要用切换器，而我们自己的线程只有两个筛选框共用它。
    if (m_warming.contains(m_switchWindow)) {
        return;
    }
    // 卡片没显示的时候也不动（例如隐藏之后 QML 那边又冒出一个焦点变化）：
    // 关掉卡片时我们已经把模式还回去了，这里再切一次就把还原白做了。
    if (!m_switchWindow->isVisible()) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_switchWindow->winId());
    // **打开之前**的模式只记一次（QML 的输入框每拿到一次焦点都会调进来）：
    // 关掉卡片时要还原的就是它。
    if (!m_switchModeSaved) {
        const win::ime::Mode previous = win::ime::readMode(hwnd);
        m_switchPreviousValid = previous.valid;
        m_switchPreviousConversion = previous.conversion;
        m_switchPreviousSentence = previous.sentence;
        m_switchModeSaved = true;
    }
    const win::ime::ModeSwitch result = win::ime::useAlphanumericMode(hwnd);
    // 每次都记（debug 级）：这条日志是「输入法是英文吗」的唯一现场 —— 输入框
    // 拿到焦点时这个函数会再调一次，而两次之间可能夹着输入法自己的状态变化。
    if (result.ok && result.changed) {
        win::logDebug(
            QStringLiteral("window switcher: input method set to english (%1)").arg(result.detail));
    } else if (result.ok) {
        win::logDebug(QStringLiteral("window switcher: input method already ok (%1)")
                          .arg(result.detail));
    } else {
        win::logDebug(QStringLiteral("window switcher: could not switch the input method: %1")
                          .arg(result.detail));
    }
}

void PopupHost::restoreSwitchInputMode()
{
    if (!m_switchModeSaved) {
        // 这次卡片压根没动过输入法（没真正弹出过 / 预热期间取消）：没什么可还的。
        return;
    }
    m_switchModeSaved = false;
    if (m_switchWindow == nullptr) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_switchWindow->winId());
    win::ime::Mode previous;
    previous.valid = m_switchPreviousValid;
    previous.conversion = m_switchPreviousConversion;
    previous.sentence = m_switchPreviousSentence;
    const win::ime::ModeSwitch result = win::ime::restoreMode(hwnd, previous);
    if (!result.ok) {
        win::logDebug(QStringLiteral("window switcher: could not restore the input method: %1")
                          .arg(result.detail));
    } else if (result.changed) {
        win::logDebug(
            QStringLiteral("window switcher: input method restored (%1)").arg(result.detail));
    }
}

void PopupHost::noteShown(const QString &name, bool created)
{
    win::logDebug(QStringLiteral("popup `%1` shown in %2 ms%3")
                      .arg(name)
                      .arg(m_frameTimer.elapsed())
                      .arg(created ? QStringLiteral(" (the window had to be created first)")
                                   : QString()));
    m_frameName = name;
}

void PopupHost::noteFirstFrame()
{
    if (m_frameName.isEmpty()) {
        return;
    }
    win::logDebug(QStringLiteral("popup `%1` painted its first frame %2 ms after the request")
                      .arg(m_frameName)
                      .arg(m_frameTimer.elapsed()));
    m_frameName.clear();
}

void PopupHost::preload()
{
    if (m_preloaded) {
        return;
    }
    m_preloaded = true;
    m_warmTimer.start();

    QQuickWindow *menu = ensureMenuWindow();
    QQuickWindow *help = ensureHelpWindow();
    QQuickWindow *switchWindow = ensureSwitchWindow();
    if (menu == nullptr || help == nullptr || switchWindow == nullptr) {
        return;
    }
    // 「在线更新」卡片也预热：它没有列表，装配很便宜，但能把「首次弹出要现场
    // 加载 QML」这笔钱提前付掉；顺带让启动日志里的 QML 加载错误把它的
    // 加载失败也暴露出来（`scripts/acceptance.ps1` 有一条哨兵检查盯着这个）。
    QQuickWindow *update = m_updateModel != nullptr ? ensureUpdateWindow() : nullptr;

    // 假数据：`help` / `switch` / `menu` 的列表里得有行，`ListView` 才会把
    // `ItemDelegate` 建出来（`TextField` / `ScrollBar` 这些样式件也在这一步
    // 装配）—— 它们同样是首次弹出要付的钱。行数故意多于一屏（`help` / `switch`
    // 还会因此把 `ScrollBar` 的滑块也画出来），否则用户第一次打开一个真正列着
    // 十几个窗口的卡片时还得现场建那十几个委托。
    // 真正弹出时 `showXxx()` 会重新 `setItems` 覆盖掉，所以这几行**不需要**
    // 在预热结束时清掉。
    std::vector<MenuEntry> menuItems;
    menuItems.reserve(8);
    for (int i = 0; i < 8; ++i) {
        menuItems.push_back(MenuEntry{QChar(static_cast<char16_t>(u'a' + i)),
                                      QStringLiteral("preload"), QStringLiteral("preload")});
    }
    m_menuModel->setItems(QStringLiteral("flowkeyd"), std::move(menuItems));

    std::vector<HelpEntry> helpItems;
    helpItems.reserve(14);
    for (int i = 0; i < 14; ++i) {
        helpItems.push_back(HelpEntry{QStringList{QStringLiteral("Ctrl+Alt+F%1").arg(i + 1)},
                                      QStringLiteral("preload"), QStringLiteral("none"), false});
    }
    m_helpModel->setItems(std::nullopt, std::move(helpItems));

    std::vector<WindowListEntry> switchItems;
    switchItems.reserve(14);
    for (int i = 0; i < 14; ++i) {
        switchItems.push_back(WindowListEntry{QStringLiteral("preload"),
                                              QStringLiteral("preload.exe")});
    }
    m_switchModel->setItems(std::nullopt, std::move(switchItems));

    // 屏幕之外 + 全透明。三个都是 `WindowStaysOnTopHint` 的卡片，留在屏幕里
    // 万一赶上鼠标点击就会把那次点击吃掉（透明窗口仍然可能命中），所以放到
    // 整个虚拟桌面右上角的外面去。
    QRect desktop;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        desktop = desktop.united(screen->geometry());
    }
    const QPoint offscreen(desktop.right() + 64, desktop.top());

    warmUpWindow(menu, QStringLiteral("menu"), offscreen);
    warmUpWindow(help, QStringLiteral("help"), offscreen);
    warmUpWindow(switchWindow, QStringLiteral("switch"), offscreen);
    if (update != nullptr) {
        warmUpWindow(update, QStringLiteral("update"), offscreen);
    }

    win::logDebug(QStringLiteral("popup preload: windows built in %1 ms")
                      .arg(m_warmTimer.elapsed()));
}

bool PopupHost::cancelPreload(QQuickWindow *window)
{
    const bool warming = m_warming.remove(window);
    if (warming) {
        window->setOpacity(1.0);
    }
    return warming;
}

void PopupHost::warmUpWindow(QQuickWindow *window, const QString &name, const QPoint &offscreen)
{
    m_warming.insert(window);
    // 首帧到了就算预热完成。连接以 `this` 为上下文，所以 `PopupHost` 先死掉时
    // 这个回调不会去碰已经失效的成员。
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(
        window, &QQuickWindow::frameSwapped, this, [this, window, name, connection]() {
            QObject::disconnect(*connection);
            if (!m_warming.contains(window)) {
                return; // 已经被一次真实弹出取消了
            }
            finishWarmUp(window, name);
        });
    // 兜底：万一这一帧永远不来（窗口在屏幕外，合成器可以不合成它），两秒之后
    // 也要把它藏起来，免得留一个看不见的置顶窗口在桌面外。
    QTimer::singleShot(2000, this, [this, window, name]() {
        if (!m_warming.contains(window)) {
            return;
        }
        win::logWarn(QStringLiteral("popup `%1` preload timed out; no frame was painted")
                         .arg(name));
        finishWarmUp(window, name);
    });
    window->setOpacity(0.0);
    window->setPosition(offscreen);
    window->setProperty("visible", true);
}

void PopupHost::finishWarmUp(QQuickWindow *window, const QString &name)
{
    if (!m_warming.remove(window)) {
        return;
    }
    window->setProperty("visible", false);
    window->setOpacity(1.0);
    win::logDebug(QStringLiteral("popup `%1` preloaded in %2 ms").arg(name).arg(m_warmTimer.elapsed()));
    if (m_warming.isEmpty()) {
        win::logDebug(QStringLiteral("popup preload done in %1 ms").arg(m_warmTimer.elapsed()));
    }
}

void PopupHost::showMenu(MenuRequest request)
{
    const bool created = m_menuWindow == nullptr;
    m_frameTimer.start();
    QQuickWindow *window = ensureMenuWindow();
    if (window == nullptr) {
        return;
    }
    // 预热还没收尾时用户就按了快捷键：取消预热，把这次当成**第一次**弹出
    // （窗口现在在屏幕外、全透明，位置与透明度都要重新弄）。
    const bool warming = cancelPreload(window);
    const bool wasVisible = window->isVisible() && !warming;
    m_menuRequest = std::move(request);
    m_menuModel->setItems(m_menuRequest.title, m_menuRequest.items);
    window->setProperty("visible", true);
    if (!wasVisible) {
        // 位置只在第一次算：再按一次快捷键只是把选单前置，不该让它跳来跳去。
        centreOnCursorScreen(window, m_menuModel->cardWidth(), m_menuModel->cardHeight());
    }
    activateWindow(window);
    noteShown(QStringLiteral("menu"), created);
}

void PopupHost::showHelp(HelpRequest request)
{
    const bool created = m_helpWindow == nullptr;
    m_frameTimer.start();
    QQuickWindow *window = ensureHelpWindow();
    if (window == nullptr) {
        return;
    }
    const bool warming = cancelPreload(window);
    const bool wasVisible = window->isVisible() && !warming;
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
    noteShown(QStringLiteral("help"), created);
}

void PopupHost::showSwitch(SwitchRequest request)
{
    const bool created = m_switchWindow == nullptr;
    QQuickWindow *window = ensureSwitchWindow();
    if (window == nullptr) {
        return;
    }
    // 预热还没收尾时用户就按了快捷键：取消预热，把这次当成**第一次**弹出。
    const bool warming = cancelPreload(window);
    // **卡片已经开着时，再按一次同一个快捷键就是关掉它**（项目所有者 2026-09
    // 要求，与 `Esc` 同义）。常见的绑法是「轻碰 Win」（`keys = "LWin"` +
    // `trigger = "release"`）：触发发生在 Win 键**松开**时，所以第二次轻碰走到
    // 这里时卡片正开着 —— 在这里收起来就是用户要的效果（如果在这里重新弹一次，
    // 卡片就会“关掉又立刻重开”）。
    if (window->isVisible() && !warming) {
        win::logDebug(QStringLiteral("window switcher: dismissed by its own hotkey"));
        switchDismiss();
        return;
    }
    m_frameTimer.start();
    m_switchRequest = std::move(request);
    m_switchModel->setItems(m_switchRequest.title, m_switchRequest.items);
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    m_switchModel->setMaxRows(screen != nullptr
                                  ? WindowListModel::rowsForAvailableHeight(
                                        screen->availableGeometry().height())
                                  : 12);
    window->setProperty("visible", true);
    // 位置总是这时候算：卡片可见时上面已经 return 了（那时按快捷键是「关掉」），
    // 所以走到这里一定是刚打开（包括刚取消掉预热的那种）。
    centreOnCursorScreen(window, m_switchModel->cardWidth(), m_switchModel->cardHeight());
    activateWindow(window);
    // 卡片一出来就把输入法切成英文（筛选框匹配的是进程名，不是中文）。
    switchUseEnglishInput();
    noteShown(QStringLiteral("switch"), created);
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
    QObject::connect(m_menuWindow, &QQuickWindow::frameSwapped, this, &PopupHost::noteFirstFrame);
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
    QObject::connect(m_helpWindow, &QQuickWindow::frameSwapped, this, &PopupHost::noteFirstFrame);
    return m_helpWindow;
}

QQuickWindow *PopupHost::ensureSwitchWindow()
{
    if (m_switchWindow != nullptr) {
        return m_switchWindow;
    }
    QQmlComponent component(m_engine);
    component.loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("SwitchPopup"));
    if (component.isError()) {
        win::logError(QStringLiteral("could not load SwitchPopup.qml: %1")
                          .arg(component.errorString()));
        return nullptr;
    }
    QObject *object = component.create();
    m_switchWindow = qobject_cast<QQuickWindow *>(object);
    if (m_switchWindow == nullptr) {
        delete object;
        win::logError(QStringLiteral("SwitchPopup.qml did not create a window"));
        return nullptr;
    }
    m_switchModel = new WindowListModel(this);
    m_switchWindow->setProperty("switchModel",
                                QVariant::fromValue(static_cast<QObject *>(m_switchModel)));
    m_switchWindow->setProperty("host", QVariant::fromValue(static_cast<QObject *>(this)));
    QObject::connect(m_switchWindow, &QQuickWindow::frameSwapped, this, &PopupHost::noteFirstFrame);
    return m_switchWindow;
}

void PopupHost::showUpdate()
{
    if (m_updateModel == nullptr) {
        win::logError(QStringLiteral("the update window has no model; nothing to show"));
        return;
    }
    const bool created = m_updateWindow == nullptr;
    m_frameTimer.start();
    QQuickWindow *window = ensureUpdateWindow();
    if (window == nullptr) {
        return;
    }
    const bool warming = cancelPreload(window);
    const bool wasVisible = window->isVisible() && !warming;
    window->setProperty("visible", true);
    if (!wasVisible) {
        // 位置只在第一次算：更新窗口是一个任务窗口，不跟着光标跳。
        centreOnCursorScreen(window, m_updateModel->cardWidth(), m_updateModel->cardHeight());
    }
    activateWindow(window);
    noteShown(QStringLiteral("update"), created);
}

QQuickWindow *PopupHost::ensureUpdateWindow()
{
    if (m_updateWindow != nullptr) {
        return m_updateWindow;
    }
    if (m_updateModel == nullptr) {
        return nullptr;
    }
    QQmlComponent component(m_engine);
    component.loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("UpdatePopup"));
    if (component.isError()) {
        win::logError(QStringLiteral("could not load UpdatePopup.qml: %1")
                          .arg(component.errorString()));
        return nullptr;
    }
    QObject *object = component.create();
    m_updateWindow = qobject_cast<QQuickWindow *>(object);
    if (m_updateWindow == nullptr) {
        delete object;
        win::logError(QStringLiteral("UpdatePopup.qml did not create a window"));
        return nullptr;
    }
    m_updateWindow->setProperty("updateModel",
                                QVariant::fromValue(static_cast<QObject *>(m_updateModel)));
    m_updateWindow->setProperty("host", QVariant::fromValue(static_cast<QObject *>(this)));
    QObject::connect(m_updateWindow, &QQuickWindow::frameSwapped, this, &PopupHost::noteFirstFrame);
    return m_updateWindow;
}

} // namespace flowkeyd::app
