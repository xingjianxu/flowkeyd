#include "platform/win/ime.h"

#include <cstring>

namespace flowkeyd::platform::win::ime {

namespace {

/// `imm.h` 里的 `HIMC`（`DECLARE_HANDLE`）：这里只用得到它是个不透明句柄。
using InputContext = void *;

/// `IME_CMODE_NATIVE`（`imm.h`）：置位 = 中文/假名这类「本地」输入模式
/// （语言栏上的「中」），清零 = 字母数字（「英」）。手写这两个常量，免得为了
/// 它们把 `imm.h` 整套声明拉进来（与 `power.cpp` 对 `MONITOR_OFF` 的处理一样）。
constexpr DWORD kConversionModeNative = 0x0001;

using ImmGetContextFn = InputContext(WINAPI *)(HWND);
using ImmReleaseContextFn = BOOL(WINAPI *)(HWND, InputContext);
using ImmGetConversionStatusFn = BOOL(WINAPI *)(InputContext, LPDWORD, LPDWORD);
using ImmSetConversionStatusFn = BOOL(WINAPI *)(InputContext, DWORD, DWORD);

/// `imm32.dll` 上我们用到的那几个入口（拿不到时全是 nullptr）。
struct Imm
{
    ImmGetContextFn getContext = nullptr;
    ImmReleaseContextFn releaseContext = nullptr;
    ImmGetConversionStatusFn getConversionStatus = nullptr;
    ImmSetConversionStatusFn setConversionStatus = nullptr;

    bool available() const
    {
        return getContext != nullptr && releaseContext != nullptr
               && getConversionStatus != nullptr && setConversionStatus != nullptr;
    }
};

/// 把 `GetProcAddress` 的结果转成具体签名（`-Wcast-function-type` 讨厌直接转换，
/// 与 `dwm.cpp` / `nt.cpp` 同一套写法）。
template <typename Fn>
Fn resolve(HMODULE module, const char *name)
{
    const FARPROC proc = GetProcAddress(module, name);
    if (proc == nullptr) {
        return nullptr;
    }
    static_assert(sizeof(Fn) == sizeof(proc), "function pointer sizes must match");
    Fn fn = nullptr;
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

const Imm &imm()
{
    static const Imm instance = []() {
        Imm api;
        // 模块在进程生命周期内保持加载：不 FreeLibrary，免得指针悬空。
        HMODULE module = LoadLibraryW(L"imm32.dll");
        if (module == nullptr) {
            return api;
        }
        api.getContext = resolve<ImmGetContextFn>(module, "ImmGetContext");
        api.releaseContext = resolve<ImmReleaseContextFn>(module, "ImmReleaseContext");
        api.getConversionStatus =
            resolve<ImmGetConversionStatusFn>(module, "ImmGetConversionStatus");
        api.setConversionStatus =
            resolve<ImmSetConversionStatusFn>(module, "ImmSetConversionStatus");
        return api;
    }();
    return instance;
}

QString hexMode(DWORD value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

} // namespace

ModeSwitch useAlphanumericMode(HWND hwnd)
{
    ModeSwitch result;
    const Imm &api = imm();
    if (!api.available()) {
        result.ok = true; // 没有 imm32 时不折腾：键盘本来就是直接输入
        result.detail = QStringLiteral("imm32 is unavailable; the keyboard already types latin");
        return result;
    }
    if (hwnd == nullptr) {
        result.detail = QStringLiteral("no window to switch the input method for");
        return result;
    }
    InputContext context = api.getContext(hwnd);
    if (context == nullptr) {
        // 这个窗口没有输入上下文：通常是这台机器上没装输入法（或窗口正在销毁）。
        // 那种情况下不需要做任何事，所以按成功处理。
        result.ok = true;
        result.detail = QStringLiteral("the window has no input context; no input method to switch");
        return result;
    }

    DWORD conversion = 0;
    DWORD sentence = 0;
    const BOOL read = api.getConversionStatus(context, &conversion, &sentence);
    if (read != FALSE && (conversion & kConversionModeNative) == 0) {
        // 已经是英文模式：不再写一次，免得输入法指示器无谓地闪一下。
        api.releaseContext(hwnd, context);
        result.ok = true;
        result.detail = QStringLiteral("already english (conversion %1)").arg(hexMode(conversion));
        return result;
    }

    // 之前是中文（`NATIVE` 置位）：**只把那一位清掉**，其余标志原样保留 ——
    // 这正是用户按 Shift 做的事（MS 拼音就是翻 `TF_CONVERSIONMODE_NATIVE`），
    // 比直接写 `IME_CMODE_ALPHANUMERIC`（0）更保守：不会顺手把全角/标点之类的
    // 设置一起重置掉。句模式同理原样传回。
    const DWORD next = conversion & ~kConversionModeNative;
    const BOOL written = api.setConversionStatus(context, next, sentence);
    api.releaseContext(hwnd, context);
    if (written == FALSE) {
        result.detail = QStringLiteral("imm32!ImmSetConversionStatus failed (conversion %1)")
                            .arg(hexMode(conversion));
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.detail = QStringLiteral("switched from conversion %1 to %2")
                        .arg(hexMode(conversion), hexMode(next));
    return result;
}

} // namespace flowkeyd::platform::win::ime
