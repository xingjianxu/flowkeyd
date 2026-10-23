pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 「高级」页：注入后端、实例与权限、远程桌面放行、窗口缺省显示器、配置文件位置。
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
        title: qsTr("切换虚拟桌面")
        subtitle: qsTr("用 `desktop` 动作切虚拟桌面时，如果前台窗口不在光标所在的那块显示器上，就把它一起带到新桌面上（窗口的位置与显示器都不变）。")

        BoolRow {
            Layout.fillWidth: true
            label: qsTr("带着前台窗口一起切")
            hint: qsTr("只在窗口不在鼠标那块屏上时生效；在鼠标那块屏上的窗口不搬。默认开。")
            onText: qsTr("跟着走")
            offText: qsTr("不搬窗口")
            value: page.editor ? page.editor.settings.desktop_follow_foreground !== false : true
            onPicked: function (value) { page.editor.setSetting("desktop_follow_foreground", value) }
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
}
