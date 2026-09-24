pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `windows` 动作弹出的**窗口切换器**：列出当前所有程序窗口，打字就把进程名
// 前缀匹配的窗口留下；只剩一个窗口时直接激活它。
//
// 与 `HelpPopup.qml` 同一套骨架：除了卡片外框（无边框圆角窗口总得有一块底）
// 之外全是标准控件 —— 筛选框是真正的 `TextField`，列表是 `ListView` +
// 标准 `ItemDelegate`，滚动由 Qt 自带的 `ScrollBar` 负责。
//
// **卡片没有标题行**（项目所有者 2026-09 要求）：筛选框就是第一行，列表紧跟在
// 它下面而且**宽度与筛选框一致**（`filterRect`）。所以窗口计数在底部提示里
// （和键盘提示同一行），给外面看的计数仍在窗口标题（`caption`）里。
//
// 逻辑全在 `app::WindowListModel`（纯逻辑、有单测）：筛选（进程名前缀）、
// `可见/总数` 计数、键盘选中项、`Enter`/`Esc`，以及「只剩一个窗口就直接激活」。
// QML 只做两件事：
//   * 把 `setFilter()` / `handleKey()` / `activate()` 给的决定执行掉；
//   * 把模型的状态（筛选文本、选中行、计数）同步给控件。
//
// **数字选择模式**：筛到一个进程、而它开了好几个窗口时，每一行左边会出现一个
// 数字徽标（`1`..`9`、`0`，超过 10 个窗口的只到第 10 个），按数字直接跳过去。
// 数字键在那种模式下被模型吃掉（`handleKey`），所以不会跑进筛选框里。
//
// **输入法**：一打开就把输入法切成英文（`host.switchUseEnglishInput()`，见
// `platform/win/ime.*`）—— 筛选框匹配的是进程名（ASCII），中文输入法的候选字
// 一条都筛不出来。输入框拿到焦点时再确认一次（鼠标点进来也算）。
Window {
    id: root

    // 无边框 + 始终置顶 + **不进任务栏**（`Qt.Tool` = `WS_EX_TOOLWINDOW`，
    // 与选单同一个道理，见 `MenuPopup.qml`）；`color` 必须是 transparent。
    flags: Qt.Window | Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    // 标题里带着可见条数（窗口本身无边框，这行字用户看不到），外面想断言
    // 「筛选生效了」时能从窗口标题读出来。
    title: root.switchModel ? root.switchModel.caption : qsTr("flowkeyd 窗口")

    property var switchModel: null
    property var host: null

    // 中文一律用微软雅黑（与另外两个弹窗一致；理由见 AGENTS.md 第 10 节）。
    readonly property string uiFontFamily: "Microsoft YaHei"

    width: root.switchModel ? root.switchModel.cardWidth : 560
    height: root.switchModel ? root.switchModel.cardHeight : 220

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
                root.host.switchDismiss()
        }
    }

    readonly property int rowHeight: root.switchModel ? root.switchModel.rowHeight : 46
    readonly property int rowSpacing: root.switchModel ? root.switchModel.rowSpacing : 2
    readonly property int listTop: root.switchModel ? root.switchModel.listTop : 88
    readonly property int listBottom: root.switchModel ? root.switchModel.listBottom : 44
    readonly property int rowInset: root.switchModel ? root.switchModel.rowInset : 10

    /// 把输入框的文本同步成模型里的筛选串（模型改筛选只有 `reset` 一处，
    /// 打等值判断就不会跟正在打字的用户打架）。
    function syncFilterField() {
        if (!filterField)
            return
        var wanted = root.switchModel ? root.switchModel.filter : ""
        if (filterField.text !== wanted)
            filterField.text = wanted
    }

    /// 把键盘选中项带进视野。滚动本身完全交给 `ListView`。
    function followSelection(line) {
        if (!listView || line < 0)
            return
        listView.positionViewAtIndex(line, ListView.Contain)
    }

    /// 执行模型给的决定（`setFilter()` / `handleKey()` / `activate()` 共用）。
    function applyDecision(decision) {
        if (!decision || !root.host)
            return
        if (decision.decision === "cancel")
            root.host.switchDismiss()
        else if (decision.decision === "choose")
            root.host.switchChoose(decision.index)
    }

    /// 激活一行（单击一行 / `Enter`）。
    function activateRow(line) {
        if (!root.switchModel)
            return
        root.applyDecision(root.switchModel.activate(line))
    }

    /// 按键决定由模型给，这里只执行。
    function handleKeyEvent(event) {
        if (!root.switchModel || !root.host) {
            event.accepted = false
            return
        }
        var decision = root.switchModel.handleKey(event.key)
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
        radius: root.switchModel ? root.switchModel.cardRadius : 12
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
            target: root.switchModel

            function onItemsChanged() {
                root.syncFilterField()
            }

            function onSelectedChanged() {
                root.followSelection(root.switchModel ? root.switchModel.selected : -1)
            }

            function onStateChanged() {
                root.syncFilterField()
            }
        }

        // 筛选框：标准 `TextField`，直接摆在模型给的矩形里。**卡片没有标题行**
        // （项目所有者 2026-09 要求），所以它就是卡片的第一行。
        TextField {
            id: filterField

            z: 1
            x: root.switchModel ? root.switchModel.filterRect.x : 0
            y: root.switchModel ? root.switchModel.filterRect.y : 0
            width: root.switchModel ? root.switchModel.filterRect.width : 0
            height: root.switchModel ? root.switchModel.filterRect.height : 30
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            placeholderText: root.switchModel ? root.switchModel.filterPlaceholder : ""
            selectByMouse: true
            focus: true

            // 输入框拿到焦点就再确认一次输入法是英文（窗口打开时 `host` 那里
            // 已经设过；鼠标点进来、或用户中途切回中文时这一下补上）。
            onActiveFocusChanged: {
                if (activeFocus && root.host)
                    root.host.switchUseEnglishInput()
            }

            // 文本一变就回给模型；`setFilter` 若回一个「只剩一个窗口」的决定，
            // 就立刻激活它（这就是“输入到唯一匹配时自动切过去”）。
            onTextChanged: {
                if (root.switchModel && filterField.text !== root.switchModel.filter)
                    root.applyDecision(root.switchModel.setFilter(filterField.text))
            }

            Keys.onPressed: function (event) {
                // 输入法正在组字：方向键与回车属于候选窗，别抢。
                if (filterField.preeditText.length > 0)
                    return
                root.handleKeyEvent(event)
            }
        }

        // 列表：Qt 自己的 `ListView` + 自带滚动条；每一行是标准 `ItemDelegate`。
        // **左右与宽度都跟筛选框一致**（项目所有者 2026-09 要求）：行的高亮底、
        // 滚动条都落在输入框的那条竖线上，不再比输入框宽出一截。
        ListView {
            id: listView

            x: root.switchModel ? root.switchModel.filterRect.x : 0
            y: root.listTop
            width: root.switchModel ? root.switchModel.filterRect.width : 0
            height: Math.max(parent.height - root.listTop - root.listBottom, 0)
            clip: true
            focus: false
            model: root.switchModel
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            // 列表刚长出来时 Qt 会把 `contentY` 摆到「保持滚动比例」的位置，
            // 内容 / 几何稳下来之后再摆一次选中项（幂等，不干扰用户自己滚）。
            onContentHeightChanged: root.followSelection(root.switchModel ? root.switchModel.selected : -1)
            onHeightChanged: root.followSelection(root.switchModel ? root.switchModel.selected : -1)

            delegate: ItemDelegate {
                id: rowItem

                required property int index
                required property string windowTitle
                required property string windowProcess
                // **不叫 `highlighted`**：`ItemDelegate` 自己就有这个属性。
                required property bool rowSelected
                // 数字选择模式下的快捷键（空串表示这一行没有快捷数字）。
                required property string rowKey

                width: listView.width
                height: root.rowHeight + root.rowSpacing
                // 委托不自取焦点：焦点始终留在筛选框里。
                focusPolicy: Qt.NoFocus
                highlighted: rowItem.rowSelected

                // 悬停即高亮（与 `menu` 一致）：`Enter` 切换的就是光标下那一条。
                onHoveredChanged: {
                    if (hovered && root.switchModel)
                        root.switchModel.setHover(rowItem.index)
                }

                // 左键点一行 = 激活它。
                onClicked: root.activateRow(rowItem.index)

                contentItem: Item {
                    // 数字选择模式下的快捷键徽标（`1`..`9`、`0`）；不在那种模式时
                    // 整块不占位，标题与进程名顶到最左边。样式与帮助窗口的
                    // 按键徽标一致（标准控件不画这个，它是模型让显示的提示）。
                    Rectangle {
                        id: keyBadge

                        visible: rowItem.rowKey.length > 0
                        width: 22
                        height: 22
                        radius: height / 4
                        color: root.palette.alternateBase
                        border.width: 1
                        border.color: root.palette.mid
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            anchors.centerIn: parent
                            text: rowItem.rowKey
                            color: root.palette.text
                            font.family: root.uiFontFamily
                            font.pointSize: 9
                        }
                    }

                    Item {
                        id: textColumn

                        anchors.left: parent.left
                        // 22 宽的徽标 + 8 的间隔；没有徽标就不缩进。
                        anchors.leftMargin: rowItem.rowKey.length > 0 ? 30 : 0
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom

                        Label {
                            id: titleText

                            // 有进程名时标题占上面 55%；没有就占满整行。
                            width: parent.width
                            height: rowItem.windowProcess.length > 0 ? parent.height * 55 / 100
                                                                    : parent.height
                            text: rowItem.windowTitle
                            color: root.palette.text
                            font.family: root.uiFontFamily
                            font.pointSize: 10.5
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: rowItem.windowProcess.length > 0
                            y: titleText.height
                            width: parent.width
                            height: parent.height - titleText.height
                            text: rowItem.windowProcess
                            color: root.palette.placeholderText
                            font.family: root.uiFontFamily
                            font.pointSize: 8.5
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // 筛选之后一条不剩时给一句人话。
        Label {
            z: 1
            visible: root.switchModel ? !root.switchModel.hasMatches : false
            x: root.switchModel ? root.switchModel.filterRect.x : 0
            y: root.listTop
            width: root.switchModel ? root.switchModel.filterRect.width : 0
            height: root.rowHeight
            text: root.switchModel ? root.switchModel.emptyMessage : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            z: 1
            x: root.switchModel ? root.switchModel.footerRect.x : 0
            y: root.switchModel ? root.switchModel.footerRect.y : 0
            width: root.switchModel ? root.switchModel.footerRect.width : 0
            height: root.switchModel ? root.switchModel.footerRect.height : 0
            text: root.switchModel ? root.switchModel.footerText : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
