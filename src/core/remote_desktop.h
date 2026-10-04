// 「键盘现在是不是在远程桌面里」的**纯逻辑**：只回答「这个前台窗口的属主进程
// 算不算远程桌面客户端」，不碰 Win32（前台窗口查询与取进程名在
// `platform/win/{window,hook}.cpp`）。
//
// 判据就是**前台窗口的属主进程名**：用户在本机开着 `mstsc.exe` / Windows App 时，
// 键是发给对面那台机器的，flowkeyd 不该插手（见 AGENTS.md 第 2 节第 26 条）。
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace flowkeyd::core {

/// 内置的「远程桌面客户端」进程名单（小写；只列微软自己的 RDP 客户端）。
///
/// 配置里的 `settings.remote_desktop.processes` 会**整体替换**它（空表 = 谁都不算，
/// 也就等于关掉了这个功能）。第三方远程控制软件（ToDesk / 向日葵 / AnyDesk……）
/// 刻意**不在**默认名单里：把它们的窗口当成远程桌面会整片放行，用户自己写。
const QStringList &builtinRemoteDesktopProcesses();

/// 这个可执行文件名算不算远程桌面客户端。
///
/// 与 `window_rule` / `window` 动作的 `process` 用同一套匹配：大小写无关的
/// **子串**匹配（`windowProcessMatches()`）。`executableName` 为空（拿不到属主
/// 进程）时返回 false —— 未知属主不能假定它是远程桌面。
bool isRemoteDesktopProcess(const std::optional<QString> &executableName,
                            const QStringList &processes);

} // namespace flowkeyd::core
