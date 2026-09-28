// 本仓库所有 Win32 声明的唯一入口：统一打开 `windows.h`，并把
// “结构体布局是契约”的那几条写成 `static_assert`（见 AGENTS.md 第 7 节第 8 条）。
//
// 未公开的 `win32u!NtUser*` 走 `nt.h`（运行时解析），
// 未静态链接的公开库（`dwmapi`）同样走运行时解析。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QString>

#include <cstdint>

// x64 上 `INPUT` 必须是 40 字节（联合体最大的成员是 `MOUSEINPUT`）。
// 这条写错不是返回错误码，而是注入莫名其妙的输入或直接崩溃。
static_assert(sizeof(void *) == 8, "flowkeyd targets x64 Windows");
static_assert(sizeof(INPUT) == 40, "INPUT layout on x64");
static_assert(sizeof(KEYBDINPUT) == 24, "KEYBDINPUT layout on x64");
static_assert(sizeof(MOUSEINPUT) == 32, "MOUSEINPUT layout on x64");
static_assert(offsetof(INPUT, type) == 0, "INPUT.type must be first");

namespace flowkeyd::platform::win {

/// 单调递增的毫秒计数（`GetTickCount64`），引擎计时用它。
inline std::uint64_t monotonicMs()
{
    return static_cast<std::uint64_t>(GetTickCount64());
}

/// 本模块（exe）的 `HMODULE`；装钩子时用它。
HMODULE moduleHandle();

/// `FormatMessageW` 渲染的 Windows 错误文本（英文，可断言）。
QString winErrorMessage(unsigned long code);

/// `"<what> failed: <message>"`，`code` 默认取 `GetLastError()`。
QString lastErrorMessage(const char *what);

/// 常见 `HRESULT` 的人话说明。
///
/// COM 接口的失败信息对定位“vtable 布局与系统不匹配”很重要：`E_NOINTERFACE`
/// 意味着版本表选错了 IID，而不是随便一个内部错误。
QString hresultText(long code);

/// `"<what> failed: <hresultText>"`。
QString hresultMessage(long code, const char *what);

} // namespace flowkeyd::platform::win
