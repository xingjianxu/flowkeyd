pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 一行一个和弦的编辑器，带即时校验。
//
// 为什么不做「按一下录入」：那要在录制期间把引擎**挂起**（否则键根本到不了窗口），
// 也就意味着全局快捷键在那几秒里集体失效。文本录入 + 即时校验的代价是得多打几个
// 字，但任何时候都不会动到正在跑的快捷键；`--list-keys` 能查全部合法键名。
//
// 校验直接问 `ConfigEditor::chordError()`，它用的是与 `--check` 同一套
// `core::parseChord()`，所以「写法不合法」与「和别的绑定重了」在输入时就看得见。
Column {
    id: list

    property var chords: []
    /// 当前条目的**其它**和弦：校验时先把它们排掉，否则编辑一条已有的绑定会一路
    /// 报「这个和弦已经被用了」——被它自己用了。
    property var editor: null
    /// 结构性变化（增删一行）：让 `Repeater` 重建。
    property int revision: 0
    /// 只改了一个和弦的文字：只重算校验文案，**不重建**列表（重建会把用户正在打字
    /// 的那个输入框销毁掉）。
    property int validationRevision: 0

    signal mutated()

    spacing: 6

    Repeater {
        model: (list.revision, list.chords)

        delegate: RowLayout {
            id: line

            required property int index

            spacing: 8

            TextField {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 240
                text: list.chords[line.index] !== undefined ? list.chords[line.index] : ""
                placeholderText: qsTr("例如 Ctrl+Alt+t")
                font.family: "Microsoft YaHei"
                font.pointSize: 10
                onTextEdited: {
                    list.chords[line.index] = text
                    list.validationRevision++
                    list.mutated()
                }
            }

            Label {
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
                text: {
                    if (!list.editor || list.chords[line.index] === undefined)
                        return ""
                    // 逗号表达式：让文案依赖 `validationRevision`，改一个字就重算。
                    return (list.validationRevision,
                            list.editor.chordError(list.chords[line.index], list.chords))
                }
                color: "#D13438"
                font.family: "Microsoft YaHei"
                font.pointSize: 8.5
                elide: Text.ElideRight
            }

            Button {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("删除")
                enabled: list.chords.length > 1
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                onClicked: list.removeAt(line.index)
            }
        }
    }

    Button {
        text: qsTr("添加和弦")
        font.family: "Microsoft YaHei"
        font.pointSize: 10
        onClicked: list.append()
    }

    function append() {
        list.chords.push("")
        list.bump()
    }

    function removeAt(index) {
        if (list.chords.length <= 1)
            return
        list.chords.splice(index, 1)
        list.bump()
    }

    function bump() {
        list.revision++
        list.mutated()
    }
}
