pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 每一行条目的外观：标题 + 灰副标题 + 一串操作按钮。四个列表页共用。
//
// **内容区必须是 `RowLayout`，不能是 `Row` + `anchors.verticalCenter`。**
// 这一行是 `ColumnLayout` 里由 `Repeater` 建出来的委托，而 `contentItem` 的
// `implicitWidth` 又会被用来推委托自己的宽度 —— `Row`（定位器）里放几个带垂向锚点
// 的子项时，这条回路会让 Qt 的布局/定位器递归到爆栈：**SIGSEGV，一条警告都不打**。
// 实测（2026-10）：同一个 `Row` 里 1 个按钮没事、5 个按钮必崩；换成
// `RowLayout` + `Layout.alignment` 就稳。所以这一层一律用布局，不在定位器里给子项
// 加锚点。
ItemDelegate {
    id: row

    property string title: ""
    property string subtitle: ""
    property bool entryEnabled: true
    /// 这一行要不要显示「复制」按钮。
    property bool canDuplicate: true
    /// 这一行能不能上下移动（顺序对匹配结果有意义，所以四个列表都允许）。
    property bool canMove: true

    signal editRequested()
    signal duplicateRequested()
    signal removeRequested()
    signal moveRequested(int delta)

    implicitHeight: 52

    contentItem: RowLayout {
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            // 给一个不大的首选宽度：标题很长时也不会把整行的隐式宽度撑到屏幕之外
            // （真正的宽度由布局分配，太长了就省略号）。
            Layout.preferredWidth: 180
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: row.title
                color: row.entryEnabled ? row.palette.text : row.palette.placeholderText
                font.family: "Microsoft YaHei"
                font.pointSize: 10.5
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: row.subtitle
                color: row.palette.placeholderText
                font.family: "Microsoft YaHei"
                font.pointSize: 8.5
                elide: Text.ElideRight
            }
        }

        Button {
            Layout.alignment: Qt.AlignVCenter
            // 固定宽度：FluentWinUI3 的 `Button` 有一个很大的最小宽度，五个按钮会白
            // 吃掉半行，把标题挤扁（「不要拥挤」）。
            Layout.preferredWidth: 72
            text: qsTr("编辑")
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            onClicked: row.editRequested()
        }

        Button {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 72
            visible: row.canDuplicate
            text: qsTr("复制")
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            onClicked: row.duplicateRequested()
        }

        Button {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 44
            visible: row.canMove
            text: qsTr("↑")
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            onClicked: row.moveRequested(-1)
        }

        Button {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 44
            visible: row.canMove
            text: qsTr("↓")
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            onClicked: row.moveRequested(1)
        }

        Button {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 72
            text: qsTr("删除")
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            onClicked: row.removeRequested()
        }
    }
}
