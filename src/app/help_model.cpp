#include "app/help_model.h"

#include <QChar>

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 oskeyd 的 `help.rs::Metrics` 同源）。
constexpr int kCardWidth = 500;
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
constexpr int kKeysWidth = 150;
constexpr int kBadgeHeight = 19;
constexpr int kBadgePad = 7;
constexpr int kBadgeGap = 3;
constexpr int kCardRadius = 12;
constexpr int kMaxRows = 12;

/// 列表区顶部：表头 + 间距 + 筛选框 + 间距。
constexpr int kListTop = kPad + kHeaderHeight + kFilterGap + kFilterHeight + kListGap;
/// 列表区底部：底部提示 + 它上面的空隙 + 卡片内边距。
constexpr int kListBottom = kFooterGap + kFooterHeight + kPad;

/// 一条条目参与筛选的文本（小写，只在装载时算一次）。
QString haystack(const HelpEntry &item)
{
    QString text = item.chords.join(QLatin1Char(' '));
    text += QLatin1Char(' ');
    text += item.label;
    if (item.detail.has_value()) {
        text += QLatin1Char(' ');
        text += *item.detail;
    }
    return text.toLower();
}

} // namespace

HelpModel::HelpModel(QObject *parent)
    : QAbstractListModel(parent)
{
    relayout();
}

void HelpModel::setItems(std::optional<QString> title, std::vector<HelpEntry> items)
{
    beginResetModel();
    m_title = std::move(title);
    m_items = std::move(items);
    m_haystacks.clear();
    m_haystacks.reserve(m_items.size());
    for (const HelpEntry &item : m_items) {
        m_haystacks.push_back(haystack(item));
    }
    m_filter.clear();
    refilter();
    endResetModel();
    emit itemsChanged();
    emit stateChanged();
    // 换了一批条目（同一个窗口复用），视图要回到第一条。
    emit selectedChanged();
}

void HelpModel::setMaxRows(int rows)
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

QString HelpModel::caption() const
{
    return tr("flowkeyd 快捷键 — %1/%2 项").arg(visibleCount()).arg(totalCount());
}

QString HelpModel::countText() const
{
    if (visibleCount() == totalCount()) {
        return tr("%1 项").arg(totalCount());
    }
    return tr("%1 / %2 项").arg(visibleCount()).arg(totalCount());
}

int HelpModel::cardRadius() const
{
    return kCardRadius;
}

QString HelpModel::filterPlaceholder() const
{
    return tr("输入以筛选…");
}

QString HelpModel::emptyMessage() const
{
    return m_items.empty() ? tr("配置里还没有快捷键") : tr("没有匹配的快捷键");
}

std::optional<int> HelpModel::hover() const
{
    if (m_hover < 0) {
        return std::nullopt;
    }
    return m_hover;
}

QString HelpModel::footerText() const
{
    return tr("输入筛选    ↑↓ 滚动    Enter 复制    Esc 关闭");
}

int HelpModel::cardWidth() const
{
    return kCardWidth;
}

int HelpModel::rowsForAvailableHeight(int availableHeight)
{
    // 卡片上下内边距 + 标题 + 筛选框（含间距）+ 底部提示（含它上面的空隙）。
    const int chrome = kPad * 2 + kHeaderHeight + kFilterGap + kFilterHeight + kListGap
                       + kFooterGap + kFooterHeight;
    // 上下各留一点，让卡片不与屏幕边缘贴住。
    const int available = availableHeight - chrome - 48;
    const int fits = available / (kRowHeight + kRowGap);
    return std::clamp(std::max(fits, 1), 1, kMaxRows);
}

int HelpModel::rowHeight() const
{
    return kRowHeight;
}

int HelpModel::rowSpacing() const
{
    return kRowGap;
}

int HelpModel::listTop() const
{
    return kListTop;
}

int HelpModel::listBottom() const
{
    return kListBottom;
}

int HelpModel::keysWidth() const
{
    return kKeysWidth;
}

int HelpModel::badgeHeight() const
{
    return kBadgeHeight;
}

int HelpModel::badgePad() const
{
    return kBadgePad;
}

int HelpModel::badgeGap() const
{
    return kBadgeGap;
}

int HelpModel::rowInset() const
{
    return kInset;
}

