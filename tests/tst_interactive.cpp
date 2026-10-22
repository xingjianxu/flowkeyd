// 需要真实桌面的交互式测试：剪贴板、Core Audio、窗口后端、虚拟桌面。
//
// 它们会碰这台机器的真实剪贴板 / 音量 / 前台窗口，所以**默认跳过**，
// 必须显式开启：
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
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <cstring>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "app/updater.h"
#include "core/action.h"
#include "core/config.h"
#include "core/keys.h"
#include "core/placement.h"
#include "core/workspace_event.h"
#include "core/workspace_log.h"
#include "platform/win/apps.h"
#include "platform/win/audio.h"
#include "platform/win/clipboard.h"
#include "platform/win/desktop.h"
#include "platform/win/dwm.h"
#include "platform/win/elevate.h"
#include "platform/win/ffi.h"
#include "platform/win/freeze.h"
#include "platform/win/hook.h"
#include "platform/win/ime.h"
#include "platform/win/input.h"
#include "platform/win/monitor.h"
#include "platform/win/process.h"
#include "platform/win/window.h"
#include "platform/win/workspace.h"

// `ConvertSidToStringSidW`（完整性级别的 SID 文本）。（`windows.h` 已经由
// `platform/win/ffi.h` 引进来。）
#include <sddl.h>

using namespace flowkeyd;

namespace {

bool interactiveEnabled()
{
    return !qEnvironmentVariable("FLOWKEYD_ALLOW_INTERACTIVE_TESTS").isEmpty();
}

/// 需要联网 / 需要 GitHub 可用的那几条（在线更新）单独一道门：开启交互式测试
/// 并不等于想在这台机器上跑真实网络请求。
bool networkEnabled()
{
    return !qEnvironmentVariable("FLOWKEYD_ALLOW_NETWORK_TESTS").isEmpty();
}

/// 原地转事件循环直到 `done()` 为真或超时（异步的 `QNetworkAccessManager` 需要
/// 有人转事件循环才会往前跑）。
bool spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    return done();
}

/// 某个进程的令牌完整性级别（`S-1-16-8192` = Medium、`S-1-16-12288` = High）。
///
/// 用它断言「以普通用户权限启动」：`whoami /groups` 的输出是本地化的，而 SID
/// 永远一样；直接读令牌比让子进程写文件更省事。
QString integritySidOf(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return QString();
    }
    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token) == 0) {
        CloseHandle(process);
        return QString();
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<unsigned char> storage(size + sizeof(wchar_t) * 2, 0);
    auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(storage.data());
    QString result;
    if (size != 0 && GetTokenInformation(token, TokenIntegrityLevel, label, size, &size) != 0) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(label->Label.Sid, &text) != 0) {
            result = QString::fromWCharArray(text);
            LocalFree(text);
        }
    }
    CloseHandle(token);
    CloseHandle(process);
    return result;
}

/// 从 `runCommand()` 的 detail（`pid 1234 (as user)`）里取出进程号。
DWORD pidFromDetail(const QString &detail)
{
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("pid (\\d+)")).match(detail);
    return match.hasMatch() ? static_cast<DWORD>(match.captured(1).toULong()) : 0;
}

/// 把测试启动的进程收掉，别在机器上留一个跑几十秒的控制台子进程。
void terminateProcess(DWORD pid)
{
    if (HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
        TerminateProcess(process, 0);
        CloseHandle(process);
    }
}

/// 代理进程用的那个 exe（就在测试自己的产物目录里）。
///
/// 产品里代理进程**就是本进程**；测试进程是 `tst_interactive.exe`，它自己没有
/// `--shell-*` 那条内部命令，所以要显式指向 `flowkeyd.exe`。
QString shellHelperExecutable()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("flowkeyd.exe"));
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
    explicit TestWindow(const QString &title, DWORD exStyle = 0);
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

