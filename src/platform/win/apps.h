// 开始菜单扫描（程序启动器 `apps` 动作的数据源）与 shell 图标提取。
//
// 「什么算一个程序」的判据在 `core/app_list.h`（纯逻辑、可单测）；这里只做三件
// 平台上的事：
//   * 找到「全局开始菜单」与「当前用户开始菜单」两个 `…\Start Menu\Programs`
//     目录，递归收集 `.lnk`；
//   * 用 `IShellLink` 把每个快捷方式解析出目标与参数（`GetPath` / `GetArguments`），
//     再按 `core::appTargetIsProgram()` 过滤；
//   * `IShellItemImageFactory::GetImage()` 取某个路径在当前 DPI 下合适尺寸的图标。
//
// **启动用的是快捷方式本身**（`ShellExecuteW("open", <lnk>)`，见 dispatcher）：
// 参数、工作目录、`runas` 标记都由 shell 照原样处理，与点开始菜单完全一致。
//
// **COM 单元模型**：`IShellLink` 与 shell 的 `IShellItemImageFactory` 都按 STA
// 使用，而动作线程可能是 MTA（音频后端要 MTA），所以扫描自己开一条一次性的
// STA 线程（与 `platform/win/desktop.cpp` 同一套做法）。图标提取**不**在这里
// 开线程：调用方（`app/app_icons.cpp`）自己拥有一条常驻的 STA 线程。
//
// 图标数据按**直通 alpha**（`QImage::Format_ARGB32`，不是预乘）返回，这一点是
// 真机实测的：`GetImage` 回来的 32 位 DIB 里有 RGB > alpha 的像素
// （83 个图标里 32973 个半透明像素中有 15155 个这样），预乘的数据不可能出现。
#pragma once

#include "core/app_list.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace flowkeyd::platform::win::apps {

/// 一次开始菜单扫描的结果。
struct StartMenuScan
{
    /// 扫到并**通过「是程序」过滤**的条目（原始顺序，没有去重、没有排序 ——
    /// 去重与排序是 `core::prepareAppEntries()` 的事）。
    std::vector<core::AppEntry> entries;
    /// 扫到的 `.lnk` 总数（过滤之前），用来记日志。
    int shortcuts = 0;
    /// 只有硬错误（COM 初始化失败之类）；没有开始菜单目录**不是**错误，
    /// 只是空列表。
    QString error;
};

/// 扫描两个开始菜单目录。可以在这条线程上任意调用（内部自己开一次性 STA 线程）。
StartMenuScan listStartMenuApps();

/// 从 shell 里取出来的一个图标位图。
///
/// 32 位 **BGRA 直通 alpha**、自顶向下（`pixels` 的第 0 字节是左上角的蓝）。
/// 这份布局与 `QImage::Format_ARGB32` 逐字节一致。
struct ShellIcon
{
    bool ok = false;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    QString error;
};

/// 取 `path`（快捷方式 / exe / 文件夹……）在 shell 里的图标，边长 `size` 像素。
///
/// **必须在已经初始化成 STA 的线程上调用**（用 `StaThread` 保证）；不满足时
/// `error` 里是 COM 那条错。
/// 代价在真机上是每个图标约 3 ms（123 个 `.lnk` 连取一遍约 400 ms），所以调用
/// 方必须缓存 —— 每一次弹出都全量取一遍会让卡片等上小半秒。
ShellIcon shellIcon(const QString &path, int size);

/// 把当前线程初始化成 STA，析构时再反初始化。
///
/// `shellIcon()` 与快捷方式解析都要求 STA，而动作线程可能已经被音频后端初始化
/// 成 MTA（AGENTS.md 第 7 节第 13 条），所以「要 COM 的那条线程」自己先声明一次。
/// 这个线程已经是别的单元模型时（`RPC_E_CHANGED_MODE`）`ok()` 仍然是 true，
/// 但**不会**去反初始化（那会把别人的单元模型拆掉）。
class StaThread
{
public:
    StaThread();
    ~StaThread();

    StaThread(const StaThread &) = delete;
    StaThread &operator=(const StaThread &) = delete;

    bool ok() const { return m_ok; }
    const QString &error() const { return m_error; }

private:
    bool m_ok = false;
    bool m_owned = false;
    QString m_error;
};

} // namespace flowkeyd::platform::win::apps
