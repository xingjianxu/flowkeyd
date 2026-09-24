// 弹窗宿主：在 Qt GUI 线程上创建/复用 `menu` 与 `help` 两个 QML 窗口。
//
// 为什么必须有这一层：QML 窗口只能在 GUI 线程上碰，而动作是在**工作线程**上
// 执行的（`app::Dispatcher`）。所以 `requestMenu` / `requestHelp` 可以从任意
// 线程调用，内部一律 `Qt::QueuedConnection` 投到 GUI 线程；用户选了第几项之后，
// 由 `onChoose` / `onCopy` 回调把活儿交回工作线程——**弹窗自己从不执行动作**
// （照抄 oskeyd 的 `win::menu` / `win::help` 的分工）。
//
// 与 oskeyd 的差异：oskeyd 给每个弹窗开一条自己的线程（因为它是自绘的原生
// 窗口，而钩子线程不能停在可见窗口里）；flowkeyd 的窗口是 Qt 窗口，只能在
// GUI 线程上创建，而 GUI 线程本来就不跑钩子回调，所以直接在 GUI 线程上跑。
#pragma once

#include "app/help_model.h"
#include "app/menu_model.h"
#include "app/window_list_model.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QQmlEngine;
class QQuickWindow;

namespace flowkeyd::app {

/// 打开选单需要的一切。
struct MenuRequest
{
    std::optional<QString> title;
    std::vector<MenuEntry> items;
    /// 用户选中第 `index` 项时调用。此时窗口已经在屏幕上消失，
    /// 回调在 GUI 线程上执行，所以它只应该把动作转交给别处（`Dispatcher`
    /// 就是这么做：内部再投一次队列），不要阻塞。
    std::function<void(int)> onChoose;
};

/// 打开帮助窗口需要的一切。
struct HelpRequest
{
    std::optional<QString> title;
    std::vector<HelpEntry> items;
    /// 用户按 `Enter`（或单击某一行）时带着那一条的按键文本调用。
    /// 同样在 GUI 线程上，必须很快返回（`Dispatcher` 给它的实现只写一次剪贴板）。
    std::function<void(const QString &)> onCopy;
    /// 用户按 `Enter`（或双击某一行）要执行那一行的动作时，带着**条目下标**
    /// （也就是 `items` 里的下标，不是筛选之后的可见行下标 —— `PopupHost`
    /// 会用 `HelpModel::itemIndexForVisible()` 先换算好）调用。
    ///
    /// 调用前窗口已经藏起来了（跟 `menu` 的 `onChoose` 一样），因为很多动作
    /// （`send`/`type`/`window`）会作用到**前台窗口**上 —— 帮助窗口必须先把前台
    /// 让回去，否则那些按键会打回它自己的筛选框。
    /// 同样在 GUI 线程上，所以实现只应该把活儿转交给别处（`Dispatcher` 再投一次
    /// 队列），不要阻塞。
    std::function<void(int)> onRun;
};

/// 打开窗口切换器需要的一切。
struct SwitchRequest
{
    std::optional<QString> title;
    std::vector<WindowListEntry> items;
    /// 用户选中第 `index` 项（**条目**下标，不是筛选后的可见行下标）时调用。
    /// 此时窗口已经在屏幕上消失，回调在 GUI 线程上执行；实现只应该把活儿转交
    /// 给别处（`Dispatcher` 再投一次队列），不要阻塞。
    std::function<void(int)> onChoose;
};

/// `menu` / `help` / 窗口切换器三个弹窗的宿主（GUI 线程亲和）。
class PopupHost : public QObject
{
    Q_OBJECT

public:
    explicit PopupHost(QQmlEngine *engine, QObject *parent = nullptr);

    // ---- 以下可从任意线程调用 ----

    /// 弹出选单；已经开着时只是前置并把选中项复位，不会开出第二个窗口。
    void requestMenu(MenuRequest request);
    /// 弹出帮助窗口；已经开着时只是前置并清空筛选。
    void requestHelp(HelpRequest request);
    /// 弹出窗口切换器；已经开着时只是前置、清空筛选并把窗口列表换成最新的。
    void requestSwitch(SwitchRequest request);

    // ---- 以下只在 GUI 线程调用 ----

    /// 关掉所有弹窗并把未完成的请求丢掉。
    ///
    /// **退出流程必须在销毁 `Dispatcher` 之前调它**：`onChoose` 会碰
    /// `Dispatcher`，留着未完成的回调就是在给崩溃找机会。
    void closeAll();

    bool menuVisible() const;
    bool helpVisible() const;
    bool switchVisible() const;

    // 供 QML 调用（GUI 线程）：模型只做判断，执行决定的是这几个方法。
    Q_INVOKABLE void menuChoose(int index);
    Q_INVOKABLE void menuDismiss();
    Q_INVOKABLE void helpCopy(int index);
    Q_INVOKABLE void helpRun(int index);
    Q_INVOKABLE void helpDismiss();
    Q_INVOKABLE void switchChoose(int index);
    Q_INVOKABLE void switchDismiss();

private:
    void showMenu(MenuRequest request);
    void showHelp(HelpRequest request);
    void showSwitch(SwitchRequest request);
    QQuickWindow *ensureMenuWindow();
    QQuickWindow *ensureHelpWindow();
    QQuickWindow *ensureSwitchWindow();
    void placePopup(QQuickWindow *window, int width, int height);
    void activate(QQuickWindow *window);

    QQmlEngine *m_engine = nullptr;

    QQuickWindow *m_menuWindow = nullptr;
    MenuModel *m_menuModel = nullptr;
    MenuRequest m_menuRequest;

    QQuickWindow *m_helpWindow = nullptr;
    HelpModel *m_helpModel = nullptr;
    HelpRequest m_helpRequest;

    QQuickWindow *m_switchWindow = nullptr;
    WindowListModel *m_switchModel = nullptr;
    SwitchRequest m_switchRequest;
};

} // namespace flowkeyd::app