TestWindow::TestWindow(const QString &title, DWORD exStyle)
{
    registerTestWindowClass();
    m_hwnd = CreateWindowExW(exStyle, L"FlowkeydInteractiveTestWindow",
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

/// 一个句柄在顶层窗口 Z 序里的位置（0 = 最上面）；不在链上时返回 -1。
///
/// 隐藏的窗口**仍然**在链上（`GetWindow(GW_HWNDNEXT)` 不看可见性），所以同一套
/// 断言在藏起来前后都能用。
int zIndexOf(HWND hwnd)
{
    int index = 0;
    for (HWND current = GetTopWindow(nullptr); current != nullptr;
         current = GetWindow(current, GW_HWNDNEXT)) {
        if (current == hwnd) {
            return index;
        }
        ++index;
    }
    return -1;
}

/// 一个“UI 线程很忙”的窗口：窗口建在**自己的线程**上，`WM_SHOWWINDOW` 里睡一下。
///
/// 为什么需要它：`ShowWindow`（`SW_HIDE` / `SW_SHOWNOACTIVATE`）对**别的线程**的窗口
/// 是 `SendMessage` 语义 —— 目标线程慢多久，调用方就等多久。工作空间的显隐因此必须
/// **并行**做（`workspace::hideMany()` / `showMany()`），否则一屏窗口会“依次”
/// 消失 / 出现。真机上这条最痛：Chrome / Electron 的 UI 线程忙一下，切一次工作空间
/// 就是“一个一个地消失”。
class SlowWindow
{
public:
    explicit SlowWindow(DWORD delayMs, const QString &title);
    ~SlowWindow();
    SlowWindow(const SlowWindow &) = delete;
    SlowWindow &operator=(const SlowWindow &) = delete;

    /// 窗口建好了才返回（超时返回 nullptr）。
    HWND waitForWindow(int timeoutMs);

private:
    static DWORD WINAPI threadProc(LPVOID param);

    HANDLE m_thread = nullptr;
    HANDLE m_ready = nullptr;
    HWND m_hwnd = nullptr;
    DWORD m_delay = 0;
    QString m_title;
};

LRESULT CALLBACK slowWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_SHOWWINDOW) {
        // 故意慢：调用方（`ShowWindow`）会在这里等（跨线程 `SendMessage`）。
        const auto delay = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (delay > 0) {
            Sleep(delay);
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

SlowWindow::SlowWindow(DWORD delayMs, const QString &title)
    : m_delay(delayMs), m_title(title)
{
    m_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_thread = CreateThread(nullptr, 0, &SlowWindow::threadProc, this, 0, nullptr);
}

SlowWindow::~SlowWindow()
{
    if (m_hwnd != nullptr && IsWindow(m_hwnd) != 0) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
    if (m_thread != nullptr) {
        WaitForSingleObject(m_thread, 5000);
        CloseHandle(m_thread);
    }
    if (m_ready != nullptr) {
        CloseHandle(m_ready);
    }
}

DWORD WINAPI SlowWindow::threadProc(LPVOID param)
{
    auto *self = static_cast<SlowWindow *>(param);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = slowWindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"FlowkeydInteractiveSlowWindow";
    RegisterClassExW(&cls);
    self->m_hwnd = CreateWindowExW(0, cls.lpszClassName,
                                   reinterpret_cast<const wchar_t *>(self->m_title.utf16()),
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480,
                                   320, nullptr, nullptr, cls.hInstance, nullptr);
    if (self->m_hwnd != nullptr) {
        SetWindowLongPtrW(self->m_hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(self->m_delay));
    }
    SetEvent(self->m_ready);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

HWND SlowWindow::waitForWindow(int timeoutMs)
{
    if (m_ready == nullptr) {
        return nullptr;
    }
    if (WaitForSingleObject(m_ready, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0) {
        return nullptr;
    }
    return m_hwnd;
}

/// 按 `Dispatcher::applyWorkspaceEffects()` 的同一顺序执行效果：
/// `show`（底 → 顶）→ `restack` → `hide`（顶 → 底）。
///
/// 不自己写第二套顺序：工作空间的 Z 序就是靠这两步保住的，测试走错路就等于
/// 什么也没验。显隐两步同样走**批量**接口（`showMany()` / `hideMany()`）——
/// 生产代码就是这么做的，分批/逐个的差异由 `hidesAndShowsManyWindowsAtOnce` 钉住。
/// （生产代码里那边外面还套了一层「冻结快照」遮罩，那是纯观感用的，不改变这里的
/// 逻辑；遮罩本身由 `freezesTheScreenWithAnOverlay` 验。）
bool applyWorkspaceEffectsForTest(const core::WorkspaceEffects &effects, QString *error)
{
    namespace ws = platform::win::workspace;
    std::vector<HWND> showTargets;
    showTargets.reserve(effects.show.size());
    for (const core::WindowId id : effects.show) {
        const HWND hwnd = ws::manageTarget(ws::handleOf(id));
        if (hwnd == nullptr) {
            *error = QStringLiteral("a window that should be shown is gone");
            return false;
        }
        showTargets.push_back(hwnd);
    }
    const std::vector<ws::BatchResult> showResults = ws::showMany(showTargets);
    for (std::size_t i = 0; i < showTargets.size(); ++i) {
        if (!showResults.at(i).ok) {
            *error = showResults.at(i).error;
            return false;
        }
    }
    if (!ws::restack(showTargets, error)) {
        return false;
    }
    std::vector<HWND> hideTargets;
    hideTargets.reserve(effects.hide.size());
    for (const core::WindowId id : effects.hide) {
        const HWND hwnd = ws::manageTarget(ws::handleOf(id));
        if (hwnd == nullptr) {
            *error = QStringLiteral("a window that should be hidden is gone");
            return false;
        }
        hideTargets.push_back(hwnd);
    }
    const std::vector<ws::BatchResult> hideResults = ws::hideMany(hideTargets);
    for (const ws::BatchResult &result : hideResults) {
        if (!result.ok) {
            *error = result.error;
            return false;
        }
    }
    return true;
}

/// 测试自己把窗口 cloaked 掉：`DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, …)`。
///
/// 生产代码不需要改这个属性（`platform/win/dwm` 里只有只读的 `isCloaked`），
/// 所以解析代码就留在测试里。`DWMWA_CLOAK`（13）是应用唯一能自己写的“把我藏
/// 起来”：效果与 shell 藏窗口在 `DWMWA_CLOAKED` 上完全一样（非零），因此能
/// 确定性地复现真实现场（本机的「Windows 输入体验」是 shell 藏起来的）。
bool setWindowCloaked(HWND hwnd, bool cloaked)
{
    using SetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    static SetWindowAttributeFn set = []() -> SetWindowAttributeFn {
        HMODULE module = LoadLibraryW(L"dwmapi.dll");
        if (module == nullptr) {
            return nullptr;
        }
        const FARPROC proc = GetProcAddress(module, "DwmSetWindowAttribute");
        if (proc == nullptr) {
            return nullptr;
        }
        SetWindowAttributeFn fn = nullptr;
        static_assert(sizeof(fn) == sizeof(proc), "function pointer sizes must match");
        std::memcpy(&fn, &proc, sizeof(fn));
        return fn;
    }();
    if (set == nullptr) {
        return false;
    }
    const BOOL value = cloaked ? TRUE : FALSE;
    return SUCCEEDED(set(hwnd, 13 /* DWMWA_CLOAK */, &value, sizeof(value)));
}

/// 测试用的 IMM32 探针（只给“输入法切换”那条用例用）。
///
/// 生产代码只需要「切到英文」这一个方向（`platform/win/ime.*`），所以“切成中文”
/// 与“读回当前模式”的解析代码就留在测试里 —— 与上面的 `setWindowCloaked` 同一个
/// 道理（那边也只读不写的那半在产品里）。
class ImeProbe
{
public:
    ImeProbe()
    {
        // 模块在进程生命周期内保持加载：不 FreeLibrary，免得指针悬空。
        HMODULE module = LoadLibraryW(L"imm32.dll");
        if (module == nullptr) {
            return;
        }
        m_getContext = resolve<GetContextFn>(module, "ImmGetContext");
        m_releaseContext = resolve<ReleaseContextFn>(module, "ImmReleaseContext");
        m_getConversionStatus = resolve<GetConversionStatusFn>(module, "ImmGetConversionStatus");
        m_setConversionStatus = resolve<SetConversionStatusFn>(module, "ImmSetConversionStatus");
    }

    bool available() const
    {
        return m_getContext != nullptr && m_releaseContext != nullptr
               && m_getConversionStatus != nullptr && m_setConversionStatus != nullptr;
    }

    /// 读“现在是不是中文（`NATIVE`）模式”；拿不到输入上下文时是 `nullopt`。
    std::optional<bool> isNative(HWND hwnd) const
    {
        std::optional<bool> result;
        withContext(hwnd, [this, &result](void *context) {
            DWORD conversion = 0;
            DWORD sentence = 0;
            if (m_getConversionStatus(context, &conversion, &sentence) != FALSE) {
                result = (conversion & kConversionModeNative) != 0;
            }
        });
        return result;
    }

    /// 把输入法切成中文（`native = true`）或英文；拿不到上下文 / 写失败时 false。
    bool setNative(HWND hwnd, bool native) const
    {
        bool written = false;
        withContext(hwnd, [this, native, &written](void *context) {
            const DWORD mode = native ? kConversionModeNative : 0;
            written = m_setConversionStatus(context, mode, 0) != FALSE;
        });
        return written;
    }

private:
    using GetContextFn = void *(WINAPI *)(HWND);
    using ReleaseContextFn = BOOL(WINAPI *)(HWND, void *);
    using GetConversionStatusFn = BOOL(WINAPI *)(void *, LPDWORD, LPDWORD);
    using SetConversionStatusFn = BOOL(WINAPI *)(void *, DWORD, DWORD);

    static constexpr DWORD kConversionModeNative = 0x0001;

    template <typename Fn>
    static Fn resolve(HMODULE module, const char *name)
    {
        const FARPROC proc = GetProcAddress(module, name);
        if (proc == nullptr) {
            return nullptr;
        }
        Fn fn = nullptr;
        std::memcpy(&fn, &proc, sizeof(fn));
        return fn;
    }

    /// 取到输入上下文就跑 `body`，无论成败都配上 `ImmReleaseContext`。
    template <typename Body>
    void withContext(HWND hwnd, Body body) const
    {
        if (!available() || hwnd == nullptr) {
            return;
        }
        void *context = m_getContext(hwnd);
        if (context == nullptr) {
            return;
        }
        body(context);
        m_releaseContext(hwnd, context);
    }

    GetContextFn m_getContext = nullptr;
    ReleaseContextFn m_releaseContext = nullptr;
    GetConversionStatusFn m_getConversionStatus = nullptr;
    SetConversionStatusFn m_setConversionStatus = nullptr;
};

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
    void movesAWindowToTheAdjacentDesktop();
    void movesAWindowToTheAdjacentDesktopAndFollows();
    void activatesAWindowThatIsOnAnotherDesktop();
    void bringsAWindowOnAnotherDesktopToTheCurrentOne();
    void placementMovesAWindowToAnotherMonitor();
    void movesAWindowToTheAdjacentMonitor();
    void movesAWindowToTheMonitorUnderTheCursor();
    void pinsAWindowToAllDesktops();
    void topmostIsAppliedAndCleared();
    void foregroundQuerySkipsOverlayWindows();
    void listsMainWindowsButSkipsOurOwn();
    void cloakedWindowsAreHiddenFromTheSwitcher();
    void cloakedWindowsOnOtherDesktopsStaySwitchable();
    void switchesTheInputMethodToEnglish();
    void hidesAndShowsAWindowWithoutTouchingIt();
    void hidesAndShowsManyWindowsAtOnce();
    void freezesTheScreenWithAnOverlay();    void enumeratesManageableWindowsByTheirRecord();
    void switchesWorkspacesWithRealWindows();
    void revealsAWindowHiddenInAnotherWorkspace();
    void relocatesDisplacedWindowsWithoutChangingState();
    void restoresHiddenWindowsFromTheRecoveryLog();
    void hookThreadReportsWorkspaceEventsForForeignWindows();
    void launchesProgramsWithTheRequestedPrivilege();
    void opensTargetsAsTheLoggedOnUser();
    void reportsALauncherFailureFromTheShellHelper();
    void checksAndDownloadsAnUpdateFromGitHub();
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

    // 托盘数字图标用的那个精简接口必须与 `probe()` 给出同一组数字（它俩走的是
    // 同一条 COM 会话，但读错一个字段就会把托盘上的数字画错）。
    std::uint32_t index = 0;
    std::uint32_t count = 0;
    QVERIFY2(platform::win::desktop::currentDesktopIndex(&index, &count, &error),
             qPrintable(error));
    QCOMPARE(index, snapshot.current);
    QCOMPARE(count, snapshot.count);

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

/// 跨桌面「挪窗口」：`moveWindowToAdjacentDesktop`。
///
/// 与 `moveWindowToDesktop` 不同，这里的目标是“相邻”的那一张（首尾相接），
/// 而且**视图不跟着走** —— 移动的是窗口，不是当前桌面。
void TestInteractive::movesAWindowToTheAdjacentDesktop()
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
        QStringLiteral("flowkeyd-adj-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    // 刚创建、shell 还没登记的窗口 `GetWindowDesktopId` 会给全零 GUID（本机实测）；
    // 先等它真正属于某张虚拟桌面，否则 `moveWindowToAdjacentDesktop` 只能报
    // “不属于任何虚拟桌面”。真实使用里目标都是已经在桌面上的前台窗口。
    const QString zeroGuid = QStringLiteral("{00000000-0000-0000-0000-000000000000}");
    std::optional<QString> beforeId;
    for (int i = 0; i < 60; ++i) {
        beforeId = platform::win::desktop::windowDesktopId(hwnd, &error);
        if (beforeId.has_value() && *beforeId != zeroGuid) {
            break;
        }
        QThread::msleep(50);
    }
    QVERIFY2(beforeId.has_value() && *beforeId != zeroGuid, qPrintable(error));

    QString detail;
    QVERIFY2(platform::win::desktop::moveWindowToAdjacentDesktop(hwnd, 1, &detail, &error),
             qPrintable(error));
    QVERIFY2(detail.startsWith(QStringLiteral("desktop ")), qPrintable(detail));

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
             "the window's desktop id did not change after moving it to the next desktop");

    // 视图**不**跟着走：移动的是窗口，当前桌面上已经看不到它了。
    const auto onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
    if (onCurrent.has_value()) {
        QVERIFY2(!*onCurrent,
                 "moving a window to another desktop must not follow it with the view");
    }

    // 再搬回来：GUID 应当回到原值。
    QVERIFY2(platform::win::desktop::moveWindowToAdjacentDesktop(hwnd, -1, &detail, &error),
             qPrintable(error));
    std::optional<QString> backId;
    for (int i = 0; i < 60; ++i) {
        backId = platform::win::desktop::windowDesktopId(hwnd, &error);
        QVERIFY2(backId.has_value(), qPrintable(error));
        if (*backId == *beforeId) {
            break;
        }
        QThread::msleep(50);
    }
    QCOMPARE(backId.value_or(QString()), beforeId.value_or(QString()));
}

/// `window` 动作的 `follow = true`：把当前窗口挪到相邻桌面时，**视图也跟过去**，
/// 并且那个窗口重新拿到前台。
///
/// 这正是 `Win+i` / `Win+u` 与 `Win+Shift+i` / `Win+Shift+u` 的区别：后者只搬
/// 窗口（和 Windows 自己的 `Win+Ctrl+Shift+←/→` 一样），前者把用户一起带过去。
void TestInteractive::movesAWindowToTheAdjacentDesktopAndFollows()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QString error;
    platform::win::desktop::Snapshot snapshot;
    QVERIFY2(platform::win::desktop::probe(&snapshot, &error), qPrintable(error));
    if (snapshot.count < 2) {
        QSKIP("only one virtual desktop exists; nothing to follow");
    }

    const QString marker =
        QStringLiteral("flowkeyd-follow-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");
    // 真实用法里被搬的就是前台窗口，先让它拿到前台。
    QVERIFY2(platform::win::window::raiseWindow(hwnd), "could not activate the test window");

    // 刚创建、shell 还没登记的窗口不属于任何虚拟桌面，先等它归属某一张。
    const QString zeroGuid = QStringLiteral("{00000000-0000-0000-0000-000000000000}");
    std::optional<QString> beforeId;
    for (int i = 0; i < 60; ++i) {
        beforeId = platform::win::desktop::windowDesktopId(hwnd, &error);
        if (beforeId.has_value() && *beforeId != zeroGuid) {
            break;
        }
        QThread::msleep(50);
    }
    QVERIFY2(beforeId.has_value() && *beforeId != zeroGuid, qPrintable(error));

    QString detail;
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::MoveNextDesktop, false, &detail,
                                            &error, true),
             qPrintable(error));
    QVERIFY2(detail.contains(QStringLiteral("view followed")), qPrintable(detail));

    // 视图跟过去了：窗口现在就在当前桌面上，并且重新拿到了前台。
    bool onCurrent = false;
    for (int i = 0; i < 60 && !onCurrent; ++i) {
        const auto value = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(value.has_value(), qPrintable(error));
        onCurrent = *value;
        if (!onCurrent) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(onCurrent, "follow = true must switch the view to the window's new desktop");

    bool active = false;
    for (int i = 0; i < 30 && !active; ++i) {
        active = platform::win::window::isActive(hwnd);
        if (!active) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(active, "the window must stay active after following it to the new desktop");

    // 再跟回来，别把用户留在别的桌面上。
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::MovePrevDesktop, false, &detail,
                                            &error, true),
             qPrintable(error));
    std::optional<QString> backId;
    for (int i = 0; i < 60; ++i) {
        backId = platform::win::desktop::windowDesktopId(hwnd, &error);
        QVERIFY2(backId.has_value(), qPrintable(error));
        if (*backId == *beforeId) {
            break;
        }
        QThread::msleep(50);
    }
    QCOMPARE(backId.value_or(QString()), beforeId.value_or(QString()));

    platform::win::desktop::Snapshot after;
    QVERIFY2(platform::win::desktop::probe(&after, &error), qPrintable(error));
    QCOMPARE(after.current, snapshot.current);
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

/// `window` 动作的另一半：**没**声明固定桌面的程序，把窗口搬到当前桌面而不是切视图。
///
/// 这是 2026-10 那次的配套：上面那个用例盯“声明了固定桌面 → 切视图”，
/// 这里盯“没声明 → 搬窗口”，也就是 `moveWindowToCurrentDesktop`。
void TestInteractive::bringsAWindowOnAnotherDesktopToTheCurrentOne()
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
        QStringLiteral("flowkeyd-bringback-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");
    QString detail;

    // 把它激活一次并等 shell 登记它（刚创建的窗口 `GetWindowDesktopId` 会给全零 GUID）。
    QVERIFY2(platform::win::window::raiseWindow(hwnd), "could not activate the fresh window");
    for (int i = 0; i < 40; ++i) {
        if (platform::win::desktop::windowDesktopId(hwnd, &error).has_value()) {
            break;
        }
        QThread::msleep(50);
    }

    const std::uint32_t other = snapshot.current == 1 ? 2 : 1;
    bool changed = false;
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, other, &detail, &error, &changed),
             qPrintable(error));
    QVERIFY2(changed, "moveWindowToDesktop did not report a real desktop change");
    // 把视图明确留在原来那张桌面（搬前台窗口时 Windows 有时候会把视图一起带走）。
    QVERIFY2(platform::win::desktop::switchTo(snapshot.current, &detail, &error), qPrintable(error));

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

    // 关键断言：窗口被带回当前桌面，而视图**没有**被切走。
    QVERIFY2(platform::win::desktop::moveWindowToCurrentDesktop(hwnd, &detail, &error),
             qPrintable(error));
    QVERIFY2(detail.startsWith(QStringLiteral("desktop ")), qPrintable(detail));

    platform::win::desktop::Snapshot after;
    QVERIFY2(platform::win::desktop::probe(&after, &error), qPrintable(error));
    QCOMPARE(after.current, snapshot.current);
    const auto onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
    QVERIFY2(onCurrent.has_value(), qPrintable(error));
    QVERIFY2(*onCurrent, "the window did not come back to the current desktop");
    // 这正是 `window` 动作里紧接着做的那一步：窗口已经在当前桌面上，所以
    // `raiseWindow` 不再需要（也不应该）切视图。
    QVERIFY2(platform::win::window::raiseWindow(hwnd), "could not activate the brought window");
    QVERIFY2(platform::win::window::isActive(hwnd),
             "a window brought to the current desktop must count as active again");

    // 已经在当前桌面上时是空操作（不要白调一次 `MoveViewToDesktop`）。
    QVERIFY2(platform::win::desktop::moveWindowToCurrentDesktop(hwnd, &detail, &error),
             qPrintable(error));
    QVERIFY2(detail.startsWith(QStringLiteral("already on desktop ")), qPrintable(detail));
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

