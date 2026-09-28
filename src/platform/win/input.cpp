#include "platform/win/input.h"

#include "platform/win/logging.h"
#include "platform/win/nt.h"

#include <QVector>

#include <cstdlib>
#include <mutex>
#include <utility>

namespace flowkeyd::platform::win {

namespace {

/// 可能被按住的修饰键，按 Ctrl/Alt/Shift/Win 的顺序排列。
struct ModifierKey
{
    core::Vk vk;
    core::Modifiers mods;
};

const ModifierKey kModifierKeys[] = {
    {core::vk::LCONTROL, core::Modifiers::Ctrl},
    {core::vk::RCONTROL, core::Modifiers::Ctrl},
    {core::vk::LMENU, core::Modifiers::Alt},
    {core::vk::RMENU, core::Modifiers::Alt},
    {core::vk::LSHIFT, core::Modifiers::Shift},
    {core::vk::RSHIFT, core::Modifiers::Shift},
    {core::vk::LWIN, core::Modifiers::Win},
    {core::vk::RWIN, core::Modifiers::Win},
};

std::mutex &preferenceMutex()
{
    static std::mutex mutex;
    return mutex;
}

BackendPreference &storedPreference()
{
    static BackendPreference preference = BackendPreference::Auto;
    return preference;
}

} // namespace

bool acceptInjectedInput()
{
    // 只要变量存在（且非空）就算开启。
    static const bool accept = [] {
        const char *value = std::getenv("FLOWKEYD_ACCEPT_INJECTED");
        return value != nullptr && *value != '\0';
    }();
    return accept;
}

std::optional<BackendPreference> parseBackendPreference(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("auto")) {
        return BackendPreference::Auto;
    }
    if (key == QLatin1String("user32") || key == QLatin1String("documented")) {
        return BackendPreference::User32;
    }
    if (key == QLatin1String("ntuser") || key == QLatin1String("win32u")
        || key == QLatin1String("undocumented")) {
        return BackendPreference::NtUser;
    }
    return std::nullopt;
}

namespace {

enum class ResolvedBackend { User32, NtUser };

ResolvedBackend resolveBackend()
{
    BackendPreference preference = BackendPreference::Auto;
    {
        std::lock_guard<std::mutex> lock(preferenceMutex());
        preference = storedPreference();
    }
    const bool ntReady = nt::ntUser().sendInputVerified;
    switch (preference) {
    case BackendPreference::User32:
        return ResolvedBackend::User32;
    case BackendPreference::NtUser:
        if (ntReady) {
            return ResolvedBackend::NtUser;
        }
        logWarn(QStringLiteral(
            "settings.input_backend = \"ntuser\" but win32u!NtUserSendInput is unavailable; "
            "falling back to user32!SendInput"));
        return ResolvedBackend::User32;
    case BackendPreference::Auto:
        return ntReady ? ResolvedBackend::NtUser : ResolvedBackend::User32;
    }
    return ResolvedBackend::User32;
}

ResolvedBackend backend()
{
    static const ResolvedBackend resolved = resolveBackend();
    return resolved;
}

} // namespace

void configureInput(BackendPreference preference)
{
    {
        std::lock_guard<std::mutex> lock(preferenceMutex());
        storedPreference() = preference;
    }
    // 强制解析一次，以便启动横幅能报告实际生效的后端。
    (void)backend();
}

QString inputBackendName()
{
    return backend() == ResolvedBackend::NtUser ? QStringLiteral("win32u!NtUserSendInput")
                                               : QStringLiteral("user32!SendInput");
}

int inputSize()
{
    return static_cast<int>(sizeof(INPUT));
}

INPUT keyInput(core::Vk vk, bool down)
{
    const auto [native, extended] = core::nativeKey(vk);
    DWORD flags = 0;
    if (!down) {
        flags |= KEYEVENTF_KEYUP;
    }
    if (extended) {
        flags |= KEYEVENTF_EXTENDEDKEY;
    }
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = native;
    input.ki.wScan = 0;
    input.ki.dwFlags = flags;
    input.ki.time = 0;
    input.ki.dwExtraInfo = kSyntheticTag;
    return input;
}

INPUT unicodeInput(char16_t unit, bool down)
{
    DWORD flags = KEYEVENTF_UNICODE;
    if (!down) {
        flags |= KEYEVENTF_KEYUP;
    }
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = unit;
    input.ki.dwFlags = flags;
    input.ki.time = 0;
    input.ki.dwExtraInfo = kSyntheticTag;
    return input;
}

bool sendInputs(const std::vector<INPUT> &inputs, QString *error)
{
    if (inputs.empty()) {
        return true;
    }
    if (backend() == ResolvedBackend::NtUser) {
        if (const auto result = nt::sendInput(inputs.data(), static_cast<std::uint32_t>(inputs.size()));
            result.has_value()) {
            if (*result) {
                return true;
            }
            logDebug(QStringLiteral("NtUserSendInput failed; falling back to SendInput"));
        }
    }
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()),
                                const_cast<INPUT *>(inputs.data()),
                                static_cast<int>(sizeof(INPUT)));
    if (sent == inputs.size()) {
        return true;
    }
    if (error != nullptr) {
        *error = lastErrorMessage("SendInput");
    }
    return false;
}

