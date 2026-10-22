pragma Singleton

import QtQuick

// 设置界面里所有「英文取值 -> 中文说明」的对照表。
//
// 配置里那些取值（`op = "move_next_desktop"`、`show = "hidden"`……）是**公开接口**，
// 界面上一个字都不能改，但让人在下拉框里读英文也不是个事。所以统一放这里：
// 一边给下拉框的显示文案（`labels`），一边给单个值的显示文案（`label`）。
//
// 认不出来的取值一律原样显示 —— 界面宁可难看一点，也不能把用户看不懂的值藏起来。
QtObject {
    id: text

    readonly property var tables: ({
        // `action` 的 type
        action: {
            "run": qsTr("启动程序"),
            "send": qsTr("发送按键"),
            "type": qsTr("输入文本"),
            "open": qsTr("打开"),
            "window": qsTr("窗口"),
            "menu": qsTr("弹出自定义选单"),
            "notify": qsTr("通知"),
            "volume": qsTr("音量"),
            "media": qsTr("媒体控制"),
            "clipboard": qsTr("剪贴板"),
            "desktop": qsTr("切换虚拟桌面"),
            "caps_lock": qsTr("关掉 Caps Lock"),
            "power": qsTr("电源"),
            "suspend": qsTr("挂起"),
            "help": qsTr("快捷键帮助"),
            "windows": qsTr("窗口切换器"),
            "apps": qsTr("程序启动器"),
            "workspace": qsTr("工作空间"),
            "reload": qsTr("重载配置"),
            "quit": qsTr("退出"),
            "none": qsTr("什么都不做")
        },
        show: {
            "normal": qsTr("正常"),
            "hidden": qsTr("隐藏"),
            "minimized": qsTr("最小化"),
            "maximized": qsTr("最大化")
        },
        volume: {
            "up": qsTr("调高"),
            "down": qsTr("调低"),
            "set": qsTr("设为指定值"),
            "mute": qsTr("静音"),
            "unmute": qsTr("取消静音"),
            "toggle": qsTr("静音开关")
        },
        media: {
            "play_pause": qsTr("播放 / 暂停"),
            "next": qsTr("下一首"),
            "prev": qsTr("上一首"),
            "stop": qsTr("停止")
        },
        clipboard: {
            "get": qsTr("复制选中内容"),
            "set": qsTr("写入"),
            "append": qsTr("追加"),
            "clear": qsTr("清空")
        },
        window: {
            "activate": qsTr("激活 / 收起"),
            "minimize": qsTr("最小化"),
            "maximize": qsTr("最大化"),
            "restore": qsTr("还原"),
            "close": qsTr("关闭"),
            "toggle_topmost": qsTr("置顶开关"),
            "move_prev_desktop": qsTr("移到上一张桌面"),
            "move_next_desktop": qsTr("移到下一张桌面"),
            "move_left_monitor": qsTr("移到左边显示器"),
            "move_right_monitor": qsTr("移到右边显示器")
        },
        power: {
            "sleep": qsTr("睡眠"),
            "hibernate": qsTr("休眠"),
            "shutdown": qsTr("关机"),
            "restart": qsTr("重启"),
            "logoff": qsTr("注销"),
            "lock": qsTr("锁定"),
            "screen_off": qsTr("关闭屏幕")
        },
        toggleState: {
            "on": qsTr("挂起"),
            "off": qsTr("恢复"),
            "toggle": qsTr("切换")
        },
        workspaceOp: {
            "switch": qsTr("切到某块屏的空间"),
            "move": qsTr("把当前窗口发过去"),
            "prev": qsTr("上一个空间（不循环）"),
            "next": qsTr("下一个空间（不循环）")
        },
        workspaceSwitchTarget: {
            "cursor": qsTr("光标所在的那块屏"),
            "focus": qsTr("前台窗口所在的那块屏")
        },
        trayBadge: {
            "both": qsTr("左上工作空间号 + 右下虚拟桌面号（绿 / 蓝）"),
            "desktop": qsTr("只显示虚拟桌面号（蓝底）"),
            "workspace": qsTr("只显示工作空间号（绿底）")
        },
        trigger: {
            "press": qsTr("按下时"),
            "release": qsTr("松开时"),
            "repeat": qsTr("按住重复")
        },
        remapMode: {
            "hold": qsTr("按住期间等效"),
            "tap": qsTr("按一下触发一次")
        },
        logLevel: {
            "trace": qsTr("trace（最详细）"),
            "debug": qsTr("debug"),
            "info": qsTr("info"),
            "warn": qsTr("warn"),
            "error": qsTr("error"),
            "off": qsTr("off（关掉日志）")
        },
        inputBackend: {
            "auto": qsTr("auto（自动）"),
            "user32": qsTr("user32（公开 API）"),
            "ntuser": qsTr("ntuser（未公开 API）")
        }
    })

    /// 一个取值的中文显示名（认不出来就原样返回）。
    function label(kind, value) {
        const table = text.tables[kind]
        if (!table || value === undefined || value === null)
            return value === undefined || value === null ? "" : String(value)
        const hit = table[String(value)]
        return hit === undefined ? String(value) : hit
    }

    /// 一组取值的中文显示名（顺序与 `values` 一致）。
    function labels(kind, values) {
        const out = []
        for (let i = 0; i < values.length; ++i)
            out.push(text.label(kind, values[i]))
        return out
    }
}