/// 跨显示器「挪窗口」：`moveToAdjacentMonitor`（不循环）。
///
/// 普通窗口保持大小并居中；最大化窗口搬到新显示器后仍然最大化；
/// 已经在最边上时失败（与虚拟桌面不同，显示器不首尾相接）。
void TestInteractive::movesAWindowToTheAdjacentMonitor()
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
        QStringLiteral("flowkeyd-adjmon-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    const auto beforeIndex = platform::win::monitor::indexForWindow(monitors, hwnd);
    QVERIFY(beforeIndex.has_value());
    const core::Rect before =
        platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 800, 600});

    // 优先往右；窗口已经在最右那一块时往左（两个方向共用同一条路径）。
    const int delta = (*beforeIndex + 1 < monitors.size()) ? 1 : -1;
    const std::size_t target =
        delta > 0 ? *beforeIndex + 1 : *beforeIndex - 1;
    QString detail;
    QString error;
    QVERIFY2(platform::win::monitor::moveToAdjacentMonitor(hwnd, delta, &detail, &error),
             qPrintable(error));
    QVERIFY(!detail.isEmpty());

    bool onTarget = false;
    for (int i = 0; i < 60 && !onTarget; ++i) {
        const auto index = platform::win::monitor::indexForWindow(monitors, hwnd);
        onTarget = index.has_value() && *index == target;
        if (!onTarget) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(onTarget, "the window did not end up on the target monitor");

    // 普通窗口：保持原有大小，居中到目标显示器的工作区。
    const core::Rect after =
        platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 0, 0});
    QCOMPARE(after.width, before.width);
    QCOMPARE(after.height, before.height);
    QCOMPARE(after.x, monitors[target].work.x + (monitors[target].work.width - after.width) / 2);
    QCOMPARE(after.y, monitors[target].work.y + (monitors[target].work.height - after.height) / 2);

    // 最大化之后搬回去：最大化状态要保留。
    QVERIFY2(platform::win::monitor::applyPlacement(hwnd, monitors[target].work, true, &error),
             qPrintable(error));
    QVERIFY(IsZoomed(hwnd) != 0);
    QVERIFY2(platform::win::monitor::moveToAdjacentMonitor(hwnd, -delta, &detail, &error),
             qPrintable(error));
    bool back = false;
    for (int i = 0; i < 60 && !back; ++i) {
        const auto index = platform::win::monitor::indexForWindow(monitors, hwnd);
        back = index.has_value() && *index == *beforeIndex && IsZoomed(hwnd) != 0;
        if (!back) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(back, "a maximized window should stay maximized on the adjacent monitor");

    // 边界：把窗口放到最左那一块，再往左就该失败（显示器不循环）。
    QVERIFY2(platform::win::monitor::applyPlacement(hwnd, monitors.front().work, false, &error),
             qPrintable(error));
    error.clear();
    QVERIFY2(!platform::win::monitor::moveToAdjacentMonitor(hwnd, -1, &detail, &error),
             "moving left from the leftmost monitor must fail");
    QVERIFY(!error.isEmpty());
}

/// `monitor = "cursor"` 的真实效果：看光标在哪一块屏，就把窗口搬过去
/// （`indexForCursor` + `moveToMonitor`，默认 `settings.window_monitor` 走的就是
/// 这两步）。
///
/// 需要两块屏；这个用例会暂时把光标挪到另一块屏上，跑完必定放回去（即使是断言
/// 失败也会通过 `CursorGuard` 放回去）。
void TestInteractive::movesAWindowToTheMonitorUnderTheCursor()
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
        QStringLiteral("flowkeyd-cursormon-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    // 光标跑完要放回原处（不然用户的鼠标会莫名其妙停在另一块屏上）。
    POINT original{};
    QVERIFY(GetCursorPos(&original) != 0);
    struct CursorGuard
    {
        POINT point{};
        ~CursorGuard() { SetCursorPos(point.x, point.y); }
    } guard{original};

    // 把窗口放到一块屏上，把光标放到**另一块**屏上。
    const auto windowIndex = platform::win::monitor::indexForWindow(monitors, hwnd);
    QVERIFY(windowIndex.has_value());
    const std::size_t cursorIndex = *windowIndex == 0 ? monitors.size() - 1 : 0;
    const core::MonitorDescription &cursorMonitor = monitors[cursorIndex];
    const core::Rect before =
        platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 800, 600});
    QString error;
    QVERIFY2(platform::win::monitor::applyPlacement(
                 hwnd, core::Rect{monitors[*windowIndex].work.x + 60,
                                  monitors[*windowIndex].work.y + 60,
                                  before.width,
                                  before.height},
                 false, &error),
             qPrintable(error));
    QVERIFY(SetCursorPos(cursorMonitor.work.x + cursorMonitor.work.width / 2,
                         cursorMonitor.work.y + cursorMonitor.work.height / 2)
            != 0);

    // 光标那一块屏就是 `"cursor"` 解析出来的目标（`GetCursorPos` 与
    // `MonitorFromPoint` 都会立刻看到刚设的位置）。
    QCOMPARE(platform::win::monitor::indexForCursor(monitors),
             std::optional<std::size_t>(cursorIndex));

    const core::Rect beforeMove =
        platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 800, 600});
    QString detail;
    QVERIFY2(platform::win::monitor::moveToMonitor(hwnd, monitors, cursorIndex, &detail, &error),
             qPrintable(error));
    QVERIFY(!detail.isEmpty());

    bool onTarget = false;
    for (int i = 0; i < 60 && !onTarget; ++i) {
        const auto index = platform::win::monitor::indexForWindow(monitors, hwnd);
        onTarget = index.has_value() && *index == cursorIndex;
        if (!onTarget) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(onTarget, "the window did not end up on the monitor under the cursor");

    // 保持大小并居中（与 `move_left_monitor` / `move_right_monitor` 同一套）。
    const core::Rect after =
        platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 0, 0});
    QCOMPARE(after.width, beforeMove.width);
    QCOMPARE(after.height, beforeMove.height);
    QCOMPARE(after.x, cursorMonitor.work.x + (cursorMonitor.work.width - after.width) / 2);
    QCOMPARE(after.y, cursorMonitor.work.y + (cursorMonitor.work.height - after.height) / 2);
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

/// 「对前台窗口做动作」时，`WS_EX_TOOLWINDOW` 的覆盖层（实机上的例子：**PowerToys
/// 的「快捷键指南」**，按住 Win 约一秒就弹出来并成为 `GetForegroundWindow()`）不能被
/// 当成用户的目标：它不是用户真正在用的窗口，而且它不属于任何虚拟桌面，
/// 位置无关的 `window` 动作会在它身上失败。`window::find(foreground)` 必须
/// 沿 Z 序往下找到真正的主窗口。
void TestInteractive::foregroundQuerySkipsOverlayWindows()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    const QString marker = QStringLiteral("flowkeyd-fg-%1").arg(QCoreApplication::applicationPid());
    TestWindow main(marker);
    TestWindow overlay(marker + QStringLiteral(" overlay"), WS_EX_TOOLWINDOW);
    QVERIFY2(main.hwnd() != nullptr, "the main test window was not created");
    QVERIFY2(overlay.hwnd() != nullptr, "the overlay test window was not created");
    QVERIFY2((GetWindowLongPtrW(overlay.hwnd(), GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0,
             "the overlay is not a tool window");

    QString error;
    // Keep both in the topmost band, with the overlay above the main window, so the
    // Z-order walk finds the test window deterministically (no other topmost main
    // windows are expected on the machine).
    QVERIFY2(platform::win::window::setTopmost(main.hwnd(), true, &error), qPrintable(error));
    QVERIFY2(platform::win::window::setTopmost(overlay.hwnd(), true, &error), qPrintable(error));

    const core::WindowQuery foreground = core::WindowQuery::make(std::nullopt, std::nullopt);

    // A normal foreground window is returned as-is.
    QVERIFY2(platform::win::window::raiseWindow(main.hwnd()), "could not raise the test window");
    QCOMPARE(reinterpret_cast<quintptr>(GetForegroundWindow()),
             reinterpret_cast<quintptr>(main.hwnd()));
    QCOMPARE(reinterpret_cast<quintptr>(platform::win::window::find(foreground)),
             reinterpret_cast<quintptr>(main.hwnd()));

    // With the overlay in the foreground the query must skip it.
    QVERIFY2(platform::win::window::raiseWindow(overlay.hwnd()), "could not raise the overlay");
    QCOMPARE(reinterpret_cast<quintptr>(GetForegroundWindow()),
             reinterpret_cast<quintptr>(overlay.hwnd()));
    QCOMPARE(reinterpret_cast<quintptr>(platform::win::window::find(foreground)),
             reinterpret_cast<quintptr>(main.hwnd()));
}

void TestInteractive::listsMainWindowsButSkipsOurOwn()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }

    // 窗口切换器（`windows` 动作）枚举的就是这批窗口。这里用测试自己的窗口验证
    // 判据与过滤：普通顶层窗口算「主窗口」，工具窗口不算，而**自己进程的窗口
    // 不进列表**（切换器不该列出 flowkeyd 自己的弹窗 / 日志窗口）。
    const QString token =
        QStringLiteral("flowkeyd-switch-%1").arg(QCoreApplication::applicationPid());
    TestWindow normal(token);
    QVERIFY(normal.hwnd() != nullptr);
    QVERIFY(platform::win::window::isMainWindow(normal.hwnd()));

    TestWindow tool(token + QStringLiteral("-tool"), WS_EX_TOOLWINDOW);
    QVERIFY(tool.hwnd() != nullptr);
    QVERIFY(!platform::win::window::isMainWindow(tool.hwnd()));

    const std::vector<platform::win::window::OpenWindow> windows =
        platform::win::window::listOpenWindows();
    for (const platform::win::window::OpenWindow &entry : windows) {
        QVERIFY2(entry.hwnd != normal.hwnd(), "the current process must not be listed");
        QVERIFY(entry.title != token);
        // 列出来的每一条都必须是「主窗口」，进程名是小写的可执行文件名
        // （拿不到属主时可以是空串）。
        QVERIFY(platform::win::window::isMainWindow(entry.hwnd));
        if (!entry.process.isEmpty()) {
            QCOMPARE(entry.process, entry.process.toLower());
        }
    }
}

