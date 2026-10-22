// 「把一块屏幕冻住」的遮罩（切工作空间专用）。
//
// 为什么需要它：`SW_HIDE` 过的窗口再显示出来时，Chromium 类程序（Chrome / Edge /
// Electron / VSCode）会在**揭示的那一瞬**白 30–45 ms —— 它的遮挡计算
// （`CalculateNativeWinOcclusion`）让它在被别的窗口盖着时**根本不画**，所以
// 「先在旧窗口底下画好再露出来」这条路走不通（M11 实测：等 31 / 94 / 180 ms，
// 白屏都恰好出现在藏掉旧窗口那一刻）。跨进程 `DwmSetWindowAttribute(DWMWA_CLOAK)`
// 是 `E_ACCESSDENIED`（shell 内部能力），也学不来。
//
// 剩下的唯一办法是**把过渡盖住**：切之前抓一张那块屏幕的快照，用它铺一个
// 置顶窗口挡住那块区域，然后照常显隐窗口、等 App 画完，最后撤掉遮罩。屏幕上看到
// 的是「旧画面冻住一下 → 新空间完整出现」。
//
// 遮罩窗口的几条约束（都是必须的）：
//   * `WS_EX_TOPMOST`：要在所有窗口之上才能挡住显隐；
//   * `WS_EX_TOOLWINDOW`：**不进** `Alt+Tab`，也不出现在任务栏；
//   * `WS_EX_NOACTIVATE`：不抢前台（切空间不能把焦点从用户的目标窗口上抢走）；
//   * `WS_EX_TRANSPARENT` + `WS_EX_LAYERED`：鼠标**点穿**，不吞点击；
//   * `WS_EX_LAYERED` 还顺带让 App 的遮挡计算不把这块当成「我被挡住了」——
//     实测（M11）Chromium 在遮罩底下照样会把内容画出来，这才是这个方案能成立的
//     前提。
//
// 只有 GUI 能力，不碰配置、不碰状态机；**只在动作工作线程上**建立与撤掉
// （与显隐同一条线程，先后顺序才有意义）。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <vector>

namespace flowkeyd::platform::win::freeze {

/// 一次性的屏幕快照遮罩：`cover()` 抓图并铺上去，`release()` 撤掉。
///
/// 析构会顺手 `release()`，所以调用方可以直接在栈上放一个。
class FreezeFrame
{
public:
    FreezeFrame();
    ~FreezeFrame();

    FreezeFrame(const FreezeFrame &) = delete;
    FreezeFrame &operator=(const FreezeFrame &) = delete;

    /// 抓 `area`（**物理像素**）的屏幕内容并盖住它。
    ///
    /// 失败（拿不到屏幕 DC / 建窗口失败）返回 false 且不留下任何窗口：调用方照常
    /// 切空间，只是没有遮罩（也就是回到底层行为）。
    bool cover(const RECT &area, QString *error = nullptr);

    /// 盖住这扇窗口的**并集**（裁到所在显示器的工作区）；允许窗口正藏着。
    ///
    /// 空列表、或者全部拿不到矩形时返回 false。
    bool coverWindows(const std::vector<HWND> &windows, QString *error = nullptr);

    /// 撤掉遮罩（幂等，可以在任何时候重复调）。
    void release();

    /// 遮罩现在是否真的盖着。
    bool active() const { return m_window != nullptr; }

    /// 往 `dc`（窗口客户区，`void *` 是为了不让这个头文件拖进 `<windows.h>`）
    /// 贴回快照。只给窗口过程用。
    void paint(void *dc);

private:
    HWND m_window = nullptr;
    HDC m_memory = nullptr;
    HBITMAP m_bitmap = nullptr;
    void *m_bits = nullptr;
    int m_width = 0;
    int m_height = 0;};

} // namespace flowkeyd::platform::win::freeze
