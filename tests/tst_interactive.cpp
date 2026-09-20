// 需要真实桌面的交互式测试：剪贴板、Core Audio、窗口后端。
//
// 它们会碰这台机器的真实剪贴板 / 音量 / 前台窗口，所以**默认跳过**，
// 必须显式开启（与 oskeyd 的 `OSKEYD_ALLOW_INTERACTIVE_TESTS` 同一套约定）：
//
//   $env:FLOWKEYD_ALLOW_INTERACTIVE_TESTS = "1"
//   .\build\windows-debug\tst_interactive.exe
//
// ctest 不会设置这个变量，因此 CI / 日常 `ctest` 里它们只是 skip。
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include "core/action.h"
#include "core/keys.h"
#include "platform/win/audio.h"
#include "platform/win/clipboard.h"
#include "platform/win/desktop.h"
#include "platform/win/dwm.h"
#include "platform/win/ffi.h"
#include "platform/win/input.h"
#include "platform/win/power.h"
#include "platform/win/process.h"
#include "platform/win/window.h"

using namespace flowkeyd;

namespace {

bool interactiveEnabled()
{
    return !qEnvironmentVariable("FLOWKEYD_ALLOW_INTERACTIVE_TESTS").isEmpty();
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
    void powerScreenOffBlanksTheDisplay();
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

    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Activate, false, &detail, &error),
             qPrintable(error));
    QVERIFY2(platform::win::window::isActive(hwnd), "activate did not bring the window to the foreground");

    // `animate = false` 走的就是 `TransitionGuard`：这里显式验证 dwmapi 解析成功
    // 且“设 TRUE → 设回 FALSE”这条往返能走通（属性读不回来，所以只能这么查）。
    if (platform::win::dwm::available()) {
        QVERIFY2(platform::win::dwm::forceDisableTransitions(hwnd, true, &error), qPrintable(error));
        QVERIFY2(platform::win::dwm::forceDisableTransitions(hwnd, false, &error), qPrintable(error));
    }

    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Minimize, false, &detail, &error),
             qPrintable(error));
    QVERIFY(!platform::win::window::isActive(hwnd));
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Restore, false, &detail, &error),
             qPrintable(error));

    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Close, false, &detail, &error),
             qPrintable(error));
    bool gone = false;
    for (int i = 0; i < 120 && !gone; ++i) {
        gone = IsWindow(hwnd) == 0;
        if (!gone) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(gone, "the Notepad window did not close");
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

/// 关屏：会真的把屏幕黑掉，所以比其它交互式测试多一道闸门。
///
/// 屏幕是会话级的，一条 `WM_SYSCOMMAND`/`SC_MONITORPOWER` 广播就把全部显示器
/// 送进待机；随后注入一个无害的 Shift 把它点亮（任何输入都会唤醒）。
/// 睡眠 / 关机 / 重启 / 注销 / 锁定**绝不**在这里调用。
void TestInteractive::powerScreenOffBlanksTheDisplay()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    if (qEnvironmentVariable("FLOWKEYD_ALLOW_SCREEN_OFF").isEmpty()) {
        QSKIP("set FLOWKEYD_ALLOW_SCREEN_OFF=1 to blank the screen for a moment");
    }
    QString detail;
    QString error;
    QVERIFY2(platform::win::power::execute(core::PowerOp::ScreenOff, &detail, &error),
             qPrintable(error));
    QCOMPARE(detail, QStringLiteral("display(s) off"));
    // 让屏幕真的黑一下，然后自己点亮。
    QThread::msleep(800);
    QVERIFY2(platform::win::tapKey(core::vk::LSHIFT, &error), qPrintable(error));
    QThread::msleep(300);
}

QTEST_MAIN(TestInteractive)
#include "tst_interactive.moc"
