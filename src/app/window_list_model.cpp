#include "app/window_list_model.h"

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 `help_model` 同一套纵向度量，卡片更宽一点，
// 好放下「标题 —— 进程名」）。
constexpr int kCardWidth = 560;
constexpr int kPad = 12;
constexpr int kFilterHeight = 30;
constexpr int kListGap = 8;
constexpr int kRowHeight = 46;
constexpr int kRowGap = 2;
constexpr int kFooterHeight = 24;
constexpr int kFooterGap = 8;
constexpr int kInset = 10;
constexpr int kCardRadius = 12;
constexpr int kMaxRows = 12;
/// 数字选择模式下一共分配多少个快捷键：`1`..`9`、`0`（第 10 个）。
constexpr int kNumberedKeys = 10;

/// 列表区顶部：卡片内边距 + 筛选框 + 间距。
///
/// **卡片没有标题行**（项目所有者 2026-09 要求）：筛选框就是第一行，所以这里
/// 没有表头那一项。
constexpr int kListTop = kPad + kFilterHeight + kListGap;
/// 列表区底部：底部提示 + 它上面的空隙 + 卡片内边距。
constexpr int kListBottom = kFooterGap + kFooterHeight + kPad;

/// 一个窗口的进程名（小写，只在装载时算一次）。
///
/// **筛选只看进程名**（前缀匹配，标题不参与）：打 `chr` 命中 `chrome.exe`；
/// 标题只用于卡片上显示，让用户自己分辨同一个程序的多个窗口。
QString processKey(const WindowListEntry &entry)
{
    return entry.process.toLower();
}

QVariantMap noneDecision()
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);
    return result;
}

/// 数字键对应的可见行下标：`1` -> 0 … `9` -> 8，`0` -> 9；不是数字键时是 -1。
/// 数字小键盘与主键盘在 Qt 里都报 `Qt::Key_0`..`Qt::Key_9`，所以两边都算。
int digitLineForKey(int key)
{
    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
        return key - Qt::Key_1;
    }
    if (key == Qt::Key_0) {
        return kNumberedKeys - 1;
    }
    return -1;
}

/// 第 `line` 行的数字快捷键文本（`1`..`9`、`0`）；超出前 10 行时是空串。
QString digitLabelForLine(int line)
{
    if (line < 0 || line >= kNumberedKeys) {
        return QString();
    }
    return QString::number((line + 1) % 10);
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
    m_processes.clear();
    m_processes.reserve(m_items.size());
    for (const WindowListEntry &entry : m_items) {
        m_processes.push_back(processKey(entry));
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
    // 卡片上下内边距 + 筛选框（含它下面的空隙）+ 底部提示（含它上面的空隙）。
    const int chrome =
        kPad * 2 + kFilterHeight + kListGap + kFooterGap + kFooterHeight;
    // 上下各留一点，让卡片不与屏幕边缘贴住。
    const int available = availableHeight - chrome - 48;
    const int fits = available / (kRowHeight + kRowGap);
    return std::clamp(std::max(fits, 1), 1, kMaxRows);
}

QString WindowListModel::caption() const
{
    // 卡片里没有标题行了（项目所有者 2026-09 要求）：这行字只是**窗口标题**
    // （无边框窗口，用户看不到）。外面（验收 / 诊断脚本）靠它读「现在列了几个
    // 窗口」；`windows("切换窗口")` 给的名字也出现在这里。
    const QString name = m_title.value_or(tr("flowkeyd 窗口"));
    return tr("%1 — %2 个").arg(name).arg(visibleCount());
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
    return tr("输入进程名前缀筛选…");
}

QString WindowListModel::emptyMessage() const
{
    return m_items.empty() ? tr("没有打开的窗口") : tr("没有匹配的窗口");
}

QString WindowListModel::footerText() const
{
    // 卡片没有标题行，原来在标题右边的「N / M 个窗口」就挪到了这里；顺手去掉
    // 原先那句「输入筛选」—— 筛选框自己有占位文本，那句是重复的。
    if (m_numbered) {
        return tr("%1    数字键直接切换    ↑↓ 选择    Enter 切换    Esc 关闭").arg(countText());
    }
    return tr("%1    ↑↓ 选择    Enter 切换    Esc 关闭").arg(countText());
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
    // 数字选择模式：数字键永远归快捷键（哪怕前 10 行不够用也不能漏给筛选框，
    // 否则「5」会跑进筛选串里、把列表筛空）。
    if (m_numbered) {
        const int line = digitLineForKey(key);
        if (line >= 0) {
            return line < visibleCount() ? activate(line) : noneDecision();
        }
    }
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
    case RowKeyRole:
        // `m_rowKeys` 与 `m_visible` 一一对应，用可见行下标取。
        return m_rowKeys[static_cast<std::size_t>(line)];
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
        {RowKeyRole, QByteArrayLiteral("rowKey")},
    };
}

void WindowListModel::refilter()
{
    // 大小写无关的**前缀**匹配：`chr` 命中 `chrome.exe`，`hrome` 不命中。
    // 标题不参与（见头文件的说明）。
    const QString needle = m_filter.trimmed().toLower();
    m_visible.clear();
    for (std::size_t index = 0; index < m_processes.size(); ++index) {
        if (needle.isEmpty() || m_processes[index].startsWith(needle)) {
            m_visible.push_back(static_cast<int>(index));
        }
    }
    m_selected = 0;

    // 数字选择模式：筛选非空、命中窗口全属于同一个进程名、而且不止一个窗口
    // （见头文件）。前 10 行各分一个数字，剩下的窗口不分配。
    m_numbered = false;
    m_rowKeys.assign(m_visible.size(), QString());
    if (!needle.isEmpty() && m_visible.size() >= 2) {
        const QString &group = m_processes[static_cast<std::size_t>(m_visible.front())];
        const bool oneProcess =
            !group.isEmpty()
            && std::all_of(m_visible.begin(), m_visible.end(), [this, &group](int index) {
                   return m_processes[static_cast<std::size_t>(index)] == group;
               });
        if (oneProcess) {
            m_numbered = true;
            const int keys = std::min(visibleCount(), kNumberedKeys);
            for (int line = 0; line < keys; ++line) {
                m_rowKeys[static_cast<std::size_t>(line)] = digitLabelForLine(line);
            }
        }
    }

    relayout();
}

void WindowListModel::relayout()
{
    m_rows = std::clamp(visibleCount(), 1, std::max(m_maxRows, 1));
    // 与 `ListView` 的内容高度严格对齐：header(listTop) + 行数 * (行高 + 空隙)
    // + footer(listBottom)。这样「内容放不下」就等价于「可见条数 > 能画的行数」。
    m_cardHeight = kListTop + m_rows * (kRowHeight + kRowGap) + kListBottom;
    const int inner = kCardWidth - 2 * kPad - 2 * kInset;
    m_filterRect = PopupRect{kPad + kInset, kPad, inner, kFilterHeight};
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
