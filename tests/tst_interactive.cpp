// 需要真实桌面的交互式测试：剪贴板、Core Audio、窗口后端、虚拟桌面。
//
// 它们会碰这台机器的真实剪贴板 / 音量 / 前台窗口，所以**默认跳过**，
// 必须显式开启（与 oskeyd 的 `OSKEYD_ALLOW_INTERACTIVE_TESTS` 同一套约定）：
//
//   $env:FLOWKEYD_ALLOW_INTERACTIVE_TESTS = "1"
//   .\build\windows-release\tst_interactive.exe
//
// ctest 不会设置这个变量，因此 CI / 日常 `ctest` 里它们只是 skip。
//
// **电源动作一个都不许在这里执行**（AGENTS.md 工作约定第 10 条）：
// 关机 / 重启 / 注销 / 睡眠 / 休眠 / 锁定 / 关屏都不进自动化测试，
// 只能由用户自己按键或点选单来验证。
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include "core/action.h"
#include "core/keys.h"
#include "core/placement.h"
#include "platform/win/audio.h"
#include "platform/win/clipboard.h"
#include "platform/win/desktop.h"
#include "platform/win/dwm.h"
#include "platform/win/ffi.h"
#include "platform/win/input.h"
#include "platform/win/monitor.h"
#include "platform/win/process.h"
#include "platform/win/window.h"

using namespace flowkeyd;

namespace {

bool interactiveEnabled()
{
    return !qEnvironmentVariable("FLOWKEYD_ALLOW_INTERACTIVE_TESTS").isEmpty();
}

/// 测试自己的一个普通顶层窗口。
///
/// “窗口后端”的那些断言本来都拿 `notepad.exe` 当载体，但本机的记事本已经变成
/// **单实例、带标签页与会话恢复**的 Store 应用（`Microsoft.WindowsNotepad` 11.26xx）：
/// `notepad file` 有时只是给一个既有窗口加一个标签页（标题会随手切来切去，`find`
/// 可能拿到别人的窗口），而 `WM_CLOSE` 会因为别的标签页 / 恢复的会话弹确认框，
/// 于是“关闭之后窗口消失”永远等不到结果。**同一个失败在修复前的提交上一样能复现**
/// （用 `git worktree` 建 `ee38ac6` 跑过，见 AGENTS 第 10 节），所以不是产品回归。
///
/// 于是这里的窗口自己建：标题可控、线程/进程独占、`WM_CLOSE` 就是销毁自己。
/// 记事本仍然用来覆盖“启动一个真程序 + 按标题找到它的窗口”那条路径。
class TestWindow
{
public:
    explicit TestWindow(const QString &title);
    ~TestWindow();
    TestWindow(const TestWindow &) = delete;
    TestWindow &operator=(const TestWindow &) = delete;

    HWND hwnd() const { return m_hwnd; }
    /// 只抽这个窗口自己的消息（`WM_CLOSE` 靠消息队列投递）。
    /// 不碰 Qt 自己的消息，免得把事件循环的水搅浑。
    void pump();

private:
    HWND m_hwnd = nullptr;
};

LRESULT CALLBACK testWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void registerTestWindowClass()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = testWindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"FlowkeydInteractiveTestWindow";
    RegisterClassExW(&cls);
    registered = true;
}

TestWindow::TestWindow(const QString &title)
{
    registerTestWindowClass();
    m_hwnd = CreateWindowExW(0, L"FlowkeydInteractiveTestWindow",
                             reinterpret_cast<const wchar_t *>(title.utf16()),
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (m_hwnd != nullptr) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }
}

TestWindow::~TestWindow()
{
    if (m_hwnd != nullptr && IsWindow(m_hwnd) != 0) {
        DestroyWindow(m_hwnd);
    }
}