std::optional<int> HelpModel::itemIndexForVisible(int line) const
{
    if (line < 0 || line >= visibleCount()) {
        return std::nullopt;
    }
    return m_visible[static_cast<std::size_t>(line)];
}

void HelpModel::setFilter(const QString &filter)
{
    if (filter == m_filter) {
        return;
    }
    beginResetModel();
    m_filter = filter;
    refilter();
    endResetModel();
    emit stateChanged();
    // 筛选之后列表短了：让视图把选中项（第 0 条）带回视野。
    emit selectedChanged();
}

void HelpModel::clearFilter()
{
    setFilter(QString());
}

void HelpModel::reset()
{
    beginResetModel();
    m_filter.clear();
    refilter();
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

QString HelpModel::copyText() const
{
    const int line = activeLine();
    if (line < 0) {
        return QString();
    }
    return copyTextForVisible(line);
}

QString HelpModel::copyTextForVisible(int line) const
{
    const std::optional<int> index = itemIndexForVisible(line);
    if (!index.has_value()) {
        return QString();
    }
    return m_items[static_cast<std::size_t>(*index)].chords.join(QStringLiteral(" / "));
}

QVariantList HelpModel::badgesForVisible(int line) const
{
    const std::optional<int> index = itemIndexForVisible(line);
    if (!index.has_value()) {
        return {};
    }
    return badgesForItem(static_cast<std::size_t>(*index));
}

QVariantList HelpModel::badgesForItem(std::size_t itemIndex) const
{
    QVariantList out;
    if (itemIndex >= m_items.size()) {
        return out;
    }
    bool first = true;
    for (const QString &chord : m_items[itemIndex].chords) {
        if (!first) {
            // 多个和弦之间用一个小圆点分隔（`Ctrl+A · Ctrl+B`）。
            out.append(QVariantMap{{QStringLiteral("text"), QStringLiteral("·")},
                                   {QStringLiteral("badge"), false}});
        }
        first = false;
        for (const QString &part : chord.split(QLatin1Char('+'))) {
            const QString trimmed = part.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            out.append(QVariantMap{{QStringLiteral("text"), trimmed},
                                   {QStringLiteral("badge"), true}});
        }
    }
    return out;
}

void HelpModel::setHover(int line)
{
    const int next = line >= 0 && line < visibleCount() ? line : -1;
    if (next == m_hover) {
        return;
    }
    m_hover = next;
    notifyRows();
}

void HelpModel::moveSelection(int delta)
{
    moveSelection(delta, true);
}

void HelpModel::moveSelection(int delta, bool clearHover)
{
    if (m_visible.empty()) {
        return;
    }
    const int last = visibleCount() - 1;
    const int next = std::clamp(m_selected + delta, 0, last);
    if (clearHover) {
        // 键盘接管高亮：把鼠标悬停交回去（滚轮已经不管滚动位置了，所以这条路
        // 只剩键盘用得到）。
        m_hover = -1;
    }
    const bool moved = next != m_selected;
    m_selected = next;
    if (moved) {
        emit selectedChanged();
    }
    notifyRows();
}

int HelpModel::scrollTargetY(int line,
                            int contentY,
                            int topMargin,
                            int bottomMargin,
                            int viewportHeight) const
{
    if (line < 0 || line >= visibleCount()) {
        return contentY;
    }
    const int slot = kRowHeight + kRowGap;
    const int top = line * slot;
    // 视口坐标里，内容坐标 `c` 落在 `c - contentY`（`topMargin` 只决定
    // `contentY` 的起始位置，不改变这个换算）。行区域是
    // `[topMargin, viewportHeight - bottomMargin]`。
    const int areaTop = topMargin;
    const int areaBottom = viewportHeight - bottomMargin;
    if (top - contentY < areaTop) {
        return top - areaTop;
    }
    if (top + kRowHeight - contentY > areaBottom) {
        return top + kRowHeight - areaBottom;
    }
    return contentY;
}

QVariantMap HelpModel::handleKey(int key, const QString &text)
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);

    switch (key) {
    case Qt::Key_Escape:
        // 先清筛选；筛选本来就是空的才关窗——否则删错一个字就得重开。
        if (!m_filter.isEmpty()) {
            clearFilter();
        } else {
            result.insert(QStringLiteral("decision"), QStringLiteral("cancel"));
        }
        return result;
    case Qt::Key_Backspace:
        if (!m_filter.isEmpty()) {
            QString next = m_filter;
            next.chop(1);
            setFilter(next);
        }
        return result;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const int line = activeLine();
        if (line >= 0) {
            result.insert(QStringLiteral("decision"), QStringLiteral("copy"));
            result.insert(QStringLiteral("index"), line);
        }
        return result;
    }
    case Qt::Key_Up:
        moveSelection(-1);
        return result;
    case Qt::Key_Down:
        moveSelection(1);
        return result;
    case Qt::Key_PageUp:
        moveSelection(-m_rows);
        return result;
    case Qt::Key_PageDown:
        moveSelection(m_rows);
        return result;
    case Qt::Key_Home:
        if (!m_visible.empty()) {
            moveSelection(-visibleCount());
        }
        return result;
    case Qt::Key_End:
        if (!m_visible.empty()) {
            moveSelection(visibleCount());
        }
        return result;
    default:
        break;
    }

    // 字符键：Qt 已经按当前键盘布局翻译过（`QKeyEvent::text()`），
    // 控制字符不是输入内容（退格另有处理）。
    if (text.size() == 1 && text.at(0).isPrint()) {
        setFilter(m_filter + text);
        return result;
    }
    result.insert(QStringLiteral("handled"), false);
    return result;
}

int HelpModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    // 全部筛选结果都交给 `ListView`（它自己决定画哪几条、滚到哪里）。
    return visibleCount();
}

QVariant HelpModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const int line = index.row();
    const std::optional<int> itemIndex = itemIndexForVisible(line);
    if (!itemIndex.has_value()) {
        return {};
    }
    const HelpEntry &item = m_items[static_cast<std::size_t>(*itemIndex)];
    switch (role) {
    case BadgesRole:
        return badgesForItem(static_cast<std::size_t>(*itemIndex));
    case LabelRole:
        return item.label;
    case DetailRole:
        return item.detail.value_or(QString());
    case HighlightedRole:
        return activeLine() == line;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> HelpModel::roleNames() const
{
    return {
        {BadgesRole, QByteArrayLiteral("badges")},
        {LabelRole, QByteArrayLiteral("label")},
        {DetailRole, QByteArrayLiteral("detail")},
        {HighlightedRole, QByteArrayLiteral("highlighted")},
    };
}

void HelpModel::refilter()
{
    const QString needle = m_filter.trimmed().toLower();
    m_visible.clear();
    for (std::size_t index = 0; index < m_haystacks.size(); ++index) {
        if (needle.isEmpty() || m_haystacks[index].contains(needle)) {
            m_visible.push_back(static_cast<int>(index));
        }
    }
    m_selected = 0;
    m_hover = -1;
    relayout();
}

void HelpModel::relayout()
{
    m_rows = std::clamp(visibleCount(), 1, std::max(m_maxRows, 1));
    // 与 `ListView` 的内容高度严格对齐：header(listTop) + 行数 * (行高 + 空隙)
    // + footer(listBottom)。这样「内容放不下」就等价于「可见条数 > 能画的行数」，
    // 自带的滚动条会自己出现/消失。
    m_cardHeight = kListTop + m_rows * (kRowHeight + kRowGap) + kListBottom;
    const int inner = kCardWidth - 2 * kPad - 2 * kInset;
    m_titleRect = PopupRect{kPad + kInset, kPad, inner * 6 / 10, kHeaderHeight};
    m_countRect = PopupRect{kPad + kInset, kPad, inner, kHeaderHeight};
    m_filterRect = PopupRect{kPad + kInset, kPad + kHeaderHeight + kFilterGap, inner, kFilterHeight};
    m_footerRect =
        PopupRect{kPad + kInset, m_cardHeight - kPad - kFooterHeight, inner, kFooterHeight};
}

void HelpModel::notifyRows()
{
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    emit stateChanged();
}

int HelpModel::activeLine() const
{
    if (m_visible.empty()) {
        return -1;
    }
    const int line = m_hover >= 0 ? m_hover : m_selected;
    return std::clamp(line, 0, visibleCount() - 1);
}

} // namespace flowkeyd::app
