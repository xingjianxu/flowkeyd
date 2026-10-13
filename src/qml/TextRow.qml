pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 「标签 + 单行输入框」的一行。
//
// `onTextEdited` 而不是 `onTextChanged`：后者在外部改 `value` 时也会发，写回去就
// 成了来回打架（AGENTS.md 里同样的坑）。没有占位提示时 `placeholder` 留空。
SettingRow {
    id: row

    property string value: ""
    property string placeholder: ""
    property int fieldWidth: 320

    signal edited(string value)

    TextField {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(row.fieldWidth, parent.width)
        placeholderText: row.placeholder
        text: row.value
        font.family: "Microsoft YaHei"
        font.pointSize: 10
        onTextEdited: row.edited(text)
    }
}
