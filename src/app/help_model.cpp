#include "app/help_model.h"

#include <QChar>
#include <QRect>

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
constexpr int kScrollbar = 5;
constexpr int kCardRadius = 12;
constexpr int kScrollThumbMinHeight = 24;
constexpr int kMaxRows = 12;

constexpr int itemsTop()
{
    return kPad + kHeaderHeight + kFilterGap + kFilterHeight + kListGap;
}

QRect toQmlRect(const PopupRect &rect)
{
    return QRect(rect.x, rect.y, rect.width, rect.height);
}

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
    ensureVisible();
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

int HelpModel::listTop() const
{
    return itemsTop();
}

std::optional<int> HelpModel::itemIndexForVisible(int line) const
{
    if (line < 0 || line >= visibleCount()) {
        return std::nullopt;
    }
    return m_visible[static_cast<std::size_t>(line)];
}

PopupRect HelpModel::rowRect(int line) const
{
    return PopupRect{kPad + kInset,
                     itemsTop() + line * (kRowHeight + kRowGap),
                     kCardWidth - 2 * kPad - 2 * kInset,
                     kRowHeight};
}

PopupRect HelpModel::keysRect(int line) const
{
    const PopupRect row = rowRect(line);
    return PopupRect{row.x + kInset, row.y, kKeysWidth, kRowHeight};
}

PopupRect HelpModel::textRect(int line) const
{
    const PopupRect row = rowRect(line);
    const int left = row.x + kInset + kKeysWidth + kInset;
    return PopupRect{left, row.y, row.x + row.width - kInset - left, row.height};
}

bool HelpModel::hasScrollbar() const
{
    return visibleCount() > m_rows;
}

PopupRect HelpModel::scrollTrack() const
{
    return PopupRect{kCardWidth - kPad - kScrollbar,
                     itemsTop(),
                     kScrollbar,
                     m_rows * (kRowHeight + kRowGap) - kRowGap};
}

PopupRect HelpModel::scrollThumb() const
{
    if (!hasScrollbar()) {
        return PopupRect{};
    }
    const PopupRect track = scrollTrack();
    const int span = track.height;
    const int thumb =
        std::max(span * m_rows / visibleCount(), kScrollThumbMinHeight);
    const int maxScroll = std::max(visibleCount() - m_rows, 1);
    const int offset = (span - thumb) * m_scroll / maxScroll;
    return PopupRect{track.x, track.y + offset, track.width, thumb};
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

int HelpModel::hitTest(int x, int y) const
{
    const int lines = std::min(m_rows, std::max(visibleCount() - m_scroll, 0));
    for (int line = 0; line < lines; ++line) {
        if (rowRect(line).contains(x, y)) {
            return m_scroll + line;
        }
    }
    return -1;
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
    if (m_visible.empty()) {
        return;
    }
    const int last = visibleCount() - 1;
    m_selected = std::clamp(m_selected + delta, 0, last);
    m_hover = -1;
    ensureVisible();
    notifyRows();
}

void HelpModel::wheel(int angleDeltaY)
{
    // 一格滚轮（±120）跳过三行，和系统的列表控件一致（与 oskeyd 相同）。
    const int lines = (angleDeltaY / 120) * 3;
    if (lines != 0) {
        moveSelection(lines);
    }
}

int HelpModel::clickRow(int x, int y)
{
    const int line = hitTest(x, y);
    if (line < 0) {
        return -1;
    }
    m_selected = line;
    m_hover = line;
    notifyRows();
    return line;
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
    // 画出来的行数：既不超过卡片能放的行数，也不超过滚动后剩下的条目。
    return std::min(m_rows, std::max(visibleCount() - m_scroll, 0));
}

QVariant HelpModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const int line = index.row();
    const std::optional<int> itemIndex = itemIndexForVisible(m_scroll + line);
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
        return activeLine() == m_scroll + line;
    case HoveredRole:
        return m_hover == m_scroll + line;
    case LineRole:
        return line;
    case RowRole:
        return QVariant::fromValue(toQmlRect(rowRect(line)));
    case KeysRole:
        return QVariant::fromValue(toQmlRect(keysRect(line)));
    case TextRectRole:
        return QVariant::fromValue(toQmlRect(textRect(line)));
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
        {HoveredRole, QByteArrayLiteral("hovered")},
        {LineRole, QByteArrayLiteral("line")},
        {RowRole, QByteArrayLiteral("rowRect")},
        {KeysRole, QByteArrayLiteral("keysRect")},
        {TextRectRole, QByteArrayLiteral("textRect")},
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
    m_scroll = 0;
    m_hover = -1;
    relayout();
}

void HelpModel::relayout()
{
    m_rows = std::clamp(visibleCount(), 1, std::max(m_maxRows, 1));
    m_cardHeight =
        itemsTop() + m_rows * (kRowHeight + kRowGap) + kFooterGap + kFooterHeight + kPad;
    const int inner = kCardWidth - 2 * kPad - 2 * kInset;
    m_titleRect = PopupRect{kPad + kInset, kPad, inner * 6 / 10, kHeaderHeight};
    m_countRect = PopupRect{kPad + kInset, kPad, inner, kHeaderHeight};
    m_filterRect = PopupRect{kPad + kInset, kPad + kHeaderHeight + kFilterGap, inner, kFilterHeight};
    m_footerRect =
        PopupRect{kPad + kInset, m_cardHeight - kPad - kFooterHeight, inner, kFooterHeight};
}

void HelpModel::ensureVisible()
{
    if (m_selected < m_scroll) {
        m_scroll = m_selected;
    } else if (m_selected >= m_scroll + m_rows) {
        m_scroll = m_selected + 1 - m_rows;
    }
    const int maxScroll = std::max(visibleCount() - m_rows, 0);
    m_scroll = std::clamp(m_scroll, 0, maxScroll);
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
    return std::min(line, visibleCount() - 1);
}

} // namespace flowkeyd::app