bool sendOps(const std::vector<core::SendOp> &ops, bool allowSleep, QString *error)
{
    constexpr int kBatch = 64;
    std::vector<INPUT> batch;
    batch.reserve(kBatch);
    const auto flush = [&]() -> bool {
        if (batch.empty()) {
            return true;
        }
        const bool ok = sendInputs(batch, error);
        batch.clear();
        return ok;
    };
    for (const core::SendOp &op : ops) {
        switch (op.kind) {
        case core::SendOp::Kind::Key:
            batch.push_back(keyInput(op.vk, op.down));
            break;
        case core::SendOp::Kind::Text:
            batch.push_back(unicodeInput(op.text, true));
            batch.push_back(unicodeInput(op.text, false));
            break;
        case core::SendOp::Kind::Sleep:
            if (!flush()) {
                return false;
            }
            if (allowSleep) {
                Sleep(op.ms);
            } else {
                logDebug(QStringLiteral("ignoring {Sleep %1} inside the hook callback").arg(op.ms));
            }
            break;
        }
        if (static_cast<int>(batch.size()) >= kBatch) {
            if (!flush()) {
                return false;
            }
        }
    }
    return flush();
}

bool sendOps(const QVector<core::SendOp> &ops, bool allowSleep, QString *error)
{
    return sendOps(std::vector<core::SendOp>(ops.begin(), ops.end()), allowSleep, error);
}

bool tapKey(core::Vk vk, QString *error)
{
    const std::vector<INPUT> inputs{keyInput(vk, true), keyInput(vk, false)};
    return sendInputs(inputs, error);
}

bool copySelection(std::uint64_t waitMs, QString *error)
{
    ModifierGuard guard = ModifierGuard::release();
    const std::vector<core::SendOp> ops{
        core::SendOp::keyDown(core::vk::LCONTROL),
        core::SendOp::keyDown(static_cast<core::Vk>('C')),
        core::SendOp::keyUp(static_cast<core::Vk>('C')),
        core::SendOp::keyUp(core::vk::LCONTROL),
    };
    QString localError;
    const bool ok = sendOps(ops, false, &localError);
    guard.restore();
    if (!ok) {
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    Sleep(static_cast<DWORD>(waitMs));
    return true;
}

bool isKeyDown(core::Vk vk)
{
    if (const auto state = nt::asyncKeyState(static_cast<int>(vk)); state.has_value()) {
        return (static_cast<std::uint16_t>(*state) & 0x8000u) != 0;
    }
    return (static_cast<std::uint16_t>(GetAsyncKeyState(static_cast<int>(vk))) & 0x8000u) != 0;
}

bool capsLockOn()
{
    return (static_cast<std::uint16_t>(GetKeyState(static_cast<int>(core::vk::CAPITAL))) & 1u) != 0;
}

core::Modifiers modifiersDown()
{
    core::Modifiers mods;
    for (const ModifierKey &entry : kModifierKeys) {
        if (isKeyDown(entry.vk)) {
            mods = mods.unioned(entry.mods);
        }
    }
    return mods;
}

std::vector<core::SendOp> modifierReleasePlan(const std::vector<core::Vk> &held)
{
    std::vector<core::SendOp> ops;
    for (core::Vk vk : held) {
        if (vk == core::vk::LWIN || vk == core::vk::RWIN || vk == core::vk::LMENU
            || vk == core::vk::RMENU) {
            ops.push_back(core::SendOp::keyDown(core::vk::UNASSIGNED));
            ops.push_back(core::SendOp::keyUp(core::vk::UNASSIGNED));
        }
        ops.push_back(core::SendOp::keyUp(vk));
    }
    return ops;
}

std::vector<core::SendOp> modifierRestorePlan(const std::vector<core::Vk> &released)
{
    std::vector<core::SendOp> ops;
    ops.reserve(released.size());
    for (auto it = released.rbegin(); it != released.rend(); ++it) {
        ops.push_back(core::SendOp::keyDown(*it));
    }
    return ops;
}

ModifierGuard::ModifierGuard(ModifierGuard &&other) noexcept
    : m_released(std::move(other.m_released)), m_active(other.m_active)
{
    other.m_active = false;
    other.m_released.clear();
}

ModifierGuard &ModifierGuard::operator=(ModifierGuard &&other) noexcept
{
    if (this != &other) {
        restoreInner();
        m_released = std::move(other.m_released);
        m_active = other.m_active;
        other.m_active = false;
        other.m_released.clear();
    }
    return *this;
}

ModifierGuard::~ModifierGuard()
{
    restoreInner();
}

ModifierGuard ModifierGuard::none()
{
    ModifierGuard guard;
    guard.m_active = false;
    return guard;
}

ModifierGuard ModifierGuard::release()
{
    ModifierGuard guard;
    std::vector<core::Vk> held;
    for (const ModifierKey &entry : kModifierKeys) {
        if (isKeyDown(entry.vk)) {
            held.push_back(entry.vk);
        }
    }
    if (held.empty()) {
        return guard;
    }
    const std::vector<core::SendOp> plan = modifierReleasePlan(held);
    QString error;
    if (!sendOps(plan, false, &error)) {
        logWarn(QStringLiteral("failed to release held modifiers: %1").arg(error));
        return guard;
    }
    guard.m_released = held;
    guard.m_active = true;
    QStringList names;
    names.reserve(static_cast<int>(guard.m_released.size()));
    for (core::Vk vk : guard.m_released) {
        names.append(core::nameFromKey(vk));
    }
    logDebug(QStringLiteral("released held modifier(s) %1 for the duration of the action")
                 .arg(names.join(QLatin1Char('+'))));
    return guard;
}

void ModifierGuard::restore()
{
    restoreInner();
}

void ModifierGuard::restoreInner()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    const std::vector<core::SendOp> plan = modifierRestorePlan(m_released);
    if (!plan.empty()) {
        QString error;
        if (!sendOps(plan, false, &error)) {
            logWarn(QStringLiteral("failed to re-press held modifiers: %1").arg(error));
        }
    }
    m_released.clear();
}

} // namespace flowkeyd::platform::win
