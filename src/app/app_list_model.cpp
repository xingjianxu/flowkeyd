#include "app/app_list_model.h"

#include "core/app_list.h"

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 `window_list_model` / `help_model` 同一套纵向度量）。
//
// **卡片是 800 宽**（6 列 × 126 + 两边的内边距 12 与缩进 10）：格子 126 × 88、
// 图标 40。卡片**高度**随视图变（概览里「固定 + 最近使用 + 按钮」通常比一屏矮，
// 「全部程序」列表一定是一屏高），见 `relayout()`。
constexpr int kColumns = 6;
constexpr int kCellWidth = 126;
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
/// 分段表头（「已固定」「最近使用」、`A`–`Z` / `#`）。
constexpr int kHeaderHeight = 26;
/// 整行按钮（「全部程序（N）」「← 返回」）。
constexpr int kButtonHeight = 40;
/// 「全部程序」列表里的一行。
constexpr int kListRowHeight = 44;

/// 卡片宽度：两边的内边距与缩进 + 正好六格（6 × 126 + 2 × 12 + 2 × 10 = 800）。
constexpr int kInnerWidth = kColumns * kCellWidth;
constexpr int kCardWidth = kInnerWidth + 2 * kPad + 2 * kInset;

/// 列表区顶部：卡片内边距 + 筛选框 + 间距（卡片里没有标题行）。
constexpr int kListTop = kPad + kFilterHeight + kListGap;
/// 列表区底部：底部提示 + 它上面的空隙 + 卡片内边距。
constexpr int kListBottom = kFooterGap + kFooterHeight + kPad;

QVariantMap makeNoneDecision(bool handled)
{
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("none"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), handled);
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

    m_search.clear();
    m_sort.clear();
    m_letter.clear();
    m_keyToItem.clear();
    m_search.reserve(m_items.size());
    m_sort.reserve(m_items.size());
    m_letter.reserve(m_items.size());
    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const AppListEntry &entry = m_items[index];
        // 搜索串与排序键各算一次：之后每次敲键盘只是 `contains()`，
        // 「全部程序」列表也只是按已经算好的键排一遍。
        m_search.push_back(core::appSearchText(entry.name));
        const core::AppSortInfo info = core::appSortInfo(entry.name);
        m_sort.push_back(info.sortText);
        m_letter.push_back(info.letter);
        if (!entry.key.isEmpty()) {
            // 同一个键只认第一次出现的那一条（同一个程序在开始菜单里有两份时
            // `prepareAppEntries` 已经去过重，这里是第二层保险）。
            m_keyToItem.insert(entry.key, static_cast<int>(index));
        }
    }

    m_filter.clear();
    m_allMode = false;
    rebuild(-1);
    endResetModel();
    emit itemsChanged();
    emit stateChanged();
    emit selectedChanged();
    // 新的一批数据高度可能完全不同（第一次用是满屏网格，有了固定 / 最近使用之后
    // 只剩几行）：QML 那边的 `height` 绑定会把窗口调好，`onHeightChanged` 再把它
    // 夹回屏幕里。
}

