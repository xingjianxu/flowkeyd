#include "platform/win/nt.h"

#include <cstring>

namespace flowkeyd::platform::win::nt {

namespace {

/// 以零个输入调用 `NtUserSendInput` 必须返回 0：这证明该导出能用假设的签名
/// 调用且没有副作用。
bool verifySendInput(NtUserSendInputFn function)
{
    if (function == nullptr) {
        return false;
    }
    return function(0, nullptr, static_cast<int>(sizeof(INPUT))) == 0;
}

NtUser load()
{
    NtUser result;
    result.module = LoadLibraryW(L"win32u.dll");
    if (result.module == nullptr) {
        return result;
    }
    result.sendInput = nullptr;
    result.getAsyncKeyState = nullptr;
    FARPROC raw = GetProcAddress(result.module, "NtUserSendInput");
    static_assert(sizeof(raw) == sizeof(result.sendInput), "FARPROC is a function pointer");
    std::memcpy(&result.sendInput, &raw, sizeof(raw));
    raw = GetProcAddress(result.module, "NtUserGetAsyncKeyState");
    std::memcpy(&result.getAsyncKeyState, &raw, sizeof(raw));
    result.sendInputVerified = verifySendInput(result.sendInput);
    return result;
}

} // namespace

const NtUser &ntUser()
{
    static const NtUser instance = load();
    return instance;
}

std::optional<bool> sendInput(const INPUT *inputs, std::uint32_t count)
{
    const NtUser &nt = ntUser();
    if (nt.sendInput == nullptr || !nt.sendInputVerified) {
        return std::nullopt;
    }
    const UINT sent = nt.sendInput(count, inputs, static_cast<int>(sizeof(INPUT)));
    return sent == count;
}

std::optional<SHORT> asyncKeyState(int vk)
{
    const NtUser &nt = ntUser();
    if (nt.getAsyncKeyState == nullptr) {
        return std::nullopt;
    }
    return nt.getAsyncKeyState(vk);
}

} // namespace flowkeyd::platform::win::nt