void TestWindow::pump()
{
    MSG message{};
    while (PeekMessageW(&message, m_hwnd, 0, 0, PM_REMOVE) != 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

} // namespace

class TestInteractive : public QObject
{
    Q_OBJECT

private slots:
    void clipboardRoundTrip();
    void volumeReadWriteAndRestore();
    void windowBackendLaunchesActivatesAndCloses();
    void copySelectionCopiesTheFocusedSelection();
    void desktopBackendProbesAndSwitches();
    void moveWindowToAnotherDesktopAndBack();
    void activatesAWindowThatIsOnAnotherDesktop();
    void placementMovesAWindowToAnotherMonitor();
    void pinsAWindowToAllDesktops();
    void topmostIsAppliedAndCleared();
};

void TestInteractive::clipboardRoundTrip()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QString error;
    QString saved;
    QVERIFY2(platform::win::clipboard::getText(&saved, &error), qPrintable(error));

    const QString marker = QStringLiteral("flowkeyd-clip-%1").arg(QCoreApplication::applicationPid());
    QVERIFY2(platform::win::clipboard::setText(marker, &error), qPrintable(error));
    QString got;
    QVERIFY2(platform::win::clipboard::getText(&got, &error), qPrintable(error));
    QCOMPARE(got, marker);

    QVERIFY2(platform::win::clipboard::appendText(QStringLiteral("-tail"), &error), qPrintable(error));
    QVERIFY2(platform::win::clipboard::getText(&got, &error), qPrintable(error));
    QCOMPARE(got, marker + QStringLiteral("-tail"));

    QVERIFY2(platform::win::clipboard::clear(&error), qPrintable(error));
    QVERIFY2(platform::win::clipboard::getText(&got, &error), qPrintable(error));
    QCOMPARE(got, QString());

    // 尽量把用户原来的文本放回去。
    if (!saved.isEmpty()) {
        QVERIFY2(platform::win::clipboard::setText(saved, &error), qPrintable(error));
    }
}

void TestInteractive::volumeReadWriteAndRestore()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QString error;
    int before = 0;
    if (!platform::win::audio::getPercent(&before, &error)) {
        QSKIP("no default audio endpoint on this machine");
    }
    bool muted = false;
    const bool haveMute = platform::win::audio::isMuted(&muted, &error);

    const int target = before > 50 ? 40 : 60;
    QString detail;
    QVERIFY2(platform::win::audio::apply(core::VolumeOp::Set, target, std::nullopt, &detail, &error),
             qPrintable(error));
    QCOMPARE(detail, QStringLiteral("%1%").arg(target));
    int now = 0;
    QVERIFY2(platform::win::audio::getPercent(&now, &error), qPrintable(error));
    QCOMPARE(now, target);

    // 步进 + 钳位：从 0 再往下不会变成负数。
    QVERIFY2(platform::win::audio::apply(core::VolumeOp::Set, 0, std::nullopt, &detail, &error),
             qPrintable(error));
    QVERIFY2(platform::win::audio::apply(core::VolumeOp::Down, std::nullopt, 10, &detail, &error),
             qPrintable(error));
    QCOMPARE(detail, QStringLiteral("0%"));

    // 恢复原值（以及原来的静音状态）。
    QVERIFY2(platform::win::audio::apply(core::VolumeOp::Set, before, std::nullopt, &detail, &error),
             qPrintable(error));
    if (haveMute) {
        QVERIFY2(platform::win::audio::apply(muted ? core::VolumeOp::Mute : core::VolumeOp::Unmute,
                                             std::nullopt, std::nullopt, &detail, &error),
                 qPrintable(error));
    }
}

