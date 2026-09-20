// 电源动作：睡眠 / 休眠 / 关机 / 重启 / 注销 / 锁定 / 关屏。
//
// 分成三块：
//
// * **睡眠与休眠**是 `powrprof!SetSuspendState`（`TRUE` 是休眠，`FALSE` 是睡眠）。
//   `powrprof` 在本仓库允许静态链接的集合里，所以直接链，不像 oskeyd 那样
//   运行时解析（那边是 rust-mingw 的导入库缺这个符号，见 AGENTS.md 第 2 节）。
// * **关机 / 重启 / 注销**是 `user32!ExitWindowsEx`，**锁定**是
//   `user32!LockWorkStation`。前三个需要调用进程的令牌里启用
//   `SeShutdownPrivilege`（`enableShutdownPrivilege`），所以 flowkeyd 平时默认
//   提权运行正好够用；没提权时会以一条明确的错误失败，而不是静默什么都不做。
// * **关屏**是公开的 `WM_SYSCOMMAND`/`SC_MONITORPOWER` 广播（`screenOff`）：
//   只关显示器，系统与钩子照常运行；任何键盘/鼠标输入都会把屏幕点亮。
//
// 刻意都不加 `EWX_FORCE`：只加 `EWX_FORCEIFHUNG`，也就是仅强杀那些已经卡住、
// 不响应 `WM_QUERYENDSESSION` 的程序。有未保存内容的程序照样会弹它自己的确认框，
// 所以这个“关机”按钮不会比开始菜单里的那个更粗暴。
//
// **自动化测试绝不调用 `execute`。** 睡眠、关机、重启都会打断用户手上的事情；
// 单测只覆盖 `requiresShutdownPrivilege` 这条纯逻辑，真正的系统调用只有用户
// 按下去才会发生（见 AGENTS.md 第 9 阶段的验收要求）。
#pragma once

#include "core/action.h"

#include <QString>

#include <vector>

namespace flowkeyd::platform::win::power {

/// `power` 动作的纯逻辑表项：规范名与是否需要 `SeShutdownPrivilege`。
struct PowerOpInfo
{
    core::PowerOp op = core::PowerOp::Sleep;
    /// `--list` / 日志里显示的规范 snake_case 名。
    const char *name = "sleep";
    /// 必须启用 `SeShutdownPrivilege` 才能成功。
    bool needsShutdownPrivilege = false;
};

/// 全部电源操作（顺序与 oskeyd 的 `PowerOp` 一致）。
const std::vector<PowerOpInfo> &powerOpTable();

/// 纯函数：这个 op 是否**必须**启用 `SeShutdownPrivilege` 才能成功？
///
/// 只有关机 / 重启 / 注销需要；`lock` / `sleep` / `hibernate` / `screen_off`
/// 都不需要（`sleep`/`hibernate` 会顺手试一次，但失败不拦）。`execute` 用它
/// 决定是否在动手前强制要求特权，所以“关屏不会先报一句指错方向的权限错误”
/// 是可单测的。
bool requiresShutdownPrivilege(core::PowerOp op);

/// 把 `SeShutdownPrivilege` 加进本进程的令牌。
///
/// 注意 `AdjustTokenPrivileges` 的坑：即使一个特权都没加上它也可能返回 `TRUE`，
/// 必须看 `GetLastError()` 是不是 `ERROR_NOT_ALL_ASSIGNED`。
bool enableShutdownPrivilege(QString *detail, QString *error);

/// 执行一个电源动作。成功时 `detail` 是 `"suspending"` / `"locked"` /
/// `"display(s) off"` / `"shutdown requested"` 之类的一行说明。
bool execute(core::PowerOp op, QString *detail, QString *error);

} // namespace flowkeyd::platform::win::power
