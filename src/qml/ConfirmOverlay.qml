pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 「要不要这么做?」的确认框。`ConfigWindow` 用 `action` 记住这次到底在确认什么
// （丢弃改动 / 覆盖手写的配置文件 / 删除条目）。
OverlayCard {
    id: confirm

    cardWidth: 460
    cardHeight: 210
    acceptLabel: qsTr("继续")

    Label {
        width: parent.width
        text: confirm.message
        color: confirm.palette.text
        font.family: "Microsoft YaHei"
        font.pointSize: 10.5
        wrapMode: Text.WordWrap
    }
}
