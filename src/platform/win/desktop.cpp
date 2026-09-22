#include "platform/win/desktop.h"

#include "platform/win/logging.h"

#include <objbase.h>

#include <cstring>
#include <optional>
#include <thread>
#include <utility>

namespace flowkeyd::platform::win::desktop {

namespace {

// --- 接口标识 --------------------------------------------------------------

/// `CLSID_ImmersiveShell` —— shell 的服务提供者，所有这些内部接口的入口。
const GUID kClsidImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
/// `IID_IServiceProvider`
const GUID kIidServiceProvider = {
    0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};
/// `SID_IVirtualDesktopManagerInternal`：`QueryService` 用它请求虚拟桌面管理器。
const GUID kSidManagerInternal = {
    0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};
/// `IID_IApplicationViewCollection`（`QueryService` 的 SID 与 IID 是同一个 GUID）：
/// 把 `HWND` 换成 shell 侧的 `IApplicationView*`，`MoveViewToDesktop` 要的就是它。
const GUID kIidApplicationViewCollection = {
    0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};
/// `CLSID_VirtualDesktopManager` —— **已公开**的那个管理器（只能动自己进程的窗口）。
const GUID kClsidVirtualDesktopManager = {
    0xAA509086, 0x5CA9, 0x4C25, {0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A}};
/// `IID_IVirtualDesktopManager`
const GUID kIidVirtualDesktopManager = {
    0xA5CD92FF, 0x29BE, 0x454C, {0x8D, 0x04, 0xD8, 0x28, 0x79, 0xFB, 0x3F, 0x1B}};

// --- 手写 vtable -----------------------------------------------------------
//
// 字段下标就是 vtable 下标（前三个是 `IUnknown`）。未用到的方法只留 `void *`
// 占位，因为各版本的签名不一致，写出来反而会误导。
// 布局照抄 `../oskeyd/src/win/desktop.rs`。

struct IUnknownVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
};

/// `IServiceProvider`：`IUnknown` + `QueryService`。
struct IServiceProviderVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    HRESULT(WINAPI *queryService)(void *, const GUID *, const GUID *, void **);
};

/// `IObjectArray`：`IUnknown` + `GetCount` + `GetAt`。
struct IObjectArrayVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    HRESULT(WINAPI *getCount)(void *, UINT *);
    HRESULT(WINAPI *getAt)(void *, UINT, const GUID *, void **);
};

/// `IApplicationViewCollection`：`IUnknown` + 三个未用到的 `GetViews*` +
/// `GetViewForHwnd`（vtable 下标 6）。
struct IApplicationViewCollectionVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    void *getViews;
    void *getViewsByZOrder;
    void *getViewsByAppUserModelId;
    HRESULT(WINAPI *getViewForHwnd)(void *, HWND, void **);
};

/// **已公开**的 `IVirtualDesktopManager`：`IUnknown` +
/// `IsWindowOnCurrentVirtualDesktop`（下标 3）+ `GetWindowDesktopId`（下标 4）。
/// 只用它做只读查询。
struct IVirtualDesktopManagerVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    HRESULT(WINAPI *isWindowOnCurrentVirtualDesktop)(void *, HWND, BOOL *);
    HRESULT(WINAPI *getWindowDesktopId)(void *, HWND, GUID *);
};

/// 未公开的 `IVirtualDesktop`：`IUnknown` + `IsViewVisible`（下标 3，没用到）+
/// `GetID`（下标 4）。
///
/// `GetID` 只用来把**内部**的桌面对象与**已公开**的 `GetWindowDesktopId` 给出的
/// GUID 对上号（`switchToWindowDesktop` 需要知道窗口在枚举结果里的下标，而
/// `IVirtualDesktopManagerInternal` 没有「这个窗口在哪张桌面」这种查询）。
/// 布局事实与 VD.ahk 的 `VD_getDesktopOfWindow` 一致（它也用下标 4）；
/// `IsViewVisible` 仍写下标 3 的占位，免得后面有人误以为 3、4 之间还有别的方法。
struct IVirtualDesktopVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    void *isViewVisible;
    HRESULT(WINAPI *getId)(void *, GUID *);
};

