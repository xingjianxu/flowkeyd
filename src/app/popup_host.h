// 弹窗宿主：在 Qt GUI 线程上创建/复用 `menu` / `help` / 窗口切换器 / 程序启动器 /
// 在线更新五个 QML 窗口。
//
// 为什么必须有这一层：QML 窗口只能在 GUI 线程上碰，而动作是在**工作线程**上
// 执行的（`app::Dispatcher`）。所以 `requestMenu` / `requestHelp` 可以从任意
// 线程调用，内部一律 `Qt::QueuedConnection` 投到 GUI 线程；用户选了第几项之后，
// 由 `onChoose` / `onCopy` 回调把活儿交回工作线程——**弹窗自己从不执行动作**。
//
// 每个弹窗都跑在 Qt GUI 线程上（QML 窗口只能在 GUI 线程上创建，而 GUI 线程
// 本来就不跑钩子回调），不需要每个弹窗再开一条自己的线程。
#pragma once

#include "app/app_list_model.h"
#include "app/help_model.h"
#include "app/menu_model.h"
#include "app/update_model.h"
#include "app/window_list_model.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QQmlEngine;
class QQuickWindow;

namespace flowkeyd::app {

/// 程序启动器的图标提供者（在 `main` 里注册到 QML 引擎上）。
/// 这里只持一个裸指针：提供者的生命周期归引擎（`addImageProvider` 接管所有权）。
class AppIconProvider;

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

/// 程序启动器里的一行请求数据。
struct AppLauncherItem
{
    /// 程序名。
    QString name;
    /// 快捷方式（`.lnk`）的完整路径；空串表示这一行没有图标（预热用的假数据）。
    QString shortcut;
};

/// 打开程序启动器需要的一切。
///
/// 传给窗口的是**名字 + 快捷方式路径**（而不是现成的图标 URL）：图标键与 URL
/// 都在宿主这边算（`core::appIconUrl`），顺便把「键 → 路径」登记给图标提供者。
struct AppRequest
{
    std::optional<QString> title;
    std::vector<AppLauncherItem> items;
    /// 持久状态（固定 / 最近使用）的 JSON 文件路径：与配置文件同目录的
    /// `launcher.json`（`core::launcherStatePath()`）。空串 = 不读也不写。
    QString statePath;
    /// 用户选中第 `index` 个**条目**（不是筛选后的可见格）时调用。
    /// 此时窗口已经在屏幕上消失，回调在 GUI 线程上执行；实现只应该把活儿转交
    /// 给别处（`Dispatcher` 再投一次队列），不要阻塞。
    std::function<void(int)> onChoose;
};

/// 打开「在线更新」窗口需要的一切。
///
/// 窗口要显示的模型（`app::UpdateModel`）不在这里传：它由 `setUpdateModel()`
/// 在启动时交进来一次 —— 这样预热也能把这张卡片建出来（模型的指针必须在那之前
/// 就有）。这里的四个回调是「用户点了哪个按钮」，实现是 `app::Updater` 上对应
/// 的槽。
struct UpdateRequest
{
    /// 用户点了「立即更新」。
    std::function<void()> onInstall;
    /// 用户关掉了窗口（下载中叫「取消下载」，会顺便放弃请求）。
    std::function<void()> onDismiss;
    /// 用户点了「重试」。
    std::function<void()> onRetry;
    /// 用户点了「打开发布页」。
    std::function<void()> onOpenRelease;
};

/// `menu` / `help` / 窗口切换器 / 程序启动器 / 在线更新五个弹窗的宿主
/// （GUI 线程亲和）。
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
    /// 弹出窗口切换器；**已经开着时按同一个快捷键就是关掉它**（与 `Esc` 同义，
    /// 见 `showSwitch()`）。
    void requestSwitch(SwitchRequest request);
    /// 弹出程序启动器；**已经开着时按同一个快捷键就是关掉它**（与 `Esc` 同义，
    /// 与窗口切换器同一条规则）。
    void requestApps(AppRequest request);    /// 显示「在线更新」卡片（托盘菜单「检查更新」）。窗口已经开着时只前置，
    /// 不重置里面的状态（状态是 `UpdateModel` 的事，`Updater` 已经改好了）。
    void requestUpdate(UpdateRequest request);

