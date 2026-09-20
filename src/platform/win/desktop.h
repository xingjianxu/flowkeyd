// 虚拟桌面切换（`desktop` 动作的后端）。
//
// Windows 没有公开“切到第 N 个虚拟桌面”的 API。公开的
// `IVirtualDesktopManager`（`{aa509086-...}`）只能查询某个窗口在哪个桌面、
// 以及把窗口移过去；真正切换桌面的是 shell 内部的
// `IVirtualDesktopManagerInternal`，要先 `CoCreateInstance(CLSID_ImmersiveShell)`
// 拿到 `IServiceProvider`，再用 `QueryService` 请求它。
//
// 这些接口没有任何公开的 ABI 承诺：IID 与 vtable 布局会随 Windows 版本
// （甚至补丁级别的修订号）变化。下面的 `versionTable()` 按 `build.revision`
// 索引，事实与 AutoHotkey 的 VD.ahk、MScholtes/VirtualDesktop 一致；
// 每个条目的 `build`/`revision` 表示*从哪个版本开始*换成了这套接口，
// 因此选表规则是“最后一个生效版本不高于当前系统的条目”（`apiFor`）。
//
// 每一次调用都检查 `HRESULT`：vtable 布局若与系统不匹配会返回错误码，
// 而不是随机破坏内存（与音频后端同一个约定）。
//
// **单元模型**：这些 shell 接口是给交互式桌面程序用的，要 **STA**；而音频
// 后端要 MTA。所以每次调用都发生在一条一次性的 STA 线程上，绝不让工作线程的
// 单元模型取决于哪个后端先初始化（见 AGENTS.md 第 7 节第 13 条）。
//
// **表的内容照抄 `../oskeyd/src/win/desktop.rs`**，任何改动都要先改那边并实测。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace flowkeyd::platform::win::desktop {

/// vtable 布局：同一个接口在不同 Windows 版本上的形状。
enum class Layout {
    /// 没有 `HMONITOR` 参数：`GetCurrentDesktop` / `GetDesktops` / `SwitchDesktop`
    /// 在 vtable 索引 6 / 7 / 9 上。
    Plain,
    /// 上面三个方法各多一个 `HMONITOR` 参数（本机实测传 `NULL` 即可），
    /// 索引仍是 6 / 7 / 9（Windows 11 21H2、22H2 早期）。
    Monitor,
    /// 同样带 `HMONITOR`，但 `GetDesktops` / `SwitchDesktop` 后移到了 8 / 10
    /// （Windows 11 22H2 22621.2215 起）。
    MonitorShifted,
};

/// 诊断信息与单测里用的布局名。
const char *layoutName(Layout layout);

/// 一套内部接口的形貌。
struct ApiEntry
{
    /// 这套接口生效的最低 `build`。
    std::uint32_t build = 0;
    /// 同一 `build` 内生效的最低修订号（系统 build 的 `UBR`）。
    std::uint32_t revision = 0;
    /// `IVirtualDesktopManagerInternal` 的 IID。
    GUID manager{};
    /// `IVirtualDesktop` 的 IID：`IObjectArray::GetAt` 需要它。
    GUID desktop{};
    Layout layout = Layout::Plain;
};

/// 版本表，按 `build` 升序；最后一条是给“比这张表还新”的系统准备的兜底。
/// 比表里最旧一条还旧的系统（Windows 10、Server 2016/2019）沿用第一条。
const std::vector<ApiEntry> &versionTable();

/// 纯函数：挑出适用于 `build.revision` 的表项（“最后一个生效版本不高于当前系统”）。
const ApiEntry &apiFor(std::uint32_t build, std::uint32_t revision);

/// `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}`（小写十六进制），用于诊断与单测。
QString guidText(const GUID &guid);

/// 当前系统的 `build` 与修订号（`UBR`，读不到时为 0）。
struct WindowsVersion
{
    std::uint32_t build = 0;
    std::uint32_t revision = 0;
};

/// `RtlGetVersion`（build）+ 注册表的 `UBR`（revision）。
///
/// 修订号读不到时退化成 0：也许就会选到旧一条的 IID，但至少还能干活；
/// 同时打一条 warning，免得将来只看到一句莫名其妙的 `E_NOINTERFACE`。
WindowsVersion windowsVersion();

/// `--probe` / `--selftest` 用的只读探测结果。
struct Snapshot
{
    /// 桌面总数。
    std::uint32_t count = 0;
    /// 当前桌面序号（从 1 开始）；`0` 表示它不在枚举结果里（不该发生）。
    std::uint32_t current = 0;
    std::uint32_t osBuild = 0;
    std::uint32_t osRevision = 0;
    /// 版本表里生效的那条，即它代表的最低 build。
    std::uint32_t apiBuild = 0;
    /// 这套接口的 vtable 布局名。
    QString layout;
    /// `IVirtualDesktopManagerInternal` 的 IID。
    QString managerIid;
};

/// 读出桌面数量与当前桌面，不做任何切换。
bool probe(Snapshot *out, QString *error);

/// 切到第 `index` 个虚拟桌面（从 1 开始）。成功时 `detail` 是
/// `"desktop 2/4"` / `"already on desktop 1/4"` 之类的一行说明。
///
/// 切换本身由 shell 完成；这个函数只保证请求已经送达，不等动画结束。
bool switchTo(std::uint32_t index, QString *detail, QString *error);

} // namespace flowkeyd::platform::win::desktop
