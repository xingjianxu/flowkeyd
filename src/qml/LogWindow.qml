import QtQuick
import QtQuick.Controls

// 日志窗口。stage 4 会把 LogModel（尾随日志文件）挂到这里；
// 现在只有一个占位文本，用来验证「窗口能弹出、关掉不退出进程」。
ApplicationWindow {
    id: root

    width: 760
    height: 440
    visible: false
    title: qsTr("flowkeyd 日志")

    Column {
        anchors.centerIn: parent
        spacing: 8

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: 16
            text: qsTr("flowkeyd 日志窗口")
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            opacity: 0.7
            text: qsTr("日志尾随会在阶段 4 接上。")
        }
    }
}