    /// 启动后台预热（**GUI 线程**）：把弹窗窗口建出来、填一份假数据各渲染
    /// 一帧，然后再藏起来。
    ///
    /// 为什么要有这一步（2026-09 实测，`build/windows-debug`，一次性实例 + 注入
    /// 和弦、从外面轮询窗口什么时候可见）：冷启动的第一次弹出里，真正让用户
    /// 等待的是**首帧**——`QQuickWindow` 的第一次 `frameSwapped` 在请求之后
    /// **224 ms** 才到（另一次同一个 exe 先开了日志窗口的对照里只要 **56 ms**）。
    /// 那 170 ms 是进程首次渲染的固定开销：QRhi/D3D11 设备、交换链、Quick 自己
    /// 那批材质着色器的首次编译。剩下的 50 ms 才是这个窗口自己的 QML 组件加载与
    /// 实例化，以及 `ListView` ／ `ItemDelegate` ／ `TextField` ／ `ScrollBar`
    /// 这些 FluentWinUI3 件的装配。预热把两者都提前付掉：同一份测量里，预热之后
    /// 第一次弹出只要 **30–60 ms**（menu 27 / help 59 / switch 52，后续 13–37），
    /// 而预热本身只让启动多花约 310 ms（那次量的是三个窗口的首帧，全在后台）。
    ///
    /// 守护进程是长期运行的，启动时多花这一点看不见；而“按了快捷键等四分之一秒
    /// 才看到卡片”每次都看得见。
    ///
    /// 预热窗口是**全透明 + 屏幕之外**的，用户看不到、也点不到；等它们各自的
    /// 首帧到了（或 2 秒兜底超时）就藏起来。必须在 GUI 线程上调用。
    void preload();

    // ---- 以下只在 GUI 线程调用 ----

    /// 关掉所有弹窗并把未完成的请求丢掉。
    ///
    /// **退出流程必须在销毁 `Dispatcher` 之前调它**：`onChoose` 会碰
    /// `Dispatcher`，留着未完成的回调就是在给崩溃找机会。
    void closeAll();

    bool menuVisible() const;
    bool helpVisible() const;
    bool switchVisible() const;
    bool appsVisible() const;
    bool updateVisible() const;

    /// 把程序启动器的图标提供者交给宿主（GUI 线程、启动时调一次）。
    /// 必须在 `preload()` 之前调，否则那张卡片的图标表是空的。
    void setAppIconProvider(AppIconProvider *provider) { m_appIcons = provider; }

    /// 把「在线更新」卡片要显示的模型交给宿主（GUI 线程、启动时调一次）。
    /// 必须在 `preload()` 之前调，否则那张卡片不会被预热。
    void setUpdateModel(UpdateModel *model) { m_updateModel = model; }

    /// 关掉「在线更新」窗口（不取消任何正在跑的请求 —— 那是 `Updater` 的事）。
    void hideUpdate();

    // 供 QML 调用（GUI 线程）：模型只做判断，执行决定的是这几个方法。
    Q_INVOKABLE void menuChoose(int index);
    Q_INVOKABLE void menuDismiss();
    Q_INVOKABLE void helpCopy(int index);
    Q_INVOKABLE void helpRun(int index);
    Q_INVOKABLE void helpDismiss();
    Q_INVOKABLE void switchChoose(int index);
    Q_INVOKABLE void switchDismiss();
    // 程序启动器（GUI 线程）：同上面几个，只把活儿转交出去。
    Q_INVOKABLE void appChoose(int index);
    Q_INVOKABLE void appDismiss();
    /// 为第 `item` 个**条目**（QML 每一格里那个 `index`）弹出它的**原生 shell
    /// 右键菜单**。
    ///
    /// **阻塞**：里面 `TrackPopupMenuEx` 一直等到用户选完（
    /// `platform/win/shell_menu.h` 里写了为什么必须这样）；返回值就是「用户
    /// 到底选没选」。用户选中的命令由 shell 自己执行（打开 / 以管理员身份运行 /
    /// 打开文件位置 / 属性……）。假数据（预热那一份没有快捷方式）与越界的下标
    /// 都直接返回 false。
    Q_INVOKABLE bool appContextMenu(int line);
    /// 卡片高度变了之后把它夹回屏幕里。
    ///
    /// 为什么需要：卡片高度**恒定**（`maxRows` 行网格 + 上下占位），只有弹到一块
    /// 行数上限不同的屏幕上时才会变；窗口是**居中**于光标那块屏的，换了高度不夹
    /// 一次就可能有一截落在屏幕之外。
    Q_INVOKABLE void appRelayout();
    // 「在线更新」卡片上的按钮（GUI 线程；只把活儿转交给 `Updater`）。
    Q_INVOKABLE void updateInstall();
    Q_INVOKABLE void updateDismiss();
    Q_INVOKABLE void updateRetry();
    Q_INVOKABLE void updateOpenRelease();
    /// 把窗口切换器所在窗口的输入法切成英文（字母数字）模式。
    ///
    /// 为什么需要：筛选框匹配的是**进程名**（ASCII），而用户可能正开着中文
    /// 输入法 —— 那样打进去的是候选字，列表一条都筛不出来。详见
    /// `platform/win/ime.h`（Qt 在 Windows 上不看 `ImhPreferLatin`，只能自己调）。
    ///
    /// 真实弹出时由 `showSwitch()` 调一次；输入框自己拿到焦点时 QML 再调一次
    /// （鼠标点进来、或用户中途切回中文时补上）。**预热期间不调**：那时窗口在
    /// 屏幕外，用户并没有要用切换器。
    ///
    /// 第一次调进来时先把**打开前**的输入模式记下来
    /// （`m_switchPreviousValid` / `m_switchPreviousConversion` +
    /// `m_switchPreviousSentence`），卡片关掉时由 `restoreSwitchInputMode()`
    /// 还回去。
    Q_INVOKABLE void switchUseEnglishInput();

private:
    void showMenu(MenuRequest request);
    void showHelp(HelpRequest request);
    void showSwitch(SwitchRequest request);
    void showApps(AppRequest request);
    /// 读 `m_appStatePath`（不存在就当作空状态）填给启动器模型。
    void loadAppState();
    /// 把启动器模型的固定 / 最近使用写回 `m_appStatePath`。
    void saveAppState();
    void showUpdate();
    QQuickWindow *ensureMenuWindow();
    QQuickWindow *ensureHelpWindow();
    QQuickWindow *ensureSwitchWindow();
    QQuickWindow *ensureAppWindow();
    QQuickWindow *ensureUpdateWindow();
    void placePopup(QQuickWindow *window, int width, int height);
    void activate(QQuickWindow *window);

