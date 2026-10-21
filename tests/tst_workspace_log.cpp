// 工作空间**恢复日志**（`core/workspace_log`）的纯逻辑测试。
//
// 这个文件是 M8 的安全兜底：进程被杀掉之后靠它把藏起来的窗口放回来。所以这里钉的
// 是「坏掉的时候也不能丢窗口」那一面：坏 JSON、坏条目、`HWND` / `FILETIME` 这种
// 64 位整数不能因为 JSON 只有 double 就丢精度、重复条目去重、移除只移除一条。
//
// 真正碰 Win32 的部分（`stillSame()` 校验、`ShowWindow`、写盘）在
// `platform/win/workspace` + `tst_interactive` 里。
#include <QtTest>

#include <QDir>
#include <QFileInfo>

#include "core/workspace_log.h"

using namespace flowkeyd;

namespace {

core::HiddenWindow makeEntry(core::WindowId window)
{
    core::HiddenWindow entry;
    entry.window = window;
    entry.identity.valid = true;
    entry.identity.pid = 4242;
    entry.identity.processStarted = 133123456789012345ULL;
    entry.identity.className = QStringLiteral("Notepad");
    entry.workspace.desktop = QStringLiteral("{11111111-aaaa}");
    entry.workspace.monitor = QStringLiteral("\\\\.\\DISPLAY1");
    entry.workspace.index = 3;
    entry.title = QStringLiteral("untitled - Notepad");
    return entry;
}

} // namespace

class TestWorkspaceLog : public QObject
{
    Q_OBJECT

private slots:
    void logFileSitsNextToTheConfig();
    void missingOrEmptyLogIsEmpty();
    void parseToleratesBrokenInput();
    void parseKeepsEveryField();
    void parseDropsDuplicateAndEmptyEntries();
    void serializedLogRoundTrips();
    void addReplacesAndRemoveDropsOneEntry();
};

void TestWorkspaceLog::logFileSitsNextToTheConfig()
{
    const QString path = core::workspaceLogPath(
        QStringLiteral("C:/Users/me/.config/flowkeyd/config.lua"));
    QCOMPARE(QDir::fromNativeSeparators(path),
             QStringLiteral("C:/Users/me/.config/flowkeyd/workspace.json"));

    // 相对路径按当前目录解析（与 `--config` 的行为一致）。
    const QString relative = core::workspaceLogPath(QStringLiteral("smoke.lua"));
    QCOMPARE(QFileInfo(relative).fileName(), QStringLiteral("workspace.json"));
    QVERIFY(QFileInfo(relative).isAbsolute());
}

void TestWorkspaceLog::missingOrEmptyLogIsEmpty()
{
    QString error = QStringLiteral("unset");
    const core::WorkspaceLog empty = core::parseWorkspaceLog(QByteArray(), &error);
    QVERIFY(empty.isEmpty());
    // 空文件 = 没有窗口被藏着，不是错误。
    QVERIFY(error.isEmpty());

    QVERIFY(core::parseWorkspaceLog(QByteArrayLiteral("\n  \n")).isEmpty());
    // 合法 JSON 但没有 `windows`：同样是“没有窗口被藏着”。
    QVERIFY(core::parseWorkspaceLog(QByteArrayLiteral(R"({"version": 1})")).isEmpty());
}

