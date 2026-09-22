#include "cli.h"

#include <QStringList>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

#include "lua/lua_include.h"

#include "core/version.h"

namespace flowkeyd::app {

namespace {

/// 取 `--flag=value` 里的内联取值。
bool splitInline(const QString &arg, QString *name, std::optional<QString> *inlineValue)
{
    if (!arg.startsWith(QLatin1String("--"))) {
        return false;
    }
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    if (equals < 0) {
        return false;
    }
    *name = arg.left(equals);
    *inlineValue = arg.mid(equals + 1);
    return true;
}

} // namespace

std::optional<QString> parseCli(const QStringList &args, CliOptions *out)
{
    CliOptions cli;
    qsizetype i = 1; // 跳过程序名
    while (i < args.size()) {
        const QString &raw = args.at(i);
        QString name = raw;
        std::optional<QString> inlineValue;
        splitInline(raw, &name, &inlineValue);
        ++i;

        const auto takeValue = [&]() -> std::optional<QString> {
            if (inlineValue.has_value()) {
                return inlineValue;
            }
            if (i >= args.size()) {
                return std::nullopt;
            }
            return args.at(i++);
        };

        if (name == QLatin1String("-c") || name == QLatin1String("--config")) {
            const auto value = takeValue();
            if (!value.has_value()) {
                return QStringLiteral("%1 需要一个取值").arg(name);
            }
            cli.config = *value;
        } else if (name == QLatin1String("--check")) {
            cli.check = true;
        } else if (name == QLatin1String("--list")) {
            cli.list = true;
        } else if (name == QLatin1String("--list-keys")) {
            cli.listKeys = true;
        } else if (name == QLatin1String("--quit")) {
            cli.quit = true;
        } else if (name == QLatin1String("--no-autostart")) {
            cli.noAutostart = true;
        } else if (name == QLatin1String("--remove-autostart")) {
            cli.removeAutostart = true;
        } else if (name == QLatin1String("--log-level")) {
            const auto value = takeValue();
            if (!value.has_value()) {
                return QStringLiteral("%1 需要一个取值").arg(name);
            }
            cli.logLevel = *value;
        } else if (name == QLatin1String("--log-file")) {
            const auto value = takeValue();
            if (!value.has_value()) {
                return QStringLiteral("%1 需要一个取值").arg(name);
            }
            cli.logFile = *value;
        } else if (name == QLatin1String("--parent-pid")) {
            const auto value = takeValue();
            if (!value.has_value()) {
                return QStringLiteral("%1 需要一个取值").arg(name);
            }
            bool ok = false;
            const qint64 pid = value->toLongLong(&ok);
            if (!ok) {
                return QStringLiteral("--parent-pid 需要一个进程号，得到 `%1`").arg(*value);
            }
            // 兼容 oskeyd 的 CLI：本项目不需要它（日志窗口是进程内的），
            // 因此接受但忽略。
            cli.parentPid = pid;
        } else if (name == QLatin1String("--no-color")) {
            cli.noColor = true;
        } else if (name == QLatin1String("--allow-multi")) {
            cli.allowMulti = true;
        } else if (name == QLatin1String("--no-prompt")) {
            cli.noPrompt = true;
        } else if (name == QLatin1String("--no-elevate")) {
            cli.noElevate = true;
        } else if (name == QLatin1String("--console")) {
            cli.console = true;
        } else if (name == QLatin1String("--log-window")) {
            cli.logWindow = true;
        } else if (name == QLatin1String("--elevated")) {
            cli.elevated = true;
        } else if (name == QLatin1String("-h") || name == QLatin1String("--help")) {
            cli.showHelp = true;
        } else if (name == QLatin1String("-V") || name == QLatin1String("--version")) {
            cli.showVersion = true;
        } else {
            return QStringLiteral("无法识别的参数 `%1`").arg(name);
        }
    }
    *out = cli;
    return std::nullopt;
}

QString helpText()
{
    return QStringLiteral(R"(flowkeyd %1 — 由 Lua 配置驱动的键盘钩子守护进程

用法:
    flowkeyd [选项]

选项:
    -c, --config <PATH>     配置文件（默认：%USERPROFILE%\.config\flowkeyd\config.lua，
                            找不到时依次回退到 exe 同目录、%APPDATA%\flowkeyd
                            和当前目录下的 config.lua）
        --no-elevate        不自动提权，直接以当前权限运行
        --console           保留控制台输出
        --elevated          内部标记：已经提权，不要再重启自己
        --check             校验配置并退出
        --list              打印已配置的快捷键并退出
        --list-keys         打印所有可接受的按键名并退出
        --quit              请正在运行的实例干净退出（按配置文件路径匹配，
                            最多等 10 秒；没找到在跑的实例时返回 1）
        --no-autostart      不要注册 / 刷新「登录时以最高权限启动」的计划任务
                            （开发实例与 --allow-multi / --no-elevate 本来就跳过）
        --remove-autostart  删除那个计划任务后退出（需要管理员权限）
        --log-window        启动时直接打开日志窗口
        --parent-pid <PID>  兼容参数，本项目忽略
        --log-level <LVL>   trace|debug|info|warn|error|off
        --log-file <PATH>   同时把日志追加写入文件
        --no-color          关闭 ANSI 颜色
        --allow-multi       跳过单实例检查
        --no-prompt         不弹交互提示：已在运行的提示与是否注册开机自启的询问
                            都跳过，按默认处理（脚本 / 自动化用）
    -h, --help              显示本帮助
    -V, --version           显示版本

配置:
    配置文件是一段真正的 Lua 脚本：用 settings{...}、hotkey{...}、remap{...}
    注册，或者 return { settings = ..., hotkeys = ..., remaps = ... }。
    flowkeyd.lua.example 是有文档的完整参考。注意 `repeat` 是 Lua 关键字，
    重复参数要写成 `repeatable`；Windows 路径用长字符串 [[C:\tools\app.exe]]。

示例:
    flowkeyd --config C:\tools\config.lua
    flowkeyd --check
    flowkeyd --quit
    flowkeyd --no-elevate --allow-multi --console
)")
        .arg(core::projectVersion());
}

QString versionText(const QString &buildVersion)
{
    // 第一行是「项目版本号 + 构建时间戳」，第二行是链接进来的 Lua 版本
    // （见 AGENTS.md 第 8 节：出问题时能一眼看出是哪一份 Lua）。
    return QStringLiteral("flowkeyd %1\n%2\n").arg(buildVersion, QLatin1String(LUA_RELEASE));
}

QStringList commandLineArguments()
{
    QStringList args;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.append(QString::fromWCharArray(argv[i]));
    }
    LocalFree(argv);
    return args;
}

} // namespace flowkeyd::app