/// 被 cloaked 掉的窗口不进窗口切换器。
///
/// 现场（本机 2026-09）：`TextInputHost.exe` 的「Windows 输入体验」是一个
/// `IsWindowVisible` 为真、坐标与尺寸都正常、也没有工具窗口标志的顶层窗口，
/// 于是它混进了切换器（`windows` 动作）。`DWMWA_CLOAKED` 才是实话：shell
/// 根本没有把它显示出来。
void TestInteractive::cloakedWindowsAreHiddenFromTheSwitcher()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }

    const QString marker =
        QStringLiteral("flowkeyd-cloak-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    // 正常状态下它当然是一个可切换的窗口。
    QVERIFY(!platform::win::dwm::isCloaked(hwnd));
    QVERIFY(platform::win::window::isMainWindow(hwnd));
    QVERIFY(platform::win::window::isSwitchableWindow(hwnd));

    // 自己把它藏起来：`IsWindowVisible` / 尺寸一概不变，只有 DWM 知道。
    QVERIFY2(setWindowCloaked(hwnd, true), "DwmSetWindowAttribute(DWMWA_CLOAK) failed");
    QVERIFY(platform::win::dwm::isCloaked(hwnd));
    QString error;
    // 刚创建的窗口 shell 可能还没登记（`IsWindowOnCurrentVirtualDesktop` 报 FALSE、
    // 桌面 GUID 全零）：那时 `isWindowOnCurrentDesktop()` 给的是 `nullopt`
    // （“不属于任何虚拟桌面”），而产品的判据在这种情况下是**保留**它。
    // 这一条要盯的是“cloaked + 在当前桌面上”，所以先等 shell 登记。
    std::optional<bool> onCurrent;
    for (int i = 0; i < 40; ++i) {
        onCurrent = platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        if (onCurrent.has_value()) {
            break;
        }
        QThread::msleep(50);
    }
    QVERIFY2(onCurrent.has_value() && *onCurrent, qPrintable(error));
    // 「主窗口」判据本身没变（`window_rule` 走的是那一条），变的是切换器的判据。
    QVERIFY(platform::win::window::isMainWindow(hwnd));
    QVERIFY(!platform::win::window::isSwitchableWindow(hwnd));

    // 取消 cloaking 之后又回来了。
    QVERIFY2(setWindowCloaked(hwnd, false), "could not uncloak the test window");
    QVERIFY(!platform::win::dwm::isCloaked(hwnd));
    QVERIFY(platform::win::window::isSwitchableWindow(hwnd));
}

/// cloaked 但**在别的虚拟桌面上**的窗口要留着。
///
/// shell 也拿 `DWMWA_CLOAKED` 藏“在别的桌面上”的窗口，所以切换器不能只看
/// cloaking：那些窗口 `windows` 动作会切过去（这正是它比 Alt+Tab 多出来的能力）。
void TestInteractive::cloakedWindowsOnOtherDesktopsStaySwitchable()
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
        QStringLiteral("flowkeyd-cloak-desktop-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    // 搬到另一张桌面，等它真的离开当前桌面（`MoveViewToDesktop` 是异步的）。
    const std::uint32_t other = snapshot.current == 1 ? 2 : 1;
    QString detail;
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, other, &detail, &error),
             qPrintable(error));
    bool left = false;
    for (int i = 0; i < 60 && !left; ++i) {
        const std::optional<bool> onCurrent =
            platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(onCurrent.has_value(), qPrintable(error));
        left = !*onCurrent;
        if (!left) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(left, "the window did not leave the current desktop");

    // 自己再 cloaked 一次，把「在别的桌面上」与「被藏起来」两件事分开：只有
    // 前者才能让切换器留下它。
    QVERIFY2(setWindowCloaked(hwnd, true), "DwmSetWindowAttribute(DWMWA_CLOAK) failed");
    QVERIFY(platform::win::dwm::isCloaked(hwnd));
    QVERIFY(platform::win::window::isMainWindow(hwnd));
    QVERIFY2(platform::win::window::isSwitchableWindow(hwnd),
             "a cloaked window on another desktop must stay switchable");

    // 收尾：搬回当前桌面、取消 cloaking，别给用户留下一个跑到别处的窗口。
    QVERIFY2(platform::win::desktop::moveWindowToDesktop(hwnd, snapshot.current, &detail, &error),
             qPrintable(error));
    bool back = false;
    for (int i = 0; i < 60 && !back; ++i) {
        const std::optional<bool> onCurrent =
            platform::win::desktop::isWindowOnCurrentDesktop(hwnd, &error);
        QVERIFY2(onCurrent.has_value(), qPrintable(error));
        back = *onCurrent;
        if (!back) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(back, "the window did not come back to the current desktop");
    QVERIFY2(setWindowCloaked(hwnd, false), "could not uncloak the test window");
    QVERIFY(platform::win::window::isSwitchableWindow(hwnd));
}

/// 窗口切换器的输入法切换（`platform/win/ime.*`）：卡片一出来（以及输入框拿到
/// 焦点时）要把输入法切成**英文**，否则筛选框里打的是中文候选字，进程名前缀
/// 一条都筛不出来；卡片关掉时要把**打开前**的模式还原回去（输入模式是本进程
/// 这个线程的共享状态，不能给 help / 日志窗口留下痕迹）。
///
/// 真机上验证：先用测试自己的 IMM32 探针把这个线程的输入法切到中文（`NATIVE`，
/// 也就是用户按 Shift 之前的那个状态），再让产品把它切回来、还回去。
void TestInteractive::switchesTheInputMethodToEnglish()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    const ImeProbe probe;
    if (!probe.available()) {
        QSKIP("imm32 is unavailable on this machine");
    }
    TestWindow window(QStringLiteral("flowkeyd-ime-%1").arg(QCoreApplication::applicationPid()));
    QVERIFY(window.hwnd() != nullptr);

    // 模拟“用户开着中文输入法时按了轻碰 Win”。这台机器上没装输入法（或输入法
    // 拒绝切成中文）时跳过 —— 那不是产品的事。
    if (!probe.setNative(window.hwnd(), true)) {
        QSKIP("this machine has no IME to switch (no input context)");
    }
    if (!probe.isNative(window.hwnd()).value_or(false)) {
        QSKIP("this machine's IME refuses to switch to the native (chinese) mode");
    }

    const platform::win::ime::ModeSwitch result =
        platform::win::ime::useAlphanumericMode(window.hwnd());
    QVERIFY2(result.ok, qPrintable(result.detail));
    QVERIFY2(result.changed, qPrintable(result.detail));
    QVERIFY(!probe.isNative(window.hwnd()).value_or(true));

    // 已经是英文就不应该再写一次（输入框反复拿到焦点时不会有副作用）。
    const platform::win::ime::ModeSwitch again =
        platform::win::ime::useAlphanumericMode(window.hwnd());
    QVERIFY2(again.ok, qPrintable(again.detail));
    QVERIFY(!again.changed);

    // ---- 快照 + 还原 ----------------------------------------------------------
    // 窗口切换器打开时先记一份（`readMode`），关掉时写回去（`restoreMode`）：
    // 输入模式是本进程这个线程的共享状态，卡片不能在 help / 日志窗口里留下痕迹。
    QVERIFY2(probe.setNative(window.hwnd(), true),
             "could not put the input method into the native mode");
    const platform::win::ime::Mode before = platform::win::ime::readMode(window.hwnd());
    QVERIFY2(before.valid, "could not read the input mode back");
    QVERIFY(probe.isNative(window.hwnd()).value_or(false));

    const platform::win::ime::ModeSwitch switched =
        platform::win::ime::useAlphanumericMode(window.hwnd());
    QVERIFY2(switched.ok && switched.changed, qPrintable(switched.detail));
    QVERIFY(!probe.isNative(window.hwnd()).value_or(true));

    // 关掉卡片：应该回到打开前那个“中文”的模式（不是留着英文）。
    const platform::win::ime::ModeSwitch restored =
        platform::win::ime::restoreMode(window.hwnd(), before);
    QVERIFY2(restored.ok, qPrintable(restored.detail));
    QVERIFY2(restored.changed, qPrintable(restored.detail));
    QVERIFY(probe.isNative(window.hwnd()).value_or(false));
    QCOMPARE(platform::win::ime::readMode(window.hwnd()).conversion, before.conversion);

    // 已经是那个模式就不应该再写一次（关掉的路径可能在好几个分支上重复走）。
    const platform::win::ime::ModeSwitch restoredAgain =
        platform::win::ime::restoreMode(window.hwnd(), before);
    QVERIFY2(restoredAgain.ok, qPrintable(restoredAgain.detail));
    QVERIFY(!restoredAgain.changed);

    // 没有快照（`readMode` 当时没读到）时“还原”是空操作，不算失败。
    const platform::win::ime::ModeSwitch nothing =
        platform::win::ime::restoreMode(window.hwnd(), platform::win::ime::Mode{});
    QVERIFY2(nothing.ok, qPrintable(nothing.detail));
    QVERIFY(!nothing.changed);
}

