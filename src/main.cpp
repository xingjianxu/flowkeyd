// flowkeyd —— 由 Lua 配置驱动的键盘钩子守护进程（oskeyd 的 Qt/C++ 复刻版）。
//
// 这里只做「组装」：解析命令行 → 处理离线命令 → 构造 Qt 应用 → 托盘 + 日志窗口。
// 钩子、引擎、动作分发分别在 stage 3 之后接进来。
#include <QApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "app/log_window.h"
#include "cli.h"
#include "core/keys.h"
#include "platform/win/console.h"
#include "platform/win/tray.h"

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
        win::writeStderr(QStringLiteral(
            "flowkeyd: --check and --list need the Lua configuration layer, "
            "which is implemented in stage 2\r\n"));
        return 2;
    }

    QApplication application(argc, argv);

    // 日志窗口是普通窗口；关掉它绝不能退出应用（见 AGENTS.md 第 7 节第 20 条）。
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // FluentWinUI3 必须在加载任何 QML 之前设置。
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QQmlApplicationEngine engine;

    app::LogWindow logWindow(&engine);
    platform::win::Tray tray;

    QObject::connect(&tray, &platform::win::Tray::logWindowRequested, &logWindow, [&logWindow]() {
        logWindow.toggle();
    });
    QObject::connect(&tray, &platform::win::Tray::quitRequested, &application, &QApplication::quit);

    tray.show();
    if (options.logWindow) {
        logWindow.show();
    }

    return QApplication::exec();
}
