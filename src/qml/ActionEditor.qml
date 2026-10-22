pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 一个声明式动作的编辑器（`run` / `send` / `window` / `menu` / `power`……21 种）。
//
// 三条规矩：
//
//  1. **字段名与配置里的一模一样**（`program` / `delay_ms` / `op`……），所以界面里
//     任意一格都能一眼对回 README 的配置表。只有两处例外：`args` / `env` 这种
//     「一串东西」在界面上是一个多行输入框（一行一项 / 一行 `K=V`）。
//  2. **动作对象就是唯一的状态**：每次改动都立刻写回 `action` 这个 JS 对象，
//     没有「暂存」这一层。所以增删动作（`ActionListEditor` 会重建委托）不会丢内容。
//  3. `undefined` 表示「这个字段没写过」：`TriRow` 汇报 `undefined` 时要把键**删掉**，
//     而不是写成 `false`。`window` 动作的 `launch` 里这一点尤其要紧 —— 那张表里
//     「写没写某个字段」本身就是语义（`core::LaunchFields`）。
//
// `menu` 的条目里还能再有动作，所以这里用 `ActionListEditor`（它按 URL 加载本文件，
// 见那个文件里的说明）。
Column {
    id: root

    /// 一个动作字典（JS 对象，来自 `core::configToMap()`）。
    property var action: ({})
    /// `app::ConfigEditor`（只用它的下拉框取值与 `newAction()`）。
    property var editor: null

    /// 界面自己的「当前类型 / 当前 op」：改它们要连带写回 `action`，而写回之后
    /// 还要能驱动下面各段的 `visible` —— JS 对象的改动不会发通知，所以这两份
    /// 状态在根对象上各留一个副本（由 `setType` / `setWindowOp` 同步）。
    property string type: root.action && root.action.type ? root.action.type : "none"
    property string windowOp: root.action && root.action.op ? root.action.op : "activate"
    property bool hasLaunch: root.action && root.action.launch !== undefined

    width: parent ? parent.width : 0
    spacing: 6

    function setType(value) {
        root.type = value
        if (!root.action)
            return
        root.action.type = value
        // `workspace` 的两个字段都是必填的（`--check` 会拒“missing field `index`”）：
        // 切到它时先把默认值补上，否则用户在界面上什么都不改就保存会失败。
        if (value === "workspace") {
            if (root.action.op === undefined)
                root.action.op = "switch"
            if (root.action.index === undefined)
                root.action.index = 1
        }
    }

    function setWindowOp(value) {
        root.windowOp = value
        if (root.action)
            root.action.op = value
    }

    /// `TriRow` 的三态值写回：`undefined` 等于把这个键删掉（回到默认）。
    function setOptional(key, value) {
        if (!root.action)
            return
        if (value === undefined)
            delete root.action[key]
        else
            root.action[key] = value
    }

    /// 可选字符串：界面里清空一个格子 = 这个字段没写过（而不是空串）。
    function setOptionalString(key, text) {
        root.setOptional(key, text.length > 0 ? text : undefined)
    }

    /// `animate` 只对这些 op 有意义（别的 op 不产生窗口过渡，写了会被 `--check` 拒）。
    function opSupportsAnimate() {
        const supported = ["activate", "minimize", "maximize", "restore", "move_left_monitor",
                           "move_right_monitor"]
        return supported.indexOf(root.windowOp) >= 0
    }

    /// 多行输入框 <-> 字符串列表（一行一项；空行会被丢掉）。
    ///
    /// `Array.isArray` 的检查是必需的：`run.args` 是列表，而 `open.args` 是**一个
    /// 字符串**，两者同名；不可见的那一段的绑定**照样会被求值**，所以少了这一句，
    /// 编辑一个 `open` 动作时 `run` 那一段就会抛
    /// `TypeError: Property 'join' of object ... is not a function`
    /// （界面看上去一切正常，日志里却有一堆 TypeError）。
    function linesOf(list) {
        if (!list || !Array.isArray(list))
            return ""
        return list.join("\n")
    }

    function listOfLines(text) {
        if (text.length === 0)
            return []
        return text.split("\n").filter(function (line) {
            return line.length > 0
        })
    }
    /// 多行输入框 <-> `K=V` 映射。
    function mapToText(map) {
        if (!map)
            return ""
        const parts = []
        for (const key in map)
            parts.push(key + "=" + map[key])
        return parts.join("\n")
    }

    function textToMap(text) {
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

    // ---- 动作类型 --------------------------------------------------------
    SettingRow {
        id: typeRow

        width: root.width
        label: qsTr("动作")

        ComboBox {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(280, parent.width)
            model: root.editor ? ConfigText.labels("action", root.editor.actionKinds) : []
            // 绑定而不是 `Component.onCompleted`：`editor` 是在这个编辑器被创建
            // **之后**才挂上来的（`ActionListEditor` 里的 `Loader`），`onCompleted`
            // 时它还是 null，`actionKinds` 也就是空的 —— 那样类型框会永远停在第一项
            // （「启动程序」），与下面真正显示的那组字段对不上（踩过）。
            currentIndex: {
                const kinds = root.editor ? root.editor.actionKinds : []
                const index = kinds.indexOf(root.type)
                return index >= 0 ? index : 0
            }
            onActivated: {
                const kinds = root.editor ? root.editor.actionKinds : []
                root.setType(kinds[currentIndex])
            }
        }
    }

    // ---- run -------------------------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "run"

        TextRow {
            width: root.width
            label: qsTr("程序")
            placeholder: qsTr("例如 notepad.exe 或 C:\\tools\\app.exe")
            value: root.action.program !== undefined ? root.action.program : ""
            onEdited: function (value) { root.action.program = value }
        }
        SettingRow {
            width: root.width
            label: qsTr("参数")
            hint: qsTr("一行一个参数")
            TextField {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(340, parent.width)
                text: root.linesOf(root.action.args)
                onTextEdited: root.action.args = root.listOfLines(text)
            }
        }
        TextRow {
            width: root.width
            label: qsTr("工作目录")
            value: root.action.cwd !== undefined ? root.action.cwd : ""
            onEdited: function (value) { root.setOptionalString("cwd", value) }
        }
        ChoiceRow {
            width: root.width
            label: qsTr("窗口状态")
            options: root.editor ? root.editor.showModes : []
            labels: ConfigText.labels("show", options)
            value: root.action.show !== undefined ? root.action.show : "normal"
            onPicked: function (value) { root.action.show = value }
        }
        BoolRow {
            width: root.width
            label: qsTr("用 cmd.exe 执行")
            value: root.action.shell === true
            onPicked: function (value) { root.action.shell = value }
        }
        BoolRow {
            width: root.width
            label: qsTr("等它结束")
            value: root.action.wait === true
            onPicked: function (value) { root.action.wait = value }
        }
        BoolRow {
            width: root.width
            label: qsTr("以管理员身份运行")
            hint: qsTr("默认以当前登录用户的普通权限启动")
            value: root.action.elevated === true
            onPicked: function (value) { root.action.elevated = value }
        }
        SettingRow {
            width: root.width
            label: qsTr("环境变量")
            hint: qsTr("一行一个 K=V")
            TextField {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(340, parent.width)
                text: root.mapToText(root.action.env)
                onTextEdited: root.action.env = root.textToMap(text)
            }
        }
    }

    // ---- send / type -----------------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "send" || root.type === "type"

        TextRow {
            width: root.width
            label: root.type === "send" ? qsTr("按键脚本") : qsTr("要输入的文本")
            placeholder: root.type === "send" ? qsTr("例如 ^{c} 或 {Enter}") : qsTr("按字面原样输入")
            value: root.type === "send"
                ? (root.action.keys !== undefined ? root.action.keys : "")
                : (root.action.text !== undefined ? root.action.text : "")
            onEdited: {
                if (root.type === "send")
                    root.action.keys = value
                else
                    root.action.text = value
            }
        }
        SpinRow {
            width: root.width
            label: qsTr("按键间隔")
            hint: qsTr("两个按键之间的停顿，0 表示不额外等待")
            value: root.action.delay_ms !== undefined ? root.action.delay_ms : 0
            from: 0
            to: 2000
            suffix: " ms"
            onPicked: function (value) { root.setOptional("delay_ms", value) }
        }
        TriRow {
            width: root.width
            label: qsTr("先松开修饰键")
            value: root.action.release_modifiers
            onPicked: function (value) { root.setOptional("release_modifiers", value) }
        }
    }

    // ---- open ------------------------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "open"

        TextRow {
            width: root.width
            label: qsTr("目标")
            placeholder: qsTr("文件、文件夹或网址")
            value: root.action.target !== undefined ? root.action.target : ""
            onEdited: function (value) { root.action.target = value }
        }
        TextRow {
            width: root.width
            label: qsTr("参数")
            value: root.action.args !== undefined ? root.action.args : ""
            onEdited: function (value) { root.setOptionalString("args", value) }
        }
        TextRow {
            width: root.width
            label: qsTr("工作目录")
            value: root.action.cwd !== undefined ? root.action.cwd : ""
            onEdited: function (value) { root.setOptionalString("cwd", value) }
        }
        ChoiceRow {
            width: root.width
            label: qsTr("窗口状态")
            options: root.editor ? root.editor.showModes : []
            labels: ConfigText.labels("show", options)
            value: root.action.show !== undefined ? root.action.show : "normal"
            onPicked: function (value) { root.action.show = value }
        }
    }

    // ---- window ----------------------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "window"

        ChoiceRow {
            width: root.width
            label: qsTr("操作")
            options: root.editor ? root.editor.windowOps : []
            labels: ConfigText.labels("window", options)
            value: root.windowOp
            onPicked: function (value) { root.setWindowOp(value) }
        }
        TextRow {
            width: root.width
            label: qsTr("窗口标题包含")
            placeholder: qsTr("留空表示不限制")
            value: root.action.target !== undefined ? root.action.target : ""
            onEdited: function (value) { root.setOptionalString("target", value) }
        }
        TextRow {
            width: root.width
            label: qsTr("进程名包含")
            placeholder: qsTr("例如 notepad")
            value: root.action.process !== undefined ? root.action.process : ""
            onEdited: function (value) { root.setOptionalString("process", value) }
        }
        TriRow {
            width: root.width
            label: qsTr("再次按下收起")
            hint: qsTr("只对「激活 / 收起」有意义：已经在前台时按一下把它最小化")
            visible: root.windowOp === "activate"
            value: root.action.toggle
            onPicked: function (value) { root.setOptional("toggle", value) }
        }
        TriRow {
            width: root.width
            label: qsTr("让视图跟过去")
            visible: root.windowOp === "move_prev_desktop" || root.windowOp === "move_next_desktop"
            value: root.action.follow
            onPicked: function (value) { root.setOptional("follow", value) }
        }
        TriRow {
            width: root.width
            label: qsTr("带动画")
            visible: root.opSupportsAnimate()
            value: root.action.animate
            onPicked: function (value) { root.setOptional("animate", value) }
        }
        TextRow {
            width: root.width
            label: qsTr("摆到显示器")
            hint: qsTr("序号（1 起）／primary／DISPLAY2／cursor／last／none")
            visible: root.windowOp === "activate"
            value: root.action.monitor !== undefined ? String(root.action.monitor) : ""
            onEdited: function (value) { root.setOptional("monitor", value.length > 0 ? value : undefined) }
        }

        BoolRow {
            width: root.width
            label: qsTr("没有窗口时启动")
            hint: qsTr("找不到匹配窗口时启动下面这个程序，等它出现再激活（需要上面填了标题或进程名）")
            visible: root.windowOp === "activate"
            value: root.hasLaunch
            onPicked: {
                root.hasLaunch = value
                if (value)
                    root.action.launch = {
                        program: "",
                        show: "normal"
                    }
                else
                    delete root.action.launch
            }
        }

        Column {
            width: root.width
            spacing: 6
            visible: root.windowOp === "activate" && root.hasLaunch

            // `launch` 的字段块与 `app{}` 的启动方式共用同一个组件（`program` /
            // `args` / `cwd` / `show` / `shell` / `env` / `wait_ms` 七项一个不少）。
            // `wait_ms` 由下面那条自己画：动作顶层的 `wait_ms` 覆盖 `launch.wait_ms`，
            // 两个一起摆出来只会让人困惑。
            LaunchFieldsEditor {
                editor: root.editor
                showWaitMs: false
                spec: root.action.launch ? root.action.launch : ({})
            }

            SpinRow {
                width: root.width
                label: qsTr("等窗口出现")
                hint: qsTr("最多等多久，默认 3000")
                value: root.action.wait_ms !== undefined
                    ? root.action.wait_ms
                    : (root.action.launch && root.action.launch.wait_ms !== undefined ? root.action.launch.wait_ms : 3000)
                from: 0
                to: 60000
                stepSize: 100
                suffix: " ms"
                onPicked: function (value) { root.setOptional("wait_ms", value) }
            }
        }
    }

    // ---- menu ------------------------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "menu"

        TextRow {
            width: root.width
            label: qsTr("选单标题")
            value: root.action.title !== undefined ? root.action.title : ""
            onEdited: function (value) { root.setOptionalString("title", value) }
        }

        Repeater {
            model: (root.menuRevision, root.menuItems())

            delegate: Rectangle {
                id: menuItem

                required property int index
                required property var modelData

                width: root.width
                implicitHeight: menuBody.implicitHeight + 14
                color: menuItem.palette.base
                radius: 6
                border.width: 1
                border.color: menuItem.palette.mid

                Column {
                    id: menuBody

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
                            text: qsTr("条目 %1").arg(menuItem.index + 1)
                            color: menuItem.palette.placeholderText
                            font.family: "Microsoft YaHei"
                            font.pointSize: 9
                        }

                        Button {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("上移")
                            enabled: menuItem.index > 0
                            font.family: "Microsoft YaHei"
                            font.pointSize: 9
                            onClicked: root.moveMenuItem(menuItem.index, -1)
                        }

                        Button {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("下移")
                            enabled: menuItem.index + 1 < root.menuItems().length
                            font.family: "Microsoft YaHei"
                            font.pointSize: 9
                            onClicked: root.moveMenuItem(menuItem.index, 1)
                        }

                        Button {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("删除条目")
                            font.family: "Microsoft YaHei"
                            font.pointSize: 9
                            onClicked: root.removeMenuItem(menuItem.index)
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    TextRow {
                        width: parent.width
                        label: qsTr("文字")
                        value: menuItem.modelData.label !== undefined ? menuItem.modelData.label : ""
                        onEdited: function (value) { menuItem.modelData.label = value }
                    }
                    TextRow {
                        width: parent.width
                        label: qsTr("单键快捷键")
                        hint: qsTr("卡片开着时按这个键选中它，例如 s")
                        fieldWidth: 80
                        value: menuItem.modelData.key !== undefined ? menuItem.modelData.key : ""
                        onEdited: function (value) { root.setMenuItemKey(menuItem.modelData, value) }
                    }
                    TextRow {
                        width: parent.width
                        label: qsTr("右侧说明")
                        value: menuItem.modelData.hint !== undefined ? menuItem.modelData.hint : ""
                        onEdited: function (value) { menuItem.modelData.hint = value }
                    }

                    ActionListEditor {
                        width: parent.width
                        editor: root.editor
                        actions: root.menuItemActions(menuItem.modelData)
                        addLabel: qsTr("添加这个条目的动作")
                    }
                }
            }
        }

        Button {
            text: qsTr("添加条目")
            font.family: "Microsoft YaHei"
            font.pointSize: 10
            onClicked: root.addMenuItem()
        }
    }

    // ---- volume / media / clipboard -------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "volume"

        ChoiceRow {
            width: root.width
            label: qsTr("操作")
            options: root.editor ? root.editor.volumeOps : []
            labels: ConfigText.labels("volume", options)
            value: root.action.op !== undefined ? root.action.op : "up"
            onPicked: function (value) { root.action.op = value }
        }
        SpinRow {
            width: root.width
            label: qsTr("目标音量")
            visible: root.action.op === "set"
            value: root.action.level !== undefined ? root.action.level : 50
            from: 0
            to: 100
            onPicked: function (value) { root.action.level = value }
        }
        SpinRow {
            width: root.width
            label: qsTr("步长")
            hint: qsTr("每次调高／调低多少，0 表示用系统默认")
            value: root.action.step !== undefined ? root.action.step : 0
            from: 0
            to: 50
            onPicked: function (value) { root.setOptional("step", value) }
        }
    }

    Column {
        width: root.width
        spacing: 6
        visible: root.type === "media"

        ChoiceRow {
            width: root.width
            label: qsTr("操作")
            options: root.editor ? root.editor.mediaOps : []
            labels: ConfigText.labels("media", options)
            value: root.action.op !== undefined ? root.action.op : "play_pause"
            onPicked: function (value) { root.action.op = value }
        }
    }

    Column {
        width: root.width
        spacing: 6
        visible: root.type === "clipboard"

        ChoiceRow {
            width: root.width
            label: qsTr("操作")
            options: root.editor ? root.editor.clipboardOps : []
            labels: ConfigText.labels("clipboard", options)
            value: root.action.op !== undefined ? root.action.op : "get"
            onPicked: function (value) { root.action.op = value }
        }
        SettingRow {
            width: root.width
            label: qsTr("文本")
            hint: qsTr("可以用 {clipboard} / {selection} / {date} 占位符")
            visible: root.action.op === "set" || root.action.op === "append"
            TextField {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(340, parent.width)
                text: root.action.text !== undefined ? root.action.text : ""
                onTextEdited: root.action.text = text
            }
        }
    }

    // ---- notify / desktop / suspend / power ------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "notify"

        TextRow {
            width: root.width
            label: qsTr("标题")
            value: root.action.title !== undefined ? root.action.title : ""
            onEdited: function (value) { root.action.title = value }
        }
        TextRow {
            width: root.width
            label: qsTr("正文")
            value: root.action.body !== undefined ? root.action.body : ""
            onEdited: function (value) { root.setOptionalString("body", value) }
        }
    }

    Column {
        width: root.width
        spacing: 6
        visible: root.type === "desktop"

        SpinRow {
            width: root.width
            label: qsTr("第几张桌面")
            hint: qsTr("从 1 开始，按任务视图里的顺序")
            value: root.action.switch !== undefined ? root.action.switch : 1
            from: 1
            to: 64
            onPicked: function (value) { root.action.switch = value }
        }
    }

    Column {
        width: root.width
        spacing: 6
        visible: root.type === "suspend"

        ChoiceRow {
            width: root.width
            label: qsTr("状态")
            options: root.editor ? root.editor.toggleStates : []
            labels: ConfigText.labels("toggleState", options)
            value: root.action.state !== undefined ? root.action.state : "toggle"
            onPicked: function (value) { root.action.state = value }
        }
    }

    Column {
        width: root.width
        spacing: 6
        visible: root.type === "workspace"

        ChoiceRow {
            width: root.width
            label: qsTr("操作")
            options: root.editor ? root.editor.workspaceOps : []
            labels: ConfigText.labels("workspaceOp", options)
            value: root.action.op !== undefined ? root.action.op : "switch"
            onPicked: function (value) { root.action.op = value }
        }
        SpinRow {
            width: root.width
            label: qsTr("第几个工作空间")
            hint: qsTr("从 1 开始，不能超过 settings.workspace.count（默认 9）")
            // `prev` / `next` 是**相对**切换（按当前活动空间算，不循环），没有空间号可填。
            visible: root.action.op !== "prev" && root.action.op !== "next"
            value: root.action.index !== undefined ? root.action.index : 1
            from: 1
            to: 10
            onPicked: function (value) { root.action.index = value }
        }
        Label {
            width: root.width
            text: qsTr("切换只影响一块屏（默认 = 光标所在的那块）；发出的窗口进非活动空间时会立即隐藏，你留在原来的空间里。prev / next 到头了就什么都不做，不会循环。")
            color: root.palette.placeholderText
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            wrapMode: Text.WordWrap
        }
    }

    // ---- power -----------------------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "power"

        ChoiceRow {
            width: root.width
            label: qsTr("操作")
            options: root.editor ? root.editor.powerOps : []
            labels: ConfigText.labels("power", options)
            value: root.action.op !== undefined ? root.action.op : "sleep"
            onPicked: function (value) { root.action.op = value }
        }
        Label {
            width: root.width
            text: qsTr("注意：这一条会真的执行系统电源操作。")
            color: root.palette.placeholderText
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            wrapMode: Text.WordWrap
        }
    }

    // ---- help / windows / apps ------------------------------------------
    Column {
        width: root.width
        spacing: 6
        visible: root.type === "help" || root.type === "windows" || root.type === "apps"

        TextRow {
            width: root.width
            label: qsTr("卡片标题")
            placeholder: qsTr("留空表示用默认标题")
            value: root.action.title !== undefined ? root.action.title : ""
            onEdited: function (value) { root.setOptionalString("title", value) }
        }
    }

    // ---- caps_lock / reload / quit / none -------------------------------
    Label {
        width: root.width
        visible: root.type === "caps_lock" || root.type === "reload" || root.type === "quit"
            || root.type === "none"
        text: root.type === "caps_lock" ? qsTr("把 Caps Lock 关掉。")
            : root.type === "reload" ? qsTr("重新读取配置文件（保存后会自动重载，一般不需要这一条）。")
            : root.type === "quit" ? qsTr("退出 flowkeyd（会弹一次确认）。")
            : qsTr("什么都不做 —— 这条快捷键只用来屏蔽按键。")
        color: root.palette.placeholderText
        font.family: "Microsoft YaHei"
        font.pointSize: 9
        wrapMode: Text.WordWrap
    }

    // ---- menu 条目的增删（`items` 列表自己的修订号） ---------------------
    property int menuRevision: 0

    function menuItems() {
        return root.action && root.action.items ? root.action.items : []
    }

    function menuItemActions(item) {
        if (!item.actions)
            item.actions = []
        return item.actions
    }

    function setMenuItemKey(item, value) {
        const trimmed = value.trim()
        if (trimmed.length === 0)
            delete item.key
        else
            item.key = trimmed
    }

    function addMenuItem() {
        if (!root.action)
            return
        if (!root.action.items)
            root.action.items = []
        root.action.items.push({
            label: qsTr("新条目"),
            actions: []
        })
        root.menuRevision++
    }

    function removeMenuItem(index) {
        root.action.items.splice(index, 1)
        root.menuRevision++
    }

    function moveMenuItem(index, delta) {
        const items = root.action.items
        const target = index + delta
        if (!items || target < 0 || target >= items.length)
            return
        const moved = items.splice(index, 1)[0]
        items.splice(target, 0, moved)
        root.menuRevision++
    }
}
