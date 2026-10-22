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
//   1. `SW_HIDE` 是**跨线程 `SendMessage` 语义**：目标进程卡死时可能把调用线程
//      挂住，而且**耗时就是目标 App 的响应时间** → 这些函数**只允许在动作工作
//      线程上调用**，绝不上钩子线程（不变量 1）。`IsHungAppWindow()` 是这里唯一的
//      廉价护航。
//      **切工作空间必须用批量接口**（`hideMany()` / `showMany()`）：逐个做的时候
//      一屏窗口会“依次”消失 / 出现（目标 App 忙多久，后面的窗口就晚多久），
//      批量版把每个窗口的 `ShowWindow` 并行做掉，总耗时从“求和”变成“取最大”。
//      真机实测：UI 线程睡 300 ms 的窗口，串行 3 个要 900 ms。
//   2. 每个 hide/show 都会产生一条 `EVENT_OBJECT_HIDE` / `EVENT_OBJECT_SHOW`，而且
//      **分不出是谁干的** → 「自己引起的事件」只能靠调用方的期望事件表抑制（M5）。
//
// 「窗口在不在」也不能看 `IsWindowVisible`：被我们藏起来的窗口全都是不可见的。
// 状态机自己的记录才是判据，本文件的 `enumerateManageable()` 因此要接受一份
// 「已记录的窗口」列表（见 `core::isManageableWindow()`）。
#pragma once

#include "core/placement.h"
#include "core/workspace.h"
#include "core/workspace_log.h"
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
///
/// 定义在 `core::WindowIdentity`（`core/workspace_log.h`）：恢复日志要把它写进
/// `workspace.json`，而 `core` 不能反过来依赖平台层。
using Identity = core::WindowIdentity;

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

/// 一个窗口在这次批量显隐里的结果（与传进去的 `windows` **一一对应**）。
struct BatchResult
{
    bool ok = false;
    /// 英文失败原因（与 `hide()` / `show()` 的 `error` 同一个口径）。
    QString error;
};

/// 一次性藏 / 露**一批**窗口。
///
/// 切工作空间专用。与逐个 `hide()` / `show()` 的三个区别：
///  1. **并行**：每个窗口的 `ShowWindow` 在各自的临时线程上做，然后一起 `join`。
///     逐个做是“一屏窗口依次消失”的根源 —— `SW_HIDE` / `SW_SHOWNOACTIVATE` 是
///     跨线程 `SendMessage` 语义，耗时取决于**目标 App 的 UI 线程**；
///     `show()` 里收尾的 `SetWindowPlacement` / `SetWindowPos` 同理。
///     并行之后总耗时从“求和”变成“取最大”，一个 App 卡住不再拖住后面的窗口。
///  2. **整批关过渡动画**（`dwm::setTransitionsDisabled()`）：这一批窗口的显隐
///     是一个视觉动作，属性要在整批前后各设一次，不逐窗口复原。
///  3. 返回**一一对应**的结果，失败原因留在 `BatchResult` 里由调用方记日志
///     （工作线程里既不写日志也不碰任何共享状态）。
///
/// `savedPlacements()`（`WINDOWPLACEMENT` 兜底记录）仍然只在**调用线程**上读写。
///
/// 只在动作工作线程上调用。
std::vector<BatchResult> hideMany(const std::vector<HWND> &windows);
std::vector<BatchResult> showMany(const std::vector<HWND> &windows);

/// 隐藏一个窗口（`ShowWindow(SW_HIDE)`），并记下 `WINDOWPLACEMENT` 备用。
///
/// 位置 / 大小 / 最大化 / 最小化都由 Windows 自己保持（M0 实测），记录只是给
/// `show()` 一个“万一被改了就摆回去”的兜底。
///
/// **不播动画**：`SW_HIDE` 本身会让 DWM 淡出，所以这里关掉这个窗口的过渡
/// （切工作空间要的是“直接不见”）。设不上这个属性时只是多一段动画，不影响结果。
///
/// 单个窗口的一次性操作用它；**切工作空间请用 `hideMany()`**（理由见那里）。
/// 只在动作工作线程上调用。目标进程无响应时直接失败（英文原因），不去冒把线程
/// 挂住的风险。
bool hide(HWND hwnd, QString *error);

/// 把窗口显示回来（不激活）。
///
/// 最小化的窗口用 `SW_SHOWMINNOACTIVE`（否则会把用户刚收起来的窗口弹出来），
/// 其余用 `SW_SHOWNOACTIVATE`（我们随后会显式决定聚焦谁）。同样不播动画。
///
/// `insertAfter` 是**记录栈里比它低一层**的那个窗口（`nullptr` = 不动 Z 序）；
/// 真正的整栈还原交给 `restack()`。
///
/// 单个窗口的一次性操作用它；**切工作空间请用 `showMany()`**（理由见那里）。
bool show(HWND hwnd, HWND insertAfter, QString *error);

