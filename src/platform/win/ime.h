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
// 状态，弹窗关掉也不需要还原。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

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

/// 把 `hwnd` 所属线程的输入法切成英文（字母数字）模式。
///
/// 拿不到输入上下文（没有安装输入法、句柄无效）时**不算错误**：那种情况下键盘
/// 本来就直接输入英文，`ok` 会是 true、`changed` 为 false。
ModeSwitch useAlphanumericMode(HWND hwnd);

} // namespace flowkeyd::platform::win::ime
