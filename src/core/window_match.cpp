#include "core/window_match.h"

namespace flowkeyd::core {

namespace {

/// 大小写无关的子串匹配；`needle` 为空表示不限制。
bool containsFolded(const QString &haystack, const std::optional<QString> &needle)
{
    if (!needle.has_value() || needle->isEmpty()) {
        return true;
    }
    return haystack.contains(*needle, Qt::CaseInsensitive);
}

} // namespace

QString executableBaseName(const QString &fullPath)
{
    const qsizetype slash = fullPath.lastIndexOf(QLatin1Char('\\'));
    const qsizetype forward = fullPath.lastIndexOf(QLatin1Char('/'));
    const qsizetype cut = std::max(slash, forward);
    const QString name = cut >= 0 ? fullPath.mid(cut + 1) : fullPath;
    return name.toLower();
}

bool windowTitleMatches(const QString &title, const std::optional<QString> &needle)
{
    return containsFolded(title, needle);
}

bool windowProcessMatches(const std::optional<QString> &executableName,
                          const std::optional<QString> &needle)
{
    if (!needle.has_value() || needle->isEmpty()) {
        return true;
    }
    if (!executableName.has_value()) {
        return false;
    }
    return executableName->contains(*needle, Qt::CaseInsensitive);
}

bool windowMatchesQuery(const WindowQuery &query,
                        const QString &title,
                        const std::optional<QString> &executableName)
{
    return windowTitleMatches(title, query.title) && windowProcessMatches(executableName, query.process);
}

WindowPlan planWindowAction(WindowOp op, std::optional<bool> toggle, bool alreadyActive)
{
    if (op == WindowOp::Activate && toggle.value_or(true) && alreadyActive) {
        return WindowPlan::MinimizeBecauseActive;
    }
    return WindowPlan::ApplyOp;
}

bool windowOpHasTransition(WindowOp op)
{
    switch (op) {
    case WindowOp::Activate:
    case WindowOp::Minimize:
    case WindowOp::Maximize:
    case WindowOp::Restore:
        return true;
    case WindowOp::Close:
    case WindowOp::ToggleTopmost:
        return false;
    }
    return false;
}

} // namespace flowkeyd::core
