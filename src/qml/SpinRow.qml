pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 「标签 + 数字框」的一行。`suffix` 只影响显示（`ms` 这类单位）。
SettingRow {
    id: row

    property int value: 0
    property int from: 0
    property int to: 100000
    property int stepSize: 1
    property string suffix: ""

    signal picked(int value)

    SpinBox {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(160, parent.width)
        from: row.from
        to: row.to
        stepSize: row.stepSize
        editable: true
        value: row.value
        textFromValue: function (value) {
            return value + row.suffix
        }
        valueFromText: function (text) {
            const parsed = parseInt(text, 10)
            return isNaN(parsed) ? row.value : parsed
        }
        onValueModified: row.picked(value)
    }
}
