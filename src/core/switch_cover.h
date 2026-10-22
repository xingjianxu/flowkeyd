// 「这次切工作空间要不要建冻结遮罩」的判据（`settings.workspace.switch_effect = "auto"`）。
//
// 为什么需要它：被 `SW_HIDE` 过再显示出来的窗口里，只有**自带合成器**的那一类
// （Chromium / Electron / VSCode / WezTerm）会在重新露出来的那一瞬先给一帧空白的
// 客户端区 —— 标题栏、地址栏都画好了，空的只是页面区，实测 35–48 ms
// （`tmp/wsflashfix/`）；记事本这类纯 GDI 程序从来不会。
//
// 于是 `auto` 档就按这个判据决定盖不盖：绝大多数切换（native 程序）是 0 延迟，
// 只有切到浏览器 / 终端那几次付遮罩的钱。
//
// 判据只吃**窗口类名 + 可执行文件名**两个字符串，所以留在 `core/` 里、可单测。
#pragma once

#include <QString>

#include <vector>

namespace flowkeyd::core {

/// 判据的输入：平台层读出来的两个字符串（都是可空的）。
struct RevealTraits
{
    /// 窗口类名，例如 `Chrome_WidgetWin_1`。
    QString className;
    /// 可执行文件名（含扩展名），例如 `msedge.exe`；读不出来时为空。
    QString processName;

    friend bool operator==(const RevealTraits &, const RevealTraits &) = default;
};

/// 这个窗口被藏过再显示时会不会先空一帧。
///
/// 认两类：Chromium / Electron 共用的窗口类 `Chrome_WidgetWin_1`，以及若干
/// 自带合成器、类名不同的程序（WezTerm 这类）。两边都比较完整字符串、大小写无关。
bool flickersWhenRevealed(const RevealTraits &traits);

/// 这次切换要不要建冻结遮罩：**任何一个**要露出来的窗口会闪就盖。
///
/// 空列表 = 不盖（没有窗口要露出来，也就没有可闪的东西）。
bool needsCoverForReveal(const std::vector<RevealTraits> &windows);

} // namespace flowkeyd::core
