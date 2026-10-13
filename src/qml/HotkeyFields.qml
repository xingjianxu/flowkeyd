pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 一条快捷键的字段块（全局的 `hotkey{}` 与 `app` 里的每一条都用它）。
//
// `hotkey` 就是那个 JS 对象本身，**就地修改**。`compact` 用来省掉 `app` 里那几条
// 很少用得上的字段（名称 / 备注 / 启用）—— 一个程序下面的每条绑定都重复问一遍
// 反而不好读。
Column {
    id: fields

    property var editor: null
    property var hotkey: ({})
    property bool compact: false

    /// 有没有 `on_release` 那一段。因为空的动作列表是合法的「没有」，
    /// 所以「用户刚打开这个开关」得单独记一个状态。
    property bool hasRelease: hotkey.release_actions !== undefined
        && hotkey.release_actions.length > 0
    /// 「按住重复」同理：`repeatable` 没写过 / 写成 `false` 都算关。
    property bool repeatOn: hotkey.repeatable !== undefined && hotkey.repeatable !== false

    width: parent ? parent.width : 0
    spacing: 6

    onHotkeyChanged: fields.ensure()

    component Head: Label {
        width: fields.width
        height: 24
        color: fields.palette.text
        font.family: "Microsoft YaHei"
        font.pointSize: 11
        font.bold: true
        verticalAlignment: Text.AlignVCenter
    }

    Head {
        text: qsTr("按键")
    }

    TextRow {
        width: fields.width
        visible: !fields.compact
        label: qsTr("名称")
        hint: qsTr("只用在日志与帮助窗口里")
        value: fields.hotkey.name !== undefined ? fields.hotkey.name : ""
        onEdited: function (value) { fields.setOptional("name", value.length > 0 ? value : undefined) }
    }

    Column {
        width: fields.width
        spacing: 6

        Label {
            width: parent.width
            text: qsTr("和弦")
            color: fields.palette.text
            font.family: "Microsoft YaHei"
            font.pointSize: 10.5
        }

        ChordsEditor {
            width: parent.width
            chords: fields.hotkey.keys
            editor: fields.editor
        }
    }

    ChoiceRow {
        width: fields.width
        label: qsTr("触发时机")
        hint: qsTr("单个修饰键（例如 LWin）配「松开时」就是「轻碰」")
        options: fields.editor ? fields.editor.triggerModes : []
        labels: ConfigText.labels("trigger", options)
        value: fields.hotkey.trigger !== undefined ? fields.hotkey.trigger : "press"
        onPicked: function (value) { fields.hotkey.trigger = value }
    }

    TriRow {
        width: fields.width
        label: qsTr("吞掉按键")
        hint: qsTr("跟随默认时用全局设置（常规页里的「默认吞掉按键」）")
        value: fields.hotkey.swallow
        onPicked: function (value) { fields.setOptional("swallow", value) }
    }

    BoolRow {
        width: fields.width
        label: qsTr("按住重复")
        hint: qsTr("按住不放时反复执行；可以和「触发时机」的「按住重复」互相替代")
        value: fields.repeatOn
        onPicked: {
            fields.repeatOn = value
            if (value) {
                fields.hotkey.repeatable = true
            } else {
                delete fields.hotkey.repeatable
                delete fields.hotkey.repeat_interval_ms
                delete fields.hotkey.repeat_delay_ms
            }
        }
    }

    Column {
        width: fields.width
        spacing: 6
        visible: fields.repeatOn

        SpinRow {
            width: fields.width
            label: qsTr("重复间隔")
            hint: qsTr("0 表示用全局设置")
            value: fields.hotkey.repeat_interval_ms !== undefined ? fields.hotkey.repeat_interval_ms : 0
            from: 0
            to: 5000
            suffix: " ms"
            onPicked: {
                if (value > 0)
                    fields.hotkey.repeat_interval_ms = value
                else
                    delete fields.hotkey.repeat_interval_ms
            }
        }
        SpinRow {
            width: fields.width
            label: qsTr("开始重复前等待")
            hint: qsTr("0 表示用全局设置")
            value: fields.hotkey.repeat_delay_ms !== undefined ? fields.hotkey.repeat_delay_ms : 0
            from: 0
            to: 5000
            suffix: " ms"
            onPicked: {
                if (value > 0)
                    fields.hotkey.repeat_delay_ms = value
                else
                    delete fields.hotkey.repeat_delay_ms
            }
        }
    }

    TriRow {
        width: fields.width
        label: qsTr("远程桌面里也生效")
        value: fields.hotkey.remote_desktop
        onPicked: function (value) { fields.setOptional("remote_desktop", value) }
    }

    TextRow {
        width: fields.width
        visible: !fields.compact
        label: qsTr("备注")
        hint: qsTr("会显示在帮助窗口与 `--list` 里")
        value: fields.hotkey.comment !== undefined ? fields.hotkey.comment : ""
        onEdited: function (value) { fields.setOptional("comment", value.length > 0 ? value : undefined) }
    }

    BoolRow {
        width: fields.width
        visible: !fields.compact
        label: qsTr("启用")
        value: fields.hotkey.enabled !== false
        onPicked: function (value) { fields.hotkey.enabled = value }
    }

    Head {
        text: qsTr("按下时执行")
    }

    ActionListEditor {
        width: fields.width
        editor: fields.editor
        actions: fields.hotkey.actions
        addLabel: qsTr("添加动作")
    }

    BoolRow {
        width: fields.width
        label: qsTr("松开时另外执行")
        value: fields.hasRelease
        onPicked: {
            fields.hasRelease = value
            if (value) {
                fields.hotkey.release_actions = [fields.editor.newAction("none")]
            } else {
                delete fields.hotkey.release_actions
            }
        }
    }

    Column {
        width: fields.width
        spacing: 6
        visible: fields.hasRelease

        ActionListEditor {
            width: fields.width
            editor: fields.editor
            actions: fields.hotkey.release_actions ? fields.hotkey.release_actions : []
            addLabel: qsTr("添加动作")
        }
    }

    function setOptional(key, value) {
        if (value === undefined)
            delete fields.hotkey[key]
        else
            fields.hotkey[key] = value
    }

    function ensure() {
        if (!fields.hotkey)
            return
        if (!fields.hotkey.keys)
            fields.hotkey.keys = [""]
        if (!fields.hotkey.actions)
            fields.hotkey.actions = []
        if (fields.hotkey.enabled === undefined)
            fields.hotkey.enabled = true
        fields.hasRelease = fields.hotkey.release_actions !== undefined
            && fields.hotkey.release_actions.length > 0
        fields.repeatOn = fields.hotkey.repeatable !== undefined
            && fields.hotkey.repeatable !== false
    }
}