/// `Layout::Plain` 的 `IVirtualDesktopManagerInternal` vtable。
struct ManagerPlainVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    void *index3;
    /// 4 `MoveViewToDesktop(IApplicationView*, IVirtualDesktop*)`：
    /// 三种布局的签名一致（不带 `HMONITOR`），所以下标 4 可以统一调用。
    HRESULT(WINAPI *moveViewToDesktop)(void *, void *, void *);
    void *index5;
    HRESULT(WINAPI *getCurrentDesktop)(void *, void **);
    HRESULT(WINAPI *getDesktops)(void *, void **);
    void *index8;
    HRESULT(WINAPI *switchDesktop)(void *, void *);
};

/// `Layout::Monitor`（Windows 11 21H2 / 22H2 早期）：
/// 三个方法多一个 `HMONITOR` 参数，索引不变。
struct ManagerMonitorVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    void *index3;
    HRESULT(WINAPI *moveViewToDesktop)(void *, void *, void *);
    void *index5;
    HRESULT(WINAPI *getCurrentDesktop)(void *, void *, void **);
    HRESULT(WINAPI *getDesktops)(void *, void *, void **);
    void *index8;
    HRESULT(WINAPI *switchDesktop)(void *, void *, void *);
};

/// `Layout::MonitorShifted`（Windows 11 22H2 22621.2215+）：
/// 带 `HMONITOR`，但 `GetDesktops` / `SwitchDesktop` 在 8 / 10。
struct ManagerMonitorShiftedVtbl
{
    HRESULT(WINAPI *queryInterface)(void *, const GUID *, void **);
    ULONG(WINAPI *addRef)(void *);
    ULONG(WINAPI *release)(void *);
    void *index3;
    HRESULT(WINAPI *moveViewToDesktop)(void *, void *, void *);
    void *index5;
    HRESULT(WINAPI *getCurrentDesktop)(void *, void *, void **);
    void *index7;
    HRESULT(WINAPI *getDesktops)(void *, void *, void **);
    void *index9;
    HRESULT(WINAPI *switchDesktop)(void *, void *, void *);
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
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    ComPtr(ComPtr &&other) noexcept : m_object(other.m_object) { other.m_object = nullptr; }
    ComPtr &operator=(ComPtr &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_object = other.m_object;
            other.m_object = nullptr;
        }
        return *this;
    }

    void *get() const { return m_object; }

    void reset()
    {
        releaseUnknown(m_object);
        m_object = nullptr;
    }

    /// 两个接口指针是否指向同一个 COM 对象（VD.ahk 判定“当前桌面”的方式）。
    bool same(const ComPtr &other) const { return m_object == other.m_object; }

private:
    void *m_object = nullptr;
};

/// COM 单元（apartment）初始化守卫。与音频后端同形，但这里要的是 STA。
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
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hr == RPC_E_CHANGED_MODE) {
            // 这个线程已经是别的单元模型；对象照样能用，只是别去反初始化。
            return true;
        }
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "CoInitializeEx(STA)");
            }
            return false;
        }
        m_owned = true;
        return true;
    }

private:
    bool m_owned = false;
};

/// 已打开的虚拟桌面管理器。
class Session
{
public:
    Session() = default;

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    /// 连接 shell 并请求虚拟桌面管理器。
    bool open(const ApiEntry &api, const WindowsVersion &version, QString *error);

    /// 按 Task View 顺序（也就是从左到右）枚举桌面。
    bool enumerateDesktops(std::vector<ComPtr> *out, QString *error) const;

    /// 只读快照：桌面数量与当前桌面序号。
    bool snapshot(Snapshot *out, QString *error) const;

    /// 切到第 `index` 个桌面（从 1 开始）。
    bool goTo(std::uint32_t index, QString *detail, QString *error) const;

    /// 切到 `hwnd` 所在的那张桌面。
    bool goToWindowDesktop(HWND hwnd, QString *detail, QString *error) const;

    /// 把窗口移到第 `index` 个桌面（从 1 开始）。
    bool moveWindowToDesktop(HWND hwnd, std::uint32_t index, QString *detail, QString *error) const;

    /// 已公开的 `IVirtualDesktopManager::GetWindowDesktopId`（只读）。
    std::optional<QString> windowDesktopId(HWND hwnd) const;

    /// `hwnd` 所在桌面在 `desktops` 里的下标。
    std::optional<std::size_t> desktopIndexOfWindow(HWND hwnd,
                                                   const std::vector<ComPtr> &desktops,
                                                   QString *error) const;

private:
    bool currentDesktop(ComPtr *out, QString *error) const;
    bool getDesktops(ComPtr *out, QString *error) const;
    bool switchDesktop(const ComPtr &target, QString *error) const;

