// 窗口摆放的持久记忆（`core/window_monitor_state`）的纯逻辑测试：JSON 解析 /
// 序列化的容错、程序身份键的规范化、记忆的写入与查询，以及状态文件的路径。
//
// 真正的读写（`QFile`）在 `app::Dispatcher` 里，不在这里测；`windows.json`
// 与用户无关地放在配置文件旁边，所以路径那条也要钉住。
#include <QtTest>

#include <QDir>
#include <QFileInfo>

#include "core/window_monitor_state.h"

using namespace flowkeyd;

class TestWindowMonitorState : public QObject
{
    Q_OBJECT

private slots:
    void stateFileSitsNextToTheConfig();
    void programKeyIsTheLowercaseExecutableName();
    void missingOrEmptyFileIsAnEmptyState();
    void parseKeepsUsableEntriesAndDropsJunk();
    void parseRejectsMalformedJsonButStillReturnsEmpty();
    void rememberReportsChangesOnce();
    void serializedStateRoundTrips();
};

void TestWindowMonitorState::stateFileSitsNextToTheConfig()
{
    const QString path = QDir::toNativeSeparators(
        core::windowMonitorStatePath(QStringLiteral("C:/Users/me/.config/flowkeyd/config.lua")));
    QCOMPARE(QDir::fromNativeSeparators(path),
             QStringLiteral("C:/Users/me/.config/flowkeyd/windows.json"));

    // 相对路径按当前目录解析（与 `--config` 的行为一致）。
    const QString relative = core::windowMonitorStatePath(QStringLiteral("smoke.lua"));
    QCOMPARE(QFileInfo(relative).fileName(), QStringLiteral("windows.json"));
    QVERIFY(QFileInfo(relative).isAbsolute());
}

void TestWindowMonitorState::programKeyIsTheLowercaseExecutableName()
{
    QCOMPARE(core::windowMonitorKey(QStringLiteral("Code.exe")), QStringLiteral("code.exe"));
    // `EnumWindows` 给的 `szExeFile` 与配置里手写的 `process` 大小写并不一致。
    QCOMPARE(core::windowMonitorKey(QStringLiteral("  Chrome.EXE  ")),
             QStringLiteral("chrome.exe"));
    // 拿不到进程名：记不了，调用方应当跳过。
    QCOMPARE(core::windowMonitorKey(std::nullopt), QString());
}

void TestWindowMonitorState::missingOrEmptyFileIsAnEmptyState()
{
    QString error = QStringLiteral("unset");
    const core::WindowMonitorState empty = core::parseWindowMonitorState(QByteArray(), &error);
    QVERIFY(empty.monitors.isEmpty());
    // 空文件 = 还没有记忆（第一次用 `"last"`），不是错误。
    QVERIFY(error.isEmpty());

    const core::WindowMonitorState blank =
        core::parseWindowMonitorState(QByteArrayLiteral("\n  \n"));
    QVERIFY(blank.monitors.isEmpty());
}

