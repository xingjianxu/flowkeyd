// 窗口切换器（`windows` 动作）的**纯逻辑**模型。
//
// 与 `help_model` / `menu_model` 一样只依赖 QtCore：几何、筛选、`可见/总数`
// 计数、键盘选中项、`Enter`/`Esc` 的语义都在这里，`tst_window_list_model`
// 直接覆盖它们。窗口枚举（`EnumWindows`）与真正的激活动作都在 `platform` /
// `app::Dispatcher` 里 —— 模型只认「标题 + 进程名」这两个字符串。
//
// **界面上的交互全部交给标准控件**（与 `HelpPopup.qml` 同一条路线）：
//   * 筛选框是真正的 `TextField`：光标、选区、输入法、鼠标点选全部由 Qt 负责，
//     模型只在 `setFilter()` 里接收最终文本；
//   * 列表是 `ListView` + 标准 `ItemDelegate`：滚动归 Qt，鼠标悬停由委托的
//     `hovered` 驱动（悬停即高亮，与 `menu` 一样），单击直接给出可见行下标。
//
// **自动激活**：`setFilter()` 在「筛选非空、且只剩一个窗口」时会返回
// `{ decision = "choose", index }`，QML 立刻把它交给宿主去激活。这样用户输入到
// 唯一匹配时窗口就换了，不必再按 `Enter`。空的筛选（刚打开时）绝不自动激活。
#pragma once

#include "app/popup_layout.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace flowkeyd::app {

/// 切换器里显示的一个窗口（纯显示数据，句柄不在这里）。
struct WindowListEntry
{
    /// 窗口标题。
    QString title;
    /// 属主进程的小写可执行文件名（`chrome.exe`）；拿不到时为空串。
    QString process;
};

/// 窗口切换器的模型。
///
/// 作为 `QAbstractListModel` 直接喂给 QML 的 `ListView`，角色是
/// `windowTitle` / `windowProcess` / `rowSelected`。
class WindowListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString title READ title NOTIFY itemsChanged)
    Q_PROPERTY(QString caption READ caption NOTIFY stateChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY stateChanged)
    Q_PROPERTY(int cardWidth READ cardWidth NOTIFY stateChanged)
    Q_PROPERTY(int cardHeight READ cardHeight NOTIFY stateChanged)
    Q_PROPERTY(int cardRadius READ cardRadius CONSTANT)
    Q_PROPERTY(QRect titleRect READ titleRect NOTIFY stateChanged)
    Q_PROPERTY(QRect countRect READ countRect NOTIFY stateChanged)
    Q_PROPERTY(QRect filterRect READ filterRect NOTIFY stateChanged)
    Q_PROPERTY(QRect footerRect READ footerRect NOTIFY stateChanged)
    Q_PROPERTY(QString filter READ filter NOTIFY stateChanged)
    Q_PROPERTY(QString filterPlaceholder READ filterPlaceholder CONSTANT)
    Q_PROPERTY(QString emptyMessage READ emptyMessage NOTIFY stateChanged)
    Q_PROPERTY(QString footerText READ footerText CONSTANT)
    Q_PROPERTY(bool hasMatches READ hasMatches NOTIFY stateChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY itemsChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY stateChanged)
    /// 卡片一次最多能画出来的行数（`min(可见条数, maxRows)`，至少 1）。
    Q_PROPERTY(int visibleRows READ visibleRows NOTIFY stateChanged)
    /// 键盘选中项 / 高亮的可见行下标（悬停也会改它）。
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY stateChanged)
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    /// 行与行之间的空隙；`ListView` 的委托高度是 `rowHeight + rowSpacing`。
    Q_PROPERTY(int rowSpacing READ rowSpacing CONSTANT)
    Q_PROPERTY(int listTop READ listTop CONSTANT)
    Q_PROPERTY(int listBottom READ listBottom CONSTANT)
    Q_PROPERTY(int rowInset READ rowInset CONSTANT)

