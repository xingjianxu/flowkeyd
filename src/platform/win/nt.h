// 运行时解析的未公开 Win32 API（`win32u.dll`）。
//
// `user32.dll` 只是 `win32u.dll`（`win32k.sys` 的用户态部分）前面的一层薄壳：
//
// | 未公开导出               | 已公开包装            |
// | ------------------------ | --------------------- |
// | `NtUserSendInput`        | `SendInput`           |
// | `NtUserGetAsyncKeyState` | `GetAsyncKeyState`    |
//
// 用 `LoadLibraryW`/`GetProcAddress` 而不是导入库：`win32u.dll` 的导出集会随
// Windows 构建变化，而且 MinGW 没有它的导入库。每个入口都是可选的，
// `NtUserSendInput` 在被使用前先用一次零输入调用校验。
#pragma once

#include "platform/win/ffi.h"

#include <cstdint>
#include <optional>

namespace flowkeyd::platform::win::nt {

using NtUserSendInputFn = UINT(WINAPI *)(UINT, const INPUT *, int);
using NtUserGetAsyncKeyStateFn = SHORT(WINAPI *)(int);

/// 已解析的 `win32u.dll` 导出。
struct NtUser
{
    HMODULE module = nullptr;
    NtUserSendInputFn sendInput = nullptr;
    NtUserGetAsyncKeyStateFn getAsyncKeyState = nullptr;
    /// 零输入的 `NtUserSendInput` 调用行为符合预期时置位。
    bool sendInputVerified = false;
};

/// 进程级的解析结果（只解析一次）。
const NtUser &ntUser();

/// 可用且已验证时的 `NtUserSendInput`。失败时返回 `nullopt`，调用方回退。
std::optional<bool> sendInput(const INPUT *inputs, std::uint32_t count);

/// 未公开的异步按键状态读取（不因队列回绕而失真）。
std::optional<SHORT> asyncKeyState(int vk);

} // namespace flowkeyd::platform::win::nt