void TestWindowMonitorState::parseKeepsUsableEntriesAndDropsJunk()
{
    const QByteArray json = R"({
      "version": 1,
      "monitors": {
        "Code.exe": "\\\\.\\DISPLAY2",
        "chrome.exe": "  \\\\.\\DISPLAY1  ",
        "empty.exe": "",
        "": "\\\\.\\DISPLAY3",
        "number.exe": 7
      },
      "somethingElse": true
    })";
    const core::WindowMonitorState state = core::parseWindowMonitorState(json);
    // 键统一成小写、值去掉首尾空白；空键 / 空值 / 非字符串条目丢掉。
    QCOMPARE(state.monitors.size(), 2);
    QCOMPARE(state.monitors.value(QStringLiteral("code.exe")),
             QStringLiteral("\\\\.\\DISPLAY2"));
    QCOMPARE(state.monitors.value(QStringLiteral("chrome.exe")),
             QStringLiteral("\\\\.\\DISPLAY1"));
    QVERIFY(!state.monitors.contains(QStringLiteral("empty.exe")));
    QVERIFY(!state.monitors.contains(QStringLiteral("number.exe")));

    // `monitors` 不是对象时当作空的（并给一句说明）。
    QString error;
    const core::WindowMonitorState wrong =
        core::parseWindowMonitorState(QByteArrayLiteral(R"({"monitors": [1, 2]})"), &error);
    QVERIFY(wrong.monitors.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestWindowMonitorState::parseRejectsMalformedJsonButStillReturnsEmpty()
{
    QString error;
    const core::WindowMonitorState state =
        core::parseWindowMonitorState(QByteArrayLiteral("{ not json"), &error);
    QVERIFY(state.monitors.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("JSON")), qPrintable(error));
    // 调用方复用同一个 `QString` 时不该拿到上一次的旧错误。
    QVERIFY(core::parseWindowMonitorState(QByteArrayLiteral("{}"), &error).monitors.isEmpty());
    QVERIFY(error.isEmpty());
}

void TestWindowMonitorState::rememberReportsChangesOnce()
{
    core::WindowMonitorState state;
    // 第一次写：变了。
    QVERIFY(core::rememberWindowMonitor(&state, QStringLiteral("Code.exe"),
                                       QStringLiteral("\\\\.\\DISPLAY2")));
    QCOMPARE(state.monitors.value(QStringLiteral("code.exe")),
             QStringLiteral("\\\\.\\DISPLAY2"));
    // 同一个值再写一次：没变（调用方因此可以省掉一次落盘）。
    QVERIFY(!core::rememberWindowMonitor(&state, QStringLiteral("code.exe"),
                                         QStringLiteral("\\\\.\\DISPLAY2")));
    // 换了显示器：变了。
    QVERIFY(core::rememberWindowMonitor(&state, QStringLiteral("code.exe"),
                                        QStringLiteral("\\\\.\\DISPLAY1")));
    QCOMPARE(state.monitors.value(QStringLiteral("code.exe")),
             QStringLiteral("\\\\.\\DISPLAY1"));
    // 空键 / 空值都记不了。
    QVERIFY(!core::rememberWindowMonitor(&state, QString(), QStringLiteral("\\\\.\\DISPLAY1")));
    QVERIFY(!core::rememberWindowMonitor(&state, QStringLiteral("code.exe"), QString()));

    // 查询：键不区分大小写、空白会被去掉；没有记忆时返回 `nullopt`。
    QCOMPARE(core::rememberedWindowMonitor(state, QStringLiteral(" CODE.EXE ")),
             std::optional<QString>(QStringLiteral("\\\\.\\DISPLAY1")));
    QCOMPARE(core::rememberedWindowMonitor(state, QStringLiteral("wezterm.exe")), std::nullopt);
    QCOMPARE(core::rememberedWindowMonitor(state, QString()), std::nullopt);
}

void TestWindowMonitorState::serializedStateRoundTrips()
{
    core::WindowMonitorState state;
    core::rememberWindowMonitor(&state, QStringLiteral("code.exe"),
                                QStringLiteral("\\\\.\\DISPLAY2"));
    core::rememberWindowMonitor(&state, QStringLiteral("chrome.exe"),
                                QStringLiteral("\\\\.\\DISPLAY1"));
    const QByteArray json = core::serializeWindowMonitorState(state);
    QVERIFY(json.contains("\"monitors\""));
    QVERIFY(json.contains("code.exe"));
    // 多次序列化同一份状态得到同样的字节（这样“没变就不落盘”才有意义）。
    QCOMPARE(json, core::serializeWindowMonitorState(state));

    const core::WindowMonitorState parsed = core::parseWindowMonitorState(json);
    QCOMPARE(parsed.monitors, state.monitors);
}

QTEST_MAIN(TestWindowMonitorState)
#include "tst_window_monitor_state.moc"
