#include "app/app_list_model.h"

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 `window_list_model` / `help_model` 同一套纵向度量）。
constexpr int kColumns = 5;
constexpr int kCellWidth = 104;
constexpr int kCellHeight = 88;
constexpr int kIconSize = 40;
constexpr int kPad = 12;
constexpr int kInset = 10;
constexpr int kFilterHeight = 30;
constexpr int kListGap = 8;
constexpr int kFooterHeight = 24;
constexpr int kFooterGap = 8;
constexpr int kCardRadius = 12;
constexpr int kMaxRows = 6;

/// 卡片宽度：两边的内边距与缩进 + 正好五格。
constexpr int kInnerWidth = kColumns * kCellWidth;
constexpr int kCardWidth = kInnerWidth + 2 * kPad + 2 * kInset;

/// 网格区顶部：卡片内边距 + 筛选框 + 间距（与 `window_list_model` 一致，
/// 卡片里没有标题行，筛选框就是第一行）。
constexpr int kListTop = kPad + kFilterHeight + kListGap;
/// 网格区底部：底部提示 + 它上面的空隙 + 卡片内边距。
constexpr int kListBottom = kFooterGap + kFooterHeight + kPad;

QVariantMap noneDecision()
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);
    return result;
}

} // namespace

AppListModel::AppListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    relayout();
}

