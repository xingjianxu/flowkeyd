pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 弹层的外壳：半透明遮罩 + 居中的卡片 + 标题 + 可滚动的表单 + 底部按钮。
//
// **为什么不用 `Dialog`**：这张界面整体是 FluentWinUI3 样式，而弹层里要塞进一整套
// 表单（含 `ScrollView`）。自己搭一层外壳，样式、尺寸、滚动与 `Esc` 的行为就都在
// 这里一处说了算；子类只需要往默认属性里写自己的表单。
//
// `Esc` 用 `Shortcut` 而不是 `Keys.onEscapePressed`：快捷键在任何焦点状态下都先于
// 焦点项处理，所以哪怕用户正在输入框里打字，`Esc` 也一定能关掉弹层。
Item {
    id: overlay

    property string title: ""
    property string acceptLabel: qsTr("确定")
    /// 非空时在按钮上方显示一行红色提示（保存前的最后一次校验）。
    property string errorText: ""
    property int cardWidth: 620
    property int cardHeight: 560
    /// 顶部提示（说明这个弹层在编辑什么）。
    property string note: ""

    // ---- 由 `ConfigWindow` 在创建之后填上 ----
    /// `app::ConfigEditor`。
    property var editor: null
    /// 正在编辑的条目（JS 对象）。子类**就地修改**它；`ConfigWindow` 在确定时把同一
    /// 个对象交回去（新增）或写回原下标（编辑）。
    property var entry: ({})
    /// 编辑已有的条目时是它的下标，新增时是 -1。
    property int index: -1
    /// 确认框的正文与「这次确认到底要干什么」。
    property string message: ""
    property string action: ""

    /// 确定时先跑一次校验（返回非空字符串 = 有问题，阻止关闭）。
    property var validate: null
    /// 校验通过之后干什么（不设就直接发 `accepted()`）。
    property var commit: null

    /// 子类把表单写进这里。
    default property alias content: body.data

    signal accepted()
    signal cancelled()

    function close() {
        overlay.visible = false
    }

    /// 确定按钮的入口：先校验、再提交。
    function accept() {
        if (overlay.validate) {
            const problem = overlay.validate()
            if (problem && problem.length > 0) {
                overlay.errorText = problem
                return
            }
        }
        overlay.errorText = ""
        if (overlay.commit)
            overlay.commit()
        else
            overlay.accepted()
    }

    // 点遮罩 = 取消（点卡片本身不会传到这一层）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.35)

        MouseArea {
            anchors.fill: parent
            onClicked: overlay.cancelled()
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: overlay.visible
        onActivated: overlay.cancelled()
    }

    Rectangle {
        id: card

        anchors.centerIn: parent
        width: Math.min(overlay.cardWidth, overlay.width - 40)
        height: Math.min(overlay.cardHeight, overlay.height - 40)
        color: overlay.palette.base
        radius: 10
        border.width: 1
        border.color: overlay.palette.mid

        Label {
            id: titleLabel

            x: 18
            y: 12
            width: parent.width - 36
            height: 26
            text: overlay.title
            color: overlay.palette.text
            font.family: "Microsoft YaHei"
            font.pointSize: 12.5
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            id: noteLabel

            visible: overlay.note.length > 0
            x: 18
            y: 38
            width: parent.width - 36
            height: 16
            text: overlay.note
            color: overlay.palette.placeholderText
            font.family: "Microsoft YaHei"
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        ScrollView {
            id: scroll

            x: 18
            y: 58
            width: parent.width - 36
            height: parent.height - 58 - 62
            clip: true

            Column {
                id: body

                width: scroll.availableWidth
                spacing: 6
            }
        }

        Label {
            id: errorLabel

            visible: overlay.errorText.length > 0
            x: 18
            y: parent.height - 54
            width: parent.width - 36
            height: 16
            text: overlay.errorText
            color: "#D13438"
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            spacing: 8

            Button {
                text: qsTr("取消")
                font.family: "Microsoft YaHei"
                font.pointSize: 10
                onClicked: overlay.cancelled()
            }

            Button {
                highlighted: true
                text: overlay.acceptLabel
                font.family: "Microsoft YaHei"
                font.pointSize: 10
                onClicked: overlay.accept()
            }
        }
    }
}
