#include "platform/win/power.h"

#include "platform/win/ffi.h"
#include "platform/win/logging.h"

#include <algorithm>

namespace flowkeyd::platform::win::power {

// `powrprof.h` 会连带拉进 `powerbase.h` / `powersetting.h`，而我们只需要这一个
// 入口；手写声明更干净（导入库照样静态链接，见 AGENTS.md 第 2 节）。
extern "C" BOOLEAN WINAPI SetSuspendState(BOOLEAN hibernate, BOOLEAN forceCritical,
                                          BOOLEAN disableWakeEvent);

namespace {

/// `SC_MONITORPOWER` 的参数：关掉显示器。MinGW 的头文件里没有这个常量。
constexpr LPARAM kMonitorOff = 2;

/// 广播关屏消息时等待单个窗口的上限。
constexpr UINT kScreenOffTimeoutMs = 1000;

} // namespace

const std::vector<PowerOpInfo> &powerOpTable()
{
    static const std::vector<PowerOpInfo> table = {
        // 睡眠：在现代待机（S0 low power idle）的机器上就是“屏幕关掉、系统继续待机”。
        {core::PowerOp::Sleep, "sleep", false},
        // 休眠：内存写进磁盘后断电。
        {core::PowerOp::Hibernate, "hibernate", false},
        {core::PowerOp::Shutdown, "shutdown", true},
        {core::PowerOp::Restart, "restart", true},
        {core::PowerOp::Logoff, "logoff", true},
        // 锁定工作站：不需要管理员权限。
        {core::PowerOp::Lock, "lock", false},
        // 关掉全部显示器，但**不**睡眠；系统与钩子继续运行。
        {core::PowerOp::ScreenOff, "screen_off", false},
    };
    return table;
}

bool requiresShutdownPrivilege(core::PowerOp op)
{
    const std::vector<PowerOpInfo> &table = powerOpTable();
    const auto entry = std::find_if(
        table.begin(), table.end(), [op](const PowerOpInfo &info) { return info.op == op; });
    return entry != table.end() && entry->needsShutdownPrivilege;
}

bool enableShutdownPrivilege(QString *detail, QString *error)
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token) == 0) {
        if (error != nullptr) {
            *error = lastErrorMessage("OpenProcessToken(SeShutdownPrivilege)");
        }
        return false;
    }

    LUID luid{};
    if (LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &luid) == 0) {
        if (error != nullptr) {
            *error = lastErrorMessage("LookupPrivilegeValueW(SeShutdownPrivilege)");
        }
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES state{};
    state.PrivilegeCount = 1;
    state.Privileges[0].Luid = luid;
    state.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    SetLastError(0);
    const BOOL ok = AdjustTokenPrivileges(token, FALSE, &state, 0, nullptr, nullptr);
    const DWORD last = GetLastError();
    CloseHandle(token);

    if (ok == 0) {
        if (error != nullptr) {
            *error = lastErrorMessage("AdjustTokenPrivileges");
        }
        return false;
    }
    // `AdjustTokenPrivileges` 可能返回 TRUE 却一个特权都没加上。
    if (last == ERROR_NOT_ALL_ASSIGNED) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "the process token does not hold SeShutdownPrivilege (flowkeyd is not elevated); "
                "shutdown/restart/logoff need administrator rights");
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("SeShutdownPrivilege enabled");
    }
    return true;
}

namespace {

/// 关掉显示器（**所有屏幕**），但不进入睡眠。
///
/// 收件人是 `HWND_BROADCAST`，也就是每个顶层窗口都会收到这条系统消息，其中
/// 外壳（`Progman`/`WorkerW`）负责真正执行它。用 `SendMessageTimeoutW` 而不是
/// `SendMessageW`：广播要挨个等每个顶层窗口返回，任何一个卡住的窗口都会把
/// 调用者无限期挂住，而这个调用发生在工作线程上（不是钩子回调，但同样不该被
/// 第三方窗口拖住）。
bool screenOff(QString *detail, QString *error)
{
    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(HWND_BROADCAST,
                                             WM_SYSCOMMAND,
                                             SC_MONITORPOWER,
                                             kMonitorOff,
                                             SMTO_ABORTIFHUNG,
                                             kScreenOffTimeoutMs,
                                             &result);
    if (sent == 0) {
        if (error != nullptr) {
            *error = lastErrorMessage("SendMessageTimeoutW(SC_MONITORPOWER)");
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("display(s) off");
    }
    return true;
}

/// 睡眠（`hibernate = false`）或休眠（`true`）。
bool suspend(bool hibernate, QString *detail, QString *error)
{
    // 睡眠同样先试着要权限：待机是系统级操作，有它更稳；拿不到也不该拦住睡眠。
    QString ignored;
    if (!enableShutdownPrivilege(nullptr, &ignored)) {
        logDebug(QStringLiteral("sleeping without SeShutdownPrivilege: %1").arg(ignored));
    }
    // `disableWakeEvent = FALSE`，保持正常的唤醒事件。
    const BOOLEAN ok = SetSuspendState(hibernate ? TRUE : FALSE, FALSE, FALSE);
    if (ok == FALSE) {
        if (error != nullptr) {
            *error = lastErrorMessage(hibernate ? "SetSuspendState(hibernate)"
                                                : "SetSuspendState(sleep)");
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = hibernate ? QStringLiteral("hibernating") : QStringLiteral("suspending");
    }
    return true;
}

} // namespace

bool execute(core::PowerOp op, QString *detail, QString *error)
{
    // 不需要特权的先处理掉，并且**在** `enableShutdownPrivilege` 之前 return，
    // 否则没提权时会先报一句指错方向的权限错误。
    switch (op) {
    case core::PowerOp::Lock:
        if (LockWorkStation() == 0) {
            if (error != nullptr) {
                *error = lastErrorMessage("LockWorkStation");
            }
            return false;
        }
        if (detail != nullptr) {
            *detail = QStringLiteral("locked");
        }
        return true;
    case core::PowerOp::Sleep:
        return suspend(false, detail, error);
    case core::PowerOp::Hibernate:
        return suspend(true, detail, error);
    case core::PowerOp::ScreenOff:
        return screenOff(detail, error);
    case core::PowerOp::Shutdown:
    case core::PowerOp::Restart:
    case core::PowerOp::Logoff:
        break;
    }

    // 只有关机 / 重启 / 注销走到这里，它们确实需要特权。
    if (!enableShutdownPrivilege(nullptr, error)) {
        return false;
    }

    DWORD flags = 0;
    const char *what = "shutdown";
    switch (op) {
    case core::PowerOp::Shutdown:
        flags = EWX_POWEROFF | EWX_FORCEIFHUNG;
        what = "shutdown";
        break;
    case core::PowerOp::Restart:
        flags = EWX_REBOOT | EWX_FORCEIFHUNG;
        what = "restart";
        break;
    case core::PowerOp::Logoff:
        flags = EWX_LOGOFF | EWX_FORCEIFHUNG;
        what = "logoff";
        break;
    default:
        break;
    }
    // `dwReason` 用 0：关机原因在事件日志里是可选的。
    if (ExitWindowsEx(flags, 0) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("ExitWindowsEx(%1) failed: %2")
                         .arg(QString::fromLatin1(what), winErrorMessage(GetLastError()));
        }
        return false;
    }
    if (detail != nullptr) {
        *detail = QStringLiteral("%1 requested").arg(QString::fromLatin1(what));
    }
    return true;
}

} // namespace flowkeyd::platform::win::power
