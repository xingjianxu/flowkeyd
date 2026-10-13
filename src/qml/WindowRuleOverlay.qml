pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 编辑一条 `window_rule{}`：某个程序的窗口出现在哪张虚拟桌面、哪块显示器上。
OverlayCard {
    id: overlay

    title: overlay.index >= 0 ? qsTr("编辑窗口规则") : qsTr("新增窗口规则")
    note: qsTr("窗口**第一次出现**时按这里的设置摆好位置；之后不再干预。显示器重新接上时也会再摆一次。")
    cardWidth: 700
    cardHeight: 620

    onEntryChanged: overlay.ensure()

    WindowRuleFields {
        width: parent.width
        rule: overlay.entry
        editor: overlay.editor
    }

    validate: function () {
        const process = overlay.entry.process
        const title = overlay.entry.title
        if ((!process || process.length === 0) && (!title || title.length === 0))
            return qsTr("「进程名包含」与「标题包含」至少填一个，否则这条规则会匹配每一个窗口。")
        if (overlay.entry.desktop !== undefined && overlay.entry.all_desktops === true)
            return qsTr("「指定虚拟桌面」与「钉在所有桌面」不能同时用。")
        if (overlay.entry.maximize === true && overlay.entry.width !== undefined)
            return qsTr("「最大化」与「指定尺寸与位置」不能同时用。")
        return ""
    }

    function ensure() {
        if (overlay.entry.enabled === undefined)
            overlay.entry.enabled = true
    }
}
