#include "core/workspace.h"

#include <algorithm>
#include <utility>

namespace flowkeyd::core {

namespace {

/// 排序用的 Z 序；窗口已经不在表里时给 0（底下）。效果列表在排序前都刚填好，
/// 不会出现「查不到」，这里只是防御。
std::uint64_t zOrderOf(const std::map<WindowId, WindowRecord> &windows, WindowId id)
{
    const auto it = windows.find(id);
    return it == windows.end() ? 0 : it->second.zOrder;
}

} // namespace

std::vector<WindowId> filterToCurrentDesktop(const std::vector<WindowId> &windows,
                                             const std::vector<std::optional<bool>> &onCurrent)
{
    if (onCurrent.size() != windows.size()) {
        return windows;
    }
    std::vector<WindowId> out;
    out.reserve(windows.size());
    for (std::size_t i = 0; i < windows.size(); ++i) {
        // `std::nullopt`（问不出来）与 `true` 都留下，只去掉确定在别处的。
        if (onCurrent[i].value_or(true)) {
            out.push_back(windows[i]);
        }
    }
    return out;
}

WorkspaceState::WorkspaceState(std::uint32_t count)
    : m_count(std::clamp(count, kMinWorkspaceCount, kMaxWorkspaceCount))
{
}

// ---------------------------------------------------------------------------
// 内部：空间表的查找与懒创建
// ---------------------------------------------------------------------------

MonitorSpaces &WorkspaceState::ensureSpaces(const DesktopKey &desktop,
                                            const MonitorKey &monitor)
{
    // 还没登记过的显示器也补进顺序表：`windowAppeared` 可能比 `monitorsChanged`
    // 先到（启动时收编窗口，那时显示器轮询还没跑完）。
    if (!m_monitorOrder.contains(monitor)) {
        m_monitorOrder.append(monitor);
    }
    MonitorMap &monitors = m_desktops[desktop];
    const auto it = monitors.find(monitor);
    if (it != monitors.end()) {
        return it->second;
    }
    MonitorSpaces fresh;
    fresh.count = m_count;
    fresh.active = 1;
    return monitors.emplace(monitor, fresh).first->second;
}

const MonitorSpaces *WorkspaceState::findSpaces(const DesktopKey &desktop,
                                               const MonitorKey &monitor) const
{
    const auto desktopIt = m_desktops.find(desktop);
    if (desktopIt == m_desktops.end()) {
        return nullptr;
    }
    const auto it = desktopIt->second.find(monitor);
    return it == desktopIt->second.end() ? nullptr : &it->second;
}

std::uint32_t WorkspaceState::clampIndex(std::uint32_t index, const MonitorSpaces &spaces)
{
    if (spaces.count == 0) {
        return 1;
    }
    if (index == 0) {
        return 1;
    }
    return index > spaces.count ? spaces.count : index;
}

std::vector<const WindowRecord *> WorkspaceState::orderedWindows(const DesktopKey &desktop,
                                                                const MonitorKey &monitor,
                                                                std::uint32_t index) const
{
    std::vector<const WindowRecord *> windows;
    for (const auto &entry : m_windows) {
        const WindowRecord &record = entry.second;
        if (record.workspace.desktop != desktop || record.workspace.monitor != monitor
            || record.workspace.index != index) {
            continue;
        }
        windows.push_back(&record);
    }
    std::sort(windows.begin(),
              windows.end(),
              [](const WindowRecord *lhs, const WindowRecord *rhs) {
                  return lhs->zOrder < rhs->zOrder;
              });
    return windows;
}

std::optional<WindowId> WorkspaceState::focusCandidate(const std::vector<const WindowRecord *> &windows,
                                                      std::optional<WindowId> exclude) const
{
    const WindowRecord *best = nullptr;
    for (const WindowRecord *record : windows) {
        if (exclude.has_value() && record->id == *exclude) {
            continue;
        }
        // 最小化的窗口不能当焦点目标：把它「激活」等于把用户刚收起来的窗口又弹
        // 出来。整个空间都最小化时宁可不抢焦点。
        if (record->minimized) {
            continue;
        }
        // 同样近（都是 0，从没拿到过焦点）时取列表里靠后的那个 = Z 序更靠上的。
        if (best == nullptr || record->lastFocusSeq >= best->lastFocusSeq) {
            best = record;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return best->id;
}

void WorkspaceState::orderForShow(std::vector<WindowId> *windows) const
{
    if (windows == nullptr) {
        return;
    }
    std::sort(windows->begin(), windows->end(), [this](WindowId lhs, WindowId rhs) {
        return zOrderOf(m_windows, lhs) < zOrderOf(m_windows, rhs);
    });
}

void WorkspaceState::orderForHide(std::vector<WindowId> *windows) const
{
    if (windows == nullptr) {
        return;
    }
    // 藏的时候反过来：先从最上面那个开始，剩下的窗口不会先闪一下。
    std::sort(windows->begin(), windows->end(), [this](WindowId lhs, WindowId rhs) {
        return zOrderOf(m_windows, lhs) > zOrderOf(m_windows, rhs);
    });
}

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------

WorkspaceEffects WorkspaceState::reconfigure(std::uint32_t count)
{
    WorkspaceEffects effects;
    const std::uint32_t clamped = std::clamp(count, kMinWorkspaceCount, kMaxWorkspaceCount);
    if (clamped == m_count) {
        return effects;
    }

    // 先记下每块屏的旧活动空间：判断「本来可不可见」必须用夹之前的值 —— 活动空间
    // 自己也可能被夹小，夹完之后再算会把「原本藏着的窗口」误判成「本来就在显示」。
    std::map<std::pair<DesktopKey, MonitorKey>, std::uint32_t> previousActive;
    for (auto &desktopEntry : m_desktops) {
        for (auto &monitorEntry : desktopEntry.second) {
            previousActive.emplace(std::make_pair(desktopEntry.first, monitorEntry.first),
                                   monitorEntry.second.active);
            monitorEntry.second.count = clamped;
            if (monitorEntry.second.active > clamped) {
                monitorEntry.second.active = clamped;
            }
        }
    }
    m_count = clamped;

    for (auto &entry : m_windows) {
        WindowRecord &record = entry.second;
        MonitorSpaces &spaces = ensureSpaces(record.workspace.desktop, record.workspace.monitor);
        if (record.homeIndex.has_value() && *record.homeIndex > spaces.count) {
            record.homeIndex = spaces.count;
        }
        const auto previous =
            previousActive.find(std::make_pair(record.workspace.desktop, record.workspace.monitor));
        const std::uint32_t oldActive = previous == previousActive.end() ? 1 : previous->second;
        const bool wasVisible = record.workspace.index == oldActive;
        if (record.workspace.index > spaces.count) {
            // 越界的空间并入最后一个（不是丢掉）。合并进来的排到目标空间最上面。
            record.workspace.index = spaces.count;
            record.zOrder = ++m_zSeq;
        }
        // 活动空间本身也可能被夹小了，所以显隐要按**夹之后**的归属重算：原来藏着的
        // 窗口可能刚好落进新的活动空间（这时必须显式显示，否则它会永远藏在
        // 「活动空间」里，而对活动空间切换是幂等无操作）。
        const bool nowVisible = record.workspace.index == spaces.active;
        if (wasVisible == nowVisible) {
            continue;
        }
        if (nowVisible) {
            effects.show.push_back(record.id);
        } else {
            effects.hide.push_back(record.id);
        }
    }
    orderForShow(&effects.show);
    orderForHide(&effects.hide);
    return effects;
}

// ---------------------------------------------------------------------------
// 环境
// ---------------------------------------------------------------------------

WorkspaceEffects WorkspaceState::setDesktop(const DesktopKey &desktop)
{
    WorkspaceEffects effects;
    if (desktop == m_desktop) {
        return effects;
    }
    m_desktop = desktop;
    // 第一次见到这张虚拟桌面：立刻给它已知的显示器各建一份空间表，这样之后
    // `spaces()` 查得到、活动空间也不会落到懒创建的默认值上。窗口的显隐完全交给
    // Windows（它按 `WS_VISIBLE` 决定在这张桌面上露不露面）。
    MonitorMap &monitors = m_desktops[m_desktop];
    for (const MonitorKey &monitor : m_monitorOrder) {
        if (monitors.find(monitor) == monitors.end()) {
            ensureSpaces(m_desktop, monitor);
        }
    }
    return effects;
}

WorkspaceEffects WorkspaceState::monitorsChanged(const QStringList &present)
{
    WorkspaceEffects effects;

    QStringList ordered;
    for (const QString &device : present) {
        if (device.isEmpty() || ordered.contains(device)) {
            continue;
        }
        ordered.append(device);
    }
    // 一块屏都不剩（正在拔最后一块、或者拿到空列表）：什么都不做。宁可不动窗口，
    // 也不要按一个猜出来的归属把窗口挪来挪去。
    if (ordered.isEmpty()) {
        return effects;
    }

    const QStringList previous = m_monitorOrder;
    QStringList removed;
    for (const QString &device : previous) {
        if (!ordered.contains(device)) {
            removed.append(device);
        }
    }
    m_monitorOrder = ordered;
    const MonitorKey fallback = ordered.first();

    // 1) 拔出：消失的显示器上的窗口并入第一块现存显示器的**活动空间**，同时记下
    //    原归属。看不见的那批必须在这里显示出来 —— 否则它们会落进「活动空间却不
    //    可见」的坏状态，而 `switchTo(活动空间)` 是幂等无操作，再也救不回来。
    for (auto &desktopEntry : m_desktops) {
        const DesktopKey &desktop = desktopEntry.first;
        MonitorMap &monitors = desktopEntry.second;
        for (const QString &gone : removed) {
            const auto goneIt = monitors.find(gone);
            if (goneIt == monitors.end()) {
                continue;
            }
            const std::uint32_t goneActive = goneIt->second.active;
            std::vector<WindowId> moved;
            for (const auto &entry : m_windows) {
                const WindowRecord &record = entry.second;
                if (record.workspace.desktop == desktop && record.workspace.monitor == gone) {
                    moved.push_back(record.id);
                }
            }
            if (!moved.empty()) {
                MonitorSpaces &target = ensureSpaces(desktop, fallback);
                for (const WindowId id : moved) {
                    WindowRecord &record = m_windows.at(id);
                    const bool wasVisible = record.workspace.index == goneActive;
                    record.homeMonitor = gone;
                    record.homeIndex = record.workspace.index;
                    record.workspace.monitor = fallback;
                    record.workspace.index = target.active;
                    record.zOrder = ++m_zSeq;
                    if (!wasVisible) {
                        effects.show.push_back(id);
                    }
                }
            }
            monitors.erase(goneIt);
        }
    }

    // 2) 新接入的显示器：每张已知虚拟桌面都给它建一份空间表（各自的活动空间保持
    //    用户上次在那边留下的值）。
    for (const QString &device : ordered) {
        if (previous.contains(device)) {
            continue;
        }
        for (auto &desktopEntry : m_desktops) {
            ensureSpaces(desktopEntry.first, device);
        }
    }

    // 3) 物归原主：home 那块屏回来了就把窗口搬回去（原空间是它那边的活动空间就
    //    显示，否则隐藏）。
    for (auto &entry : m_windows) {
        WindowRecord &record = entry.second;
        if (!record.homeMonitor.has_value() || !ordered.contains(*record.homeMonitor)) {
            continue;
        }
        const bool wasVisible = isVisible(record.id);
        MonitorSpaces &home = ensureSpaces(record.workspace.desktop, *record.homeMonitor);
        const std::uint32_t index = clampIndex(record.homeIndex.value_or(home.active), home);
        record.workspace.monitor = *record.homeMonitor;
        record.workspace.index = index;
        record.zOrder = ++m_zSeq;
        record.homeMonitor.reset();
        record.homeIndex.reset();
        if (!wasVisible) {
            if (index == home.active) {
                effects.show.push_back(record.id);
            }
        } else if (index != home.active) {
            effects.hide.push_back(record.id);
        }
    }

    orderForShow(&effects.show);
    orderForHide(&effects.hide);
    return effects;
}

// ---------------------------------------------------------------------------
// 命令
// ---------------------------------------------------------------------------

WorkspaceEffects WorkspaceState::switchTo(const MonitorKey &monitor, std::uint32_t index)
{
    WorkspaceEffects effects;
    if (monitor.isEmpty()) {
        return effects;
    }
    MonitorSpaces &spaces = ensureSpaces(m_desktop, monitor);
    const std::uint32_t target = clampIndex(index, spaces);
    // 幂等：目标就是当前活动空间时绝不重复显隐（自触发事件会因此打成一个环）。
    if (target == spaces.active) {
        return effects;
    }
    const std::vector<const WindowRecord *> previous = orderedWindows(m_desktop, monitor, spaces.active);
    spaces.active = target;
    const std::vector<const WindowRecord *> next = orderedWindows(m_desktop, monitor, target);

    // 先显示新的（底 → 顶 = 还原用的 Z 序栈），再隐藏旧的（顶 → 底）。
    for (const WindowRecord *record : next) {
        effects.show.push_back(record->id);
    }
    for (auto it = previous.rbegin(); it != previous.rend(); ++it) {
        effects.hide.push_back((*it)->id);
    }
    effects.focus = focusCandidate(next, std::nullopt);
    return effects;
}

WorkspaceEffects WorkspaceState::moveWindow(WindowId window, std::uint32_t index, bool follow)
{
    WorkspaceEffects effects;
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return effects;
    }
    WindowRecord &record = it->second;
    MonitorSpaces &spaces = ensureSpaces(record.workspace.desktop, record.workspace.monitor);
    const std::uint32_t target = clampIndex(index, spaces);
    if (target == record.workspace.index) {
        return effects; // 已经在那个空间里：无操作
    }
    // 视图跟随只在窗口就在**当前虚拟桌面**上时才有意义（否则要切的是另一张桌面的
    // 空间，那件事不归这里管）。
    const bool followView = follow && record.workspace.desktop == m_desktop;
    const MonitorKey monitor = record.workspace.monitor;
    record.workspace.index = target;
    record.zOrder = ++m_zSeq;
    record.homeMonitor.reset();
    record.homeIndex.reset();
    if (target == spaces.active) {
        // 从藏着的空间被叫回活动空间：露出来并聚焦（用户刚要求的就是它）。
        effects.show.push_back(window);
        effects.focus = window;
        return effects;
    }
    if (followView) {
        // 视图也跟着过去：等价于“先把它放进目标空间，再把那块屏切到目标空间”，
        // 但只算一份效果（否则会先藏后显闪一下）。窗口刚被搬、又刚刚在前台，
        // 所以切完之后聚焦它（而不是让 `focusCandidate()` 去猜）。
        m_lastFocused = window;
        effects = switchTo(monitor, target);
        effects.focus = window;
        return effects;
    }
    effects.hide.push_back(window);
    // 用户正在操作的就是这个窗口时，隐藏它之后前台会停在「那个隐藏窗口」上
    // （M0 实测），所以必须显式把焦点交给活动空间里次近的窗口。
    if (m_lastFocused.has_value() && *m_lastFocused == window) {
        effects.focus = focusCandidate(orderedWindows(record.workspace.desktop,
                                                     record.workspace.monitor,
                                                     spaces.active),
                                       window);
        m_lastFocused.reset();
    }
    return effects;
}

WorkspaceEffects WorkspaceState::revealWindow(WindowId window)
{
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return {}; // 不认识的窗口：等 `windowAppeared` 收编
    }
    WindowRecord &record = it->second;
    if (record.workspace.desktop != m_desktop) {
        // 记录里的虚拟桌面已经过时（窗口刚从别的虚拟桌面被搬到当前这一张）：
        // 就地归入那块屏的当前活动空间，必要时显式显示 —— 这时去切「那张旧桌面」
        // 的空间毫无意义（它已经在这张桌面的活动空间里了）。
        WorkspaceEffects effects;
        const bool wasVisible = isVisible(window);
        MonitorSpaces &spaces = ensureSpaces(m_desktop, record.workspace.monitor);
        record.workspace.desktop = m_desktop;
        record.workspace.index = spaces.active;
        record.homeMonitor.reset();
        record.homeIndex.reset();
        if (!wasVisible) {
            effects.show.push_back(window);
        }
        return effects;
    }
    // 它那块屏的活动空间就是它所在的空间 → 无操作（已经在活动空间里了）。
    return switchTo(record.workspace.monitor, record.workspace.index);
}

WorkspaceEffects WorkspaceState::windowAppeared(WindowId window,
                                                const MonitorKey &monitor,
                                                std::optional<std::uint32_t> index)
{
    WorkspaceEffects effects;
    if (window == 0 || monitor.isEmpty()) {
        return effects;
    }
    // 幂等：已经被跟踪的窗口再「出现」一次不当新窗口处理（我们自己的 SW_HIDE →
    // SW_SHOW 事件靠 M5 的期望事件表吞掉，这里再兜一层）。
    if (m_windows.contains(window)) {
        return effects;
    }
    MonitorSpaces &spaces = ensureSpaces(m_desktop, monitor);
    const std::uint32_t target = clampIndex(index.value_or(spaces.active), spaces);

    WindowRecord record;
    record.id = window;
    record.workspace = WorkspaceId{m_desktop, monitor, target};
    record.zOrder = ++m_zSeq;
    m_windows.emplace(window, record);

    if (target != spaces.active) {
        // 规则把它放进了一个藏着的空间：立刻藏起来，**不抢焦点**。
        effects.hide.push_back(window);
    }
    return effects;
}

WorkspaceEffects WorkspaceState::windowDestroyed(WindowId window)
{
    m_windows.erase(window);
    if (m_lastFocused.has_value() && *m_lastFocused == window) {
        m_lastFocused.reset();
    }
    // 永远不产生效果：不能去显示一个已经没了的窗口（它的 HWND 可能马上被别人复用）。
    return {};
}

WorkspaceEffects WorkspaceState::windowMovedToMonitor(WindowId window, const MonitorKey &monitor)
{
    WorkspaceEffects effects;
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return effects; // 不认识的窗口：等 `windowAppeared` 收编
    }
    WindowRecord &record = it->second;
    if (monitor.isEmpty() || monitor == record.workspace.monitor) {
        return effects;
    }
    const bool wasVisible = isVisible(window);
    MonitorSpaces &spaces = ensureSpaces(record.workspace.desktop, monitor);
    record.workspace.monitor = monitor;
    record.workspace.index = spaces.active;
    record.zOrder = ++m_zSeq;
    record.homeMonitor.reset();
    record.homeIndex.reset();
    if (!wasVisible) {
        // 藏着却还能被拖走（理论上只有隐藏失败才会发生）：让它在新显示器上出现。
        effects.show.push_back(window);
    }
    return effects;
}

