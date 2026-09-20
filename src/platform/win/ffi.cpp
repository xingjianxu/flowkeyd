#include "platform/win/ffi.h"

namespace flowkeyd::platform::win {

HMODULE moduleHandle()
{
    static HMODULE handle = GetModuleHandleW(nullptr);
    return handle;
}

QString winErrorMessage(unsigned long code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                            | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        code,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    QString text;
    if (length != 0 && buffer != nullptr) {
        text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
        LocalFree(buffer);
    }
    if (text.isEmpty()) {
        text = QStringLiteral("error %1").arg(code);
    }
    return text;
}

QString lastErrorMessage(const char *what)
{
    return QStringLiteral("%1 failed: %2")
        .arg(QString::fromLatin1(what), winErrorMessage(GetLastError()));
}

} // namespace flowkeyd::platform::win