void TestInteractive::windowBackendLaunchesActivatesAndCloses()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    // 用一个唯一的文件名当窗口标题，这样绝不会误伤用户自己开着的记事本。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString marker =
        QStringLiteral("flowkeyd-interactive-%1").arg(QCoreApplication::applicationPid());
    const QString file = dir.filePath(marker + QStringLiteral(".txt"));
    {
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write("hello\n");
    }

    platform::win::RunCommandSpec spec;
    spec.program = QStringLiteral("notepad.exe");
    spec.args << QDir::toNativeSeparators(file);
    spec.show = core::ShowMode::Normal;
    QString detail;
    QString error;
    QVERIFY2(platform::win::runCommand(spec, &detail, &error), qPrintable(error));

    const core::WindowQuery query =
        core::WindowQuery::make(std::optional<QString>(marker), std::nullopt);
    HWND hwnd = nullptr;
    for (int i = 0; i < 120 && hwnd == nullptr; ++i) {
        hwnd = platform::win::window::find(query);
        if (hwnd == nullptr) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(hwnd != nullptr, "the launched Notepad window never appeared");

    // 记事本只负责盖“启动一个真程序 + 按标题找到它的窗口”（`runCommand` + `find`）；
    // 后面的前台/最小化/关闭断言全部挪到测试自己的窗口上（见 `TestWindow` 的注释）：
    // 本机的记事本是单实例、带标签页与会话恢复的 Store 应用，窗口会在多次启动之间
    // 被复用、标题会随手切、后台窗口也未必真能拿到前台。
    // 这里只尽量把它关掉当作清理，**不**把它当成断言。
    (void)platform::win::window::applyTo(hwnd, core::WindowOp::Close, false, &detail, &error);

    TestWindow own(marker + QStringLiteral("-backend"));
    HWND backend = own.hwnd();
    QVERIFY2(backend != nullptr, "the test window was not created");

    QVERIFY2(platform::win::window::applyTo(backend, core::WindowOp::Activate, false, &detail,
                                            &error),
             qPrintable(error));
    {
        // “已经激活”还要看虚拟桌面（见 `window::isActive`），出错时把三个子条件
        // 都打出来，否则以后只能看着一句“没有拿到前台”猜。
        const std::optional<bool> onCurrent =
            platform::win::desktop::isWindowOnCurrentDesktop(backend, &error);
        QVERIFY2(
            platform::win::window::isActive(backend),
            qPrintable(QStringLiteral("activate did not bring the window to the foreground "
                                      "(foreground=%1 iconic=%2 onCurrentDesktop=%3 %4, hwnd=%5)")
                           .arg(GetForegroundWindow() == backend ? QStringLiteral("yes")
                                                                : QStringLiteral("no"),
                                IsIconic(backend) != 0 ? QStringLiteral("yes")
                                                       : QStringLiteral("no"),
                                onCurrent.has_value()
                                    ? (*onCurrent ? QStringLiteral("yes") : QStringLiteral("no"))
                                    : QStringLiteral("unknown"),
                                error)
                           .arg(reinterpret_cast<quintptr>(backend))));
    }

    // `animate = false` 走的就是 `TransitionGuard`：这里显式验证 dwmapi 解析成功
    // 且“设 TRUE → 设回 FALSE”这条往返能走通（属性读不回来，所以只能这么查）。
    if (platform::win::dwm::available()) {
        QVERIFY2(platform::win::dwm::forceDisableTransitions(backend, true, &error),
                 qPrintable(error));
        QVERIFY2(platform::win::dwm::forceDisableTransitions(backend, false, &error),
                 qPrintable(error));
    }

    QVERIFY2(platform::win::window::applyTo(backend, core::WindowOp::Minimize, false, &detail,
                                           &error),
             qPrintable(error));
    QVERIFY(!platform::win::window::isActive(backend));
    QVERIFY2(platform::win::window::applyTo(backend, core::WindowOp::Restore, false, &detail,
                                           &error),
             qPrintable(error));

    QVERIFY2(platform::win::window::applyTo(backend, core::WindowOp::Close, false, &detail, &error),
             qPrintable(error));
    bool gone = false;
    for (int i = 0; i < 120 && !gone; ++i) {
        own.pump();
        gone = IsWindow(backend) == 0;
        if (!gone) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(gone, "the test window did not close");
}

void TestInteractive::copySelectionCopiesTheFocusedSelection()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    // `{selection}` 的核心机制：合成一次 Ctrl+C，然后读剪贴板。
    // 用一个唯一的临时文件，避免误伤用户自己的记事本。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString marker =
        QStringLiteral("flowkeyd-sel-%1").arg(QCoreApplication::applicationPid());
    const QString file = dir.filePath(marker + QStringLiteral(".txt"));
    {
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write("SELECTME-12345\n");
    }

    platform::win::RunCommandSpec spec;
    spec.program = QStringLiteral("notepad.exe");
    spec.args << QDir::toNativeSeparators(file);
    spec.show = core::ShowMode::Normal;
    QString detail;
    QString error;
    QVERIFY2(platform::win::runCommand(spec, &detail, &error), qPrintable(error));

    const core::WindowQuery query =
        core::WindowQuery::make(std::optional<QString>(marker), std::nullopt);
    HWND hwnd = nullptr;
    for (int i = 0; i < 120 && hwnd == nullptr; ++i) {
        hwnd = platform::win::window::find(query);
        if (hwnd == nullptr) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(hwnd != nullptr, "the launched Notepad window never appeared");
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Activate, false, &detail, &error),
             qPrintable(error));
    QThread::msleep(300);

    // Ctrl+A 全选。
    const std::vector<core::SendOp> selectAll{
        core::SendOp::keyDown(core::vk::LCONTROL),
        core::SendOp::keyDown(static_cast<core::Vk>('A')),
        core::SendOp::keyUp(static_cast<core::Vk>('A')),
        core::SendOp::keyUp(core::vk::LCONTROL),
    };
    QVERIFY2(platform::win::sendOps(selectAll, false, &error), qPrintable(error));
    QThread::msleep(150);

    QVERIFY2(platform::win::copySelection(300, &error), qPrintable(error));
    QString copied;
    QVERIFY2(platform::win::clipboard::getText(&copied, &error), qPrintable(error));
    QVERIFY2(copied.contains(QStringLiteral("SELECTME-12345")),
             qPrintable(QStringLiteral("clipboard after copy_selection was %1").arg(copied)));

    (void)platform::win::window::applyTo(hwnd, core::WindowOp::Close, false, &detail, &error);
}

