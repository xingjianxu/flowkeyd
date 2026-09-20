// 启动进程与打开外部目标（`run` / `open` 动作）。
//
// 终端窗口不能靠标题找，也不能用 `DETACHED_PROCESS`（会静默杀死控制台子进程），
// 详见 AGENTS.md 第 10 节与 ../oskeyd/AGENTS.md 第 6 节。
#pragma once

#include "core/action.h"

#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace flowkeyd::platform::win {

struct RunCommandSpec
{
    QString program;
    QStringList args;
    std::optional<QString> cwd;
    core::ShowMode show = core::ShowMode::Normal;
    /// 通过 `cmd.exe /C` 运行，并允许 shell 元字符。
    bool shell = false;
    /// 阻塞直到进程退出。
    bool wait = false;
    QMap<QString, QString> env;
};

/// `CreateProcessW` 启动一个程序。成功时返回 true（`detail` 里是 `pid N` 或退出码）。
bool runCommand(const RunCommandSpec &spec, QString *detail, QString *error);

/// `cmd.exe /C` 给含空格的参数加引号。
QString quoteForCmd(const QString &arg);

/// `ShowMode` → `ShellExecuteW` 的 `nShowCmd`。
int showCode(core::ShowMode show);

/// `ShellExecuteW("open", ...)`：打开 URL / 文档 / 文件夹。
bool openTarget(const QString &target,
                const std::optional<QString> &args,
                const std::optional<QString> &cwd,
                core::ShowMode show,
                QString *error);

/// 构造传给 `CreateProcessW` 的 UTF-16 环境块（当前环境 + 覆盖项）。
/// 空覆盖项时返回的块仍然可用（Windows 要求按名称不区分大小写排序）。
std::vector<wchar_t> buildEnvironmentBlock(const QMap<QString, QString> &overrides);

} // namespace flowkeyd::platform::win
