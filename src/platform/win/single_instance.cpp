#include "platform/win/single_instance.h"

#include <QDir>

#include <optional>
#include <utility>

namespace flowkeyd::platform::win {

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
