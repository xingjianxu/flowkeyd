// 程序启动器（`apps` 动作）的**纯逻辑**模型：一张**行**式列表，行有四种形状。
//
// 与 `window_list_model` / `help_model` / `menu_model` 一样只依赖 QtCore：几何、
// 筛选、键盘选中项、`Enter`/`Space`/`Esc` 的语义都在这里，`tst_app_list_model`
// 直接覆盖它们。开始菜单的扫描在 `platform/win/apps`（Win32），图标的取像素在
// `app::AppIconProvider`（QtGui + shell），持久状态（固定 / 最近使用）的读写由
// `app::PopupHost` 负责 —— 模型只认「名字 + 图标 URL + 稳定键」这几个字符串。
//
// ---------------------------------------------------------------------------
// 卡片里的三种视图（同一个模型、同一个 `ListView`，只是行不一样）
// ---------------------------------------------------------------------------
//   * **概览**（默认）：从上到下是「已固定」网格、「最近使用」网格（最多两行
//     = 12 个，`core::kRecentLimit`），最后一个「全部程序（N）」按钮。
//     还什么都没固定、也没启动过任何程序时（第一次用），概览直接就是**全部程序
//     的网格** —— 比让用户对着一个空卡片点按钮友好。
//   * **筛选**（输入框非空）：一行一行铺开匹配到的程序（扁平网格，不分段、
//     没有按钮），与加分区之前的行为一致。
//   * **全部**（点「全部程序」按钮）：一个「← 返回」按钮 + 按**名字 / 拼音首字母
//     分组**的一行一个程序的列表，带滚动条。`Esc` 回到概览。
//
// 行 = `Row`，`rowKind` 是给 QML 的分支依据：
//   * `header` —— 分组表头（「已固定」「最近使用」、或 `A`–`Z` / `#`）；不可选中。
//   * `grid`   —— 一行最多 6 个「图标 + 名字」格子。
//   * `button` —— 整行一个按钮（「全部程序（N）」/「← 返回」）。
//   * `list`   —— 「全部」列表里的一行（图标 + 名字 + 固定标记）。
//
// **选中项是（行, 列）**：`↑`/`↓` 上下走一行（跳过表头）、`←`/`→` 在同一行里挪
// 一格（到边界夹住、不回绕）、`Home`/`End` 到头尾、`PgUp`/`PgDn` 翻页。
// QML 只需要 `selectedRow` 就能把选中行带进视野。
//
// **`Space` 切换固定**（只有在筛选框为空时才截走它 —— 否则用户没法在筛选串里
// 打空格，而 `Visual Studio Code` 这种名字很需要）。`Enter` 启动选中的程序，
// 落在按钮上就是「打开全部」/「返回概览」。
//
// **数字快速启动键**（项目所有者 2026-10 要求，easymotion 风格）：**筛选之后**
// 前 10 条各分到一个数字键，号码就是它的**显示序号**（第 1 个 `0`、第 2 个 `1`、
// … 第 10 个 `9`；与窗口切换器的 `1`..`9`、`0` 不同），画在那一格图标的右上角，
// 按一下直接启动它。只在筛选那一种扁平网格里生效（概览 / 「全部程序」列表不编号，
// 因为那时按键列表并不稳定）；号码只给前 10 条，超出的没有号。
// **没有对应条目的号码会被吃掉但什么都不做**（不能漏给筛选框，否则「5」会把
// 列表筛空）；筛选框为空时数字键照常打进筛选框（`7-Zip` 这类名字要能用数字筛）。
//
// **`Alt` + 数字 = 最近使用程序的跳转键**（项目所有者 2026-10 要求）：按
// 「最近使用」里的顺序，第 1 个是 `Alt+1`、第 2 个 `Alt+2`……第 9 个 `Alt+9`、
// 第 10 个 `Alt+0`；最多 10 个，再往后的没有（角上也不画徽标）。
//
// **`Alt` + 功能键 = 已固定程序的固定快捷键**（项目所有者 2026-10 要求）：按
// 「已固定」里的顺序，第 1 个是 `Alt+F1`、第 2 个 `Alt+F2`……第 12 个 `Alt+F12`；
// 第 13 个起没有功能键、角上也不画徽标。
//
// 与筛选号码不同，这两套键**不随筛选变化**：卡片开着的任何视图里都能按
// （筛选之后也照样能启动）。
//
// 为什么必须带 `Alt`：裸字母/数字是筛选框的主要输入，占用它们就打不了字了
// （`handleKey()` 里那条注释、以及 `scripts/acceptance.ps1` 的启动器分段）。
// 徽标上写的是**完整按键**（`Alt+1` / `Alt+F1`），与数字号码同一种形状 ——
// 看一眼就知道该按什么；**筛选号码优先显示**：同一个程序既在前 10 条里又是
// 固定 / 最近的，角上显示号码（号码必须与显示序号一一对应），但 `Alt` + 它的
// 数字 / 功能键照样有效。
//
// **不自动启动**：筛选到只剩一个也**不**自动执行（窗口切换器那边会自动激活，
// 因为那是「切」；这里是「启动一个新程序」，误启动的代价比多按一下 `Enter` 大）。
//
// **图标**：每一行的角色是一个 `image://flowkeyd-app/<key>` URL（由 `PopupHost`
// 那边的 dispatcher 算好），像素由异步的图片提供者按需去 shell 里取（每个图标
// 约 3 ms，不能同步做）。
#pragma once