/// 工作空间后端：隐藏 / 显示要保持几何与最大化·最小化，且不动焦点、不把窗口弄丢。
///
/// M0 的实测结论（`ShowWindow(SW_HIDE)` 唯一可用）在这里被钉成回归测试：如果
/// 以后有人换成 `SWP_HIDEWINDOW` 或 cloak，这一条会立刻看出来。
void TestInteractive::hidesAndShowsAWindowWithoutTouchingIt()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    const QString marker =
        QStringLiteral("flowkeyd-workspace-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    // 把窗口摆到一个确定的几何上（后面要比对它有没有被动过）。
    QVERIFY(SetWindowPos(hwnd, nullptr, 200, 160, 640, 420, SWP_NOZORDER | SWP_NOACTIVATE) != 0);
    RECT before{};
    QVERIFY(GetWindowRect(hwnd, &before) != 0);

    // 身份：隐藏前后同一个窗口仍然是“那一个”。
    const ws::Identity identity = ws::identityOf(hwnd);
    QVERIFY(identity.valid);
    QCOMPARE(identity.className, QStringLiteral("FlowkeydInteractiveTestWindow"));
    QVERIFY(ws::stillSame(hwnd, identity));
    QCOMPARE(ws::manageTarget(hwnd), hwnd);

    QString error;
    QVERIFY2(ws::hide(hwnd, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) == 0);

    // 显隐不带过渡动画（`dwm::TransitionGuard`）：`show()` 要在窗口**还藏着**的
    // 时候就先设这个属性，所以“藏着也设得上”必须成立（设不上只会退化成有动画，
    // 代码里只记一行 debug、不报错 —— 这里把它钉住）。
    if (platform::win::dwm::available()) {
        QString dwmError;
        QVERIFY2(platform::win::dwm::forceDisableTransitions(hwnd, true, &dwmError),
                 qPrintable(dwmError));
        QVERIFY2(platform::win::dwm::forceDisableTransitions(hwnd, false, &dwmError),
                 qPrintable(dwmError));
    }

    QVERIFY2(IsWindow(hwnd) != 0, "the window must still exist while hidden");
    RECT hidden{};
    QVERIFY(GetWindowRect(hwnd, &hidden) != 0);
    QCOMPARE(hidden.left, before.left);
    QCOMPARE(hidden.top, before.top);
    QCOMPARE(hidden.right, before.right);
    QCOMPARE(hidden.bottom, before.bottom);

    // 构造一个“假的”身份：PID 对不上就不能当成同一个窗口（HWND 复用的防护）。
    ws::Identity impostor = identity;
    impostor.pid += 1;
    QVERIFY(!ws::stillSame(hwnd, impostor));
    impostor = identity;
    impostor.className = QStringLiteral("NotOurClass");
    QVERIFY(!ws::stillSame(hwnd, impostor));

    QVERIFY2(ws::show(hwnd, nullptr, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) != 0);
    QVERIFY(ws::stillSame(hwnd, identity));
    RECT shown{};
    QVERIFY(GetWindowRect(hwnd, &shown) != 0);
    QCOMPARE(shown.left, before.left);
    QCOMPARE(shown.top, before.top);
    QCOMPARE(shown.right, before.right);
    QCOMPARE(shown.bottom, before.bottom);

    // 最大化的窗口藏起来再显示仍是最大化。
    ShowWindow(hwnd, SW_MAXIMIZE);
    QVERIFY(IsZoomed(hwnd) != 0);
    QVERIFY2(ws::hide(hwnd, &error), qPrintable(error));
    QVERIFY2(ws::show(hwnd, nullptr, &error), qPrintable(error));
    QVERIFY2(IsZoomed(hwnd) != 0, "the maximized state was lost");
    ShowWindow(hwnd, SW_RESTORE);

    // 最小化的窗口藏起来再显示仍是最小化（隐藏不该把它弹出来）。
    ShowWindow(hwnd, SW_MINIMIZE);
    QVERIFY(IsIconic(hwnd) != 0);
    QVERIFY2(ws::hide(hwnd, &error), qPrintable(error));
    QVERIFY2(ws::show(hwnd, nullptr, &error), qPrintable(error));
    QVERIFY2(IsIconic(hwnd) != 0, "the minimized state was lost");
}

/// 一屏窗口的显隐必须是**一个**动作：不能按各 App 的 `WM_SHOWWINDOW` 响应速度排队。
///
/// 真机上的现象是“切一次工作空间，窗口一个一个地消失”，根因不在 DWM 过渡动画，而是
/// `SW_HIDE` / `SW_SHOWNOACTIVATE` 是**跨线程 `SendMessage`**：目标 App 的 UI 线程
/// 忙多久，我们就等多久，而旧代码是一个窗口一个窗口地做。
///
/// 这里用三个“`WM_SHOWWINDOW` 里睡 250 ms”的窗口（各自一条线程，与别的进程的窗口
/// 等价）把这件事钉死：并行做 ~250 ms，串行做就是 750 ms。阈值留在中间（500 ms），
/// 免得这台机器偶尔抖一下就红。
void TestInteractive::hidesAndShowsManyWindowsAtOnce()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    constexpr DWORD kSlowMs = 250;
    constexpr int kCount = 3;
    constexpr qint64 kBudgetMs = 500; // 并行 ~250 ms / 串行 ~750 ms，取中间
    const QString marker =
        QStringLiteral("flowkeyd-workspace-many-%1").arg(QCoreApplication::applicationPid());

    std::vector<std::unique_ptr<SlowWindow>> windows;
    std::vector<HWND> handles;
    for (int index = 0; index < kCount; ++index) {
        windows.push_back(std::make_unique<SlowWindow>(
            kSlowMs, QStringLiteral("%1-%2").arg(marker).arg(index)));
        HWND hwnd = windows.back()->waitForWindow(5000);
        QVERIFY2(hwnd != nullptr, "the slow test window was not created");
        handles.push_back(hwnd);
    }
    // 让三个窗口真的先显示出来（这才回到“工作空间里有多个窗口”的起点）。
    {
        QElapsedTimer warmup;
        warmup.start();
        const std::vector<ws::BatchResult> shown = ws::showMany(handles);
        QCOMPARE(static_cast<int>(shown.size()), kCount);
        for (const ws::BatchResult &result : shown) {
            QVERIFY2(result.ok, qPrintable(result.error));
        }
        for (HWND hwnd : handles) {
            QVERIFY(IsWindowVisible(hwnd) != 0);
        }
        QVERIFY2(warmup.elapsed() < kBudgetMs,
                 qPrintable(QStringLiteral("showing %1 slow windows took %2 ms (sequential?)")
                                .arg(kCount)
                                .arg(warmup.elapsed())));
    }

    QElapsedTimer timer;
    timer.start();
    const std::vector<ws::BatchResult> hidden = ws::hideMany(handles);
    const qint64 hideMs = timer.elapsed();
    QCOMPARE(static_cast<int>(hidden.size()), kCount);
    for (const ws::BatchResult &result : hidden) {
        QVERIFY2(result.ok, qPrintable(result.error));
    }
    for (HWND hwnd : handles) {
        QVERIFY(IsWindowVisible(hwnd) == 0);
    }
    QVERIFY2(hideMs < kBudgetMs,
             qPrintable(QStringLiteral("hiding %1 slow windows took %2 ms (sequential?)")
                            .arg(kCount)
                            .arg(hideMs)));

    timer.restart();
    const std::vector<ws::BatchResult> shownAgain = ws::showMany(handles);
    const qint64 showMs = timer.elapsed();
    QCOMPARE(static_cast<int>(shownAgain.size()), kCount);
    for (const ws::BatchResult &result : shownAgain) {
        QVERIFY2(result.ok, qPrintable(result.error));
    }
    for (HWND hwnd : handles) {
        QVERIFY(IsWindowVisible(hwnd) != 0);
    }
    QVERIFY2(showMs < kBudgetMs,
             qPrintable(QStringLiteral("showing %1 slow windows took %2 ms (sequential?)")
                            .arg(kCount)
                            .arg(showMs)));

    // 结果与输入**一一对应**：句柄已经没了的那个只报它自己，别的照常干完。
    HWND gone = nullptr;
    {
        SlowWindow doomed(kSlowMs, marker + QStringLiteral("-doomed"));
        gone = doomed.waitForWindow(5000);
        QVERIFY2(gone != nullptr, "the doomed test window was not created");
    }
    const std::vector<ws::BatchResult> mixed = ws::showMany({handles.at(0), gone, handles.at(1)});
    QCOMPARE(static_cast<int>(mixed.size()), 3);
    QVERIFY2(mixed.at(0).ok, qPrintable(mixed.at(0).error));
    QVERIFY2(!mixed.at(1).ok, "a destroyed window must report a failure");
    QVERIFY2(mixed.at(2).ok, qPrintable(mixed.at(2).error));
}

/// 「冻结快照」遮罩：盖住要露出来的那块、不抢前台、点穿、撤得干净。
///
/// 它是 M11 修「切工作空间时 Chromium 类程序白屏 30–45 ms」的唯一手段（那类程序
/// 被盖着时根本不画，所以只能把过渡盖住）。这里钉住三件容易写错的事：
/// 矩形必须是窗口的并集、样式必须是“点穿 + 不进 Alt+Tab + 不激活”、`release()` 真的销毁。
void TestInteractive::freezesTheScreenWithAnOverlay()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace freeze = platform::win::freeze;
    const QString marker =
        QStringLiteral("flowkeyd-freeze-%1").arg(QCoreApplication::applicationPid());
    TestWindow first(marker + QStringLiteral("-a"));
    TestWindow second(marker + QStringLiteral("-b"));
    HWND a = first.hwnd();
    HWND b = second.hwnd();
    QVERIFY2(a != nullptr && b != nullptr, "the test windows were not created");
    SetWindowPos(a, nullptr, 120, 120, 300, 200, SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(b, nullptr, 200, 180, 260, 160, SWP_NOACTIVATE | SWP_NOZORDER);

    const HWND foreground = GetForegroundWindow();
    freeze::FreezeFrame frame;
    QString error;
    QVERIFY2(frame.coverWindows({a, b}, &error), qPrintable(error));
    QVERIFY(frame.active());

    const HWND overlay = FindWindowW(L"FlowkeydFreezeWindow", nullptr);
    QVERIFY2(overlay != nullptr, "the freeze window was not created");
    QVERIFY2(IsWindowVisible(overlay) != 0, "the freeze window must be visible");
    RECT overlayRect{};
    QVERIFY(GetWindowRect(overlay, &overlayRect) != 0);
    QCOMPARE(overlayRect.left, 120L);
    QCOMPARE(overlayRect.top, 120L);
    QCOMPARE(overlayRect.right, 460L); // a: 120..420，b: 200..460 → 并集 120..460
    QCOMPARE(overlayRect.bottom, 340L); // a: 120..320，b: 180..340

    const LONG_PTR style = GetWindowLongPtrW(overlay, GWL_EXSTYLE);
    QVERIFY2((style & WS_EX_TRANSPARENT) != 0, "the freeze window must not swallow clicks");
    QVERIFY2((style & WS_EX_TOOLWINDOW) != 0, "the freeze window must stay out of Alt+Tab");
    QVERIFY2((style & WS_EX_NOACTIVATE) != 0, "the freeze window must not activate");
    QVERIFY2((style & WS_EX_TOPMOST) != 0, "the freeze window must be on top");
    QVERIFY2(GetForegroundWindow() == foreground, "covering the screen must not move the focus");

    frame.release();
    QVERIFY(!frame.active());
    QVERIFY2(FindWindowW(L"FlowkeydFreezeWindow", nullptr) == nullptr,
             "the freeze window was not destroyed");
}

