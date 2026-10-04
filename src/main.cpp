// flowkeyd —— 由 Lua 配置驱动的键盘钩子守护进程。
//
// 这里只做「组装」：解析命令行 → 处理离线命令 → 构造 Qt 应用 → 托盘 + 日志窗口。
// 钩子、引擎、动作分发分别在 stage 3 之后接进来。
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

#include "app/app_icon.h"
#include "app/log_window.h"
#include "app/popup_host.h"
#include "app/runtime.h"
#include "app/updater.h"
#include "cli.h"
#include "core/config.h"
#include "core/keys.h"
#include "core/version.h"
#include "lua/lua_config.h"
#include "platform/win/autostart.h"
#include "platform/win/console.h"
#include "platform/win/elevate.h"
#include "platform/win/ffi.h"
#include "platform/win/hook.h"
#include "platform/win/input.h"
#include "platform/win/logging.h"
#include "platform/win/process.h"
#include "platform/win/single_instance.h"
#include "platform/win/tray.h"
#include "platform/win/update.h"

#include <memory>
#include <optional>
#include <string>

using namespace flowkeyd;

namespace win = flowkeyd::platform::win;

namespace {

QString toConsole(const QString &text)
{
    QString out = text;
    out.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    out.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    return out;
}

/// `--list` 里的 `press = ...` / `release = ...` 一栏。
QString describeActions(const std::vector<core::Action> &actions)
{
    if (actions.empty()) {
        return QStringLiteral("-");
    }
    QStringList parts;
    parts.reserve(static_cast<int>(actions.size()));
    for (const core::Action &action : actions) {
        parts.append(action.summary());
    }
    return parts.join(QStringLiteral(" ; "));
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

/// `--list` 的输出形状固定：脚本可以直接对它的文本做断言。
QString renderBindings(const core::Compiled &compiled)
{
    QString out;
    out += QStringLiteral("%1 hotkey(s), %2 remap(s), %3 window rule(s) from %4\n")
               .arg(compiled.bindings.size())
               .arg(compiled.remaps.size())
               .arg(compiled.windowRules.size())
               .arg(compiled.source);
    // 远程桌面放行是一项全局行为，却由单条绑定上的 `remote_desktop = true` 决定
    // 例外，所以在这里把两边一起列出来。
    if (compiled.settings.remoteDesktop) {
        const QStringList &processes = compiled.settings.remoteDesktopProcesses;
        QStringList keep;
        for (const core::Binding &binding : compiled.bindings) {
            if (!binding.remoteDesktop) {
                continue;
            }
            QStringList chords;
            for (const core::Chord &chord : binding.chords) {
                chords.append(chord.render());
            }
            keep.append(chords.join(QStringLiteral(", ")));
        }
        for (const core::CompiledRemap &remap : compiled.remaps) {
            if (remap.remoteDesktop) {
                keep.append(remap.from.render());
            }
        }
        out += QStringLiteral("  remote desktop: pass through in front of %1; keep working: %2\n")
                   .arg(processes.isEmpty()
                            ? QStringLiteral("(no process configured)")
                            : processes.join(QStringLiteral(", ")),
                        keep.isEmpty() ? QStringLiteral("-") : keep.join(QStringLiteral("; ")));
    }
    for (const core::Binding &binding : compiled.bindings) {
        QStringList chords;
        for (const core::Chord &chord : binding.chords) {
            chords.append(chord.render());
        }
        out += QStringLiteral("  %1 swallow=%2 trigger=%3 press=%4 release=%5\n")
                   .arg(chords.join(QStringLiteral(", ")).leftJustified(24),
                        boolText(binding.swallow).leftJustified(5),
                        core::triggerModeName(binding.trigger).leftJustified(8),
                        describeActions(binding.press),
                        describeActions(binding.release));
        if (binding.comment.has_value()) {
            out += QStringLiteral("      # %1\n").arg(*binding.comment);
        }
    }
    for (const core::CompiledRemap &remap : compiled.remaps) {
        out += QStringLiteral("  %1 remap `%2` -> %3 press op(s) / %4 release op(s)\n")
                   .arg(remap.from.render().leftJustified(24), remap.name)
                   .arg(remap.press.size())
                   .arg(remap.release.size());
    }
    for (const core::WindowRule &rule : compiled.windowRules) {
        out += QStringLiteral("  window rule `%1`: %2\n").arg(rule.name, rule.summary());
    }
    for (const QString &warning : compiled.warnings) {
        out += QStringLiteral("  warning: %1\n").arg(warning);
    }
    return out;
}

/// 守护进程模式下配置读不出来 / 校验不过时的收尾：把错误写进 stderr（终端里
/// 看得见），再弹一个 Qt 标准消息框。双击启动时进程没有控制台，只有弹窗才能
/// 让用户知道到底出了什么事。
///
/// **离线命令（`--check` / `--list`）不走这里**：它们照旧只打印、不弹窗、不提权
/// （不变量 11）。
int reportConfigFailure(int argc, char *argv[], const QString &message)
{
    win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n").arg(message)));
    QApplication application(argc, argv);
    // 消息框的标题栏也用应用图标，不然它会在任务栏里光秃秃的。
    QApplication::setWindowIcon(app::applicationIcon());
    QMessageBox box(QMessageBox::Critical,
                    QStringLiteral("flowkeyd 配置错误"),
                    QStringLiteral("flowkeyd 无法启动：配置文件有错误。\n\n%1").arg(message),
                    QMessageBox::Ok);
    // 错误文本可以选中复制，便于用户拿去搜索或反馈。
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
    return 1;
}

/// 「同一个配置文件已经有一个实例在跑」的提示框。
///
/// 用原生的 `MessageBoxW`，因此**不需要 Qt 应用对象** —— 这个提示必须在
/// **提权之前**弹出来（双重启不该白弹一次 UAC，也不该动到那个实例）。
/// 用户点“确定”之后调用方直接退出。
void promptAlreadyRunning(const QString &configPath)
{
    const QString text =
        QStringLiteral("flowkeyd 已经有一个实例在运行，本次启动将退出。\n\n"
                       "配置文件：%1\n\n"
                       "继续使用正在运行的那个实例即可；确实需要重启，"
                       "请先用 --quit 把它停掉。")
            .arg(QDir::toNativeSeparators(configPath));
    const std::wstring title = QStringLiteral("flowkeyd 已在运行").toStdWString();
    const std::wstring body = text.toStdWString();
    MessageBoxW(nullptr,
                body.c_str(),
                title.c_str(),
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

/// 询问用户是否注册 / 刷新开机自启的计划任务。返回 true 表示同意。
///
/// 同样用原生 `MessageBoxW`（这一段跑在 `QApplication` 构造之前）。
bool confirmAutostartAction(win::AutostartState state,
                            const QString &currentCommand,
                            const QString &executable)
{
    const QString nativeExe = QDir::toNativeSeparators(executable);
    QString text;
    if (state == win::AutostartState::Absent) {
        text = QStringLiteral("还没有设置开机自启。\n\n"
                              "是否注册一个计划任务，在每次登录时以最高权限"
                              "自动启动 flowkeyd？（提权启动不会弹 UAC）\n\n"
                              "程序：%1")
                   .arg(nativeExe);
    } else {
        text = QStringLiteral("开机自启的计划任务当前指向另一个程序：\n\n"
                              "现在：%1\n"
                              "本次：%2\n\n"
                              "是否把它更新为本次运行的这个程序？")
                   .arg(QDir::toNativeSeparators(currentCommand), nativeExe);
    }
    const std::wstring title = QStringLiteral("flowkeyd 开机自启").toStdWString();
    const std::wstring body = text.toStdWString();
    return MessageBoxW(nullptr,
                       body.c_str(),
                       title.c_str(),
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1 | MB_SETFOREGROUND
                           | MB_TOPMOST)
        == IDYES;
}

} // namespace

int main(int argc, char *argv[])
{
    // Windows 上用 FreeType 字体引擎替换系统那个 DirectWrite 后端：它渲染出来的
    // 字形更清晰，能消掉 Qt Quick 界面里的发虚/发糊（本机 225% 缩放下尤其明显）。
    // 字体引擎是平台插件的**构造参数**，平台插件一被加载就定死了，所以必须在
    // 任何 `QApplication` / `QGuiApplication` 之前设好。放在 `main()` 的最前面
    // 还有一个好处：`reportConfigFailure()` 会自己建一个 `QApplication`（配置
    // 错误弹窗），那条路也盖住了。
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
#endif

    // 第一件事：把输出接回启动我们的终端（GUI 子系统进程默认没有控制台）。
    win::attachParentConsole();

    // 用 GetCommandLineW 取参数，避免窄字符代码页把非 ASCII 的路径打乱。
    const QStringList args = app::commandLineArguments();

    // 构建版本号 = 本次构建的时间戳（取运行中这个 exe 的最后写入时间）。
    // `--help`、`--version`、启动日志与托盘菜单都用它，见 core/version.h。
    const QString buildVersion = core::buildVersion(win::currentExecutablePath());

    app::CliOptions options;
    if (const auto error = app::parseCli(args, &options); error.has_value()) {
        win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n\n").arg(*error)));
        win::writeStderr(toConsole(app::helpText(buildVersion)));
        return 2;
    }

    // 在线更新重启新实例时要原样转发的命令行参数：去掉程序名，以及 `--updated-from`
    // （那一个由 `applyExecutableUpdate()` 按这次的新版本号自己加）。
    QStringList restartArgs;
    restartArgs.reserve(args.size());
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--updated-from")) {
            ++i; // 连取值一起跳过
            continue;
        }
        if (arg.startsWith(QLatin1String("--updated-from="))) {
            continue;
        }
        restartArgs.append(arg);
    }
    if (options.showHelp) {
        win::writeStdout(toConsole(app::helpText(buildVersion)));
        return 0;
    }
    if (options.showVersion) {
        win::writeStdout(toConsole(app::versionText(buildVersion)));
        return 0;
    }
    if (options.listKeys) {
        win::writeStdout(core::allKeyNames().join(QStringLiteral("\r\n")) + QStringLiteral("\r\n"));
        return 0;
    }
    // `--remove-autostart`：删掉「登录自启」计划任务。它不读配置、不装钩子，
    // 但要管理员权限才能真正删任务（所以不在「离线命令」那一类）。
    // 和 `--quit` 一起用时先删任务、再请实例退出，否则它下次启动会把任务注册回来。
    if (options.removeAutostart) {
        QString error;
        if (!win::removeAutostartTask(win::autostartTaskName(), &error)) {
            win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n").arg(error)));
            return 1;
        }
        win::writeStdout(toConsole(
            QStringLiteral("flowkeyd: logon autostart task `%1` removed\n")
                .arg(win::autostartTaskName())));
        if (!options.quit) {
            return 0;
        }
    }
    // `--quit`：请正在运行的实例干净退出。它自己不装钩子、不提权，也不读配置
    // （只看配置文件路径对不对得上）；等对方真的把钩子卸掉再返回，脚本才能接着
    // 替换 exe（见 AGENTS.md 第 10 节）。
    if (options.quit) {
        const QString path = options.config.value_or(core::defaultConfigPath());
        const QString key = win::instanceKey(path);
        const QString shown = QDir::toNativeSeparators(path);
        bool running = false;
        QString quitError;
        if (!win::requestQuit(key, &running, &quitError)) {
            win::writeStderr(toConsole(QStringLiteral("flowkeyd: could not ask %1 to quit: %2\n")
                                           .arg(shown, quitError)));
            return 1;
        }
        if (!running) {
            win::writeStderr(toConsole(
                QStringLiteral("flowkeyd: no running instance for %1\n").arg(shown)));
            return 1;
        }
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (!win::quitEventExists(key)) {
                win::writeStdout(
                    toConsole(QStringLiteral("flowkeyd: %1 exited\n").arg(shown)));
                return 0;
            }
            Sleep(100);
        }
        win::writeStderr(toConsole(
            QStringLiteral("flowkeyd: %1 is still running 10 s after the quit request\n")
                .arg(shown)));
        return 1;
    }
    // 离线命令到此为止：**绝不允许**提权，也绝不安装钩子（不变量 11）。
    if (options.check || options.list) {
        const bool usingDefault = !options.config.has_value();
        const QString path = options.config.value_or(core::defaultConfigPath());
        // 首选位置没配置、但旧版 TOML 还在：先说清楚该搬到哪儿。
        if (usingDefault && !QFileInfo::exists(path)) {
            if (const auto legacy = core::staleTomlConfig(); legacy.has_value()) {
                win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n")
                                               .arg(core::ConfigError::makeLegacyToml(
                                                        *legacy, core::preferredConfigPath())
                                                        .toString())));
                return 1;
            }
        }
        core::Compiled compiled;
        if (const auto error = core::loadConfig(lua::makeLuaEvaluator(), path, &compiled);
            error.has_value()) {
            win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n").arg(error->toString())));
            return 1;
        }
        for (const QString &warning : compiled.warnings) {
            win::writeStderr(toConsole(QStringLiteral("flowkeyd: warning: %1\n").arg(warning)));
        }
        if (usingDefault && path != core::preferredConfigPath()) {
            win::writeStderr(toConsole(
                QStringLiteral("flowkeyd: warning: using the fallback config location %1; the "
                               "default is %2\n")
                    .arg(QDir::toNativeSeparators(path),
                         QDir::toNativeSeparators(core::preferredConfigPath()))));
        }
        if (options.check) {
            QString summary = QStringLiteral("%1: OK (%2 hotkey(s), %3 remap(s)")
                                  .arg(QDir::toNativeSeparators(path))
                                  .arg(compiled.bindings.size())
                                  .arg(compiled.remaps.size());
            if (!compiled.windowRules.empty()) {
                summary += QStringLiteral(", %1 window rule(s)").arg(compiled.windowRules.size());
            }
            summary += QStringLiteral(")\n");
            win::writeStdout(toConsole(summary));
            return 0;
        }
        win::writeStdout(toConsole(renderBindings(compiled)));
        return 0;
    }

    // ---------------------------------------------------------------------
    // 守护进程模式
    // ---------------------------------------------------------------------
    // 先把配置读出来：日志级别、提权开关、单实例开关都看它。
    const bool usingDefault = !options.config.has_value();
    const QString configPath = options.config.value_or(core::defaultConfigPath());
    if (usingDefault && !QFileInfo::exists(configPath)) {
        if (const auto legacy = core::staleTomlConfig(); legacy.has_value()) {
            return reportConfigFailure(
                argc,
                argv,
                core::ConfigError::makeLegacyToml(*legacy, core::preferredConfigPath()).toString());
        }
    }
    core::Compiled compiled;
    if (const auto error = core::loadConfig(lua::makeLuaEvaluator(), configPath, &compiled);
        error.has_value()) {
        return reportConfigFailure(argc, argv, error->toString());
    }

    // 日志：配置文件配了级别，但命令行优先。
    const QString levelName = options.logLevel.value_or(compiled.settings.logLevel);
    const auto level = win::parseLogLevel(levelName);
    if (!level.has_value()) {
        win::writeStderr(toConsole(QStringLiteral("flowkeyd: unknown log level `%1`\n").arg(levelName)));
        return 2;
    }
    const QString logPath = options.logFile.value_or(core::preferredLogPath());
    QString logError;
    if (!win::initLogging(*level, true, std::optional<QString>(logPath), &logError)) {
        // 默认日志文件只是便利设施；用户显式给出的 --log-file 则必须能用。
        if (options.logFile.has_value()) {
            win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n").arg(logError)));
            return 1;
        }
        win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1 (continuing without a log file)\n")
                                       .arg(logError)));
        win::initLogging(*level, true, std::nullopt, nullptr);
    }
    for (const QString &warning : compiled.warnings) {
        win::logWarn(warning);
    }
    if (usingDefault && configPath != core::preferredConfigPath()) {
        win::logWarn(QStringLiteral("using the fallback config location %1; the default is %2")
                         .arg(QDir::toNativeSeparators(configPath),
                              QDir::toNativeSeparators(core::preferredConfigPath())));
    }

    // 注入后端（校验已在 compile() 里做过，这里只是落地）。
    if (const auto backend = win::parseBackendPreference(compiled.settings.inputBackend);
        backend.has_value()) {
        win::configureInput(*backend);
    } else {
        win::logError(QStringLiteral("settings.input_backend `%1` is not one of auto|user32|ntuser")
                          .arg(compiled.settings.inputBackend));
        return 1;
    }

    // 单实例：**在提权之前**先看一眼有没有同配置的实例在跑。有的话提示用户，
    // 用户确认后直接退出 —— 双重启不该白弹一次 UAC，也不该动到那个实例。
    // （真正的互斥体获取在提权之后仍然保留，用来兜住「两个进程同时启动」的竞态。）
    if (compiled.settings.singleInstance && !options.allowMulti) {
        if (win::instanceRunning(win::instanceKey(configPath))) {
            win::logError(QStringLiteral(
                              "another flowkeyd instance already owns %1 (use --allow-multi to override)")
                              .arg(configPath));
            if (!options.noPrompt) {
                promptAlreadyRunning(configPath);
            }
            return 1;
        }
    }

    // 提权只发生在守护进程模式（不变量 11），且必须能降级。
    const bool wantsElevation = compiled.settings.elevate && !options.noElevate;
    if (wantsElevation && !options.elevated && !win::isElevated()) {
        win::logInfo(QStringLiteral("requesting administrator rights; accept the UAC prompt to continue"));
        QString elevateError;
        if (win::relaunchElevated(&elevateError)) {
            // 新进程接管：单实例锁、钩子与托盘都交给它。
            return 0;
        }
        win::logWarn(QStringLiteral("could not restart with administrator rights (%1); "
                                    "continuing as a normal user")
                         .arg(elevateError));
    } else if (!wantsElevation && !win::isElevated()) {
        win::logDebug(QStringLiteral("elevation disabled (--no-elevate or settings.elevate = false)"));
    }

    // 单实例：提权重启时父进程可能还握着互斥体，让出几秒等它交棒。
    std::optional<win::SingleInstance> instance;
    if (compiled.settings.singleInstance && !options.allowMulti) {
        const QString key = win::instanceKey(configPath);
        const int attempts = options.elevated ? 20 : 1;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            bool alreadyRunning = false;
            QString instanceError;
            auto acquired = win::SingleInstance::acquire(key, &alreadyRunning, &instanceError);
            if (!acquired.has_value()) {
                win::logError(instanceError);
                return 1;
            }
            if (!alreadyRunning) {
                instance = std::move(*acquired);
                break;
            }
            if (attempt + 1 == attempts) {
                win::logError(QStringLiteral(
                                  "another flowkeyd instance already owns %1 (use --allow-multi to override)")
                                  .arg(configPath));
                return 1;
            }
            Sleep(150);
        }
    }

    win::logInfo(QStringLiteral("flowkeyd %1 starting").arg(buildVersion));
    win::logInfo(QStringLiteral("configuration: %1").arg(QDir::toNativeSeparators(configPath)));
    win::logInfo(QStringLiteral("%1 hotkey(s), %2 remap(s), %3 window rule(s), tick %4 ms")
                     .arg(compiled.bindings.size())
                     .arg(compiled.remaps.size())
                     .arg(compiled.windowRules.size())
                     .arg(compiled.settings.tickMs));
    win::logInfo(QStringLiteral("key injection: %1, logging at %2")
                     .arg(win::inputBackendName(),
                          win::logLevelName(win::currentLogLevel()).toLower()));
    if (win::isElevated()) {
        win::logInfo(QStringLiteral("running elevated: actions can drive windows of elevated processes"));
    } else if (compiled.settings.elevate) {
        win::logInfo(QStringLiteral("not elevated: hotkeys work, but actions cannot drive windows of "
                                    "elevated processes"));
    } else {
        win::logInfo(QStringLiteral("not elevated (elevation is off): actions cannot drive windows of "
                                    "elevated processes"));
    }
    win::logInfo(QStringLiteral("swallow default %1, exact_modifiers %2, release_modifiers %3")
                     .arg(boolText(compiled.settings.swallow),
                          boolText(compiled.settings.exactModifiers),
                          boolText(compiled.settings.releaseModifiers)));

    // 开机自启：每次启动都检查一次计划任务，缺失、或指向的 exe 不是当前这一个，
    // 就把它注册 / 刷新成当前路径（守护进程跑到哪儿，自启就指向哪儿）。
    //
    // 只在「真正的常驻实例」上做：开发 / 测试实例（`--no-elevate` 或
    // `--allow-multi`）与未提权的进程都跳过，免得把用户真实的开机自启劫持到
    // 构建目录（计划任务指向失效路径是**完全静默**的失败，见 AGENTS.md 第 10 节）。
    if (options.noAutostart) {
        win::logDebug(QStringLiteral("autostart task not managed: --no-autostart"));
    } else if (options.noElevate || options.allowMulti) {
        win::logDebug(QStringLiteral("autostart task not managed: development instance "
                                     "(--no-elevate or --allow-multi)"));
    } else if (!win::isElevated()) {
        win::logDebug(QStringLiteral("autostart task not managed: not running elevated"));
    } else {
        win::AutostartSpec spec;
        spec.executable = win::currentExecutablePath();
        spec.workingDirectory = QFileInfo(spec.executable).absolutePath();
        spec.userId = win::currentUserAccount();
        // 添加 / 修改计划任务要动系统状态，所以先问用户；`--no-prompt` 直接同意。
        win::ensureAutostart(
            spec, [&spec, &options](win::AutostartState state, const QString &currentCommand) {
                if (options.noPrompt) {
                    return true;
                }
                return confirmAutostartAction(state, currentCommand, spec.executable);
            });
    }

    // 托盘模式下控制台属于自己时隐藏它（日志窗口就是控制界面）。
    if (!options.console && win::consoleIsOwned() == 1 && win::hideConsoleWindow()) {
        win::logInfo(QStringLiteral("console window hidden; use the tray menu to control flowkeyd"));
    }

    QApplication application(argc, argv);

    // 应用图标：QML 窗口（日志/选单/帮助）与消息框都跟着它，托盘也用它。
    // exe 在资源管理器/任务栏里显示的那一份是 windres 嵌的资源，见 CMakeLists.txt。
    const QIcon appIcon = app::applicationIcon();
    QApplication::setWindowIcon(appIcon);

    // 日志窗口是普通窗口；关掉它绝不能退出应用（见 AGENTS.md 第 7 节第 20 条）。
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // FluentWinUI3 必须在加载任何 QML 之前设置。
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QQmlApplicationEngine engine;

    app::LogWindow logWindow(&engine, win::logFilePath());
    platform::win::Tray tray(appIcon);

    // `menu` / `help` 的窗口：必须在 GUI 线程上创建，所以放在这里，
    // 由 Runtime 转发（动作跑在工作线程上）。
    app::PopupHost popupHost(&engine);

    // 在线更新：只负责查 GitHub、下载、解压、把新 exe 落到 `<exe>.new`。
    // 实时替换 exe 与重启在事件循环退出之后做（见文件末尾）。
    app::Updater updater(buildVersion, win::currentExecutablePath());
    popupHost.setUpdateModel(updater.model());

    app::Runtime runtime;
    runtime.setPopupHost(&popupHost);
    auto compiledPointer = std::make_shared<core::Compiled>(std::move(compiled));
    QString startError;
    if (!runtime.start(configPath, compiledPointer, &startError)) {
        win::logError(startError);
        return 1;
    }

    QObject::connect(&tray, &platform::win::Tray::logWindowRequested, &logWindow, [&logWindow]() {
        // 再点一次只是把它前置，不重复开、也不关掉（关窗口是窗口自己的叉）。
        logWindow.show();
    });
    QObject::connect(&tray, &platform::win::Tray::suspendToggleRequested, &runtime,
                     [&runtime, &tray]() {
                         const bool target = !runtime.isSuspended();
                         runtime.postControl(target ? win::ControlCmd::Suspend
                                                    : win::ControlCmd::Resume);
                         tray.setSuspended(target);
                         win::logInfo(target ? QStringLiteral("hotkeys suspended from the tray menu")
                                             : QStringLiteral("hotkeys resumed from the tray menu"));
                     });
    QObject::connect(&tray, &platform::win::Tray::reloadRequested, &runtime,
                     &app::Runtime::reloadFromAnyThread);
    QObject::connect(&tray, &platform::win::Tray::openConfigRequested, &runtime, [&runtime]() {
        QString error;
        if (!win::openTarget(QDir::toNativeSeparators(runtime.configPath()), std::nullopt,
                             std::nullopt, core::ShowMode::Normal, &error)) {
            win::logError(QStringLiteral("could not open %1: %2")
                              .arg(runtime.configPath(), error));
        } else {
            win::logInfo(QStringLiteral("opened the configuration file from the tray menu"));
        }
    });
    QObject::connect(&tray, &platform::win::Tray::quitRequested, &runtime,
                     &app::Runtime::requestShutdownFromAnyThread);
    QObject::connect(&runtime, &app::Runtime::notificationRequested, &tray,
                     &platform::win::Tray::showMessage);
    QObject::connect(&runtime, &app::Runtime::suspendedChanged, &tray,
                     &platform::win::Tray::setSuspended);
    // 托盘图标上的数字 = 当前虚拟桌面号（工作线程 500 ms 轮询一次，见 Runtime）。
    // 图标在 GUI 线程上现画（一次改号只画 9 张位图，几百微秒）；读不到桌面时
    // `desktopIcon()` 返回空 QIcon，托盘自己退回应用图标。
    QObject::connect(&runtime, &app::Runtime::desktopChanged, &tray,
                     [&tray](int number, int count) {
                         tray.setDesktop(number, count, app::desktopIcon(number));
                     });
    QObject::connect(&runtime, &app::Runtime::finished, &application, &QApplication::quit);

    // ---- 在线更新（托盘菜单「检查更新」） ----
    // 关掉窗口、中止下载、重试、打开发布页都只是把活儿转交给 `Updater`；
    // 真正联网 / 解压 / 重启都在那边。
    bool restartForUpdate = false;
    QObject::connect(&tray, &platform::win::Tray::checkUpdateRequested, &updater,
                     &app::Updater::checkForUpdates);
    QObject::connect(&updater, &app::Updater::windowRequested, &popupHost,
                     [&popupHost, &updater]() {
                         app::UpdateRequest request;
                         request.onInstall = [&updater]() { updater.startDownload(); };
                         request.onDismiss = [&updater]() { updater.dismiss(); };
                         request.onRetry = [&updater]() { updater.retry(); };
                         request.onOpenRelease = [&updater]() { updater.openReleasePage(); };
                         popupHost.requestUpdate(std::move(request));
                     });
    QObject::connect(&updater, &app::Updater::stagedReady, &runtime,
                     [&runtime, &restartForUpdate]() {
                         // 更新已经下好、校验通过。先把「更新已就绪，正在重启」
                         // 留在屏幕上一小会儿，再走那条干净的退出路径。
                         restartForUpdate = true;
                         QTimer::singleShot(500, &runtime, &app::Runtime::requestShutdownFromAnyThread);
                     });

    tray.setBuildVersion(buildVersion);
    tray.show();

    // 「重启完后通知用户更新成功」：这一条托盘气泡就是 Windows 的通知
    // （`QSystemTrayIcon::showMessage` 在 Win10/11 上进通知中心）。
    if (options.updatedFrom.has_value()) {
        win::logInfo(QStringLiteral("started by the online update (was %1)").arg(*options.updatedFrom));
        tray.showMessage(QStringLiteral("flowkeyd 更新成功"),
                         QStringLiteral("已经更新到 %1（原 %2）。")
                             .arg(buildVersion, *options.updatedFrom));
    }

    // 在线更新把旧 exe 改名成了 `<exe>.old`。那个文件是**上一个进程**的映像，
    // 我们自己（新实例）删得掉，但它可能还在退出过程中，所以隔一会儿重试几次。
    const QString executablePath = win::currentExecutablePath();
    if (QFileInfo::exists(executablePath + QStringLiteral(".old"))) {
        auto attempts = std::make_shared<int>(0);
        auto *cleanupTimer = new QTimer(&application);
        cleanupTimer->setInterval(700);
        QObject::connect(cleanupTimer, &QTimer::timeout, &application,
                         [cleanupTimer, attempts, executablePath]() {
                             ++(*attempts);
                             const bool removed = win::removeExecutableBackup(executablePath);
                             if (!removed && *attempts < 20) {
                                 return;
                             }
                             cleanupTimer->stop();
                             if (removed) {
                                 win::logDebug(QStringLiteral(
                                     "removed the previous executable %1.old")
                                                   .arg(QDir::toNativeSeparators(executablePath)));
                             } else {
                                 win::logWarn(QStringLiteral(
                                     "could not remove %1.old yet; it will be cleaned up next time")
                                                  .arg(QDir::toNativeSeparators(executablePath)));
                             }
                         });
        cleanupTimer->start();
    }
    if (options.logWindow) {
        logWindow.show();
    }

    // 弹窗预热（见 `app::PopupHost::preload`）：事件循环一开始转就排队做掉，把
    // “进程第一次渲染”那 ~170 ms 从“用户第一次按弹窗快捷键”搬到启动时。
    // 排在 `runtime.start()`（钩子已装好、快捷键可用）与托盘之后，所以它不会
    // 拖慢“能用了”的那一刻；预热窗口是全透明且屏幕外的，用户看不到。
    QTimer::singleShot(0, &popupHost, &app::PopupHost::preload);

    const int code = QApplication::exec();
    runtime.shutdown();
    int exitCode = code;
    if (restartForUpdate && updater.hasStagedUpdate()) {
        // 更新已经下好、校验过：现在是唯一会打断运行的一步（改名 + 启动新进程），
        // 所以放在最后做。
        //
        // 单实例锁必须先放掉：新实例启动时会先看一眼「有没有同名实例在跑」，
        // 不放手它会以为还有一个在运行、直接退出。
        instance.reset();
        win::UpdateInstallPlan plan;
        plan.targetExecutable = executablePath;
        plan.stagedExecutable = updater.stagedExecutable();
        plan.restartArgs = restartArgs;
        plan.currentVersion = buildVersion;
        QString updateError;
        if (win::applyExecutableUpdate(plan, &updateError)) {
            win::logInfo(QStringLiteral("restarting as flowkeyd %1; the previous version %2 was "
                                        "renamed to %3.old")
                             .arg(updater.newVersion(), buildVersion,
                                  QDir::toNativeSeparators(executablePath)));
        } else {
            win::logError(QStringLiteral("could not apply the update: %1").arg(updateError));
            // 弹窗提醒用户（旧版本已经恢复回去了，手动重启即可）。
            // `--no-prompt`（脚本 / 自动化）只记日志：一个没人点的模态框会把
            // 进程吊在那里，而它这时已经卸了钩子、什么也做不了。
            if (!options.noPrompt) {
                QMessageBox box(QMessageBox::Warning,
                                QStringLiteral("flowkeyd 更新失败"),
                                QStringLiteral("在线更新没能完成，已经恢复成更新前的版本。\n"
                                               "请手动重新启动 flowkeyd。\n\n%1")
                                    .arg(updateError),
                                QMessageBox::Ok);
                box.setTextInteractionFlags(Qt::TextSelectableByMouse);
                box.exec();
            }
            exitCode = 1;
        }
    }
    win::shutdownLogging();
    return exitCode;
}
