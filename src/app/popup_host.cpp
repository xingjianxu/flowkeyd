#include "app/popup_host.h"

#include "app/app_icons.h"
#include "core/app_list.h"
#include "platform/win/ime.h"
#include "platform/win/logging.h"
#include "platform/win/shell_menu.h"
#include "platform/win/window.h"

#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <algorithm>
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

/// 弹窗该出现在哪块屏上：鼠标所在的那块（拿不到就是主屏）。
QScreen *cursorScreen()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    return screen != nullptr ? screen : QGuiApplication::primaryScreen();
}

/// 把窗口居中放到鼠标所在的那块显示器上（拿不到就是主显示器）。
void centreOnCursorScreen(QQuickWindow *window, int width, int height)
{
    QScreen *screen = cursorScreen();
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

/// 窗口现在**真的**有多大（物理像素）。用 Win32 直接问，**绕开 Qt 的缩放记账** ——
/// 要检查的正是「Qt 记的缩放和窗口实际的样子对不对得上」
/// （见 `app::popupPixelSizeIsStale()`）。问不到时返回空尺寸，调用方按「没问题」处理。
QSize windowPixelSize(QQuickWindow *window)
{
    if (window == nullptr) {
        return {};
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    RECT rect{};
    if (hwnd == nullptr || GetWindowRect(hwnd, &rect) == 0) {
        return {};
    }
    return QSize(rect.right - rect.left, rect.bottom - rect.top);
}

} // namespace

PopupHost::PopupHost(QQmlEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
    // 显示器配置一变就把弹窗窗口重建（见头文件里那段「显示器缩放 / 几何变化」）。
    // 一次切换（插上显示器 / 改缩放）会连着发好几个信号，所以用一条 500 ms 的
    // 单次定时器去抖：每建一整套卡片要花掉几百毫秒，不能每个信号都来一遍。
    m_screenChangeTimer.setSingleShot(true);
    m_screenChangeTimer.setInterval(500);
    QObject::connect(&m_screenChangeTimer, &QTimer::timeout, this,
                     &PopupHost::discardPopupWindowsAndPreload);

    if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        QObject::connect(application, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            watchScreen(screen);
            onScreenConfigurationChanged();
        });
        QObject::connect(application, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
            m_watchedScreens.remove(screen);
            onScreenConfigurationChanged();
        });
    }
    for (QScreen *screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
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

void PopupHost::requestApps(AppRequest request)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, request]() { showApps(request); }, Qt::QueuedConnection);
        return;
    }
    showApps(std::move(request));
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
    // 程序启动器也归这里管（它的回调碰 `Dispatcher`，与 `onChoose` 同理）。
    if (m_appWindow != nullptr) {
        m_appWindow->setProperty("visible", false);
    }
    m_appRequest = AppRequest{};
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

