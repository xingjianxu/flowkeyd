#include "platform/win/single_instance.h"

#include <QDir>

#include <sddl.h>

#include <optional>
#include <utility>
#include <vector>

namespace flowkeyd::platform::win {

namespace {

/// 退出事件的安全描述符字符串：
///
/// * `D:(A;;GA;;;<当前用户 SID>)` —— 只给这个用户账号完全访问（不用 Everyone，
///   否则同机器上任何账号都能把守护进程关掉）；
/// * `S:(ML;;NW;;;LW)` —— 给对象打 **Low** 强制完整性标签。提权的守护进程会
///   以 High 创建对象，而「no write up」禁止低完整性主体写高完整性对象；
///   把标签压到 Low 之后，非提权的 `--quit` 也能 `SetEvent`。
QString quitEventSecurityDescriptor()
{
    QString user = QStringLiteral("WD"); // 兜底：Everyone（拿不到 SID 时）
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != 0) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        if (size > 0) {
            std::vector<unsigned char> buffer(size);
            if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0) {
                const auto *entry = reinterpret_cast<const TOKEN_USER *>(buffer.data());
                LPWSTR text = nullptr;
                if (ConvertSidToStringSidW(entry->User.Sid, &text) != 0) {
                    user = QString::fromWCharArray(text);
                    LocalFree(text);
                }
            }
        }
        CloseHandle(token);
    }
    return QStringLiteral("D:(A;;GA;;;%1)S:(ML;;NW;;;LW)").arg(user);
}

} // namespace

QString quitEventName(const QString &key)
{
    return key + QStringLiteral("-quit");
}

HANDLE createQuitEvent(const QString &key, QString *error)
{
    const std::wstring name = quitEventName(key).toStdWString();

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = FALSE;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const std::wstring sddl = quitEventSecurityDescriptor().toStdWString();
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                            &descriptor, nullptr) == 0) {
        // 退回默认描述符：对象会带上我们的完整性级别，于是 `--quit` 得用同样的
        // 权限才叫得动它（脚本里会看到一条 warning），但守护进程照常跑。
        descriptor = nullptr;
    }
    attributes.lpSecurityDescriptor = descriptor;

    SetLastError(ERROR_SUCCESS);
    HANDLE handle = CreateEventW(&attributes, FALSE, FALSE, name.c_str());
    const DWORD last = GetLastError();
    if (descriptor != nullptr) {
        LocalFree(descriptor);
    }
    if (handle == nullptr) {
        if (error != nullptr) {
            *error = winErrorMessage(last);
        }
        return nullptr;
    }
    return handle;
}

bool requestQuit(const QString &key, bool *running, QString *error)
{
    if (running != nullptr) {
        *running = false;
    }
    const std::wstring name = quitEventName(key).toStdWString();
    SetLastError(ERROR_SUCCESS);
    HANDLE handle = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (handle == nullptr) {
        const DWORD last = GetLastError();
        if (last == ERROR_FILE_NOT_FOUND) {
            return true; // 没有在跑的实例：不是错误
        }
        if (error != nullptr) {
            *error = winErrorMessage(last);
        }
        return false;
    }
    if (running != nullptr) {
        *running = true;
    }
    const bool ok = SetEvent(handle) != 0;
    const QString failure = ok ? QString() : winErrorMessage(GetLastError());
    CloseHandle(handle);
    if (!ok && error != nullptr) {
        *error = failure;
    }
    return ok;
}

bool quitEventExists(const QString &key)
{
    HANDLE handle = OpenEventW(SYNCHRONIZE, FALSE, quitEventName(key).toStdWString().c_str());
    if (handle == nullptr) {
        return false;
    }
    CloseHandle(handle);
    return true;
}

std::uint64_t fnv1a64(const QByteArray &bytes)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (char byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

QString instanceKey(const QString &configPath)
{
    const QString normalized =
        QDir::toNativeSeparators(QDir::cleanPath(configPath)).toLower();
    const std::uint64_t hash = fnv1a64(normalized.toUtf8());
    return QStringLiteral("Local\\flowkeyd-%1").arg(hash, 16, 16, QLatin1Char('0'));
}

SingleInstance::~SingleInstance()
{
    if (m_handle != nullptr) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
}

SingleInstance::SingleInstance(SingleInstance &&other) noexcept
    : m_handle(other.m_handle), m_alreadyRunning(other.m_alreadyRunning)
{
    other.m_handle = nullptr;
    other.m_alreadyRunning = false;
}

SingleInstance &SingleInstance::operator=(SingleInstance &&other) noexcept
{
    if (this != &other) {
        if (m_handle != nullptr) {
            CloseHandle(m_handle);
        }
        m_handle = other.m_handle;
        m_alreadyRunning = other.m_alreadyRunning;
        other.m_handle = nullptr;
        other.m_alreadyRunning = false;
    }
    return *this;
}

std::optional<SingleInstance> SingleInstance::acquire(const QString &key,
                                                     bool *alreadyRunning,
                                                     QString *error)
{
    const std::wstring wide = key.toStdWString();
    SetLastError(ERROR_SUCCESS);
    HANDLE handle = CreateMutexW(nullptr, FALSE, wide.c_str());
    if (handle == nullptr) {
        if (error != nullptr) {
            *error = lastErrorMessage("CreateMutexW");
        }
        return std::nullopt;
    }
    SingleInstance instance;
    instance.m_handle = handle;
    instance.m_alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    if (alreadyRunning != nullptr) {
        *alreadyRunning = instance.m_alreadyRunning;
    }
    return instance;
}

} // namespace flowkeyd::platform::win
