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

bool isMainWindow(const TopLevelWindowFacts &window)
{
    if (!window.visible || window.isShellWindow || window.toolWindow) {
        return false;
    }
    // 有属主的窗口是对话框 / 弹出菜单，一般不单独出现在任务栏与 Alt+Tab 里；
    // 只有应用显式写了 `WS_EX_APPWINDOW` 才把它当独立窗口（照抄任务栏的规则）。
    if (window.hasOwner && !window.appWindow) {
        return false;
    }
    return window.hasTitle && window.hasArea;
}

bool isSwitchableWindow(const TopLevelWindowFacts &window)
{
    if (!isMainWindow(window)) {
        return false;
    }
    // 被藏起来、又留在当前桌面上的窗口用户切不过去；cloaked 但在别的桌面上的
    // 窗口要留住（切换器会切过去）。
    return !(window.cloaked && window.onCurrentDesktop);
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
    // 跨显示器移动会改变窗口几何，所以有过渡动画；跨虚拟桌面移动不会。
    case WindowOp::MoveLeftMonitor:
    case WindowOp::MoveRightMonitor:
        return true;
    case WindowOp::Close:
    case WindowOp::ToggleTopmost:
    case WindowOp::MovePrevDesktop:
    case WindowOp::MoveNextDesktop:
        return false;
    }
    return false;
}

} // namespace flowkeyd::core