public:
    enum Role {
        WindowTitleRole = Qt::UserRole + 1,
        WindowProcessRole,
        /// 这一行是不是当前高亮的那一行。
        ///
        /// **不叫 `highlighted`**：QML 里每一行是标准的 `ItemDelegate`，它自己
        /// 就有一个 `highlighted` 属性（标准样式用它画高亮），而委托里的
        /// `required property` 名字必须等于模型角色名 —— 撞名就声明不了。
        RowSelectedRole,
    };
    Q_ENUM(Role)

    explicit WindowListModel(QObject *parent = nullptr);

    /// 换一批窗口（同一个窗口复用；`PopupHost` 再次弹出时调它）。
    void setItems(std::optional<QString> title, std::vector<WindowListEntry> items);
    /// 所在的显示器最多能显示多少行（`PopupHost` 按工作区算好传进来）。
    void setMaxRows(int rows);
    int maxRows() const { return m_maxRows; }

    /// 给定一块工作区的高度，最多能放几行（与 `help_model` 同源，纯算术）。
    static int rowsForAvailableHeight(int availableHeight);

    QString title() const;
    QString caption() const;
    QString countText() const;
    int cardWidth() const;
    int cardHeight() const { return m_cardHeight; }
    int cardRadius() const;
    PopupRect titleRect() const { return m_titleRect; }
    PopupRect countRect() const { return m_countRect; }
    PopupRect filterRect() const { return m_filterRect; }
    PopupRect footerRect() const { return m_footerRect; }
    QString filter() const { return m_filter; }
    QString filterPlaceholder() const;
    QString emptyMessage() const;
    QString footerText() const;
    bool hasMatches() const { return !m_visible.empty(); }
    int totalCount() const { return static_cast<int>(m_items.size()); }
    int visibleCount() const { return static_cast<int>(m_visible.size()); }
    int visibleRows() const { return m_rows; }
    int selected() const { return m_selected; }
    int rowHeight() const;
    int rowSpacing() const;
    int listTop() const;
    int listBottom() const;
    int rowInset() const;

    /// 可见行下标对应的原始条目下标。
    const std::vector<int> &visibleIndices() const { return m_visible; }
    std::optional<int> itemIndexForVisible(int line) const;

    /// 换筛选串。**QML 的筛选框直接调它**（`onTextChanged`）。
    ///
    /// 返回值是一个决定：筛选非空且只剩一个窗口时是
    /// `{ decision: "choose", index }`（`index` 是**条目**下标），QML 立刻把
    /// 它交给宿主；否则是 `{ decision: "none", index: -1 }`。
    Q_INVOKABLE QVariantMap setFilter(const QString &filter);
    Q_INVOKABLE void clearFilter();

    /// 上下移动选中项（高亮跟着走）；到边界夹住（不回绕）。
    Q_INVOKABLE void moveSelection(int delta);
    /// 鼠标悬停到第 `line` 行（可见行下标）；越界或负数忽略。悬停即高亮。
    Q_INVOKABLE void setHover(int line);

    /// 激活第 `line` 行（可见行下标）。返回 `{ decision: "choose", index }`
    /// （`index` 是条目下标）；没有可见条目时返回 `{ decision: "none" }`。
    Q_INVOKABLE QVariantMap activate(int line);

    /// 一次按键的处理结果：
    /// `{ decision: "none"|"choose"|"cancel", index, handled }`。
    ///
    /// 只管 `↑`/`↓`/`PgUp`/`PgDn`、`Enter`（激活高亮那一行）与 `Esc`（关窗）。
    /// **字符与退格不在这里**：它们归筛选框那个标准 `TextField`。没被接住的键
    /// 返回 `handled == false`，QML 把它放行给输入框。
    Q_INVOKABLE QVariantMap handleKey(int key);

    /// 再次打开时清空筛选、选中项。
    void reset();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void itemsChanged();
    void stateChanged();
    /// 选中行变了（或者列表被换过 / 筛过，视图该把选中项带回视野）。
    void selectedChanged();

private:
    /// 重新算筛选结果、几何与计数（不发信号，调用方负责把 reset 包起来）。
    void refilter();
    void relayout();
    void notifyRows();
    /// 当前高亮那条的行下标；没有可见条目时返回 -1。
    int activeLine() const;

    std::optional<QString> m_title;
    std::vector<WindowListEntry> m_items;
    /// 与 `m_items` 一一对应的可搜索文本（小写：进程名 + 标题）。
    std::vector<QString> m_haystacks;
    /// 用户输入的筛选串（原样保留大小写）。
    QString m_filter;
    /// 当前筛选结果在 `m_items` 里的下标。
    std::vector<int> m_visible;
    int m_maxRows = 12;
    int m_rows = 1;
    int m_selected = 0;

    int m_cardHeight = 0;
    PopupRect m_titleRect;
    PopupRect m_countRect;
    PopupRect m_filterRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
