// 工作空间的**平台后端**：隐藏 / 显示 / 重排 Z 序 / 窗口身份 / 可管理窗口枚举。
//
// 隐藏方式只有一个可用选项（`docs/notes/pitfalls.md` 的 M0 实测结论）：
//   * `ShowWindow(SW_HIDE)` ✅ —— 唯一还能在藏起来之后 `SetForegroundWindow`
//     抢回前台的办法，位置/大小/最大化/最小化全部保持不变；
//   * `SetWindowPos(SWP_HIDEWINDOW)` ❌ —— 单次更快，但隐藏之后抢不到前台；
//   * 跨进程 DWM cloak ❌ —— 对别的进程的窗口直接 `E_ACCESSDENIED`。
//
// 所以本文件是「隐藏方式」唯一的家：以后要换方案（或再加一条兼容路径）只改这里。
//
// 两条硬约束（M0 实测）：
//   1. `SW_HIDE` 是**跨线程 `SendMessage` 语义**（实测 0.6–2.2 ms/窗口）：目标进程
//      卡死时可能把调用线程挂住 → 这些函数**只允许在动作工作线程上调用**，绝不上
//      钩子线程（不变量 1）。`IsHungAppWindow()` 是这里唯一的廉价护航。
//   2. 每个 hide/show 都会产生一条 `EVENT_OBJECT_HIDE` / `EVENT_OBJECT_SHOW`，而且
//      **分不出是谁干的** → 「自己引起的事件」只能靠调用方的期望事件表抑制（M5）。
//
// 「窗口在不在」也不能看 `IsWindowVisible`：被我们藏起来的窗口全都是不可见的。
// 状态机自己的记录才是判据，本文件的 `enumerateManageable()` 因此要接受一份
// 「已记录的窗口」列表（见 `core::isManageableWindow()`）。
#pragma once

#include "core/workspace.h"
#include "platform/win/ffi.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace flowkeyd::platform::win::workspace {

// --- 句柄与状态机的不透明窗口 id 之间的转换 ---------------------------------
//
// 状态机只认 `core::WindowId`（`uint64`，不透明），钩子事件与分发层在这一对
// 转换函数上往返。放在这里而不是各调用点：两边的形状一旦改了就只改一处。

inline core::WindowId idOf(HWND hwnd)
{
    return static_cast<core::WindowId>(reinterpret_cast<quintptr>(hwnd));
}

inline HWND handleOf(core::WindowId id)
{
    return reinterpret_cast<HWND>(static_cast<quintptr>(id));
}

/// 窗口身份：进程 ID + **进程创建时间** + 窗口类名。
///
/// 用途是「日志重放时确认这个 `HWND` 还是当年那个窗口」（`HWND` 会被复用）。
/// 窗口自己的创建时间没有公开 API，所以用属主**进程**的创建时间：
/// 进程一换（PID 复用）就对不上；同一个进程里 `HWND` 复用的情况由状态机在
/// `EVENT_OBJECT_DESTROY` 上的记录清理兜住（`core::WorkspaceState::windowDestroyed`）。
struct Identity
{
    bool valid = false;
    unsigned long pid = 0;
    /// 属主进程的创建时间（`FILETIME`，100 ns 自 1601 起）。
    std::uint64_t processStarted = 0;
    QString className;

    bool operator==(const Identity &) const = default;
};

/// 读出窗口身份；窗口已经不在时 `valid` 为 false。
Identity identityOf(HWND hwnd);

/// 这个句柄还是不是**当初那个**窗口。
bool stillSame(HWND hwnd, const Identity &identity);

/// 句柄还有效吗（`IsWindow`）。
bool isAlive(HWND hwnd);

/// 真正要藏 / 显的那个窗口。
///
/// UWP（商店应用）的顶层窗口是 `ApplicationFrameHost.exe` 的
/// `ApplicationFrameWindow`，里面才是应用自己的 `Windows.UI.Core.CoreWindow`：
/// 要藏的是**宿主窗口**（藏/显同一个 `HWND`，内容跟着走）。传进来一个子窗口时
/// 这里沿 `GA_ROOT` 往上找到那个宿主，其它情况原样返回。
HWND manageTarget(HWND hwnd);

/// 隐藏一个窗口（`ShowWindow(SW_HIDE)`），并记下 `WINDOWPLACEMENT` 备用。
///
/// 位置 / 大小 / 最大化 / 最小化都由 Windows 自己保持（M0 实测），记录只是给
/// `show()` 一个“万一被改了就摆回去”的兜底。
///
/// 只在动作工作线程上调用。目标进程无响应时直接失败（英文原因），不去冒把线程
/// 挂住的风险。
bool hide(HWND hwnd, QString *error);

/// 把窗口显示回来（不激活）。
///
/// 最小化的窗口用 `SW_SHOWMINNOACTIVE`（否则会把用户刚收起来的窗口弹出来），
/// 其余用 `SW_SHOWNOACTIVATE`（我们随后会显式决定聚焦谁）。
///
/// `insertAfter` 是**记录栈里比它低一层**的那个窗口（`nullptr` = 不动 Z 序）；
/// 真正的整栈还原交给 `restack()`。
bool show(HWND hwnd, HWND insertAfter, QString *error);

/// 按记录栈还原 Z 序（`bottomToTop`，底 → 顶），**不激活**。
///
/// M0 实测：`SW_HIDE` 本来就不丢 Z 槽位，窗口显示回来会原地归位；这个函数是
/// 给“还原之后顺序确实乱了”的兜底（也用于跨显示器 / 跨空间的成批还原）。
bool restack(const std::vector<HWND> &bottomToTop, QString *error);

/// 枚举「归工作空间管」的窗口，按 `EnumWindows` 的 Z 序（最近用过的在前）。
///
/// `tracked` 是工作空间状态机记录里的窗口：它们可能正被我们藏着
/// （`IsWindowVisible` 为假），因此**靠记录纳入**，不能用可见性判断。
/// `excludedProcesses` 是 `settings.workspace.exclude`（进程名子串，空串已被
/// `--check` 拒绝）。判断本身在 `core::isManageableWindow()`（纯逻辑、可单测）。
std::vector<HWND> enumerateManageable(const std::vector<HWND> &tracked,
                                      const QStringList &excludedProcesses);

/// 单个窗口版本的 `enumerateManageable()`：「这个窗口归不归工作空间管」。
///
/// `tracked` = 它已经在状态机的记录里（于是**不看可见性**：我们藏起来的窗口
/// `IsWindowVisible` 是假的）。钩子事件（出现/位置变化）要逐窗口判断，不能为
/// 一个事件枚举全部窗口。
bool isManageable(HWND hwnd, bool tracked, const QStringList &excludedProcesses);

/// 窗口的类名（取不到时为空串）。
QString classNameOf(HWND hwnd);

} // namespace flowkeyd::platform::win::workspace
