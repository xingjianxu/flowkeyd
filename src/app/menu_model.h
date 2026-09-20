// `menu` 动作弹出的选单的**纯逻辑**模型。
//
// 几何、高亮、单字符选中、命中测试、`Esc`/`Enter` 语义都在这里；只依赖 QtCore，
// 所以 `tst_menu_model` 能在没有桌面会话时覆盖它们。`qml/MenuPopup.qml` 只把
// 模型算出来的矩形画出来，`app/popup_host.cpp` 只做窗口与前台锁的杂活。
//
// 与 oskeyd 的 `../oskeyd/src/win/menu.rs` 一一对应（`Metrics` + `State`），
// 但拆掉了所有 Win32/GDI 部分。
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
/// 作为 `QAbstractListModel` 直接喂给 QML 的 `Repeater`，角色是
/// `label` / `hint` / `keyText` / `highlighted` / `hovered` /
/// `rowRect` / `badgeRect` / `labelRect` / `hintRect`（后四个是 `QRect`）。
class MenuModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString title READ title NOTIFY itemsChanged)
    Q_PROPERTY(bool hasTitle READ hasTitle NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    Q_PROPERTY(int cardWidth READ cardWidth NOTIFY itemsChanged)
    Q_PROPERTY(int cardHeight READ cardHeight NOTIFY itemsChanged)
    Q_PROPERTY(int cardRadius READ cardRadius CONSTANT)
    Q_PROPERTY(int highlight READ highlight NOTIFY highlightChanged)
    Q_PROPERTY(QRect titleRect READ titleRect NOTIFY itemsChanged)
    Q_PROPERTY(QRect footerRect READ footerRect NOTIFY itemsChanged)
    Q_PROPERTY(QString footerText READ footerText CONSTANT)

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        HintRole,
        KeyRole,
        HighlightedRole,
        HoveredRole,
        RowRole,
        BadgeRole,
        LabelRectRole,
        HintRectRole,
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
    int highlight() const { return m_highlight; }
    PopupRect titleRect() const { return m_titleRect; }
    PopupRect footerRect() const { return m_footerRect; }
    QString footerText() const;

    /// 第 `index` 个条目的矩形（绘制与命中测试用的是同一个，所以永远不会对不上）。
    PopupRect rowRect(int index) const;
    /// 条目里的按键徽标。
    PopupRect badgeRect(int index) const;
    /// 条目里标签/副标题的矩形。
    PopupRect labelRect(int index) const;
    PopupRect hintRect(int index) const;

    /// 客户区坐标下的命中测试；不在任何条目上时返回 -1。
    Q_INVOKABLE int hitTest(int x, int y) const;
    /// 当前高亮的条目（鼠标悬停优先，与 oskeyd 的 `State::active` 一致）；
    /// 空选单返回 -1。
    Q_INVOKABLE int accept() const;
    /// 移动高亮；到边界回绕（选单与帮助窗口不同，帮助窗口会夹住）。
    Q_INVOKABLE void moveHighlight(int delta);
    /// 鼠标悬停（-1 = 不在任何条目上）。
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
    /// 被当成字符键——这正是 oskeyd 里那段显式判断要解决的问题。
    Q_INVOKABLE QVariantMap handleKey(int key, const QString &text);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void itemsChanged();
    void highlightChanged();

private:
    /// 重算几何并（可选地）发信号。
    void relayout(bool itemsChanged);
    void refresh(bool itemsChanged);

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