    const ApiEntry *m_api = nullptr;
    ComPtr m_manager;
    /// 把 `HWND` 换成 `IApplicationView*` 的集合；拿不到时移动窗口不可用。
    ComPtr m_viewCollection;
    /// 已公开的 `IVirtualDesktopManager`；只用来读窗口在哪个桌面。
    ComPtr m_publicManager;
};

bool Session::open(const ApiEntry &api, const WindowsVersion &version, QString *error)
{
    void *provider = nullptr;
    HRESULT hr = CoCreateInstance(
        kClsidImmersiveShell, nullptr, CLSCTX_ALL, kIidServiceProvider, &provider);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "CoCreateInstance(ImmersiveShell)");
        }
        return false;
    }
    ComPtr providerGuard(provider);

    void *manager = nullptr;
    hr = vtableOf<IServiceProviderVtbl>(provider)->queryService(
        provider, &kSidManagerInternal, &api.manager, &manager);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = QStringLiteral(
                         "QueryService(IVirtualDesktopManagerInternal %1) failed: %2 — "
                         "Windows build %3.%4 is mapped to the table entry starting at build %5 (%6)")
                         .arg(guidText(api.manager),
                              hresultText(hr),
                              QString::number(version.build),
                              QString::number(version.revision),
                              QString::number(api.build),
                              QString::fromLatin1(layoutName(api.layout)));
        }
        return false;
    }
    if (manager == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("QueryService returned a null interface pointer");
        }
        return false;
    }
    m_manager = ComPtr(manager);
    m_api = &api;

    // 移动窗口用的视图集合。拿不到时不让整个 Session 失败：切换桌面仍然可用，
    // 只是 `window_rule` 的 `desktop` 会报一条 warning。
    void *views = nullptr;
    hr = vtableOf<IServiceProviderVtbl>(provider)->queryService(
        provider, &kIidApplicationViewCollection, &kIidApplicationViewCollection, &views);
    if (FAILED(hr) || views == nullptr) {
        logWarn(QStringLiteral("IApplicationViewCollection is unavailable (%1); window rules "
                               "cannot move windows between desktops")
                    .arg(hresultText(hr)));
    } else {
        m_viewCollection = ComPtr(views);
    }

    // 已公开的那个管理器：只用来读窗口当前在哪个桌面（验证与诊断）。
    void *publicManager = nullptr;
    hr = CoCreateInstance(
        kClsidVirtualDesktopManager, nullptr, CLSCTX_ALL, kIidVirtualDesktopManager, &publicManager);
    if (SUCCEEDED(hr) && publicManager != nullptr) {
        m_publicManager = ComPtr(publicManager);
    } else {
        logWarn(QStringLiteral("IVirtualDesktopManager is unavailable (%1); cannot verify "
                               "window moves between desktops")
                    .arg(hresultText(hr)));
    }
    return true;
}

bool Session::getDesktops(ComPtr *out, QString *error) const
{
    void *object = m_manager.get();
    void *result = nullptr;
    // 布局来自版本表；`HMONITOR` 传 `NULL`（等价于主显示器）。
    HRESULT hr = E_FAIL;
    switch (m_api->layout) {
    case Layout::Plain:
        hr = vtableOf<ManagerPlainVtbl>(object)->getDesktops(object, &result);
        break;
    case Layout::Monitor:
        hr = vtableOf<ManagerMonitorVtbl>(object)->getDesktops(object, nullptr, &result);
        break;
    case Layout::MonitorShifted:
        hr = vtableOf<ManagerMonitorShiftedVtbl>(object)->getDesktops(object, nullptr, &result);
        break;
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "IVirtualDesktopManagerInternal::GetDesktops");
        }
        return false;
    }
    if (result == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("GetDesktops returned a null interface pointer");
        }
        return false;
    }
    *out = ComPtr(result);
    return true;
}

