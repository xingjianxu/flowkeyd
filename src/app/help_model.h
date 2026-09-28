// `help` 动作弹出的快捷键帮助窗口的**纯逻辑**模型。
//
// 与 `menu_model` 一样只依赖 QtCore：筛选、`可见/总数` 计数、`Enter` 复制哪一行、
// 两级 `Esc` 与键盘选中行都在这里，`tst_help_model` 直接覆盖它们。
//
// 卡片外框的几何与键盘状态都收在这个类里。
//
// **有意偏离（2026-09，项目所有者拍板）：滚动不归模型管。**
// 列表是一个真正的 QML `ListView` + Qt 自带的 `ScrollBar`，滚轮、拖动滑块、
// 平滑滚动全部交给 Qt；模型只保留「键盘选中哪一行」以及卡片外框的几何。
// 旧实现自己算滑槽/滑块几何、自己命中测试、自己滚轮步进，结果是滑块拖不动、
// 滚轮下高亮闪（见 AGENTS.md 第 10 节）。因此这里**没有**
// `scroll`/`hitTest`/`wheel`：行下标就是 `ListView` 的下标，滚动位置由视图
// 自己持有，鼠标点击直接交给委托的 `TapHandler`。
//
// **2026-09 修订：鼠标悬停不再改变高亮。** 高亮就是键盘选中项，只有
// `moveSelection()` 会改它。悬停高亮是一种「自定义列表行为」：拖动滚动条时
// 指针压在列表上，高亮会随滚动在行之间乱跳 —— 项目所有者要求取消，改用
// 系统列表控件的语义（悬停不动高亮、拖动与滚轮只滚视图）。所以这里也**没有**
// `hover`/`setHover`。
//
// **2026-09 修订（第二个版本）：界面上的交互全部交给标准控件。**
// 筛选框是一个真正的 `TextField`：光标、选区、输入法、右键菜单、鼠标点选
// 全部由 Qt 负责，模型只在 `setFilter()` 里接收最终文本，因此 `handleKey()`
// **不再处理字符与退格**（那正是把自绘输入框换成标准控件的意义）。列表的委托
// 是一个真正的 `ItemDelegate`：鼠标点一行 = `setSelected()` 选中它 + `helpCopy()`
// 执行它，滚动位置由 `ListView` 自己持有，所以模型**也没有** `hitTest`/`wheel`。
// **2026-09 修订（第三个版本）：`Enter`/双击不再只是复制，而是执行那一行的动作。**
// 项目所有者要求「双击高亮选中的列表项或者直接回车，应该可以直接触发对应的
// action」。于是这里多了一层「危险动作要再确认一次」的状态机：
//   * `Enter`（或双击）落在 `quit`/`suspend`/`power` 这类危险行上时，第一次只是
//     把这一行**武装**（`armed`，界面上高亮成待确认色 + 底部提示换成确认文案），
//     再按一次才真的执行；`Esc`、挪动选中项、换筛选都取消武装；
//   * 其余行一次就执行（决策 `run`）；`remap` 行等价于按一下那个键。
// 是否危险由 `core::isDestructive()` 判定（`HelpEntry::destructive`），模型自己
// 不认识动作，仍然只用 QtCore。
//
// **单击仍然是「选中 + 复制」**（项目所有者拍板）：复制这个能力保留在鼠标上，
// 键盘的 `Enter` 才是执行。
//
// `handleKey()` 只剩导航键（`↑`/`↓`/`PgUp`/`PgDn`）、`Enter`（执行/武装）与
// `Esc`（取消武装 → 清筛选 → 关窗）；`Home`/`End` 归输入框（标准的光标移动）。
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
    /// 执行它之前要不要再确认一次（`quit`/`suspend`/`power`，见 `core::isDestructive`）。
    bool destructive = false;
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
    /// 正在等第二次确认的那一行（`-1` 表示没有）。
    Q_PROPERTY(int armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY stateChanged)
    Q_PROPERTY(QString footerText READ footerText NOTIFY stateChanged)
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
        /// 这一行是不是键盘选中的那一行（高亮）。
        ///
        /// **不叫 `highlighted`**：QML 里每一行是标准的 `ItemDelegate`，它自己
        /// 就有一个 `highlighted` 属性（标准样式用它画高亮），而委托里的
        /// `required property` 名字必须等于模型角色名 —— 撞名就声明不了。
        RowSelectedRole,
        /// 这一行正在等第二次 `Enter` 确认（危险动作的武装状态）。
        RowArmedRole,
    };
    Q_ENUM(Role)

    explicit HelpModel(QObject *parent = nullptr);

    /// 换一批条目（同一个窗口复用）。
    void setItems(std::optional<QString> title, std::vector<HelpEntry> items);
    /// 所在的显示器最多能显示多少行（`PopupHost` 按工作区算好传进来）。
    void setMaxRows(int rows);
    int maxRows() const { return m_maxRows; }

    /// 给定一块工作区的高度，最多能放几行。
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
    /// 它也是高亮的唯一来源：鼠标悬停与滚动都不碰它。
    int selected() const { return m_selected; }
    /// 正在等第二次确认的行下标；`-1` 表示没有。
    int armed() const { return m_armed; }
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

    /// 换筛选串。**QML 的筛选框直接调它**（`onTextChanged`），`handleKey` 与
    /// 窗口复位时也用它。
    Q_INVOKABLE void setFilter(const QString &filter);
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

    /// 上下移动选中项（高亮跟着它走）；到边界夹住（与选单的回绕不同）。
    Q_INVOKABLE void moveSelection(int delta);

    /// 鼠标点选某一行（`ItemDelegate.onClicked`）。越界时夹进 `[0, 可见条数-1]`。
    ///
    /// 与 `moveSelection` 分开是刻意的：点选是**绝对**位置，键盘导航是相对位移；
    /// 两者都只改「键盘选中项」这一个状态，高亮跟着它走。
    Q_INVOKABLE void setSelected(int line);

    /// 触发某一行的动作（`Enter` 与 `ItemDelegate.onDoubleClicked` 共用）。
    ///
    /// 先把这一行选中（双击时鼠标那一下已经选过了，这里是幂等的），再决定
    /// 「现在就能执行」还是「危险动作、先武装起来等第二次确认」：
    ///
    /// * 返回 `{ decision: "run", index }`：执行它（QML 交给 `host.helpRun`）；
    /// * 返回 `{ decision: "arm", index }`：只是武装（模型状态已经改了，
    ///   界面靠 `rowArmed` 角色与 `footerText` 自己表现）；
    /// * 一条可见条目都没有时返回 `{ decision: "none" }`。
    Q_INVOKABLE QVariantMap activateRow(int line);

    /// 一次按键的处理结果：
    /// `{ decision: "none"|"run"|"arm"|"disarm"|"copy"|"cancel"|"clear", index, handled }`。
    ///
    /// 只管导航键（`↑`/`↓`/`PgUp`/`PgDn`）、`Enter`（执行光标那一行；危险动作
    /// 先返回一次 `arm`）与 `Esc`（有武装就先取消武装，其次清筛选，最后才关窗）。
    /// **编辑键不在这里**：字符、退格、`Home`/`End`/左右方向键都归筛选框那个
    /// 标准 `TextField` 自己。没被接住的键返回 `handled == false`，QML 把它放行
    /// 给输入框。
    ///
    /// `clear` 表示模型已经把筛选清掉了，QML 要把输入框里的文本同步过来。
    /// `arm`/`disarm`/`none` 不需要 QML 做任何事（状态已经变了，信号会到）。
    Q_INVOKABLE QVariantMap handleKey(int key);

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
    /// 「等第二次确认」的那一行变了（包括被取消）。
    void armedChanged();

private:
    /// 重新算筛选结果、几何与计数（不发信号，调用方负责把 reset 包起来）。
    void refilter();
    void relayout();
    void notifyRows();
    /// 当前高亮那条的行下标（就是键盘选中项）；没有可见条目时返回 -1。
    int activeLine() const;
    /// 武装 / 取消武装（只改状态；要不要发信号由调用方决定）。
    void armLine(int line);
    /// 清掉武装状态；返回是否真的清掉了（调用方据此决定发不发信号）。
    bool disarm();

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
    /// 正在等第二次确认的行下标（`-1` = 没有）。
    int m_armed = -1;

    int m_cardHeight = 0;
    PopupRect m_titleRect;
    PopupRect m_countRect;
    PopupRect m_filterRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
