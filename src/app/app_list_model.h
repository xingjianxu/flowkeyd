// 程序启动器（`apps` 动作）的**纯逻辑**模型：一张「图标 + 名字」的网格。
//
// 与 `window_list_model` / `help_model` 一样只依赖 QtCore：几何、筛选、键盘
// 选中项、`Enter`/`Esc` 的语义都在这里，`tst_app_list_model` 直接覆盖它们。
// 开始菜单的扫描在 `platform/win/apps`（Win32），图标的取像素在
// `app::AppIconProvider`（QtGui + shell），模型只认「名字 + 图标 URL」两个字符串。
//
// **网格**：固定 6 列（`columns`，卡片 800 逻辑像素宽），每一格是「上面一个图标、
// 下面一到两行名字」。
// 选中项在模型里是**扁平下标**（也就是 `ListView` / `GridView` 的行号），
// `←`/`→` 走一格、`↑`/`↓` 走一整行；到边界夹住、不回绕（网格里回绕到上一行
// 的末尾在筛过之后很容易让人失去方向感）。
//
// **筛选**：按程序名做大小写无关的**子串**匹配（`core::appNameMatches` 同一条
// 判据，但这里是模型自己的实现，免得 `flowkeyd_models` 拖上 `flowkeyd_core`）。
// 输入一个字符列表就窄一圈，`Enter` 启动当前高亮那一条。
//
// **不自动启动**：筛选到只剩一条时也**不**自动执行（窗口切换器那边会自动激活，
// 因为那是「切」；这里是「启动一个新程序」，误启动的代价比多按一下 `Enter` 大）。
//
// **图标**：每一行的角色是一个 `image://flowkeyd-app/<key>` URL（由 `PopupHost`
// 那边的 dispatcher 算好），像素由异步的图片提供者按需去 shell 里取（每个图标
// 约 3 ms，不能同步做）。
#pragma once

#include "app/popup_layout.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QRect>

#include <optional>
#include <vector>

namespace flowkeyd::app {

/// 启动器里显示的一个程序（纯显示数据）。
struct AppListEntry
{
    /// 程序名（快捷方式的文件名去掉 `.lnk`）。
    QString name;
    /// 图标的 `image://` URL；空串表示这一行没有图标（画一个占位的方块）。
    QString iconSource;
};

/// 程序启动器的网格模型。
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
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY stateChanged)
    /// 网格一次画几列（固定 6）。
    Q_PROPERTY(int columns READ columns CONSTANT)
    Q_PROPERTY(int cellWidth READ cellWidth CONSTANT)
    Q_PROPERTY(int cellHeight READ cellHeight CONSTANT)
    Q_PROPERTY(int iconSize READ iconSize CONSTANT)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY stateChanged)
    /// 卡片当前要显示几行（`ceil(可见条数 / 列数)`，夹在 `1..maxRows`）。
    Q_PROPERTY(int visibleRows READ visibleRows NOTIFY stateChanged)
    /// 网格顶边 / 底边（与 `WindowListModel` 同一套算法）。
    Q_PROPERTY(int listTop READ listTop CONSTANT)
    Q_PROPERTY(int listBottom READ listBottom CONSTANT)
    /// 键盘选中项 = 可见行的扁平下标。
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)

public:
    enum Role {
        AppNameRole = Qt::UserRole + 1,
        /// 图标的 `image://` URL。
        AppIconRole,
        /// 这一行是不是当前高亮的那一行。
        ///
        /// **不叫 `highlighted`**：QML 里每一行是标准的 `ItemDelegate`，它自己
        /// 就有这个属性；委托里的 `required property` 名字必须等于角色名。
        RowSelectedRole,
    };
    Q_ENUM(Role)

    explicit AppListModel(QObject *parent = nullptr);

    /// 换一批程序（同一个窗口复用；`PopupHost` 再次弹出时调它）。
    void setItems(std::optional<QString> title, std::vector<AppListEntry> items);
    /// 所在的显示器最多能显示几行（`PopupHost` 按工作区算好传进来）。
    void setMaxRows(int rows);
    int maxRows() const { return m_maxRows; }

    /// 给定一块工作区的高度，最多能放几行（纯算术）。
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
    bool hasMatches() const { return !m_visible.empty(); }
    int totalCount() const { return static_cast<int>(m_items.size()); }
    int visibleCount() const { return static_cast<int>(m_visible.size()); }
    int columns() const;
    int cellWidth() const;
    int cellHeight() const;
    int iconSize() const;
    int visibleRows() const { return m_rows; }
    int listTop() const;
    int listBottom() const;
    int selected() const { return m_selected; }

    /// 可见行下标对应的原始条目下标。
    std::optional<int> itemIndexForVisible(int line) const;

    /// 换筛选串（QML 的筛选框直接调它）。
    ///
    /// 筛选之后选中项回到第一条（否则高亮会停在一个已经不存在的行号上）。
    ///
    /// 返回值与 `WindowListModel::setFilter()` 同形，但**永远是**
    /// `{ decision: "none" }`：启动器不做「筛到一个就自动启动」（那是「切窗口」
    /// 的语义，而这里会真的拉起一个新进程）。留这个形状是为了两个筛选框在 QML
    /// 里能用同一段 `applyDecision` 代码。
    Q_INVOKABLE QVariantMap setFilter(const QString &filter);
    Q_INVOKABLE void clearFilter();

    /// 上下左右移动选中项（到边界夹住，不回绕）。
    Q_INVOKABLE void moveSelection(int delta);
    /// 鼠标悬停到第 `line` 行（可见行下标）；越界或负数忽略。悬停即高亮。
    Q_INVOKABLE void setHover(int line);

    /// 激活第 `line` 行（可见行下标）：
    /// `{ decision: "choose", index: <条目下标>, handled: true }`。
    Q_INVOKABLE QVariantMap activate(int line);

    /// 一次按键的处理结果：
    /// `{ decision: "none"|"choose"|"cancel", index, handled }`。
    ///
    /// 只管方向键 / `Home` / `End` / `PgUp` / `PgDn` / `Enter` / `Esc`；
    /// **字符与退格不在这里**（它们归筛选框那个标准 `TextField`）。
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
    /// 当前高亮那条的可见行下标；没有可见条目时返回 -1。
    int activeLine() const;

    std::optional<QString> m_title;
    std::vector<AppListEntry> m_items;
    /// 与 `m_items` 一一对应的可匹配文本（小写名字，装载时算一次）。
    std::vector<QString> m_names;
    /// 用户输入的筛选串（原样保留大小写）。
    QString m_filter;
    /// 当前筛选结果在 `m_items` 里的下标。
    std::vector<int> m_visible;
    int m_maxRows = 6;
    int m_rows = 1;
    int m_selected = 0;

    int m_cardHeight = 0;
    PopupRect m_filterRect;
    PopupRect m_footerRect;
};

} // namespace flowkeyd::app
