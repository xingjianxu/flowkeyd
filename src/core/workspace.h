// 工作空间的**纯逻辑状态机**：显示器 → 工作空间 → 窗口记录的簿记，以及
// 「切换 / 移动 / 窗口出现 / 显示器拔插」这些命令应当产生的**效果列表**。
//
// 这一层只认三种不透明标识：窗口句柄（`uint64`）、虚拟桌面键（字符串）、显示器
// 设备名。它不知道 `HWND`、`ShowWindow`、`IWorkspace`，也不读配置 —— 效果列表
// 由 `app::Dispatcher`（动作工作线程，**唯一**的状态所有者）交给
// `platform/win/workspace` 去执行。于是全部规则都能在没有桌面的情况下被 Qt Test
// 覆盖（见 AGENTS.md 第 4 节的「分层铁律」）。
//
// 两条来自 M0 真机验证的硬约束（结论见 `docs/notes/pitfalls.md`）：
//   1. 「窗口在不在」只能看**自己的记录**，不能看 `IsWindowVisible` —— 被我们
//      藏起来的窗口（`ShowWindow(SW_HIDE)`）全都变成不可见了；
//   2. 窗口的虚拟桌面归属由调用方查好再喂进来：窗口一被隐藏，
//      `IVirtualDesktopManager::GetWindowDesktopId` 就失败（`0x8002802B`），所以
//      记录里存的是缓存值，状态机自己绝不去查。
//
// 状态按 **（虚拟桌面，显示器）** 分开记（`plan.md` 的 Q1）：在桌面 B 上切换不会
// 动到桌面 A 的「活动工作空间」。窗口记录本身是全局一张表（一个窗口同时只在一张
// 虚拟桌面上）。
#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace flowkeyd::core {

/// 窗口句柄（`HWND`）。纯逻辑层只把它当不透明标识：不比较大小、不解引用，
/// 也不假设它唯一复用（那件事是 `identityOf()` / `stillSame()` 的职责，见 M4）。
using WindowId = std::uint64_t;

/// 显示器身份：设备名（`\\.\DISPLAY1`），与 `window_rule` 的 `monitor` 用的是
/// 同一个东西。**不用序号**：序号会随排列顺序变化，设备名跟着那块屏走。
using MonitorKey = QString;

/// 虚拟桌面身份：`GUID` 字符串。状态机只把它当不透明的键，不解析、不比较语义。
using DesktopKey = QString;

/// 工作空间数量的取值范围（`settings.workspace.count`）。
inline constexpr std::uint32_t kMinWorkspaceCount = 1;
inline constexpr std::uint32_t kMaxWorkspaceCount = 10;
inline constexpr std::uint32_t kDefaultWorkspaceCount = 9;

/// 一个工作空间的完整身份。`index` 是**1 起**的空间号（配置里写的就是它）。
struct WorkspaceId
{
    DesktopKey desktop;
    MonitorKey monitor;
    std::uint32_t index = 1;

    friend bool operator==(const WorkspaceId &, const WorkspaceId &) = default;
};

/// 一个（被管理的）窗口的簿记。
struct WindowRecord
{
    WindowId id = 0;
    WorkspaceId workspace;
    /// 「最近用过」的排序键，越大越近；**0 表示从来没拿到过焦点**。
    std::uint64_t lastFocusSeq = 0;
    /// 这个窗口在它所在空间里的 Z 序，越大越靠上（新出现 / 刚拿到焦点都会变大）。
    std::uint64_t zOrder = 0;
    /// 最小化的窗口仍然属于它那个空间，只是切换回去时不该把它当焦点目标
    /// （否则会把用户刚收起来的窗口又弹出来）。
    bool minimized = false;
    /// 显示器被拔出时暂存的**原归属**：重新接入时按这两个字段物归原主。
    std::optional<MonitorKey> homeMonitor;
    std::optional<std::uint32_t> homeIndex;
};

