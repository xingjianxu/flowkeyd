pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `launch` 表的字段块（`window` 动作的 `launch` 与 `app` 的 `launch` 共用）。
//
// `spec` 就是那个 JS 对象本身，**就地修改**；调用方拿到的就是改过的那一份。
// 这里刻意不碰 `program` 以外的必填判断：校验由 `core::compile()` 统一做，界面不
// 重复实现一遍规则（否则两边的说法迟早会不一致）。
Column {
    id: launch

    property var spec: ({})
    /// 调用方自己也要显示一个「等窗口出现」时，把这个关掉（`window` 动作的
    /// 动作级 `wait_ms` 优先于 `launch.wait_ms`，两个一起显示只会让人困惑）。
    property bool showWaitMs: true

    width: parent ? parent.width : 0
    spacing: 6

    TextRow {
        width: launch.width
        label: qsTr("程序")
        placeholder: qsTr("例如 C:\\tools\\app.exe")
        value: launch.spec.program !== undefined ? launch.spec.program : ""
        onEdited: function (value) { launch.spec.program = value }
    }

    SettingRow {
        width: launch.width
        label: qsTr("参数")
        hint: qsTr("一行一个参数")
        TextField {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(340, parent.width)
            text: launch.linesToText(launch.spec.args)
            onTextEdited: launch.spec.args = launch.textToLines(text)
        }
    }

    TextRow {
        width: launch.width
        label: qsTr("工作目录")
        value: launch.spec.cwd !== undefined ? launch.spec.cwd : ""
        onEdited: {
            if (value.length > 0)
                launch.spec.cwd = value
            else
                delete launch.spec.cwd
        }
    }

    ChoiceRow {
        width: launch.width
        label: qsTr("窗口状态")
        options: launch.editor ? launch.editor.showModes : []
        labels: ConfigText.labels("show", options)
        value: launch.spec.show !== undefined ? launch.spec.show : "normal"
        onPicked: function (value) { launch.spec.show = value }
    }

    BoolRow {
        width: launch.width
        label: qsTr("用 cmd.exe 执行")
        value: launch.spec.shell === true
        onPicked: function (value) { launch.spec.shell = value }
    }

    SettingRow {
        width: launch.width
        label: qsTr("环境变量")
        hint: qsTr("一行一个 K=V")
        TextField {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(340, parent.width)
            text: launch.envText(launch.spec.env)
            onTextEdited: launch.spec.env = launch.envMap(text)
        }
    }

    SpinRow {
        width: launch.width
        visible: launch.showWaitMs
        label: qsTr("等窗口出现")
        hint: qsTr("最多等多久，默认 3000")
        value: launch.spec.wait_ms !== undefined ? launch.spec.wait_ms : 3000
        from: 0
        to: 60000
        stepSize: 100
        suffix: " ms"
        onPicked: function (value) { launch.spec.wait_ms = value }
    }

    /// `ConfigEditor`，用来取下拉框取值（`show`）。
    property var editor: null

    /// 字符串列表 <-> 多行输入框（一行一项；空行会被丢掉）。
    /// `Array.isArray` 的检查是必需的：这一张表的 `args` 在别处（`open` 动作）可能是
    /// 一个字符串，而不可见的绑定也会被求值。
    function linesToText(list) {
        if (!list || !Array.isArray(list))
            return ""
        return list.join("\n")
    }

    function textToLines(text) {
        if (text.length === 0)
            return []
        return text.split("\n").filter(function (line) {
            return line.length > 0
        })
    }

    function envText(map) {
        if (!map)
            return ""
        const parts = []
        for (const key in map)
            parts.push(key + "=" + map[key])
        return parts.join("\n")
    }

    function envMap(text) {
        const out = {}
        const lines = text.length === 0 ? [] : text.split("\n")
        for (let i = 0; i < lines.length; ++i) {
            const at = lines[i].indexOf("=")
            if (at <= 0)
                continue
            out[lines[i].substring(0, at).trim()] = lines[i].substring(at + 1)
        }
        return out
    }
}
