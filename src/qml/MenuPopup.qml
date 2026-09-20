pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `menu` 动作弹出的选单：无边框的圆角卡片（FluentWinUI3 的配色 + 系统主题色）。
//
// 这里**只画**，不做任何判断：条目几何、高亮移动、单字符选中、`Esc`/`Enter`
// 语义全在 `app::MenuModel`（纯逻辑、有单测，见 AGENTS.md 第 4 节的分层要求）。
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

        Repeater {
            model: root.menuModel

            delegate: Item {
                id: rowItem

                required property string label
                required property string hint
                required property string keyText
                required property bool highlighted
                required property var rowRect
                required property var badgeRect
                required property var labelRect
                required property var hintRect

                x: rowRect.x
                y: rowRect.y
                width: rowRect.width
                height: rowRect.height

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: rowItem.highlighted ? root.palette.highlight : "transparent"
                }

                // 按键徽标：只有写了 `key` 的条目才有。
                Rectangle {
                    visible: rowItem.keyText.length > 0
                    x: rowItem.badgeRect.x - rowItem.rowRect.x
                    y: rowItem.badgeRect.y - rowItem.rowRect.y
                    width: rowItem.badgeRect.width
                    height: rowItem.badgeRect.height
                    radius: rowItem.badgeRect.width / 4
                    color: rowItem.highlighted ? root.palette.highlightedText
                                               : root.palette.alternateBase
                    border.width: 1
                    border.color: root.palette.mid

                    Label {
                        anchors.centerIn: parent
                        text: rowItem.keyText
                        color: rowItem.highlighted ? root.palette.highlight : root.palette.text
                        font.pointSize: 9
                    }
                }
                Label {
                    x: rowItem.labelRect.x - rowItem.rowRect.x
                    y: rowItem.labelRect.y - rowItem.rowRect.y
                    width: rowItem.labelRect.width
                    height: rowItem.labelRect.height
                    text: rowItem.label
                    color: rowItem.highlighted ? root.palette.highlightedText : root.palette.text
                    font.pointSize: 11
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                Label {
                    x: rowItem.hintRect.x - rowItem.rowRect.x
                    y: rowItem.hintRect.y - rowItem.rowRect.y
                    width: rowItem.hintRect.width
                    height: rowItem.hintRect.height
                    text: rowItem.hint
                    color: rowItem.highlighted ? root.palette.highlightedText
                                               : root.palette.placeholderText
                    font.pointSize: 9
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
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

        // 悬停与点击：命中测试也在模型里（返回条目下标，不在任何条目上时是 -1）。
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onPositionChanged: {
                if (root.menuModel)
                    root.menuModel.setHover(root.menuModel.hitTest(mouseX, mouseY))
            }
            onExited: {
                if (root.menuModel)
                    root.menuModel.setHover(-1)
            }
            onClicked: {
                if (!root.menuModel || !root.host)
                    return
                var index = root.menuModel.hitTest(mouseX, mouseY)
                if (index >= 0)
                    root.host.menuChoose(index)
            }
        }
    }
}
