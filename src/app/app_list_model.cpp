#include "app/app_list_model.h"

#include "core/app_list.h"

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

// 96 DPI 下的逻辑像素（与 `window_list_model` / `help_model` 同一套纵向度量）。
//
// **卡片是 800 宽**（6 列 × 126 + 两边的内边距 12 与缩进 10）：格子 126 × 88、
// 图标 40。卡片**高度也是固定的** —— `maxRows` 行网格 + 上下占位（本机 6 行
// → 622），概览 / 筛选 / 「全部程序」列表三个视图一样高，见 `relayout()`。
// 2026-10-05 之前高度是「各行高度之和」，于是只固定了一两条时卡片会缩成
// 248 高的窄条（概览里那点内容 + 大片空白），项目所有者判定为「过小」。
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
/// 数字快速启动键一共发多少个：`0`..`9`（第 1 个拿 `0`、第 10 个拿 `9`）。
constexpr int kNumberedKeys = 10;
/// 最近使用程序的 `Alt` + 数字快捷键一共发多少个：`1`..`9`、`0`。
constexpr int kRecentKeys = 10;
/// 已固定程序的 `Alt` + 功能键快捷键一共发多少个：`F1`..`F12`。
constexpr int kPinnedKeys = 12;

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

/// 第 `line` 行的数字快速启动键（`0`..`9`）；超过前 10 行时是空串。
///
/// **号码就是显示序号**（第一个程序是 `0`）：与窗口切换器的 `1`..`9`、`0`
/// 不同，于是 `handleKey()` 里数字 → 行号是一次减法。
QString digitLabelForLine(int line)
{
    if (line < 0 || line >= kNumberedKeys) {
        return QString();
    }
    return QString::number(line);
}

/// 第 `line` 个**最近使用**程序的跳转键徽标文字（`Alt+1`..`Alt+9`、`Alt+0`）。
///
/// `Alt` 前缀是写在徽标上的：裸数字的徽标会被当成「按一下 1」——那正好是筛选
/// 号码或筛选框的打字。号码就是排序：「从 `alt 1` 排到 `alt+9` `alt+0`」。
QString recentKeyLabelForLine(int line)
{
    if (line < 0 || line >= kRecentKeys) {
        return QString();
    }
    // 第 1 个拿 `1`、……第 9 个拿 `9`、第 10 个拿 `0`。
    const int digit = (line + 1) % 10;
    return QStringLiteral("Alt+") + QString::number(digit);
}

