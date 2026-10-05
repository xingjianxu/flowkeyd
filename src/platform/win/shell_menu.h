// 「某个开始菜单快捷方式的**原生右键菜单**」。
//
// 程序启动器（`apps()` 动作）里右键一格，弹出来的就是 Windows 自己给这个
// `.lnk` 生成的那份菜单：打开、以管理员身份运行、打开文件位置、固定到「开始」
// 屏幕、属性、卸载……逐条与资源管理器、开始菜单一致，而且是**系统生成**的 ——
// 我们不去猜有哪些条目，也不去实现它们（`IContextMenu::InvokeCommand` 由 shell
// 自己执行）。
//
// 这里用到的都是**已公开**的 COM 接口（`IShellFolder` / `IContextMenu` /
// `IContextMenu2` / `IContextMenu3`，MinGW 的 `shobjidl.h` 里有全套声明），所以
// 不需要像虚拟桌面那样手写 vtable；真正需要小心的只有一件事：菜单消息的转发
// （见 `.cpp` 里的 `MenuMessageRouter`）。
//
// 与 `apps.h` 一样：COM 按 STA 用，所以**必须在已经初始化成 STA 的线程上调用**。
// 启动器卡片跑在 Qt 的 GUI 线程上（Qt 为 OLE/拖放把它初始化成 STA），满足这一条。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <functional>

namespace flowkeyd::platform::win::shell_menu {

/// 一次「弹出并执行一个文件的原生右键菜单」的结果。
struct MenuResult
{
    /// 用户真的选了某一条（false = 按 `Esc`、点了菜单外面，什么都没发生）。
    bool invoked = false;
    /// 选中那条在 shell 菜单里的命令偏移（`idCmdFirst` 起的序号，诊断用；
    /// 取消时是 0）。
    unsigned command = 0;
    /// 硬错误（COM / shell 出错）。**用户取消不算错误。**
    QString error;
};

/// 在**光标处**弹出 `path`（开始菜单里的 `.lnk`，也可以是 exe / 文件夹）的
/// 系统右键菜单，并执行用户选中的那一条。
///
/// * **阻塞**：内部 `TrackPopupMenuEx` 一直等到用户选完（或取消）才返回。所以
///   只能在**拥有 `owner` 的那条线程**上调用。
/// * `owner` 必须是**可见的顶层窗口**，而且调用时它就在前台（菜单的「点外面就
///   关掉」要求拥有窗口是前台窗口）。调用方（启动器卡片）正好满足这两条。
/// * 位置取**光标当前位置**（`GetCursorPos`），不从参数传：Qt 的全局坐标是
///   设备无关的逻辑像素，而 `TrackPopupMenuEx` 要的是物理像素 —— 右键那一刻
///   光标就压在那一格上，直接问系统最省事，也天然跨 DPI 正确。
/// * `beforeInvoke` 在**执行**用户选中的命令之前同步调一次（用户取消时不调）。
///   调用方用它把卡片收起来：一是「选中条目就关」的产品语义，二是执行
///   「属性」这类命令会开一个以 `owner` 为属主的对话框，卡片先藏起来才不会连
///   对话框一起被藏掉，而且「打开文件位置」拉起的资源管理器窗口要拿得到前台。
MenuResult showItemMenu(HWND owner,
                        const QString &path,
                        const std::function<void()> &beforeInvoke = {});

} // namespace flowkeyd::platform::win::shell_menu