WorkspaceEffects WorkspaceState::windowReboundToMonitor(WindowId window, const MonitorKey &monitor)
{
    WorkspaceEffects effects;
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return effects; // 不认识的窗口：等 `windowAppeared` 收编
    }
    WindowRecord &record = it->second;
    if (monitor.isEmpty() || monitor == record.workspace.monitor) {
        return effects;
    }
    const bool wasVisible = isVisible(window);
    record.workspace.monitor = monitor;
    MonitorSpaces &spaces = ensureSpaces(record.workspace.desktop, monitor);
    // 空间号**不动**：它是规则 / 用户给的，换屏不该把它没收。
    record.workspace.index = clampIndex(record.workspace.index, spaces);
    record.zOrder = ++m_zSeq;
    record.homeMonitor.reset();
    record.homeIndex.reset();
    const bool nowVisible = record.workspace.index == spaces.active;
    if (wasVisible && !nowVisible) {
        effects.hide.push_back(window);
    } else if (!wasVisible && nowVisible) {
        effects.show.push_back(window);
    }
    return effects;
}

WorkspaceEffects WorkspaceState::windowMinimized(WindowId window, bool minimized)
{
    const auto it = m_windows.find(window);
    if (it == m_windows.end() || it->second.minimized == minimized) {
        return {};
    }
    it->second.minimized = minimized;
    return {};
}