///「归工作空间管」的枚举：自己进程的窗口永远不进去，未记录的窗口必须真的可见。
///
/// 真正的跟踪列表在状态机里（M5 接线），所以这里只验后端这一侧能验的部分：
/// 自进程排除与“未记录 = 看可见性”。“已记录的藏起来窗口也要算进来”那条
/// 属于纯逻辑，由 `tst_window_match` 的 `manageableWindowsFollowTheRecordNotVisibility`
/// 钉住；真机那一半要等状态机接线（M5）后才能做出“外国进程 + 被我们藏起来”
/// 的现场。
void TestInteractive::enumeratesManageableWindowsByTheirRecord()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    const QString marker =
        QStringLiteral("flowkeyd-manageable-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    // 自己进程的窗口永远不归工作空间管（否则切一下空间就能把 flowkeyd 的弹窗藏了）。
    const std::vector<HWND> untracked = ws::enumerateManageable({}, {});
    for (HWND other : untracked) {
        DWORD pid = 0;
        GetWindowThreadProcessId(other, &pid);
        QVERIFY2(pid != GetCurrentProcessId(),
                 "the current process's own windows must never be manageable");
        QVERIFY(IsWindowVisible(other) != 0);
    }

    // 藏起来之后：当成“未记录的窗口”时它已经被可见性筛掉了；
    // 带着记录去问时它仍被“不是自己的窗口”这一条筛掉（flowkeyd 自己的弹窗
    // 绝不能因为切空间而被藏起来）。隐藏前先确认它本来算一个「主窗口」。
    QVERIFY(platform::win::window::isMainWindow(hwnd));
    QString error;
    QVERIFY2(ws::hide(hwnd, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) == 0);
    QVERIFY(platform::win::window::isMainWindowIgnoringVisibility(hwnd));
    const std::vector<HWND> withRecord = ws::enumerateManageable({hwnd}, {});
    const bool listedWithRecord =
        std::find(withRecord.begin(), withRecord.end(), hwnd) != withRecord.end();
    QVERIFY2(!listedWithRecord,
             "own-process windows must be skipped even when they are tracked");
    QVERIFY2(ws::show(hwnd, nullptr, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) != 0);
}

/// 工作空间的**核心动作**（M6）：两个窗口在同一块屏的两个空间之间来回切，显隐、
/// 几何与 Z 序都要保持不变。
///
/// 这里走的是 `Dispatcher::applyWorkspaceEffects()` 的同一条路（状态机出效果 →
/// `show` 底→顶 → `restack` → `hide` 顶→底 → 聚焦），只是不经过整台守护进程 ——
/// 那需要一份真配置、还会抢用户的前台（见上面那条钩子用例的同一段说明）。
void TestInteractive::switchesWorkspacesWithRealWindows()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    const QString marker =
        QStringLiteral("flowkeyd-spaces-%1").arg(QCoreApplication::applicationPid());
    TestWindow lower(marker + QStringLiteral("-lower"));
    TestWindow upper(marker + QStringLiteral("-upper"));
    HWND a = lower.hwnd();
    HWND b = upper.hwnd();
    QVERIFY2(a != nullptr && b != nullptr, "the test windows were not created");

    // 两个确定的几何（后面要比对它们有没有被动过），并让 `b` 在 Z 序上压着 `a`。
    QVERIFY(SetWindowPos(a, nullptr, 120, 120, 640, 420, SWP_NOZORDER | SWP_NOACTIVATE) != 0);
    QVERIFY(SetWindowPos(b, nullptr, 260, 240, 700, 460, SWP_NOZORDER | SWP_NOACTIVATE) != 0);
    QVERIFY(SetWindowPos(b, a, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0);
    RECT aBefore{};
    RECT bBefore{};
    QVERIFY(GetWindowRect(a, &aBefore) != 0);
    QVERIFY(GetWindowRect(b, &bBefore) != 0);
    QVERIFY2(zIndexOf(a) < zIndexOf(b), "the test windows are not in the expected Z order");

    // 批量问「在不在当前虚拟桌面」：一次 STA 会话问完。不存在的句柄得到的是
    // `std::nullopt`（“不知道”），不能当成“在别的桌面上”。
    // 刚创建的窗口可能还没被 shell 登记到任何虚拟桌面（`GetWindowDesktopId` 给全零
    // GUID → `std::nullopt`），这是系统那边的竞态，所以这里允许重试一秒。
    {
        std::vector<std::optional<bool>> onCurrent;
        for (int attempt = 0; attempt < 20; ++attempt) {
            QString error;
            onCurrent = platform::win::desktop::windowsOnCurrentDesktop({a, nullptr, b}, &error);
            if (onCurrent.size() == 3 && onCurrent.at(0).value_or(false)
                && onCurrent.at(2).value_or(false)) {
                break;
            }
            QThread::msleep(50);
        }
        QCOMPARE(onCurrent.size(), static_cast<std::size_t>(3));
        QVERIFY2(onCurrent.at(0).value_or(false), "the window is not on the current desktop");
        QVERIFY(!onCurrent.at(1).has_value());
        QVERIFY2(onCurrent.at(2).value_or(false), "the window is not on the current desktop");
    }

    // 状态机：两个窗口都在空间 1，`a` 是最近用过的（先出现 `a`，再由 `b` 压在上面）。
    const core::MonitorKey monitor = QStringLiteral("test-monitor");
    core::WorkspaceState state(2);
    state.windowAppeared(ws::idOf(a), monitor);
    state.windowAppeared(ws::idOf(b), monitor);
    state.focusChanged(ws::idOf(a));

    // 1) 空间 2 是空的：两个窗口都藏起来，而且**不抢焦点**。
    {
        const core::WorkspaceEffects effects = state.switchTo(monitor, 2);
        QCOMPARE(effects.show.size(), static_cast<std::size_t>(0));
        QCOMPARE(effects.hide.size(), static_cast<std::size_t>(2));
        QVERIFY2(!effects.focus.has_value(), "an empty space must not steal the focus");
        QString error;
        QVERIFY2(applyWorkspaceEffectsForTest(effects, &error), qPrintable(error));
        QVERIFY(IsWindowVisible(a) == 0);
        QVERIFY(IsWindowVisible(b) == 0);
        QVERIFY(IsWindow(a) != 0 && IsWindow(b) != 0);
    }

    // 2) 切回空间 1：两个窗口回来，几何与 Z 序不变，焦点回到最近用过的 `a`。
    {
        const core::WorkspaceEffects effects = state.switchTo(monitor, 1);
        QCOMPARE(effects.show.size(), static_cast<std::size_t>(2));
        QCOMPARE(effects.hide.size(), static_cast<std::size_t>(0));
        // 记录栈是底 → 顶：`b` 先出现，`a` 后出现，所以 `a` 在最上面。
        QCOMPARE(effects.show.front(), ws::idOf(b));
        QCOMPARE(effects.show.back(), ws::idOf(a));
        QVERIFY2(effects.focus.has_value(), "a non-empty space must hand the focus back");
        QCOMPARE(*effects.focus, ws::idOf(a));

        QString error;
        QVERIFY2(applyWorkspaceEffectsForTest(effects, &error), qPrintable(error));
        QVERIFY(IsWindowVisible(a) != 0);
        QVERIFY(IsWindowVisible(b) != 0);

        RECT aAfter{};
        RECT bAfter{};
        QVERIFY(GetWindowRect(a, &aAfter) != 0);
        QVERIFY(GetWindowRect(b, &bAfter) != 0);
        QCOMPARE(aAfter.left, aBefore.left);
        QCOMPARE(aAfter.top, aBefore.top);
        QCOMPARE(aAfter.right, aBefore.right);
        QCOMPARE(aAfter.bottom, aBefore.bottom);
        QCOMPARE(bAfter.left, bBefore.left);
        QCOMPARE(bAfter.top, bBefore.top);
        QCOMPARE(bAfter.right, bBefore.right);
        QCOMPARE(bAfter.bottom, bBefore.bottom);

        // Z 序：`b` 仍然压在 `a` 上面（`restack()` 拼错了方向会立刻在这里露出来）。
        QVERIFY2(zIndexOf(a) < zIndexOf(b), "the Z order of the space was not restored");
    }
}

/// 联动（M9）：被工作空间藏起来的窗口仍然要“找得到、看得见”。
///
/// 三件事一起验：
///   * `window::find()` / `listOpenWindows()` 靠**状态机的记录**把被我们藏起来的窗口
///     也算进来（不能看 `IsWindowVisible`，它已经是假的了）；
///   * 唤起之前先把那块屏切到它所在的空间（`revealWindow()` → 效果列表里就带着它）；
///   * 切完之后它真的又可见了（`raiseWindow()` 对一个 `SW_HIDE` 着的窗口是没用的）。
void TestInteractive::revealsAWindowHiddenInAnotherWorkspace()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    const QString marker =
        QStringLiteral("flowkeyd-reveal-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");
    QVERIFY(platform::win::window::isMainWindow(hwnd));

    const core::MonitorKey monitor = QStringLiteral("test-monitor");
    const core::WindowId id = ws::idOf(hwnd);
    core::WorkspaceState state(2);
    state.windowAppeared(id, monitor);

    // 切到空间 2（空的）：窗口被藏起来，与本机上的真实效果一致。
    const core::WorkspaceEffects hidden = state.switchTo(monitor, 2);
    QCOMPARE(hidden.hide.size(), static_cast<std::size_t>(1));
    QString error;
    QVERIFY2(applyWorkspaceEffectsForTest(hidden, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) == 0);

    // 切换器与 `window("activate")` 靠记录把它找回来（不传记录就找不到）。
    //
    // 注意：`listOpenWindows()` 会把**本进程**的窗口全部排除（切换器不该列出
    // flowkeyd 自己的弹窗 / 日志窗口），所以“列表里真的有一条”那一步在这里验不了；
    // 收集器拿来判断的就是 `isSwitchableWindow(hwnd, tracked)`，这里直接盯它。
    // 跨进程那一半留给 M11 的验收脚本（需要一个外国进程的窗口）。
    QVERIFY2(platform::win::window::isSwitchableWindow(hwnd, true),
             "a window hidden by the workspace must stay switchable");
    QVERIFY2(!platform::win::window::isSwitchableWindow(hwnd, false),
             "without the record, visibility alone decides (and it is hidden)");

    const core::WindowQuery query =
        core::WindowQuery::make(std::optional<QString>(marker), std::nullopt);
    QCOMPARE(platform::win::window::find(query, {hwnd}), hwnd);
    QVERIFY(platform::win::window::find(query) == nullptr);

    // 唤起：先切回空间 1（效果列表里就带着它），再显式激活它。
    const core::WorkspaceEffects reveal = state.revealWindow(id);
    QCOMPARE(reveal.show.size(), static_cast<std::size_t>(1));
    QCOMPARE(state.spaces(monitor)->active, 1u);
    QVERIFY2(applyWorkspaceEffectsForTest(reveal, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) != 0);
    QVERIFY(platform::win::window::isSwitchableWindow(hwnd, false));

    // 已经在活动空间里：唤起是无操作（幂等，不重复显隐）。
    QVERIFY(state.revealWindow(id).isEmpty());
}

