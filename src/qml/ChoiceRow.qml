pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 「标签 + 下拉框」的一行。`value` 是配置里的原始取值，`labels` 是它的中文显示名
// （长度可以不一样：`options` 里的每一项都得有显示名，但显示名不必是唯一值）。
//
// `currentIndex` 用**绑定**而不是 `Component.onCompleted`：这些下拉框常常是在
// `editor` 挂上来之前就建好的（弹层与动作编辑器里，条目是创建之后才赋给它们的），
// 那时 `options` 还是空的，`onCompleted` 会把 `currentIndex` 定死在 0 —— 界面显示的
// 值与真实值不一样，而且没有任何报错（踩过：一个 `trigger = "release"` 的绑定显示成
// 了「按下时」）。绑定会在 `options`/`value` 变化时自己重新求值。
SettingRow {
    id: row

    property var options: []
    property var labels: row.options
    property string value: ""

    signal picked(string value)

    ComboBox {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(280, parent.width)
        model: row.labels
        enabled: row.options.length > 1
        currentIndex: {
            const index = row.options.indexOf(row.value)
            return index >= 0 ? index : 0
        }
        onActivated: row.picked(row.options[currentIndex])
    }
}
