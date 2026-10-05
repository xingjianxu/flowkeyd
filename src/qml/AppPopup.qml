pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `apps` 动作弹出的**程序启动器**。
//
// 卡片里从上到下是三段（`app::AppListModel` 把这些算成"行"，QML 只按 `rowKind`
// 画出来）：
//   1. **已固定** —— 用户按过 `Space` 的那些程序（网格）；
//   2. **最近使用** —— 真的从启动器里启动过的程序，最多两行（网格）；
//   3. **全部程序（N）** —— 一个按钮，点开就在同一张卡片里换成「全部程序」列表
//      （像 Windows 10 开始菜单的「所有应用」：按名字 / 拼音首字母分组、一行一个、
//      带滚动条），`Esc` 或「← 返回」回到上面那三段。
//
// 还什么都没固定、也没启动过任何程序时（第一次用），概览直接就是**全部程序**的
// 网格 —— 比让用户对着一个空卡片去点按钮友好。
//
// 除了卡片外框（无边框圆角窗口总得有一块底）之外全是标准控件：筛选框是真正的
// `TextField`，列表是标准 `ListView` + 标准 `ItemDelegate` + 标准 `ScrollBar`。
//
// **右键一个程序** = 那个程序的**原生 Windows 右键菜单**（与开始菜单 / 资源管理器
// 逐条一致，由 `platform/win/shell_menu` 弹出并执行）：选中条目就收卡片，
// 取消（`Esc` / 点菜单外面）就留着。**不切输入法**：这里的名字可能是中文
// （「记事本」），切成英文反而筛不出东西。
//
// 逻辑全在 `app::AppListModel`（纯逻辑、有单测）：分段、分组、拼音筛选、键盘
// 选中项、`Space` 固定、`Enter`/`Esc`。QML 只做两件事：
//   * 把 `handleKey()` / `activateItem()` 给的决定执行掉；
//   * 把模型的状态（筛选文本、选中行、卡片高度）同步给控件。
//
// **图标**：`icon` 是一个 `image://flowkeyd-app/…` URL，像素由
// `app::AppIconProvider` 在一条后台 STA 线程上按需去 shell 里取（每个约 3 ms），
// 所以图标会一格格「长出来」。`sourceSize` 用**设备像素**给（逻辑边长 × 缩放），
// 这样在 200% 缩放下取的就是 80 像素的图标，而不是把 40 像素的放大糊掉。
Window {
    id: root

    // 无边框 + 始终置顶 + **不进任务栏**（`Qt.Tool` = `WS_EX_TOOLWINDOW`）；
    // `color` 必须是 transparent（见 AGENTS.md 第 10 节）。
    flags: Qt.Window | Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    // 标题里带着可见条数（无边框窗口，这行字用户看不到），外面想断言「筛选生效
    // 了」（或者「固定之后条数变了」）时能从窗口标题读出来。
    title: root.appModel ? root.appModel.caption : qsTr("flowkeyd 程序")

    property var appModel: null
    property var host: null

    // 中文一律用微软雅黑（与其它几个弹窗一致；理由见 AGENTS.md 第 10 节）。
    readonly property string uiFontFamily: "Microsoft YaHei"

    readonly property int cellWidth: root.appModel ? root.appModel.cellWidth : 126
    readonly property int cellHeight: root.appModel ? root.appModel.cellHeight : 88
    readonly property int iconSize: root.appModel ? root.appModel.iconSize : 40
    readonly property int tileTextWidth: root.cellWidth - 24
    /// 取图标时用的边长（设备像素）：逻辑边长 × 本窗口那块屏的缩放。
    ///
    /// 用 `Window.devicePixelRatio`（`QWindow` 的属性，从 `QtQuick` 就能拿到）
    /// 而不是 `Screen.devicePixelRatio`：后者要额外 `import QtQuick.Window`，
    /// 那会给发布包多拖一个 QML 模块进去。
    readonly property int iconPixels: Math.max(8, Math.round(root.iconSize * root.devicePixelRatio))
    /// 「全部程序」列表里的小图标（一行一个字，图标小一点）。
    readonly property int listIconSize: 22
    readonly property int listIconPixels:
        Math.max(8, Math.round(root.listIconSize * root.devicePixelRatio))

    // 卡片尺寸来自模型：宽度恒定 800，高度也恒定（`maxRows` 行网格 + 上下占位，
    // 本机 622）—— 三个视图（概览 / 筛选 / 「全部程序」列表）一样高。
    width: root.appModel ? root.appModel.cardWidth : 800
    height: root.appModel ? root.appModel.cardHeight : 622

    // 与选单 / 帮助同一个理由：刚显示出来的 300 ms 内失去焦点不算「用户点了别处」。
    property double armedAt: 0
    onActiveChanged: {
        if (active) {
            root.armedAt = Date.now()
            // 窗口拿到前台就说明用户想用了：把光标放进筛选框，直接就能打字。
            if (filterField)
                filterField.forceActiveFocus()
        } else if (root.armedAt > 0 && Date.now() - root.armedAt > 300) {
            if (root.host)
                root.host.appDismiss()
        }
    }

    // 卡片高度是恒定的，但 `m_maxRows` 会随**屏幕高度**变（弹到另一块屏上时
    // `showApps()` 会重算）：换过高度就把它夹回屏幕里。`appRelayout()` 只夹
    // 位置、**不重新居中** —— 卡片是“以筛选框那一行为锚”的，位置在弹出时已经
    // 按光标那块屏算过了。
    onHeightChanged: {
        if (root.visible && root.host)
            root.host.appRelayout()
    }

    /// 把输入框的文本同步成模型里的筛选串（打等值判断就不会跟正在打字的用户打架）。
    function syncFilterField() {
        if (!filterField)
            return
        var wanted = root.appModel ? root.appModel.filter : ""
        if (filterField.text !== wanted)
            filterField.text = wanted
    }

    /// 把选中的行带进视野。滚动本身完全交给 `ListView`。
    function followSelection(line) {
        if (!list || line < 0)
            return
        list.positionViewAtIndex(line, ListView.Contain)
    }

    /// 执行模型给的决定（`setFilter()` / `handleKey()` / `activateItem()` 共用）。
    function applyDecision(decision) {
        if (!decision || !root.host)
            return
        if (decision.decision === "cancel")
            root.host.appDismiss()
        else if (decision.decision === "choose")
            root.host.appChoose(decision.index)
    }

    /// 启动一个程序（单击一格 / 双击一行）。
    function activateTile(itemIndex) {
        if (!root.appModel)
            return
        root.applyDecision(root.appModel.activateItem(itemIndex))
    }

    /// 右键一个程序：弹那个程序的**原生 shell 菜单**（与开始菜单 / 资源管理器一致，
    /// 见 `platform/win/shell_menu.h`）。
    ///
    /// `host.appContextMenu()` 是**阻塞**的（里面 `TrackPopupMenuEx` 一直等到用户
    /// 选完），返回「用户到底选没选」。规则交给模型：选了就收卡片，取消则留着。
    function contextMenuTile(itemIndex) {
        if (!root.appModel || !root.host)
            return
        var invoked = root.host.appContextMenu(itemIndex)
        root.applyDecision(root.appModel.afterContextMenu(invoked))
    }

    /// 按键决定由模型给，这里只执行。
    function handleKeyEvent(event) {
        if (!root.appModel || !root.host) {
            event.accepted = false
            return
        }
        var decision = root.appModel.handleKey(event.key)
        if (!decision.handled) {
            event.accepted = false
            return
        }
        event.accepted = true
        root.applyDecision(decision)
    }

    /// 点「全部程序（N）」按钮。
    function openAll() {
        if (!root.appModel)
            return
        root.appModel.showAll()
        // 视图换掉之后焦点要回到筛选框（点按钮会把焦点带走）。
        if (filterField)
            filterField.forceActiveFocus()
    }

    /// 点「← 返回」按钮，回到概览。
    function backToOverview() {
        if (!root.appModel)
            return
        root.appModel.showOverview()
        if (filterField)
            filterField.forceActiveFocus()
    }

    /// 那个整行按钮被点了：现在是概览就是「打开全部」，是列表就是「返回」。
    function activateButtonRow() {
        if (!root.appModel)
            return
        if (root.appModel.allMode)
            root.backToOverview()
        else
            root.openAll()
    }

    Rectangle {
        id: card

        anchors.fill: parent
        radius: root.appModel ? root.appModel.cardRadius : 12
        color: root.palette.base
        border.width: 1
        border.color: root.palette.mid
        clip: true
        focus: true

        // 筛选框不接的键（比如焦点落在滚动条上时）会冒到这里。
        Keys.onPressed: function (event) {
            root.handleKeyEvent(event)
        }

        Connections {
            target: root.appModel

            function onItemsChanged() {
                root.syncFilterField()
            }

            function onSelectedChanged() {
                root.followSelection(root.appModel ? root.appModel.selectedRow : -1)
            }

            function onStateChanged() {
                root.syncFilterField()
            }
        }

        // 筛选框：标准 `TextField`，直接摆在模型给的矩形里（卡片没有标题行）。
        TextField {
            id: filterField

            z: 2
            x: root.appModel ? root.appModel.filterRect.x : 0
            y: root.appModel ? root.appModel.filterRect.y : 0
            width: root.appModel ? root.appModel.filterRect.width : 0
            height: root.appModel ? root.appModel.filterRect.height : 30
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            placeholderText: root.appModel ? root.appModel.filterPlaceholder : ""
            selectByMouse: true
            focus: true

            // 文本一变就回给模型（模型会把选中项复位到第一行）。
            onTextChanged: {
                if (root.appModel && filterField.text !== root.appModel.filter)
                    root.applyDecision(root.appModel.setFilter(filterField.text))
            }

            Keys.onPressed: function (event) {
                // 输入法正在组字：方向键与回车属于候选窗，别抢。
                if (filterField.preeditText.length > 0)
                    return
                root.handleKeyEvent(event)
            }
        }

        // 行列表：标准 `ListView` + 自带滚动条。每一行的**形状**由 `rowKind`
        // 决定，四种形状分别对应下面四个内联组件（表头 / 网格 / 按钮 / 列表行）。
        ListView {
            id: list

            x: root.appModel ? root.appModel.filterRect.x : 0
            y: root.appModel ? root.appModel.listTop : 0
            width: root.appModel ? root.appModel.filterRect.width : 0
            height: Math.max(parent.height - y - (root.appModel ? root.appModel.listBottom : 0), 0)
            clip: true
            focus: false
            model: root.appModel
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            // 列表的位置只需要在两件事上动：视图整个换掉时（Qt 会把 `contentY`
            // 留在旧位置，模型 reset / 高度变化之后由 `onHeightChanged` 与
            // `onSelectedChanged` 把它摆回选中行 = 顶部），以及键盘选中项变化时
            // 把它带进视野。
            //
            // **不要挂 `onContentHeightChanged`**：`ListView` 对**还没创建出来的**
            // 委托是用「已见过的高度的平均值」估高的，滚动时创建的委托一直在换，
            // 这个估算就会抖几个像素（实测 123 行时 8597 → 8602 → 8406 → …），
            // 于是 `contentHeightChanged` 在滚动中不停发；跟着它
            // `positionViewAtIndex(选中行, Contain)` 就会把滚轮 / 拖滑块刚滚出来的
            // 位置立刻拽回选中行 —— 现象是「有滚动条，但滚不动」（2026-10-05 修）。
            // 行高全都一样时不会抖（帮助窗口就是），所以这个坑只在“一行一种高度”
            // 的列表里出现；`tmp/preview/` 与 `scripts/acceptance.ps1` 各有一条检查
            // 盯着它。
            onHeightChanged: root.followSelection(root.appModel ? root.appModel.selectedRow : -1)

            delegate: Item {
                id: rowItem

                required property int index
                required property string rowKind
                required property string rowTitle
                required property var rowItems
                required property bool rowSelected
                required property int rowSelectedColumn
                required property int rowHeight

                readonly property bool isHeader: rowItem.rowKind === "header"
                readonly property bool isGrid: rowItem.rowKind === "grid"
                readonly property bool isButton: rowItem.rowKind === "button"
                readonly property bool isList: rowItem.rowKind === "list"

                width: list.width
                height: rowItem.rowHeight

                // 拖动 / 悬停不改选中项：只有点了才算。
                HeaderRow {
                    visible: rowItem.isHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    label: rowItem.rowTitle
                }

                ButtonRow {
                    visible: rowItem.isButton
                    anchors.fill: parent
                    label: rowItem.rowTitle
                    highlighted: rowItem.rowSelected
                    onActivated: root.activateButtonRow()
                }

                GridRow {
                    visible: rowItem.isGrid
                    anchors.left: parent.left
                    height: parent.height
                    entries: rowItem.rowItems
                    selectedColumn: rowItem.rowSelectedColumn
                }

                ListRow {
                    visible: rowItem.isList
                    anchors.fill: parent
                    entry: rowItem.rowItems.length > 0 ? rowItem.rowItems[0] : null
                    highlighted: rowItem.rowSelected
                    onActivated: root.activateTile(rowItem.rowItems.length > 0
                                                   ? rowItem.rowItems[0].index : -1)
                    onContextMenuRequested: root.contextMenuTile(rowItem.rowItems.length > 0
                                                                ? rowItem.rowItems[0].index : -1)
                }
            }
        }

        // 一行可选中的都没有时给一句人话。
        Label {
            z: 1
            visible: root.appModel ? !root.appModel.hasMatches : false
            x: root.appModel ? root.appModel.filterRect.x : 0
            y: root.appModel ? root.appModel.listTop : 0
            width: root.appModel ? root.appModel.filterRect.width : 0
            height: root.appModel ? root.appModel.cellHeight : 88
            text: root.appModel ? root.appModel.emptyMessage : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            z: 1
            x: root.appModel ? root.appModel.footerRect.x : 0
            y: root.appModel ? root.appModel.footerRect.y : 0
            width: root.appModel ? root.appModel.footerRect.width : 0
            height: root.appModel ? root.appModel.footerRect.height : 0
            text: root.appModel ? root.appModel.footerText : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    // -----------------------------------------------------------------------
    // 四种「行」。
    //
    // 每种都是一个内联组件，由上面那个委托直接实例化（**不是** `Loader`）：
    // `ListView` 只为看得见的十来行建委托，所以一行同时建出这四样也不贵，
    // 换来的是每种行都能写自己那点属性、不用绕 `loader.item` 传数据。
    //
    // 不能叫 `text` / `highlighted` 之类的名字：这些基类（`Label` /
    // `ItemDelegate`）自己就有，重名会被 QML 拒绝。
    // -----------------------------------------------------------------------

    /// 分段表头（「已固定」「最近使用」、`A`–`Z` / `#`）。
    component HeaderRow: Label {
        required property string label

        text: label
        color: root.palette.placeholderText
        font.family: root.uiFontFamily
        font.pointSize: 9
        font.bold: true
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    /// 整行按钮（「全部程序（N）」/「← 返回」）。
    component ButtonRow: ItemDelegate {
        id: buttonRow

        required property string label

        signal activated()

        text: label
        focusPolicy: Qt.NoFocus
        font.family: root.uiFontFamily
        font.pointSize: 10.5
        onClicked: buttonRow.activated()
    }

    /// 一行网格（最多 6 个「图标 + 名字」的格子）。
    component GridRow: Row {
        id: gridRow

        required property var entries
        required property int selectedColumn

        Repeater {
            model: gridRow.entries

            delegate: ItemDelegate {
                id: tile

                required property var modelData
                required property int index

                width: root.cellWidth
                height: root.cellHeight
                // 委托不自取焦点：焦点始终留在筛选框里。
                focusPolicy: Qt.NoFocus
                highlighted: gridRow.selectedColumn === tile.index

                // 悬停即高亮（与 `menu` / 窗口切换器一致）。
                onHoveredChanged: {
                    if (tile.hovered && root.appModel)
                        root.appModel.hoverItem(tile.modelData.index)
                }

                // 左键点一格 = 启动它。
                onClicked: root.activateTile(tile.modelData.index)

                // 右键点一格 = 那个程序的原生 shell 菜单（打开 / 以管理员身份运行 /
                // 打开文件位置 / 属性……）。用 `TapHandler` 而不是再盖一个 `MouseArea`：
                // 只吃右键（左边的单击仍然是 `ItemDelegate` 自己的），
                // 而且不会把悬停高亮挡掉。`tapped` 在**松开**时发，所以弹菜单之前
                // 那次右键的 key-up 已经走完了。
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: root.contextMenuTile(tile.modelData.index)
                }

                contentItem: Item {
                    Image {
                        id: tileIcon

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        width: root.iconSize
                        height: root.iconSize
                        source: tile.modelData.icon
                        // 设备像素：200% 缩放下要的是 80 像素的图标。
                        sourceSize: Qt.size(root.iconPixels, root.iconPixels)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        asynchronous: true
                    }

                    // 数字快速启动键（easymotion 风格）：筛选之后前 10 条各拿一个
                    // 号码（第 1 个是 0），画在**图标右上角**，按一下直接启动它。
                    Rectangle {
                        id: keyBadge

                        readonly property bool shown: tile.modelData.key !== undefined
                                                      && tile.modelData.key.length > 0

                        visible: keyBadge.shown
                        width: 16
                        height: 16
                        radius: 4
                        color: root.palette.highlight
                        border.width: 1
                        border.color: root.palette.base
                        anchors.right: tileIcon.right
                        anchors.top: tileIcon.top

                        Label {
                            anchors.centerIn: parent
                            text: tile.modelData.key
                            color: root.palette.highlightedText
                            font.family: root.uiFontFamily
                            font.pointSize: 8.5
                            font.bold: true
                        }
                    }

                    // 已经固定住的：图标角上一个小圆点。有快速启动号码时让到
                    // **左上角**去（两个都锚在右上角会叠在一起）。用 x/y 而不是
                    // 条件锚点：后者的 `anchors.left/right` 互相切换很容易写错。
                    Rectangle {
                        visible: tile.modelData.pinned === true
                        width: 6
                        height: 6
                        radius: 3
                        color: root.palette.highlight
                        x: keyBadge.shown ? tileIcon.x
                                          : tileIcon.x + tileIcon.width - width
                        y: tileIcon.y
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: tileIcon.bottom
                        anchors.topMargin: 4
                        width: root.tileTextWidth
                        text: tile.modelData.name
                        color: root.palette.text
                        font.family: root.uiFontFamily
                        font.pointSize: 8.5
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignTop
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    /// 「全部程序」列表里的一行：小图标 + 名字 +（固定住的话）一个小圆点。
    component ListRow: ItemDelegate {
        id: listRow

        required property var entry

        signal activated()
        signal contextMenuRequested()

        focusPolicy: Qt.NoFocus
        onClicked: listRow.activated()

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: listRow.contextMenuRequested()
        }

        contentItem: Item {
            Image {
                id: rowIcon

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: root.listIconSize
                height: root.listIconSize
                source: listRow.entry ? listRow.entry.icon : ""
                sourceSize: Qt.size(root.listIconPixels, root.listIconPixels)
                fillMode: Image.PreserveAspectFit
                smooth: true
                asynchronous: true
            }

            Rectangle {
                id: rowPin

                visible: listRow.entry !== null && listRow.entry.pinned === true
                width: 6
                height: 6
                radius: 3
                color: root.palette.highlight
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }

            Label {
                anchors.left: rowIcon.right
                anchors.leftMargin: 10
                anchors.right: rowPin.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: listRow.entry ? listRow.entry.name : ""
                color: root.palette.text
                font.family: root.uiFontFamily
                font.pointSize: 10.5
                elide: Text.ElideRight
            }
        }
    }
}
