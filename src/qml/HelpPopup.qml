pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `help` 动作弹出的快捷键帮助窗口：**看**的而不是**选**的。
//
// 筛选、`Enter` 复制哪一行、两级 `Esc`、键盘选中项全在 `app::HelpModel`
// 里（纯逻辑、有单测）；列表本身是一个真正的 `ListView` + Qt 自带的
// `ScrollBar`：滚轮、拖动滑块、平滑滚动全部交给 Qt。**不再自己画滑槽/滑块，
// 也不自己算滚动位置** —— 旧实现那套既拖不动滑块，又在滚轮下闪（AGENTS.md 第 10 节）。
//
// 高亮就是键盘选中项，只有 `↑`/`↓`/`PgUp`/`PgDn`/`Home`/`End` 会改它。
// **鼠标悬停不改高亮**：悬停高亮属于非标准的自定义行为 —— 拖动滚动条时指针
// 压在列表上，高亮会随着滚动在行之间乱跳；现在拖动与滚轮都只滚视图
// （2026-09，AGENTS.md 第 10 节）。
//
// 布局上的三个要点：
// * 列表铺满整张卡片，上下用 `Flickable` 的 `topMargin`/`bottomMargin` 把表头与
//   底部提示的位置让出来（所以光标停在表头上滚轮照样能滚，Qt 会把没被接住的
//   滚轮事件递给下面的列表）。
// * 表头/底部提示各有一块**不透明底色**盖住列表：行滚到边缘时会从它们下面
//   穿过，不盖住就会与标题/筛选框叠在一起。
// * 筛选框不是 `TextField`，而是自己画的一行字 + 一根光标：所有按键都走
//   `Keys.onPressed` 交给模型，于是 `↑`/`↓`/`Enter`/`Esc` 与输入端不会互相抢键，
//   而且字符来自 Qt 译好的 `QKeyEvent::text()`（等价于 oskeyd 的 `WM_CHAR`）。
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
    readonly property int rowHeight: root.helpModel ? root.helpModel.rowHeight : 46

    /// 键盘改过选中项之后把它带进视野。
    ///
    /// 目标位置由模型算（纯算术、有单测），因为 Qt 的 `ListView.positionViewAtIndex`
    /// 只看列表自己的矩形，会把行留在表头/底部提示底下。滚动本身仍然全部是
    /// `ListView` 的事：滚轮、拖滑块、惯性都不经过这里。
    function followSelection(line) {
        if (!listView || !root.helpModel)
            return
        listView.contentY = root.helpModel.scrollTargetY(line,
                                                         listView.contentY,
                                                         listView.topMargin,
                                                         listView.bottomMargin,
                                                         listView.height)
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

        // 列表：Qt 自己的 `ListView` + 自带滚动条（可拖动、原生滚轮手感）。
        ListView {
            id: listView

            anchors.fill: parent
            clip: true
            focus: false
            // 行区域 = 卡片上下各让出 listTop / listBottom。
            topMargin: root.helpModel ? root.helpModel.listTop : 0
            bottomMargin: root.helpModel ? root.helpModel.listBottom : 0
            boundsBehavior: Flickable.StopAtBounds
            model: root.helpModel

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                z: 3
            }

            // 键盘移动选中项之后把它带进视野（鼠标滚动不动选中项）。
            // 模型在换条目/筛选之后也会发一次，于是视图回到第一条。
            Connections {
                target: root.helpModel

                function onSelectedChanged() {
                    if (listView)
                        root.followSelection(root.helpModel ? root.helpModel.selected : -1)
                }
            }

            delegate: Item {
                id: rowItem

                required property int index
                required property var badges
                required property string label
                required property string detail
                required property bool highlighted

                width: listView.width
                // 行本身 46，下面留 2 像素空隙（`spacing` 不用，免得表头/底部
                // 与第一/最后一行之间多出空隙）。
                height: root.rowHeight + (root.helpModel ? root.helpModel.rowSpacing : 2)

                Rectangle {
                    id: rowBackground

                    width: parent.width
                    height: root.rowHeight
                    radius: 6
                    color: rowItem.highlighted ? root.palette.highlight : "transparent"
                }

                // 点一行 = 复制它的按键（帮助窗口复制完不关，用户可能还要抄下一条）。
                TapHandler {
                    onTapped: {
                        if (root.host)
                            root.host.helpCopy(rowItem.index)
                    }
                }

                // 按键徽标列：宽度固定，所以每一行的说明文字都对齐。
                Item {
                    id: keysColumn

                    anchors.left: parent.left
                    anchors.leftMargin: root.rowInset
                    anchors.top: parent.top
                    width: root.helpModel ? root.helpModel.keysWidth : 150
                    height: root.rowHeight
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

                // 说明 + 动作摘要。
                Item {
                    id: textColumn

                    anchors.left: keysColumn.right
                    anchors.leftMargin: root.rowInset
                    anchors.right: parent.right
                    anchors.rightMargin: root.rowInset
                    anchors.top: parent.top
                    height: root.rowHeight

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

            // 这里**没有** `onContentYChanged` 处理：滚动只改视图，高亮只跟键盘
            // 选中项走，悬停不再参与（2026-09 取消了悬停高亮）。
            // 列表刚长出来时 Qt 会把 `contentY` 摆到「保持当前滚动比例」的位置
            // （实测 30 条时是 90，而不是顶部），内容/几何稳下来之后要再摆一次。
            // `followSelection` 是幂等的：行已经在行区域里就什么都不改，所以这
            // 两条不会干扰用户自己的滚动。
            onContentHeightChanged: root.followSelection(root.helpModel ? root.helpModel.selected : -1)
            onHeightChanged: root.followSelection(root.helpModel ? root.helpModel.selected : -1)
        }

        // 表头/底部提示的不透明底色：行滚到边缘会从下面穿过（`topMargin` 只保证
        // 滚到两端时行停在正确位置，不负责遮挡）。
        Rectangle {
            id: headerMask

            z: 1
            x: 0
            y: 0
            width: parent.width
            height: root.helpModel ? root.helpModel.listTop : 0
            color: root.palette.base
        }

        Rectangle {
            id: footerMask

            z: 1
            x: 0
            y: parent.height - height
            width: parent.width
            height: root.helpModel ? root.helpModel.listBottom : 0
            color: root.palette.base
        }

        Label {
            z: 2
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
            z: 2
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

            z: 2
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

        // 筛选之后一条不剩时给一句人话，而不是留一块空白。
        Label {
            z: 2
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

        Label {
            z: 2
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
    }
}