bool Session::currentDesktop(ComPtr *out, QString *error) const
{
    void *object = m_manager.get();
    void *result = nullptr;
    HRESULT hr = E_FAIL;
    switch (m_api->layout) {
    case Layout::Plain:
        hr = vtableOf<ManagerPlainVtbl>(object)->getCurrentDesktop(object, &result);
        break;
    case Layout::Monitor:
        hr = vtableOf<ManagerMonitorVtbl>(object)->getCurrentDesktop(object, nullptr, &result);
        break;
    case Layout::MonitorShifted:
        hr = vtableOf<ManagerMonitorShiftedVtbl>(object)->getCurrentDesktop(object, nullptr, &result);
        break;
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "IVirtualDesktopManagerInternal::GetCurrentDesktop");
        }
        return false;
    }
    if (result == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("GetCurrentDesktop returned a null interface pointer");
        }
        return false;
    }
    *out = ComPtr(result);
    return true;
}

bool Session::switchDesktop(const ComPtr &target, QString *error) const
{
    void *object = m_manager.get();
    void *desktop = target.get();
    HRESULT hr = E_FAIL;
    switch (m_api->layout) {
    case Layout::Plain:
        hr = vtableOf<ManagerPlainVtbl>(object)->switchDesktop(object, desktop);
        break;
    case Layout::Monitor:
        hr = vtableOf<ManagerMonitorVtbl>(object)->switchDesktop(object, nullptr, desktop);
        break;
    case Layout::MonitorShifted:
        hr = vtableOf<ManagerMonitorShiftedVtbl>(object)->switchDesktop(object, nullptr, desktop);
        break;
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "IVirtualDesktopManagerInternal::SwitchDesktop");
        }
        return false;
    }
    return true;
}

bool Session::enumerateDesktops(std::vector<ComPtr> *out, QString *error) const
{
    ComPtr array;
    if (!getDesktops(&array, error)) {
        return false;
    }
    const IObjectArrayVtbl *vtbl = vtableOf<IObjectArrayVtbl>(array.get());

    UINT count = 0;
    HRESULT hr = vtbl->getCount(array.get(), &count);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "IObjectArray::GetCount");
        }
        return false;
    }

    out->reserve(count);
    for (UINT index = 0; index < count; ++index) {
        void *desktop = nullptr;
        hr = vtbl->getAt(array.get(), index, &m_api->desktop, &desktop);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = QStringLiteral("IObjectArray::GetAt(%1) failed: %2")
                             .arg(index)
                             .arg(hresultText(hr));
            }
            return false;
        }
        if (desktop == nullptr) {
            if (error != nullptr) {
                *error = QStringLiteral("IObjectArray::GetAt(%1) returned null").arg(index);
            }
            return false;
        }
        out->push_back(ComPtr(desktop));
    }
    return true;
}

/// `desktop` 在枚举结果里的下标。
std::optional<std::size_t> position(const std::vector<ComPtr> &desktops, const ComPtr &desktop)
{
    for (std::size_t index = 0; index < desktops.size(); ++index) {
        if (desktops[index].same(desktop)) {
            return index;
        }
    }
    return std::nullopt;
}

bool Session::snapshot(Snapshot *out, QString *error) const
{
    std::vector<ComPtr> desktops;
    if (!enumerateDesktops(&desktops, error)) {
        return false;
    }
    ComPtr current;
    if (!currentDesktop(&current, error)) {
        return false;
    }
    const WindowsVersion version = windowsVersion();
    out->count = static_cast<std::uint32_t>(desktops.size());
    const std::optional<std::size_t> index = position(desktops, current);
    out->current = index.has_value() ? static_cast<std::uint32_t>(*index) + 1 : 0;
    out->osBuild = version.build;
    out->osRevision = version.revision;
    out->apiBuild = m_api->build;
    out->layout = QString::fromLatin1(layoutName(m_api->layout));
    out->managerIid = guidText(m_api->manager);
    return true;
}

bool Session::goTo(std::uint32_t index, QString *detail, QString *error) const
{
    std::vector<ComPtr> desktops;
    if (!enumerateDesktops(&desktops, error)) {
        return false;
    }
    const std::size_t count = desktops.size();
    if (index == 0 || index > count) {
        if (error != nullptr) {
            *error = QStringLiteral("desktop %1 does not exist (%2 desktop(s) on this machine)")
                         .arg(index)
                         .arg(count);
        }
        return false;
    }
    ComPtr current;
    if (!currentDesktop(&current, error)) {
        return false;
    }
    const std::optional<std::size_t> here = position(desktops, current);
    if (here.has_value() && *here == static_cast<std::size_t>(index) - 1) {
        if (detail != nullptr) {
            *detail = QStringLiteral("already on desktop %1/%2").arg(index).arg(count);
        }
        return true;
    }
    if (!switchDesktop(desktops[static_cast<std::size_t>(index) - 1], error)) {
        return false;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("desktop %1/%2").arg(index).arg(count);
    }
    return true;
}

