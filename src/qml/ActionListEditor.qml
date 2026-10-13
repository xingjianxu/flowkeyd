pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 一串动作的编辑器（一条快捷键的 `action`、`on_release`，或者一个选单条目的动作）。
//
// **就地修改**：`actions` 是父对象里那个 JS 数组本身，增删只是 `splice`/`push`，
// 于是父对象不用做任何同步。数组本身不会发通知，所以改完调一次 `bump()`
// （`revision++`）让 `Repeater` 重建 —— 每个编辑框都是「边打字边写回对象」的，
// 重建不会丢内容。
//
// 每一项是一个完整动作，用 `ActionEditor` 呈现。**它按 URL 加载**（而不是当类型
// 引用）：`ActionEditor` 的 `menu` 动作里又要用本组件编辑条目里的动作，两者互相
// 引用，按 URL 加载才是 QML 里能成立的那种递归（静态类型引用会形成一个环）。
Column {
    id: list

    property var actions: []
    property var editor: null
    property string addLabel: qsTr("添加动作")
    property int revision: 0

    signal mutated()

    spacing: 8

    Repeater {
        model: (list.revision, list.actions)

        delegate: Rectangle {
            id: block

            required property int index
            required property var modelData

            width: list.width
            implicitHeight: body.implicitHeight + 14
            color: block.palette.alternateBase
            radius: 6
            border.width: 1
            border.color: block.palette.mid

            Column {
                id: body

                x: 7
                y: 7
                width: parent.width - 14
                spacing: 4

                RowLayout {
                    width: parent.width
                    spacing: 8

                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        width: 60
                        text: qsTr("动作 %1").arg(block.index + 1)
                        color: block.palette.placeholderText
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("上移")
                        enabled: block.index > 0
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                        onClicked: list.moveItem(block.index, -1)
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("下移")
                        enabled: block.index + 1 < list.actions.length
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                        onClicked: list.moveItem(block.index, 1)
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("删除")
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                        onClicked: list.removeItem(block.index)
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Loader {
                    id: actionLoader

                    // 逗号表达式让这一条依赖 `list.editor`：解析器比委托先拿到
                    // `editor` 也能在之后补上（`Loader` 是同步加载的，`onLoaded`
                    // 可能早于父对象的绑定求值）。
                    readonly property var targetAction: (list.editor, block.modelData)

                    width: parent.width
                    height: item ? item.implicitHeight : 0
                    source: Qt.resolvedUrl("ActionEditor.qml")
                    onLoaded: actionLoader.bind()
                    onTargetActionChanged: actionLoader.bind()

                    function bind() {
                        if (!item)
                            return
                        item.action = actionLoader.targetAction
                        item.editor = list.editor
                    }
                }
            }
        }
    }

    Button {
        text: list.addLabel
        font.family: "Microsoft YaHei"
        font.pointSize: 10
        onClicked: list.appendItem()
    }

    function appendItem() {
        if (!list.editor)
            return
        list.actions.push(list.editor.newAction("run"))
        list.bump()
    }

    function removeItem(index) {
        list.actions.splice(index, 1)
        list.bump()
    }

    function moveItem(index, delta) {
        const target = index + delta
        if (target < 0 || target >= list.actions.length)
            return
        const moved = list.actions.splice(index, 1)[0]
        list.actions.splice(target, 0, moved)
        list.bump()
    }

    function bump() {
        list.revision++
        list.mutated()
    }
}
