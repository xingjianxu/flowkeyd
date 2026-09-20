#include "app/menu_model.h"

#include <QRect>

#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 oskeyd 的 `menu.rs::Metrics` 同源；Qt 会按显示器的
// DPI 自己缩放，所以这里不需要 dpi 参数）。
constexpr int kCardWidth = 300;
constexpr int kPad = 10;
constexpr int kHeaderHeight = 30;
constexpr int kRowHeight = 38;
constexpr int kRowGap = 2;
constexpr int kBadgeSize = 22;
constexpr int kCardRadius = 10;
constexpr int kFooterHeight = 22;
constexpr int kInset = 8;
constexpr int kListBottomGap = 2;

QRect toQmlRect(const PopupRect &rect)
{
    return QRect(rect.x, rect.y, rect.width, rect.height);
}

} // namespace

MenuModel::MenuModel(QObject *parent)
    : QAbstractListModel(parent)
{
    relayout(false);
}

void MenuModel::setItems(std::optional<QString> title, std::vector<MenuEntry> items)
{
    beginResetModel();
    m_title = std::move(title);
    m_items = std::move(items);
    m_highlight = 0;
    m_hover = -1;
    relayout(false);
    endResetModel();
    emit itemsChanged();
    emit highlightChanged();
}

int MenuModel::cardRadius() const
{
    return kCardRadius;
}

QString MenuModel::footerText() const
{
    return tr("↑↓ 选择    Enter 确定    Esc 关闭");
}

PopupRect MenuModel::rowRect(int index) const
{
    const int top = kPad + (m_title.has_value() ? kHeaderHeight : 0) + index * (kRowHeight + kRowGap);
    return PopupRect{kPad, top, kCardWidth - 2 * kPad, kRowHeight};
}

PopupRect MenuModel::badgeRect(int index) const
{
    const PopupRect row = rowRect(index);
    return PopupRect{row.x + kInset, row.y + (kRowHeight - kBadgeSize) / 2, kBadgeSize, kBadgeSize};
}

PopupRect MenuModel::labelRect(int index) const
{
    const PopupRect row = rowRect(index);
    const int left = row.x + kInset + kBadgeSize + kInset;
    // 分割点与 oskeyd 相同：条目宽度的 60% 处，右边留给副标题。
    const int split = row.x + row.width * 6 / 10;
    return PopupRect{left, row.y, split - left, row.height};
}

PopupRect MenuModel::hintRect(int index) const
{
    const PopupRect row = rowRect(index);
    const int split = row.x + row.width * 6 / 10;
    return PopupRect{split, row.y, row.x + row.width - kInset - split, row.height};
}

int MenuModel::hitTest(int x, int y) const
{
    for (int index = 0; index < count(); ++index) {
        if (rowRect(index).contains(x, y)) {
            return index;
        }
    }
    return -1;
}

int MenuModel::accept() const
{
    if (m_items.empty()) {
        return -1;
    }
    const int active = m_hover >= 0 ? m_hover : m_highlight;
    return std::min(active, count() - 1);
}

void MenuModel::moveHighlight(int delta)
{
    const int n = count();
    if (n == 0 || delta == 0) {
        return;
    }
    // 一次一格，到边界回绕（与 oskeyd 的 `↑`/`↓` 一致；帮助窗口那边则是夹住）。
    if (delta > 0) {
        m_highlight = m_highlight >= n - 1 ? 0 : m_highlight + 1;
    } else {
        m_highlight = m_highlight == 0 ? n - 1 : m_highlight - 1;
    }
    m_hover = -1;
    refresh(false);
    emit highlightChanged();
}

void MenuModel::setHover(int index)
{
    const int next = index >= 0 && index < count() ? index : -1;
    if (next == m_hover) {
        return;
    }
    m_hover = next;
    refresh(false);
    emit highlightChanged();
}