std::optional<std::size_t> Session::desktopIndexOfWindow(HWND hwnd,
                                                        const std::vector<ComPtr> &desktops,
                                                        QString *error) const
{
    const std::optional<QString> wanted = windowDesktopId(hwnd);
    if (!wanted.has_value()) {
        if (error != nullptr) {
            *error = QStringLiteral("could not read the desktop id of the window");
        }
        return std::nullopt;
    }

    // 逐个问桌面的 GUID，有且只有一个能对上才算数。这同时是一道自检：万一
    // `IVirtualDesktop::GetID` 的 vtable 下标在这台机器上不是 4（版本表选错、
    // 或者系统换了布局），拿到的就是一堆对不上的 GUID，于是我们只是报错。
    std::optional<std::size_t> found;
    for (std::size_t index = 0; index < desktops.size(); ++index) {
        GUID id{};
        const HRESULT hr = vtableOf<IVirtualDesktopVtbl>(desktops[index].get())
                               ->getId(desktops[index].get(), &id);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = hresultMessage(hr, "IVirtualDesktop::GetID");
            }
            return std::nullopt;
        }
        if (guidText(id) != *wanted) {
            continue;
        }
        if (found.has_value()) {
            if (error != nullptr) {
                *error = QStringLiteral("two virtual desktops report the same id (%1); the shell's "
                                        "IVirtualDesktop layout does not match this build")
                             .arg(*wanted);
            }
            return std::nullopt;
        }
        found = index;
    }
    if (!found.has_value() && error != nullptr) {
        *error = QStringLiteral("no virtual desktop has the id of the window (%1); the shell's "
                                "IVirtualDesktop layout does not match this build")
                     .arg(*wanted);
    }
    return found;
}

bool Session::goToWindowDesktop(HWND hwnd, QString *detail, QString *error) const
{
    std::vector<ComPtr> desktops;
    if (!enumerateDesktops(&desktops, error)) {
        return false;
    }
    const std::optional<std::size_t> index = desktopIndexOfWindow(hwnd, desktops, error);
    if (!index.has_value()) {
        return false;
    }
    ComPtr current;
    if (!currentDesktop(&current, error)) {
        return false;
    }
    if (desktops[*index].same(current)) {
        if (detail != nullptr) {
            *detail = QStringLiteral("already on desktop %1/%2").arg(*index + 1).arg(desktops.size());
        }
        return true;
    }
    if (!switchDesktop(desktops[*index], error)) {
        return false;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("desktop %1/%2").arg(*index + 1).arg(desktops.size());
    }
    return true;
}

bool Session::moveWindowToDesktop(HWND hwnd,
                                  std::uint32_t index,
                                  QString *detail,
                                  QString *error) const
{
    if (m_viewCollection.get() == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("the shell's IApplicationViewCollection is not available; "
                                    "cannot move windows between desktops");
        }
        return false;
    }
    std::vector<ComPtr> desktops;
    if (!enumerateDesktops(&desktops, error)) {
        return false;
    }
    if (index == 0 || index > desktops.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("desktop %1 does not exist (%2 desktop(s) on this machine)")
                         .arg(index)
                         .arg(desktops.size());
        }
        return false;
    }

    void *view = nullptr;
    HRESULT hr = vtableOf<IApplicationViewCollectionVtbl>(m_viewCollection.get())
                     ->getViewForHwnd(m_viewCollection.get(), hwnd, &view);
    if (FAILED(hr) || view == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("IApplicationViewCollection::GetViewForHwnd failed: %1")
                         .arg(hresultText(hr));
        }
        return false;
    }
    ComPtr viewGuard(view);

    void *object = m_manager.get();
    void *desktop = desktops[static_cast<std::size_t>(index) - 1].get();
    hr = E_FAIL;
    switch (m_api->layout) {
    case Layout::Plain:
        hr = vtableOf<ManagerPlainVtbl>(object)->moveViewToDesktop(object, view, desktop);
        break;
    case Layout::Monitor:
        hr = vtableOf<ManagerMonitorVtbl>(object)->moveViewToDesktop(object, view, desktop);
        break;
    case Layout::MonitorShifted:
        hr = vtableOf<ManagerMonitorShiftedVtbl>(object)->moveViewToDesktop(object, view, desktop);
        break;
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresultMessage(hr, "IVirtualDesktopManagerInternal::MoveViewToDesktop");
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("desktop %1/%2").arg(index).arg(desktops.size());
    }
    return true;
}

