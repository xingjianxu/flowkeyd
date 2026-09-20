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

QString hresultMessage(long code, const char *what)
{
    const auto raw = static_cast<unsigned long>(code);
    QString text = winErrorMessage(raw);
    if (text.startsWith(QLatin1String("error "))) {
        text = QStringLiteral("0x%1").arg(raw, 8, 16, QLatin1Char('0'));
    }
    return QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(what), text);
}

} // namespace flowkeyd::platform::win
