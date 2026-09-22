#include "core/placement.h"

#include "core/window_match.h"

#include <algorithm>

namespace flowkeyd::core {

QString normalizeDeviceName(const QString &device)
{
    QString text = device.trimmed();
    if (text.startsWith(QLatin1String("\\\\.\\"))) {
        text = text.mid(4);
    } else if (text.startsWith(QLatin1String("\\\\?\\"))) {
        text = text.mid(4);
    }
    return text.toUpper();
}

std::vector<MonitorDescription> sortedMonitors(std::vector<MonitorDescription> monitors)
{
    std::sort(monitors.begin(), monitors.end(), [](const MonitorDescription &a, const MonitorDescription &b) {
        if (a.bounds.x != b.bounds.x) {
            return a.bounds.x < b.bounds.x;
        }
        if (a.bounds.y != b.bounds.y) {
            return a.bounds.y < b.bounds.y;
        }
        return normalizeDeviceName(a.device) < normalizeDeviceName(b.device);
    });
    return monitors;
}

std::optional<std::size_t> selectMonitor(const std::vector<MonitorDescription> &sorted,
                                         const MonitorRef &ref)
{
    switch (ref.kind) {
    case MonitorRef::Kind::Index:
        if (ref.index == 0 || ref.index > sorted.size()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(ref.index) - 1;
    case MonitorRef::Kind::Primary:
        for (std::size_t index = 0; index < sorted.size(); ++index) {
            if (sorted[index].primary) {
                return index;
            }
        }
        return std::nullopt;
    case MonitorRef::Kind::Device: {
        const QString wanted = normalizeDeviceName(ref.device);
        for (std::size_t index = 0; index < sorted.size(); ++index) {
            if (normalizeDeviceName(sorted[index].device) == wanted) {
                return index;
            }
        }
        return std::nullopt;
    }
    }
    return std::nullopt;
}

QStringList newMonitorDevices(const QStringList &previous, const QStringList &current)
{
    QStringList out;
    for (const QString &device : current) {
        const QString normalized = normalizeDeviceName(device);
        if (normalized.isEmpty() || out.contains(normalized)) {
            continue;
        }
        const bool known = std::any_of(previous.begin(), previous.end(), [&normalized](const QString &old) {
            return normalizeDeviceName(old) == normalized;
        });
        if (!known) {
            out.append(normalized);
        }
    }
    return out;
}

Rect placementRect(const Rect &current,
                   const MonitorDescription &target,
                   bool maximize,
                   std::optional<std::int32_t> x,
                   std::optional<std::int32_t> y,
                   std::optional<std::uint32_t> width,
                   std::optional<std::uint32_t> height)
{
    const Rect &work = target.work;
    if (maximize) {
        return work;
    }

    int w = static_cast<int>(width.value_or(static_cast<std::uint32_t>(std::max(current.width, 1))));
    int h = static_cast<int>(height.value_or(static_cast<std::uint32_t>(std::max(current.height, 1))));
    w = std::max(w, 1);
    h = std::max(h, 1);

    int px = x.has_value() ? work.x + *x : work.x + (work.width - w) / 2;
    int py = y.has_value() ? work.y + *y : work.y + (work.height - h) / 2;

    // 夹进工作区；窗口比工作区还大时对齐左上角，保证标题栏可见。
    if (w <= work.width) {
        px = std::clamp(px, work.x, work.x + work.width - w);
    } else {
        px = work.x;
    }
    if (h <= work.height) {
        py = std::clamp(py, work.y, work.y + work.height - h);
    } else {
        py = work.y;
    }
    return Rect{px, py, w, h};
}

bool windowMatchesRule(const WindowRule &rule,
                       const QString &title,
                       const std::optional<QString> &executableName)
{
    if (!rule.title.has_value() && !rule.process.has_value()) {
        return false;
    }
    if (rule.title.has_value() && !windowTitleMatches(title, rule.title)) {
        return false;
    }
    if (rule.process.has_value() && !windowProcessMatches(executableName, rule.process)) {
        return false;
    }
    return true;
}

} // namespace flowkeyd::core
