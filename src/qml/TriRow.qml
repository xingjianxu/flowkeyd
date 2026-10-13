pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 三态的一行：`跟随默认` / `是` / `否`。
//
// 配置里有一批**可选**布尔量（`swallow`、`release_modifiers`、`toggle`、`animate`、
// `maximize`、`all_desktops`、`topmost`……）：不写就是「用默认值」，而默认值可能是
// 全局设置、也可能是别的东西。一个普通开关表达不出「没写过」这个状态，写死成
// `true`/`false` 又会把「跟随默认」变成一个固定值 —— 所以这里用三项。
//
// `value` 为 `undefined` 时就是「跟随默认」，回调里也是。
SettingRow {
    id: row

    property var value: undefined

    signal picked(var value)

    ComboBox {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(240, parent.width)
        model: [qsTr("跟随默认"), qsTr("是"), qsTr("否")]
        // 同 `ChoiceRow`：用绑定，别用 `Component.onCompleted`。
        currentIndex: row.indexOf(row.value)
        onActivated: row.picked(row.valueFor(currentIndex))
    }

    function indexOf(v) {
        return v === true ? 1 : (v === false ? 2 : 0)
    }

    function valueFor(index) {
        return index === 1 ? true : (index === 2 ? false : undefined)
    }
}