/// 第 `line` 个**已固定**程序的固定快捷键的徽标文字（`Alt+F1`..`Alt+F12`）。
///
/// `Alt` 前缀是写在徽标上的：裸功能键的徽标会被当成「按一下 F1」——那会弹出
/// 帮助。
QString pinKeyLabelForLine(int line)
{
    if (line < 0 || line >= kPinnedKeys) {
        return QString();
    }
    return QStringLiteral("Alt+F") + QString::number(line + 1);
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
    // 底部提示就是「现在这一屏能干哪些事」的清单：卡片只有 776 宽，拼得太多会
    // 被省略号截掉，所以每一段都尽量短。
    QStringList parts;
    parts << tr("%1 个程序").arg(visibleCount());
    if (m_numbered) {
        // 筛选之后的扁平网格：数字键是最省事的第二段输入（easymotion 风格）。
        parts << tr("0–9 直接启动");
    }
    if (!m_recentKeys.isEmpty()) {
        parts << tr("Alt+1–0 直接启动");
    }
    if (!m_pinKeys.isEmpty()) {
        parts << tr("Alt+F1–F12 直接启动");
    }
    if (m_allMode && m_filter.trimmed().isEmpty()) {
        // 「全部程序」列表：没有左右可走的格子，`Space`（固定）也就没提。
        parts << tr("↑↓ 选择") << tr("Enter 启动") << tr("Esc 返回");
        return parts.join(QStringLiteral("    "));
    }
    parts << tr("↑↓←→ 选择") << tr("Enter 启动");
    if (m_filter.trimmed().isEmpty()) {
        // 筛选框里有字时 `Space` 是打空格，不能再提「固定」。
        parts << tr("Space 固定");
    }
    parts << tr("右键菜单") << tr("Esc 关闭");
    return parts.join(QStringLiteral("    "));
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
    // 选中项跟着走：这一格在当前视图里就把高亮挪过去。**找不到也照样返回
    // `choose`** —— `Alt` + 数字 / 功能键（已固定 / 最近使用的快捷键）在筛选
    // 之后可能作用在一个没显示出来的条目上，那正是它存在的意义。
    const std::optional<std::pair<int, int>> position = positionOfItem(itemIndex);
    if (position.has_value()
        && (position->first != m_selectedRow || position->second != m_selectedColumn)) {
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

QVariantMap AppListModel::handleKey(int key, int modifiers)
{
    // 最近使用程序的跳转键：`Alt` + `1`..`9`、`Alt` + `0`（见头文件）。
    //
    // 必须**恰好**按住 `Alt`：`Ctrl`/`Shift` 的组合与 `AltGr`（在 Windows 上是
    // `Ctrl+Alt`）都要放行给筛选框。
    if (!m_recentKeys.isEmpty() && modifiers == Qt::AltModifier && key >= Qt::Key_0
        && key <= Qt::Key_9) {
        // 第 1 个是 `Alt+1`、……第 9 个是 `Alt+9`、第 10 个是 `Alt+0`。
        const int line = key == Qt::Key_0 ? 9 : key - Qt::Key_1;
        if (line < static_cast<int>(m_recentShown.size())) {
            return activateItem(m_recentShown[static_cast<std::size_t>(line)]);
        }
        // 没分到号的数字：吃掉它。放行的话它会变成筛选框里的一个字符
        // （用户想按 `Alt+3` 却看到筛选串多了一个 `3`）。
        return makeNoneDecision(true);
    }
    // 已固定程序的固定快捷键：`Alt` + `F1`..`F12`（见头文件）。
    //
    // 同样必须**恰好**按住 `Alt`。
    if (!m_pinKeys.isEmpty() && modifiers == Qt::AltModifier && key >= Qt::Key_F1
        && key <= Qt::Key_F12) {
        const int line = key - Qt::Key_F1;
        if (line < static_cast<int>(m_pinnedShown.size())) {
            return activateItem(m_pinnedShown[static_cast<std::size_t>(line)]);
        }
        // 没固定到第 13 个以后的：吃掉这个键（理由同上）。
        return makeNoneDecision(true);
    }
    // 数字快速启动键：只有在「筛选之后」这种模式里才把数字键吃掉（否则用户要在
    // 筛选串里打数字，例如名字里带数字的 `7-Zip`）。没有对应条目的号码被吃掉但
    // 什么都不做 —— 不能漏给筛选框，否则「9」会把列表筛空。
    if (m_numbered && key >= Qt::Key_0 && key <= Qt::Key_9) {
        const int line = key - Qt::Key_0;
        if (line < visibleCount()) {
            return activateItem(m_visible[static_cast<std::size_t>(line)]);
        }
        return makeNoneDecision(true);
    }
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

    // 最近使用程序的跳转键（`Alt` + `1`..`9` / `Alt` + `0`，与 `m_recentShown`
    // 的顺序一一对应，最多 10 个）。**不随筛选 / 视图变化**。
    m_recentKeys.clear();
    const int recents = std::min(static_cast<int>(m_recentShown.size()), kRecentKeys);
    for (int line = 0; line < recents; ++line) {
        m_recentKeys.insert(m_recentShown[static_cast<std::size_t>(line)],
                            recentKeyLabelForLine(line));
    }

    // 已固定程序的固定快捷键（`Alt` + `F1`..`F12`，与 `m_pinnedShown` 的顺序
    // 一一对应，最多 12 个）。**不随筛选 / 视图变化**：卡片开着就一直有效。
    //
    // 与 `m_itemKeys` / `m_recentKeys` 分开存：筛选号码是「这次筛选里第几个」，
    // 数字 / 功能键是「最近使用 / 固定列表里第几个」，它们可能同时命中同一个
    // 条目；`itemModels()` 让筛选号码优先显示，但 `handleKey()` 每条路径都认。
    m_pinKeys.clear();
    const int pins = std::min(static_cast<int>(m_pinnedShown.size()), kPinnedKeys);
    for (int line = 0; line < pins; ++line) {
        m_pinKeys.insert(m_pinnedShown[static_cast<std::size_t>(line)],
                         pinKeyLabelForLine(line));
    }

    const bool filtering = !m_filter.trimmed().isEmpty();

    // 数字快速启动键（easymotion 风格）：**筛选之后**前 10 条各分一个数字键，
    // 号码就是显示序号（第一个程序拿 `0`）。行是按 `m_visible` 铺的，所以
    // 号码也只在这一种视图里存在（见头文件）。
    m_numbered = false;
    m_itemKeys.clear();
    if (filtering && !m_visible.empty()) {
        m_numbered = true;
        const int keys = std::min(static_cast<int>(m_visible.size()), kNumberedKeys);
        for (int line = 0; line < keys; ++line) {
            m_itemKeys.insert(m_visible[static_cast<std::size_t>(line)],
                              digitLabelForLine(line));
        }
    }

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
    int items = 0;
    for (const Row &row : m_rows) {
        if (row.kind == Row::Kind::Grid || row.kind == Row::Kind::List) {
            items += static_cast<int>(row.items.size());
        }
    }
    m_visibleItems = items;

    // 卡片高度**恒定** = 一屏网格（`m_maxRows` 行 × 格高）+ 上下占位。
    //
    // 不跟着内容走（项目所有者 2026-10-05 拍板）：概览里只固定了一两条时，
    // 按内容算出来的卡片只有 248 高 —— 半屏空白的一根窄条，看着就是「过小」。
    // 三个视图（概览 / 筛选 / 「全部程序」列表）用同一个高度，切视图也不会再
    // 忽高忽低。`m_maxRows` 仍然按屏幕高度算（`rowsForAvailableHeight()`），
    // 所以矮屏上卡片照样不会溢出；超出一屏的行由右侧滚动条滚。
    m_cardHeight = kListTop + std::max(m_maxRows, 1) * kCellHeight + kListBottom;
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
        // 筛选号码优先（号码必须与显示序号一一对应）；否则是最近使用的
        // `Alt` + 数字，再否则是已固定的 `Alt` + 功能键。
        const QString digit = m_itemKeys.value(index);
        const QString recent = m_recentKeys.value(index);
        map.insert(QStringLiteral("key"),
                   !digit.isEmpty() ? digit
                                    : (!recent.isEmpty() ? recent
                                                         : m_pinKeys.value(index)));
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
