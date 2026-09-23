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

/// 把窗口移到第 `index` 个虚拟桌面（从 1 开始，Task View 顺序）。
///
/// 走未公开的 `IVirtualDesktopManagerInternal::MoveViewToDesktop`：公开的
/// `IVirtualDesktopManager::MoveWindowToDesktop` 拒绝移动**别的进程**的窗口，
/// 而 `window_rule` 要摆的正是别的进程。窗口已经在该桌面上时是一个空操作。
/// 成功时 `detail` 是 `"desktop 2/4"`。
///
/// `changed`（可空）回答「这次调用**真的**把它搬到另一个桌面了吗」：窗口本来就在
/// 目标桌面上时是 `false`（`window_rule` 靠它决定要不要让视图跟着窗口走）。
/// 只有拿得到移动前后的桌面 GUID 才能回答，拿不到时是 `false`。
bool moveWindowToDesktop(HWND hwnd,
                         std::uint32_t index,
                         QString *detail,
                         QString *error,
                         bool *changed = nullptr);

/// 把窗口移到相邻的虚拟桌面（`delta`：`-1` 上一张，`+1` 下一张）。
///
/// 与 `moveWindowToDesktop` 不同，这里**首尾相接**：窗口在第一张桌面上再往
/// 前会到最右那一张，在最后一张再往后会回到第一张（用户 2026-09 拍板）。
/// 视图不会跟着窗口走 —— 移动的是窗口，不是当前桌面（与 Windows 自己的
/// `Win+Ctrl+Shift+←/→` 一致，显示器上的几何完全不变）。
///
/// 成功时 `detail` 是 `"desktop 2/4"`；只有一张桌面、或窗口不属于任何虚拟
/// 桌面时失败（`error` 里是英文原因）。
bool moveWindowToAdjacentDesktop(HWND hwnd, int delta, QString *detail, QString *error);

/// 把视图切到 `hwnd` 所在的那个虚拟桌面（它已经在当前桌面时是空操作）。
///
/// 为什么不能只靠 `SetForegroundWindow`：窗口被 `MoveViewToDesktop` 搬到别的桌面
/// 之后，**shell 仍然把它当作前台窗口**（本机 24H2 实测），这时
/// `SetForegroundWindow` 会老老实实返回 TRUE 却什么都不做 —— 视图永远留在原桌面，
/// 用户按快捷键也看不到那个窗口。所以这里显式 `SwitchDesktop`。
///
/// 窗口与内部桌面对象的对应关系靠 `IVirtualDesktop::GetID`（vtable 下标 4）取得，
/// 并与**已公开**的 `IVirtualDesktopManager::GetWindowDesktopId` 逐个比对：对不上
/// （说明这套 vtable 布局与版本表不符）时只报错、不去切一张可能是错误的桌面。
bool switchToWindowDesktop(HWND hwnd, QString *detail, QString *error);

/// 窗口是不是在当前（前台）虚拟桌面上。
///
/// 用**已公开**的 `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop`
/// （`CLSID_VirtualDesktopManager`），与版本表无关，因此最适合拿来做验证
/// （`tst_interactive` 就是用它确认 `moveWindowToDesktop` 真的生效了）。
/// 失败时返回 `std::nullopt`（`error` 里是英文原因）。
///
/// **“窗口不属于任何虚拟桌面”也算 `std::nullopt`**（不是“在别的桌面上”）：打包应用的
/// 宿主窗口、刚创建还没被 shell 登记的窗口都会这样，返回 FALSE 且 GUID 全零；
/// 调用方应当按“不知道”处理，不要把她当成“跑到别的桌面去了”。
std::optional<bool> isWindowOnCurrentDesktop(HWND hwnd, QString *error);

/// 窗口所在虚拟桌面的 GUID（`{xxxxxxxx-...}`）。
///
/// 同样用已公开的 `IVirtualDesktopManager::GetWindowDesktopId`：`window_rule`
/// 移动窗口前后对比它，就能知道未公开的 `MoveViewToDesktop` 到底有没有生效。
std::optional<QString> windowDesktopId(HWND hwnd, QString *error);

/// 窗口是不是被钉在**所有**虚拟桌面上（Task View 里的“在所有桌面显示”）。
///
/// 走未公开的 `IVirtualDesktopPinnedApps::IsViewPinned`（`{4CE81583-...}`）：
/// 这个 IID 自 Windows 10 起没有变过，所以它不进版本表。拿不到那个接口时返回
/// `std::nullopt`（`error` 里是英文原因）。
std::optional<bool> isWindowPinned(HWND hwnd, QString *error);

/// 把窗口钉在所有虚拟桌面上（`pinned = false` 取消钉住）。
///
/// 成功时 `detail` 是 `"all desktops"` / `"this desktop only"`；
/// `changed`（可空）报告状态是不是真的变了（已经是目标状态时为 `false`）。
/// 与 `moveWindowToDesktop` 同一个风险等级：vtable 布局若与系统不匹配只会
/// 返回错误码，不会随机破坏内存。
bool setWindowPinned(HWND hwnd,
                     bool pinned,
                     QString *detail,
                     QString *error,
                     bool *changed = nullptr);

} // namespace flowkeyd::platform::win::desktop