/// 虚拟桌面后端：只读探测 + 一次可逆的切换（切走再切回来）。
///
/// 切换桌面会短暂打断用户，但它是可逆且非破坏性的，所以放在这套 opt-in 的
/// 交互式测试里（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）。
void TestInteractive::desktopBackendProbesAndSwitches()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QString error;
    platform::win::desktop::Snapshot snapshot;
    QVERIFY2(platform::win::desktop::probe(&snapshot, &error), qPrintable(error));
    QVERIFY(snapshot.count >= 1);
    QVERIFY(snapshot.current >= 1);
    QVERIFY(snapshot.current <= snapshot.count);
    QVERIFY(snapshot.osBuild > 1000);
    QVERIFY(!snapshot.layout.isEmpty());
    QVERIFY(!snapshot.managerIid.isEmpty());
    qInfo().noquote() << "desktop probe:" << "count" << snapshot.count << "current"
                      << snapshot.current << "os"
                      << QStringLiteral("%1.%2").arg(snapshot.osBuild).arg(snapshot.osRevision)
                      << "api" << snapshot.apiBuild << "layout" << snapshot.layout << "manager"
                      << snapshot.managerIid;

    if (snapshot.count < 2) {
        QSKIP("only one virtual desktop exists; nothing to switch to");
    }

    const std::uint32_t other = snapshot.current == 1 ? 2 : 1;
    QString detail;
    QVERIFY2(platform::win::desktop::switchTo(other, &detail, &error), qPrintable(error));
    QVERIFY(!detail.isEmpty());

    // shell 的切换是异步的，轮询等它落到目标桌面。
    platform::win::desktop::Snapshot moved;
    bool landed = false;
    for (int i = 0; i < 60 && !landed; ++i) {
        QVERIFY2(platform::win::desktop::probe(&moved, &error), qPrintable(error));
        landed = moved.current == other;
        if (!landed) {
            QThread::msleep(50);
        }
    }
    QCOMPARE(moved.current, other);

    // 切回原来的桌面，别把用户留在别处。
    QVERIFY2(platform::win::desktop::switchTo(snapshot.current, &detail, &error), qPrintable(error));
}

