#include "platform/win/clipboard.h"

#include "platform/win/ffi.h"

#include <cstring>

namespace flowkeyd::platform::win::clipboard {

namespace {

constexpr int kOpenAttempts = 10;
/// 读取时的硬上限，防止被一个畸形的剪贴板句柄拖住。
constexpr std::size_t kMaxChars = 1u << 20;

bool openClipboard(QString *error)
{
    for (int attempt = 0; attempt < kOpenAttempts; ++attempt) {
        // 空的属主会把剪贴板关联到当前任务。
        if (OpenClipboard(nullptr) != 0) {
            return true;
        }
        Sleep(5 + static_cast<DWORD>(attempt));
    }
    if (error != nullptr) {
        *error = lastErrorMessage("OpenClipboard");
    }
    return false;
}

} // namespace

bool getText(QString *out, QString *error)
{
    if (!openClipboard(error)) {
        return false;
    }
    QString text;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != nullptr) {
        const auto *ptr = static_cast<const wchar_t *>(GlobalLock(handle));
        if (ptr == nullptr) {
            CloseClipboard();
            if (error != nullptr) {
                *error = lastErrorMessage("GlobalLock(clipboard)");
            }
            return false;
        }
        std::size_t length = 0;
        // 缓冲区以 NUL 结尾；扫描时加一个硬上限。
        while (length < kMaxChars && ptr[length] != L'\0') {
            ++length;
        }
        text = QString::fromWCharArray(ptr, static_cast<int>(length));
        GlobalUnlock(handle);
    }
    CloseClipboard();
    if (out != nullptr) {
        *out = text;
    }
    return true;
}

bool setText(const QString &text, QString *error)
{
    const int chars = static_cast<int>(text.size());
    const SIZE_T bytes = static_cast<SIZE_T>(chars + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (handle == nullptr) {
        if (error != nullptr) {
            *error = lastErrorMessage("GlobalAlloc(clipboard)");
        }
        return false;
    }
    auto *target = static_cast<wchar_t *>(GlobalLock(handle));
    if (target == nullptr) {
        GlobalFree(handle);
        if (error != nullptr) {
            *error = lastErrorMessage("GlobalLock(clipboard)");
        }
        return false;
    }
    if (chars > 0) {
        std::memcpy(target, text.utf16(), static_cast<std::size_t>(chars) * sizeof(wchar_t));
    }
    target[chars] = L'\0';
    GlobalUnlock(handle);

    if (!openClipboard(error)) {
        GlobalFree(handle);
        return false;
    }
    bool ok = true;
    if (EmptyClipboard() == 0) {
        ok = false;
        if (error != nullptr) {
            *error = lastErrorMessage("EmptyClipboard");
        }
    } else if (SetClipboardData(CF_UNICODETEXT, handle) == nullptr) {
        ok = false;
        if (error != nullptr) {
            *error = lastErrorMessage("SetClipboardData");
        }
    }
    CloseClipboard();
    if (!ok) {
        // 所有权还在我们手上，所以释放缓冲区。
        GlobalFree(handle);
    }
    return ok;
}

bool appendText(const QString &text, QString *error)
{
    QString current;
    if (!getText(&current, error)) {
        return false;
    }
    current += text;
    return setText(current, error);
}

bool clear(QString *error)
{
    if (!openClipboard(error)) {
        return false;
    }
    bool ok = true;
    if (EmptyClipboard() == 0) {
        ok = false;
        if (error != nullptr) {
            *error = lastErrorMessage("EmptyClipboard");
        }
    }
    CloseClipboard();
    return ok;
}

} // namespace flowkeyd::platform::win::clipboard