std::optional<QString> Session::windowDesktopId(HWND hwnd) const
{
    if (m_publicManager.get() == nullptr) {
        return std::nullopt;
    }
    GUID id{};
    const HRESULT hr = vtableOf<IVirtualDesktopManagerVtbl>(m_publicManager.get())
                           ->getWindowDesktopId(m_publicManager.get(), hwnd, &id);
    if (FAILED(hr)) {
        return std::nullopt;
    }
    return guidText(id);
}

// --- 系统版本 --------------------------------------------------------------

struct RtlOsVersionInfoW
{
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platformId;
    WCHAR csdVersion[128];
};

using RtlGetVersionFn = LONG(WINAPI *)(RtlOsVersionInfoW *);

/// `RtlGetVersion`（ntdll 里，`GetVersionEx` 会被应用清单骗）。ntdll 每个进程都加载。
RtlGetVersionFn rtlGetVersion()
{
    static RtlGetVersionFn resolved = []() -> RtlGetVersionFn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr) {
            return nullptr;
        }
        const FARPROC proc = GetProcAddress(ntdll, "RtlGetVersion");
        if (proc == nullptr) {
            return nullptr;
        }
        RtlGetVersionFn fn = nullptr;
        static_assert(sizeof(fn) == sizeof(proc), "FARPROC is a function pointer");
        std::memcpy(&fn, &proc, sizeof(fn));
        return fn;
    }();
    return resolved;
}

/// `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion` 的 `UBR`。
std::optional<std::uint32_t> updateRevision()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,
                                        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                        L"UBR",
                                        RRF_RT_REG_DWORD,
                                        nullptr,
                                        &value,
                                        &size);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

/// 在一条一次性的 STA 线程上跑 `work` 并等它结束。
///
/// 虚拟桌面接口要求 STA，而工作线程的单元模型取决于哪个后端先初始化了 COM
/// （音频要 MTA）。与其让两者互相影响，不如给这条调用路径一条干净、
/// 用完即弃的线程：切换桌面本来就不是热路径。
template <typename Work>
void inSta(Work work)
{
    std::thread worker(std::move(work));
    worker.join();
}

} // namespace

// --- 公开接口 --------------------------------------------------------------

const std::vector<ApiEntry> &versionTable()
{
    static const std::vector<ApiEntry> table = {
        ApiEntry{20348,
                 0,
                 {0xF31574D6, 0xB682, 0x4CDC, {0xBD, 0x56, 0x18, 0x27, 0x86, 0x0A, 0xBE, 0xC6}},
                 {0xFF72FFDD, 0xBE7E, 0x43FC, {0x9C, 0x03, 0xAD, 0x81, 0x68, 0x1E, 0x88, 0xE4}},
                 Layout::Plain},
        ApiEntry{22000,
                 0,
                 {0x094AFE11, 0x44F2, 0x4BA0, {0x97, 0x6F, 0x29, 0xA9, 0x7E, 0x26, 0x3E, 0xE0}},
                 {0x62FDF88B, 0x11CA, 0x4AFB, {0x8B, 0xD8, 0x22, 0x96, 0xDF, 0xAE, 0x49, 0xE2}},
                 Layout::Monitor},
        ApiEntry{22483,
                 0,
                 {0xB2F925B9, 0x5A0F, 0x4D2E, {0x9F, 0x4D, 0x2B, 0x15, 0x07, 0x59, 0x3C, 0x10}},
                 {0x536D3495, 0xB208, 0x4CC9, {0xAE, 0x26, 0xDE, 0x81, 0x11, 0x27, 0x5B, 0xF8}},
                 Layout::Monitor},
        ApiEntry{22621,
                 2215,
                 {0xB2F925B9, 0x5A0F, 0x4D2E, {0x9F, 0x4D, 0x2B, 0x15, 0x07, 0x59, 0x3C, 0x10}},
                 {0x536D3495, 0xB208, 0x4CC9, {0xAE, 0x26, 0xDE, 0x81, 0x11, 0x27, 0x5B, 0xF8}},
                 Layout::MonitorShifted},
        ApiEntry{22631,
                 3085,
                 {0xA3175F2D, 0x239C, 0x4BD2, {0x8A, 0xA0, 0xEE, 0xBA, 0x8B, 0x0B, 0x13, 0x8E}},
                 {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}},
                 Layout::Plain},
        // Windows 11 24H2 / 25H2。
        ApiEntry{26100,
                 0,
                 {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}},
                 {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}},
                 Layout::Plain},
        // 兜底：比表里所有条目都新的系统沿用 24H2 的接口。
        ApiEntry{99999,
                 0,
                 {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}},
                 {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}},
                 Layout::Plain},
    };
    return table;
}