    /// 记一次「弹窗已经显示出来」的 debug 日志（耗时 + 这次是否新建了窗口）。
    /// 同时把「还要等首帧」的标记立起来，`noteFirstFrame()` 收到首帧时再补一条。
    /// 这两条日志是排查「弹出很慢」的唯一现场（第一次弹出要现场加载 QML 组件、
    /// 装配 FluentWinUI3 样式、创建原生窗口并初始化 RHI，后面几次只是显示）。
    /// 计时器用 `QElapsedTimer` 而不是 `GetTickCount64`：后者的粒度是系统时钟
    /// 中断（~15.6 ms），这两个数字全在几十毫秒的量级，量化误差会把结论带偏。
    void noteShown(const QString &name, bool created);
    void noteFirstFrame();

    /// 真实弹出前调用：如果这个窗口正在预热，就取消预热（它可能正透明地待在
    /// 屏幕外）。返回 true 表示刚才在预热 —— 调用方要当「第一次弹出」处理：
    /// 重新摆位置，并把透明度还回去。
    bool cancelPreload(QQuickWindow *window);

    /// 关掉窗口切换器时把输入法还原成**打开前**的模式。
    ///
    /// 为什么需要：输入模式是本进程这个**线程**的共享状态（`platform/win/ime.h`
    /// 里写了为什么），卡片把它切成英文之后，同一个线程的 help / 日志窗口也会
    /// 跟着变成英文。用户不希望在卡片之外留下痕迹，所以打开前的状态要还回去。
    /// 没有快照（没真正弹出过、或这台机器压根没有输入法）时是空操作。
    void restoreSwitchInputMode();

    /// 把 `window` 显示成「全透明 + 屏幕外」，等它画出第一帧之后自己藏起来。
    void warmUpWindow(QQuickWindow *window, const QString &name, const QPoint &offscreen);
    /// 预热收尾：藏起来、恢复透明度、写一条 debug 日志。
    void finishWarmUp(QQuickWindow *window, const QString &name);

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

    QQuickWindow *m_appWindow = nullptr;
    AppListModel *m_appModel = nullptr;
    AppRequest m_appRequest;
    /// 当前这一份启动器状态该写到哪（`AppRequest::statePath`）。
    QString m_appStatePath;
    /// 图标提供者（引擎拥有它；这里只用来登记「图标键 → 快捷方式路径」）。
    AppIconProvider *m_appIcons = nullptr;

    // 「在线更新」卡片（第五个弹窗）。模型由 `Updater` 拥有，宿主只持有指针。
    QQuickWindow *m_updateWindow = nullptr;
    UpdateModel *m_updateModel = nullptr;
    UpdateRequest m_updateRequest;

    // 窗口切换器的输入模式快照：真正弹出时记一份（`switchUseEnglishInput()` 里
    // 只记一次），卡片关掉时 `restoreSwitchInputMode()` 写回去。
    // 窗口切换器的输入模式快照（就是 `platform/win/ime.h` 的 `ime::Mode`）。
    //
    // 这里**不**直接用它、只存这几个标量：一旦这个头文件（会被 `main.cpp` 拉到）
    // 把 `platform/win/ime.h` 间接带进来，`windows.h` 就会先于 `core/keys.h`
    // 被包含，而 `winnt.h` 的 `DELETE` 宏会与 `core/keys.h` 里的 `Vk DELETE`
    // 撞名（报 `expected unqualified-id before numeric constant`）。所以这个
    // 头文件保持不碰 Win32。
    std::uint32_t m_switchPreviousConversion = 0;
    std::uint32_t m_switchPreviousSentence = 0;
    bool m_switchPreviousValid = false;
    bool m_switchModeSaved = false;

    // 首帧计时的状态（debug 日志用）：`m_frameName` 非空表示正在等那个弹窗的下一帧。
    QString m_frameName;
    QElapsedTimer m_frameTimer;

    // 正在预热的窗口（通常五个）。真实弹出先从里面拿掉一个，于是它的首帧回调
    // 不会再把它藏起来（用户已经把它打开了）。
    QSet<QQuickWindow *> m_warming;
    bool m_preloaded = false;
    QElapsedTimer m_warmTimer;
};

} // namespace flowkeyd::app
