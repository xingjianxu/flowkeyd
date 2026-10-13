pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 一张分组卡片：小标题 + 副标题 + 若干 `Layout` 子项（通常每项是一个
// `SettingRow`）。Windows 11 设置里那种「一组相关选项装在一张浅色卡片里」的样子。
//
// 卡片自己只负责画底与内边距，行怎么排由调用方用 `SettingRow` 决定 —— 这样
// 「不要拥挤」就落在两处数字上：卡片之间的间距（页面里的 `ColumnLayout.spacing`）
// 与卡片内行距（`body.spacing`）。
Rectangle {
    id: card

    property string title: ""
    property string subtitle: ""
    default property alias content: body.data

    color: card.palette.base
    radius: 8
    border.width: 1
    border.color: card.palette.mid
    implicitHeight: layout.implicitHeight + layout.anchors.margins * 2
    implicitWidth: 420

    ColumnLayout {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 4

        Label {
            visible: card.title.length > 0
            Layout.fillWidth: true
            text: card.title
            color: card.palette.text
            font.family: "Microsoft YaHei"
            font.pointSize: 12.5
            font.bold: true
            elide: Text.ElideRight
        }

        Label {
            visible: card.subtitle.length > 0
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            text: card.subtitle
            color: card.palette.placeholderText
            font.family: "Microsoft YaHei"
            font.pointSize: 9
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            id: body

            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 6
        }
    }
}
