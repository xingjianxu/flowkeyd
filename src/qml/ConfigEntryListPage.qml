pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// 四个列表页共用的一张「条目列表」：快捷键 / 按键重映射 / 窗口规则 / 程序。
//
// 四种条目的字段完全不同，但**列表长什么样是一样的**（标题 + 摘要 + 操作按钮），
// 编辑都交给一个弹层（`HotkeyOverlay` 等）。所以这里做成一个按 `kind` 参数化的页面，
// 而不是四份几乎一样的 QML。
ColumnLayout {
    id: page

    property var editor: null
    /// `ConfigWindow` 自己：新增 / 编辑 / 删除都交给它（它拿得到弹层）。
    property var host: null
    property string kind: "hotkeys"
    property var entries: []
    property string addLabel: qsTr("新增")
    property string emptyHint: ""
    property bool canDuplicate: false
    /// 列表上方的额外内容（窗口规则页用它放「缺省显示器」）。
    property Component headerExtra: null

    width: parent ? parent.width : 0
    spacing: 12

    Loader {
        Layout.fillWidth: true
        Layout.preferredHeight: item ? item.implicitHeight : 0
        visible: page.headerExtra !== null
        sourceComponent: page.headerExtra
    }

    RowLayout {
        Layout.fillWidth: true

        Label {
            text: qsTr("共 %1 条").arg(page.entries.length)
            color: page.palette.placeholderText
            font.family: "Microsoft YaHei"
            font.pointSize: 10.5
        }

        Item {
            Layout.fillWidth: true
        }

        Button {
            text: page.addLabel
            highlighted: true
            font.family: "Microsoft YaHei"
            font.pointSize: 10
            onClicked: page.host.openEntry(page.kind, -1)
        }
    }

    Label {
        Layout.fillWidth: true
        visible: page.entries.length === 0
        text: page.emptyHint
        color: page.palette.placeholderText
        font.family: "Microsoft YaHei"
        font.pointSize: 10
        wrapMode: Text.WordWrap
    }

    // 条目装在**一张卡片**里：视觉上与「常规 / 高级」两页一致（都是「一组相关的
    // 东西装在一张浅色卡片里」），列表行也不再像散落在白底上。
    SettingCard {
        Layout.fillWidth: true
        visible: page.entries.length > 0

        Repeater {
            model: page.entries

            delegate: EntryRow {
                id: row

                required property int index
                required property var modelData

                Layout.fillWidth: true
                title: row.modelData._title !== undefined ? row.modelData._title : ""
                subtitle: row.modelData._subtitle !== undefined ? row.modelData._subtitle : ""
                entryEnabled: row.modelData.enabled !== false
                canDuplicate: page.canDuplicate
                onEditRequested: page.host.openEntry(page.kind, row.index)
                onDuplicateRequested: page.host.duplicateEntry(page.kind, row.index)
                onRemoveRequested: page.host.removeEntry(page.kind, row.index)
                onMoveRequested: function (delta) {
                    page.host.moveEntry(page.kind, row.index, delta)
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 8
    }
}
