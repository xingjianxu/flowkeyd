pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 图形化的配置界面（项目所有者 2026-10 要求）。
//
// 形态是 Windows 11 设置那种「左边导航、右边分组卡片」的**普通窗口**（不是那种
// 无边框的小卡片）：配置项有几十个，塞进一张弹窗只会又挤又难找；这是一个可以慢慢
// 改、来回对比的界面，应该能最大化、能进任务栏、能滚。
//
// 分工：
//   * **本文件只管界面与导航**，配置怎么读怎么写全在 `app::ConfigEditor`
//     （纯逻辑、有单测 `tst_config_editor`）；
//   * 每个条目的编辑是一个**盖在窗口上的弹层**（`HotkeyOverlay` 等），不是
//     `Dialog`：整套界面是 FluentWinUI3，自建弹层才能保证样式一致，而且弹层里要
//     塞一整套表单；
//   * **弹层每次都是新建的**（`Loader` + `sourceComponent`）：这样每个输入框都从
//     当前值初始化一次，「上一次编辑留下的状态」不可能串到下一次。
//
// 保存是**整体重写** `config.lua`（见 `core::config_writer`）：原文件先留一份
// `.bak`，而覆盖一份**不是界面生成的**文件之前会先问一句 —— 因为那意味着用户手写的
// 注释与 Lua 代码会被重写掉。
Window {
    id: root

    /// `app::ConfigEditor`（由 `app::ConfigWindow` 这个宿主通过 setProperty 挂上）。
    property var editor: null

    readonly property string uiFontFamily: "Microsoft YaHei"
    readonly property int navWidth: 200

    readonly property var pageTitles: [qsTr("常规"), qsTr("快捷键"), qsTr("按键重映射"),
                                       qsTr("窗口规则"), qsTr("程序"), qsTr("高级")]

    property int pageIndex: 0
    /// 保存成功之后显示一小会儿的提示。
    property string statusText: ""

    // ---- 弹层状态 ----
    /// 空串 = 没有弹层；否则是 hotkeys / remaps / window_rules / apps / confirm。
    property string overlayKind: ""
    property var overlayEntry: ({})
    property int overlayIndex: -1
    property string overlayMessage: ""
    /// 用户点了「确定」之后要干什么（确认框用它）。
    property string pendingAction: ""

    width: 1000
    height: 700
    minimumWidth: 820
    minimumHeight: 560
    visible: false
    title: qsTr("flowkeyd 设置")
    color: root.palette.window

    // ---------------------------------------------------------------------
    // 打开 / 关闭
    // ---------------------------------------------------------------------

    /// 关窗口时如果还有没保存的改动，先问一句（用确认弹层，不直接关）。
    onClosing: function (close) {
        if (root.editor && root.editor.dirty) {
            close.accepted = false
            root.askConfirm(qsTr("还有没保存的改动。要丢弃它们并关闭窗口吗？"), "discard")
        }
    }

    // ---------------------------------------------------------------------
    // 弹层
    // ---------------------------------------------------------------------

    /// 把弹层显示出来。
    ///
    /// **顺序很重要**：`overlayKind` 一变，`Loader` 就会**同步**把弹层建出来并跑
    /// `applyOverlay()`；所以要先备好数据、最后才切种类。反过来的话弹层拿到的是上一
    /// 次的（首次打开时是空的）内容 —— 界面看起来一切正常，但「确定」写回去的是一个
    /// 空条目（踩过，而且是静默的：没有任何警告）。
    function showOverlay(kind, entry, index, message, action) {
        if (root.overlayKind.length > 0)
            root.overlayKind = ""
        root.overlayIndex = index
        root.overlayEntry = entry
        root.overlayMessage = message
        root.pendingAction = action
        root.overlayKind = kind
    }

    function askConfirm(message, action) {
        root.showOverlay("confirm", ({}), -1, message, action)
    }

    function openEntry(kind, index) {
        if (!root.editor || !root.editor.loaded)
            return
        const entries = root.entriesOf(kind)
        if (index >= 0 && index >= entries.length)
            return
        root.showOverlay(kind, index >= 0 ? entries[index] : root.freshEntry(kind), index, "", "")
    }

    function overlayComponent() {
        if (root.overlayKind === "hotkeys")
            return hotkeyComponent
        if (root.overlayKind === "remaps")
            return remapComponent
        if (root.overlayKind === "window_rules")
            return windowRuleComponent
        if (root.overlayKind === "apps")
            return appComponent
        return confirmComponent
    }

    function applyOverlay() {
        const item = overlayLoader.item
        if (!item)
            return
        // `editor` 排在 `entry` 前面：条目一挂上去，子组件就会用它做初始化。
        item.editor = root.editor
        item.index = root.overlayIndex
        item.message = root.overlayMessage
        item.action = root.pendingAction
        item.entry = root.overlayEntry
        item.accepted.connect(root.overlayAccepted)
        item.cancelled.connect(root.overlayCancelled)
    }

    // ---------------------------------------------------------------------
    // 列表页要用的入口（`ConfigEntryListPage` 通过 `host` 调它们）
    // ---------------------------------------------------------------------

    function entriesOf(kind) {
        if (!root.editor)
            return []
        if (kind === "hotkeys")
            return root.editor.hotkeys
        if (kind === "remaps")
            return root.editor.remaps
        if (kind === "window_rules")
            return root.editor.windowRules
        return root.editor.apps
    }

    function freshEntry(kind) {
        if (kind === "hotkeys")
            return root.editor.newHotkey()
        if (kind === "remaps")
            return root.editor.newRemap()
        if (kind === "window_rules")
            return root.editor.newWindowRule()
        return root.editor.newApp()
    }

    function duplicateEntry(kind, index) {
        if (!root.editor)
            return
        if (kind === "hotkeys") {
            root.editor.duplicateHotkey(index)
            root.statusText = qsTr("已复制一条快捷键（和弦换成了一条没被占用的），别忘了保存")
            return
        }
        const entries = root.entriesOf(kind)
        if (index < 0 || index >= entries.length)
            return
        // `JSON` 往返就是一张干净的深拷贝；`undefined` 的键会被丢掉，而这正好是
        // 我们要的语义（“没写过”而不是“写了一个空值”）。
        const copy = JSON.parse(JSON.stringify(entries[index]))
        delete copy._title
        delete copy._subtitle
        if (kind === "remaps")
            copy.from = ""
        else if (kind === "window_rules")
            copy.process = ""
        else
            copy.process = ""
        if (kind === "remaps")
            root.editor.appendRemap(copy)
        else if (kind === "window_rules")
            root.editor.appendWindowRule(copy)
        else
            root.editor.appendApp(copy)
        root.statusText = qsTr("已复制一条，别忘了保存")
    }

    function removeEntry(kind, index) {
        root.askConfirm(qsTr("确定删除这一条吗？（保存之前都可以用「还原」撤销）"),
                        "remove:" + kind + ":" + index)
    }

    function moveEntry(kind, index, delta) {
        if (!root.editor)
            return
        if (kind === "hotkeys")
            root.editor.moveHotkey(index, delta)
        else if (kind === "remaps")
            root.editor.moveRemap(index, delta)
        else if (kind === "window_rules")
            root.editor.moveWindowRule(index, delta)
        else
            root.editor.moveApp(index, delta)
    }

    // ---------------------------------------------------------------------
    // 保存 / 还原
    // ---------------------------------------------------------------------

    function requestSave() {
        if (!root.editor || !root.editor.loaded)
            return
        if (!root.editor.editorGenerated) {
            root.askConfirm(qsTr("这个配置文件里有界面之外的内容（注释、循环、变量）。"
                                 + "在这里保存会把整个文件重写成界面生成的那一份；原文件会留一份 .bak。要继续吗？"),
                            "overwrite")
            return
        }
        root.doSave()
    }

    function doSave() {
        if (!root.editor)
            return
        root.statusText = ""
        root.editor.save()
    }

    function runPendingAction() {
        const action = root.pendingAction
        root.pendingAction = ""
        if (action === "overwrite") {
            root.doSave()
        } else if (action === "discard") {
            if (root.editor)
                root.editor.revert()
            root.close()
        } else if (action.startsWith("remove:")) {
            const parts = action.split(":")
            const kind = parts[1]
            const index = parseInt(parts[2], 10)
            if (kind === "hotkeys")
                root.editor.removeHotkey(index)
            else if (kind === "remaps")
                root.editor.removeRemap(index)
            else if (kind === "window_rules")
                root.editor.removeWindowRule(index)
            else
                root.editor.removeApp(index)
            root.statusText = qsTr("已修改，别忘了保存")
        }
    }

    // ---------------------------------------------------------------------
    // 弹层的收尾（打开在 `showOverlay()` 里，它们就在上面）
    // ---------------------------------------------------------------------

    function closeOverlay() {
        root.overlayKind = ""
        root.overlayEntry = ({})
        root.overlayIndex = -1
        root.overlayMessage = ""
        root.pendingAction = ""
    }

    function overlayAccepted() {
        const kind = root.overlayKind
        if (kind === "confirm") {
            // 先把弹层收掉（延后销毁：现在还在它自己的信号里），再执行。
            const action = root.pendingAction
            root.closeOverlay()
            Qt.callLater(function () {
                root.pendingAction = action
                root.runPendingAction()
            })
            return
        }
        const entry = root.overlayEntry
        const index = root.overlayIndex
        if (kind === "hotkeys") {
            if (index >= 0)
                root.editor.updateHotkey(index, entry)
            else
                root.editor.appendHotkey(entry)
        } else if (kind === "remaps") {
            if (index >= 0)
                root.editor.updateRemap(index, entry)
            else
                root.editor.appendRemap(entry)
        } else if (kind === "window_rules") {
            if (index >= 0)
                root.editor.updateWindowRule(index, entry)
            else
                root.editor.appendWindowRule(entry)
        } else if (kind === "apps") {
            if (index >= 0)
                root.editor.updateApp(index, entry)
            else
                root.editor.appendApp(entry)
        }
        root.closeOverlay()
        root.statusText = qsTr("已修改，别忘了保存")
    }

    function overlayCancelled() {
        root.closeOverlay()
    }

    // ---------------------------------------------------------------------
    // 界面
    // ---------------------------------------------------------------------

    // 顶部标题栏区域：页面名 + 当前配置概览 + 「没保存」提示。
    Rectangle {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 64
        color: root.palette.window

        Label {
            id: headerTitle

            x: 20
            y: 12
            width: parent.width - 40
            height: 26
            text: root.pageTitles[Math.min(root.pageIndex, root.pageTitles.length - 1)]
            color: root.palette.text
            font.family: root.uiFontFamily
            font.pointSize: 15
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }

        Label {
            x: 20
            y: 38
            width: parent.width - 40
            height: 18
            text: root.editor ? root.editor.summary : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 9
            verticalAlignment: Text.AlignVCenter
        }
    }

    // 左边导航。
    Rectangle {
        id: nav

        anchors.left: parent.left
        anchors.top: header.bottom
        anchors.bottom: footer.top
        width: root.navWidth
        color: root.palette.window

        ListView {
            id: navList

            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            clip: true
            model: root.pageTitles
            currentIndex: root.pageIndex

            delegate: ItemDelegate {
                id: navItem

                required property int index
                required property string modelData

                width: navList.width
                height: 40
                highlighted: navItem.index === root.pageIndex
                onClicked: root.pageIndex = navItem.index

                contentItem: Label {
                    leftPadding: 12
                    text: navItem.modelData
                    color: navItem.palette.text
                    font.family: root.uiFontFamily
                    font.pointSize: 11
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    // 右边内容：所有页面装在同一个 `StackLayout` 里，外面一个 `ScrollView`。
    // 卡片高度不一样，所以整页可滚；`StackLayout` 保证只有当前页占位。
    ScrollView {
        id: pageScroll

        anchors.left: nav.right
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.leftMargin: 8
        anchors.rightMargin: 20
        clip: true

        StackLayout {
            id: pages

            width: pageScroll.availableWidth
            currentIndex: root.pageIndex

            ConfigGeneralPage {
                editor: root.editor
            }

            ConfigEntryListPage {
                editor: root.editor
                host: root
                kind: "hotkeys"
                entries: root.editor ? root.editor.hotkeys : []
                addLabel: qsTr("新增快捷键")
                canDuplicate: true
                emptyHint: qsTr("还没有任何快捷键。点上面的「新增快捷键」，或者把 `flowkeyd.lua.example` 里的例子抄过来。")
            }

            ConfigEntryListPage {
                editor: root.editor
                host: root
                kind: "remaps"
                entries: root.editor ? root.editor.remaps : []
                addLabel: qsTr("新增按键重映射")
                emptyHint: qsTr("还没有按键重映射。最经典的一条是 CapsLock → Esc。")
            }

            ConfigEntryListPage {
                editor: root.editor
                host: root
                kind: "window_rules"
                entries: root.editor ? root.editor.windowRules : []
                addLabel: qsTr("新增窗口规则")
                headerExtra: windowMonitorCard
                emptyHint: qsTr("还没有窗口规则。规则决定某个程序的窗口第一次出现时摆在哪张虚拟桌面、哪块显示器上。")
            }

            ConfigEntryListPage {
                editor: root.editor
                host: root
                kind: "apps"
                entries: root.editor ? root.editor.apps : []
                addLabel: qsTr("新增程序")
                emptyHint: qsTr("还没有配置任何程序。程序条目把「某个程序的窗口摆放」与「它的快捷键」写在一起。")
            }

            ConfigAdvancedPage {
                editor: root.editor
            }
        }
    }

    // 底部：状态 / 报错 + 三个按钮。
    Rectangle {
        id: footer

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 58
        color: root.palette.window

        Label {
            id: errorLabel

            x: 20
            y: 6
            width: parent.width - 40
            height: 20
            visible: root.editor && root.editor.saveError.length > 0
            text: root.editor ? root.editor.saveError : ""
            color: "#D13438"
            font.family: root.uiFontFamily
            font.pointSize: 9.5
            elide: Text.ElideMiddle
        }

        Label {
            id: statusLabel

            x: 20
            y: 28
            width: parent.width - 40
            height: 20
            text: {
                if (root.editor && root.editor.saveError.length > 0)
                    return qsTr("配置没有保存 —— 上面那条错误需要先解决。")
                if (root.editor && root.editor.dirty)
                    return qsTr("有未保存的改动。保存之后 flowkeyd 会立刻重载配置。")
                return root.statusText.length > 0 ? root.statusText
                                                  : qsTr("改动在保存之后立刻生效。")
            }
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 9
            elide: Text.ElideRight
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            layoutDirection: Qt.LeftToRight

            Button {
                text: qsTr("还原")
                enabled: root.editor !== null && root.editor.dirty
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: if (root.editor) root.editor.revert()
            }

            Button {
                highlighted: true
                text: qsTr("保存")
                enabled: root.editor !== null && root.editor.loaded
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: root.requestSave()
            }

            Button {
                text: qsTr("关闭")
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: root.close()
            }
        }
    }

    // 「读不出来」时盖住整页：那种情况下没有任何东西可编辑。
    Rectangle {
        anchors.fill: parent
        visible: root.editor !== null && !root.editor.loaded
        color: root.palette.window

        Column {
            anchors.centerIn: parent
            width: Math.min(root.width - 80, 620)
            spacing: 14

            Label {
                width: parent.width
                text: qsTr("这份配置读不出来，界面没法编辑它。")
                color: root.palette.text
                font.family: root.uiFontFamily
                font.pointSize: 12.5
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Label {
                width: parent.width
                text: root.editor ? root.editor.loadError : ""
                color: "#D13438"
                font.family: root.uiFontFamily
                font.pointSize: 9.5
                wrapMode: Text.WordWrap
            }

            Label {
                width: parent.width
                text: qsTr("可以先按报错修好配置文件（用托盘菜单的「打开配置文件」），"
                           + "或者从一份空白配置重新开始（原文件会留一份 .bak）。")
                color: root.palette.placeholderText
                font.family: root.uiFontFamily
                font.pointSize: 9.5
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8

                Button {
                    text: qsTr("从空白配置开始")
                    highlighted: true
                    font.family: root.uiFontFamily
                    font.pointSize: 10
                    onClicked: if (root.editor) root.editor.startFromDefaults()
                }

                Button {
                    text: qsTr("关闭")
                    font.family: root.uiFontFamily
                    font.pointSize: 10
                    onClicked: root.close()
                }
            }
        }
    }

    // 校验过不去时的一条提示（文件读得出来，但 `--check` 有意见）。
    Label {
        anchors.left: nav.right
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 20
        height: 22
        visible: root.editor !== null && root.editor.loaded
            && root.editor.loadError.length > 0
        text: qsTr("注意：当前配置通不过校验 —— %1").arg(
                  root.editor ? root.editor.loadError.replace(/\s+/g, " ").trim() : "")
        color: "#9D5D00"
        font.family: root.uiFontFamily
        font.pointSize: 9
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    // 弹层：每次都是新建的一份（见文件开头的说明）。
    Loader {
        id: overlayLoader

        objectName: "overlayLoader"
        anchors.fill: parent
        active: root.overlayKind.length > 0
        sourceComponent: root.overlayComponent()
        onLoaded: root.applyOverlay()
    }

    Component {
        id: hotkeyComponent

        HotkeyOverlay {}
    }

    Component {
        id: remapComponent

        RemapOverlay {}
    }

    Component {
        id: windowRuleComponent

        WindowRuleOverlay {}
    }

    Component {
        id: appComponent

        AppOverlay {}
    }

    Component {
        id: confirmComponent

        ConfirmOverlay {}
    }

    Component {
        id: windowMonitorCard

        SettingCard {
            title: qsTr("缺省显示器")
            subtitle: qsTr("规则里没有写 `monitor` 时，窗口摆到哪一块屏上。")

            TextRow {
                Layout.fillWidth: true
                label: qsTr("缺省目标")
                hint: qsTr("cursor 光标所在的那一块 / last 这个程序上次被摆到的那一块 / none 不碰位置 / 序号（1 起）/ primary / DISPLAY2")
                value: root.editor && root.editor.settings.window_monitor !== undefined
                    ? String(root.editor.settings.window_monitor) : "cursor"
                onEdited: function (value) {
                    root.editor.setSetting("window_monitor",
                                           value.length > 0 ? value : "cursor")
                }
            }
        }
    }

    Connections {
        target: root.editor

        function onSaved(path) {
            root.statusText = qsTr("已保存到 %1，flowkeyd 正在重载配置。").arg(path)
        }
    }
}