void AppListModel::setState(const core::LauncherState &state)
{
    if (state.pinned == m_pinned && state.recent == m_recent) {
        return;
    }
    const int keep = selectedItem();
    beginResetModel();
    m_pinned = state.pinned;
    m_recent = state.recent;
    rebuild(keep);
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

core::LauncherState AppListModel::launcherState() const
{
    core::LauncherState state;
    state.pinned = m_pinned;
    state.recent = m_recent;
    return state;
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
    return tr("输入程序名 / 拼音筛选…");
}

QString AppListModel::emptyMessage() const
{
    return m_items.empty() ? tr("没有找到程序") : tr("没有匹配的程序");
}

QString AppListModel::footerText() const
{
    if (m_allMode && m_filter.trimmed().isEmpty()) {
        return tr("%1 个程序    ↑↓ 选择    Enter 启动    Esc 返回").arg(visibleCount());
    }
    return tr("%1 个程序    ↑↓←→ 选择    Enter 启动    Space 固定    右键菜单    Esc 关闭")
        .arg(visibleCount());
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

QString AppListModel::allButtonText() const
{
    return tr("全部程序（%1）").arg(static_cast<int>(m_items.size()));
}

std::optional<int> AppListModel::itemIndexForVisible(int line) const
{
    if (line < 0) {
        return std::nullopt;
    }
    int remaining = line;
    for (const Row &row : m_rows) {
        if (row.kind != Row::Kind::Grid && row.kind != Row::Kind::List) {
            continue;
        }
        if (remaining < static_cast<int>(row.items.size())) {
            return row.items[static_cast<std::size_t>(remaining)];
        }
        remaining -= static_cast<int>(row.items.size());
    }
    return std::nullopt;
}

int AppListModel::selectedItem() const
{
    const std::optional<int> item = itemAt(m_selectedRow, m_selectedColumn);
    return item.value_or(-1);
}

bool AppListModel::hasMatches() const
{
    // 没有可选中的行（既没有条目也没有按钮）时就是「一条都筛不出来」——
    // `selectFirst()` 保证这种情况下 `m_selectedRow` 是 -1。
    return m_selectedRow >= 0;
}

QVariantMap AppListModel::setFilter(const QString &filter)
{
    if (filter == m_filter) {
        return makeNoneDecision(true);
    }
    beginResetModel();
    m_filter = filter;
    rebuild(-1);
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
    return makeNoneDecision(true);
}

void AppListModel::clearFilter()
{
    if (m_filter.isEmpty()) {
        return;
    }
    beginResetModel();
    m_filter.clear();
    rebuild(-1);
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::reset()
{
    beginResetModel();
    m_filter.clear();
    m_allMode = false;
    rebuild(-1);
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::showAll()
{
    if (m_allMode) {
        return;
    }
    beginResetModel();
    m_allMode = true;
    rebuild(-1);
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::showOverview()
{
    if (!m_allMode) {
        return;
    }
    beginResetModel();
    m_allMode = false;
    rebuild(-1);
    endResetModel();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::moveSelection(int dx, int dy)
{
    if (m_rows.empty() || m_selectedRow < 0) {
        return;
    }
    if (dy != 0) {
        const int step = dy > 0 ? 1 : -1;
        int row = m_selectedRow;
        int remaining = dy > 0 ? dy : -dy;
        while (remaining > 0) {
            int next = row;
            // 表头不是可选项：一直挪到下一个能选中的行为止。
            do {
                next += step;
                if (next < 0 || next >= static_cast<int>(m_rows.size())) {
                    // 到边界就夹住（不回绕）：网格里回绕到上一行的末尾很容易
                    // 让人失去方向感。
                    next = row;
                    break;
                }
            } while (!isSelectableRow(next));
            if (next == row) {
                // 走到头了：停在最后一个能到的行上（**不是**把这一整次移动丢掉）。
                break;
            }
            row = next;
            --remaining;
        }
        const int column = std::clamp(m_selectedColumn, 0, maxColumn(row));
        if (row == m_selectedRow && column == m_selectedColumn) {
            return;
        }
        m_selectedRow = row;
        m_selectedColumn = column;
    } else if (dx != 0) {
        const int column = std::clamp(m_selectedColumn + dx, 0, maxColumn(m_selectedRow));
        if (column == m_selectedColumn) {
            return;
        }
        m_selectedColumn = column;
    } else {
        return;
    }
    emit selectedChanged();
    notifyRows();
}

void AppListModel::hoverItem(int itemIndex)
{
    const std::optional<std::pair<int, int>> position = positionOfItem(itemIndex);
    if (!position.has_value()) {
        return;
    }
    if (position->first == m_selectedRow && position->second == m_selectedColumn) {
        return;
    }
    m_selectedRow = position->first;
    m_selectedColumn = position->second;
    emit selectedChanged();
    notifyRows();
}

QVariantMap AppListModel::activateItem(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) {
        return makeNoneDecision(true);
    }
    const std::optional<std::pair<int, int>> position = positionOfItem(itemIndex);
    if (!position.has_value()) {
        return makeNoneDecision(true);
    }
    if (position->first != m_selectedRow || position->second != m_selectedColumn) {
        m_selectedRow = position->first;
        m_selectedColumn = position->second;
        emit selectedChanged();
        notifyRows();
    }
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("choose"));
    result.insert(QStringLiteral("index"), itemIndex);
    result.insert(QStringLiteral("handled"), true);
    return result;
}

void AppListModel::togglePinItem(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) {
        return;
    }
    const QString key = m_items[static_cast<std::size_t>(itemIndex)].key;
    if (key.isEmpty()) {
        // 预热用的假数据没有稳定键：没有东西可固定。
        return;
    }
    const QStringList next = core::togglePinned(m_pinned, key);
    if (next == m_pinned) {
        return;
    }
    beginResetModel();
    m_pinned = next;
    // 固定之后那一条会挪到「已固定」那一组去，选中项跟着它走（用户刚按的
    // `Space`，光标不该跳走）。
    rebuild(itemIndex);
    endResetModel();
    emit stateEdited();
    emit stateChanged();
    emit selectedChanged();
}

void AppListModel::noteLaunched(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) {
        return;
    }
    const QString key = m_items[static_cast<std::size_t>(itemIndex)].key;
    if (key.isEmpty()) {
        return;
    }
    const QStringList next = core::touchRecent(m_recent, key);
    if (next == m_recent) {
        return;
    }
    beginResetModel();
    m_recent = next;
    rebuild(itemIndex);
    endResetModel();
    emit stateEdited();
    emit stateChanged();
    emit selectedChanged();
}

QVariantMap AppListModel::afterContextMenu(bool invoked)
{
    if (!invoked) {
        return makeNoneDecision(true);
    }
    // 真的执行了某条命令：把卡片收掉（`cancel` 在 QML 那边就是 `appDismiss()`）。
    // 卡片其实已经被 `PopupHost` 在执行命令**之前**收起来了——返回值只是让
    // 「选中条目就关」这条规则有一个可单测的家，不至于变成 QML 里的一个分叉。
    QVariantMap result;
    result.insert(QStringLiteral("decision"), QStringLiteral("cancel"));
    result.insert(QStringLiteral("index"), -1);
    result.insert(QStringLiteral("handled"), true);
    return result;
}

QVariantMap AppListModel::handleKey(int key)
{
    switch (key) {
    case Qt::Key_Escape:
        if (m_allMode && m_filter.trimmed().isEmpty()) {
            // 「全部程序」列表里 `Esc` 是「返回概览」，不是关卡片（Win10 开始
            // 菜单的「所有应用」也是这个手感）。
            showOverview();
            return makeNoneDecision(true);
        }
        {
            QVariantMap result;
            result.insert(QStringLiteral("decision"), QStringLiteral("cancel"));
            result.insert(QStringLiteral("index"), -1);
            result.insert(QStringLiteral("handled"), true);
            return result;
        }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (m_selectedRow < 0) {
            return makeNoneDecision(true);
        }
        const Row &row = m_rows[static_cast<std::size_t>(m_selectedRow)];
        if (row.kind == Row::Kind::Button) {
            // 落在按钮上：`Enter` 就是点它一下。
            if (row.opensAll) {
                showAll();
            } else {
                showOverview();
            }
            return makeNoneDecision(true);
        }
        const std::optional<int> item = itemAt(m_selectedRow, m_selectedColumn);
        if (!item.has_value()) {
            return makeNoneDecision(true);
        }
        return activateItem(*item);
    }
    case Qt::Key_Space: {
        // **只有筛选框为空时才截走空格**：一旦用户开始打字，他可能需要打
        // `visual studio` 这种带空格的名字（`Keys.BeforeItem` 会先到我们这里，
        // 放行之后才轮到标准 `TextInput` 插一个空格）。
        if (!m_filter.trimmed().isEmpty()) {
            return makeNoneDecision(false);
        }
        const std::optional<int> item = itemAt(m_selectedRow, m_selectedColumn);
        if (!item.has_value()) {
            return makeNoneDecision(true);
        }
        togglePinItem(*item);
        return makeNoneDecision(true);
    }
    case Qt::Key_Left:
        moveSelection(-1, 0);
        return makeNoneDecision(true);
    case Qt::Key_Right:
        moveSelection(1, 0);
        return makeNoneDecision(true);
    case Qt::Key_Up:
        moveSelection(0, -1);
        return makeNoneDecision(true);
    case Qt::Key_Down:
        moveSelection(0, 1);
        return makeNoneDecision(true);
    case Qt::Key_Home:
        selectFirst();
        return makeNoneDecision(true);
    case Qt::Key_End:
        selectLast();
        return makeNoneDecision(true);
    case Qt::Key_PageUp:
        moveSelection(0, -std::max(m_maxRows, 1));
        return makeNoneDecision(true);
    case Qt::Key_PageDown:
        moveSelection(0, std::max(m_maxRows, 1));
        return makeNoneDecision(true);
    default:
        // 其余的键（字符、退格、输入法的候选键……）全部放行给筛选框那个标准
        // `TextField`，`↑`/`↓`/`←`/`→` 会被它故意忽略、冒到这里（见 AGENTS.md）。
        return makeNoneDecision(false);
    }
}

int AppListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

QVariant AppListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const int line = index.row();
    const Row &row = m_rows[static_cast<std::size_t>(line)];
    const bool selected = line == m_selectedRow && isSelectableRow(line);
    switch (role) {
    case RowKindRole:
        switch (row.kind) {
        case Row::Kind::Header:
            return QStringLiteral("header");
        case Row::Kind::Grid:
            return QStringLiteral("grid");
        case Row::Kind::Button:
            return QStringLiteral("button");
        case Row::Kind::List:
            return QStringLiteral("list");
        }
        return QStringLiteral("grid");
    case RowTitleRole:
        return row.title;
    case RowItemsRole:
        return row.models;
    case RowSelectedRole:
        return selected;
    case RowSelectedColumnRole:
        return selected ? m_selectedColumn : -1;
    case RowHeightRole:
        return row.height;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> AppListModel::roleNames() const
{
    return {
        {RowKindRole, QByteArrayLiteral("rowKind")},
        {RowTitleRole, QByteArrayLiteral("rowTitle")},
        {RowItemsRole, QByteArrayLiteral("rowItems")},
        {RowSelectedRole, QByteArrayLiteral("rowSelected")},
        {RowSelectedColumnRole, QByteArrayLiteral("rowSelectedColumn")},
        {RowHeightRole, QByteArrayLiteral("rowHeight")},
    };
}

void AppListModel::rebuild(int keepItem)
{
    rebuildOrder();
    rebuildRows(keepItem);
    relayout();
}

void AppListModel::rebuildOrder()
{
    m_order.clear();
    m_order.reserve(m_items.size());
    for (std::size_t index = 0; index < m_items.size(); ++index) {
        m_order.push_back(static_cast<int>(index));
    }
    // 按**主读音全拼**排：中文按拼音、拉丁按字母，数字 / 符号开头的那一组排在最前
    // （`appSortText()` 里那个 `0`/`1` 前缀保证同一组的条目是连续的）。
    std::stable_sort(m_order.begin(), m_order.end(), [this](int a, int b) {
        int order = QString::compare(m_sort[static_cast<std::size_t>(a)],
                                    m_sort[static_cast<std::size_t>(b)]);
        if (order != 0) {
            return order < 0;
        }
        order = QString::compare(m_items[static_cast<std::size_t>(a)].name,
                                 m_items[static_cast<std::size_t>(b)].name,
                                 Qt::CaseInsensitive);
        if (order != 0) {
            return order < 0;
        }
        // 同名（只是大小写不同）时让大写在前，保证顺序确定。
        order = QString::compare(m_items[static_cast<std::size_t>(a)].name,
                                 m_items[static_cast<std::size_t>(b)].name);
        if (order != 0) {
            return order < 0;
        }
        return m_items[static_cast<std::size_t>(a)].key
            < m_items[static_cast<std::size_t>(b)].key;
    });

    // 筛选：在排好序的那一份上走一遍（这样 `↑` 的顺序与筛选结果一致）。
    const QString needle = m_filter.trimmed().toLower();
    m_visible.clear();
    for (int index : m_order) {
        if (needle.isEmpty()
            || m_search[static_cast<std::size_t>(index)].contains(needle)) {
            m_visible.push_back(index);
        }
    }
}

void AppListModel::rebuildRows(int keepItem)
{
    m_rows.clear();
    m_pinnedShown.clear();
    m_recentShown.clear();

    // 把状态里的键对到**当前目录**的条目上：目录里已经没有的（卸载了、改名了）
    // 直接跳过，但**不从状态里删掉** —— 用户可能只是暂时换了开始菜单（比如
    // 换了一台机器同步过来的配置），文件里留着它没坏处。
    QHash<QString, bool> claimed;
    for (const QString &key : m_pinned) {
        const auto found = m_keyToItem.constFind(key);
        if (found == m_keyToItem.constEnd() || claimed.contains(key)) {
            continue;
        }
        claimed.insert(key, true);
        m_pinnedShown.push_back(found.value());
    }
    for (const QString &key : m_recent) {
        if (static_cast<int>(m_recentShown.size()) >= core::kRecentLimit) {
            break;
        }
        const auto found = m_keyToItem.constFind(key);
        if (found == m_keyToItem.constEnd() || claimed.contains(key)) {
            // 已经固定住的不再在「最近使用」里重复出现一遍。
            continue;
        }
        claimed.insert(key, true);
        m_recentShown.push_back(found.value());
    }

    const bool filtering = !m_filter.trimmed().isEmpty();
    if (filtering) {
        // 筛选：扁平网格（不分区、没有按钮）—— 与加分区之前完全一致。
        pushGridRows(m_visible);
    } else if (m_allMode) {
        pushButtonRow(tr("← 返回"), false);
        QString previous;
        for (int index : m_order) {
            const QString &letter = m_letter[static_cast<std::size_t>(index)];
            if (letter != previous) {
                pushHeaderRow(letter);
                previous = letter;
            }
            pushListRow(index);
        }
    } else if (m_pinnedShown.empty() && m_recentShown.empty()) {
        // 第一次用（还什么都没固定、也没启动过）：直接把全部程序铺成网格，
        // 比让用户对着一个空卡片去点「全部程序」友好。
        pushGridRows(m_visible);
    } else {
        if (!m_pinnedShown.empty()) {
            pushHeaderRow(tr("已固定"));
            pushGridRows(m_pinnedShown);
        }
        if (!m_recentShown.empty()) {
            pushHeaderRow(tr("最近使用"));
            pushGridRows(m_recentShown);
        }
        pushButtonRow(allButtonText(), true);
    }

    if (m_rows.empty()) {
        m_selectedRow = -1;
        m_selectedColumn = 0;
        return;
    }
    if (keepItem >= 0) {
        const std::optional<std::pair<int, int>> position = positionOfItem(keepItem);
        if (position.has_value()) {
            m_selectedRow = position->first;
            m_selectedColumn = position->second;
            return;
        }
    }
    selectFirst();
}

void AppListModel::relayout()
{
    int content = 0;
    int items = 0;
    for (const Row &row : m_rows) {
        content += row.height;
        if (row.kind == Row::Kind::Grid || row.kind == Row::Kind::List) {
            items += static_cast<int>(row.items.size());
        }
    }
    m_visibleItems = items;

    // 网格区最多这么高（超出的靠滚动条）。至少留一格的空白，否则「一条都
    // 没筛出来」的那句提示没有地方画。
    const int budget = std::max(m_maxRows, 1) * kCellHeight;
    const int clamped = std::clamp(content, kCellHeight, budget);
    m_cardHeight = kListTop + clamped + kListBottom;
    m_filterRect = PopupRect{kPad + kInset, kPad, kInnerWidth, kFilterHeight};
    m_footerRect = PopupRect{kPad + kInset, m_cardHeight - kPad - kFooterHeight, kInnerWidth,
                             kFooterHeight};
}

void AppListModel::pushGridRows(const std::vector<int> &items)
{
    std::vector<int> chunk;
    chunk.reserve(kColumns);
    for (int index : items) {
        chunk.push_back(index);
        if (static_cast<int>(chunk.size()) == kColumns) {
            pushGridRow(std::move(chunk));
            chunk.clear();
            chunk.reserve(kColumns);
        }
    }
    if (!chunk.empty()) {
        pushGridRow(std::move(chunk));
    }
}

void AppListModel::pushGridRow(std::vector<int> items)
{
    Row row;
    row.kind = Row::Kind::Grid;
    row.models = itemModels(items);
    row.items = std::move(items);
    row.height = kCellHeight;
    m_rows.push_back(std::move(row));
}

void AppListModel::pushHeaderRow(const QString &title)
{
    Row row;
    row.kind = Row::Kind::Header;
    row.title = title;
    row.height = kHeaderHeight;
    m_rows.push_back(std::move(row));
}

void AppListModel::pushButtonRow(const QString &title, bool opensAll)
{
    Row row;
    row.kind = Row::Kind::Button;
    row.title = title;
    row.opensAll = opensAll;
    row.height = kButtonHeight;
    m_rows.push_back(std::move(row));
}

void AppListModel::pushListRow(int itemIndex)
{
    Row row;
    row.kind = Row::Kind::List;
    row.items = {itemIndex};
    row.models = itemModels(row.items);
    row.height = kListRowHeight;
    m_rows.push_back(std::move(row));
}

QVariantList AppListModel::itemModels(const std::vector<int> &items) const
{
    QVariantList list;
    list.reserve(static_cast<int>(items.size()));
    for (int index : items) {
        const AppListEntry &entry = m_items[static_cast<std::size_t>(index)];
        QVariantMap map;
        map.insert(QStringLiteral("index"), index);
        map.insert(QStringLiteral("name"), entry.name);
        map.insert(QStringLiteral("icon"), entry.iconSource);
        map.insert(QStringLiteral("pinned"),
                   !entry.key.isEmpty() && m_pinned.contains(entry.key));
        list.push_back(map);
    }
    return list;
}

void AppListModel::notifyRows()
{
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    emit stateChanged();
}

void AppListModel::selectFirst()
{
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        if (isSelectableRow(static_cast<int>(row))) {
            m_selectedRow = static_cast<int>(row);
            m_selectedColumn = 0;
            return;
        }
    }
    m_selectedRow = -1;
    m_selectedColumn = 0;
}

void AppListModel::selectLast()
{
    for (int row = static_cast<int>(m_rows.size()) - 1; row >= 0; --row) {
        if (isSelectableRow(row)) {
            m_selectedRow = row;
            m_selectedColumn = maxColumn(row);
            return;
        }
    }
    m_selectedRow = -1;
    m_selectedColumn = 0;
}

bool AppListModel::isSelectableRow(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return false;
    }
    return m_rows[static_cast<std::size_t>(row)].kind != Row::Kind::Header;
}