/// `window_rule` 的虚拟桌面部分：把一个真实窗口移到另一个桌面，再移回来。
///
/// 用**已公开**的 `IsWindowOnCurrentVirtualDesktop` 从外面确认它真的走了 ——
/// `MoveViewToDesktop` 是未公开接口，只有这个才能证明 vtable 下标没选错。
void TestInteractive::moveWindowToAnotherDesktopAndBack()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QString error;
    platform::win::desktop::Snapshot snapshot;
    QVERIFY2(platform::win::desktop::probe(&snapshot, &error), qPrintable(error));
    if (snapshot.count < 2) {
        QSKIP("only one virtual desktop exists; nothing to move a window to");
    }

    const QString marker =
        QStringLiteral("flowkeyd-place-%1").arg(QCoreApplication::applicationPid());

    // 用测试自己的窗口：见 `TestWindow` 的注释（不再拿记事本当载体）。
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");
    QString detail;

    const auto before = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
    QVERIFY2(before.has_value(), qPrintable(error));
    QVERIFY2(*before, "the freshly launched window is not on the current desktop");
    const auto beforeId = platform::win::desktop::windowDesktopId(hwnd, &error);
    QVERIFY2(beforeId.has_value(), qPrintable(error));

    const std::uint32_t other = snapshot.current == 1 ? 2 : 1;
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, other, &detail, &error),
             qPrintable(error));
    QVERIFY(!detail.isEmpty());

    // 未公开的 `MoveViewToDesktop` 返回 S_OK 也可能什么都没发生：用已公开的
    // `GetWindowDesktopId` 确认桌面的 GUID 真的换了。
    std::optional<QString> afterId;
    for (int i = 0; i < 60; ++i) {
        afterId = platform::win::desktop::windowDesktopId(hwnd, &error);
        QVERIFY2(afterId.has_value(), qPrintable(error));
        if (*afterId != *beforeId) {
            break;
        }
        QThread::msleep(50);
    }
    QVERIFY2(afterId.has_value() && *afterId != *beforeId,
             "the window's desktop id did not change after moveWindowToDesktop");

    bool left = false;
    for (int i = 0; i < 60 && !left; ++i) {
        const auto onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(onCurrent.has_value(), qPrintable(error));
        left = !*onCurrent;
        if (!left) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(left, "the window did not leave the current desktop");

    // 移回当前桌面：别给用户留下一个跑到别的桌面上的窗口。
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, snapshot.current, &detail, &error),
             qPrintable(error));
    bool back = false;
    for (int i = 0; i < 60 && !back; ++i) {
        const auto onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(onCurrent.has_value(), qPrintable(error));
        back = *onCurrent;
        if (!back) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(back, "the window did not come back to the current desktop");

    // 移回来之后 GUID 也应该回到原值。
    const auto backId = platform::win::desktop::windowDesktopId(hwnd, &error);
    QVERIFY2(backId.has_value(), qPrintable(error));
    QCOMPARE(*backId, *beforeId);
}

/// 跨桌面“唤醒”：窗口被搬到别的虚拟桌面之后，`window::isActive` 不能再被 shell
/// 记着的“前台窗口”骗到，`window::raiseWindow` 必须能把视图切回去并让它拿到前台。
///
/// 这就是用户报的那个 bug：`window_rule` 把刚启动的 WPS 摆到第 3 个桌面，而 shell
/// 仍然把那个窗口当前台窗口（实测），于是下一次按 `Win+3` 反而把它*收起*了 ——
/// 用户看到的是一个既不在眼前、也没被激活的窗口。
void TestInteractive::activatesAWindowThatIsOnAnotherDesktop()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QString error;
    platform::win::desktop::Snapshot snapshot;
    QVERIFY2(platform::win::desktop::probe(&snapshot, &error), qPrintable(error));
    if (snapshot.count < 2) {
        QSKIP("only one virtual desktop exists; nothing to move a window to");
    }

    const QString marker =
        QStringLiteral("flowkeyd-desktop-%1").arg(QCoreApplication::applicationPid());

    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");
    QString detail;

    // 先让它拿到前台（模拟“用户刚启动它”），再搬到别的桌面 —— 也就是
    // `window_rule` 在那个窗口刚出现时干的事。
    QVERIFY2(platform::win::window::raiseWindow(hwnd), "could not activate the fresh window");
    const std::uint32_t other = snapshot.current == 1 ? 2 : 1;
    bool changed = false;
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, other, &detail, &error, &changed),
             qPrintable(error));
    QVERIFY2(changed, "moveWindowToDesktop did not report a real desktop change");

    // 把视图明确留在原来那张桌面：把**前台**窗口搬到别的桌面时 Windows 有时候会把
    // 视图一起带过去，而这里要构造的是用户报的那个现场 —— 窗口在别的桌面上，视图
    // 却还留在这儿（`window_rule` 搬走刚启动的 WPS 之后就是这样）。
    QVERIFY2(platform::win::desktop::switchTo(snapshot.current, &detail, &error),
             qPrintable(error));

    bool left = false;
    for (int i = 0; i < 60 && !left; ++i) {
        const auto onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(onCurrent.has_value(), qPrintable(error));
        left = !*onCurrent;
        if (!left) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(left, "the window did not leave the current desktop");

    // 关键断言：它在别的桌面上，所以它**不是**“用户正在用的那个窗口”。
    // （搬前台窗口时 shell 常常还把它当前台窗口，本机实测就是这样。）
    qInfo().noquote() << "window on another desktop: shellForegroundIsIt"
                      << (GetForegroundWindow() == hwnd);
    QVERIFY2(!platform::win::window::isActive(hwnd),
             "a window on another virtual desktop must not count as active");

    // 把视图切回去并重新激活它。
    QVERIFY2(platform::win::window::raiseWindow(hwnd), "raiseWindow failed across desktops");
    bool backOnCurrent = false;
    for (int i = 0; i < 60 && !backOnCurrent; ++i) {
        const auto onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(onCurrent.has_value(), qPrintable(error));
        backOnCurrent = *onCurrent;
        if (!backOnCurrent) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(backOnCurrent, "the view did not switch back to the desktop of the window");
    QVERIFY2(GetForegroundWindow() == hwnd, "the window did not get the foreground");
    QVERIFY2(platform::win::window::isActive(hwnd), "isActive is false right after raiseWindow");

    // 已经在当前桌面上时，`switchToWindowDesktop` 是空操作。
    QVERIFY2(platform::win::desktop::switchToWindowDesktop(hwnd, &detail, &error),
             qPrintable(error));
    QVERIFY2(detail.startsWith(QStringLiteral("already on ")), qPrintable(detail));

    // 移回原来的桌面，并把视图也带回去（别把用户留在别的桌面上）。
    bool movedAgain = true;
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, snapshot.current, &detail, &error,
                                                        &movedAgain),
             qPrintable(error));
    QVERIFY2(platform::win::desktop::switchTo(snapshot.current, &detail, &error), qPrintable(error));

    // 搬到它已经在的那张桌面不是“搬迁” —— `window_rule` 靠这个布尔量决定要不要
    // 让视图跟着走，所以这里必须为假。
    bool changedBack = true;
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, snapshot.current, &detail, &error,
                                                        &changedBack),
             qPrintable(error));
    QVERIFY2(!changedBack, "moving a window to the desktop it already is on reported a change");
}