const char *layoutName(Layout layout)
{
    switch (layout) {
    case Layout::Plain:
        return "plain";
    case Layout::Monitor:
        return "hmonitor";
    case Layout::MonitorShifted:
        return "hmonitor-shifted";
    }
    return "plain";
}

const ApiEntry &apiFor(std::uint32_t build, std::uint32_t revision)
{
    const std::vector<ApiEntry> &table = versionTable();
    const ApiEntry *best = &table.front();
    for (const ApiEntry &entry : table) {
        // 表按升序排列，所以“生效版本不高于系统版本”的最后一条就是答案。
        if (entry.build < build || (entry.build == build && entry.revision <= revision)) {
            best = &entry;
        }
    }
    return *best;
}

QString guidText(const GUID &guid)
{
    return QString::asprintf("{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                             static_cast<unsigned>(guid.Data1),
                             static_cast<unsigned>(guid.Data2),
                             static_cast<unsigned>(guid.Data3),
                             static_cast<unsigned>(guid.Data4[0]),
                             static_cast<unsigned>(guid.Data4[1]),
                             static_cast<unsigned>(guid.Data4[2]),
                             static_cast<unsigned>(guid.Data4[3]),
                             static_cast<unsigned>(guid.Data4[4]),
                             static_cast<unsigned>(guid.Data4[5]),
                             static_cast<unsigned>(guid.Data4[6]),
                             static_cast<unsigned>(guid.Data4[7]));
}

WindowsVersion windowsVersion()
{
    WindowsVersion version;
    if (const RtlGetVersionFn fn = rtlGetVersion(); fn != nullptr) {
        RtlOsVersionInfoW info{};
        info.size = sizeof(info);
        if (fn(&info) >= 0) {
            version.build = info.build;
        }
    }
    if (const std::optional<std::uint32_t> revision = updateRevision(); revision.has_value()) {
        version.revision = *revision;
    } else {
        // 宁可退化成 0，也不要因此不干活；但必须说出来，否则将来只会看到
        // 一条莫名其妙的 E_NOINTERFACE。
        logWarn(QStringLiteral(
            "could not read the Windows update revision (UBR) from the registry; "
            "the virtual desktop interface table may pick the wrong entry"));
    }
    return version;
}

bool probe(Snapshot *out, QString *error)
{
    Snapshot result;
    QString localError;
    bool ok = false;
    inSta([&]() {
        Apartment apartment;
        if (!apartment.enter(&localError)) {
            return;
        }
        const WindowsVersion version = windowsVersion();
        const ApiEntry &api = apiFor(version.build, version.revision);
        Session session;
        if (!session.open(api, version, &localError)) {
            return;
        }
        ok = session.snapshot(&result, &localError);
    });
    if (!ok) {
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    *out = result;
    return true;
}

bool switchTo(std::uint32_t index, QString *detail, QString *error)
{
    if (index == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("desktop numbers start at 1");
        }
        return false;
    }
    QString localDetail;
    QString localError;
    bool ok = false;
    inSta([&]() {
        Apartment apartment;
        if (!apartment.enter(&localError)) {
            return;
        }
        const WindowsVersion version = windowsVersion();
        const ApiEntry &api = apiFor(version.build, version.revision);
        Session session;
        if (!session.open(api, version, &localError)) {
            return;
        }
        ok = session.goTo(index, &localDetail, &localError);
    });
    if (!ok) {
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = localDetail;
    }
    return true;
}

