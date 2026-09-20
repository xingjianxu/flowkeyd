// flowkeyd —— 由 Lua 配置驱动的键盘钩子守护进程（oskeyd 的 Qt/C++ 复刻版）。
//
// 这里只做「组装」：解析命令行 → 处理离线命令 → 构造 Qt 应用 → 托盘 + 日志窗口。
// 钩子、引擎、动作分发分别在 stage 3 之后接进来。
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "app/log_window.h"
#include "app/runtime.h"
#include "cli.h"
#include "core/config.h"
#include "core/keys.h"
#include "lua/lua_config.h"
#include "platform/win/console.h"
#include "platform/win/elevate.h"
#include "platform/win/input.h"
#include "platform/win/logging.h"
#include "platform/win/single_instance.h"
#include "platform/win/tray.h"

#include <memory>
#include <optional>

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

/// 与 oskeyd 的 `print_bindings` 逐字形似：形状一致，`--list` 的输出可以对比。
QString renderBindings(const core::Compiled &compiled)
{
    QString out;
    out += QStringLiteral("%1 hotkey(s), %2 remap(s) from %3\n")
               .arg(compiled.bindings.size())
               .arg(compiled.remaps.size())
               .arg(compiled.source);
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
    for (const QString &warning : compiled.warnings) {
        out += QStringLiteral("  warning: %1\n").arg(warning);
    }
    return out;
}

} // namespace

int main(int argc, char *argv[])
{
    // 第一件事：把输出接回启动我们的终端（GUI 子系统进程默认没有控制台）。
    win::attachParentConsole();

    // 用 GetCommandLineW 取参数，避免窄字符代码页把非 ASCII 的路径打乱。
    const QStringList args = app::commandLineArguments();

    app::CliOptions options;
    if (const auto error = app::parseCli(args, &options); error.has_value()) {
        win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n\n").arg(*error)));
        win::writeStderr(toConsole(app::helpText()));
        return 2;
    }
    if (options.showHelp) {
        win::writeStdout(toConsole(app::helpText()));
        return 0;
    }
    if (options.showVersion) {
        win::writeStdout(toConsole(app::versionText()));
        return 0;
    }
    if (options.listKeys) {
        win::writeStdout(core::allKeyNames().join(QStringLiteral("\r\n")) + QStringLiteral("\r\n"));
        return 0;
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
            win::writeStdout(toConsole(QStringLiteral("%1: OK (%2 hotkey(s), %3 remap(s))\n")
                                           .arg(QDir::toNativeSeparators(path))
                                           .arg(compiled.bindings.size())
                                           .arg(compiled.remaps.size())));
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
            win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n")
                                           .arg(core::ConfigError::makeLegacyToml(
                                                    *legacy, core::preferredConfigPath())
                                                    .toString())));
            return 1;
        }
    }
    core::Compiled compiled;
    if (const auto error = core::loadConfig(lua::makeLuaEvaluator(), configPath, &compiled);
        error.has_value()) {
        win::writeStderr(toConsole(QStringLiteral("flowkeyd: %1\n").arg(error->toString())));
        return 1;
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

    win::logInfo(QStringLiteral("flowkeyd %1 starting").arg(QLatin1String(FLOWKEYD_VERSION)));
    win::logInfo(QStringLiteral("configuration: %1").arg(QDir::toNativeSeparators(configPath)));
    win::logInfo(QStringLiteral("%1 hotkey(s), %2 remap(s), tick %3 ms")
                     .arg(compiled.bindings.size())
                     .arg(compiled.remaps.size())
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

    // 托盘模式下控制台属于自己时隐藏它（日志窗口就是控制界面）。
    if (!options.console && win::consoleIsOwned() == 1 && win::hideConsoleWindow()) {
        win::logInfo(QStringLiteral("console window hidden; use the tray menu to control flowkeyd"));
    }

    QApplication application(argc, argv);

    // 日志窗口是普通窗口；关掉它绝不能退出应用（见 AGENTS.md 第 7 节第 20 条）。
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // FluentWinUI3 必须在加载任何 QML 之前设置。
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QQmlApplicationEngine engine;

    app::LogWindow logWindow(&engine);
    platform::win::Tray tray;

    app::Runtime runtime;
    auto compiledPointer = std::make_shared<core::Compiled>(std::move(compiled));
    QString startError;
    if (!runtime.start(configPath, compiledPointer, &startError)) {
        win::logError(startError);
        return 1;
    }

    QObject::connect(&tray, &platform::win::Tray::logWindowRequested, &logWindow, [&logWindow]() {
        logWindow.toggle();
    });
    QObject::connect(&tray, &platform::win::Tray::quitRequested, &runtime,
                     &app::Runtime::requestShutdownFromAnyThread);
    QObject::connect(&runtime, &app::Runtime::notificationRequested, &tray,
                     &platform::win::Tray::showMessage);
    QObject::connect(&runtime, &app::Runtime::suspendedChanged, &tray,
                     &platform::win::Tray::setSuspended);
    QObject::connect(&runtime, &app::Runtime::finished, &application, &QApplication::quit);

    tray.show();
    if (options.logWindow) {
        logWindow.show();
    }

    const int code = QApplication::exec();
    runtime.shutdown();
    win::shutdownLogging();
    return code;
}
