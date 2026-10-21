// 窗口查找与窗口动作（`window` 动作的后端）。
//
// 目标要么是前台窗口（空查询），要么是第一个满足查询中每一项条件的可见顶层
// 窗口：标题子串和/或拥有该窗口的可执行文件名。
//
// `EnumWindows` 按 Z 序遍历窗口，因此最先找到的就是最近使用过的匹配窗口。
// 只有在没有已还原窗口匹配时才接受最小化的窗口，这样激活最小化窗口永远不会
// 盖过可见窗口。
//
// 「候选窗口算不算匹配」这条判断在 `core/window_match`（纯逻辑、可单测）；
// 这里只剩枚举、取进程名与调用 Win32。
#pragma once

#include "core/action.h"
#include "platform/win/ffi.h"

#include <QString>

#include <optional>
#include <vector>

namespace flowkeyd::platform::win::window {

/// 定位查询描述的窗口；没有任何匹配时返回 nullptr。
/// 查询意为“前台窗口”而没有前台窗口时同样返回 nullptr。
///
/// `tracked` 是工作空间状态机记着的窗口：它们可能正被我们藏起来
/// （`ShowWindow(SW_HIDE)` → `IsWindowVisible` 为假），但仍然是用户自己的程序
/// 窗口，所以 `window("activate")` / `windows()` 必须能找到它们（M9）。
/// 优先级：看得见的 > 被我们藏起来的 > 最小化的。
HWND find(const core::WindowQuery &query, const std::vector<HWND> &tracked = {});

/// 拥有这个窗口的可执行文件的**小写文件名**（`wezterm-gui.exe`）。
/// 拿不到（进程已退出、访问被拒）时返回 `std::nullopt`。
/// `window_rule` 的匹配用它。
std::optional<QString> processName(HWND hwnd);

/// 当前所有可见、没有属主的顶层窗口（按 `EnumWindows` 的 Z 序）。
/// flowkeyd 启动 / 显示器重新接入时，`window_rule` 就是遍历这批窗口。
std::vector<HWND> topLevelWindows();

/// 这个窗口是不是用户眼中的「主窗口」。
///
/// 判据：可见、没有属主、不是 `WS_EX_TOOLWINDOW`、有标题、尺寸非零，
/// 并且不是外壳自己的「Program Manager」。带 `WS_EX_APPWINDOW` 的有属主窗口
/// 也算 —— 那是应用显式要求它出现在任务栏 / Alt+Tab 里。很多程序会拿无标题 /
/// 工具窗口当消息汇或渲染宿主（拿 `process` 匹配时会一次命中一堆），真正的
/// 主窗口至少会写个标题。
///
/// `window_rule` 与窗口切换器用的是同一条判据（纯逻辑在 `core/window_match`）。
bool isMainWindow(HWND hwnd);

/// 与 `isMainWindow()` 同一套判据，但**不要求窗口当前可见**。
///
/// 工作空间把窗口藏起来用的是 `ShowWindow(SW_HIDE)`（M0 实测的唯一可用办法），
/// 那之后 `IsWindowVisible` 就是假的了 —— 而“它还是一个主窗口吗”这个问题仍然
/// 要能回答（我们的记录里可能就存着它，回来的时候还得显示它）。
bool isMainWindowIgnoringVisibility(HWND hwnd);

/// 窗口切换器（`windows` 动作）要不要列出这个窗口。
///
/// 判据是 `isMainWindow()` 再加上一句「**shell 真的会显示它**」：被
/// `DWMWA_CLOAKED` 藏起来、又留在当前虚拟桌面上的窗口用户切不过去
/// （本机实测：Windows 输入法的宿主 `TextInputHost.exe` 的「Windows 输入体验」
/// 就是一个 `IsWindowVisible` 为真、尺寸正常的顶层窗口），所以不入列表。
///
/// 被藏起来但在**别的**虚拟桌面上的窗口要保留：`windows` 动作会切到那张桌面去，
/// 这正是它比 Alt+Tab 多出来的能力。判断“在别的桌面上”会问一句虚拟桌面后端
/// （COM），所以**只对 cloaked 的窗口问**，不是每条都问。
///
/// `hiddenByUs` = 这个窗口正被**工作空间**藏着（来自状态机的记录，不是 Win32）：
/// 那时 `IsWindowVisible` 是假的，但它仍然是用户自己的程序窗口，必须能列出来
/// （选中它 = 先切到它那个工作空间再激活）。
///
/// `window_rule` 不走这一条：它只关心“看起来像不像主窗口”，而在窗口出现的
/// 路径（钩子线程）上加一次 COM 查询不合适。
bool isSwitchableWindow(HWND hwnd, bool hiddenByUs = false);

/// 当前打开的一个程序窗口（窗口切换器用）。
struct OpenWindow
{
    HWND hwnd = nullptr;
    /// 窗口标题。
    QString title;
    /// 属主进程的小写可执行文件名（`chrome.exe`）；拿不到时为空串。
    QString process;
};

/// 当前所有「可切换的窗口」（`isSwitchableWindow()`），
/// 按 `EnumWindows` 的 Z 序（最近用过的在前）。
///
/// **不包含 flowkeyd 自己的窗口**（弹窗 / 日志窗口不该出现在切换器里），
/// 也不含锁屏 / 隐藏窗口；跨虚拟桌面的窗口会被列出来（激活时会把视图切过去），
/// 而被 shell 藏起来的假窗口（输入法宿主、隐藏的 UWP 窗口）不会。
///
/// `tracked` 是工作空间状态机记着的窗口：被它们藏起来的那批虽然
/// `IsWindowVisible` 为假，也必须列出来（M9）。
std::vector<OpenWindow> listOpenWindows(const std::vector<HWND> &tracked = {});

/// 窗口的可见标题（取不到时返回 `"<untitled>"` / `"<invalid window>"`）。
QString windowTitle(HWND hwnd);

/// 这个窗口是否已经是“用户正在用的那个”：它是前台窗口，而且没有最小化。
///
/// `window` 动作的 `toggle`（默认开）靠它决定再按一次快捷键是唤起还是收起。
bool isActive(HWND hwnd);

/// 尽力“把这个窗口变成前台窗口”（前台锁的三级递进绕行，见实现）。
bool raiseWindow(HWND hwnd);

/// 让窗口始终在最上层（`topmost = false` 取消置顶）。
///
/// 同时改 `WS_EX_TOPMOST` 与 Z 序（`SetWindowPos` 的 `HWND_TOPMOST` /
/// `HWND_NOTOPMOST`），**不激活窗口**。`window_rule` 的 `topmost` 用它；
/// `window` 动作的 `toggle_topmost` 也走它。
bool setTopmost(HWND hwnd, bool topmost, QString *error);

/// 对一个已经解析出来的句柄应用 `window` 动作。
///
/// `animate = false`（默认）时先让这个窗口的 DWM 过渡动画静下来，于是
/// 最小化/最大化/还原不再播放缩放效果——只影响这个窗口这一次的调用。
/// `close` 与 `toggle_topmost` 不产生过渡，所以不去动它们。
///
/// `follow` 只对 `move_prev_desktop` / `move_next_desktop` 有意义：搬完之后
/// 把视图也切到目标桌面，并让窗口重新拿到前台（`SwitchDesktop` 激活的是目标
/// 桌面上上次用过的窗口，不一定是它）。激活失败只写一条 warning，因为窗口
/// 确实已经搬过去、视图也确实跟着走了。
bool applyTo(HWND hwnd,
             core::WindowOp op,
             bool animate,
             QString *detail,
             QString *error,
             bool follow = false);

/// “什么都没匹配到”的错误文本，与启动回退共享。
QString missing(const core::WindowQuery &query);

/// 前台窗口的标题与类名，供启动横幅与诊断使用。
QString foregroundTitle();

} // namespace flowkeyd::platform::win::window