int AppListModel::maxColumn(int row) const
{
    if (!isSelectableRow(row)) {
        return 0;
    }
    const Row &entry = m_rows[static_cast<std::size_t>(row)];
    if (entry.kind == Row::Kind::Grid || entry.kind == Row::Kind::List) {
        return std::max(static_cast<int>(entry.items.size()) - 1, 0);
    }
    return 0;
}

std::optional<int> AppListModel::itemAt(int row, int column) const
{
    if (!isSelectableRow(row)) {
        return std::nullopt;
    }
    const Row &entry = m_rows[static_cast<std::size_t>(row)];
    if (entry.kind != Row::Kind::Grid && entry.kind != Row::Kind::List) {
        return std::nullopt;
    }
    if (column < 0 || column >= static_cast<int>(entry.items.size())) {
        return std::nullopt;
    }
    return entry.items[static_cast<std::size_t>(column)];
}

std::optional<std::pair<int, int>> AppListModel::positionOfItem(int itemIndex) const
{
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        const Row &entry = m_rows[row];
        for (std::size_t column = 0; column < entry.items.size(); ++column) {
            if (entry.items[column] == itemIndex) {
                return std::make_pair(static_cast<int>(row), static_cast<int>(column));
            }
        }
    }
    return std::nullopt;
}

} // namespace flowkeyd::app
