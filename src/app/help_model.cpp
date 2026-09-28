#include "app/help_model.h"

#include <QChar>

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素。
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
    const bool wasArmed = m_armed >= 0;
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
    if (wasArmed) {
        emit armedChanged();
    }
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

QString HelpModel::footerText() const
{
    if (m_armed >= 0) {
        // 危险动作的第二次确认：明确写出「再按一次」与怎么取消。
        const std::optional<int> index = itemIndexForVisible(m_armed);
        const QString label = index.has_value() ? m_items[static_cast<std::size_t>(*index)].label
                                                : QString();
        return tr("危险动作「%1」：再按一次 Enter 执行，Esc 取消").arg(label);
    }
    return tr("输入筛选    ↑↓ 选择    Enter 执行    Esc 关闭");
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
    const bool wasArmed = m_armed >= 0;
    beginResetModel();
    m_filter = filter;
    refilter();
    endResetModel();
    emit stateChanged();
    if (wasArmed) {
        emit armedChanged();
    }
    // 筛选之后列表短了：让视图把选中项（第 0 条）带回视野。
    emit selectedChanged();
}

void HelpModel::clearFilter()
{
    setFilter(QString());
}

void HelpModel::reset()
{
    const bool wasArmed = m_armed >= 0;
    beginResetModel();
    m_filter.clear();
    refilter();
    endResetModel();
    emit stateChanged();
    if (wasArmed) {
        emit armedChanged();
    }
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

void HelpModel::moveSelection(int delta)
{
    if (m_visible.empty()) {
        return;
    }
    const int last = visibleCount() - 1;
    const int next = std::clamp(m_selected + delta, 0, last);
    const bool moved = next != m_selected;
    m_selected = next;
    if (moved) {
        // 换了一行，危险动作的「等第二次确认」作废（项目所有者拍板：换行取消）。
        const bool wasArmed = disarm();
        emit selectedChanged();
        if (wasArmed) {
            emit armedChanged();
        }
    }
    notifyRows();
}

void HelpModel::setSelected(int line)
{
    if (m_visible.empty()) {
        return;
    }
    const int next = std::clamp(line, 0, visibleCount() - 1);
    if (next == m_selected) {
        // 点在已经选中的那一行上（双击的第一下也会走到这里）：**不动武装状态**，
        // 否则第二次双击就又变成「第一次确认」了。
        return;
    }
    m_selected = next;
    const bool wasArmed = disarm();
    emit selectedChanged();
    if (wasArmed) {
        emit armedChanged();
    }
    notifyRows();
}

QVariantMap HelpModel::activateRow(int line)
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);
    if (m_visible.empty()) {
        return result;
    }

    const int next = std::clamp(line, 0, visibleCount() - 1);
    if (next != m_selected) {
        m_selected = next;
        const bool wasArmed = disarm();
        emit selectedChanged();
        if (wasArmed) {
            emit armedChanged();
        }
        notifyRows();
    }

    const std::optional<int> index = itemIndexForVisible(m_selected);
    if (!index.has_value()) {
        return result;
    }
    // 危险动作（`quit`/`suspend`/`power`）要两次：第一次只是武装起来。
    if (m_items[static_cast<std::size_t>(*index)].destructive && m_armed != m_selected) {
        armLine(m_selected);
        emit armedChanged();
        notifyRows();
        result.insert(QStringLiteral("decision"), QStringLiteral("arm"));
        result.insert(QStringLiteral("index"), m_selected);
        return result;
    }

    // 真的执行了：把武装状态清掉（下一次再按又是「第一次确认」）。
    if (disarm()) {
        emit armedChanged();
        notifyRows();
    }
    result.insert(QStringLiteral("decision"), QStringLiteral("run"));
    result.insert(QStringLiteral("index"), m_selected);
    return result;
}

QVariantMap HelpModel::handleKey(int key)
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);

    switch (key) {
    case Qt::Key_Escape:
        // 三级：先取消危险动作的确认（窗口不关）→ 再清筛选 → 最后才关窗。
        // `clear` 告诉 QML 把输入框里的文本也跟着清掉（模型是筛选的唯一真相，
        // 但输入框自己持有它显示的文本）。
        if (m_armed >= 0) {
            disarm();
            emit armedChanged();
            notifyRows();
            result.insert(QStringLiteral("decision"), QStringLiteral("disarm"));
            return result;
        }
        if (!m_filter.isEmpty()) {
            clearFilter();
            result.insert(QStringLiteral("decision"), QStringLiteral("clear"));
        } else {
            result.insert(QStringLiteral("decision"), QStringLiteral("cancel"));
        }
        return result;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // 执行光标那一行（危险动作第一次只会得到 `arm`）。
        return activateRow(m_selected);
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
    default:
        break;
    }

    // 其余的键（字符、退格、`Home`/`End`、左右方向键、输入法的候选键……）
    // 全部放行给筛选框那个标准 `TextField`。
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
    case RowSelectedRole:
        return activeLine() == line;
    case RowArmedRole:
        return m_armed == line;
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
        {RowSelectedRole, QByteArrayLiteral("rowSelected")},
        {RowArmedRole, QByteArrayLiteral("rowArmed")},
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
    // 列表换了内容，武装状态一律作废。调用方在 `endResetModel()` 之后补
    // `armedChanged`（在 reset 中间发 `dataChanged` 是非法的）。
    m_armed = -1;
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
    return std::clamp(m_selected, 0, visibleCount() - 1);
}

void HelpModel::armLine(int line)
{
    m_armed = line;
}

bool HelpModel::disarm()
{
    if (m_armed < 0) {
        return false;
    }
    m_armed = -1;
    return true;
}

} // namespace flowkeyd::app