/// `window_rule` 的显示器部分：把窗口摆到另一块显示器并最大化，再还原。
void TestInteractive::placementMovesAWindowToAnotherMonitor()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    const std::vector<core::MonitorDescription> monitors =
        core::sortedMonitors(platform::win::monitor::list());
    if (monitors.size() < 2) {
        QSKIP("only one monitor is connected; nothing to move a window to");
    }

    const QString marker =
        QStringLiteral("flowkeyd-mon-%1").arg(QCoreApplication::applicationPid());

    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");
    QString detail;
    QString error;

    // 挑一块和窗口当前所在不同的显示器。
    const auto currentIndex = platform::win::monitor::indexForWindow(monitors, hwnd);
    QVERIFY(currentIndex.has_value());
    const std::size_t targetIndex = *currentIndex == 0 ? monitors.size() - 1 : 0;
    const core::MonitorDescription &target = monitors[targetIndex];

    const core::Rect current =
        platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 800, 600});
    const core::Rect maximized = core::placementRect(current, target, true, std::nullopt,
                                                     std::nullopt, std::nullopt, std::nullopt);
    QCOMPARE(maximized.width, target.work.width);
    QVERIFY2(platform::win::monitor::applyPlacement(hwnd, maximized, true, &error),
             qPrintable(error));

    bool placed = false;
    for (int i = 0; i < 60 && !placed; ++i) {
        const auto index = platform::win::monitor::indexForWindow(monitors, hwnd);
        placed = index.has_value() && *index == targetIndex && IsZoomed(hwnd) != 0;
        if (!placed) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(placed, "the window did not end up maximized on the target monitor");

    // 还原成普通窗口：仍应在目标显示器上，但不再是最大化。
    const core::Rect restored{target.work.x + 40, target.work.y + 40, 800, 600};
    QVERIFY2(platform::win::monitor::applyPlacement(hwnd, restored, false, &error),
             qPrintable(error));
    bool normal = false;
    for (int i = 0; i < 60 && !normal; ++i) {
        const auto index = platform::win::monitor::indexForWindow(monitors, hwnd);
        normal = index.has_value() && *index == targetIndex && IsZoomed(hwnd) == 0;
        if (!normal) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(normal, "the window did not go back to a normal window on the target monitor");
}

/// `window_rule` 的 `all_desktops`：走未公开的 `IVirtualDesktopPinnedApps`
/// （`PinView` / `UnpinView` / `IsViewPinned`）。
///
/// `PinView` 返回 S_OK 不代表真的钉上了，所以这里用 `IsViewPinned` 从外面确认
/// 状态真的变了 —— 这也是唯一能证明那个 vtable 下标（6/7/8）没选错的办法。
void TestInteractive::pinsAWindowToAllDesktops()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    const QString marker =
        QStringLiteral("flowkeyd-pin-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    QString error;
    const auto before = platform::win::desktop::isWindowPinned(hwnd, &error);
    if (!before.has_value()) {
        QSKIP(qPrintable(QStringLiteral("IVirtualDesktopPinnedApps is unavailable: %1")
                             .arg(error)));
    }
    QVERIFY2(!*before, "the fresh test window is already pinned to all desktops");

    QString detail;
    bool changed = false;
    QVERIFY2(platform::win::desktop::setWindowPinned(hwnd, true, &detail, &error, &changed),
             qPrintable(error));
    QCOMPARE(detail, QStringLiteral("all desktops"));
    QVERIFY2(changed, "pinning a fresh window should report a real change");

    bool pinned = false;
    for (int i = 0; i < 60 && !pinned; ++i) {
        const auto now = platform::win::desktop::isWindowPinned(hwnd, &error);
        QVERIFY2(now.has_value(), qPrintable(error));
        pinned = *now;
        if (!pinned) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(pinned, "the window did not become pinned after PinView");

    // 幂等：已经是目标状态时不再调 `PinView`，`changed` 为假。
    changed = true;
    QVERIFY2(platform::win::desktop::setWindowPinned(hwnd, true, &detail, &error, &changed),
             qPrintable(error));
    QVERIFY(!changed);
    QCOMPARE(detail, QStringLiteral("already on all desktops"));

    // 取消钉住：别给用户留下一个到处都显示的窗口。
    changed = false;
    QVERIFY2(platform::win::desktop::setWindowPinned(hwnd, false, &detail, &error, &changed),
             qPrintable(error));
    QVERIFY2(changed, "unpinning a pinned window should report a real change");
    QCOMPARE(detail, QStringLiteral("this desktop only"));

    bool unpinned = false;
    for (int i = 0; i < 60 && !unpinned; ++i) {
        const auto now = platform::win::desktop::isWindowPinned(hwnd, &error);
        QVERIFY2(now.has_value(), qPrintable(error));
        unpinned = !*now;
        if (!unpinned) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(unpinned, "the window stayed pinned after UnpinView");
}

/// `window_rule` 的 `topmost`：`WS_EX_TOPMOST` 与 Z 序都要跟着变，而且不能
/// 把窗口激活。用的是已公开的 `SetWindowPos`，风险很低，但值得盯住。
///
/// 这里刻意先做一遍**几何摆放**（跨屏 + 最大化 + 还原）再置顶，因为
/// `placeWindowOnce` 就是这个顺序：先搬桌面 / 摆几何，最后才置顶。
void TestInteractive::topmostIsAppliedAndCleared()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    const QString marker =
        QStringLiteral("flowkeyd-top-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    QString error;

    // 先按 `window_rule` 的顺序摆一次几何（有第二块屏时）。
    const std::vector<core::MonitorDescription> monitors =
        core::sortedMonitors(platform::win::monitor::list());
    if (monitors.size() >= 2) {
        const auto currentIndex = platform::win::monitor::indexForWindow(monitors, hwnd);
        QVERIFY(currentIndex.has_value());
        const std::size_t targetIndex = *currentIndex == 0 ? monitors.size() - 1 : 0;
        const core::MonitorDescription &target = monitors[targetIndex];
        const core::Rect current =
            platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 800, 600});
        const core::Rect maximized = core::placementRect(current, target, true, std::nullopt,
                                                         std::nullopt, std::nullopt, std::nullopt);
        QVERIFY2(platform::win::monitor::applyPlacement(hwnd, maximized, true, &error),
                 qPrintable(error));
        const core::Rect restored{target.work.x + 40, target.work.y + 40, 800, 600};
        QVERIFY2(platform::win::monitor::applyPlacement(hwnd, restored, false, &error),
                 qPrintable(error));
    }

    QVERIFY2((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
             "the fresh test window is already topmost");

    QVERIFY2(platform::win::window::setTopmost(hwnd, true, &error), qPrintable(error));
    QVERIFY2((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
             "setTopmost(true) did not set WS_EX_TOPMOST");

    QVERIFY2(platform::win::window::setTopmost(hwnd, false, &error), qPrintable(error));
    QVERIFY2((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
             "setTopmost(false) did not clear WS_EX_TOPMOST");
}

QTEST_MAIN(TestInteractive)
#include "tst_interactive.moc"
