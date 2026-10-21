pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// `window_rule` 的字段块（独立的规则与 `app` 里的 `window` 共用）。
//
// `rule` 就是那个 JS 对象本身，**就地修改**。几个「整块可选」的字段（指定虚拟桌面、
// 指定尺寸与位置）在配置里是「写没写这个键」，界面用一个开关去切换它，同时把状态
// 镜像到本组件自己的属性上 —— JS 对象的改动不会发通知，靠属性才能驱动 `visible`。
Column {
    id: fields

    property var rule: ({})
    property var editor: null

    property bool hasDesktop: rule.desktop !== undefined
    property bool hasWorkspace: rule.workspace !== undefined
    property bool hasGeometry: rule.x !== undefined || rule.y !== undefined
        || rule.width !== undefined || rule.height !== undefined
    property bool maximized: rule.maximize === true

    width: parent ? parent.width : 0
    spacing: 6

    TextRow {
        width: fields.width
        label: qsTr("名称")
        hint: qsTr("只用在日志与 `--list` 里")
        value: fields.rule.name !== undefined ? fields.rule.name : ""
        onEdited: {
            if (value.length > 0)
                fields.rule.name = value
            else
                delete fields.rule.name
        }
    }

    TextRow {
        width: fields.width
        label: qsTr("进程名包含")
        placeholder: qsTr("例如 wezterm")
        value: fields.rule.process !== undefined ? fields.rule.process : ""
        onEdited: {
            if (value.length > 0)
                fields.rule.process = value
            else
                delete fields.rule.process
        }
    }

    TextRow {
        width: fields.width
        label: qsTr("标题包含")
        placeholder: qsTr("留空表示不限制")
        value: fields.rule.title !== undefined ? fields.rule.title : ""
        onEdited: {
            if (value.length > 0)
                fields.rule.title = value
            else
                delete fields.rule.title
        }
    }

    TextRow {
        width: fields.width
        label: qsTr("摆到显示器")
        hint: qsTr("序号（1 起）／primary／DISPLAY2／cursor／last／none；留空则用全局设置")
        value: fields.rule.monitor !== undefined ? String(fields.rule.monitor) : ""
        onEdited: {
            if (value.length > 0)
                fields.rule.monitor = value
            else
                delete fields.rule.monitor
        }
    }

    BoolRow {
        width: fields.width
        label: qsTr("指定虚拟桌面")
        value: fields.hasDesktop
        onPicked: {
            fields.hasDesktop = value
            if (value)
                fields.rule.desktop = 1
            else
                delete fields.rule.desktop
        }
    }

    SpinRow {
        width: fields.width
        visible: fields.hasDesktop
        label: qsTr("第几张桌面")
        value: fields.rule.desktop !== undefined ? fields.rule.desktop : 1
        from: 1
        to: 64
        onPicked: function (value) { fields.rule.desktop = value }
    }

    TriRow {
        width: fields.width
        label: qsTr("钉在所有桌面")
        hint: qsTr("与「指定虚拟桌面」互斥")
        value: fields.rule.all_desktops
        onPicked: function (value) { fields.optional("all_desktops", value) }
    }

    BoolRow {
        width: fields.width
        label: qsTr("指定工作空间")
        hint: qsTr("这个程序的新窗口进第几个工作空间；不写就看全局设置")
        value: fields.hasWorkspace
        onPicked: {
            fields.hasWorkspace = value
            if (value)
                fields.rule.workspace = 1
            else
                delete fields.rule.workspace
        }
    }

    SpinRow {
        width: fields.width
        visible: fields.hasWorkspace
        label: qsTr("第几个工作空间")
        value: fields.rule.workspace !== undefined ? fields.rule.workspace : 1
        from: 1
        to: 10
        onPicked: function (value) { fields.rule.workspace = value }
    }

    TriRow {
        width: fields.width
        label: qsTr("始终在最上层")
        value: fields.rule.topmost
        onPicked: function (value) { fields.optional("topmost", value) }
    }

    TriRow {
        width: fields.width
        label: qsTr("最大化")
        hint: qsTr("与下面的尺寸／位置互斥")
        value: fields.rule.maximize
        onPicked: {
            fields.maximized = value === true
            fields.optional("maximize", value)
        }
    }

    BoolRow {
        width: fields.width
        label: qsTr("指定尺寸与位置")
        enabled: !fields.maximized
        value: fields.hasGeometry
        onPicked: {
            fields.hasGeometry = value
            if (value) {
                fields.rule.x = 0
                fields.rule.y = 0
                fields.rule.width = 1280
                fields.rule.height = 720
            } else {
                delete fields.rule.x
                delete fields.rule.y
                delete fields.rule.width
                delete fields.rule.height
            }
        }
    }

    Column {
        width: fields.width
        spacing: 6
        visible: fields.hasGeometry && !fields.maximized

        SpinRow {
            width: fields.width
            label: qsTr("左边距")
            hint: qsTr("相对目标显示器工作区左上角")
            value: fields.rule.x !== undefined ? fields.rule.x : 0
            from: -10000
            to: 10000
            suffix: " px"
            onPicked: function (value) { fields.rule.x = value }
        }
        SpinRow {
            width: fields.width
            label: qsTr("上边距")
            value: fields.rule.y !== undefined ? fields.rule.y : 0
            from: -10000
            to: 10000
            suffix: " px"
            onPicked: function (value) { fields.rule.y = value }
        }
        SpinRow {
            width: fields.width
            label: qsTr("宽度")
            value: fields.rule.width !== undefined ? fields.rule.width : 1280
            from: 1
            to: 10000
            suffix: " px"
            onPicked: function (value) { fields.rule.width = value }
        }
        SpinRow {
            width: fields.width
            label: qsTr("高度")
            value: fields.rule.height !== undefined ? fields.rule.height : 720
            from: 1
            to: 10000
            suffix: " px"
            onPicked: function (value) { fields.rule.height = value }
        }
    }

    BoolRow {
        width: fields.width
        label: qsTr("启用这条规则")
        value: fields.rule.enabled !== false
        onPicked: function (value) { fields.rule.enabled = value }
    }

    /// `TriRow` 的三态值写回：`undefined` = 这个键没写过。
    function optional(key, value) {
        if (value === undefined)
            delete fields.rule[key]
        else
            fields.rule[key] = value
    }
}
