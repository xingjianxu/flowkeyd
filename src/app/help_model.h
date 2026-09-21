// `help` 动作弹出的快捷键帮助窗口的**纯逻辑**模型。
//
// 与 `menu_model` 一样只依赖 QtCore：筛选、`可见/总数` 计数、`Enter` 复制哪一行、
// 两级 `Esc`、选中行与光标悬停行都在这里，`tst_help_model` 直接覆盖它们。
//
// 对应 oskeyd 的 `../oskeyd/src/win/help.rs` 里的 `Metrics` + `State`。
//
// **有意偏离（2026-09，项目所有者拍板）：滚动不归模型管。**
// 列表是一个真正的 QML `ListView` + Qt 自带的 `ScrollBar`，滚轮、拖动滑块、
// 平滑滚动全部交给 Qt；模型只保留「键盘选中哪一行 / 光标悬停哪一行」以及
// 卡片外框的几何。旧实现自己算滑槽/滑块几何、自己命中测试、自己滚轮步进，
// 结果是滑块拖不动、滚轮下高亮闪（见 AGENTS.md 第 10 节）。因此这里**没有**
// `scroll`/`hitTest`/`wheel`：行下标就是 `ListView` 的下标，命中交给
// `ListView.indexAt()`，滚动位置由视图自己持有。
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
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY stateChanged)
    Q_PROPERTY(QString footerText READ footerText CONSTANT)
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    /// 行与行之间的空隙；`ListView` 的 `spacing` 用它，行高本身不含它。
    Q_PROPERTY(int rowSpacing READ rowSpacing CONSTANT)
    /// 列表区顶部距卡片上边的距离（表头 + 筛选框占掉的那一块）。
    Q_PROPERTY(int listTop READ listTop CONSTANT)
    /// 列表区底部到卡片下边的距离（底部提示 + 内边距）。
    Q_PROPERTY(int listBottom READ listBottom CONSTANT)
    Q_PROPERTY(int keysWidth READ keysWidth CONSTANT)
    Q_PROPERTY(int badgeHeight READ badgeHeight CONSTANT)
    Q_PROPERTY(int badgePad READ badgePad CONSTANT)
    Q_PROPERTY(int badgeGap READ badgeGap CONSTANT)
    Q_PROPERTY(int rowInset READ rowInset CONSTANT)

public:
    enum Role {
        BadgesRole = Qt::UserRole + 1,
        LabelRole,
        DetailRole,
        HighlightedRole,
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
    /// 卡片一次最多能画出来的行数（`min(可见条数, maxRows)`，至少 1）。
    int visibleRows() const { return m_rows; }
    /// 键盘选中的行下标（**不是**滚动位置；滚动由 `ListView` 自己持有）。
    int selected() const { return m_selected; }
    /// 光标悬停的行下标（-1 = 光标不在任何行上）。
    std::optional<int> hover() const;
    QString footerText() const;
    int rowHeight() const;
    int rowSpacing() const;
    int listTop() const;
    int listBottom() const;
    int keysWidth() const;
    int badgeHeight() const;
    int badgePad() const;
    int badgeGap() const;
    int rowInset() const;

    /// 可见行下标对应的原始条目下标。
    const std::vector<int> &visibleIndices() const { return m_visible; }
    std::optional<int> itemIndexForVisible(int line) const;

    /// 换筛选串（`handleKey` 会自己调它；窗口复位时也用它）。
    void setFilter(const QString &filter);
    Q_INVOKABLE void clearFilter();

    /// 已生效的那一行的按键文本（`Ctrl+A / Ctrl+B`）；没有选中时返回空串。
    Q_INVOKABLE QString copyText() const;

    /// 一行条目的按键文本（`copyText` 对任意可见行都用它）。
    QString copyTextForVisible(int line) const;

    /// 按键徽标序列：每个和弦的每一段一个牌子，和弦之间插一个小圆点。
    ///
    /// 元素是 `{ text, badge }`（`badge == false` 表示分隔点）。牌子的**宽度**
    /// 由 QML 按文字量出来（`Text.implicitWidth`），所以这里只给内容。
    QVariantList badgesForVisible(int line) const;
    QVariantList badgesForItem(std::size_t itemIndex) const;

    /// 鼠标悬停的行下标（-1 = 不在任何行上）。QML 用 `ListView.indexAt()` 算出来。
    Q_INVOKABLE void setHover(int line);
    /// 上下移动选中项；到边界夹住（与选单的回绕不同，与 oskeyd 一致）。
    Q_INVOKABLE void moveSelection(int delta);

    /// 键盘改过选中项之后，视图的 `contentY` 应该放在哪里。
    ///
    /// 为什么不由 `ListView.positionViewAtIndex(line, Contain)` 自己搞定：列表
    /// 铺满整张卡片，视口上下各有表头/底部提示盖着，而 Qt 的 `Contain` 只保证
    /// 「行落在**列表自己的矩形**里」——对最后几行它会把行留在底部提示底下
    /// （实测 13 条时只挪 4 像素，行基本看不见）。所以这里按**行区域**
    /// （`topMargin` 到 `viewportHeight - bottomMargin`）算一个目标值，滚动本身
    /// 仍然完全交给 `ListView`（滚轮、拖滑块、惯性都不经过这里）。
    ///
    /// 参数是视图的几何；纯算术，`tst_help_model` 直接盯着它。
    Q_INVOKABLE int scrollTargetY(int line,
                                  int contentY,
                                  int topMargin,
                                  int bottomMargin,
                                  int viewportHeight) const;

    /// 一次按键的处理结果：`{ decision: "none"|"copy"|"cancel", index, handled }`。
    Q_INVOKABLE QVariantMap handleKey(int key, const QString &text);

    /// 再次打开时清空筛选、高亮。
    void reset();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void itemsChanged();
    void stateChanged();
    /// 选中行变了（或者列表被换过/筛过，视图该把选中项带回视野）。
    void selectedChanged();

private:
    /// `moveSelection` 的实体：`clearHover` 为假时保留鼠标悬停。
    void moveSelection(int delta, bool clearHover);
    /// 重新算筛选结果、几何与计数（不发信号，调用方负责把 reset 包起来）。
    void refilter();
    void relayout();
    void notifyRows();
    /// 当前高亮那条的行下标（鼠标悬停优先）；没有可见条目时返回 -1。
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
    int m_hover = -1;

    int m_cardHeight = 0;
    PopupRect m_titleRect;
    PopupRect m_countRect;
    PopupRect m_filterRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
