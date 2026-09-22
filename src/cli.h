// 命令行解析。手写实现，以便帮助文本和错误信息完全贴合本工具的需要，
// 且不引入额外依赖（见 AGENTS.md 第 2 节第 7 条）。
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace flowkeyd::app {

/// 解析后的命令行。
struct CliOptions
{
    std::optional<QString> config;
    /// 校验配置后退出。
    bool check = false;
    /// 打印已配置的快捷键后退出。
    bool list = false;
    /// 打印 `keys = ...` 中可用的全部按键名。
    bool listKeys = false;
    /// 请正在运行的实例干净退出（按配置文件路径匹配）。
    bool quit = false;
    /// 不自动注册 / 刷新「登录时以最高权限启动」的计划任务。
    bool noAutostart = false;
    /// 删除那个计划任务后退出（需要管理员权限）。
    bool removeAutostart = false;
    std::optional<QString> logLevel;
    std::optional<QString> logFile;
    bool noColor = false;
    /// 即使另一个实例已占用同一配置文件也照常启动。
    bool allowMulti = false;
    /// 不弹交互提示：已在运行的提示、是否注册开机自启的询问都跳过，按默认处理。
    bool noPrompt = false;
    /// 不自动提权（测试与调试用）。
    bool noElevate = false;
    /// 保留控制台输出。
    bool console = false;
    /// 启动时直接打开日志窗口。
    bool logWindow = false;
    /// 兼容 oskeyd 的内部参数；本项目接受但忽略它。
    std::optional<qint64> parentPid;
    /// 内部标记：本进程已经以管理员权限运行，不要再重启自己。
    bool elevated = false;
    bool showHelp = false;
    bool showVersion = false;

    /// 处理完请求后进程是否应当立即退出。
    /// 这些命令**绝不允许**提权，也绝不安装钩子。
    ///
    /// `--quit` 不在其中：它不装钩子、不提权，但确实要去碰另一个正在跑的实例
    /// （所以由 main 单独处理，见 AGENTS.md 第 10 节）。
    /// `--remove-autostart` 也不在：它要管理员权限才能删任务。
    bool isOfflineCommand() const { return showHelp || showVersion || listKeys || check || list; }
};

/// 解析参数（含程序名，会被跳过）。
/// 出错时返回一条中文错误信息（用户可见的 CLI 文案用中文）。
std::optional<QString> parseCli(const QStringList &args, CliOptions *out);

/// 中文帮助文本。
QString helpText();

/// 版本文本，其中包含所链接的 Lua 版本（见 AGENTS.md 第 8 节）。
QString versionText();

/// 本进程的命令行参数（含程序名），来自 `GetCommandLineW`，因此不受代码页影响。
QStringList commandLineArguments();

} // namespace flowkeyd::app