/// 一块显示器在**一张虚拟桌面**上的空间集合。
struct MonitorSpaces
{
    /// 这块屏一共有几个空间（`settings.workspace.count`）。
    std::uint32_t count = kDefaultWorkspaceCount;
    /// 当前活动空间（1 起，恒在 `[1, count]` 里）。
    std::uint32_t active = 1;
};

/// 一次命令要执行的效果。**先 `show` 再 `hide`**（减少闪烁，M0 实测的顺序），
/// 最后按 `focus` 抢一次前台。空效果 = 什么都不用做（调用方据此跳过 Win32 调用）。
struct WorkspaceEffects
{
    /// 要显示的窗口，**从底到顶**：这就是还原用的 Z 序栈，平台层按这个顺序
    /// `ShowWindow(SW_SHOWNOACTIVATE)` + 重排。
    std::vector<WindowId> show;
    /// 要隐藏的窗口，**从顶到底**：先藏最上面那个，剩下的窗口不会先闪一下。
    std::vector<WindowId> hide;
    /// 要聚焦的窗口。`std::nullopt` = **不抢焦点**（空空间、以及显示器拔插这种
    /// 用户没主动要求换窗口的场合）。
    ///
    /// 不可省：M0 实测隐藏掉前台窗口之后 `GetForegroundWindow()` 仍然指向那个
    /// 隐藏的窗口，系统不会自己把前台交给别人。
    std::optional<WindowId> focus;

    bool isEmpty() const { return show.empty() && hide.empty() && !focus.has_value(); }
};

/// 把一批窗口过滤成「只留下**当前虚拟桌面上的**」之后的结果。
///
/// `onCurrent` 与 `windows` 一一对应（就是 `platform::win::desktop::
/// windowsOnCurrentDesktop()` 的返回值）。规则：
///   * `true` → 留下；
///   * `false` → 去掉（确定在别的虚拟桌面上）；
///   * `std::nullopt` → **留下**（问不出来）。M0 实测被我们 `SW_HIDE` 藏起来的窗口
///     `GetWindowDesktopId` 会失败，「不知道它在哪」不等于「它在别的桌面上」，当成
///     别处就会在回切时把它漏掉。
///
/// 长度不一致时**原样返回全部窗口**：宁可不藏，也不要藏错。
std::vector<WindowId> filterToCurrentDesktop(const std::vector<WindowId> &windows,
                                             const std::vector<std::optional<bool>> &onCurrent);

/// 每显示器独立的工作空间状态机。所有命令都返回「要做的事」，不改任何窗口。
///
/// 不变量（`tst_workspace` 的随机序列测试逐条检查）：
///   * 每个被跟踪的窗口恰属于一个（虚拟桌面，显示器，空间）；
///   * 每个（虚拟桌面，显示器）恰有一个活动空间，且窗口号都在 `[1, count]` 里；
///   * 一个空间里的窗口 Z 序 / 焦点序互不相同（全局单调递增的序号）；
///   * 「活动空间里的窗口」= 「应该处于可见状态的窗口」（`isVisible()`）。
class WorkspaceState
{
public:
    /// `count` 是每块屏的空间数量（`settings.workspace.count`，1–10）；越界时
    /// 夹到合法范围。
    explicit WorkspaceState(std::uint32_t count = kDefaultWorkspaceCount);

    // --- 配置 ---------------------------------------------------------------

    /// 每块显示器的工作空间数量。
    std::uint32_t count() const { return m_count; }

    /// `count` 变了（`reload`）。**比新数量大的空间并入最后一个空间**（不丢窗口、
    /// 也不把窗口藏在一个再也切不过去的空间里）；活动空间也跟着夹住。
    ///
    /// 效果里的 `show` 只包含「合并之后落进了活动空间」的窗口 —— 它们本来是被藏
    /// 起来的，现在得露出来。
    WorkspaceEffects reconfigure(std::uint32_t count);

    // --- 环境 ---------------------------------------------------------------

    /// 当前虚拟桌面（空串 = 还不知道 / 拿不到）。
    const DesktopKey &desktop() const { return m_desktop; }