/// 按记录栈还原 Z 序（`bottomToTop`，底 → 顶），**不激活**。
///
/// M0 实测：`SW_HIDE` 本来就不丢 Z 槽位，窗口显示回来会原地归位；这个函数是
/// 给“还原之后顺序确实乱了”的兜底（也用于跨显示器 / 跨空间的成批还原）。
bool restack(const std::vector<HWND> &bottomToTop, QString *error);

/// 把窗口搬回 `destination` 这块屏上，**同时保持它现在的显隐 / 最大化 / 最小化
/// 状态**。
///
/// 显示器拔插专用（M7）。为什么不直接用 `monitor::moveToMonitor()`：它靠
/// `SetWindowPlacement` 的 `showCmd` 收起 / 恢复窗口，对**被我们藏起来的窗口**
/// 会把它们弹出来；而拔插那一刻窗口正处在不受我们控制的状态（藏着的、最大化的、
/// 最小化的都有）。这里只改 `rcNormalPosition` 与坐标：
///   * 几何由 `core::placementRect()` 算（最大化时就是目标屏的工作区，普通窗口
///     保持大小并居中）；
///   * 收尾按进来时的可见性：本来藏着就再 `SW_HIDE` 一次（`SetWindowPlacement`
///     会按 `showCmd` 改显隐，所以这一步不可省）。
///
/// 只在动作工作线程上调用（与 `hide()` / `show()` 同一条理由：这些调用都是跨线程
/// `SendMessage` 语义，目标进程卡死时会把调用线程挂住）。
bool relocateToMonitor(HWND hwnd, const core::MonitorDescription &destination, QString *error);

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

// --- 恢复日志（M8：安全与恢复） -------------------------------------------
//
// 「先写日志再隐藏」的写盘在动作线程上做（见 `Dispatcher`），而**崩溃恢复**是
// 启动最早期的一次性动作：那时动作线程还不存在（`main.cpp` 在读配置之前就做），
// 所以读写与恢复都放在这里，让三条入口共用同一份实现：
//   * 启动恢复（`main.cpp`）；
//   * `--restore-windows`（没有实例在跑时的离线恢复）；
//   * `Dispatcher` 的「恢复全部窗口」（托盘菜单 / 退出 / 注销）。

/// 读 `<配置目录>\workspace.json`。文件不存在 = 空日志；坏文件只记 warning，
/// 当作空的重来（不能因为这个文件坏掉就让窗口永远回不来）。
core::WorkspaceLog loadWorkspaceLog(const QString &configPath, QString *error = nullptr);

/// 写恢复日志（`QSaveFile` 原子替换）。空日志 = 删掉文件（不留一个空壳）。
bool saveWorkspaceLog(const QString &configPath, const core::WorkspaceLog &log, QString *error);

/// 把日志里记着的窗口**放回可见**，返回**还没有恢复成功**的那几条
/// （已经不在 / 身份对不上 / 显示失败的分别计数见 `RestoreReport`）。
///
/// 身份对不上（`HWND` 被复用）或窗口已经没了的条目**不返回**：它们已经无从恢复，
/// 留着只会每次启动都白试一次。显示失败的条目要留着（下次 / 手动恢复再试）。
struct RestoreReport
{
    /// 日志里有几条。
    int entries = 0;
    /// 真的放回可见的窗口数。
    int restored = 0;
    /// 窗口已经不在 / 身份对不上（从日志里丢掉）。
    int dropped = 0;
    /// 试了但失败（留在日志里，下次再试）。
    int failed = 0;
};

/// `restoreFromLog()` 的结果：报告 + 还没恢复成功的那些条目。
struct RestoreResult
{
    RestoreReport report;
    core::WorkspaceLog remaining;
};

/// 把一份日志里的窗口逐个恢复；返回剩下的（调用方负责写回）。
RestoreResult restoreHiddenWindows(const core::WorkspaceLog &log);

/// 完整的一条恢复路径：读日志 → 恢复 → 把剩下的写回（全恢复就删文件）。
///
/// 崩溃恢复与 `--restore-windows` 都走它。**不依赖配置**：日志里的路径信息足够
/// 找到窗口，所以配置坏掉时窗口也能回来。
RestoreResult restoreFromLog(const QString &configPath);

} // namespace flowkeyd::platform::win::workspace
