pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.FluentWinUI3

// 编辑一条全局的 `hotkey{}`。
OverlayCard {
    id: overlay

    title: overlay.index >= 0 ? qsTr("编辑快捷键") : qsTr("新增快捷键")
    note: qsTr("和弦写小写字母（`a` 是 A 键）；要用 Shift 就显式写 `Shift+a`。")
    cardWidth: 720
    cardHeight: 660

    HotkeyFields {
        width: parent.width
        editor: overlay.editor
        hotkey: overlay.entry
    }

    validate: function () {
        const keys = overlay.entry.keys
        if (!keys || keys.length === 0 || keys[0].length === 0)
            return qsTr("请至少填一个和弦。")
        for (let i = 0; i < keys.length; ++i) {
            if (keys[i].length === 0)
                continue
            const problem = overlay.editor.chordError(keys[i], keys)
            if (problem.length > 0)
                return qsTr("%1：%2").arg(keys[i]).arg(problem)
        }
        return ""
    }
}
