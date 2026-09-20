import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 日志窗口：尾随日志文件，最多显示 1000 行，支持子串筛选与按级别配色。
//
// 关掉这个窗口绝不能退出应用（`setQuitOnLastWindowClosed(false)`，
// 见 AGENTS.md 第 7 节第 20 条）。
ApplicationWindow {
    id: root

    width: 860
    height: 500
    visible: false
    title: qsTr("flowkeyd 日志 — %1 行").arg(root.logModel ? root.logModel.visibleCount : 0)

    // 由 `app::LogWindow` 通过 setProperty 挂上来。
    property var logModel: null

    // 日志级别 → 颜色。INFO 及以下用窗口的默认前景色，跟着系统主题走。
    function colorFor(level) {
        if (level === "ERROR")
            return "#c42b1c"
        if (level === "WARN")
            return "#9d5d00"
        if (level === "DEBUG")
            return "#6b4fa8"
        if (level === "TRACE")
            return root.palette.placeholderText
        return root.palette.text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: filterField
                Layout.fillWidth: true
                placeholderText: qsTr("筛选日志（子串，不区分大小写）")
                onTextChanged: if (root.logModel) root.logModel.filter = text
            }

            Label {
                text: root.logModel
                      ? (root.logModel.visibleCount + " / " + root.logModel.totalCount)
                      : "0 / 0"
                opacity: 0.7
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.logModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Text {
                required property string line
                required property string level
                width: list.width
                text: line
                font.family: "Consolas"
                font.pixelSize: 12
                color: root.colorFor(level)
                wrapMode: Text.NoWrap
            }

            // 新行进来时自动滚到底；用户已经往上翻看时不打扰他。
            Connections {
                target: root.logModel
                function onAppended() {
                    if (list.atYEnd)
                        list.positionViewAtEnd()
                }
            }
        }
    }

    Component.onCompleted: {
        if (root.logModel)
            root.logModel.refresh()
        list.positionViewAtEnd()
    }
}