/// 显示器拔插之后的「物归原位」（M7）：`relocateToMonitor()` 把窗口搬到另一块
/// 屏上，而且**不改变它现在的显隐 / 最大化 / 最小化状态**。
///
/// 为什么不能用 `monitor::moveToMonitor()`：它靠 `SetWindowPlacement` 的
/// `showCmd` 收 / 放窗口，会把被我们藏起来的窗口弹出来（那正是 M7 的新函数存在
/// 的理由）。本用例需要两块屏。
void TestInteractive::relocatesDisplacedWindowsWithoutChangingState()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    const std::vector<core::MonitorDescription> monitors =
        core::sortedMonitors(platform::win::monitor::list());
    if (monitors.size() < 2) {
        QSKIP("only one monitor is connected; nothing to move a window to");
    }

    const QString marker =
        QStringLiteral("flowkeyd-relocate-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker);
    HWND hwnd = window.hwnd();
    QVERIFY2(hwnd != nullptr, "the test window was not created");

    const std::optional<std::size_t> startIndex =
        platform::win::monitor::indexForWindow(monitors, hwnd);
    QVERIFY(startIndex.has_value());
    const std::size_t other = *startIndex == 0 ? monitors.size() - 1 : 0;

    const auto rectOf = [hwnd]() {
        return platform::win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 0, 0});
    };
    // 有的程序（以及最大化 / 最小化的窗口）要等一个消息回合才把几何落定，与
    // `movesAWindowToTheAdjacentMonitor` 同一个等待套路。
    const auto waitForMonitor = [&monitors, hwnd](std::size_t target) {
        for (int i = 0; i < 60; ++i) {
            if (platform::win::monitor::indexForWindow(monitors, hwnd)
                == std::optional<std::size_t>(target)) {
                return true;
            }
            QThread::msleep(50);
        }
        return false;
    };

    QString error;
    // 1) **藏起来的窗口**：搬完仍然藏着（不能把用户“藏起来”的窗口弹出来），但
    //    位置已经在新屏上 —— 这是拔掉显示器之后它还能被看见的前提。
    QVERIFY2(ws::hide(hwnd, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) == 0);
    QVERIFY2(ws::relocateToMonitor(hwnd, monitors[other], &error), qPrintable(error));
    QVERIFY2(IsWindowVisible(hwnd) == 0, "relocating a hidden window must not reveal it");
    QVERIFY(IsWindow(hwnd) != 0);
    QVERIFY2(!core::windowIsOffMonitor(rectOf(), monitors[other]),
             "a hidden window was not moved onto the target monitor");

    QVERIFY2(ws::show(hwnd, nullptr, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) != 0);
    QVERIFY2(waitForMonitor(other), "the window did not come back on the target monitor");

    // 2) **最大化的窗口**：搬回去之后仍然最大化，而且落在原来那块屏上。
    ShowWindow(hwnd, SW_MAXIMIZE);
    QVERIFY(IsZoomed(hwnd) != 0);
    QVERIFY2(ws::relocateToMonitor(hwnd, monitors[*startIndex], &error), qPrintable(error));
    QVERIFY2(IsZoomed(hwnd) != 0, "relocating a maximized window must keep it maximized");
    QVERIFY2(waitForMonitor(*startIndex), "the maximized window did not move back");

    // 3) **最小化的窗口**：搬过去之后仍然最小化（不被弹出来）。
    ShowWindow(hwnd, SW_MINIMIZE);
    QVERIFY(IsIconic(hwnd) != 0);
    QVERIFY2(ws::relocateToMonitor(hwnd, monitors[other], &error), qPrintable(error));
    QVERIFY2(IsIconic(hwnd) != 0, "relocating a minimized window must keep it minimized");

    // 边界：窗口已经没了 / 目标屏不存在时直接失败，不报错也不崩。
    QVERIFY(!ws::relocateToMonitor(nullptr, monitors[other], &error));
    QVERIFY(!error.isEmpty());
}

/// 崩溃恢复（M8）：恢复日志里记着的窗口，重启后第一件事就是被放回来。
///
/// 这里模拟「进程被 `taskkill /F` 杀掉」：先把窗口藏起来、再把恢复日志写好，然后
/// **丢掉所有内存状态**（本用例里根本没有状态机），只留一个配置目录 —— 再走一遍
/// `ws::restoreFromLog()`。顺带钉住两条容错：句柄已经没了的条目、身份对不上的条目
/// （`HWND` 被复用）只从日志里丢掉，**不碰**那个窗口，也不会挡住其他条目的恢复。
void TestInteractive::restoresHiddenWindowsFromTheRecoveryLog()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString configPath = dir.filePath(QStringLiteral("config.lua"));
    const QString logPath = core::workspaceLogPath(configPath);
    QVERIFY(!QFileInfo::exists(logPath));

    const QString marker =
        QStringLiteral("flowkeyd-restore-%1").arg(QCoreApplication::applicationPid());
    TestWindow window(marker + QStringLiteral("-logged"));
    TestWindow stranger(marker + QStringLiteral("-impostor"));
    HWND hwnd = window.hwnd();
    HWND other = stranger.hwnd();
    QVERIFY2(hwnd != nullptr && other != nullptr, "the test windows were not created");

    QString error;
    QVERIFY2(ws::hide(hwnd, &error), qPrintable(error));
    QVERIFY2(ws::hide(other, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(hwnd) == 0);
    QVERIFY(IsWindowVisible(other) == 0);

    // 「先写日志再隐藏」的那份日志：一条真记录 + 一条身份对不上的（模拟 `HWND` 被
    // 复用：同一个句柄，但类名 / 进程号已经不是当年那个窗口）。
    core::WorkspaceLog log;
    core::HiddenWindow entry;
    entry.window = ws::idOf(hwnd);
    entry.identity = ws::identityOf(hwnd);
    entry.workspace.desktop = QStringLiteral("{00000000-0000-0000-0000-000000000000}");
    entry.workspace.monitor = QStringLiteral("\\\\.\\DISPLAY1");
    entry.workspace.index = 2;
    entry.title = QStringLiteral("the window we hid");
    core::addHiddenWindow(log, entry);

    core::HiddenWindow impostor = entry;
    impostor.window = ws::idOf(other);
    impostor.identity.pid = 0xFFFF;  // 想错了：这是个不存在的进程号
    impostor.identity.className = QStringLiteral("NotTheWindowWeHid");
    core::addHiddenWindow(log, impostor);

    // 另一条：窗口早就不在了（`HWND` 1 从来不是有效窗口）。
    core::HiddenWindow gone = entry;
    gone.window = 1;
    gone.title = QStringLiteral("gone");
    core::addHiddenWindow(log, gone);

    QVERIFY2(ws::saveWorkspaceLog(configPath, log, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(logPath));

    // 重启之后：只凭日志（没有状态机、没有配置）恢复。
    const ws::RestoreResult result = ws::restoreFromLog(configPath);
    QCOMPARE(result.report.entries, 3);
    QCOMPARE(result.report.restored, 1);
    QCOMPARE(result.report.dropped, 2);
    QCOMPARE(result.report.failed, 0);
    QVERIFY2(IsWindowVisible(hwnd) != 0, "the logged window was not restored");
    // 身份对不上的那个窗口不能被碰：它仍然是藏着的。
    QVERIFY2(IsWindowVisible(other) == 0, "a handle whose identity does not match was touched");
    // 全部处理完（恢复 / 丢掉）：日志文件被删掉，下次启动不会再白试一次。
    QVERIFY(!QFileInfo::exists(logPath));

    // 再跑一次是空操作：退出 / 注销 / 托盘菜单会重复触发恢复。
    const ws::RestoreResult again = ws::restoreFromLog(configPath);
    QCOMPARE(again.report.entries, 0);
    QVERIFY(IsWindowVisible(hwnd) != 0);

    // 坏掉的日志文件：不崩、不报错、并且被清掉（否则每次启动都会警告一次）。
    QFile broken(logPath);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{ this is not json");
    broken.close();
    const ws::RestoreResult damaged = ws::restoreFromLog(configPath);
    QCOMPARE(damaged.report.entries, 0);
    QVERIFY(!QFileInfo::exists(logPath));

    // 收尾：把本用例藏起来的第二个窗口放回来（测试不能给别人留下“消失”的窗口）。
    QVERIFY2(ws::show(other, nullptr, &error), qPrintable(error));
    QVERIFY(IsWindowVisible(other) != 0);
}

/// 工作空间的事件管线（M5）：钩子线程把**别的进程**的窗口报成工作空间事件，而且
/// 命中「期望事件表」的事件会被吞掉。
///
/// 这里不起整个守护进程（那要装一份配置、还会抢用户的前台），只装一个
/// `HookThread` 并收事件 —— 验证的正是「钩子 → 事件表 → 交给工作线程」这一段；
/// 状态机那一半由 `tst_workspace` / `tst_workspace_event` 盯住。
void TestInteractive::hookThreadReportsWorkspaceEventsForForeignWindows()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    namespace ws = platform::win::workspace;

    std::mutex mutex;
    std::vector<core::WorkspaceEvent> events;
    // 与生产里同一个形状：动作线程执行效果前登记，钩子线程命中即吞。
    auto expected = std::make_shared<core::ExpectedWorkspaceEvents>();
    platform::win::HookThread hook;
    QString error;
    const bool started = hook.start(
        std::make_shared<core::Compiled>(),
        [](const std::shared_ptr<const core::Compiled> &, const core::Trigger &) {},
        [](const std::shared_ptr<const core::Compiled> &, const platform::win::PlacementEvent &) {},
        [&mutex, &events](const std::shared_ptr<const core::Compiled> &,
                          const core::WorkspaceEvent &event) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
        },
        expected,
        &error);
    QVERIFY2(started, qPrintable(error));

    const auto seen = [&mutex, &events](core::WorkspaceEvent::Kind kind, core::WindowId window) {
        std::lock_guard<std::mutex> lock(mutex);
        int found = 0;
        for (const core::WorkspaceEvent &event : events) {
            if (event.kind == kind && event.window == window) {
                ++found;
            }
        }
        return found;
    };

    // 一个**别的进程**的主窗口（钩子带 `WINEVENT_SKIPOWNPROCESS`，自己的窗口收不到）。
    //
    // **不用记事本**：本机的记事本是单实例、带标签页的 Store 应用，`notepad file`
    // 往往只给一个**已经存在**的窗口开一个新标签（测试文件里已有一大段注释说这件事）
    // —— 那就没有“新窗口出现”，这条用例也就无从断言。字符映射表是普通 Win32
    // 程序：每个实例自己开窗、`WM_CLOSE` 就退出，而且没人会一直开着它。
    platform::win::RunCommandSpec spec;
    spec.program = QStringLiteral("charmap.exe");
    spec.show = core::ShowMode::Normal;
    QString detail;
    QVERIFY2(platform::win::runCommand(spec, &detail, &error), qPrintable(error));
    const DWORD pid = pidFromDetail(detail);

    const core::WindowQuery query =
        core::WindowQuery::make(std::nullopt, std::optional<QString>(QStringLiteral("charmap.exe")));
    HWND hwnd = nullptr;
    for (int i = 0; i < 120 && hwnd == nullptr; ++i) {
        hwnd = platform::win::window::find(query);
        if (hwnd == nullptr) {
            QThread::msleep(50);
        }
    }
    QVERIFY2(hwnd != nullptr, "the launched Character Map window never appeared");
    const core::WindowId window = ws::idOf(hwnd);

    // 失败时把收到的每一条事件都打出来（否则只剩一句“没收到”，没法判断是管线
    // 没接上还是收的是另一个句柄）。
    const auto dump = [&mutex, &events, hwnd]() {
        std::lock_guard<std::mutex> lock(mutex);
        QStringList lines;
        lines.append(QStringLiteral("waiting for hwnd %1 (title %2), got %3 event(s)")
                         .arg(reinterpret_cast<quintptr>(hwnd))
                         .arg(platform::win::window::windowTitle(hwnd))
                         .arg(events.size()));
        for (const core::WorkspaceEvent &event : events) {
            lines.append(QStringLiteral("%1: %2")
                             .arg(QString::fromLatin1(core::workspaceEventName(event.kind)))
                             .arg(event.window));
        }
        return lines.join(QStringLiteral(" | "));
    };

    // 1) 窗口出现会被报出来（`EVENT_OBJECT_SHOW` → 350 ms 防抖 → 事件）。
    {
        const bool appeared = spinUntil(
            [&] { return seen(core::WorkspaceEvent::Kind::WindowAppeared, window) > 0; }, 10000);
        const QString diagnostics = dump();
        QVERIFY2(appeared, qPrintable(diagnostics));
    }

    // 2) 命中期望事件表的事件被吞掉：先登记“我要最小化它”，再真的最小化。
    expected->expect(core::WorkspaceEvent::Kind::WindowMinimized, window,
                     platform::win::monotonicMs());
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Minimize, false, &detail, &error),
             qPrintable(error));
    // 等一个防抖周期再多给一点余量；没被吞的话足够到货。
    spinUntil([&] { return seen(core::WorkspaceEvent::Kind::WindowMinimized, window) > 0; }, 1200);
    QCOMPARE(seen(core::WorkspaceEvent::Kind::WindowMinimized, window), 0);

    // 3) 没登记的最小化照常报（证明第 2 步的“没收到”是吞掉的，不是根本没这事件）。
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Restore, false, &detail, &error),
             qPrintable(error));
    QThread::msleep(300);
    QVERIFY2(platform::win::window::applyTo(hwnd, core::WindowOp::Minimize, false, &detail, &error),
             qPrintable(error));
    QVERIFY2(spinUntil([&] { return seen(core::WorkspaceEvent::Kind::WindowMinimized, window) > 0; },
                       5000),
             "minimizing a window was not reported");

    // 4) 销毁也会报（状态机靠它清记录，否则 `HWND` 复用会认错窗口）。
    (void)platform::win::window::applyTo(hwnd, core::WindowOp::Close, false, &detail, &error);
    if (!spinUntil([&] { return seen(core::WorkspaceEvent::Kind::WindowDestroyed, window) > 0; },
                   3000)) {
        // 本机的记事本是带标签页的 Store 应用，关闭可能先弹确认框：直接收掉进程。
        if (pid != 0) {
            terminateProcess(pid);
        }
    }
    QVERIFY2(spinUntil([&] { return seen(core::WorkspaceEvent::Kind::WindowDestroyed, window) > 0; },
                       10000),
             "destroying a window was not reported");

    hook.stop();
    if (pid != 0) {
        terminateProcess(pid);
    }
}

