pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 设置界面里的一行：「左边写标签、右边放控件」。
//
// 存在的理由：这张界面里几十行都是这个形状，而每一行手写一遍 `Label` 的宽度、
// 垂直居中与灰色提示的排版既啰嗦又容易写得一行一个样。左边固定 132 px 的标签栏
// 也是「不要拥挤」的一半 —— 所有控件都从同一条竖线开始。
//
// 默认属性直接指向右边那个容器，所以用法就是：
//
//     SettingRow { label: qsTr("日志级别"); ComboBox { ... } }
Item {
    id: row

    property string label: ""
    /// 灰色小字，排在控件下方（比塞在标签里更好读）。
    property string hint: ""
    property int labelWidth: 132
    /// 标签与控件之间的间隔。
    property int gap: 12

    default property alias content: host.data

    implicitHeight: 36 + (row.hint.length > 0 ? 16 : 0)
    implicitWidth: 320

    Label {
        id: nameLabel

        x: 0
        y: 0
        width: row.labelWidth
        height: 36
        text: row.label
        color: row.palette.text
        // 中文一律微软雅黑（FluentWinUI3 的默认族没有中文字形）。
        font.family: "Microsoft YaHei"
        font.pointSize: 10.5
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Label {
        id: hintLabel

        visible: row.hint.length > 0
        x: row.labelWidth + row.gap
        y: 36
        width: Math.max(row.width - row.labelWidth - row.gap, 0)
        height: 16
        text: row.hint
        color: row.palette.placeholderText
        font.family: "Microsoft YaHei"
        font.pointSize: 8.5
        verticalAlignment: Text.AlignTop
        elide: Text.ElideRight
    }

    Item {
        id: host

        x: row.labelWidth + row.gap
        y: 0
        width: Math.max(row.width - row.labelWidth - row.gap, 0)
        height: 36
    }
}