WorkspaceEffects WorkspaceState::focusChanged(WindowId window)
{
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return {}; // 不认识的窗口：等 `windowAppeared` 收编
    }
    // 幂等：同一次前景变化会被 `EVENT_SYSTEM_FOREGROUND` 与「窗口出现」两条路各报
    // 一遍，重复处理没有意义。
    if (m_lastFocused.has_value() && *m_lastFocused == window) {
        return {};
    }
    m_lastFocused = window;
    it->second.lastFocusSeq = ++m_focusSeq;
    it->second.zOrder = ++m_zSeq;
    // 拿到焦点说明它已经还原了（最小化的窗口不可能拿到焦点）。
    it->second.minimized = false;
    // 前台事件绝不能反过来引起显隐：那会与我们自己的切换互相追尾。
    return {};
}

WorkspaceEffects WorkspaceState::restoreAll()
{
    WorkspaceEffects effects;
    for (auto &entry : m_windows) {
        WindowRecord &record = entry.second;
        const bool wasVisible = isVisible(record.id);
        MonitorSpaces &spaces = ensureSpaces(record.workspace.desktop, record.workspace.monitor);
        record.workspace.index = spaces.active;
        record.homeMonitor.reset();
        record.homeIndex.reset();
        if (!wasVisible) {
            effects.show.push_back(record.id);
        }
    }
    orderForShow(&effects.show);
    return effects;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

bool WorkspaceState::tracks(WindowId window) const
{
    return m_windows.find(window) != m_windows.end();
}

std::vector<WindowId> WorkspaceState::trackedWindows() const
{
    std::vector<WindowId> ids;
    ids.reserve(m_windows.size());
    // `m_windows` 本身就是按句柄升序的 map。
    for (const auto &entry : m_windows) {
        ids.push_back(entry.first);
    }
    return ids;
}

std::optional<WindowRecord> WorkspaceState::record(WindowId window) const
{
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool WorkspaceState::isVisible(WindowId window) const
{
    const auto it = m_windows.find(window);
    if (it == m_windows.end()) {
        return false;
    }
    const MonitorSpaces *found = findSpaces(it->second.workspace.desktop, it->second.workspace.monitor);
    return found != nullptr && found->active == it->second.workspace.index;
}

std::optional<MonitorSpaces> WorkspaceState::spaces(const DesktopKey &desktop,
                                                    const MonitorKey &monitor) const
{
    const MonitorSpaces *found = findSpaces(desktop, monitor);
    if (found == nullptr) {
        return std::nullopt;
    }
    return *found;
}

std::optional<MonitorSpaces> WorkspaceState::spaces(const MonitorKey &monitor) const
{
    return spaces(m_desktop, monitor);
}

std::vector<WindowId> WorkspaceState::windowsInSpace(const DesktopKey &desktop,
                                                     const MonitorKey &monitor,
                                                     std::uint32_t index) const
{
    std::vector<WindowId> ids;
    for (const WindowRecord *record : orderedWindows(desktop, monitor, index)) {
        ids.push_back(record->id);
    }
    return ids;
}

std::vector<WindowId> WorkspaceState::windowsInSpace(const MonitorKey &monitor,
                                                     std::uint32_t index) const
{
    return windowsInSpace(m_desktop, monitor, index);
}

} // namespace flowkeyd::core