void AppListModel::setItems(std::optional<QString> title, std::vector<AppListEntry> items)
{
    beginResetModel();
    m_title = std::move(title);
    m_items = std::move(items);
    m_names.clear();
    m_names.reserve(m_items.size());
    for (const AppListEntry &entry : m_items) {
        m_names.push_back(entry.name.toLower());
    }
    m_filter.clear();
    refilter();
    endResetModel();
    emit itemsChanged();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::setMaxRows(int rows)
{
    const int next = rows > 0 ? rows : 1;
    if (next == m_maxRows) {
        return;
    }
    beginResetModel();
    m_maxRows = next;
    relayout();
    endResetModel();
    emit stateChanged();
}

int AppListModel::rowsForAvailableHeight(int availableHeight)
{
    // 卡片上下内边距 + 筛选框（含它下面的空隙）+ 底部提示（含它上面的空隙）。
    const int chrome = kPad * 2 + kFilterHeight + kListGap + kFooterGap + kFooterHeight;
    // 上下各留一点，让卡片不与屏幕边缘贴住。
    const int available = availableHeight - chrome - 48;
    const int fits = available / kCellHeight;
    return std::clamp(std::max(fits, 1), 1, kMaxRows);
}

QString AppListModel::caption() const
{
    // 无边框窗口，这行字用户看不到：它只是**窗口标题**，验收 / 诊断脚本靠它读
    // 「现在列了几个程序」。`apps("程序")` 给的名字也出现在这里。
    const QString name = m_title.value_or(tr("flowkeyd 程序"));
    return tr("%1 — %2 个").arg(name).arg(visibleCount());
}

int AppListModel::cardWidth() const
{
    return kCardWidth;
}

int AppListModel::cardRadius() const
{
    return kCardRadius;
}

QString AppListModel::filterPlaceholder() const
{
    return tr("输入程序名筛选…");
}

QString AppListModel::emptyMessage() const
{
    return m_items.empty() ? tr("没有找到程序") : tr("没有匹配的程序");
}

QString AppListModel::footerText() const
{
    return tr("%1 个程序    ↑↓←→ 选择    Enter 启动    Esc 关闭").arg(visibleCount());
}

int AppListModel::columns() const
{
    return kColumns;
}

int AppListModel::cellWidth() const
{
    return kCellWidth;
}

int AppListModel::cellHeight() const
{
    return kCellHeight;
}

int AppListModel::iconSize() const
{
    return kIconSize;
}

int AppListModel::listTop() const
{
    return kListTop;
}

int AppListModel::listBottom() const
{
    return kListBottom;
}

std::optional<int> AppListModel::itemIndexForVisible(int line) const
{
    if (line < 0 || line >= visibleCount()) {
        return std::nullopt;
    }
    return m_visible[static_cast<std::size_t>(line)];
}

QVariantMap AppListModel::setFilter(const QString &filter)
{
    if (filter == m_filter) {
        return noneDecision();
    }
    beginResetModel();
    m_filter = filter;
    refilter();
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
    return noneDecision();
}

void AppListModel::clearFilter()
{
    if (m_filter.isEmpty()) {
        return;
    }
    beginResetModel();
    m_filter.clear();
    refilter();
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::reset()
{
    beginResetModel();
    m_filter.clear();
    refilter();
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::moveSelection(int delta)
{
    if (m_visible.empty() || delta == 0) {
        return;
    }
    const int last = visibleCount() - 1;
    const int next = std::clamp(m_selected + delta, 0, last);
    if (next == m_selected) {
        return;
    }
    m_selected = next;
    emit selectedChanged();
    notifyRows();
}

void AppListModel::setHover(int line)
{
    if (line < 0 || m_visible.empty()) {
        return;
    }
    const int next = std::clamp(line, 0, visibleCount() - 1);
    if (next == m_selected) {
        return;
    }
    m_selected = next;
    emit selectedChanged();
    notifyRows();
}

QVariantMap AppListModel::activate(int line)
{
    if (m_visible.empty()) {
        return noneDecision();
    }
    const int next = std::clamp(line, 0, visibleCount() - 1);
    if (next != m_selected) {
        m_selected = next;
        emit selectedChanged();
        notifyRows();
    }
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("choose"));
    result.insert(QStringLiteral("index"), m_visible[static_cast<std::size_t>(next)]);
    result.insert(QStringLiteral("handled"), true);
    return result;
}

QVariantMap AppListModel::handleKey(int key)
{
    switch (key) {
    case Qt::Key_Escape: {
        QVariantMap result;
        result.insert(QStringLiteral("decision"), QStringLiteral("cancel"));
        result.insert(QStringLiteral("index"), -1);
        result.insert(QStringLiteral("handled"), true);
        return result;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return activate(m_selected);
    case Qt::Key_Left:
        moveSelection(-1);
        break;
    case Qt::Key_Right:
        moveSelection(1);
        break;
    case Qt::Key_Up:
        moveSelection(-kColumns);
        break;
    case Qt::Key_Down:
        moveSelection(kColumns);
        break;
    case Qt::Key_Home:
        moveSelection(-visibleCount());
        break;
    case Qt::Key_End:
        moveSelection(visibleCount());
        break;
    case Qt::Key_PageUp:
        moveSelection(-kColumns * m_rows);
        break;
    case Qt::Key_PageDown:
        moveSelection(kColumns * m_rows);
        break;
    default: {
        // 其余的键（字符、退格、输入法的候选键……）全部放行给筛选框那个标准
        // `TextField`，`↑`/`↓`/`←`/`→` 会被它故意忽略、冒到这里（见 AGENTS.md）。
        QVariantMap result = noneDecision();
        result.insert(QStringLiteral("handled"), false);
        return result;
    }
    }
    return noneDecision();
}

int AppListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    // 全部筛选结果都交给网格（它自己决定画哪几格、滚到哪里）。
    return visibleCount();
}

QVariant AppListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const int line = index.row();
    const std::optional<int> itemIndex = itemIndexForVisible(line);
    if (!itemIndex.has_value()) {
        return {};
    }
    const AppListEntry &entry = m_items[static_cast<std::size_t>(*itemIndex)];
    switch (role) {
    case AppNameRole:
        return entry.name;
    case AppIconRole:
        return entry.iconSource;
    case RowSelectedRole:
        return activeLine() == line;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> AppListModel::roleNames() const
{
    return {
        {AppNameRole, QByteArrayLiteral("appName")},
        {AppIconRole, QByteArrayLiteral("appIcon")},
        {RowSelectedRole, QByteArrayLiteral("rowSelected")},
    };
}

void AppListModel::refilter()
{
    const QString needle = m_filter.trimmed().toLower();
    m_visible.clear();
    for (std::size_t index = 0; index < m_names.size(); ++index) {
        if (needle.isEmpty() || m_names[index].contains(needle)) {
            m_visible.push_back(static_cast<int>(index));
        }
    }
    // 筛选之后永远从第 1 格开始（高亮停在一个已经不存在的行号上比从头开始更糟）。
    m_selected = 0;
    relayout();
}

void AppListModel::relayout()
{
    const int lines = visibleCount();
    const int rows = (lines + kColumns - 1) / kColumns;
    m_rows = std::clamp(std::max(rows, 1), 1, std::max(m_maxRows, 1));
    m_cardHeight = kListTop + m_rows * kCellHeight + kListBottom;
    m_filterRect = PopupRect{kPad + kInset, kPad, kInnerWidth, kFilterHeight};
    m_footerRect = PopupRect{kPad + kInset, m_cardHeight - kPad - kFooterHeight, kInnerWidth,
                             kFooterHeight};
}

void AppListModel::notifyRows()
{
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    emit stateChanged();
}

int AppListModel::activeLine() const
{
    if (m_visible.empty()) {
        return -1;
    }
    return std::clamp(m_selected, 0, visibleCount() - 1);
}

} // namespace flowkeyd::app
