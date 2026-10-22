pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 编辑一个 `app{}`：把「同一个程序」的窗口规则与快捷键写在一起。
//
// 三块内容是**可选**的（配置里对应「写没写 `window` / `launch`」「`hotkeys` 里有几条」），
// 所以每块前面一个开关，关掉就把键整个删掉 —— 空表和「没写过」在配置里不是一回事。
OverlayCard {
    id: overlay

    title: overlay.index >= 0 ? qsTr("编辑程序") : qsTr("新增程序")
    note: qsTr("`process` / `title` 至少填一个；窗口规则与快捷键里的匹配条件会自动继承它。")
    cardWidth: 780
    cardHeight: 700

    property bool hasLaunch: entry.launch !== undefined
    property bool hasWindow: entry.window !== undefined
    property int hotkeyRevision: 0

    onEntryChanged: overlay.ensure()

    component Head: Label {
        width: overlay.width
        height: 24
        color: overlay.palette.text
        font.family: "Microsoft YaHei"
        font.pointSize: 11
        font.bold: true
        verticalAlignment: Text.AlignVCenter
    }

    Head {
        text: qsTr("是哪个程序")
    }

    TextRow {
        width: parent.width
        label: qsTr("名称")
        hint: qsTr("只用在日志、`--list` 与默认的窗口标题里")
        value: overlay.entry.name !== undefined ? overlay.entry.name : ""
        onEdited: function (value) { overlay.setOptional("name", value.length > 0 ? value : undefined) }
    }

    TextRow {
        width: parent.width
        label: qsTr("进程名包含")
        placeholder: qsTr("例如 wps")
        value: overlay.entry.process !== undefined ? overlay.entry.process : ""
        onEdited: function (value) { overlay.setOptional("process", value.length > 0 ? value : undefined) }
    }

    TextRow {
        width: parent.width
        label: qsTr("标题包含")
        placeholder: qsTr("留空表示不限制")
        value: overlay.entry.title !== undefined ? overlay.entry.title : ""
        onEdited: function (value) { overlay.setOptional("title", value.length > 0 ? value : undefined) }
    }

    BoolRow {
        width: parent.width
        label: qsTr("启用")
        value: overlay.entry.enabled !== false
        onPicked: function (value) { overlay.entry.enabled = value }
    }

    Head {
        text: qsTr("启动方式")
    }

    BoolRow {
        width: parent.width
        label: qsTr("配置启动方式")
        hint: qsTr("配了之后，这个程序的 `window` 动作里不写 launch 也会用它")
        value: overlay.hasLaunch
        onPicked: {
            overlay.hasLaunch = value
            if (value)
                overlay.entry.launch = {
                    program: "",
                    show: "normal"
                }
            else
                delete overlay.entry.launch
        }
    }

    LaunchFieldsEditor {
        width: parent.width
        visible: overlay.hasLaunch
        spec: overlay.entry.launch ? overlay.entry.launch : ({})
        editor: overlay.editor
    }

    Head {
        text: qsTr("窗口摆放")
    }

    BoolRow {
        width: parent.width
        label: qsTr("配置窗口摆放")
        value: overlay.hasWindow
        onPicked: {
            overlay.hasWindow = value
            if (value)
                overlay.entry.window = {
                    enabled: true
                }
            else
                delete overlay.entry.window
        }
    }

    WindowRuleFields {
        width: parent.width
        visible: overlay.hasWindow
        rule: overlay.entry.window ? overlay.entry.window : ({})
        editor: overlay.editor
    }

    Head {
        text: qsTr("快捷键")
    }

    Repeater {
        model: (overlay.hotkeyRevision, overlay.entry.hotkeys ? overlay.entry.hotkeys : [])

        delegate: Rectangle {
            id: hotkeyBlock

            required property int index

            width: parent.width
            implicitHeight: hotkeyBody.implicitHeight + 14
            color: hotkeyBlock.palette.base
            radius: 6
            border.width: 1
            border.color: hotkeyBlock.palette.mid

            Column {
                id: hotkeyBody

                x: 7
                y: 7
                width: parent.width - 14
                spacing: 4

                RowLayout {
                    spacing: 8

                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        width: 70
                        text: qsTr("绑定 %1").arg(hotkeyBlock.index + 1)
                        color: hotkeyBlock.palette.placeholderText
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("上移")
                        enabled: hotkeyBlock.index > 0
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                        onClicked: overlay.moveHotkey(hotkeyBlock.index, -1)
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("下移")
                        enabled: hotkeyBlock.index + 1 < overlay.entry.hotkeys.length
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                        onClicked: overlay.moveHotkey(hotkeyBlock.index, 1)
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("删除")
                        font.family: "Microsoft YaHei"
                        font.pointSize: 9
                        onClicked: overlay.removeHotkey(hotkeyBlock.index)
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                HotkeyFields {
                    width: parent.width
                    compact: true
                    editor: overlay.editor
                    hotkey: overlay.entry.hotkeys[hotkeyBlock.index]
                }
            }
        }
    }

    Button {
        text: qsTr("添加这个程序的快捷键")
        font.family: "Microsoft YaHei"
        font.pointSize: 10
        onClicked: overlay.appendHotkey()
    }

    validate: function () {
        const process = overlay.entry.process
        const title = overlay.entry.title
        if ((!process || process.length === 0) && (!title || title.length === 0))
            return qsTr("「进程名包含」与「标题包含」至少填一个。")
        if (overlay.hasLaunch
            && (!overlay.entry.launch.program || overlay.entry.launch.program.length === 0))
            return qsTr("启动方式里要填程序路径。")
        const hotkeys = overlay.entry.hotkeys ? overlay.entry.hotkeys : []
        for (let i = 0; i < hotkeys.length; ++i) {
            const keys = hotkeys[i].keys ? hotkeys[i].keys : []
            if (keys.length === 0 || keys[0].length === 0)
                return qsTr("第 %1 条绑定还没有和弦。").arg(i + 1)
            for (let k = 0; k < keys.length; ++k) {
                if (keys[k].length === 0)
                    continue
                const problem = overlay.editor.chordError(keys[k], keys)
                if (problem.length > 0)
                    return qsTr("第 %1 条绑定：%2：%3").arg(i + 1).arg(keys[k]).arg(problem)
            }
        }
        return ""
    }

    function setOptional(key, value) {
        if (value === undefined)
            delete overlay.entry[key]
        else
            overlay.entry[key] = value
    }

    function ensure() {
        if (!overlay.entry)
            return
        if (overlay.entry.enabled === undefined)
            overlay.entry.enabled = true
        if (!overlay.entry.hotkeys)
            overlay.entry.hotkeys = []
        overlay.hasLaunch = overlay.entry.launch !== undefined
        overlay.hasWindow = overlay.entry.window !== undefined
    }

    function appendHotkey() {
        overlay.entry.hotkeys.push({
            keys: [overlay.editor.pickFreeChord()],
            actions: [],
            enabled: true
        })
        overlay.hotkeyRevision++
    }

    function removeHotkey(index) {
        overlay.entry.hotkeys.splice(index, 1)
        overlay.hotkeyRevision++
    }

    function moveHotkey(index, delta) {
        const hotkeys = overlay.entry.hotkeys
        const target = index + delta
        if (target < 0 || target >= hotkeys.length)
            return
        const moved = hotkeys.splice(index, 1)[0]
        hotkeys.splice(target, 0, moved)
        overlay.hotkeyRevision++
    }
}