int MenuModel::indexForChar(const QString &text) const
{
    if (text.size() != 1) {
        return -1;
    }
    const QChar ch = text.at(0).toLower();
    for (int index = 0; index < count(); ++index) {
        const std::optional<QChar> &key = m_items[static_cast<std::size_t>(index)].key;
        if (key.has_value() && *key == ch) {
            return index;
        }
    }
    return -1;
}

void MenuModel::reset()
{
    m_highlight = 0;
    m_hover = -1;
    refresh(false);
    emit highlightChanged();
}

QVariantMap MenuModel::handleKey(int key, const QString &text)
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);

    switch (key) {
    case Qt::Key_Escape:
        result.insert(QStringLiteral("decision"), QStringLiteral("cancel"));
        return result;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const int index = accept();
        if (index >= 0) {
            result.insert(QStringLiteral("decision"), QStringLiteral("choose"));
            result.insert(QStringLiteral("index"), index);
        }
        return result;
    }
    case Qt::Key_Up:
        moveHighlight(-1);
        return result;
    case Qt::Key_Down:
        moveHighlight(1);
        return result;
    default:
        break;
    }

    if (!text.isEmpty()) {
        const int index = indexForChar(text);
        if (index >= 0) {
            result.insert(QStringLiteral("decision"), QStringLiteral("choose"));
            result.insert(QStringLiteral("index"), index);
        }
        return result;
    }
    result.insert(QStringLiteral("handled"), false);
    return result;
}

int MenuModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return count();
}

QVariant MenuModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const int row = index.row();
    const MenuEntry &item = m_items[static_cast<std::size_t>(row)];
    switch (role) {
    case LabelRole:
        return item.label;
    case HintRole:
        return item.hint.value_or(QString());
    case KeyRole:
        // 徽标上显示大写（与 oskeyd 的 `key.to_uppercase()` 一致）；
        // 匹配用的 `m_items[].key` 仍然是小写。
        return item.key.has_value() ? QString(item.key->toUpper()) : QString();
    case HighlightedRole:
        return accept() == row;
    case HoveredRole:
        return m_hover == row;
    case RowRole:
        return QVariant::fromValue(toQmlRect(rowRect(row)));
    case BadgeRole:
        return QVariant::fromValue(toQmlRect(badgeRect(row)));
    case LabelRectRole:
        return QVariant::fromValue(toQmlRect(labelRect(row)));
    case HintRectRole:
        return QVariant::fromValue(toQmlRect(hintRect(row)));
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> MenuModel::roleNames() const
{
    return {
        {LabelRole, QByteArrayLiteral("label")},
        {HintRole, QByteArrayLiteral("hint")},
        {KeyRole, QByteArrayLiteral("keyText")},
        {HighlightedRole, QByteArrayLiteral("highlighted")},
        {HoveredRole, QByteArrayLiteral("hovered")},
        {RowRole, QByteArrayLiteral("rowRect")},
        {BadgeRole, QByteArrayLiteral("badgeRect")},
        {LabelRectRole, QByteArrayLiteral("labelRect")},
        {HintRectRole, QByteArrayLiteral("hintRect")},
    };
}

void MenuModel::relayout(bool notify)
{
    const int header = m_title.has_value() ? kHeaderHeight : 0;
    m_cardWidth = kCardWidth;
    m_cardHeight = kPad + header + count() * (kRowHeight + kRowGap) + kListBottomGap + kFooterHeight + kPad;
    m_titleRect = PopupRect{kPad + kInset, kPad, kCardWidth - 2 * kPad - 2 * kInset, header};
    const int footerTop = m_cardHeight - kPad - kFooterHeight;
    m_footerRect = PopupRect{kPad + kInset, footerTop, kCardWidth - 2 * kPad - 2 * kInset, kFooterHeight};
    if (notify) {
        emit itemsChanged();
    }
}

void MenuModel::refresh(bool itemsChangedSignal)
{
    if (!m_items.empty()) {
        emit dataChanged(index(0), index(count() - 1));
    }
    if (itemsChangedSignal) {
        emit itemsChanged();
    }
}

} // namespace flowkeyd::app
