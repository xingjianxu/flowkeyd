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

QString hresultText(long code)
{
    const auto raw = static_cast<unsigned long>(code);
    const char *known = nullptr;
    switch (raw) {
    case 0x80004002UL:
        known = "the interface is not implemented (E_NOINTERFACE)";
        break;
    case 0x80004005UL:
        known = "unspecified failure (E_FAIL)";
        break;
    case 0x80070057UL:
        known = "invalid argument (E_INVALIDARG)";
        break;
    case 0x80040154UL:
        known = "the shell class is not registered (REGDB_E_CLASSNOTREG)";
        break;
    case 0x80040102UL:
        known = "the interface is not registered (REGDB_E_IIDNOTREG)";
        break;
    case 0x80010106UL:
        known = "wrong COM apartment model (RPC_E_CHANGED_MODE)";
        break;
    default:
        break;
    }
    if (known != nullptr) {
        return QStringLiteral("%1 (HRESULT 0x%2)")
            .arg(QString::fromLatin1(known))
            .arg(raw, 8, 16, QLatin1Char('0'));
    }
    return QStringLiteral("HRESULT 0x%1").arg(raw, 8, 16, QLatin1Char('0'));
}

QString hresultMessage(long code, const char *what)
{
    return QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(what), hresultText(code));
}

} // namespace flowkeyd::platform::win
