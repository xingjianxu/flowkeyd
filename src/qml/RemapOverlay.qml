pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 编辑一条 `remap{}`。
OverlayCard {
    id: overlay

    title: overlay.index >= 0 ? qsTr("编辑按键重映射") : qsTr("新增按键重映射")
    note: qsTr("把按下的一个键换成另一个键，或者换成一段按键脚本。")
    cardWidth: 660
    cardHeight: 460

    onEntryChanged: overlay.ensure()

    TextRow {
        width: parent.width
        label: qsTr("原按键")
        placeholder: qsTr("例如 CapsLock")
        value: overlay.entry.from !== undefined ? overlay.entry.from : ""
        onEdited: function (value) { overlay.entry.from = value }
    }

    TextRow {
        width: parent.width
        label: qsTr("换成")
        hint: qsTr("按键名（Esc）或按键脚本（^{c}）；不能是定时脚本")
        value: overlay.entry.to !== undefined ? overlay.entry.to : ""
        onEdited: function (value) { overlay.entry.to = value }
    }

    ChoiceRow {
        width: parent.width
        label: qsTr("行为")
        options: overlay.editor ? overlay.editor.remapModes : []
        labels: ConfigText.labels("remapMode", options)
        value: overlay.entry.mode !== undefined ? overlay.entry.mode : "hold"
        onPicked: function (value) { overlay.entry.mode = value }
    }

    TriRow {
        width: parent.width
        label: qsTr("吞掉原按键")
        hint: qsTr("跟随默认时用全局设置（常规页里的「默认吞掉按键」）")
        value: overlay.entry.swallow
        onPicked: function (value) { overlay.setOptional("swallow", value) }
    }

    TriRow {
        width: parent.width
        label: qsTr("远程桌面里也生效")
        value: overlay.entry.remote_desktop
        onPicked: function (value) { overlay.setOptional("remote_desktop", value) }
    }

    TextRow {
        width: parent.width
        label: qsTr("名称")
        hint: qsTr("只用在日志与列表里")
        value: overlay.entry.name !== undefined ? overlay.entry.name : ""
        onEdited: function (value) { overlay.setOptional("name", value.length > 0 ? value : undefined) }
    }

    BoolRow {
        width: parent.width
        label: qsTr("启用")
        value: overlay.entry.enabled !== false
        onPicked: function (value) { overlay.entry.enabled = value }
    }

    validate: function () {
        if (!overlay.entry.from || overlay.entry.from.length === 0)
            return qsTr("请填「原按键」。")
        if (!overlay.entry.to || overlay.entry.to.length === 0)
            return qsTr("请填「换成」。")
        // 把 `from` 自己算进「已经被用了」的那一份，于是只检查它是不是一个合法的键。
        const fromProblem = overlay.editor
            ? overlay.editor.chordError(overlay.entry.from, [overlay.entry.from]) : ""
        if (fromProblem.length > 0)
            return qsTr("原按键：%1").arg(fromProblem)
        const toProblem = overlay.editor ? overlay.editor.keyOrScriptError(overlay.entry.to) : ""
        if (toProblem.length > 0)
            return qsTr("换成：%1").arg(toProblem)
        return ""
    }

    function setOptional(key, value) {
        if (value === undefined)
            delete overlay.entry[key]
        else
            overlay.entry[key] = value
    }

    function ensure() {
        if (overlay.entry.from === undefined)
            overlay.entry.from = ""
        if (overlay.entry.to === undefined)
            overlay.entry.to = ""
        if (overlay.entry.enabled === undefined)
            overlay.entry.enabled = true
    }
}
