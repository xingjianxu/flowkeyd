#include "platform/win/dwm.h"

#include <cstring>

namespace flowkeyd::platform::win::dwm {

namespace {

/// `DWMWA_TRANSITIONS_FORCEDISABLED`：`pvAttribute` 指向一个 `BOOL`，
/// `TRUE` = 强制关掉该窗口的 DWM 过渡，`FALSE` = 恢复。
constexpr DWORD kTransitionsForcedDisabled = 3;

/// `DWMWA_CLOAKED`：`pvAttribute` 指向一个 `DWORD`，非零 = 窗口被藏起来了
/// （位含义：1 = 应用自己藏的，2 = shell 藏的，4 = 从父窗口继承来的）。
constexpr DWORD kCloaked = 14;

using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
using DwmGetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, PVOID, DWORD);

/// 把 `GetProcAddress` 的结果转成具体签名（`-Wcast-function-type` 讨厌直接转换）。
template <typename Fn>
Fn resolve(const char *name)
{
    // 模块在进程生命周期内保持加载：不 FreeLibrary，免得指针悬空。
    static HMODULE module = LoadLibraryW(L"dwmapi.dll");
    if (module == nullptr) {
        return nullptr;
    }
    const FARPROC proc = GetProcAddress(module, name);
    if (proc == nullptr) {
        return nullptr;
    }
    static_assert(sizeof(Fn) == sizeof(proc), "function pointer sizes must match");
    Fn fn = nullptr;
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

/// 解析（并缓存）`dwmapi!DwmSetWindowAttribute`；拿不到时返回 nullptr。
DwmSetWindowAttributeFn setWindowAttribute()
{
    static DwmSetWindowAttributeFn resolved = resolve<DwmSetWindowAttributeFn>("DwmSetWindowAttribute");
    return resolved;
}

/// 解析（并缓存）`dwmapi!DwmGetWindowAttribute`；拿不到时返回 nullptr。
DwmGetWindowAttributeFn getWindowAttribute()
{
    static DwmGetWindowAttributeFn resolved = resolve<DwmGetWindowAttributeFn>("DwmGetWindowAttribute");
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

bool isCloaked(HWND hwnd)
{
    const DwmGetWindowAttributeFn get = getWindowAttribute();
    if (get == nullptr || hwnd == nullptr) {
        return false;
    }
    DWORD cloaked = 0;
    if (FAILED(get(hwnd, kCloaked, &cloaked, sizeof(cloaked)))) {
        // 窗口正在销毁、或这台机器上根本没有合成器：按“它在显示”处理。
        return false;
    }
    return cloaked != 0;
}

} // namespace flowkeyd::platform::win::dwm
