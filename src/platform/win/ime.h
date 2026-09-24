// 输入法（IMM32）：把某个窗口的输入法切到**英文（字母数字）模式**。
//
// 为什么需要它：窗口切换器的筛选框匹配的是**进程名**（`chrome.exe` 这种 ASCII），
// 而用户经常正开着中文输入法 —— 那样打进去的是候选字，列表一条都筛不出来。
// 所以卡片一出来就把输入法切成英文。
//
// 为什么不能只写 Qt 的 `inputMethodHints`：Qt 在 Windows 上**不看**
// `Qt::ImhPreferLatin` —— `QWindowsInputContext` 只用 `ImEnabled` 决定要不要
// `ImmAssociateContext()`，提示位一概忽略。所以 QML 里写提示是没用的，必须自己
// 调 IMM32（读法见 AGENTS.md 第 10 节）。
//
// 为什么运行时解析 `imm32.dll`：见 AGENTS.md 第 3 节的依赖政策 —— 它不在允许静态
// 链接的那批系统 DLL 里，而我们只用到三个入口，所以与 `dwmapi` 走同一条路
// （`LoadLibraryW` + `GetProcAddress`，拿不到就退回“什么都不做”）。
//
// **只影响本进程这个线程**：TSF 的输入模式（`GUID_COMPARTMENT_KEYBOARD_INPUTMODE_
// CONVERSION`，而 IMM32 的 `ImmGetConversionStatus` / `ImmSetConversionStatus`
// 读写的就是它）是**按线程**的，所以切自己的窗口不会动用户在别的应用里的中英文
// 状态（实测：打开卡片之后在浏览器里打字，输入法照旧是中文）。
//
// **但它仍然是本进程里的一处共享状态**：实测在帮助窗口里按 `Shift` 切成中文之后，
// 窗口切换器读到的状态也带 `NATIVE` 位（同一个进程里的几个窗口互相看得见，
// 切换器 / 帮助 / 日志窗口都是这个线程的）；反过来卡片把它切成英文之后，别的
// 弹窗 / 日志窗口的输入法也可能跟着变。用户要求“别在别处留下痕迹”，因此还提供
// 「快照 + 还原」：卡片**打开之前**读一份模式（`readMode()`），关掉的时候原样
// 写回去（`restoreMode()`）。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <cstdint>

namespace flowkeyd::platform::win::ime {

/// 切换输入模式的结果（含一点可以进日志的细节）。
struct ModeSwitch
{
    /// 调用之后这个窗口确实处于英文（字母数字）模式。
    bool ok = false;
    /// 这次真的写过（之前不是英文模式）。没写的原因可能是它本来就是英文，
    /// 也可能是这台机器上压根没有输入法 / `imm32` 不可用 —— 看 `detail`。
    bool changed = false;
    /// 一句可进日志的英文说明（英文日志是本项目的约定）。
    QString detail;
};

/// 一个输入模式快照（`ImmGet/SetConversionStatus` 的那一对 `DWORD`）。
/// 窗口切换器用它做「打开前记一份、关掉时写回去」。
struct Mode
{
    /// 读过状态才为 true。拿不到输入上下文（这台机器没装输入法、窗口正在销毁、
    /// `imm32` 不可用）时是 false —— 那种情况下“还原”就是什么都不做。
    bool valid = false;
    /// 转换模式（`IME_CMODE_*`；`NATIVE` = 0x0001 置位就是中文/假名这类模式）。
    std::uint32_t conversion = 0;
    /// 句模式。一起读出来、一起写回去，原样保留（我们只改 `conversion`）。
    std::uint32_t sentence = 0;
};

/// 读取 `hwnd` 当前的输入模式快照（IMM32 的转换状态）；拿不到时 `valid` 为 false。
/// 这是只读的，不会动输入法。
Mode readMode(HWND hwnd);

/// 把 `hwnd` 的输入法切成英文（字母数字）模式。
///
/// 拿不到输入上下文（没有安装输入法、句柄无效）时**不算错误**：那种情况下键盘
/// 本来就直接输入英文，`ok` 会是 true、`changed` 为 false。
ModeSwitch useAlphanumericMode(HWND hwnd);

/// 把 `readMode()` 读到的模式写回去（`mode.valid` 为假时什么都不做）。
///
/// 已经有了那个模式就不再写一次（`changed` 为 false），所以它可以在关闭路径上
/// 无条件调用，用户没有动过输入法时不会有任何副作用。
ModeSwitch restoreMode(HWND hwnd, const Mode &mode);

} // namespace flowkeyd::platform::win::ime
