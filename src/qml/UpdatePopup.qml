pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 「检查更新」弹出的在线更新窗口：版本号 + 更新说明 + 进度条 + 确认按钮。
//
// 与 menu / help / switch 三个弹窗同一套骨架：无边框圆角卡片、`Qt.Tool`
// （不进任务栏）、配色一律走 `palette`、中文一律微软雅黑。**区别只有两处**：
//   * 它**不**在失去焦点时自动关掉：下载要几秒到几十秒，用户可能去干别的；
//   * 它是一个**任务窗口**而不是键盘弹窗：按钮点一下就干活，没有列表、没有筛选、
//     没有键盘导航（只有 `Esc` 关窗）。
//
// 逻辑全在 `app::UpdateModel`（纯逻辑、有单测）：阶段、版本号、说明、进度、
// 状态行、按钮该不该出现。QML 只读属性、只在按钮上调 `host.updateXxx()`；
// 真正去联网 / 解压 / 重启的是 `app::Updater`（QML 从不自己干活）。
Window {
    id: root

    flags: Qt.Window | Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    // 无边框窗口的标题用户看不到，但验收 / 诊断脚本靠它读「现在是什么状态」。
    title: root.updateModel ? root.updateModel.caption : "flowkeyd 在线更新"

    property var updateModel: null
    property var host: null

    // 与其它三个弹窗一致：FluentWinUI3 默认族 `Segoe UI Variable` 没有中文字形。
    readonly property string uiFontFamily: "Microsoft YaHei"
    // 系统 palette 里没有“错误色”，这是整张卡片里唯一硬编码的颜色
    // （与帮助窗口的待确认琥珀色同理）。
    readonly property color errorColor: "#D13438"

    readonly property int pad: 18
    readonly property bool downloading: updateModel
        ? (updateModel.phase === "downloading" || updateModel.phase === "extracting") : false
    readonly property bool progressVisible: updateModel
        ? (updateModel.phase === "checking" || downloading) : false

    width: root.updateModel ? root.updateModel.cardWidth : 520
    height: root.updateModel ? root.updateModel.cardHeight : 360

    /// 「当前版本 X（→ 新版本 Y）」那一行。
    function versionLine() {
        if (!root.updateModel)
            return ""
        var current = root.updateModel.currentVersion.length > 0 ? root.updateModel.currentVersion
                                                                 : qsTr("未知")
        if (root.updateModel.hasNewVersion)
            return qsTr("当前版本 %1　　新版本 %2").arg(current).arg(root.updateModel.newVersion)
        return qsTr("当前版本 %1").arg(current)
    }

    function closeWindow() {
        if (root.host)
            root.host.updateDismiss()
    }

    Keys.onEscapePressed: root.closeWindow()

    Rectangle {
        id: card

        anchors.fill: parent
        radius: root.updateModel ? root.updateModel.cardRadius : 12
        color: root.palette.base
        border.width: 1
        border.color: root.palette.mid
        clip: true
        focus: true

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) {
                event.accepted = true
                root.closeWindow()
            }
        }

        Label {
            id: titleLabel

            x: root.pad
            y: 14
            width: parent.width - root.pad * 2
            height: 26
            text: root.updateModel ? root.updateModel.title : ""
            color: root.palette.text
            font.family: root.uiFontFamily
            font.pointSize: 12.5
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            id: versionLabel

            x: root.pad
            y: 44
            width: parent.width - root.pad * 2
            height: 20
            text: root.versionLine()
            color: root.updateModel && root.updateModel.hasNewVersion ? root.palette.highlight
                                                                      : root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 更新说明：真正的 `ScrollView` + 只读 `TextArea`（发布说明是 Markdown），
        // 滚动、选择、复制都归 Qt 的标准控件。
        ScrollView {
            id: notesScroll

            x: root.pad
            y: 70
            width: parent.width - root.pad * 2
            height: 172
            clip: true

            TextArea {
                id: notesArea

                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.MarkdownText
                text: root.updateModel && root.updateModel.hasNotes ? root.updateModel.notes
                                                                    : (root.updateModel ? root.updateModel.notesPlaceholder : "")
                color: root.palette.text
                font.family: root.uiFontFamily
                font.pointSize: 10
                // 让内容按 ScrollView 的可用宽度换行（不然会横向滚）。
                width: notesScroll.availableWidth
            }
        }

        ProgressBar {
            id: progressBar

            x: root.pad
            y: 252
            width: parent.width - root.pad * 2
            height: 6
            visible: root.progressVisible
            from: 0
            // `from == to` 就是标准控件里的「不确定进度」：服务器没给
            // `Content-Length`（或者还在检查更新）时用它。
            to: root.updateModel && root.updateModel.hasProgressTotal ? 1 : 0
            value: root.updateModel ? root.updateModel.progress : 0
        }

        Label {
            x: root.pad
            y: 262
            width: parent.width - root.pad * 2
            height: 16
            visible: root.progressVisible
            text: root.updateModel ? root.updateModel.progressText : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 8.5
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            x: root.pad
            y: 282
            width: parent.width - root.pad * 2
            height: 20
            text: root.updateModel ? root.updateModel.statusText : ""
            color: root.updateModel && root.updateModel.phase === "failed" ? root.errorColor
                                                                          : root.palette.text
            font.family: root.uiFontFamily
            font.pointSize: 10.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Label {
            x: root.pad
            y: 302
            width: parent.width - root.pad * 2
            height: 16
            visible: root.updateModel ? root.updateModel.hasErrorDetail : false
            text: root.updateModel ? root.updateModel.errorDetail : ""
            color: root.palette.placeholderText
            font.family: root.uiFontFamily
            font.pointSize: 8.5
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
        }

        Row {
            id: buttons

            x: root.pad
            y: parent.height - height - 12
            width: parent.width - root.pad * 2
            height: 32
            spacing: 8
            layoutDirection: Qt.RightToLeft

            Button {
                text: root.updateModel ? root.updateModel.closeLabel : qsTr("关闭")
                visible: root.updateModel ? root.updateModel.canDismiss : true
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: root.closeWindow()
            }

            Button {
                // `highlighted` 是标准 `Button` 的「默认按钮」标记，样式会画成主题色。
                highlighted: true
                text: qsTr("立即更新")
                visible: root.updateModel ? root.updateModel.canInstall : false
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: if (root.host) root.host.updateInstall()
            }

            Button {
                text: qsTr("重试")
                visible: root.updateModel ? root.updateModel.canRetry : false
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: if (root.host) root.host.updateRetry()
            }

            Button {
                text: qsTr("打开发布页")
                visible: root.updateModel ? root.updateModel.canOpenRelease : false
                font.family: root.uiFontFamily
                font.pointSize: 10
                onClicked: if (root.host) root.host.updateOpenRelease()
            }
        }
    }
}