#include "app/popup_layout.h"
#include "core/launcher_state.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <utility>
#include <vector>

namespace flowkeyd::app {

/// 启动器里显示的一个程序（纯显示数据）。
struct AppListEntry
{
    /// 程序名（shell 给的显示名：开始菜单里看到的那个）。
    QString name;
    /// 图标的 `image://` URL；空串表示这一行没有图标（画一个占位的方块）。
    QString iconSource;
    /// **稳定身份**：`core::appIconKey()` 算出来的启动名键。
    ///
    /// 「固定」与「最近使用」都记这个键（不是下标、也不是路径原文）：
    /// 列表重扫之后条目的下标会变、路径的大小写与分隔符会变，而同一个程序的键
    /// 永远是同一个。**空串表示这一行不参与固定 / 最近使用**（预热用的假数据）。
    QString key;
};

/// 程序启动器的行式模型。
class AppListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString caption READ caption NOTIFY stateChanged)
    Q_PROPERTY(int cardWidth READ cardWidth CONSTANT)
    Q_PROPERTY(int cardHeight READ cardHeight NOTIFY stateChanged)
    Q_PROPERTY(int cardRadius READ cardRadius CONSTANT)
    Q_PROPERTY(QRect filterRect READ filterRect NOTIFY stateChanged)
    Q_PROPERTY(QRect footerRect READ footerRect NOTIFY stateChanged)
    Q_PROPERTY(QString filter READ filter NOTIFY stateChanged)
    Q_PROPERTY(QString filterPlaceholder READ filterPlaceholder CONSTANT)
    Q_PROPERTY(QString emptyMessage READ emptyMessage NOTIFY stateChanged)
    Q_PROPERTY(QString footerText READ footerText NOTIFY stateChanged)
    Q_PROPERTY(bool hasMatches READ hasMatches NOTIFY stateChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY itemsChanged)
    /// 当前视图里显示出来的程序数（概览里是「固定 + 最近使用」，第一次用时是全部）。
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY stateChanged)
    /// 网格一次画几列（固定 6）。
    Q_PROPERTY(int columns READ columns CONSTANT)
    Q_PROPERTY(int cellWidth READ cellWidth CONSTANT)
    Q_PROPERTY(int cellHeight READ cellHeight CONSTANT)
    Q_PROPERTY(int iconSize READ iconSize CONSTANT)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY stateChanged)
    /// 网格区顶边 / 底边。
    Q_PROPERTY(int listTop READ listTop CONSTANT)
    Q_PROPERTY(int listBottom READ listBottom CONSTANT)
    /// 选中的行（`ListView.positionViewAtIndex` 用的就是它）；没有可选行时是 -1。
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedChanged)
    /// 选中行里的第几格（`button` / `list` 行只在选中时是 0）。
    Q_PROPERTY(int selectedColumn READ selectedColumn NOTIFY selectedChanged)
    /// 当前是不是「全部程序」列表。
    Q_PROPERTY(bool allMode READ allMode NOTIFY stateChanged)
    Q_PROPERTY(bool hasPinned READ hasPinned NOTIFY stateChanged)
    Q_PROPERTY(bool hasRecent READ hasRecent NOTIFY stateChanged)
    Q_PROPERTY(int pinnedCount READ pinnedCount NOTIFY stateChanged)
    Q_PROPERTY(int recentCount READ recentCount NOTIFY stateChanged)
    Q_PROPERTY(QString allButtonText READ allButtonText NOTIFY stateChanged)
    /// 当前是不是「数字快速启动」模式（筛选串非空且至少有一条匹配，见文件头）。
    Q_PROPERTY(bool numberedMode READ numberedMode NOTIFY stateChanged)

