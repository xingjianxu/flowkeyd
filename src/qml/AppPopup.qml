pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `apps` 动作弹出的**程序启动器**：一张「图标 + 名字」的网格，列出开始菜单里的
// 程序（只列程序，见 `platform/win/apps`），输入就按名字筛。
//
// 与 `SwitchPopup.qml` / `HelpPopup.qml` 同一套骨架：除了卡片外框（无边框圆角
// 窗口总得有一块底）之外全是标准控件 —— 筛选框是真正的 `TextField`，网格是
// 标准 `GridView` + 标准 `ItemDelegate`，滚动由 Qt 自带的 `ScrollBar` 负责。
//
// **右键一格** = 那个程序的**原生 shell 菜单**（与开始菜单 / 资源管理器逐条一致，
// 由 `platform/win/shell_menu` 弹出并执行）：选中条目就收卡片，取消就留着。
//
// 逻辑全在 `app::AppListModel`（纯逻辑、有单测）：筛选（名字子串）、网格几何、
// 键盘选中项、`Enter`/`Esc`，以及「到边界夹住」。QML 只做两件事：
//   * 把 `handleKey()` / `activate()` 给的决定执行掉；
//   * 把模型的状态（筛选文本、选中行、格宽）同步给控件。
//
// **图标**：`appIcon` 是一个 `image://flowkeyd-app/…` URL，像素由
// `app::AppIconProvider` 在一条后台 STA 线程上按需去 shell 里取（每个约 3 ms），
// 所以图标会一格格“长出来”。`sourceSize` 用**设备像素**给（逻辑边长 × 缩放），
// 这样在 200% 缩放下取的就是 80 像素的图标，而不是把 40 像素的放大糊掉。
//
// **不切输入法**：窗口切换器那边一弹出就把输入法切成英文（它匹配的是进程名），
// 但这里的名字可能是中文（「记事本」），切英文反而筛不出东西。
Window {
    id: root

    // 无边框 + 始终置顶 + **不进任务栏**（`Qt.Tool` = `WS_EX_TOOLWINDOW`）；
    // `color` 必须是 transparent（见 AGENTS.md 第 10 节）。
    flags: Qt.Window | Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    // 标题里带着可见条数（无边框窗口，这行字用户看不到），外面想断言「筛选生效
    // 了」时能从窗口标题读出来。
    title: root.appModel ? root.appModel.caption : qsTr("flowkeyd 程序")

    property var appModel: null
    property var host: null

    // 中文一律用微软雅黑（与其它几个弹窗一致；理由见 AGENTS.md 第 10 节）。
    readonly property string uiFontFamily: "Microsoft YaHei"

    readonly property int iconSize: root.appModel ? root.appModel.iconSize : 40
    readonly property int tileTextWidth:
        root.appModel ? root.appModel.cellWidth - 24 : 102
    /// 取图标时用的边长（设备像素）：逻辑边长 × 本窗口那块屏的缩放。
    ///
    /// 用 `Window.devicePixelRatio`（`QWindow` 的属性，从 `QtQuick` 就能拿到）
    /// 而不是 `Screen.devicePixelRatio`：后者要额外 `import QtQuick.Window`，
    /// 那会给发布包多拖一个 QML 模块进去。
    readonly property int iconPixels:
        Math.max(8, Math.round(root.iconSize * root.devicePixelRatio))

    // 卡片尺寸来自模型（6 列 × 6 行 → 800 × 622）；模型还没接上时的回退值与它一致。
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

    /// 把输入框的文本同步成模型里的筛选串（打等值判断就不会跟正在打字的用户打架）。
    function syncFilterField() {
        if (!filterField)
            return
        var wanted = root.appModel ? root.appModel.filter : ""
        if (filterField.text !== wanted)
            filterField.text = wanted
    }

    /// 把键盘选中格带进视野。滚动本身完全交给 `GridView`。
    function followSelection(line) {
        if (!grid || line < 0)
            return
        grid.positionViewAtIndex(line, GridView.Contain)
    }

    /// 执行模型给的决定（`setFilter()` / `handleKey()` / `activate()` 共用）。
    function applyDecision(decision) {
        if (!decision || !root.host)
            return
        if (decision.decision === "cancel")
            root.host.appDismiss()
        else if (decision.decision === "choose")
            root.host.appChoose(decision.index)
    }

    /// 启动一格（单击 / `Enter`）。
    function activateTile(line) {
        if (!root.appModel)
            return
        root.applyDecision(root.appModel.activate(line))
    }

    /// 右键一格：弹那个程序的**原生 shell 菜单**（与开始菜单 / 资源管理器一致，
    /// 见 `platform/win/shell_menu.h`）。
    ///
    /// `host.appContextMenu()` 是**阻塞**的（里面 `TrackPopupMenuEx` 一直等到用户
    /// 选完），返回「用户到底选没选」。规则交给模型：选了就收卡片，取消则留着。
    function contextMenuTile(line) {
        if (!root.appModel || !root.host)
            return
        var invoked = root.host.appContextMenu(line)
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
                root.followSelection(root.appModel ? root.appModel.selected : -1)
            }

            function onStateChanged() {
                root.syncFilterField()
            }
        }

        // 筛选框：标准 `TextField`，直接摆在模型给的矩形里（卡片没有标题行）。
        TextField {
            id: filterField

            z: 1
            x: root.appModel ? root.appModel.filterRect.x : 0
            y: root.appModel ? root.appModel.filterRect.y : 0
            width: root.appModel ? root.appModel.filterRect.width : 0
            height: root.appModel ? root.appModel.filterRect.height : 30
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            placeholderText: root.appModel ? root.appModel.filterPlaceholder : ""
            selectByMouse: true
            focus: true

            // 文本一变就回给模型（模型会把选中项复位到第一格）。
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

        // 网格：标准 `GridView` + 自带滚动条；每一格是标准 `ItemDelegate`。
        GridView {
            id: grid

            x: root.appModel ? root.appModel.filterRect.x : 0
            y: root.appModel ? root.appModel.listTop : 0
            width: root.appModel ? root.appModel.filterRect.width : 0
            height: Math.max(parent.height - y - root.appModel.listBottom, 0)
            clip: true
            focus: false
            model: root.appModel
            cellWidth: root.appModel ? root.appModel.cellWidth : 126
            cellHeight: root.appModel ? root.appModel.cellHeight : 88
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            // 网格刚长出来时 Qt 会把 `contentY` 摆到「保持滚动比例」的位置，
            // 内容 / 几何稳下来之后再摆一次选中格（幂等，不干扰用户自己滚）。
            onContentHeightChanged: root.followSelection(root.appModel ? root.appModel.selected : -1)
            onHeightChanged: root.followSelection(root.appModel ? root.appModel.selected : -1)

            delegate: ItemDelegate {
                id: tile

                required property int index
                required property string appName
                required property string appIcon
                // **不叫 `highlighted`**：`ItemDelegate` 自己就有这个属性。
                required property bool rowSelected

                width: grid.cellWidth
                height: grid.cellHeight
                // 委托不自取焦点：焦点始终留在筛选框里。
                focusPolicy: Qt.NoFocus
                highlighted: tile.rowSelected

                // 悬停即高亮（与 `menu` / 窗口切换器一致）。
                onHoveredChanged: {
                    if (hovered && root.appModel)
                        root.appModel.setHover(tile.index)
                }

                // 左键点一格 = 启动它。
                onClicked: root.activateTile(tile.index)

                // 右键点一格 = 那个程序的原生 shell 菜单（打开 / 以管理员身份运行 /
                // 打开文件位置 / 属性……）。用 `TapHandler` 而不是再盖一个 `MouseArea`：
                // 只吃右键（左边的单击仍然是 `ItemDelegate` 自己的），
                // 而且不会把悬停高亮挡掉。`tapped` 在**松开**时发，所以弹菜单之前
                // 那次右键的 key-up 已经走完了。
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: root.contextMenuTile(tile.index)
                }

                contentItem: Item {
                    Image {
                        id: tileIcon

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        width: root.iconSize
                        height: root.iconSize
                        source: tile.appIcon
                        // 设备像素：200% 缩放下要的是 80 像素的图标。
                        sourceSize: Qt.size(root.iconPixels, root.iconPixels)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        asynchronous: true
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: tileIcon.bottom
                        anchors.topMargin: 4
                        width: root.tileTextWidth
                        text: tile.appName
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

        // 一条都筛不出来时给一句人话。
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
}
