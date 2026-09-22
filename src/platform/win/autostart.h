// 开机自启：守护进程自己注册 / 刷新登录计划任务。
//
// 为什么是计划任务而不是「启动文件夹」或 `HKCU\...\Run`（见 AGENTS.md 第 10 节）：
// 只有计划任务能在登录时**以最高权限**启动程序而不弹 UAC，而 flowkeyd 需要提权
// 才能驱动提权进程的窗口、才能执行电源动作。服务也不行：session 0 里的
// `WH_KEYBOARD_LL` 看不到桌面按键，也没有托盘。
//
// 任务指向**当前正在运行的 exe 路径**（每次启动时检查并按需刷新），而不是某个
// 预设的安装目录：把 exe 拷到哪儿就在哪儿生效，没有「安装目录过期」这回事。
//
// 实现走 `schtasks.exe /Create /XML`（与当年的 `scripts/install.ps1` 同一套 XML）：
// Task Scheduler 的 COM 接口要手写十来个 vtable（本仓库风险最高的做法，
// 见 AGENTS.md 第 3 节），而 `schtasks` 是系统自带的稳定入口。
#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace flowkeyd::platform::win {

/// 计划任务名（登录时以最高权限启动的 flowkeyd）。
QString autostartTaskName();

/// 当前进程的完整可执行文件路径。
QString currentExecutablePath();

/// 当前用户的 `DOMAIN\user`（计划任务的 `UserId`）。
QString currentUserAccount();

/// 一条登录自启任务的参数。
struct AutostartSpec
{
    QString taskName = autostartTaskName();
    /// 要启动的 exe（绝对路径）。
    QString executable;
    /// 任务的 `WorkingDirectory`（`--config` 的相对路径与模板里的 `{cwd}` 都看它）。
    QString workingDirectory;
    /// `DOMAIN\user`。
    QString userId;
    /// 登录后延迟几秒再启动（explorer 还没就绪时托盘图标会丢）。
    int logonDelaySeconds = 15;
};

/// 渲染计划任务 XML（纯函数，可单测）。
QString buildTaskXml(const AutostartSpec &spec);

/// 从计划任务 XML 里取 `<Command>` 的文本（纯函数，可单测）。
std::optional<QString> taskXmlCommand(const QString &xml);

/// 把 `schtasks` 的输出解码成文本（纯函数，可单测）。
///
/// `schtasks /Query /XML` 重定向到管道后是 UTF-8/ANSI 字节（没有 BOM），
/// 但别的命令与别的系统区域设置下可能是 UTF-16；这里把几种情况都兜住。
QString decodeTaskOutput(const QByteArray &bytes);

/// 两个 exe 路径是不是同一个（分隔符与大小写归一之后比较）。
bool sameExecutablePath(const QString &a, const QString &b);

enum class AutostartState
{
    /// 任务不存在。
    Absent,
    /// 任务存在，且指向给定的 exe。
    Matches,
    /// 任务存在，但指向别的 exe（或读不出 `<Command>`）。
    Different,
};

/// 查询任务状态。调用本身失败（比如没有读权限）时返回 false 并填 `error`。
bool queryAutostartTask(const QString &taskName,
                        const QString &executable,
                        AutostartState *state,
                        QString *error);

/// 创建 / 覆盖任务（需要管理员权限）。
bool registerAutostartTask(const AutostartSpec &spec, QString *error);

/// 删除任务；任务本来就不存在时也算成功（需要管理员权限）。
bool removeAutostartTask(const QString &taskName, QString *error);

/// 启动时的策略：任务缺失、或指向的 exe 与 `spec.executable` 不是同一个，
/// 就把它（重新）注册成 `spec.executable`；已经正确时什么都不做。
///
/// 注册失败只记一条 warning，绝不影响守护进程继续跑（没有自启的热键仍然可用）。
void ensureAutostart(const AutostartSpec &spec);

} // namespace flowkeyd::platform::win
