// 启动进程与打开外部目标（`run` / `open` 动作、程序启动器）。
//
// **守护进程自己提权运行，但它启动的程序默认不该跟着提权**：管理员令牌下的
// 浏览器 / 编辑器会失去拖放、把配置写到奇怪的地方、还会被一些程序直接拒绝。
// 所以 `RunCommandSpec::elevated` 默认是 `false`，含义是「以当前登录用户的普通
// 权限启动」——提权实例会从登录 shell 复制一份令牌再启动（见 `process.cpp` 的
// `loggedOnUserToken()`），`elevated = true` 才继承 / 申请管理员权限。
//
// 同一个道理适用于 `ShellExecute`（`open` 动作与程序启动器）：它没有令牌参数，
// 所以提权实例会把请求交给一个**普通权限的代理进程**（flowkeyd 自己的一个隐藏
// 副本，`--shell-open` / `--shell-app`），由它去调 shell。协议就是
// `ShellHelperRequest` + 一个写回结果的小文件。
//
// 终端窗口不能靠标题找，也不能用 `DETACHED_PROCESS`（会静默杀死控制台子进程），
// 详见 AGENTS.md 第 10 节。
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
    /// 以**管理员权限**启动；默认 `false` = 当前登录用户的普通权限。
    bool elevated = false;
    QMap<QString, QString> env;
};

/// 启动一个程序（三种可能的后端：直接 `CreateProcessW`、用自己的提权令牌、
/// 或者以登录用户的普通权限启动）。成功时返回 true（`detail` 里是 `pid N` 或
/// 退出码）。
bool runCommand(const RunCommandSpec &spec, QString *detail, QString *error);

/// `cmd.exe /C` 给含空格的参数加引号。
QString quoteForCmd(const QString &arg);

/// `ShowMode` → `ShellExecuteW` 的 `nShowCmd`（也是代理进程里的窗口状态）。
int showCode(core::ShowMode show);

/// `ShellExecuteW("open", ...)`：打开 URL / 文档 / 文件夹。
///
/// 提权运行时请求会交给一个**普通权限的代理进程**去做（见 `runShellHelperAsUser`），
/// 所以打开的文档 / 浏览器不会跟着 flowkeyd 一起变成管理员进程。
bool openTarget(const QString &target,
                const std::optional<QString> &args,
                const std::optional<QString> &cwd,
                core::ShowMode show,
                QString *error);

/// 与 `openTarget()` 相同，但**永远在本进程里**执行。
///
/// 代理进程自己走这条路；也用来在没有提权时省掉一次进程创建。
bool openTargetInProcess(const QString &target,
                         const std::optional<QString> &args,
                         const std::optional<QString> &cwd,
                         core::ShowMode show,
                         QString *error);

/// 代理进程要做的 shell 动作（`--shell-open` / `--shell-app`，内部命令）。
struct ShellHelperRequest
{
    enum class Mode {
        /// 用 shell 打开一个 URL / 文档 / 文件夹（`open` 动作）。
        Open,
        /// 激活一个程序启动器的条目（`shell:AppsFolder\…` / `::{…}` /
        /// `ms-settings:…`）。
        App,
    };

    Mode mode = Mode::Open;
    /// `Open` 时的目标；`App` 时的启动名。
    QString target;
    /// 只有 `Open` 用：`ShellExecuteW` 的参数串与工作目录。
    std::optional<QString> args;
    std::optional<QString> cwd;
    core::ShowMode show = core::ShowMode::Normal;
    /// 结果写回哪个文件（失败时里面是错误文本，成功时是空文件）。
    std::optional<QString> resultPath;
};

/// 代理进程的入口：执行请求、把结果写回 `resultPath`，返回进程退出码。
///
/// 只在 `--shell-open` / `--shell-app` 那条离线路径上调用：不提权、不读配置、
/// 不装钩子、不碰单实例锁。
int runShellHelper(const ShellHelperRequest &request);

/// 一次代理进程调用的结果。
struct ShellHelperOutcome
{
    /// 代理进程起来了，而且等到了它的退出码。
    bool started = false;
    int exitCode = 0;
    /// 代理进程写回来的错误文本（成功时为空）。
    QString output;
    /// 起不来 / 等不到它结束的原因（`started == false` 时有效）。
    QString error;
};

/// 用一个**普通权限的代理进程**做请求。
///
/// 提权运行时 `openTarget()` 与程序启动器走的就是这条路；没有登录 shell 可用
/// （拿不到令牌）或代理进程起不来时 `started` 为 false，调用方自己决定是报错
/// 还是退回「在本进程里直接做」。
///
/// `helperExecutable` 默认是**本进程自己的 exe**（产品路径永远如此）。只有从
/// 另一个可执行文件里调用时才需要显式给出：单元测试就是 `tst_interactive.exe`，
/// 它自己不带 `--shell-*` 那条内部命令。
ShellHelperOutcome runShellHelperAsUser(const ShellHelperRequest &request,
                                        const QString &helperExecutable = QString());

/// 构造传给 `CreateProcessW` 的 UTF-16 环境块（当前环境 + 覆盖项）。
/// 空覆盖项时返回的块仍然可用（Windows 要求按名称不区分大小写排序）。
std::vector<wchar_t> buildEnvironmentBlock(const QMap<QString, QString> &overrides);

} // namespace flowkeyd::platform::win
