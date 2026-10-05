// 开始菜单 / 控制面板扫描（程序启动器 `apps` 动作的两份数据源）、shell 图标提取与启动。
//
// **第一份数据源是 `shell:AppsFolder`**，不是那两个 `…\Start Menu\Programs` 目录
// （2026-10 改的，见 AGENTS.md 第 2 节第 27 条）：那就是开始菜单「所有应用」
// 列的那一份 —— 经典程序的快捷方式、**商店/UWP 应用**（它们根本没有 `.lnk`）、
// 系统工具（`.msc`）全在里面，名字也是 shell 给的显示名（跟系统语言走）。
//
// **第二份是控制面板**（`listControlPanelItems()`，2026-10 加，见 AGENTS.md 第 2 节
// 第 31 条）：开始菜单里只有「设置」与「控制面板」两个总入口，「网络连接」
// 「电源选项」「程序和功能」「设备管理器」「声音」这些单项都在控制面板命名空间里。
// 这一层只做平台上那几件事：
//   * 枚举 `shell:AppsFolder`，每个条目读 显示名 / AppUserModelID /
//     目标解析名 / 参数（`IShellItem2::GetString` + 几个 PKEY）；
//   * 「算不算程序」「算不算卸载程序」的判据在 `core/app_list.h`（纯逻辑、
//     可单测）—— 这里只是把每一条拿去问一遍；
//   * `IShellItemImageFactory::GetImage()` 取图标（同一层下面也有）
//     与 `launchApp()` 启动。
//
// **启动用的是 shell 解析名 `shell:AppsFolder\<AppUserModelID>`**
// （`AppEntry::launch`）：参数、工作目录、`runas` 标记、商店应用的激活全部由
// shell 照原样处理，与点开始菜单完全一致。图标与右键菜单也用同一个字符串。
//
// **COM 单元模型**：`IShellItem` 与 shell 的图像工厂都按 STA 使用，而动作线程
// 可能是 MTA（音频后端要 MTA），所以扫描自己开一条一次性的 STA 线程（与
// `platform/win/desktop.cpp` 同一套做法）。图标提取**不**在这里开线程：调用方
// （`app/app_icons.cpp`）自己拥有一条常驻的 STA 线程；`launchApp()` 也自己声明
// 一次单元模型（它从动作线程上被调）。
//
// 图标数据按**直通 alpha**（`QImage::Format_ARGB32`，不是预乘）返回，这一点是
// 真机实测的：`GetImage` 回来的 32 位 DIB 里有 RGB > alpha 的像素
// （83 个图标里 32973 个半透明像素中有 15155 个这样），预乘的数据不可能出现。
// 商店应用的图标走同一条路（`SHCreateItemFromParsingName("shell:AppsFolder\…")`
// 同样能拿到），真机上 159 个条目 159 个都有图标。
#pragma once

#include "core/app_list.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace flowkeyd::platform::win::apps {

/// 一次开始菜单扫描的结果。
///
/// `listStartMenuApps()` 与 `listControlPanelItems()` 共用它（两者的错误处理与
/// 计数器形状一模一样，只是来源不同）。
struct StartMenuScan
{
    /// 扫到并**通过两道过滤**的条目（原始顺序，没有去重、没有排序 ——
    /// 去重与排序是 `core::prepareAppEntries()` 的事）。
    std::vector<core::AppEntry> entries;
    /// `shell:AppsFolder` 里的条目总数（过滤之前），用来记日志。
    int candidates = 0;
    /// 其中「不是程序」的（文档 / 帮助 / 网址 / 坏掉的条目）。
    int hiddenNotProgram = 0;
    /// 其中「是程序但是卸载程序」的。
    int hiddenUninstaller = 0;
    /// 只有硬错误（COM 初始化失败之类）；开始菜单为空**不是**错误，
    /// 只是空列表。
    QString error;
};

/// 枚举 `shell:AppsFolder`。可以在这条线程上任意调用（内部自己开一次性 STA 线程）。
StartMenuScan listStartMenuApps();

/// 枚举**控制面板**里的条目（启动器目录的第二份来源）。
///
/// 开始菜单（`shell:AppsFolder`）里有「设置」与「控制面板」两个**总入口**，却
/// 没有「网络连接」「电源选项」「程序和功能」「设备管理器」「声音」「鼠标」这些
/// 单独的项 —— 它们在控制面板命名空间里。真机（Win11 26200）上「所有控制面板项」
/// 是 36 条；另外补上几个不在那里的（目前只有「网络连接」，它是个 delegate
/// folder）。
///
/// 走的还是 shell 那一套：显示名 = `SIGDN_NORMALDISPLAY`，启动名 = 那个条目的
/// **桌面绝对解析名**（`SIGDN_DESKTOPABSOLUTEPARSING`，形如
/// `::{26EE0668-…}\0\::{025A5937-…}`）—— 它是由 shell 自己给的 PIDL 往返得来
/// 的，所以 `SHParseDisplayName` 能再解回去（`launchApp()` 的第二条路就是它），
/// 图标也能用同一个字符串取（`shellIcon()`）。名字与图标因此与系统语言一致。
/// 解析名解不回去的那几个（真机上只有「字体」，它的解析名里带的是本地化显示名）
/// 退回 `SIGDN_FILESYSPATH`（`C:\Windows\Fonts`）；连它也没有就丢掉这条
/// （宁可少一条也不给一个按下去没反应的格子）。
///
/// `hiddenUninstaller` 恒为 0：这些条目不做「卸载程序」那道过滤（控制面板本来
/// 就不会把卸载程序摆在这里）。
StartMenuScan listControlPanelItems();

/// 启动一个 `AppEntry::launch`（`shell:AppsFolder\<AUMID>`）。
///
/// 两条路，第一条不行就走第二条（两条在真机上都验过）：
///   1. `ShellExecuteExW` 直接把解析名当 `lpFile`（`SEE_MASK_FLAG_NO_UI`：
///      失败也不要弹系统对话框，我们只把它写进日志）；
///   2. `SHParseDisplayName` + `SEE_MASK_IDLIST`（微软给商店应用写的那条路）。
/// 可以在这条线程上任意调用（内部自己声明一次 COM 单元模型）。
bool launchApp(const QString &launchName, QString *error);

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

/// 取 `path`（shell 解析名 / exe / 文件夹……）在 shell 里的图标，边长 `size` 像素。
///
/// **必须在已经初始化成 STA 的线程上调用**（用 `StaThread` 保证）；不满足时
/// `error` 里是 COM 那条错。
/// 代价在真机上是每个图标约 3 ms（123 个条目连取一遍约 400 ms），所以调用
/// 方必须缓存 —— 每一次弹出都全量取一遍会让卡片等上小半秒。
ShellIcon shellIcon(const QString &path, int size);

/// 把当前线程初始化成 STA，析构时再反初始化。
///
/// `shellIcon()` 与 shell 的文件夹对象都要求 STA，而动作线程可能已经被音频后端
/// 初始化成 MTA（AGENTS.md 第 7 节第 13 条），所以「要 COM 的那条线程」自己先
/// 声明一次。这个线程已经是别的单元模型时（`RPC_E_CHANGED_MODE`）`ok()` 仍然
/// 是 true，但**不会**去反初始化（那会把别人的单元模型拆掉）。
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
