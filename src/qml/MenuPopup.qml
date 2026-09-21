pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `menu` 动作弹出的选单：无边框的圆角卡片（FluentWinUI3 的配色 + 系统主题色）。
//
// 与帮助窗口同一条路线（项目所有者 2026-09 要求：尽量别用自绘控件）：
//   * 列表是一个真正的 `ListView`，每一行是标准 `ItemDelegate` —— 悬停 / 按下 /
//     高亮（含左边那条系统主题色的竖条）全由 FluentWinUI3 自己画；
//   * **左键单击一行 = 执行它**（`ItemDelegate.onClicked`）；
//   * 「鼠标压在哪一行」由委托自己的 `hovered` 属性报告，行下标就是 `ListView`
//     的下标 —— 模型里不再有任何行几何或命中测试。
// 2026-09 之前这里是一个 `Repeater` + 自绘 `Item`（自己画高亮底与徽标），外加一个
// 铺满卡片的 `MouseArea` 拿模型的 `hitTest()` 做命中测试，现在都删了。
//
// 这里**只画**，不做判断：高亮移动、单字符选中、`Esc`/`Enter` 语义全在
// `app::MenuModel`（纯逻辑、有单测，见 AGENTS.md 第 4 节的分层要求）。
// 键盘事件也只做「问模型要决定 → 执行决定」这一件事。
Window {
    id: root

    // 无边框 + 始终置顶；`color` 必须是 transparent，否则看不到圆角。
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    title: qsTr("flowkeyd 选单")

    // 由 `app::PopupHost` 通过 setProperty 挂上来。
    property var menuModel: null
    property var host: null

    // `MenuModel` 给出的一切尺寸都是逻辑像素，Qt 会按显示器 DPI 自己缩放。
    width: root.menuModel ? root.menuModel.cardWidth : 300
    height: root.menuModel ? root.menuModel.cardHeight : 120

    // 行度量（与帮助窗口同一套写法）：委托占满「行高 + 行距」，列表用它排版。
    readonly property int rowHeight: root.menuModel ? root.menuModel.rowHeight : 38
    readonly property int rowSpacing: root.menuModel ? root.menuModel.rowSpacing : 2
    readonly property int rowInset: root.menuModel ? root.menuModel.rowInset : 8
    readonly property int badgeSize: root.menuModel ? root.menuModel.badgeSize : 22
    readonly property int rowPitch: root.rowHeight + root.rowSpacing
    readonly property int listTop: root.menuModel ? root.menuModel.listTop : 40

    // 刚显示出来的 300 ms 内失去焦点不算「用户点了别处」（与 oskeyd 的
    // `DISMISS_GRACE_MS` 一致，否则窗口会闪一下就不见了）。
    property double armedAt: 0
    onActiveChanged: {
        if (active) {
            root.armedAt = Date.now()
        } else if (root.armedAt > 0 && Date.now() - root.armedAt > 300) {
            if (root.host)
                root.host.menuDismiss()
        }
    }

    Rectangle {
        id: card

        anchors.fill: parent
        radius: root.menuModel ? root.menuModel.cardRadius : 10
        color: root.palette.base
        border.width: 1
        border.color: root.palette.mid
        clip: true
        focus: true

        Keys.onPressed: function (event) {
            if (!root.menuModel || !root.host) {
                event.accepted = false
                return
            }
            var decision = root.menuModel.handleKey(event.key, event.text)
            if (!decision.handled) {
                event.accepted = false
                return
            }
            if (decision.decision === "cancel")
                root.host.menuDismiss()
            else if (decision.decision === "choose")
                root.host.menuChoose(decision.index)
            event.accepted = true
        }

        // 指针离开整张卡片时把高亮还给键盘选中项（悬停只在卡片上有意义）。
        // 用 `HoverHandler` 而不是 `MouseArea`：它是 Qt 自带的悬停处理器，
        // **被动**（不抢点击、不挡住委托自己的 `hovered`）。
        HoverHandler {
            onHoveredChanged: {
                if (!hovered && root.menuModel)
                    root.menuModel.setHover(-1)
            }
        }

        // 选单不滚动（条目数决定卡片高度，本来也滚不动）：滚轮在这里什么都不做，
        // 但要**接住**这个事件，别让它继续往后冒（Qt 会接着找接收者，
        // 窗口因此白白重绘一帧——就是肉眼看到的闪一下）。
        WheelHandler {
            onWheel: function (event) {
                event.accepted = true
            }
        }

        // 标题（`menu{ title = ... }` 没写时整块高度是 0）。
        Label {
            visible: root.menuModel ? root.menuModel.hasTitle : false
            x: root.menuModel ? root.menuModel.titleRect.x : 0
            y: root.menuModel ? root.menuModel.titleRect.y : 0
            width: root.menuModel ? root.menuModel.titleRect.width : 0
            height: root.menuModel ? root.menuModel.titleRect.height : 0
            text: root.menuModel ? root.menuModel.title : ""
            color: root.palette.text
            font.pointSize: 12.5
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 列表：Qt 自己的 `ListView`，每一行是标准的 `ItemDelegate`。
        ListView {
            id: listView

            x: 0
            y: root.listTop
            width: parent.width
            height: root.menuModel ? root.menuModel.count * root.rowPitch : 0
            clip: true
            focus: false
            // 列表高度就是内容高度，本来也滚不动；干脆关掉 flick，免得拖动时
            // `Flickable` 把鼠标事件抢走。
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: root.menuModel

            delegate: ItemDelegate {
                id: rowItem

                required property int index
                required property string label
                required property string hint
                required property string keyText
                // **不叫 `highlighted`**：`ItemDelegate` 自己就有这个属性
                // （标准样式用它画高亮），撞名之余也分不清是谁的。
                required property bool rowSelected

                width: listView.width
                height: root.rowPitch
                // 委托不自取焦点：焦点留在卡片上，键盘处理只有一处。
                focusPolicy: Qt.NoFocus
                highlighted: rowItem.rowSelected

                // 鼠标压在哪一行由委托自己的标准属性报告（模型不用自己命中测试）；
                // 指针离开卡片那一下由上面那个 `HoverHandler` 清掉。
                onHoveredChanged: {
                    if (rowItem.hovered && root.menuModel)
                        root.menuModel.setHover(rowItem.index)
                }

                // 单击一行 = 执行它的动作（与 `Enter` 等价）。窗口由 `PopupHost`
                // 先藏起来再执行，`send`/`window` 这类动作才作用在原来的前台
                // 应用上（而不是刚被点过的选单）。
                onClicked: {
                    if (root.host)
                        root.host.menuChoose(rowItem.index)
                }

                contentItem: Item {
                    // 按键徽标：只有写了 `key` 的条目才有。没写的时候也留出同样
                    // 宽度的空位（徽标不可见但几何仍在），好让每一行的标签对齐。
                    Rectangle {
                        id: badge

                        visible: rowItem.keyText.length > 0
                        x: root.rowInset
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.badgeSize
                        height: root.badgeSize
                        radius: root.badgeSize / 4
                        color: root.palette.alternateBase
                        border.width: 1
                        border.color: root.palette.mid

                        Label {
                            anchors.centerIn: parent
                            text: rowItem.keyText
                            color: root.palette.text
                            font.pointSize: 9
                        }
                    }

                    // 右侧的灰色副标题，右对齐。
                    Label {
                        id: hintText

                        anchors.right: parent.right
                        anchors.rightMargin: root.rowInset
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(parent.width * 4 / 10 - root.rowInset, 0)
                        text: rowItem.hint
                        color: root.palette.placeholderText
                        font.pointSize: 9
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }

                    // 条目标题：在徽标与副标题之间左对齐。
                    Label {
                        anchors.left: badge.right
                        anchors.leftMargin: root.rowInset
                        anchors.right: hintText.left
                        anchors.rightMargin: root.rowInset
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowItem.label
                        color: root.palette.text
                        font.pointSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // 底部那行操作提示。
        Label {
            x: root.menuModel ? root.menuModel.footerRect.x : 0
            y: root.menuModel ? root.menuModel.footerRect.y : 0
            width: root.menuModel ? root.menuModel.footerRect.width : 0
            height: root.menuModel ? root.menuModel.footerRect.height : 0
            text: root.menuModel ? root.menuModel.footerText : ""
            color: root.palette.placeholderText
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