void TestWorkspaceLog::parseToleratesBrokenInput()
{
    // 坏 JSON：报一句原因，返回空日志（调用方会当成“没有窗口被藏着”继续）。
    QString error;
    QVERIFY(core::parseWorkspaceLog(QByteArrayLiteral("{ this is not json"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 顶层不是对象 / `windows` 不是数组。
    error.clear();
    QVERIFY(core::parseWorkspaceLog(QByteArrayLiteral("[1, 2]"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(core::parseWorkspaceLog(QByteArrayLiteral(R"({"windows": "nope"})"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // `windows` 是数组、但每一条都坏掉：整体不报错，一条都不留。
    const core::WorkspaceLog junk = core::parseWorkspaceLog(
        QByteArrayLiteral(R"({"windows": [1, "x", {}, {"hwnd": 0}, {"hwnd": "0"}, {"hwnd": true}]})"));
    QVERIFY(junk.isEmpty());
}

void TestWorkspaceLog::parseKeepsEveryField()
{
    // `hwnd` 与 `process_started` 都超过 2^53：写成字符串才不会在 JSON 里丢精度。
    const QByteArray json = R"({
      "version": 1,
      "windows": [
        {
          "hwnd": "9223372036854775807",
          "pid": 4242,
          "process_started": "133123456789012345",
          "class": "Notepad",
          "desktop": "{11111111-aaaa}",
          "monitor": "\\\\.\\DISPLAY1",
          "space": 3,
          "title": "untitled - Notepad"
        }
      ]
    })";
    const core::WorkspaceLog log = core::parseWorkspaceLog(json);
    QCOMPARE(log.windows.size(), static_cast<std::size_t>(1));
    const core::HiddenWindow &entry = log.windows.front();
    QCOMPARE(entry.window, static_cast<core::WindowId>(9223372036854775807ULL));
    QVERIFY(entry.identity.valid);
    QCOMPARE(entry.identity.pid, static_cast<std::uint32_t>(4242));
    QCOMPARE(entry.identity.processStarted, static_cast<std::uint64_t>(133123456789012345ULL));
    QCOMPARE(entry.identity.className, QStringLiteral("Notepad"));
    QCOMPARE(entry.workspace.desktop, QStringLiteral("{11111111-aaaa}"));
    QCOMPARE(entry.workspace.monitor, QStringLiteral("\\\\.\\DISPLAY1"));
    QCOMPARE(entry.workspace.index, static_cast<std::uint32_t>(3));
    QCOMPARE(entry.title, QStringLiteral("untitled - Notepad"));

    // 身份缺一半（没有类名）时标成“没读到”：恢复时不做校验，照样把窗口放回来。
    const core::WorkspaceLog noClass =
        core::parseWorkspaceLog(QByteArrayLiteral(R"({"windows": [{"hwnd": "77", "pid": 5}]})"));
    QCOMPARE(noClass.windows.size(), static_cast<std::size_t>(1));
    QVERIFY(!noClass.windows.front().identity.valid);
    QCOMPARE(noClass.windows.front().workspace.index, static_cast<std::uint32_t>(1));
}

void TestWorkspaceLog::parseDropsDuplicateAndEmptyEntries()
{
    const QByteArray json = R"({"windows": [
      {"hwnd": "100", "class": "A", "pid": 1, "title": "first"},
      {"hwnd": "100", "class": "B", "pid": 1, "title": "second"},
      {"hwnd": "200", "class": "C", "pid": 2}
    ]})";
    const core::WorkspaceLog log = core::parseWorkspaceLog(json);
    // 同一个句柄只留一条（后写的覆盖前面的），没有重复。
    QCOMPARE(log.windows.size(), static_cast<std::size_t>(2));
    QCOMPARE(log.windows.front().window, static_cast<core::WindowId>(100));
    QCOMPARE(log.windows.front().identity.className, QStringLiteral("B"));
    QCOMPARE(log.windows.front().title, QStringLiteral("second"));
}

void TestWorkspaceLog::serializedLogRoundTrips()
{
    core::WorkspaceLog log;
    log.windows.push_back(makeEntry(0x7FF600000001ULL));
    log.windows.push_back(makeEntry(static_cast<core::WindowId>(18446744073709551615ULL)));

    const QByteArray json = core::serializeWorkspaceLog(log);
    // 人能看懂（缩进过的、带版本号），而不是一行压缩的。
    QVERIFY(json.contains('\n'));
    QVERIFY(json.contains("version"));
    // 64 位整数写成字符串（`QJsonValue` 的数字是 `double`，超过 2^53 会丢精度）。
    QVERIFY(json.contains(QString::number(0x7FF600000001ULL).toUtf8()));
    QVERIFY(json.contains(QString::number(18446744073709551615ULL).toUtf8()));

    const core::WorkspaceLog parsed = core::parseWorkspaceLog(json);
    QCOMPARE(parsed.windows, log.windows);

    // 空日志也写得出来（删文件之前的那一条路）。
    const QByteArray emptyJson = core::serializeWorkspaceLog(core::WorkspaceLog{});
    QVERIFY(core::parseWorkspaceLog(emptyJson).isEmpty());
}

void TestWorkspaceLog::addReplacesAndRemoveDropsOneEntry()
{
    core::WorkspaceLog log;
    core::addHiddenWindow(log, makeEntry(11));
    core::addHiddenWindow(log, makeEntry(22));
    QCOMPARE(log.windows.size(), static_cast<std::size_t>(2));
    QVERIFY(core::hasHiddenWindow(log, 11));
    QVERIFY(core::hasHiddenWindow(log, 22));

    // 同一个窗口再来一条：替换而不是追加。
    core::HiddenWindow replacement = makeEntry(11);
    replacement.title = QStringLiteral("second time");
    core::addHiddenWindow(log, replacement);
    QCOMPARE(log.windows.size(), static_cast<std::size_t>(2));
    QCOMPARE(log.windows.front().title, QStringLiteral("second time"));

    // 句柄 0 是无效身份（`nullptr` / 已销毁），不收。
    core::addHiddenWindow(log, makeEntry(0));
    QCOMPARE(log.windows.size(), static_cast<std::size_t>(2));

    QVERIFY(core::removeHiddenWindow(log, 11));
    QVERIFY(!core::removeHiddenWindow(log, 11));
    QVERIFY(!core::hasHiddenWindow(log, 11));
    QVERIFY(core::hasHiddenWindow(log, 22));
    QCOMPARE(log.windows.size(), static_cast<std::size_t>(1));
}

QTEST_MAIN(TestWorkspaceLog)
#include "tst_workspace_log.moc"
