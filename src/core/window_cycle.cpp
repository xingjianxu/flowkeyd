#include "core/window_cycle.h"

namespace flowkeyd::core {

std::optional<std::size_t> chooseCycleMonitor(const std::optional<std::size_t> &cursorMonitor,
                                              const std::optional<std::size_t> &foregroundMonitor,
                                              const std::optional<std::size_t> &topmostMonitor)
{
    if (cursorMonitor.has_value()) {
        return cursorMonitor;
    }
    if (foregroundMonitor.has_value()) {
        return foregroundMonitor;
    }
    return topmostMonitor;
}

std::optional<WindowCyclePlan> planWindowCycle(const WindowCycleState &state,
                                               const QStringList &candidates,
                                               const QString &foreground,
                                               int step)
{
    if (candidates.isEmpty() || step == 0) {
        return std::nullopt;
    }

    // 去重但保持顺序（同一批窗口里偶尔会出现重复的 id，重复只会让环乱掉）。
    QStringList unique;
    unique.reserve(candidates.size());
    for (const QString &id : candidates) {
        if (!id.isEmpty() && !unique.contains(id)) {
            unique.append(id);
        }
    }
    if (unique.isEmpty()) {
        return std::nullopt;
    }
    // 只有它自己：没有“下一个”可去，也别去激活一个已经在台前的窗口。
    if (unique.size() == 1 && unique.constFirst() == foreground) {
        return std::nullopt;
    }

    // 上次的顺序里只留下还在眼前的那几个窗口，相对顺序不变。
    QStringList ring;
    ring.reserve(state.ring.size());
    for (const QString &id : state.ring) {
        if (unique.contains(id) && !ring.contains(id)) {
            ring.append(id);
        }
    }

    const bool reusable = ring.size() >= 2 && !state.anchor.isEmpty()
                          && state.anchor == foreground && ring.contains(state.anchor);

    WindowCyclePlan plan;
    if (!reusable) {
        // 重新按 Z 序排一次（锚点/前台窗口本来就在最前面）。
        ring = unique;
        plan.restarted = true;
    }

    const int count = static_cast<int>(ring.size());
    const int anchorIndex = ring.indexOf(plan.restarted ? foreground : state.anchor);
    int targetIndex = 0;
    if (anchorIndex < 0) {
        // 前台窗口不在这批候选里（例如它是覆盖层、或者刚被关掉）：从头/尾取一个。
        targetIndex = step > 0 ? 0 : count - 1;
    } else {
        targetIndex = ((anchorIndex + step) % count + count) % count;
    }

    plan.target = ring.at(targetIndex);
    plan.state.ring = ring;
    plan.state.anchor = plan.target;
    return plan;
}

} // namespace flowkeyd::core