    /// 用户切了虚拟桌面。**不产生任何效果**：Windows 自己会按 `WS_VISIBLE` 显示
    /// 那张桌面上的窗口，而我们藏起来的窗口 `WS_VISIBLE` 已经是假的，回来时仍然
    /// 藏着 —— 状态与视图天然一致。这里只换掉「当前桌面」这个上下文，并在第一次
    /// 见到某张桌面时给它建一份空间表（活动空间默认 1）。
    WorkspaceEffects setDesktop(const DesktopKey &desktop);

    /// 显示器集合变了。`present` 是**现存**的显示器设备名，元素顺序决定「被拔掉的
    /// 那块屏上的窗口并入哪块屏」—— 取列表里的第一块（调用方应当传
    /// `core::sortedMonitors()` 的顺序，或按光标的偏好排好）。
    ///
    /// 两件事：
    ///   1. **拔出**：消失的显示器上的窗口并入第一块现存显示器的**活动空间**，
    ///      同时记下原归属（`homeMonitor`/`homeIndex`）。原来可见的继续可见；原来
    ///      藏在某个非活动空间里的必须**显示**出来 —— 否则它们会落进「活动空间却
    ///      不可见」的坏状态，而 `switchTo(活动空间)` 是幂等无操作，再也救不回来。
    ///   2. **重新接入**：让 `homeMonitor` 回到 `present` 里的窗口物归原主。原空间
    ///      如果是那块屏的活动空间就显示，否则隐藏。
    ///
    /// 永远不抢焦点（`focus` 为空）：用户没要求换窗口，拔插显示器不该把前台拽走。
    WorkspaceEffects monitorsChanged(const QStringList &present);

    // --- 命令 ---------------------------------------------------------------

    /// 把 `monitor` 这块屏切到空间 `index`（1 起，越界时夹到最后一个空间）。
    ///
    /// 目标就是当前活动空间时**无操作**（幂等：绝不重复显隐，否则自触发事件会
    /// 打成一个环）。否则：显示新空间（底 → 顶）→ 隐藏旧空间（顶 → 底）→ 聚焦新
    /// 空间里最近用过的那个窗口；空间是空的就不抢焦点。
    WorkspaceEffects switchTo(const MonitorKey &monitor, std::uint32_t index);

    /// 把窗口发到它**自己那块显示器**上的空间 `index`（`workspace{ op = "move" }`）。
    ///
    /// 目标是当前活动空间 → 窗口变可见（它原先是藏着的），并聚焦它；目标是非活动
    /// 空间 → 窗口立即隐藏，活动空间**不变**（用户留在原地）。只有这个窗口正是
    /// 「最近拿到焦点的那一个」（也就是用户正在操作的窗口）时，才会把焦点交给活动
    /// 空间里次近的窗口 —— 那一步不可省，见 `WorkspaceEffects::focus`。
    WorkspaceEffects moveWindow(WindowId window, std::uint32_t index);

    /// 窗口出现了（`EVENT_OBJECT_SHOW` / 启动时收编 / 规则把新窗口放进某个空间）。
    ///
    /// `index` 为空 = 落进这块屏的活动空间（默认行为）；给了值（`settings.workspace.
    /// new_window`，或者 `window_rule` / `app` 的 `workspace` 字段）就落进去，**并且
    /// 如果那不是活动空间就立刻藏起来、不抢焦点**（优先级只在
    /// `core::workspaceForNewWindow()` 一处）。
    ///
    /// 已经被跟踪的窗口再「出现」一次是无操作（幂等；我们自己引起的
    /// `SW_HIDE`→`SW_SHOW` 事件靠 M5 的期望事件表吞掉，这里再兜一层）。
    WorkspaceEffects windowAppeared(WindowId window,
                                    const MonitorKey &monitor,
                                    std::optional<std::uint32_t> index = std::nullopt);

