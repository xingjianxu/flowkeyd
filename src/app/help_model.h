// `help` 动作弹出的快捷键帮助窗口的**纯逻辑**模型。
//
// 与 `menu_model` 一样只依赖 QtCore：筛选、滚动钳位、`可见/总数` 计数、
// `Enter` 复制哪一行、两级 `Esc` 都在这里，`tst_help_model` 直接覆盖它们。
//
// 对应 oskeyd 的 `../oskeyd/src/win/help.rs` 里的 `Metrics` + `State`。
// 唯一有意偏离的是**按键徽标的宽度**：oskeyd 用 `GetTextExtentPoint32W` 量文字
// 再拼出每个小牌子，而 QML 里 `Text.implicitWidth` 就是这件事的正确答案，
// 所以模型只给出「按键列」的矩形与徽标的内边距，牌子的宽度留给 QML。
#pragma once

#include "app/popup_layout.h"

#include <QAbstractListModel>
#include <QChar>
#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace flowkeyd::app {

/// 帮助窗口里的一条：一串和弦 + 一句人话 + 一句动作摘要。
struct HelpEntry
{
    /// 和弦，例如 `"Ctrl+Alt+F12"`；多个和弦时逐个显示。
    QStringList chords;
    /// 主要文本（配置里的 `comment`，没有就用 `name`）。
    QString label;
    /// 次要文本（动作摘要）。
    std::optional<QString> detail;
};

/// `help` 帮助窗口的模型。
class HelpModel : public QAbstractListModel
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
    Q_PROPERTY(bool hasMatches READ hasMatches NOTIFY stateChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY itemsChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY stateChanged)
    Q_PROPERTY(int visibleRows READ visibleRows NOTIFY stateChanged)
    Q_PROPERTY(int selected READ selected NOTIFY stateChanged)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY stateChanged)
    Q_PROPERTY(bool hasScrollbar READ hasScrollbar NOTIFY stateChanged)
    Q_PROPERTY(QRect scrollTrack READ scrollTrack NOTIFY stateChanged)
    Q_PROPERTY(QRect scrollThumb READ scrollThumb NOTIFY stateChanged)
    Q_PROPERTY(QString footerText READ footerText CONSTANT)
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    Q_PROPERTY(int keysWidth READ keysWidth CONSTANT)
    Q_PROPERTY(int badgeHeight READ badgeHeight CONSTANT)
    Q_PROPERTY(int badgePad READ badgePad CONSTANT)
    Q_PROPERTY(int badgeGap READ badgeGap CONSTANT)
    Q_PROPERTY(int rowInset READ rowInset CONSTANT)
    Q_PROPERTY(int listTop READ listTop NOTIFY stateChanged)