public:
    enum Role {
        /// `"header"` / `"grid"` / `"button"` / `"list"`。
        RowKindRole = Qt::UserRole + 1,
        /// 表头与按钮上的文字。
        RowTitleRole,
        /// 这一行里的条目（`grid` / `list`）：
        /// `{ index, name, icon, pinned, key }` 的数组；`key` 是画在格子角上的
        /// 快速启动标识：筛选之后前 10 条是 `"0"`..`"9"`（裸按键），最近使用的
        /// 是 `"Alt+1"`..`"Alt+0"`，已固定的是 `"Alt+F1"`..`"Alt+F12"`，
        /// 都没有时是空串。与 `handleKey()` 里的规则一致：筛选号码优先。
        RowItemsRole,
        /// 这一行是不是当前选中的那一行。
        ///
        /// **不叫 `highlighted`**：QML 里那些行是标准的 `ItemDelegate`，它自己
        /// 就有这个属性；委托里的 `required property` 名字必须等于角色名。
        RowSelectedRole,
        /// 这一行里被选中的格子下标（`grid` 是列号，`button` / `list` 选中时是 0，
        /// 没选中是 -1）。
        RowSelectedColumnRole,
        /// 这一行的高度（逻辑像素）。
        RowHeightRole,
    };
    Q_ENUM(Role)

    explicit AppListModel(QObject *parent = nullptr);

    /// 换一批程序（同一个窗口复用；`PopupHost` 再次弹出时调它）。
    void setItems(std::optional<QString> title, std::vector<AppListEntry> items);
    /// 换一份持久状态（固定 / 最近使用）。可以在 `setItems()` 之前或之后调。
    void setState(const core::LauncherState &state);
    /// 当前状态（`PopupHost` 拿它落盘）。
    core::LauncherState launcherState() const;
    /// 所在的显示器最多能显示几行网格（`PopupHost` 按工作区算好传进来）。
    void setMaxRows(int rows);
    int maxRows() const { return m_maxRows; }

    /// 给定一块工作区的高度，最多能放几行网格（纯算术）。
    static int rowsForAvailableHeight(int availableHeight);

    QString caption() const;
    int cardWidth() const;
    int cardHeight() const { return m_cardHeight; }
    int cardRadius() const;
    /// 矩形（`Q_PROPERTY` 的类型必须是 `QRect`：`PopupRect` 没有注册给 QML，
    /// 写成它的话 QML 拿到的 `x`/`y` 全是 undefined，控件会堆在左上角；
    /// `PopupRect` 有到 `QRect` 的隐式转换，所以 getter 保持原类型）。
    QRect filterRect() const { return m_filterRect; }
    QRect footerRect() const { return m_footerRect; }
    QString filter() const { return m_filter; }
    QString filterPlaceholder() const;
    QString emptyMessage() const;
    QString footerText() const;
    /// 当前视图里有没有可选中的行（表头不算）。
    bool hasMatches() const;
    int totalCount() const { return static_cast<int>(m_items.size()); }
    int visibleCount() const { return m_visibleItems; }
    int columns() const;
    int cellWidth() const;
    int cellHeight() const;
    int iconSize() const;
    int listTop() const;
    int listBottom() const;

    /// 第 `line` 个**显示出来**的条目（按下标从左到右、从上到下数，表头与按钮
    /// 不占号）；越界返回 `std::nullopt`。
    ///
    /// 主要给测试与诊断工具用（预览工具拿它把筛选结果逐个列出来）；界面自己走
    /// 的是 QML 那边的行委托。
    std::optional<int> itemIndexForVisible(int line) const;

    bool allMode() const { return m_allMode; }
    int selectedRow() const { return m_selectedRow; }
    int selectedColumn() const { return m_selectedColumn; }
    /// 选中的**条目**下标（选在按钮 / 表头上、或没有可选行时是 -1）。
    int selectedItem() const;
    bool hasPinned() const { return !m_pinnedShown.empty(); }
    bool hasRecent() const { return !m_recentShown.empty(); }
    int pinnedCount() const { return static_cast<int>(m_pinnedShown.size()); }
    int recentCount() const { return static_cast<int>(m_recentShown.size()); }
    QString allButtonText() const;
    bool numberedMode() const { return m_numbered; }

    /// 换筛选串（QML 的筛选框直接调它）。筛选之后选中项回到第一行。
    ///
    /// 返回值与 `WindowListModel::setFilter()` 同形，但**永远是**
    /// `{ decision: "none" }`：启动器不做「筛到一个就自动启动」（那是「切窗口」
    /// 的语义，而这里会真的拉起一个新进程）。留这个形状是为了两个筛选框在 QML
    /// 里能用同一段 `applyDecision` 代码。
    Q_INVOKABLE QVariantMap setFilter(const QString &filter);
    Q_INVOKABLE void clearFilter();

    /// 上下左右移动选中项（到边界夹住，不回绕）。`dy` 走一行（跳过表头）。
    Q_INVOKABLE void moveSelection(int dx, int dy);
    /// 鼠标悬停到某个条目上（悬停即高亮）。找不到就忽略。
    Q_INVOKABLE void hoverItem(int itemIndex);
    /// 激活第 `itemIndex` 个条目（单击一格 / 双击一行 / `Alt` + 数字 / 功能键）：
    /// `{ decision: "choose", index, handled: true }`。
    ///
    /// 条目**不在当前视图里**（筛选把它筛掉了）也照样返回 `choose` —— `Alt` +
    /// 数字 / 功能键作用于已固定 / 最近使用的程序，而它们不随筛选变化；只是那时
    /// 不挪选中项。
    Q_INVOKABLE QVariantMap activateItem(int itemIndex);
    /// 切换某个条目的固定状态（`Space`）。
    Q_INVOKABLE void togglePinItem(int itemIndex);

    /// 打开 / 关掉「全部程序」列表（点按钮或按 `Enter` 落在按钮上）。
    Q_INVOKABLE void showAll();
    Q_INVOKABLE void showOverview();

    /// 一次按键的处理结果：
    /// `{ decision: "none"|"choose"|"cancel", index, handled }`。
    ///
    /// 只管方向键 / `Home` / `End` / `PgUp` / `PgDn` / `Enter` / `Space` / `Esc`，
    /// 外加 `Alt` + 数字（最近使用）与 `Alt` + 功能键（已固定）（见文件头）；
    /// **字符与退格不在这里**（它们归筛选框那个标准 `TextField`）。
    ///
    /// `modifiers` 是 `Qt::KeyboardModifiers`（QML 传 `event.modifiers`）。
    /// 只有**恰好按住 `Alt`** 的数字 / 功能键才被接走：`Ctrl`/`Shift` 的组合
    /// 与 `AltGr`（在 Windows 上是 `Ctrl+Alt`）都放行给筛选框。
    Q_INVOKABLE QVariantMap handleKey(int key, int modifiers = 0);

    /// 右键菜单（原生 shell 菜单，见 `platform/win/shell_menu.h`）关掉之后的决定。
    ///
    /// `invoked` = 用户在菜单里真的选了某一条。项目所有者 2026-10 拍板：
    /// **选中条目就收卡片，取消（`Esc` / 点菜单外面）就留着**——与开始菜单一致，
    /// 而且“打开文件位置”“属性”这类命令要能自己拿前台，不能跟一张置顶卡片抢。
    /// 返回 `{ decision: "cancel"|"none", handled: true }`。
    Q_INVOKABLE QVariantMap afterContextMenu(bool invoked);

    /// 启动某个条目之后记一笔「最近使用」（顺序 = 最近的在前）。
    void noteLaunched(int itemIndex);

    /// 再次打开时清空筛选与「全部」模式、选中项回到第一行。
    void reset();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void itemsChanged();
    void stateChanged();
    void selectedChanged();
    /// 固定 / 最近使用变了：`PopupHost` 接它把状态写回磁盘。
    void stateEdited();