    /// 窗口销毁了。记录与「最近焦点」一起清掉（防 `HWND` 复用把别人的窗口当成
    /// 它）。**永远不产生效果**：不能去显示一个已经没了的窗口。
    WorkspaceEffects windowDestroyed(WindowId window);

    /// 窗口被拖到（或被 `Win+Shift+←/→` 送到）另一块显示器：改归**那块屏的活动
    /// 空间**。原来可见的保持可见；万一它原来藏在非活动空间里（理论上拖不动），
    /// 就让它在新显示器上显示出来。
    WorkspaceEffects windowMovedToMonitor(WindowId window, const MonitorKey &monitor);

    /// 窗口被**我们自己**（`window_rule` 把它摆到了另一块屏）搬到另一块显示器：
    /// 只改归属的显示器，**保留空间号**。
    ///
    /// 与 `windowMovedToMonitor()` 的分工：用户拖窗口只能拖**看得见**的窗口，所以
    /// 那一条把窗口归到目标屏的**活动空间**是对的；而被我们藏起来（或规则刚指派了
    /// 空间）的窗口不会自己动，它换屏一定是我们干的，那就不能把规则要求的空间号
    /// 抹成目标屏的活动空间 —— 否则 `LOCATIONCHANGE` 晚到一步就会把刚设好的归属
    /// 覆盖掉。
    ///
    /// 仍然会按新屏的活动空间重算显隐（空间号没变但活动空间不同 → 该藏就藏），
    /// 不抢焦点。
    WorkspaceEffects windowReboundToMonitor(WindowId window, const MonitorKey &monitor);

    /// 把窗口所在的那块屏切到它自己所在的空间（`window("activate")` / 窗口
    /// 切换器 / 启动器唤起前的准备）。
    ///
    /// 目标窗口正被藏在非活动空间里时（`ShowWindow(SW_HIDE)`，`IsWindowVisible`
    /// 为假），必须先显式切过去它才会重新可见 —— 而 `raiseWindow()` 对一个藏着的
    /// 窗口是没用的。切换到新空间的效果列表里就带着它（`show`），调用方执行完这个
    /// 效果列表**再**去激活它，顺序不能反。
    ///
    /// 已经是活动空间里的窗口 → 无操作（幂等）。
    /// 记录里的虚拟桌面**已经过时**（窗口刚被 `window("activate")` 从别的虚拟桌面
    /// 搬到当前这一张）时就地归入那块屏的当前活动空间，必要时显式显示 —— 这时切
    /// 空间毫无意义（它已经在这张桌面的活动空间里了）。
    WorkspaceEffects revealWindow(WindowId window);

    /// 窗口最小化 / 还原（`EVENT_SYSTEM_MINIMIZESTART/END`）。只影响「切回去时该
    /// 不该聚焦它」，不产生效果。
    WorkspaceEffects windowMinimized(WindowId window, bool minimized);

    /// 前台变了：更新「最近用过」与 Z 序。**不产生任何效果**（前台事件绝不能反过
    /// 来引起显隐，否则与我们自己注入的切换互相追尾）。
    WorkspaceEffects focusChanged(WindowId window);

    /// 把全部窗口恢复可见（托盘「恢复全部窗口」、正常退出、以及安全兜底）。
    ///
    /// 所有被跟踪的窗口（含别的虚拟桌面上的：我们藏它们的时候 `WS_VISIBLE` 已经清
    /// 掉了，Windows 不会替我们恢复）都落进各自显示器的活动空间，并按 Z 序从底到
    /// 顶显示。不抢焦点。
    WorkspaceEffects restoreAll();

    // --- 查询（调用方与测试用；都不产生效果） ---------------------------------

    /// 这个窗口在跟踪表里吗。
    bool tracks(WindowId window) const;

    /// 全部被跟踪的窗口，按句柄升序。
    ///
    /// 两个用途：`platform::win::workspace::enumerateManageable()` 靠它把「被我们
    /// 藏起来的窗口」纳入枚举（M0：不能拿 `IsWindowVisible` 当判据），以及 M8 的
    /// 恢复日志。顺序固定只是为了让日志与测试可复现，不代表 Z 序。
    std::vector<WindowId> trackedWindows() const;

