pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `help` 动作弹出的快捷键帮助窗口：**看**的而不是**选**的。
//
// 与 `MenuPopup.qml` 同一套做法：几何、筛选、滚动、`Enter` 复制哪一行、
// 两级 `Esc` 全在 `app::HelpModel` 里（纯逻辑、有单测），这里只画。
//
// 筛选框不是 `TextField`，而是自己画的一行字 + 一根光标：所有按键都走
// `Keys.onPressed` 交给模型，于是 `↑`/`↓`/`Enter`/`Esc` 与输入端不会互相抢键，
// 而且字符来自 Qt 译好的 `QKeyEvent::text()`（等价于 oskeyd 的 `WM_CHAR`）。
Window {
    id: root

    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    // 标题里带着「可见/总数」，外面不用看窗口内部就知道筛选生效了
    // （窗口本身是无边框的，这行字用户看不到）。
    title: root.helpModel ? root.helpModel.caption : qsTr("flowkeyd 快捷键")

    property var helpModel: null
    property var host: null

    width: root.helpModel ? root.helpModel.cardWidth : 500
    height: root.helpModel ? root.helpModel.cardHeight : 220

    // 与选单同一个理由：刚显示出来的 300 ms 内失去焦点不算「用户点了别处」。
    property double armedAt: 0
    onActiveChanged: {
        if (active) {
            root.armedAt = Date.now()
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

    Rectangle {
        id: card

        anchors.fill: parent
        radius: root.helpModel ? root.helpModel.cardRadius : 12
        color: root.palette.base
        border.width: 1
        border.color: root.palette.mid
        clip: true
        focus: true

        Keys.onPressed: function (event) {
            if (!root.helpModel || !root.host) {
                event.accepted = false
                return
            }
            var decision = root.helpModel.handleKey(event.key, event.text)
            if (!decision.handled) {
                event.accepted = false
                return
            }
            if (decision.decision === "cancel")
                root.host.helpDismiss()
            else if (decision.decision === "copy")
                root.host.helpCopy(decision.index)
            event.accepted = true
        }

        Label {
            x: root.helpModel ? root.helpModel.titleRect.x : 0
            y: root.helpModel ? root.helpModel.titleRect.y : 0
            width: root.helpModel ? root.helpModel.titleRect.width : 0
            height: root.helpModel ? root.helpModel.titleRect.height : 0
            text: root.helpModel ? root.helpModel.title : ""
            color: root.palette.text
            font.pointSize: 12.5
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            x: root.helpModel ? root.helpModel.countRect.x : 0
            y: root.helpModel ? root.helpModel.countRect.y : 0
            width: root.helpModel ? root.helpModel.countRect.width : 0
            height: root.helpModel ? root.helpModel.countRect.height : 0
            text: root.helpModel ? root.helpModel.countText : ""
            color: root.palette.placeholderText
            font.pointSize: 8.5
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 筛选框：底色比卡片亮一档，里面是输入内容（空的时候显示占位文本）。
        Rectangle {
            id: filterBox

            x: root.helpModel ? root.helpModel.filterRect.x : 0
            y: root.helpModel ? root.helpModel.filterRect.y : 0
            width: root.helpModel ? root.helpModel.filterRect.width : 0
            height: root.helpModel ? root.helpModel.filterRect.height : 0
            radius: height / 4
            color: root.palette.alternateBase

            Label {
                id: filterLabel

                x: root.rowInset
                width: Math.max(filterBox.width - root.rowInset * 2, 0)
                height: filterBox.height
                text: root.helpModel && root.helpModel.filter.length > 0
                      ? root.helpModel.filter
                      : (root.helpModel ? root.helpModel.filterPlaceholder : "")
                color: root.helpModel && root.helpModel.filter.length > 0
                       ? root.palette.text
                       : root.palette.placeholderText
                font.pointSize: 10.5
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            // 光标：一小段竖线。占位文本后面不画（否则看着像已经输入了内容）。
            Rectangle {
                visible: root.helpModel ? root.helpModel.filter.length > 0 : false
                x: filterLabel.x + Math.min(filterLabel.implicitWidth, filterLabel.width) + 2
                y: filterBox.height / 4
                width: 1
                height: filterBox.height / 2
                color: root.palette.highlight
            }
        }

        // 列表。筛选之后一条不剩时给一句人话，而不是留一块空白。
        Label {
            visible: root.helpModel ? !root.helpModel.hasMatches : false
            x: root.helpModel ? root.helpModel.filterRect.x : 0
            y: root.helpModel ? root.helpModel.listTop : 0
            width: root.helpModel ? root.helpModel.filterRect.width : 0
            height: root.helpModel ? root.helpModel.rowHeight : 0
            text: root.helpModel ? root.helpModel.emptyMessage : ""
            color: root.palette.placeholderText
            font.pointSize: 10.5
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Repeater {
            model: root.helpModel

            delegate: Item {
                id: rowItem

                required property var badges
                required property string label
                required property string detail
                required property bool highlighted
                required property var rowRect
                required property var keysRect
                required property var textRect

                x: rowRect.x
                y: rowRect.y
                width: rowRect.width
                height: rowRect.height

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: rowItem.highlighted ? root.palette.highlight : "transparent"
                }

                // 按键徽标列：右边是固定起点，所以每一行的说明文字都对齐。
                Item {
                    id: keysColumn

                    x: rowItem.keysRect.x - rowItem.rowRect.x
                    y: rowItem.keysRect.y - rowItem.rowRect.y
                    width: rowItem.keysRect.width
                    height: rowItem.keysRect.height
                    clip: true

                    Row {
                        // 徽标序列（牌子 + 和弦之间的圆点）来自模型，宽度由文字量出来。
                        y: (keysColumn.height - height) / 2
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
                                    color: rowItem.highlighted ? root.palette.highlightedText
                                                               : root.palette.placeholderText
                                    font.pointSize: 8.5
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Rectangle {
                                    id: badgeBox

                                    visible: badgeItem.modelData.badge
                                    width: badgeText.implicitWidth + root.badgePad * 2
                                    height: badgeItem.height
                                    radius: height / 4
                                    color: rowItem.highlighted ? root.palette.highlightedText
                                                               : root.palette.alternateBase
                                    border.width: 1
                                    border.color: rowItem.highlighted ? root.palette.highlight
                                                                      : root.palette.mid
                                    anchors.verticalCenter: parent.verticalCenter

                                    Label {
                                        id: badgeText

                                        anchors.centerIn: parent
                                        text: badgeItem.modelData.text
                                        color: rowItem.highlighted ? root.palette.highlight
                                                                   : root.palette.text
                                        font.pointSize: 9
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    id: textColumn

                    x: rowItem.textRect.x - rowItem.rowRect.x
                    y: rowItem.textRect.y - rowItem.rowRect.y
                    width: rowItem.textRect.width
                    height: rowItem.textRect.height

                    Label {
                        id: labelText

                        width: parent.width
                        height: rowItem.detail.length > 0 ? parent.height * 55 / 100
                                                          : parent.height
                        text: rowItem.label
                        color: rowItem.highlighted ? root.palette.highlightedText
                                                   : root.palette.text
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
                        color: rowItem.highlighted ? root.palette.highlight
                                                   : root.palette.placeholderText
                        font.pointSize: 8.5
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // 滚动条：只有内容超出视口时才画（几何也来自模型）。
        Rectangle {
            visible: root.helpModel ? root.helpModel.hasScrollbar : false
            x: root.helpModel ? root.helpModel.scrollTrack.x : 0
            y: root.helpModel ? root.helpModel.scrollTrack.y : 0
            width: root.helpModel ? root.helpModel.scrollTrack.width : 0
            height: root.helpModel ? root.helpModel.scrollTrack.height : 0
            radius: width / 2
            color: root.palette.alternateBase

            Rectangle {
                x: 0
                y: root.helpModel ? root.helpModel.scrollThumb.y - root.helpModel.scrollTrack.y : 0
                width: parent.width
                height: root.helpModel ? root.helpModel.scrollThumb.height : 0
                radius: width / 2
                color: root.palette.mid
            }
        }

        Label {
            x: root.helpModel ? root.helpModel.footerRect.x : 0
            y: root.helpModel ? root.helpModel.footerRect.y : 0
            width: root.helpModel ? root.helpModel.footerRect.width : 0
            height: root.helpModel ? root.helpModel.footerRect.height : 0
            text: root.helpModel ? root.helpModel.footerText : ""
            color: root.palette.placeholderText
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onPositionChanged: hoverAt(mouseX, mouseY)
            onExited: {
                if (root.helpModel)
                    root.helpModel.setHover(-1)
            }
            onClicked: {
                if (!root.helpModel || !root.host)
                    return
                var index = root.helpModel.clickRow(mouseX, mouseY)
                if (index >= 0)
                    root.host.helpCopy(index)
            }
            onWheel: function (wheel) {
                if (root.helpModel)
                    root.helpModel.wheel(wheel.angleDelta.y)
            }

            function hoverAt(px, py) {
                if (root.helpModel)
                    root.helpModel.setHover(root.helpModel.hitTest(px, py))
            }
        }
    }
}