private:
    /// 一行。
    struct Row
    {
        enum class Kind { Header, Grid, Button, List };

        Kind kind = Kind::Grid;
        /// 表头 / 按钮上的文字。
        QString title;
        /// 这一行里的条目（`m_items` 下标）。
        std::vector<int> items;
        /// `items` 给 QML 的那一份（`{ index, name, icon, pinned }`），建行时算好，
        /// 免得每次 `data()` 都重新分配一堆 `QVariantMap`。
        QVariantList models;
        int height = 0;
        /// `Button` 行：true = 「全部程序」（打开列表），false = 「← 返回」。
        bool opensAll = false;
    };

    /// 重算顺序、行与选中项（`beginResetModel` 由调用方包）。`keepItem` 是想保住
    /// 选中项的条目下标（`< 0` 时选中第一行）。
    void rebuild(int keepItem);
    /// 重算 `m_order`（按拼音 / 字母排序的 `m_items` 下标）。
    void rebuildOrder();
    /// 重算 `m_rows` 与 `m_selectedRow` / `m_selectedColumn`（保留 `keepItem` 上的
    /// 选中；`keepItem < 0` 时选中第一行）。
    void rebuildRows(int keepItem);
    /// 重算几何与计数（不发信号）。
    void relayout();
    /// 把一串条目按 6 个一组铺成网格行。
    void pushGridRows(const std::vector<int> &items);
    /// 生成一行网格。
    void pushGridRow(std::vector<int> items);
    void pushHeaderRow(const QString &title);
    void pushButtonRow(const QString &title, bool opensAll);
    void pushListRow(int itemIndex);
    QVariantList itemModels(const std::vector<int> &items) const;
    void notifyRows();
    /// 把选中项挪到第一个可选行；没有可选行时 `m_selectedRow = -1`。
    void selectFirst();
    void selectLast();
    bool isSelectableRow(int row) const;
    int maxColumn(int row) const;
    std::optional<int> itemAt(int row, int column) const;
    std::optional<std::pair<int, int>> positionOfItem(int itemIndex) const;

    std::optional<QString> m_title;
    std::vector<AppListEntry> m_items;
    /// 与 `m_items` 一一对应的**搜索文本**（`core::appSearchText()` 算好的：
    /// 小写名字 + 全拼 + 首字母缩写），装载时算一次。
    std::vector<QString> m_search;
    /// 与 `m_items` 一一对应的**排序键**（`core::appSortText()`：主读音全拼，
    /// 数字/符号开头的前面加个 `0`），装载时算一次。
    std::vector<QString> m_sort;
    /// 与 `m_items` 一一对应的分组表头（`A`–`Z` / `#`）。
    std::vector<QString> m_letter;
    /// `key` → `m_items` 下标。
    QHash<QString, int> m_keyToItem;
    /// 按 `m_sort` 排好序的 `m_items` 下标（筛选、概览与「全部」列表都用它）。
    std::vector<int> m_order;
    /// `m_order` 里匹配当前筛选串的那些下标。
    std::vector<int> m_visible;
    /// 当前视图的行。
    std::vector<Row> m_rows;
    /// 数字快速启动键：`m_items` 下标 → `"0"`..`"9"`（只有一个扁平网格的筛选
    /// 视图里才会非空，见文件头）。
    QHash<int, QString> m_itemKeys;
    /// 最近使用程序的跳转键：`m_items` 下标 → `"Alt+1"`..`"Alt+9"` / `"Alt+0"`
    /// （按「最近使用」顺序，最多 10 个；见文件头）。
    QHash<int, QString> m_recentKeys;
    /// 已固定程序的固定快捷键：`m_items` 下标 → `"Alt+F1"`..`"Alt+F12"`（按
    /// 「已固定」顺序，最多 12 个；见文件头）。筛选号码优先显示，两者都可能
    /// 命中时角上显示号码 —— 但 `Alt` + 功能键始终有效。
    QHash<int, QString> m_pinKeys;

    /// 用户输入的筛选串（原样保留大小写）。
    QString m_filter;
    /// 固定的程序（图标键，固定顺序）。
    QStringList m_pinned;
    /// 最近使用的程序（图标键，最近的在前）。
    QStringList m_recent;
    /// `m_pinned` / `m_recent` 里真正在目录中、且当前显示出来的那些条目下标。
    std::vector<int> m_pinnedShown;
    std::vector<int> m_recentShown;

    bool m_allMode = false;
    /// 是不是「数字快速启动」模式（见文件头）。
    bool m_numbered = false;
    int m_maxRows = 6;
    int m_visibleItems = 0;
    int m_selectedRow = -1;
    int m_selectedColumn = 0;

    int m_cardHeight = 0;
    PopupRect m_filterRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