public:
    enum Role {
        BadgesRole = Qt::UserRole + 1,
        LabelRole,
        DetailRole,
        HighlightedRole,
        HoveredRole,
        LineRole,
        RowRole,
        KeysRole,
        TextRectRole,
    };
    Q_ENUM(Role)

    explicit HelpModel(QObject *parent = nullptr);

    /// 换一批条目（同一个窗口复用）。
    void setItems(std::optional<QString> title, std::vector<HelpEntry> items);
    /// 所在的显示器最多能显示多少行（`PopupHost` 按工作区算好传进来）。
    void setMaxRows(int rows);
    int maxRows() const { return m_maxRows; }

    /// 给定一块工作区的高度，最多能放几行（与 oskeyd 的 `max_rows_for` 同源）。
    /// 纯算术，所以单测直接盯着它。
    static int rowsForAvailableHeight(int availableHeight);

    QString title() const { return m_title.value_or(tr("快捷键")); }
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
    bool hasMatches() const { return !m_visible.empty(); }
    int totalCount() const { return static_cast<int>(m_items.size()); }
    int visibleCount() const { return static_cast<int>(m_visible.size()); }
    /// 当前画出来的行数（`min(可见条数, maxRows)`，至少 1）。
    int visibleRows() const { return m_rows; }
    /// 键盘选中的**可见行**下标。
    int selected() const { return m_selected; }
    /// 列表顶部显示的第一条可见行下标。
    int scroll() const { return m_scroll; }
    std::optional<int> hover() const;
    bool hasScrollbar() const;
    PopupRect scrollTrack() const;
    PopupRect scrollThumb() const;
    QString footerText() const;
    int rowHeight() const;
    int keysWidth() const;
    int badgeHeight() const;
    int badgePad() const;
    int badgeGap() const;
    int rowInset() const;
    int listTop() const;

    /// 可见行下标对应的原始条目下标。
    const std::vector<int> &visibleIndices() const { return m_visible; }
    std::optional<int> itemIndexForVisible(int line) const;

    /// 绘制/命中用的矩形（参数是**可见行**下标，不是原始条目下标）。
    PopupRect rowRect(int line) const;
    PopupRect keysRect(int line) const;
    PopupRect textRect(int line) const;

    /// 换筛选串（`handleKey` 会自己调它；窗口复位时也用它）。
    void setFilter(const QString &filter);
    Q_INVOKABLE void clearFilter();

    /// 已生效的那一条的按键文本（`Ctrl+A / Ctrl+B`）；没有选中时返回空串。
    Q_INVOKABLE QString copyText() const;

    /// 一行条目的按键文本（`copyText` 对任意可见行都用它）。
    QString copyTextForVisible(int line) const;

    /// 按键徽标序列：每个和弦的每一段一个牌子，和弦之间插一个小圆点。
    ///
    /// 元素是 `{ text, badge }`（`badge == false` 表示分隔点）。牌子的**宽度**
    /// 由 QML 按文字量出来（`Text.implicitWidth`），所以这里只给内容。
    QVariantList badgesForVisible(int line) const;
    QVariantList badgesForItem(std::size_t itemIndex) const;

    /// 客户区坐标下的命中测试（返回**可见行**/条目下标）；不在任何行上返回 -1。
    ///
    /// 返回的是**可见下标**（`scroll + 画出来的行号`），所以它可以直接喂给
    /// `setHover` / `copyTextForVisible`；`rowRect` 要的则是画出来的行号。
    Q_INVOKABLE int hitTest(int x, int y) const;
    /// 鼠标悬停的可见行（-1 = 不在任何行上）。
    Q_INVOKABLE void setHover(int line);
    /// 上下移动选中项；到边界夹住（与选单的回绕不同，与 oskeyd 一致）。
    Q_INVOKABLE void moveSelection(int delta);
    /// 鼠标滚轮：一格（±120）跳过三行，和系统的列表控件一致。
    ///
    /// 与键盘不同的是**不清掉鼠标悬停**：滚轮只是“把列表推上去”，高亮应该
    /// 留在光标那一行。清掉悬停会让高亮先跳到选中项、再被紧随其后的鼠标
    /// 微抖拉回来——两帧之间就是肉眼看到的闪烁（见 AGENTS.md 第 10 节）。
    Q_INVOKABLE void wheel(int angleDeltaY);

    /// 点击某一行：选中它并返回它的可见下标（-1 表示点到了空白处，什么也不做）。
    Q_INVOKABLE int clickRow(int x, int y);

    /// 一次按键的处理结果：`{ decision: "none"|"copy"|"cancel", index, handled }`。
    Q_INVOKABLE QVariantMap handleKey(int key, const QString &text);

    /// 再次打开时清空筛选、高亮与滚动。
    void reset();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void itemsChanged();
    void stateChanged();

private:
    /// `moveSelection` 的实体：`clearHover` 为假时保留鼠标悬停（滚轮用）。
    void moveSelection(int delta, bool clearHover);
    /// 重新算筛选结果、几何与计数，并把视图刷新一次。
    void refilter();
    void relayout();
    void ensureVisible();
    void notifyRows();
    /// 当前高亮那条的**可见行**下标（鼠标悬停优先）；没有可见条目时返回 -1。
    int activeLine() const;

    std::optional<QString> m_title;
    std::vector<HelpEntry> m_items;
    /// 与 `m_items` 一一对应的可搜索文本（小写）。
    std::vector<QString> m_haystacks;
    /// 用户输入的筛选串（原样保留大小写）。
    QString m_filter;
    /// 当前筛选结果在 `m_items` 里的下标。
    std::vector<int> m_visible;
    int m_maxRows = 12;
    int m_rows = 1;
    int m_selected = 0;
    int m_scroll = 0;
    int m_hover = -1;

    int m_cardHeight = 0;
    PopupRect m_titleRect;
    PopupRect m_countRect;
    PopupRect m_filterRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