bool PopupHost::appsVisible() const
{
    return m_appWindow != nullptr && m_appWindow->isVisible();
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

void PopupHost::appChoose(int index)
{
    if (m_appWindow == nullptr) {
        return;
    }
    // 先把窗口藏起来再交出去（与 `menuChoose` / `switchChoose` 一致）：启动的
    // 程序会自己抢前台，弹窗不该还留在那里。
    m_appWindow->setProperty("visible", false);
    // 「最近使用」就是在这里记的：用户从启动器里真的启动了一个程序。
    // 在把请求交出去**之前**记（回调会立刻启动进程，然后 `request` 就空了）。
    if (m_appModel != nullptr) {
        m_appModel->noteLaunched(index);
    }
    AppRequest request = std::move(m_appRequest);
    m_appRequest = AppRequest{};
    if (request.onChoose && index >= 0 && index < static_cast<int>(request.items.size())) {
        request.onChoose(index);
    }
}

void PopupHost::appDismiss()
{
    if (m_appWindow != nullptr) {
        m_appWindow->setProperty("visible", false);
    }
    m_appRequest = AppRequest{};
}

bool PopupHost::appContextMenu(int item)
{
    if (m_appWindow == nullptr || m_appModel == nullptr) {
        return false;
    }
    // QML 那边直接传**条目下标**（每一格里带着 `index`），不是可见行号。
    if (item < 0 || item >= static_cast<int>(m_appRequest.items.size())) {
        return false;
    }
    const QString launchName = m_appRequest.items[static_cast<std::size_t>(item)].launch;
    if (launchName.isEmpty()) {
        // 预热用的假数据没有启动名（真实条目一定有）—— 那种情况下没有菜单可弹。
        return false;
    }

    // 用户在菜单里选中了某一条时，**先**把卡片收起来再让 shell 执行：
    // （1）这是产品语义（「选中条目就关」）；
    // （2）「属性」这类命令开出来的对话框以这张卡片为属主，卡片不先藏起来就会
    //      连对话框一起被藏掉；
    // （3）「打开文件位置」拉起的资源管理器窗口要拿得到前台，不能跟一张置顶
    //      卡片抢。
    // 启动名已经拷出来了，所以清掉请求（`appDismiss()`）不影响这次调用。
    const auto beforeInvoke = [this]() { appDismiss(); };
    const HWND owner = reinterpret_cast<HWND>(m_appWindow->winId());
    // `showItemMenu()` 收的是 shell 解析名，`shell:AppsFolder\<AUMID>` 照样能用
    // （真机验过：商店应用、`.msc`、从路径当 AUMID 的那些条目都有正常菜单）。
    const win::shell_menu::MenuResult result =
        win::shell_menu::showItemMenu(owner, launchName, beforeInvoke);
    if (!result.error.isEmpty()) {
        win::logWarn(QStringLiteral("app launcher: could not show the shell menu for %1: %2")
                         .arg(launchName, result.error));
        return false;
    }
    if (result.invoked) {
        win::logInfo(QStringLiteral("app launcher: shell menu command %1 for %2")
                         .arg(result.command)
                         .arg(launchName));
        return true;
    }
    // 取消（`Esc` / 点了菜单外面）：卡片留着，用户接着选下一格。菜单的弹出
    // 窗口可能把前台拿走了，所以再抬一次（拿不到的话卡片也还在那儿，能点）。
    if (m_appWindow->isVisible()) {
        activateWindow(m_appWindow);
    }
    return false;
}

void PopupHost::appRelayout()
{
    if (m_appWindow == nullptr || m_appModel == nullptr || !m_appWindow->isVisible()) {
        return;
    }
    // 卡片高度是恒定的（`maxRows` 行网格 + 上下占位，见 `AppListModel::relayout()`），
    // 只有换到行数上限不同的屏幕上时才会变 —— 这里只负责把它推回屏幕里。
    //
    // **不能重新居中**：卡片是“以筛选框那一行为锚”的（弹出来时已经居中过），
    // 打字时每变一次高度就重新居中，会让输入框在屏幕上上下跳。所以只夹一下。
    // 用窗口自己所在的屏（不是光标下的那块）：鼠标挪到另一块屏上不该把卡片拽走。
    QScreen *screen = m_appWindow->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return;
    }
    // 本机实测 `availableGeometry()` 可能比 `geometry()` 还宽 —— 取交集当作可用区。
    const QRect work = screen->availableGeometry().intersected(screen->geometry());
    if (!work.isValid()) {
        return;
    }
    const int width = m_appModel->cardWidth();
    const int height = m_appModel->cardHeight();
    const int maxX = std::max(work.left(), work.right() - width + 1);
    const int maxY = std::max(work.top(), work.bottom() - height + 1);
    const int x = std::clamp(m_appWindow->x(), work.left(), maxX);
    const int y = std::clamp(m_appWindow->y(), work.top(), maxY);
    if (x != m_appWindow->x() || y != m_appWindow->y()) {
        m_appWindow->setPosition(x, y);
    }
}

