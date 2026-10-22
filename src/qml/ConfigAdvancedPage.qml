pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 「高级」页：注入后端、实例与权限、远程桌面放行、窗口缺省显示器、工作空间、配置文件位置。
//
// 这一页的东西改动之后多半要**重启**才生效（提权、输入后端），所以每条都写清楚了。
ColumnLayout {
    id: page

    property var editor: null

    width: parent ? parent.width : 0
    spacing: 16

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("输入注入")
        subtitle: qsTr("flowkeyd 自己发出去的按键（`send` / `type` / 重映射）走哪条路。改完要重启生效。")

        ChoiceRow {
            Layout.fillWidth: true
            label: qsTr("注入后端")
            hint: qsTr("auto 会优先用未公开的 win32u!NtUserSendInput，失败再退回 user32")
            options: page.editor ? page.editor.inputBackends : []
            labels: ConfigText.labels("inputBackend", options)
            value: page.editor ? page.editor.settings.input_backend : "auto"
            onPicked: function (value) { page.editor.setSetting("input_backend", value) }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("实例与权限")

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("单实例")
            hint: qsTr("同一份配置文件只允许一个 flowkeyd；关掉之后可以同时跑好几个（每个都会装钩子，一般不要关）")
            onText: qsTr("只允许一个")
            offText: qsTr("允许多个")
            value: page.editor ? page.editor.settings.single_instance !== false : true
            onPicked: function (value) { page.editor.setSetting("single_instance", value) }
        }

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("自动提权")
            hint: qsTr("以守护进程模式启动时，没有管理员权限就弹 UAC 重启自己；关掉之后动作无法操作管理员权限的窗口")
            value: page.editor ? page.editor.settings.elevate !== false : true
            onPicked: function (value) { page.editor.setSetting("elevate", value) }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("窗口缺省显示器")
        subtitle: qsTr("窗口规则与 `window` 动作都**没有**写明 `monitor` 时，窗口摆到哪一块屏上。")

        TextRow {
            Layout.fillWidth: true
            label: qsTr("缺省目标")
            hint: qsTr("cursor 光标所在的那一块 / last 这个程序上次被摆到的那一块 / none 不碰位置 / 序号（1 起）/ primary / DISPLAY2")
            value: page.editor && page.editor.settings.window_monitor !== undefined
                ? String(page.editor.settings.window_monitor) : "cursor"
            onEdited: function (value) { page.editor.setSetting("window_monitor", value.length > 0 ? value : "cursor") }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 144
            spacing: 6

            Button {
                text: "cursor"
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                onClicked: page.editor.setSetting("window_monitor", "cursor")
            }
            Button {
                text: "last"
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                onClicked: page.editor.setSetting("window_monitor", "last")
            }
            Button {
                text: "none"
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                onClicked: page.editor.setSetting("window_monitor", "none")
            }
            Button {
                text: "primary"
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                onClicked: page.editor.setSetting("window_monitor", "primary")
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("远程桌面放行")
        subtitle: qsTr("前台窗口属于远程桌面客户端（mstsc / Windows App 等）时，所有快捷键与重映射一律放行 —— 键原样送到对面那台机器。")

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("启用放行")
            value: page.editor ? page.editor.settings.remote_desktop !== false : true
            onPicked: function (value) { page.editor.setSetting("remote_desktop", value) }
        }

        SettingRow {
            Layout.fillWidth: true
            label: qsTr("视为远程桌面的进程")
            hint: qsTr("一行一个，按可执行文件名做大小写无关的子串匹配；清空表示谁都不算（等于关掉这项功能）")

            TextArea {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(380, parent.width)
                height: 76
                wrapMode: TextEdit.NoWrap
                font.family: "Microsoft YaHei"
                font.pointSize: 9.5
                // 还在用内置名单时，把内置的那一份当作占位文字显示出来
                // （而不是真的写成自定义名单 —— 那样以后新增的 RDP 客户端就
                // 不会自动生效了）。
                placeholderText: page.editor
                    && page.editor.settings.remote_desktop_processes === undefined
                    ? page.editor.defaultRemoteDesktopProcesses.join("\n") : ""
                text: page.remoteDesktopProcessText()
                onTextEdited: page.remoteDesktopProcessEdited(text)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 144

            Label {
                text: page.editor && page.editor.settings.remote_desktop_processes === undefined
                    ? qsTr("当前用内置名单：%1").arg(
                          page.editor.defaultRemoteDesktopProcesses.join("、"))
                    : qsTr("当前用自定义名单")
                color: page.palette.placeholderText
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                elide: Text.ElideRight
                Layout.maximumWidth: 320
            }

            Button {
                text: qsTr("恢复内置名单")
                enabled: page.editor !== null
                    && page.editor.settings.remote_desktop_processes !== undefined
                font.family: "Microsoft YaHei"
                font.pointSize: 9
                onClicked: page.editor.resetRemoteDesktopProcesses()
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("工作空间")
        subtitle: qsTr("按显示器独立的一组工作空间（Hyprland 风格）：每块屏各自切换、各自记住窗口归属，只影响那几块屏上的普通程序窗口。与系统的虚拟桌面互不干扰。")

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("启用")
            hint: qsTr("关掉就等于没有这个功能；开着但没有绑定快捷键时也只是跟着记窗口归属，不会隐藏任何窗口")
            value: page.workspace("enabled", true) !== false
            onPicked: function (value) { page.editor.setWorkspaceSetting("enabled", value) }
        }

        SpinRow {
            Layout.fillWidth: true
            label: qsTr("每块屏几个空间")
            hint: qsTr("1–10，默认 9；改小之后超出范围的空间会并入最后一个")
            value: page.workspace("count", 9)
            from: 1
            to: 10
            onPicked: function (value) { page.editor.setWorkspaceSetting("count", value) }
        }

        ChoiceRow {
            Layout.fillWidth: true
            label: qsTr("切换作用在哪块屏")
            options: page.editor ? page.editor.workspaceSwitchTargets : []
            labels: ConfigText.labels("workspaceSwitchTarget", options)
            value: page.workspace("switch_target", "cursor")
            onPicked: function (value) { page.editor.setWorkspaceSetting("switch_target", value) }
        }

        ChoiceRow {
            Layout.fillWidth: true
            label: qsTr("切换效果")
            hint: qsTr("「自动」只在 Chromium 系程序（Chrome / Edge / VSCode 等）会闪时才盖住画面，其它程序没有延迟；「立即」最快但会闪一下；「冻结画面」一律盖，代价是慢约 100 ms")
            options: page.editor ? page.editor.workspaceSwitchEffects : []
            labels: ConfigText.labels("workspaceSwitchEffect", options)
            value: page.workspace("switch_effect", "auto")
            onPicked: function (value) { page.editor.setWorkspaceSetting("switch_effect", value) }
        }

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("新窗口进当前活动空间")
            hint: qsTr("关掉之下可以指定一个固定空间（单个程序用窗口规则里的「指定工作空间」更好）")
            value: page.workspace("new_window", "active") === "active"
            onPicked: {
                if (value)
                    page.editor.setWorkspaceSetting("new_window", "active")
                else
                    page.editor.setWorkspaceSetting("new_window", 1)
            }
        }

        SpinRow {
            Layout.fillWidth: true
            visible: page.workspace("new_window", "active") !== "active"
            label: qsTr("新窗口进第几个空间")
            value: page.workspace("new_window", 1) === "active" ? 1 : page.workspace("new_window", 1)
            from: 1
            to: 10
            onPicked: function (value) { page.editor.setWorkspaceSetting("new_window", value) }
        }

        SettingRow {
            Layout.fillWidth: true
            label: qsTr("排除的程序")
            hint: qsTr("一行一个，按可执行文件名做大小写无关的子串匹配；这些程序的窗口永远不会被隐藏")

            TextArea {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(380, parent.width)
                height: 76
                wrapMode: TextEdit.NoWrap
                font.family: "Microsoft YaHei"
                font.pointSize: 9.5
                text: page.workspaceExcludeText()
                onTextEdited: page.workspaceExcludeEdited(text)
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("配置文件")
        subtitle: qsTr("在这里保存之后，文件会被整体重写成一份带标记的 Lua 配置，旧的那份留在旁边的 `.bak` 里。")

        SettingRow {
            Layout.fillWidth: true
            label: qsTr("位置")

            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                text: page.editor ? page.editor.path : ""
                color: page.palette.placeholderText
                font.family: "Microsoft YaHei"
                font.pointSize: 9.5
                elide: Text.ElideMiddle
            }
        }
    }

    function remoteDesktopProcessText() {
        if (!page.editor)
            return ""
        const list = page.editor.settings.remote_desktop_processes
        return list === undefined ? "" : list.join("\n")
    }

    function remoteDesktopProcessEdited(text) {
        if (!page.editor)
            return
        const lines = text.length === 0 ? [] : text.split("\n")
        const processes = []
        for (let i = 0; i < lines.length; ++i) {
            const trimmed = lines[i].trim()
            if (trimmed.length > 0)
                processes.push(trimmed)
        }
        page.editor.setSetting("remote_desktop_processes", processes)
    }

    // ---- 工作空间 --------------------------------------------------------

    /// 读 `settings.workspace` 里的一个字段（没写过时用 `fallback`）。
    function workspace(key, fallback) {
        if (!page.editor)
            return fallback
        const table = page.editor.settings.workspace
        if (table === undefined || table === null)
            return fallback
        const value = table[key]
        return value === undefined ? fallback : value
    }

    function workspaceExcludeText() {
        const list = page.workspace("exclude", [])
        return Array.isArray(list) ? list.join("\n") : ""
    }

    function workspaceExcludeEdited(text) {
        if (!page.editor)
            return
        const lines = text.length === 0 ? [] : text.split("\n")
        const processes = []
        for (let i = 0; i < lines.length; ++i) {
            const trimmed = lines[i].trim()
            if (trimmed.length > 0)
                processes.push(trimmed)
        }
        // 清空 = 删掉这个键（而不是写一个空表）：空表在配置里是“没有排除名单”，
        // 写进去会让以后改默认值时不生效。
        page.editor.setWorkspaceSetting("exclude", processes.length > 0 ? processes : undefined)
    }
}
