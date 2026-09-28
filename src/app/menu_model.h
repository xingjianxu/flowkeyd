// `menu` 动作弹出的选单的**纯逻辑**模型。
//
// 卡片外框（宽高、标题、底部提示）与交互语义（高亮、单字符选中、`Esc`/`Enter`）
// 都在这里；只依赖 QtCore，所以 `tst_menu_model` 能在没有桌面会话时覆盖它们。
// `app::PopupHost` 只做窗口与前台锁的杂活。
//
// **列表的行几何不归模型管**（2026-09，与 `app::HelpModel` 同一条路线，见
// AGENTS.md 第 10 节）：`qml/MenuPopup.qml` 里的列表是一个真正的 `ListView`，
// 每一行是标准的 `ItemDelegate`。行下标就是 `ListView` 的下标，鼠标点击由委托的
// `clicked` 直接给出下标，鼠标悬停由委托自己的 `hovered` 属性驱动。所以这里
// **没有** `rowRect`/`badgeRect`/`labelRect`/`hintRect`/`hitTest`/`HoveredRole`
// —— 以前那套「自己算矩形 + 用一个铺满卡片的 `MouseArea` 自己命中测试」的写法
// 已经删掉了。模型只留几个常量（一行多高、行距、列表在卡片里的起点），
// 让 QML 把 `ListView` 摆到与卡片几何一致的位置。
#pragma once

#include "app/popup_layout.h"

#include <QAbstractListModel>
#include <QChar>
#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace flowkeyd::app {

/// 选单里的一个条目（纯显示数据，动作不在这里）。
struct MenuEntry
{
    /// 按下哪个字符就选中它（已经由 `core::MenuItemDef::keyChar()` 规范化成小写）。
    std::optional<QChar> key;
    /// 条目上显示的文本。
    QString label;
    /// 右侧的灰色副标题，例如英文名。
    std::optional<QString> hint;
};

/// `menu` 选单的模型。
///
/// 作为 `QAbstractListModel` 直接喂给 QML 的 `ListView`，角色是
/// `label` / `hint` / `keyText` / `rowSelected`。
class MenuModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString title READ title NOTIFY itemsChanged)
    Q_PROPERTY(bool hasTitle READ hasTitle NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    Q_PROPERTY(int cardWidth READ cardWidth NOTIFY itemsChanged)
    Q_PROPERTY(int cardHeight READ cardHeight NOTIFY itemsChanged)
    Q_PROPERTY(int cardRadius READ cardRadius CONSTANT)
    Q_PROPERTY(QRect titleRect READ titleRect NOTIFY itemsChanged)
    Q_PROPERTY(QRect footerRect READ footerRect NOTIFY itemsChanged)
    Q_PROPERTY(QString footerText READ footerText CONSTANT)
    /// 列表区顶部距卡片上边的距离（标题占掉的那一块；没写 `title` 时就是内边距）。
    Q_PROPERTY(int listTop READ listTop NOTIFY itemsChanged)
    /// 一行**自己**的高度（不含 `rowSpacing`）。`ListView` 的委托高度是它加上
    /// `rowSpacing`，列表总高是 `count * (rowHeight + rowSpacing)`。
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    /// 行与行之间的空隙（`ListView` 不用 `spacing`，免得第一行/最后一行与列表
    /// 边缘之间多出空隙）。
    Q_PROPERTY(int rowSpacing READ rowSpacing CONSTANT)
    /// 行内内容相对行边缘再内缩多少（在样式自己的内边距之外）。
    Q_PROPERTY(int rowInset READ rowInset CONSTANT)
    /// 按键徽标的边长（正方形，只有写了 `key` 的条目才有徽标）。
    Q_PROPERTY(int badgeSize READ badgeSize CONSTANT)
    /// 键盘高亮的下标（**不是**鼠标悬停；鼠标压在哪一行由 QML 的委托告诉模型）。
    Q_PROPERTY(int highlight READ highlight NOTIFY highlightChanged)

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        HintRole,
        KeyRole,
        /// 这一行是不是当前高亮的那一行（键盘选中项，或者鼠标正悬停的那一行）。
        ///
        /// **不叫 `highlighted`**：QML 里每一行是标准的 `ItemDelegate`，它自己
        /// 就有一个 `highlighted` 属性（标准样式用它画高亮），而委托里的
        /// `required property` 名字必须等于模型角色名 —— 撞名就声明不了。
        /// 同理这里也**没有** `hovered`：委托自己就有 `hovered`，模型不需要。
        RowSelectedRole,
    };
    Q_ENUM(Role)

    explicit MenuModel(QObject *parent = nullptr);

    /// 换一批条目（同一个窗口复用；`PopupHost` 再次弹出时调它）。
    void setItems(std::optional<QString> title, std::vector<MenuEntry> items);

    QString title() const { return m_title.value_or(QString()); }
    bool hasTitle() const { return m_title.has_value(); }
    int count() const { return static_cast<int>(m_items.size()); }
    int cardWidth() const { return m_cardWidth; }
    int cardHeight() const { return m_cardHeight; }
    int cardRadius() const;
    PopupRect titleRect() const { return m_titleRect; }
    PopupRect footerRect() const { return m_footerRect; }
    QString footerText() const;
    int listTop() const;
    int rowHeight() const;
    int rowSpacing() const;
    int rowInset() const;
    int badgeSize() const;
    int highlight() const { return m_highlight; }

    /// 当前生效的那一条（鼠标悬停优先，与 `Enter`/字符选中看到的是同一个，
    /// 也就是界面上高亮的那一行）；空选单返回 -1。
    Q_INVOKABLE int accept() const;
    /// 移动高亮；到边界回绕（选单与帮助窗口不同，帮助窗口会夹住）。
    Q_INVOKABLE void moveHighlight(int delta);
    /// 鼠标悬停（-1 = 不在任何条目上）；由 QML 的委托与卡片上的 `HoverHandler` 调。
    Q_INVOKABLE void setHover(int index);
    int hover() const { return m_hover; }
    /// 按字符选中（不区分大小写）；没有匹配返回 -1。
    Q_INVOKABLE int indexForChar(const QString &text) const;
    /// 再次打开时把高亮与悬停复位。
    void reset();

    /// 一次按键的处理结果：`{ decision: "none"|"choose"|"cancel", index, handled }`。
    ///
    /// `key` 是 `Qt::Key`，`text` 是 `QKeyEvent::text()`（Qt 已经按当前键盘布局
    /// 翻译过）。`VK_UNASSIGNED`(0xE8) 那条菜单遮断注入没有文本，因此不会
    /// 被当成字符键。
    Q_INVOKABLE QVariantMap handleKey(int key, const QString &text);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void itemsChanged();
    void highlightChanged();

private:
    /// 重算卡片外框（条目数或标题变了之后调）。
    void relayout();
    /// 通知视图：所有行的显示内容都可能变了。
    void refresh();

    std::optional<QString> m_title;
    std::vector<MenuEntry> m_items;
    int m_highlight = 0;
    int m_hover = -1;

    int m_cardWidth = 0;
    int m_cardHeight = 0;
    PopupRect m_titleRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
