pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 「标签 + 开关」的一行。
//
// 只用于**一定会有值**的布尔量（`enabled` 这种）。可选的三态用 `TriRow`
// ——一个开关摆不出「没写过，跟随默认」这个状态。
SettingRow {
    id: row

    property bool value: false
    property string onText: qsTr("开")
    property string offText: qsTr("关")

    signal picked(bool value)

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Switch {
            id: control
            checked: row.value
            onToggled: row.picked(checked)
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: control.checked ? row.onText : row.offText
            color: row.palette.text
            font.family: "Microsoft YaHei"
            font.pointSize: 10
        }
    }
}
