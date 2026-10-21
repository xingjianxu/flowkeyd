// 工作空间的**恢复日志**（预写日志）：`<配置目录>\workspace.json`。
//
// 这个文件回答一个问题：「我们此刻把哪些窗口藏起来了？」。它存在的唯一理由是
// **安全兜底** —— 工作空间用 `ShowWindow(SW_HIDE)` 隐藏窗口，进程要是被
// `taskkill /F` 杀掉、或者崩在动作线程上，那些窗口就没有人再负责把它们放回来，
// 用户看到的就是「窗口不见了」。
//
// 三条规则（M8）：
//   1. **先写日志再隐藏**：写不进去就不藏（`Dispatcher::applyWorkspaceEffects`）；
//   2. **显示成功之后才移除**：显示失败的那条留着，下次启动 / 手动恢复再试；
//   3. 启动最早期读它，校验窗口身份（`platform::win::workspace::stillSame()`）后
//      全部 `show` 出来，所以**配置坏掉也能恢复**。
//
// 纯逻辑（只用 QtCore），所以 `tst_workspace_log` 能直接覆盖解析 / 序列化与
// 容错；真正碰 Win32 的部分在 `platform/win/workspace`。
//
// 与 `launcher.json` / `windows.json` 同一套路：文件放在配置文件旁边，**不进
// 仓库**、也不用用户手写，坏了就当空的重来（这个文件坏掉时最合理的反应不是
// 让守护进程起不来，而是尽量把窗口放回来）。
#pragma once

#include "core/workspace.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace flowkeyd::core {

/// 窗口身份（`platform::win::workspace::identityOf()` 返回的同一份数据）。
///
/// 为什么要存它：`HWND` 会被系统复用。恢复日志是**跨进程生命周期**的，只有
/// 「PID + 属主进程创建时间 + 窗口类名」才能确认这个句柄还是我们当年藏的那个窗口，
/// 否则就会去 `show` 一个别人的窗口。
struct WindowIdentity
{
    /// `false` 表示「没读到身份」：恢复时**不校验**（宁可贵一点也不要漏掉该放的
    /// 窗口）。
    bool valid = false;
    std::uint32_t pid = 0;
    /// 属主进程的创建时间（`FILETIME`，100 ns 自 1601 起）。0 = 没读到。
    std::uint64_t processStarted = 0;
    QString className;

    friend bool operator==(const WindowIdentity &, const WindowIdentity &) = default;
};

/// 日志里的一条：一个**正被我们藏着**的窗口。
struct HiddenWindow
{
    WindowId window = 0;
    WindowIdentity identity;
    /// 藏起来那一刻的归属。恢复时用不到（只按身份窗口 `show`），留着便于排查。
    WorkspaceId workspace;
    /// 藏起来那一刻的窗口标题，同样只为看懂日志。
    QString title;

    friend bool operator==(const HiddenWindow &, const HiddenWindow &) = default;
};

/// 一份恢复日志 = 当前被藏起来的全部窗口（先藏的在前）。
struct WorkspaceLog
{
    std::vector<HiddenWindow> windows;

    bool isEmpty() const { return windows.empty(); }
};

/// 恢复日志的路径：与配置文件同一个目录下的 `workspace.json`。
///
/// `configPath` 是相对路径时按当前目录解析（与 `--config` 的行为一致）。
QString workspaceLogPath(const QString &configPath);

/// 解析一份恢复日志。**容错**：坏 JSON、条目类型不对、缺字段、`hwnd` 不是正数
/// 一律跳过那一条（`error` 里在整体坏掉时给一句英文说明）。
WorkspaceLog parseWorkspaceLog(const QByteArray &json, QString *error = nullptr);

/// 序列化成稳定的、人能看懂的 JSON（两个空格缩进、字段顺序固定、带版本号）。
///
/// 64 位整数（`hwnd` / `process_started`）写成**字符串**：JSON 的数字在
/// `QJsonValue` 里是 `double`，超过 2^53 就会静默丢精度。
QByteArray serializeWorkspaceLog(const WorkspaceLog &log);

/// 追加一条（同一个窗口已经有记录时**替换**它，不产生重复）。
void addHiddenWindow(WorkspaceLog &log, const HiddenWindow &entry);

/// 移除一条；真的移除了才返回 true。
bool removeHiddenWindow(WorkspaceLog &log, WindowId window);

/// 这个窗口在日志里吗。
bool hasHiddenWindow(const WorkspaceLog &log, WindowId window);

} // namespace flowkeyd::core
