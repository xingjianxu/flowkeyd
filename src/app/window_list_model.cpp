#include "app/window_list_model.h"

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 `help_model` 同一套纵向度量，卡片更宽一点，
// 好放下「标题 —— 进程名」）。
constexpr int kCardWidth = 560;
constexpr int kPad = 12;
constexpr int kHeaderHeight = 30;
constexpr int kFilterGap = 8;
constexpr int kFilterHeight = 30;
constexpr int kListGap = 8;
constexpr int kRowHeight = 46;
constexpr int kRowGap = 2;
constexpr int kFooterHeight = 24;
constexpr int kFooterGap = 8;
constexpr int kInset = 10;
constexpr int kCardRadius = 12;
constexpr int kMaxRows = 12;

/// 列表区顶部：表头 + 间距 + 筛选框 + 间距。
constexpr int kListTop = kPad + kHeaderHeight + kFilterGap + kFilterHeight + kListGap;
/// 列表区底部：底部提示 + 它上面的空隙 + 卡片内边距。
constexpr int kListBottom = kFooterGap + kFooterHeight + kPad;

/// 一个窗口参与筛选的文本（小写，只在装载时算一次）。
///
/// 进程名排在前面，标题跟在后面：两者都能匹配（`proc` 命中 `chrome.exe`，
/// 也可以直接打标题里的词），但进程名是用户最常用的入口。
QString haystack(const WindowListEntry &entry)
{
    QString text = entry.process;
    text += QLatin1Char(' ');
    text += entry.title;
    return text.toLower();
}

QVariantMap noneDecision()
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);
    return result;
}

} // namespace

WindowListModel::WindowListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    relayout();
}

void WindowListModel::setItems(std::optional<QString> title, std::vector<WindowListEntry> items)
{
    beginResetModel();
    m_title = std::move(title);
    m_items = std::move(items);
    m_haystacks.clear();
    m_haystacks.reserve(m_items.size());
    for (const WindowListEntry &entry : m_items) {
        m_haystacks.push_back(haystack(entry));
    }
    m_filter.clear();
    refilter();
    endResetModel();
    emit itemsChanged();
    emit stateChanged();
    emit selectedChanged();
}

void WindowListModel::setMaxRows(int rows)
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

int WindowListModel::rowsForAvailableHeight(int availableHeight)
{
    // 卡片上下内边距 + 标题 + 筛选框（含间距）+ 底部提示（含它上面的空隙）。
    const int chrome = kPad * 2 + kHeaderHeight + kFilterGap + kFilterHeight + kListGap
                       + kFooterGap + kFooterHeight;
    // 上下各留一点，让卡片不与屏幕边缘贴住。
    const int available = availableHeight - chrome - 48;
    const int fits = available / (kRowHeight + kRowGap);
    return std::clamp(std::max(fits, 1), 1, kMaxRows);
}

QString WindowListModel::title() const
{
    return m_title.value_or(tr("窗口"));
}

QString WindowListModel::caption() const
{
    return tr("flowkeyd 窗口 — %1 个").arg(visibleCount());
}

QString WindowListModel::countText() const
{
    if (visibleCount() == totalCount()) {
        return tr("%1 个窗口").arg(totalCount());
    }
    return tr("%1 / %2 个窗口").arg(visibleCount()).arg(totalCount());
}

int WindowListModel::cardWidth() const
{
    return kCardWidth;
}

int WindowListModel::cardRadius() const
{
    return kCardRadius;
}

QString WindowListModel::filterPlaceholder() const
{
    return tr("输入进程名或标题筛选…");
}

QString WindowListModel::emptyMessage() const
{
    return m_items.empty() ? tr("没有打开的窗口") : tr("没有匹配的窗口");
}

QString WindowListModel::footerText() const
{
    return tr("输入筛选    ↑↓ 选择    Enter 切换    Esc 关闭");
}

int WindowListModel::rowHeight() const
{
    return kRowHeight;
}

int WindowListModel::rowSpacing() const
{
    return kRowGap;
}

int WindowListModel::listTop() const
{
    return kListTop;
}

int WindowListModel::listBottom() const
{
    return kListBottom;
}

int WindowListModel::rowInset() const
{
    return kInset;
}

