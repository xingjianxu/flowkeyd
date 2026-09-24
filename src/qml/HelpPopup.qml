pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `help` 动作弹出的快捷键帮助窗口：看快捷键，也能**直接把它跑起来**。
//
// 这个界面里除了卡片外框（无边框圆角窗口总得有一块底）之外，**全是 Qt 的
// 标准控件**：
//   * 筛选框是一个真正的 `TextField`：鼠标点一下就能进去打字，光标、选区、
//     输入法候选、右键菜单、`Home`/`End`/左右箭头都是 Qt 的标准行为；
//   * 列表是 `ListView`，每一行是 `ItemDelegate`：悬停 / 按下 / 高亮都由
//     FluentWinUI3 的标准样式画；**单击 = 选中它 + 把它的按键复制走**，
//     **双击 = 选中它 + 直接执行它的动作**；
//   * 滚动是 Qt 自带的 `ScrollBar`（滚轮、拖动滑块、平滑滚动全归 Qt），
//     键盘选中项用 `positionViewAtIndex(..., Contain)` 带进视野。
//
// 2026-09 重做之前这里有两处「自绘控件」：一个用 `Rectangle` + `Label` + 一根
// `Rectangle` 光标拼出来的假输入框（点不动、进不了输入状态），以及靠两块不透明
// 底色遮住滚进来的一行的表头 / 底部遮罩。两者都删了 —— 界面里尽量不要再出现
// 这种自绘件（项目所有者 2026-09 要求）。
//
// 逻辑仍然全在 `app::HelpModel`（纯逻辑、有单测）：筛选、`可见/总数` 计数、
// 键盘选中项、`Enter` 执行哪一行、危险动作的两次确认、三级 `Esc`。QML 只做两件事：
//   * 把 `handleKey()` / `activateRow()` 给的决定执行掉（`applyDecision`）；
//   * 把模型的状态（筛选文本、选中行、待确认行）同步给控件。
Window {
    id: root

    // 无边框 + 始终置顶 + **不进任务栏**（`Qt.Tool` = `WS_EX_TOOLWINDOW`，
    // 与选单同一个理由，见 `MenuPopup.qml`）；`color` 必须是 transparent。
    flags: Qt.Window | Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    // 标题里带着「可见/总数」，外面不用看窗口内部就知道筛选生效了
    // （窗口本身是无边框的，这行字用户看不到）。
    title: root.helpModel ? root.helpModel.caption : qsTr("flowkeyd 快捷键")

    property var helpModel: null
    property var host: null

    // 中文一律用微软雅黑：FluentWinUI3 默认的族是 `Segoe UI Variable`，它没有中文
    // 字形，中文只能落到系统回退字体上（本机是宋体，还是衬线的），跟旁边的拉丁
    // 字母/数字摆在一起很违和。
    // YaHei 自带拉丁字形，所以整张卡片统一用一个族就够。
    // 卡片里的每个文字控件都要显式写（`Window`/`Item` 没有 `font` 属性，
    // QML 里也没法只声明一次就自动往下传）；与 `MenuPopup.qml` 的那个同名属性
    // 保持一致。
    readonly property string uiFontFamily: "Microsoft YaHei"

    width: root.helpModel ? root.helpModel.cardWidth : 500
    height: root.helpModel ? root.helpModel.cardHeight : 220

    // 与选单同一个理由：刚显示出来的 300 ms 内失去焦点不算「用户点了别处」。
    property double armedAt: 0
    onActiveChanged: {
        if (active) {
            root.armedAt = Date.now()
            // 窗口拿到前台就说明用户想用了：把光标放进筛选框，直接就能打字。
            if (filterField)
                filterField.forceActiveFocus()
        } else if (root.armedAt > 0 && Date.now() - root.armedAt > 300) {
            if (root.host)
                root.host.helpDismiss()
        }
    }

    // 快捷键模型给出的尺寸（逻辑像素）。
    readonly property int badgeHeight: root.helpModel ? root.helpModel.badgeHeight : 19
    readonly property int badgePad: root.helpModel ? root.helpModel.badgePad : 7
    readonly property int badgeGap: root.helpModel ? root.helpModel.badgeGap : 3
    readonly property int rowInset: root.helpModel ? root.helpModel.rowInset : 10
    readonly property int rowHeight: root.helpModel ? root.helpModel.rowHeight : 46
    readonly property int rowSpacing: root.helpModel ? root.helpModel.rowSpacing : 2
    readonly property int listTop: root.helpModel ? root.helpModel.listTop : 88
    readonly property int listBottom: root.helpModel ? root.helpModel.listBottom : 44

    /// 把输入框的文本同步成模型里的筛选串。
    ///
    /// 输入框自己持有它显示的文本：用户在框里打字时 Qt 会直接改 `text`，
    /// 声明式的绑定会被打断（这是 `TextField` 的既定行为），模型只在
    /// `onTextChanged` 里收到最终文本。反过来模型改筛选的地方只有两处 ——
    /// `Esc` 清空、重新打开时复位 —— 所以在这里单向同步一次就够了；
    /// 比较过不等才写，不会和正在打字的用户打架。
    function syncFilterField() {
        if (!filterField)
            return
        var wanted = root.helpModel ? root.helpModel.filter : ""
        if (filterField.text !== wanted)
            filterField.text = wanted
    }

    /// 把键盘选中项带进视野。滚动本身完全交给 `ListView`：`Contain` 是标准语义
    /// （行已经在视野里就什么都不做），滚轮 / 拖滑块 / 惯性都不经过这里。
    function followSelection(line) {
        if (!listView || line < 0)
            return
        listView.positionViewAtIndex(line, ListView.Contain)
    }

    /// 执行模型给的决定（`handleKey()` 与双击的 `activateRow()` 共用一套）。
    ///
    /// `arm`/`disarm`/`none` 不需要做任何事：模型的状态已经改了，行上的待确认
    /// 标记（`rowArmed`）与底部提示（`footerText`）会跟着信号自己更新。
    function applyDecision(decision) {
        if (!decision || !root.host)
            return
        if (decision.decision === "cancel")
            root.host.helpDismiss()
        else if (decision.decision === "copy")
            root.host.helpCopy(decision.index)
        else if (decision.decision === "run")
            root.host.helpRun(decision.index)
        else if (decision.decision === "clear")
            root.syncFilterField()
    }

    /// 双击一行（或 `Enter`）：选中它并执行它的动作。
    /// 危险动作（`quit`/`suspend`/`power`）第一次只会得到 `arm`，再双击一次才执行。
    function activateRow(line) {
        if (!root.helpModel)
            return
        root.applyDecision(root.helpModel.activateRow(line))
    }

    /// 按键决定由模型给，这里只执行（与 `MenuPopup` 同一套）。
    function handleKeyEvent(event) {
        if (!root.helpModel || !root.host) {
            event.accepted = false
            return
        }
        var decision = root.helpModel.handleKey(event.key)
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
        radius: root.helpModel ? root.helpModel.cardRadius : 12
        color: root.palette.base
        border.width: 1
        border.color: root.palette.mid
        clip: true
        focus: true

        // 筛选框不接的键（比如焦点落在滚动条上时）会冒到这里。
        Keys.onPressed: function (event) {
            root.handleKeyEvent(event)
        }

        // 模型换了条目、改了筛选、挪了选中项之后，把状态同步给控件。
        Connections {
            target: root.helpModel

            function onItemsChanged() {
                root.syncFilterField()
            }

            // 键盘挪选中项之后把它带进视野（鼠标点选的那一行本来就在视野里）。
            function onSelectedChanged() {
                root.followSelection(root.helpModel ? root.helpModel.selected : -1)
            }

            // `Esc` 清筛选之后模型会发这个信号，输入框里的文本要跟着清掉。
            function onStateChanged() {
                root.syncFilterField()
            }
        }

        Label {
            z: 1
            x: root.helpModel ? root.helpModel.titleRect.x : 0
            y: root.helpModel ? root.helpModel.titleRect.y : 0
            width: root.helpModel ? root.helpModel.titleRect.width : 0
            height: root.helpModel ? root.helpModel.titleRect.height : 0
            text: root.helpModel ? root.helpModel.title : ""
            color: root.palette.text
            font.family: root.uiFontFamily
            font.pointSize: 12.5
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            z: 1
            x: root.helpModel ? root.helpModel.countRect.x : 0
            y: root.helpModel ? root.helpModel.countRect.y : 0
            width: root.helpModel ? root.helpModel.countRect.width : 0
            height: root.helpModel ? root.helpModel.countRect.height : 0
            text: root.helpModel ? root.helpModel.countText : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 8.5
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 筛选框：标准 `TextField`，直接摆在模型给的矩形里，底色 / 边框 / 焦点
        // 下划线全由 FluentWinUI3 的样式画。
        TextField {
            id: filterField

            z: 1
            x: root.helpModel ? root.helpModel.filterRect.x : 0
            y: root.helpModel ? root.helpModel.filterRect.y : 0
            width: root.helpModel ? root.helpModel.filterRect.width : 0
            height: root.helpModel ? root.helpModel.filterRect.height : 30
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            placeholderText: root.helpModel ? root.helpModel.filterPlaceholder : ""
            selectByMouse: true
            focus: true

            // 文本一变就回给模型（带等值判断，所以下面那次反向同步不会来回振荡）。
            //
            // 用 `textChanged` 而不是 `textEdited`：输入法提交中文、粘贴、以及
            // 拖动选中的文本都是「非键盘逐字」的路径，`textEdited` 不一定发；
            // `textChanged` 一定会发，而等值判断保证了我们自己同步过去的文本
            // 不会被当成用户输入回吐一次。
            onTextChanged: {
                if (root.helpModel && filterField.text !== root.helpModel.filter)
                    root.helpModel.setFilter(filterField.text)
            }

            // `Keys.BeforeItem`（默认）意味着这些处理在 `TextInput` 自己的键盘
            // 处理**之前**跑；模型不接的键原样放行，于是打字 / 退格 / `Home` /
            // `End` / 左右箭头仍然是标准输入框的行为。
            Keys.onPressed: function (event) {
                // 输入法正在组字：方向键与回车属于候选窗，别抢。
                if (filterField.preeditText.length > 0)
                    return
                root.handleKeyEvent(event)
            }
        }

        // 列表：Qt 自己的 `ListView` + 自带滚动条（可拖动、原生滚轮手感），
        // 每一行是标准 `ItemDelegate`。列表只占行区域，所以不再需要遮罩。
        ListView {
            id: listView

            x: 0
            y: root.listTop
            width: parent.width
            height: Math.max(parent.height - root.listTop - root.listBottom, 0)
            clip: true
            focus: false
            model: root.helpModel
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            // 列表刚长出来时 Qt 会把 `contentY` 摆到「保持当前滚动比例」的位置
            // （实测 30 条时是 90，而不是顶部的 0），内容 / 几何稳下来之后要再摆
            // 一次选中项。`positionViewAtIndex` 是幂等的：行已经在视野里就什么都
            // 不改，所以这两条不会干扰用户自己滚出来的位置。
            onContentHeightChanged: root.followSelection(root.helpModel ? root.helpModel.selected : -1)
            onHeightChanged: root.followSelection(root.helpModel ? root.helpModel.selected : -1)

            delegate: ItemDelegate {
                id: rowItem

                required property int index
                required property var badges
                required property string label
                required property string detail
                // **不叫 `highlighted`**：`ItemDelegate` 自己就有这个属性
                // （标准样式用它画高亮），撞名之余也分不清是谁的。
                required property bool rowSelected
                // 这一行正在等第二次 `Enter`/双击确认（危险动作）。
                required property bool rowArmed

                width: listView.width
                // 一行占 48 逻辑像素（模型的 `rowHeight` 46 + `rowSpacing` 2；
                // 不用 `spacing`，免得第一 / 最后一行与列表边缘之间多出空隙）。
                // 行**内部**的可用高度由标准 `ItemDelegate` 的内边距决定（见下）。
                height: root.rowHeight + root.rowSpacing
                // 委托不自取焦点：焦点始终留在筛选框里，键盘处理只有一处。
                focusPolicy: Qt.NoFocus
                // **不要覆盖 padding**：左右 12 / 上下 8 是 FluentWinUI3 给列表项
                // 定的标准内边距，内容区跟着它自适应（下面全部锚在 contentItem 上）。
                highlighted: rowItem.rowSelected

                // 单击一行 = 选中它 + 把它复制到剪贴板（帮助窗口复制完不关，
                // 用户可能还要抄下一条）。
                onClicked: {
                    if (!root.helpModel || !root.host)
                        return
                    root.helpModel.setSelected(rowItem.index)
                    root.host.helpCopy(rowItem.index)
                }

                // 双击一行 = 选中它 + 直接执行它的动作（危险动作要两次双击）。
                // 注意：双击前 Qt 会先发两次 `clicked`，上面那个处理器因此会跑两次
                // （把按键文本复制进剪贴板），这是无害的；`setSelected` 在选中项没
                // 变时不会清掉待确认状态，所以「第二次双击」仍然是第二次。
                onDoubleClicked: root.activateRow(rowItem.index)

                contentItem: Item {
                    // 「待确认」的底色：系统 `palette` 里没有警告色，所以这是整个
                    // 帮助窗口里唯一一处硬编码颜色 —— 只用来提示「再按一次才执行」。
                    Rectangle {
                        anchors.fill: parent
                        visible: rowItem.rowArmed
                        color: "#E8A33D"
                        opacity: 0.22
                    }

                    // 按键徽标列：宽度固定，所以每一行的说明文字都对齐。
                    // 高度跟着标准内边距留下的内容区（不写死 46，免得溢出到下一行）。
                    Item {
                        id: keysColumn

                        anchors.left: parent.left
                        anchors.leftMargin: root.rowInset
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: root.helpModel ? root.helpModel.keysWidth : 150
                        clip: true

                        Row {
                            // 徽标序列（牌子 + 和弦之间的圆点）来自模型，宽度由文字量出来。
                            y: (parent.height - height) / 2
                            spacing: root.badgeGap

                            Repeater {
                                model: rowItem.badges

                                delegate: Item {
                                    id: badgeItem

                                    required property var modelData

                                    height: root.badgeHeight
                                    width: modelData.badge ? badgeBox.width : separator.implicitWidth

                                    Label {
                                        id: separator

                                        visible: !badgeItem.modelData.badge
                                        text: badgeItem.modelData.text
                                        color: root.palette.placeholderText
                                        font.family: root.uiFontFamily
                                        font.pointSize: 8.5
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Rectangle {
                                        id: badgeBox

                                        visible: badgeItem.modelData.badge
                                        width: badgeText.implicitWidth + root.badgePad * 2
                                        height: badgeItem.height
                                        radius: height / 4
                                        color: root.palette.alternateBase
                                        border.width: 1
                                        border.color: root.palette.mid
                                        anchors.verticalCenter: parent.verticalCenter

                                        Label {
                                            id: badgeText

                                            anchors.centerIn: parent
                                            text: badgeItem.modelData.text
                                            color: root.palette.text
                                            font.family: root.uiFontFamily
                                            font.pointSize: 9
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 说明 + 动作摘要。
                    Item {
                        id: textColumn

                        anchors.left: keysColumn.right
                        anchors.leftMargin: root.rowInset
                        anchors.right: parent.right
                        anchors.rightMargin: root.rowInset
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom

                        Label {
                            id: labelText

                            width: parent.width
                            height: rowItem.detail.length > 0 ? parent.height * 55 / 100
                                                              : parent.height
                            text: rowItem.label
                            color: root.palette.text
                            font.family: root.uiFontFamily
                            font.pointSize: 10.5
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: rowItem.detail.length > 0
                            y: labelText.height
                            width: parent.width
                            height: parent.height - labelText.height
                            text: rowItem.detail
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

        // 筛选之后一条不剩时给一句人话，而不是留一块空白。
        Label {
            z: 1
            visible: root.helpModel ? !root.helpModel.hasMatches : false
            x: root.helpModel ? root.helpModel.filterRect.x : 0
            y: root.listTop
            width: root.helpModel ? root.helpModel.filterRect.width : 0
            height: root.rowHeight
            text: root.helpModel ? root.helpModel.emptyMessage : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            z: 1
            x: root.helpModel ? root.helpModel.footerRect.x : 0
            y: root.helpModel ? root.helpModel.footerRect.y : 0
            width: root.helpModel ? root.helpModel.footerRect.width : 0
            height: root.helpModel ? root.helpModel.footerRect.height : 0
            text: root.helpModel ? root.helpModel.footerText : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
