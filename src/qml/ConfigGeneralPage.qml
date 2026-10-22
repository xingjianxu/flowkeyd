pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 「常规」页：日志、按键行为、引擎节奏。
//
// 这一页只放**全局缺省值** —— 它们决定没有显式写过的那些条目怎么表现。
ColumnLayout {
    id: page

    property var editor: null

    width: parent ? parent.width : 0
    spacing: 16

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("日志")
        subtitle: qsTr("日志文件在配置目录下的 flowkeyd.log；托盘菜单的「查看日志」看的就是它。")

        ChoiceRow {
            Layout.fillWidth: true
            label: qsTr("日志级别")
            options: page.editor ? page.editor.logLevels : []
            labels: ConfigText.labels("logLevel", options)
            value: page.editor ? page.editor.settings.log_level : "info"
            onPicked: function (value) { page.editor.setSetting("log_level", value) }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("按键行为")
        subtitle: qsTr("这些是单条快捷键没有显式写 `swallow` 时用的缺省值。")

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("默认吞掉按键")
            onText: qsTr("吞掉（前台应用收不到）")
            offText: qsTr("放行（前台应用也收到）")
            value: page.editor ? page.editor.settings.swallow !== false : true
            onPicked: function (value) { page.editor.setSetting("swallow", value) }
        }

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("严格匹配修饰键")
            hint: qsTr("关掉时：按住 Alt 再按 Ctrl+h 也会触发 Ctrl+h（AutoHotkey 行为）")
            onText: qsTr("必须完全一致")
            offText: qsTr("多按了别的修饰键也算")
            value: page.editor ? page.editor.settings.exact_modifiers === true : false
            onPicked: function (value) { page.editor.setSetting("exact_modifiers", value) }
        }

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("发送前松开修饰键")
            hint: qsTr("执行 send / type 之前先松开你正按着的 Ctrl/Alt/Shift/Win，之后再按回来")
            value: page.editor ? page.editor.settings.release_modifiers !== false : true
            onPicked: function (value) { page.editor.setSetting("release_modifiers", value) }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("引擎节奏")
        subtitle: qsTr("一般不用动；只有在「按住重复」的手感不对时才需要调。")

        SpinRow {
            Layout.fillWidth: true
            label: qsTr("重复间隔")
            hint: qsTr("按住不放时每隔多久执行一次，默认 50")
            value: page.editor ? page.editor.settings.repeat_interval_ms : 50
            from: 1
            to: 5000
            suffix: " ms"
            onPicked: function (value) { page.editor.setSetting("repeat_interval_ms", value) }
        }

        SpinRow {
            Layout.fillWidth: true
            label: qsTr("开始重复前等待")
            hint: qsTr("按住之后先等这么久再开始重复，默认 400")
            value: page.editor ? page.editor.settings.repeat_delay_ms : 400
            from: 0
            to: 5000
            suffix: " ms"
            onPicked: function (value) { page.editor.setSetting("repeat_delay_ms", value) }
        }

        SpinRow {
            Layout.fillWidth: true
            label: qsTr("引擎滴答周期")
            hint: qsTr("内部定时器粒度，默认 15；越小越精确，也越费 CPU")
            value: page.editor ? page.editor.settings.tick_ms : 15
            from: 1
            to: 1000
            suffix: " ms"
            onPicked: function (value) { page.editor.setSetting("tick_ms", value) }
        }
    }
}