/// `run` 的默认权限：**以当前登录用户的普通权限启动**。
///
/// 这是本特性的核心断言：提权实例必须把子进程降到中等完整性
/// （`S-1-16-8192`），而不是继承自己的管理员令牌（`S-1-16-12288`）。
/// `elevated = true` 那一半只在**本进程已经提权**时才跑 —— 没提权时它会弹 UAC，
/// 而自动化绝不能弹出那个框。
void TestInteractive::launchesProgramsWithTheRequestedPrivilege()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    platform::win::RunCommandSpec spec;
    // 一个活 20 秒左右的隐藏控制台进程：我们只需要读完它的令牌。
    spec.program = QStringLiteral("ping -n 20 127.0.0.1 >nul");
    spec.shell = true;
    spec.show = core::ShowMode::Hidden;
    QString detail;
    QString error;
    QVERIFY2(platform::win::runCommand(spec, &detail, &error), qPrintable(error));
    const DWORD pid = pidFromDetail(detail);
    QVERIFY2(pid != 0, qPrintable(detail));
    const QString integrity = integritySidOf(pid);
    terminateProcess(pid);
    QVERIFY2(integrity == QStringLiteral("S-1-16-8192"),
             qPrintable(QStringLiteral("the default `run` did not start a normal-user process "
                                       "(integrity %1; detail %2)")
                            .arg(integrity.isEmpty() ? QStringLiteral("(unknown)") : integrity,
                                 detail)));

    if (!platform::win::isElevated()) {
        // `elevated = true` 会弹 UAC：自动化不碰。
        return;
    }
    spec.elevated = true;
    QVERIFY2(platform::win::runCommand(spec, &detail, &error), qPrintable(error));
    const DWORD elevatedPid = pidFromDetail(detail);
    QVERIFY2(elevatedPid != 0, qPrintable(detail));
    const QString elevatedIntegrity = integritySidOf(elevatedPid);
    terminateProcess(elevatedPid);
    QVERIFY2(elevatedIntegrity == QStringLiteral("S-1-16-12288"),
             qPrintable(QStringLiteral("`elevated = true` did not start an elevated process "
                                       "(integrity %1; detail %2)")
                            .arg(elevatedIntegrity.isEmpty() ? QStringLiteral("(unknown)")
                                                             : elevatedIntegrity,
                                 detail)));
}

/// `open` 动作与程序启动器的 shell 调用：提权实例会把它们交给一个**普通权限的
/// 代理进程**，所以打开的文档 / 脚本 / 启动器条目都不会跟着 flowkeyd 一起提权。
///
/// 让一个批处理写 `whoami /groups`（SID 与系统语言无关）：它由 shell 间接启动，
/// 所以这条同时验证了「代理进程起来了」与「它调 shell 时也是普通权限」。两种
/// 模式（`--shell-open` / `--shell-app`）各跑一遍。
void TestInteractive::opensTargetsAsTheLoggedOnUser()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const std::pair<platform::win::ShellHelperRequest::Mode, QString> cases[] = {
        {platform::win::ShellHelperRequest::Mode::Open, QStringLiteral("open")},
        {platform::win::ShellHelperRequest::Mode::App, QStringLiteral("app")},
    };
    for (const auto &[mode, tag] : cases) {
        const QString script = dir.filePath(tag + QStringLiteral(".bat"));
        const QString output = dir.filePath(tag + QStringLiteral(".txt"));
        {
            QFile file(script);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QStringLiteral("@echo off\r\nwhoami /groups > \"%~dp0%1.txt\"\r\n")
                           .arg(tag)
                           .toUtf8());
        }

        // 产品路径：提权时 `openTarget()` / `apps::launchAppAsUser()` 就是这么把请求
        // 交给代理进程的（测试进程不是 flowkeyd.exe，所以要显式给出）。
        platform::win::ShellHelperRequest request;
        request.mode = mode;
        request.target = script;
        request.show = core::ShowMode::Hidden;
        const platform::win::ShellHelperOutcome outcome =
            platform::win::runShellHelperAsUser(request, shellHelperExecutable());
        QVERIFY2(outcome.started, qPrintable(outcome.error));
        QVERIFY2(outcome.exitCode == 0, qPrintable(outcome.output));

        QElapsedTimer timer;
        timer.start();
        while (!QFile::exists(output) && timer.elapsed() < 20000) {
            QThread::msleep(50);
        }
        QFile written(output);
        QVERIFY2(written.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("the %1 batch file never wrote its output").arg(tag)));
        const QString text = QString::fromLatin1(written.readAll());
        QVERIFY2(text.contains(QStringLiteral("S-1-16-8192")),
                 qPrintable(QStringLiteral("the %1 target did not run as a normal user:\n%2")
                                .arg(tag, text)));
        QVERIFY2(!text.contains(QStringLiteral("S-1-16-12288")),
                 qPrintable(QStringLiteral("the %1 target inherited the elevated token:\n%2")
                                .arg(tag, text)));
    }
}

/// 程序启动器：条目在**普通权限的代理进程**里激活；代理失败时它写回的错误文本
/// 要原样回到调用方，否则日志里就只剩一句“没启动”。
///
/// 故意用一个不存在的条目：它不会真的升起任何程序，只验证 `--shell-app` 那条
/// 分支与结果文件（`--shell-result`）的回传。
void TestInteractive::reportsALauncherFailureFromTheShellHelper()
{
    if (!interactiveEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1 to run the interactive tests");
    }
    platform::win::ShellHelperRequest request;
    request.mode = platform::win::ShellHelperRequest::Mode::App;
    request.target = QStringLiteral("shell:AppsFolder\\flowkeyd-no-such-package-000000!App");
    const platform::win::ShellHelperOutcome outcome =
        platform::win::runShellHelperAsUser(request, shellHelperExecutable());
    QVERIFY2(outcome.started, qPrintable(outcome.error));
    QVERIFY(outcome.exitCode != 0);
    QVERIFY2(!outcome.output.isEmpty(), "the shell helper did not report why the launch failed");
}

/// 在线更新真的去 GitHub 走一遍：检查最新发布 → 下载 **slim 升级包** →
/// sha256 校验 → 解压出新的 exe。
///
/// **默认跳过**（要联网、要 GitHub 可用、会真的下载约 700 KB），显式开：
///
///     $env:FLOWKEYD_ALLOW_NETWORK_TESTS = "1"
///
/// 它只把新 exe 落到一个**临时目录**，既不替换任何东西、也不重启 ——
/// 「替换 + 重启」由 `tst_update_install` 与手工冒烟覆盖。
/// 报告一个很旧的当前版本，这样 GitHub 上的最新发布一定看起来是“新版本”，
/// 于是这条用例不依赖本文档所在的提交是否已经发布。
void TestInteractive::checksAndDownloadsAnUpdateFromGitHub()
{
    if (!networkEnabled()) {
        QSKIP("set FLOWKEYD_ALLOW_NETWORK_TESTS=1 to run the network tests");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    app::Updater updater(QStringLiteral("20-01-01-0000000"),
                         dir.filePath(QStringLiteral("flowkeyd.exe")));
    app::UpdateModel *model = updater.model();

    updater.checkForUpdates();
    const bool settled = spinUntil(
        [model]() {
            return model->phaseName() == QStringLiteral("available")
                || model->phaseName() == QStringLiteral("uptodate")
                || model->phaseName() == QStringLiteral("failed");
        },
        60000);
    QVERIFY2(settled, qPrintable(QStringLiteral("the update check never finished: %1 %2")
                                    .arg(model->phaseName(), model->errorDetail())));
    QVERIFY2(model->phaseName() == QStringLiteral("available"),
             qPrintable(QStringLiteral("expected an update, got %1: %2")
                            .arg(model->phaseName(), model->errorDetail())));
    QVERIFY(!model->newVersion().isEmpty());
    QVERIFY(!model->notes().isEmpty());

    updater.startDownload();
    const bool staged = spinUntil(
        [&updater, model]() {
            return updater.hasStagedUpdate() || model->phaseName() == QStringLiteral("failed");
        },
        180000);
    QVERIFY2(staged, "the download never finished");
    QVERIFY2(model->phaseName() == QStringLiteral("ready"),
             qPrintable(QStringLiteral("expected ready, got %1: %2")
                            .arg(model->phaseName(), model->errorDetail())));
    // sha256 对不上 / 解压失败 / 拿到的不是一个 PE 都会在这里变成 failed。
    QVERIFY(updater.hasStagedUpdate());
    QVERIFY(updater.stagedExecutable().startsWith(dir.path()));

    QFile file(updater.stagedExecutable());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    // Windows 上文件句柄没关就删不掉（下一步的清理会失败）。
    file.close();
    QCOMPARE(bytes.left(2), QByteArray("MZ"));
    QVERIFY2(bytes.size() > 1024 * 1024,
             qPrintable(QStringLiteral("the downloaded executable is suspiciously small: %1")
                            .arg(bytes.size())));

    // 收尾：把 `flowkeyd.exe.new` 删掉（`dismiss` 不会动已经落盘的文件）。
    QVERIFY(QFile::remove(updater.stagedExecutable()));
}

QTEST_MAIN(TestInteractive)
#include "tst_interactive.moc"