void PopupHost::loadAppState()
{
    if (m_appModel == nullptr) {
        return;
    }
    core::LauncherState state;
    if (!m_appStatePath.isEmpty()) {
        QFile file(m_appStatePath);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly)) {
                win::logWarn(QStringLiteral("app launcher: could not read %1: %2")
                                 .arg(QDir::toNativeSeparators(m_appStatePath),
                                      file.errorString()));
            } else {
                const QByteArray bytes = file.readAll();
                file.close();
                QString error;
                state = core::parseLauncherState(bytes, &error);
                if (!error.isEmpty()) {
                    win::logWarn(QStringLiteral("app launcher: %1: %2")
                                     .arg(QDir::toNativeSeparators(m_appStatePath), error));
                }
            }
        }
    }
    m_appModel->setState(state);
}

void PopupHost::saveAppState()
{
    if (m_appModel == nullptr || m_appStatePath.isEmpty()) {
        return;
    }
    const QByteArray bytes = core::serializeLauncherState(m_appModel->launcherState());
    const QDir dir = QFileInfo(m_appStatePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        win::logWarn(QStringLiteral("app launcher: could not create %1")
                         .arg(QDir::toNativeSeparators(dir.absolutePath())));
        return;
    }
    QFile file(m_appStatePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        win::logWarn(QStringLiteral("app launcher: could not write %1: %2")
                         .arg(QDir::toNativeSeparators(m_appStatePath), file.errorString()));
        return;
    }
    const qint64 written = file.write(bytes);
    file.close();
    if (written != bytes.size()) {
        win::logWarn(QStringLiteral("app launcher: short write to %1")
                         .arg(QDir::toNativeSeparators(m_appStatePath)));
        return;
    }
    win::logDebug(QStringLiteral("app launcher: saved pinned=%1 recent=%2 to %3")
                      .arg(m_appModel->pinnedCount())
                      .arg(m_appModel->recentCount())
                      .arg(QDir::toNativeSeparators(m_appStatePath)));
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

void PopupHost::watchScreen(QScreen *screen)
{
    if (screen == nullptr || m_watchedScreens.contains(screen)) {
        return;
    }
    m_watchedScreens.insert(screen);
    const auto note = [this](auto) { onScreenConfigurationChanged(); };
    QObject::connect(screen, &QScreen::geometryChanged, this, note);
    QObject::connect(screen, &QScreen::availableGeometryChanged, this, note);
    QObject::connect(screen, &QScreen::logicalDotsPerInchChanged, this, note);
    QObject::connect(screen, &QScreen::physicalDotsPerInchChanged, this, note);
}

void PopupHost::onScreenConfigurationChanged()
{
    m_screenChangeTimer.start();
}

void PopupHost::discardPopupWindowsAndPreload()
{
    discardPopupWindows();
    // 重新预热：不然用户下一次按快捷键要多等一次 QML 组件加载（几十到两百毫秒，
    // 见 `preload()` 的注释）。`preload()` 自己会重新填一份假数据。
    m_preloaded = false;
    preload();
}

void PopupHost::discardPopupWindows()
{
    // 窗口切换器可能把输入法切成了英文（`switchUseEnglishInput()`）：删窗口之前
    // 先还回去，否则那份快照就白白丢了（`restoreSwitchInputMode()` 看不到窗口
    // 就直接放弃）。
    restoreSwitchInputMode();
    // 五个窗口都是 `QQmlComponent::create()` 出来的（没有 parent），得自己删。
    //
    // **模型一律留着**：启动器的「固定 / 最近使用」就存在 `AppListModel` 里，
    // 重建窗口不能把它弄丢（每个 `ensureXxxWindow()` 里都有「模型只建一次」
    // 的判断）。删窗口时那些 `frameSwapped` 连接跟着窗口一起消失，而预热用的
    // 2 秒兜底回调只会看到 `m_warming` 里没有它、直接 return。
    m_warming.clear();
    for (QQuickWindow *window : {m_menuWindow, m_helpWindow, m_switchWindow, m_appWindow, m_updateWindow}) {
        delete window;
    }
    m_menuWindow = nullptr;
    m_helpWindow = nullptr;
    m_switchWindow = nullptr;
    m_appWindow = nullptr;
    m_updateWindow = nullptr;
}

bool PopupHost::rebuildPopupIfScaleIsStale(QQuickWindow *window, int logicalWidth, int logicalHeight,
                                          QScreen *screen, const QString &name)
{
    if (window == nullptr || screen == nullptr) {
        return false;
    }
    const QSize physical = windowPixelSize(window);
    if (!popupPixelSizeIsStale(physical, logicalWidth, logicalHeight, screen->devicePixelRatio())) {
        return false;
    }
    // 命中就说明这张卡片的缩放已经过期（启动之后显示器缩放变过）：它现在是
    // 「逻辑数字当作物理像素」的尺寸，只有正确大小的一半。全部丢掉重建
    // （它们五个是一起预热的，缩放也一样过期）。
    win::logWarn(QStringLiteral("popup `%1` kept a stale scale (%2x%3 px for %4x%5 logical at %6x): "
                                "rebuilding the popup windows")
                     .arg(name)
                     .arg(physical.width())
                     .arg(physical.height())
                     .arg(logicalWidth)
                     .arg(logicalHeight)
                     .arg(screen->devicePixelRatio()));
    discardPopupWindows();
    return true;
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
    QQuickWindow *appsWindow = ensureAppWindow();
    if (menu == nullptr || help == nullptr || switchWindow == nullptr || appsWindow == nullptr) {
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

    // 启动器是多于一屏的（6 列 × 7 行 > 默认的 6 行上限），所以预热也会把
    // 网格的 `ScrollBar` 装配一遍。图标 URL 是空串：预热不需要真去 shell 里取
    // 图标（那会白白花掉几十毫秒）。
    std::vector<AppListEntry> appItems;
    appItems.reserve(42);
    core::LauncherState fakeState;
    for (int i = 0; i < 42; ++i) {
        const QString key = QStringLiteral("preload%1").arg(i, 2, 10, QLatin1Char('0'));
        appItems.push_back(AppListEntry{QStringLiteral("preload"), QString(), key});
        // 全部当成「已固定」：于是概览是「表头 + 7 行网格 + 全部程序按钮」、
        // 比一屏（6 行）高，网格的 `ScrollBar`、表头与按钮的委托也一起装配掉。
        fakeState.pinned.push_back(key);
    }
    m_appModel->setState(fakeState);
    m_appModel->setItems(std::nullopt, std::move(appItems));

    // 屏幕之外 + 全透明。它们都是 `WindowStaysOnTopHint` 的卡片，留在屏幕里
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
    warmUpWindow(appsWindow, QStringLiteral("apps"), offscreen);
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
    // 日志里带上**逻辑尺寸与缩放**：显示器缩放变过之后，这两项与
    // `GetWindowRect` 量出来的物理尺寸对不对得上，是判断「弹窗是不是只有一半大」
    // 的唯一现场（见 `popupPixelSizeIsStale()`）。
    win::logDebug(QStringLiteral("popup `%1` preloaded in %2 ms (logical %3x%4 dpr %5)")
                      .arg(name)
                      .arg(m_warmTimer.elapsed())
                      .arg(window->width())
                      .arg(window->height())
                      .arg(window->devicePixelRatio()));
    if (m_warming.isEmpty()) {
        win::logDebug(QStringLiteral("popup preload done in %1 ms").arg(m_warmTimer.elapsed()));
    }
}

void PopupHost::showMenu(MenuRequest request)
{
    bool created = m_menuWindow == nullptr;
    m_frameTimer.start();
    QQuickWindow *window = ensureMenuWindow();
    if (window == nullptr) {
        return;
    }
    // 启动之后显示器缩放变过的话，预热好的窗口会停在旧缩放上（只有一半大）：
    // 丢掉重建（见 `popupPixelSizeIsStale()`）。
    if (rebuildPopupIfScaleIsStale(window, m_menuModel->cardWidth(), m_menuModel->cardHeight(),
                                   cursorScreen(), QStringLiteral("menu"))) {
        created = true;
        window = ensureMenuWindow();
        if (window == nullptr) {
            return;
        }
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
    bool created = m_helpWindow == nullptr;
    m_frameTimer.start();
    QQuickWindow *window = ensureHelpWindow();
    if (window == nullptr) {
        return;
    }
    if (rebuildPopupIfScaleIsStale(window, m_helpModel->cardWidth(), m_helpModel->cardHeight(),
                                   cursorScreen(), QStringLiteral("help"))) {
        created = true;
        window = ensureHelpWindow();
        if (window == nullptr) {
            return;
        }
    }
    const bool warming = cancelPreload(window);
    const bool wasVisible = window->isVisible() && !warming;
    m_helpRequest = std::move(request);
    m_helpModel->setItems(m_helpRequest.title, m_helpRequest.items);
    QScreen *screen = cursorScreen();
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
    bool created = m_switchWindow == nullptr;
    QQuickWindow *window = ensureSwitchWindow();
    if (window == nullptr) {
        return;
    }
    if (rebuildPopupIfScaleIsStale(window, m_switchModel->cardWidth(), m_switchModel->cardHeight(),
                                   cursorScreen(), QStringLiteral("switch"))) {
        created = true;
        window = ensureSwitchWindow();
        if (window == nullptr) {
            return;
        }
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
    QScreen *screen = cursorScreen();
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

void PopupHost::showApps(AppRequest request)
{
    bool created = m_appWindow == nullptr;
    QQuickWindow *window = ensureAppWindow();
    if (window == nullptr) {
        return;
    }
    // 缩放过期就重建（见 `popupPixelSizeIsStale()`）。判断要拿**现在的**卡片尺寸：
    // 下面的 `setMaxRows()` 只会在扫了屏幕高度之后改它，那张卡片本身是恒定的。
    if (rebuildPopupIfScaleIsStale(window, m_appModel->cardWidth(), m_appModel->cardHeight(),
                                   cursorScreen(), QStringLiteral("apps"))) {
        created = true;
        window = ensureAppWindow();
        if (window == nullptr) {
            return;
        }
    }
    // 预热还没收尾时用户就按了快捷键：取消预热，把这次当成**第一次**弹出。
    const bool warming = cancelPreload(window);
    // 卡片已经开着时，再按一次同一个快捷键就是关掉它（与窗口切换器同一条规则）。
    if (window->isVisible() && !warming) {
        win::logDebug(QStringLiteral("app launcher: dismissed by its own hotkey"));
        appDismiss();
        return;
    }
    m_frameTimer.start();
    m_appRequest = std::move(request);
    m_appStatePath = m_appRequest.statePath;
    // 先把上一份持久状态（固定 / 最近使用）读进来，再换程序列表。
    loadAppState();

    // 图标：把「图标键 → 启动名」登记给图片提供者，并把每一行的 URL 算好。
    // 键是启动名的哈希（`core::appIconKey`），所以列表重扫、条目换位置都
    // 不会让已经缓存下来的 `image://` URL 指向别的程序。
    // **登记给提供者的取图名字**过一道 `core::appIconLaunchName()`：内置的
    // `ms-settings:` 页面在 shell 里取不到图标，借「设置」应用那一张。
    QVector<QPair<QString, QString>> icons;
    icons.reserve(static_cast<int>(m_appRequest.items.size()));
    std::vector<AppListEntry> entries;
    entries.reserve(m_appRequest.items.size());
    for (const AppLauncherItem &item : m_appRequest.items) {
        if (item.launch.isEmpty()) {
            entries.push_back(AppListEntry{item.name, QString(), QString()});
            continue;
        }
        const QString key = core::appIconKey(item.launch);
        entries.push_back(AppListEntry{item.name, core::appIconUrl(item.launch), key});
        icons.append(qMakePair(key, core::appIconLaunchName(item.launch)));
    }
    if (m_appIcons != nullptr) {
        m_appIcons->publish(icons);
    }
    m_appModel->setItems(m_appRequest.title, std::move(entries));

    QScreen *screen = cursorScreen();
    m_appModel->setMaxRows(screen != nullptr
                               ? AppListModel::rowsForAvailableHeight(
                                     screen->availableGeometry().height())
                               : 6);
    window->setProperty("visible", true);
    // 位置总是这时候算：卡片可见时上面已经 return 了（那时按快捷键是「关掉」）。
    centreOnCursorScreen(window, m_appModel->cardWidth(), m_appModel->cardHeight());
    activateWindow(window);
    // **不切输入法**：切换器那边匹配的是进程名（ASCII），而这里的名字可能是
    // 中文（「记事本」），切成英文反而筛不出东西。
    noteShown(QStringLiteral("apps"), created);
}

QQuickWindow *PopupHost::ensureAppWindow()
{
    if (m_appWindow != nullptr) {
        return m_appWindow;
    }
    if (m_appModel == nullptr) {
        m_appModel = new AppListModel(this);
        // 固定 / 最近使用一改就落盘（`Space`、或者从启动器里启动了一个程序）。
        QObject::connect(m_appModel, &AppListModel::stateEdited, this, &PopupHost::saveAppState);
    }
    QQmlComponent component(m_engine);
    component.loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("AppPopup"));
    if (component.isError()) {
        win::logError(QStringLiteral("could not load AppPopup.qml: %1")
                          .arg(component.errorString()));
        return nullptr;
    }
    QObject *object = component.create();
    m_appWindow = qobject_cast<QQuickWindow *>(object);
    if (m_appWindow == nullptr) {
        delete object;
        win::logError(QStringLiteral("AppPopup.qml did not create a window"));
        return nullptr;
    }
    m_appWindow->setProperty("appModel",
                             QVariant::fromValue(static_cast<QObject *>(m_appModel)));
    m_appWindow->setProperty("host", QVariant::fromValue(static_cast<QObject *>(this)));
    QObject::connect(m_appWindow, &QQuickWindow::frameSwapped, this, &PopupHost::noteFirstFrame);
    return m_appWindow;
}

QQuickWindow *PopupHost::ensureMenuWindow()
{
    if (m_menuWindow != nullptr) {
        return m_menuWindow;
    }
    // 模型只建一次：显示器缩放变化时窗口会被丢掉重建（`discardPopupWindows()`），
    // 不能再顺手把模型也换一份 —— 那会把模型里已经装好的数据（启动器的固定 /
    // 最近使用）弄丢。
    if (m_menuModel == nullptr) {
        m_menuModel = new MenuModel(this);
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
    if (m_helpModel == nullptr) {
        m_helpModel = new HelpModel(this);
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
    if (m_switchModel == nullptr) {
        m_switchModel = new WindowListModel(this);
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
    bool created = m_updateWindow == nullptr;
    m_frameTimer.start();
    QQuickWindow *window = ensureUpdateWindow();
    if (window == nullptr) {
        return;
    }
    if (rebuildPopupIfScaleIsStale(window, m_updateModel->cardWidth(), m_updateModel->cardHeight(),
                                   cursorScreen(), QStringLiteral("update"))) {
        created = true;
        window = ensureUpdateWindow();
        if (window == nullptr) {
            return;
        }
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
