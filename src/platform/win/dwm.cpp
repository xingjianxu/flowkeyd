#include "platform/win/dwm.h"

#include <cstring>

namespace flowkeyd::platform::win::dwm {

namespace {

/// `DWMWA_TRANSITIONS_FORCEDISABLED`：`pvAttribute` 指向一个 `BOOL`，
/// `TRUE` = 强制关掉该窗口的 DWM 过渡，`FALSE` = 恢复。
constexpr DWORD kTransitionsForcedDisabled = 3;

using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

/// 解析（并缓存）`dwmapi!DwmSetWindowAttribute`；拿不到时返回 nullptr。
DwmSetWindowAttributeFn setWindowAttribute()
{
    static DwmSetWindowAttributeFn resolved = []() -> DwmSetWindowAttributeFn {
        // 模块在进程生命周期内保持加载：不 FreeLibrary，免得指针悬空。
        HMODULE module = LoadLibraryW(L"dwmapi.dll");
        if (module == nullptr) {
            return nullptr;
        }
        const FARPROC proc = GetProcAddress(module, "DwmSetWindowAttribute");
        if (proc == nullptr) {
            return nullptr;
        }
        // `-Wcast-function-type` 讨厌 FARPROC → 具体签名的转换；memcpy 更干净。
        DwmSetWindowAttributeFn fn = nullptr;
        static_assert(sizeof(fn) == sizeof(proc), "function pointer sizes must match");
        std::memcpy(&fn, &proc, sizeof(fn));
        return fn;
    }();
    return resolved;
}

} // namespace

bool available()
{
    return setWindowAttribute() != nullptr;
}

bool forceDisableTransitions(HWND hwnd, bool disabled, QString *error)
{
    const DwmSetWindowAttributeFn set = setWindowAttribute();
    if (set == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("dwmapi!DwmSetWindowAttribute is unavailable");
        }
        return false;
    }
    const BOOL value = disabled ? TRUE : FALSE;
    const HRESULT hr = set(hwnd, kTransitionsForcedDisabled, &value, sizeof(value));
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "DwmSetWindowAttribute");
        }
        return false;
    }
    return true;
}

} // namespace flowkeyd::platform::win::dwm
