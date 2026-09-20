#include "platform/win/audio.h"

#include "platform/win/ffi.h"

#include <objbase.h>

#include <algorithm>
#include <cmath>

namespace flowkeyd::platform::win::audio {

namespace {

// --- 接口标识 --------------------------------------------------------------

const GUID kClsidMmDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidMmDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioEndpointVolume = {
    0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

/// `EDataFlow::eRender`
constexpr int kERender = 0;
/// `ERole::eMultimedia`
constexpr int kEMultimedia = 1;

// --- 手写 vtable -----------------------------------------------------------
//
// 按 Windows SDK 头文件的声明顺序排列，这是 COM 唯一的契约。

struct IUnknownVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
};

struct IMMDeviceEnumeratorVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    HRESULT(WINAPI *enumAudioEndpoints)(void *, int, DWORD, void **);
    HRESULT(WINAPI *getDefaultAudioEndpoint)(void *, int, int, void **);
    HRESULT(WINAPI *getDevice)(void *, const wchar_t *, void **);
    HRESULT(WINAPI *registerEndpointNotificationCallback)(void *, void *);
    HRESULT(WINAPI *unregisterEndpointNotificationCallback)(void *, void *);
};

struct IMMDeviceVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    HRESULT(WINAPI *activate)(void *, const GUID *, DWORD, void *, void **);
    HRESULT(WINAPI *openPropertyStore)(void *, DWORD, void **);
    HRESULT(WINAPI *getId)(void *, wchar_t **);
    HRESULT(WINAPI *getState)(void *, DWORD *);
};

struct IAudioEndpointVolumeVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    HRESULT(WINAPI *registerControlChangeNotify)(void *, void *);
    HRESULT(WINAPI *unregisterControlChangeNotify)(void *, void *);
    HRESULT(WINAPI *getChannelCount)(void *, UINT *);
    HRESULT(WINAPI *setMasterVolumeLevel)(void *, float, const GUID *);
    HRESULT(WINAPI *setMasterVolumeLevelScalar)(void *, float, const GUID *);
    HRESULT(WINAPI *getMasterVolumeLevel)(void *, float *);
    HRESULT(WINAPI *getMasterVolumeLevelScalar)(void *, float *);
    HRESULT(WINAPI *setChannelVolumeLevel)(void *, UINT, float, const GUID *);
    HRESULT(WINAPI *setChannelVolumeLevelScalar)(void *, UINT, float, const GUID *);
    HRESULT(WINAPI *getChannelVolumeLevel)(void *, UINT, float *);
    HRESULT(WINAPI *getChannelVolumeLevelScalar)(void *, UINT, float *);
    HRESULT(WINAPI *setMute)(void *, BOOL, const GUID *);
    HRESULT(WINAPI *getMute)(void *, BOOL *);
    HRESULT(WINAPI *getVolumeStepInfo)(void *, UINT *, UINT *);
    HRESULT(WINAPI *volumeStepUp)(void *, const GUID *);
    HRESULT(WINAPI *volumeStepDown)(void *, const GUID *);
    HRESULT(WINAPI *queryHardwareSupport)(void *, DWORD *);
    HRESULT(WINAPI *getVolumeRange)(void *, float *, float *, float *);
};

template <typename Vtbl>
const Vtbl *vtableOf(void *object)
{
    return *reinterpret_cast<const Vtbl *const *>(object);
}

/// 所有 COM 接口的前三个槽都是 `IUnknown`，释放可以统一处理。
void releaseUnknown(void *object)
{
    if (object == nullptr) {
        return;
    }
    vtableOf<IUnknownVtbl>(object)->release(object);
}

/// 带引用计数的 COM 接口指针。
class ComPtr
{
public:
    ComPtr() = default;
    explicit ComPtr(void *object) : m_object(object) {}
    ~ComPtr() { releaseUnknown(m_object); }

    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    void *get() const { return m_object; }
    void reset() { m_object = nullptr; }

private:
    void *m_object = nullptr;
};

/// COM 单元（apartment）初始化守卫。
///
/// 已经激活了另一种单元模型（`RPC_E_CHANGED_MODE`）时仍然可用，
/// 只是不该由我们去 `CoUninitialize`。
class Apartment
{
public:
    Apartment() = default;
    ~Apartment()
    {
        if (m_owned) {
            CoUninitialize();
        }
    }

    Apartment(const Apartment &) = delete;
    Apartment &operator=(const Apartment &) = delete;

    bool enter(QString *error)
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == RPC_E_CHANGED_MODE) {
            return true;
        }
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "CoInitializeEx");
            }
            return false;
        }
        m_owned = true;
        return true;
    }

private:
    bool m_owned = false;
};

/// 默认渲染端点的音量控制。
class Endpoint
{
public:
    Endpoint() = default;
    ~Endpoint() { releaseUnknown(m_object); }

    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;