std::optional<int> WindowListModel::itemIndexForVisible(int line) const
{
    if (line < 0 || line >= visibleCount()) {
        return std::nullopt;
    }
    return m_visible[static_cast<std::size_t>(line)];
}

QVariantMap WindowListModel::setFilter(const QString &filter)
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

    // 「只有一个窗口匹配」时直接激活它。空的筛选（刚打开）绝不触发，
    // 否则窗口一弹出来就会把唯一一个窗口切走。
    if (!m_filter.trimmed().isEmpty() && visibleCount() == 1) {
        QVariantMap result;
        result.insert(QStringLiteral("decision"), QStringLiteral("choose"));
        result.insert(QStringLiteral("index"), m_visible.front());
        result.insert(QStringLiteral("handled"), true);
        return result;
    }
    return noneDecision();
}

void WindowListModel::clearFilter()
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

void WindowListModel::reset()
{
    beginResetModel();
    m_filter.clear();
    refilter();
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void WindowListModel::moveSelection(int delta)
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

void WindowListModel::setHover(int line)
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

QVariantMap WindowListModel::activate(int line)
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

QVariantMap WindowListModel::handleKey(int key)
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
    case Qt::Key_Up:
        moveSelection(-1);
        break;
    case Qt::Key_Down:
        moveSelection(1);
        break;
    case Qt::Key_PageUp:
        moveSelection(-m_rows);
        break;
    case Qt::Key_PageDown:
        moveSelection(m_rows);
        break;
    default: {
        // 其余的键（字符、退格、`Home`/`End`、左右方向键、输入法的候选键……）
        // 全部放行给筛选框那个标准 `TextField`。
        QVariantMap result = noneDecision();
        result.insert(QStringLiteral("handled"), false);
        return result;
    }
    }
    return noneDecision();
}

int WindowListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    // 全部筛选结果都交给 `ListView`（它自己决定画哪几条、滚到哪里）。
    return visibleCount();
}

QVariant WindowListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const int line = index.row();
    const std::optional<int> itemIndex = itemIndexForVisible(line);
    if (!itemIndex.has_value()) {
        return {};
    }
    const WindowListEntry &entry = m_items[static_cast<std::size_t>(*itemIndex)];
    switch (role) {
    case WindowTitleRole:
        return entry.title;
    case WindowProcessRole:
        return entry.process;
    case RowSelectedRole:
        return activeLine() == line;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> WindowListModel::roleNames() const
{
    return {
        {WindowTitleRole, QByteArrayLiteral("windowTitle")},
        {WindowProcessRole, QByteArrayLiteral("windowProcess")},
        {RowSelectedRole, QByteArrayLiteral("rowSelected")},
    };
}

void WindowListModel::refilter()
{
    const QString needle = m_filter.trimmed().toLower();
    m_visible.clear();
    for (std::size_t index = 0; index < m_haystacks.size(); ++index) {
        if (needle.isEmpty() || m_haystacks[index].contains(needle)) {
            m_visible.push_back(static_cast<int>(index));
        }
    }
    m_selected = 0;
    relayout();
}

void WindowListModel::relayout()
{
    m_rows = std::clamp(visibleCount(), 1, std::max(m_maxRows, 1));
    // 与 `ListView` 的内容高度严格对齐：header(listTop) + 行数 * (行高 + 空隙)
    // + footer(listBottom)。这样「内容放不下」就等价于「可见条数 > 能画的行数」。
    m_cardHeight = kListTop + m_rows * (kRowHeight + kRowGap) + kListBottom;
    const int inner = kCardWidth - 2 * kPad - 2 * kInset;
    m_titleRect = PopupRect{kPad + kInset, kPad, inner * 6 / 10, kHeaderHeight};
    m_countRect = PopupRect{kPad + kInset, kPad, inner, kHeaderHeight};
    m_filterRect = PopupRect{kPad + kInset, kPad + kHeaderHeight + kFilterGap, inner, kFilterHeight};
    m_footerRect =
        PopupRect{kPad + kInset, m_cardHeight - kPad - kFooterHeight, inner, kFooterHeight};
}

void WindowListModel::notifyRows()
{
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    emit stateChanged();
}

int WindowListModel::activeLine() const
{
    if (m_visible.empty()) {
        return -1;
    }
    return std::clamp(m_selected, 0, visibleCount() - 1);
}

} // namespace flowkeyd::app