    /// 一份记录的快照。
    std::optional<WindowRecord> record(WindowId window) const;

    /// 按记录推算：这个窗口此刻**应该**是可见的（= 它在它那块屏的活动空间里）。
    ///
    /// 这是我们唯一能用的「在不在」判据（M0：不能用 `IsWindowVisible`）。
    bool isVisible(WindowId window) const;

    /// 某张虚拟桌面上某块屏的空间表；不认识的组合返回 `std::nullopt`。
    std::optional<MonitorSpaces> spaces(const DesktopKey &desktop,
                                        const MonitorKey &monitor) const;
    /// 当前虚拟桌面上某块屏的空间表。
    std::optional<MonitorSpaces> spaces(const MonitorKey &monitor) const;

    /// 某个空间里的窗口，**从底到顶**（= 还原用的 Z 序栈）。
    std::vector<WindowId> windowsInSpace(const DesktopKey &desktop,
                                        const MonitorKey &monitor,
                                        std::uint32_t index) const;
    /// 当前虚拟桌面上某个空间里的窗口。
    std::vector<WindowId> windowsInSpace(const MonitorKey &monitor, std::uint32_t index) const;

    /// 已知的显示器设备名，按最近一次 `monitorsChanged()` 给的顺序（还没收到过时是
    /// 懒创建的顺序）。
    const QStringList &monitors() const { return m_monitorOrder; }

private:
    using MonitorMap = std::map<MonitorKey, MonitorSpaces>;
    using DesktopMap = std::map<DesktopKey, MonitorMap>;

    /// 拿到（必要时懒创建）某块屏在某个虚拟桌面上的空间表。
    ///
    /// 懒创建是刻意的：`switchTo`/`windowAppeared` 可能比 `monitorsChanged` 先到
    /// （启动顺序、或者某块屏刚接上），那时用默认数量、活动空间 1。
    MonitorSpaces &ensureSpaces(const DesktopKey &desktop, const MonitorKey &monitor);
    const MonitorSpaces *findSpaces(const DesktopKey &desktop, const MonitorKey &monitor) const;
    /// 把空间号夹进 `[1, count]`。`count` 变小之后 `index = 9` 的绑定就落到最后
    /// 一个空间（与 `reconfigure()` 的合并规则一致）。
    static std::uint32_t clampIndex(std::uint32_t index, const MonitorSpaces &spaces);

    /// 某个空间里的窗口记录，按 Z 序从底到顶。
    std::vector<const WindowRecord *> orderedWindows(const DesktopKey &desktop,
                                                    const MonitorKey &monitor,
                                                    std::uint32_t index) const;
    /// 切过去之后该聚焦谁：**没最小化**的、`lastFocusSeq` 最大的那个；都没有焦点
    /// 记录（刚出现的一批窗口）就取最上面的那个；一个都没有 / 全都最小化 →
    /// `std::nullopt`（不抢焦点）。
    std::optional<WindowId> focusCandidate(const std::vector<const WindowRecord *> &windows,
                                           std::optional<WindowId> exclude) const;
    /// 把一组窗口 id 排成显示顺序（底 → 顶）与隐藏顺序（顶 → 底）。
    void orderForShow(std::vector<WindowId> *windows) const;
    void orderForHide(std::vector<WindowId> *windows) const;

    DesktopKey m_desktop;
    DesktopMap m_desktops;
    std::map<WindowId, WindowRecord> m_windows;
    QStringList m_monitorOrder;
    std::uint32_t m_count = kDefaultWorkspaceCount;
    std::uint64_t m_zSeq = 0;
    std::uint64_t m_focusSeq = 0;
    /// 最近一次拿到焦点的窗口（`moveWindow` 用它判断「用户正在操作的是不是这个
    /// 窗口」，从而决定要不要把焦点交接给下一个）。
    std::optional<WindowId> m_lastFocused;
};

} // namespace flowkeyd::core