    bool open(QString *error)
    {
        void *enumerator = nullptr;
        HRESULT hr = CoCreateInstance(kClsidMmDeviceEnumerator, nullptr, CLSCTX_ALL,
                                      kIidMmDeviceEnumerator, &enumerator);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "CoCreateInstance(MMDeviceEnumerator)");
            }
            return false;
        }
        ComPtr enumeratorGuard(enumerator);

        void *device = nullptr;
        hr = vtableOf<IMMDeviceEnumeratorVtbl>(enumerator)
                 ->getDefaultAudioEndpoint(enumerator, kERender, kEMultimedia, &device);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "GetDefaultAudioEndpoint(eRender)");
            }
            return false;
        }
        ComPtr deviceGuard(device);

        void *volume = nullptr;
        hr = vtableOf<IMMDeviceVtbl>(device)
                 ->activate(device, &kIidAudioEndpointVolume, CLSCTX_ALL, nullptr, &volume);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "IMMDevice::Activate(IAudioEndpointVolume)");
            }
            return false;
        }
        m_object = volume;
        return true;
    }

    bool getScalar(float *out, QString *error) const
    {
        float value = 0.0F;
        const HRESULT hr =
            vtableOf<IAudioEndpointVolumeVtbl>(m_object)->getMasterVolumeLevelScalar(m_object, &value);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "GetMasterVolumeLevelScalar");
            }
            return false;
        }
        *out = value;
        return true;
    }

    bool setScalar(float value, QString *error) const
    {
        const HRESULT hr = vtableOf<IAudioEndpointVolumeVtbl>(m_object)
                               ->setMasterVolumeLevelScalar(m_object, value, nullptr);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "SetMasterVolumeLevelScalar");
            }
            return false;
        }
        return true;
    }

    bool getMute(bool *out, QString *error) const
    {
        BOOL mute = FALSE;
        const HRESULT hr = vtableOf<IAudioEndpointVolumeVtbl>(m_object)->getMute(m_object, &mute);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "GetMute");
            }
            return false;
        }
        *out = mute != FALSE;
        return true;
    }

    bool setMute(bool mute, QString *error) const
    {
        const HRESULT hr = vtableOf<IAudioEndpointVolumeVtbl>(m_object)
                               ->setMute(m_object, mute ? TRUE : FALSE, nullptr);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "SetMute");
            }
            return false;
        }
        return true;
    }

private:
    void *m_object = nullptr;
};

int percentOf(float scalar)
{
    return static_cast<int>(std::lround(scalar * 100.0F));
}

} // namespace

float nextVolumeScalar(float current,
                       core::VolumeOp op,
                       std::optional<std::uint8_t> level,
                       std::optional<std::uint8_t> step)
{
    switch (op) {
    case core::VolumeOp::Set: {
        const float target = static_cast<float>(level.value_or(0));
        return std::clamp(target, 0.0F, 100.0F) / 100.0F;
    }
    case core::VolumeOp::Up: {
        const auto raw = std::max<std::uint8_t>(step.value_or(2), 1);
        return std::min(1.0F, current + static_cast<float>(raw) / 100.0F);
    }
    case core::VolumeOp::Down: {
        const auto raw = std::max<std::uint8_t>(step.value_or(2), 1);
        return std::max(0.0F, current - static_cast<float>(raw) / 100.0F);
    }
    case core::VolumeOp::Mute:
    case core::VolumeOp::Unmute:
    case core::VolumeOp::Toggle:
        break;
    }
    return current;
}

bool apply(core::VolumeOp op,
           std::optional<std::uint8_t> level,
           std::optional<std::uint8_t> step,
           QString *detail,
           QString *error)
{
    if (op == core::VolumeOp::Set && !level.has_value()) {
        if (error != nullptr) {
            *error = QStringLiteral("`volume` op = \"set\" needs `level = 0..100`");
        }
        return false;
    }
    Apartment apartment;
    if (!apartment.enter(error)) {
        return false;
    }
    Endpoint endpoint;
    if (!endpoint.open(error)) {
        return false;
    }

    switch (op) {
    case core::VolumeOp::Set: {
        const auto clamped = std::min<std::uint8_t>(*level, 100);
        if (!endpoint.setScalar(static_cast<float>(clamped) / 100.0F, error)) {
            return false;
        }
        if (detail != nullptr) {
            *detail = QStringLiteral("%1%").arg(clamped);
        }
        return true;
    }
    case core::VolumeOp::Up:
    case core::VolumeOp::Down: {
        float current = 0.0F;
        if (!endpoint.getScalar(&current, error)) {
            return false;
        }
        const float next = nextVolumeScalar(current, op, level, step);
        if (!endpoint.setScalar(next, error)) {
            return false;
        }
        if (detail != nullptr) {
            *detail = QStringLiteral("%1%").arg(percentOf(next));
        }
        return true;
    }
    case core::VolumeOp::Mute:
        if (!endpoint.setMute(true, error)) {
            return false;
        }
        if (detail != nullptr) {
            *detail = QStringLiteral("muted");
        }
        return true;
    case core::VolumeOp::Unmute:
        if (!endpoint.setMute(false, error)) {
            return false;
        }
        if (detail != nullptr) {
            *detail = QStringLiteral("unmuted");
        }
        return true;
    case core::VolumeOp::Toggle: {
        bool muted = false;
        if (!endpoint.getMute(&muted, error)) {
            return false;
        }
        if (!endpoint.setMute(!muted, error)) {
            return false;
        }
        if (detail != nullptr) {
            *detail = muted ? QStringLiteral("unmuted") : QStringLiteral("muted");
        }
        return true;
    }
    }
    return false;
}

bool getPercent(int *out, QString *error)
{
    Apartment apartment;
    if (!apartment.enter(error)) {
        return false;
    }
    Endpoint endpoint;
    if (!endpoint.open(error)) {
        return false;
    }
    float scalar = 0.0F;
    if (!endpoint.getScalar(&scalar, error)) {
        return false;
    }
    *out = percentOf(scalar);
    return true;
}

bool isMuted(bool *out, QString *error)
{
    Apartment apartment;
    if (!apartment.enter(error)) {
        return false;
    }
    Endpoint endpoint;
    if (!endpoint.open(error)) {
        return false;
    }
    return endpoint.getMute(out, error);
}

} // namespace flowkeyd::platform::win::audio