bool moveWindowToDesktop(HWND hwnd,
                         std::uint32_t index,
                         QString *detail,
                         QString *error,
                         bool *changed)
{
    if (changed != nullptr) {
        *changed = false;
    }
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("the window is gone");
        }
        return false;
    }
    if (index == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("desktop numbers start at 1");
        }
        return false;
    }
    QString localDetail;
    QString localError;
    bool ok = false;
    bool localChanged = false;
    inSta([&]() {
        Apartment apartment;
        if (!apartment.enter(&localError)) {
            return;
        }
        const WindowsVersion version = windowsVersion();
        const ApiEntry &api = apiFor(version.build, version.revision);
        Session session;
        if (!session.open(api, version, &localError)) {
            return;
        }
        const std::optional<QString> before = session.windowDesktopId(hwnd);
        ok = session.moveWindowToDesktop(hwnd, index, &localDetail, &localError);
        if (!ok) {
            return;
        }
        // 未公开的 `MoveViewToDesktop` 返回 S_OK 也可能什么都没发生，而且它是
        // **异步生效**的（shell 在自己的线程上搬），所以等一下再确认：移动前后的
        // 桌面 GUID 都拿得到且不一样，才算是真搬动了。
        std::optional<QString> after = before;
        if (before.has_value()) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                after = session.windowDesktopId(hwnd);
                if (after.has_value() && *after != *before) {
                    localChanged = true;
                    break;
                }
                Sleep(25);
            }
        }
        logDebug(QStringLiteral("window desktop id %1 -> %2")
                     .arg(before.value_or(QStringLiteral("?")),
                          after.value_or(QStringLiteral("?"))));
    });
    if (!ok) {
        if (error != nullptr) {
            *error = localError.isEmpty()
                         ? QStringLiteral("could not move the window to desktop %1").arg(index)
                         : localError;
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = localDetail;
    }
    if (changed != nullptr) {
        *changed = localChanged;
    }
    return true;
}

bool switchToWindowDesktop(HWND hwnd, QString *detail, QString *error)
{
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("the window is gone");
        }
        return false;
    }
    QString localDetail;
    QString localError;
    bool ok = false;
    inSta([&]() {
        Apartment apartment;
        if (!apartment.enter(&localError)) {
            return;
        }
        const WindowsVersion version = windowsVersion();
        const ApiEntry &api = apiFor(version.build, version.revision);
        Session session;
        if (!session.open(api, version, &localError)) {
            return;
        }
        ok = session.goToWindowDesktop(hwnd, &localDetail, &localError);
    });
    if (!ok) {
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = localDetail;
    }
    return true;
}

std::optional<bool> isWindowOnCurrentDesktop(HWND hwnd, QString *error)
{
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("the window is gone");
        }
        return std::nullopt;
    }
    std::optional<bool> result;
    QString localError;
    inSta([&]() {
        Apartment apartment;
        if (!apartment.enter(&localError)) {
            return;
        }
        void *manager = nullptr;
        const HRESULT hr = CoCreateInstance(
            kClsidVirtualDesktopManager, nullptr, CLSCTX_ALL, kIidVirtualDesktopManager, &manager);
        if (FAILED(hr) || manager == nullptr) {
            localError = hresultMessage(hr, "CoCreateInstance(CLSID_VirtualDesktopManager)");
            return;
        }
        ComPtr guard(manager);
        BOOL onCurrent = FALSE;
        const HRESULT query = vtableOf<IVirtualDesktopManagerVtbl>(manager)
                                  ->isWindowOnCurrentVirtualDesktop(manager, hwnd, &onCurrent);
        if (FAILED(query)) {
            localError = hresultMessage(
                query, "IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop");
            return;
        }
        result = onCurrent != FALSE;
    });
    if (!result.has_value() && error != nullptr) {
        *error = localError;
    }
    return result;
}

std::optional<QString> windowDesktopId(HWND hwnd, QString *error)
{
    if (hwnd == nullptr || IsWindow(hwnd) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("the window is gone");
        }
        return std::nullopt;
    }
    std::optional<QString> result;
    QString localError;
    inSta([&]() {
        Apartment apartment;
        if (!apartment.enter(&localError)) {
            return;
        }
        const WindowsVersion version = windowsVersion();
        const ApiEntry &api = apiFor(version.build, version.revision);
        Session session;
        if (!session.open(api, version, &localError)) {
            return;
        }
        result = session.windowDesktopId(hwnd);
        if (!result.has_value()) {
            localError = QStringLiteral("could not read the desktop id of the window");
        }
    });
    if (!result.has_value() && error != nullptr) {
        *error = localError;
    }
    return result;
}

} // namespace flowkeyd::platform::win::desktop
