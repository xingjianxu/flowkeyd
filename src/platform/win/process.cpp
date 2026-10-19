#include "platform/win/process.h"

#include "platform/win/apps.h"
#include "platform/win/elevate.h"
#include "platform/win/ffi.h"
#include "platform/win/logging.h"

#include <QDir>
#include <QFile>
#include <QUuid>

#include <algorithm>

#include <shellapi.h>
#include <tlhelp32.h>

namespace flowkeyd::platform::win {

namespace {

constexpr DWORD kCreateNoWindow = 0x08000000u;
constexpr DWORD kCreateNewProcessGroup = 0x00000200u;
constexpr DWORD kCreateUnicodeEnvironment = 0x00000400u;

/// 代理进程最多活多久。`ShellExecuteExW` 本身是毫秒级的；这里是兜底，免得一个
/// 卡住的 shell 把动作线程永远挂在那儿（动作线程还在跑别的快捷键）。
constexpr DWORD kShellHelperTimeoutMs = 30000;

DWORD creationFlags(core::ShowMode show)
{
    if (show == core::ShowMode::Hidden) {
        // 给子进程一个它看不见的控制台：有效的标准句柄、没有窗口，
        // 也不与 flowkeyd 共用控制台。
        return kCreateNoWindow;
    }
    // 这里**不能**用 `DETACHED_PROCESS`：它让控制台程序完全没有控制台，
    // 而这类程序会一声不响地启动失败（`wezterm.exe` 的 scoop shim、
    // `powershell.exe -File` 都是这样）。GUI 程序会忽略 `CREATE_NO_WINDOW`。
    return kCreateNewProcessGroup | kCreateNoWindow;
}

/// 把当前环境解析成一行行条目（保留 `=C:` 这类以 `=` 开头的条目）。
std::vector<QString> environmentEntries()
{
    std::vector<QString> entries;
    LPWCH block = GetEnvironmentStringsW();
    if (block == nullptr) {
        return entries;
    }
    const wchar_t *cursor = block;
    while (*cursor != L'\0') {
        const int length = static_cast<int>(wcslen(cursor));
        entries.emplace_back(QString::fromWCharArray(cursor, length));
        cursor += length + 1;
    }
    FreeEnvironmentStringsW(block);
    return entries;
}

QString entryName(const QString &entry)
{
    if (entry.startsWith(QLatin1Char('='))) {
        // `=C:=C:\path`：名字是第二个 `=` 之前的片段。
        const qsizetype second = entry.indexOf(QLatin1Char('='), 1);
        return second < 0 ? entry : entry.left(second);
    }
    const qsizetype equals = entry.indexOf(QLatin1Char('='));
    return equals < 0 ? entry : entry.left(equals);
}

/// `RunCommandSpec` 的两份形态：`ShellExecuteExW` 要 `lpFile` + `lpParameters`
/// 分开给，而 `CreateProcessW` 要一整条命令行。
struct LaunchCommandLine
{
    /// 可执行文件（`shell = true` 时是 `cmd.exe`）。
    QString application;
    /// 只含参数（已按 `CommandLineToArgvW` 的规则加引号）；没有参数时为空。
    QString arguments;
    /// 完整命令行：第一个 token 就是程序本身，给 `CreateProcessW` 用。
    QString line;
};

LaunchCommandLine buildCommandLine(const RunCommandSpec &spec)
{
    LaunchCommandLine out;
    QStringList args = spec.args;
    if (spec.shell) {
        QString line = spec.program;
        for (const QString &arg : spec.args) {
            line += QLatin1Char(' ');
            line += quoteForCmd(arg);
        }
        out.application = QStringLiteral("cmd.exe");
        args = QStringList{QStringLiteral("/C"), line};
    } else {
        out.application = spec.program;
    }
    QString quoted;
    for (const QString &arg : args) {
        if (!quoted.isEmpty()) {
            quoted += QLatin1Char(' ');
        }
        quoted += quoteArg(arg);
    }
    out.arguments = quoted;
    out.line = quoteArg(out.application);
    if (!quoted.isEmpty()) {
        out.line += QLatin1Char(' ') + quoted;
    }
    return out;
}

/// 从 `pid` 复制一份主令牌（需要自己能打开它的进程令牌）。
HANDLE tokenFromProcess(DWORD pid, QString *error)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("OpenProcess(%1) failed: %2")
                         .arg(pid)
                         .arg(winErrorMessage(GetLastError()));
        }
        return nullptr;
    }
    HANDLE raw = nullptr;
    if (OpenProcessToken(process, TOKEN_DUPLICATE | TOKEN_QUERY, &raw) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("OpenProcessToken(%1) failed: %2")
                         .arg(pid)
                         .arg(winErrorMessage(GetLastError()));
        }
        CloseHandle(process);
        return nullptr;
    }
    HANDLE primary = nullptr;
    if (DuplicateTokenEx(raw,
                         TOKEN_ALL_ACCESS,
                         nullptr,
                         SecurityImpersonation,
                         TokenPrimary,
                         &primary)
        == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("DuplicateTokenEx(%1) failed: %2")
                         .arg(pid)
                         .arg(winErrorMessage(GetLastError()));
        }
        CloseHandle(raw);
        CloseHandle(process);
        return nullptr;
    }
    CloseHandle(raw);
    CloseHandle(process);
    return primary;
}

bool tokenIsElevated(HANDLE token)
{
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    if (GetTokenInformation(token, TokenElevation, &elevation, size, &size) == 0) {
        return true; // 问不出来就当成提权的（保守：宁可不用它做“普通权限”）
    }
    return elevation.TokenIsElevated != 0;
}

/// 本会话里的 `explorer.exe`（`GetShellWindow()` 不适用时的兜底）。
std::vector<DWORD> explorerProcessIds()
{
    std::vector<DWORD> pids;
    DWORD session = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session) == 0) {
        return pids;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return pids;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok != 0; ok = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0) {
            continue;
        }
        DWORD processSession = 0;
        if (ProcessIdToSessionId(entry.th32ProcessID, &processSession) == 0
            || processSession != session) {
            continue;
        }
        pids.push_back(entry.th32ProcessID);
    }
    CloseHandle(snapshot);
    return pids;
}

/// 登录用户（`explorer.exe`）的**普通权限**主令牌。
///
/// 为什么不是 `CreateProcessAsUser`：它要 `SeAssignPrimaryTokenPrivilege`，而
/// 提权管理员令牌里根本没有这条权限（真机实测 `AdjustTokenPrivileges` 报
/// 1300 ERROR_NOT_ALL_ASSIGNED、`CreateProcessAsUser` 报 1314
/// ERROR_PRIVILEGE_NOT_HELD）。`CreateProcessWithTokenW` 只要
/// `SeImpersonatePrivilege`（提权管理员默认就有），代价是经过 Secondary Logon
/// 服务 —— 该服务被禁用时会失败，调用方退回继承自己的令牌。
///
/// 令牌来自 `GetShellWindow()` 的属主进程；取不到（锁屏 / 非交互会话 / 换过
/// shell）时兜底：本会话里第一个 `explorer.exe`。**优先用不提权的那个**，因为
/// 用户把 explorer 换成提权进程时也没别的来源了，至少要挑对。
HANDLE loggedOnUserToken(QString *error)
{
    DWORD shellPid = 0;
    if (HWND shell = GetShellWindow()) {
        GetWindowThreadProcessId(shell, &shellPid);
    }
    if (shellPid != 0) {
        QString shellError;
        if (HANDLE token = tokenFromProcess(shellPid, &shellError)) {
            if (!tokenIsElevated(token)) {
                return token;
            }
            // shell 本身是提权的（罕见）：没有更好的来源，用它是唯一的选择。
            logDebug(QStringLiteral("the shell (pid %1) is elevated; launched programs will too")
                         .arg(shellPid));
            return token;
        }
        if (error != nullptr) {
            *error = shellError;
        }
    }

    HANDLE elevatedFallback = nullptr;
    for (const DWORD pid : explorerProcessIds()) {
        QString explorerError;
        HANDLE token = tokenFromProcess(pid, &explorerError);
        if (token == nullptr) {
            continue;
        }
        if (!tokenIsElevated(token)) {
            CloseHandle(elevatedFallback);
            return token;
        }
        if (elevatedFallback == nullptr) {
            elevatedFallback = token;
        } else {
            CloseHandle(token);
        }
    }
    if (elevatedFallback != nullptr) {
        return elevatedFallback;
    }
    if (error != nullptr && error->isEmpty()) {
        *error = QStringLiteral("no explorer.exe in this session");
    }
    return nullptr;
}

/// 代理进程写回结果用的临时文件（`%TEMP%`，同一个用户可写）。
QString newShellHelperResultPath()
{
    return QDir::tempPath() + QStringLiteral("/flowkeyd-shell-%1-%2.txt")
                                 .arg(GetCurrentProcessId())
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

/// `CreateProcessW` 的公共部分：`token` 为空时继承当前进程的令牌，否则用
/// `CreateProcessWithTokenW`（这就是「以登录用户的普通权限启动」那条路）。
///
/// 环境块**总是显式给出去**：`CreateProcessWithTokenW` 在 `lpEnvironment` 为 null
/// 时不会继承我们的环境（真机实测：连 `PATH` 都没了），于是子进程启动的东西会找不到
/// 工具。显式传一份「当前环境 + 覆盖项」两边行为一致。
bool createProcess(const RunCommandSpec &spec,
                   const LaunchCommandLine &command,
                   HANDLE token,
                   const QString &detailSuffix,
                   QString *detail,
                   QString *error)
{
    std::vector<wchar_t> commandBuffer(command.line.utf16(),
                                      command.line.utf16() + command.line.size());
    commandBuffer.push_back(L'\0');

    std::vector<wchar_t> directoryBuffer;
    if (spec.cwd.has_value()) {
        const QString directory = QDir::toNativeSeparators(*spec.cwd);
        directoryBuffer.assign(directory.utf16(), directory.utf16() + directory.size());
        directoryBuffer.push_back(L'\0');
    }

    std::vector<wchar_t> environment = buildEnvironmentBlock(spec.env);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = creationFlags(spec.show) | kCreateUnicodeEnvironment;
    SetLastError(ERROR_SUCCESS);
    const BOOL ok = token == nullptr
        ? CreateProcessW(nullptr,
                         commandBuffer.data(),
                         nullptr,
                         nullptr,
                         FALSE,
                         flags,
                         environment.data(),
                         directoryBuffer.empty() ? nullptr : directoryBuffer.data(),
                         &startup,
                         &process)
        : CreateProcessWithTokenW(token,
                                  0,
                                  nullptr,
                                  commandBuffer.data(),
                                  flags,
                                  environment.data(),
                                  directoryBuffer.empty() ? nullptr : directoryBuffer.data(),
                                  &startup,
                                  &process);
    if (ok == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot run %1: %2")
                         .arg(spec.program, winErrorMessage(GetLastError()));
        }
        return false;
    }

    QString summary;
    if (spec.wait) {
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        summary = QStringLiteral("exit code %1").arg(exitCode);
    } else {
        summary = QStringLiteral("pid %1").arg(process.dwProcessId);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (detail != nullptr) {
        *detail = summary + detailSuffix;
    }
    return true;
}

/// `elevated = true` 而自己没提权：只能请 shell 用 `runas` 动词再走一遍
/// （会弹 UAC，用户可能拒绝 —— 拒绝时 `ShellExecuteExW` 报 1223）。
///
/// 这条路**没法转交 `env`**（`ShellExecuteExW` 没有环境块参数），所以写了 `env`
/// 时给一条警告：宁可少一个变量，也不要把整条动作变成“什么都不发生”。
bool runElevatedViaShell(const RunCommandSpec &spec,
                         const LaunchCommandLine &command,
                         QString *detail,
                         QString *error)
{
    if (!spec.env.isEmpty()) {
        logWarn(QStringLiteral("`%1`: `env` is ignored when asking for administrator rights (UAC)")
                    .arg(spec.program));
    }
    const std::wstring file = command.application.toStdWString();
    const std::wstring parameters = command.arguments.toStdWString();
    const std::wstring directory =
        spec.cwd.has_value() ? QDir::toNativeSeparators(*spec.cwd).toStdWString() : std::wstring();

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = command.arguments.isEmpty() ? nullptr : parameters.c_str();
    info.lpDirectory = spec.cwd.has_value() ? directory.c_str() : nullptr;
    info.nShow = showCode(spec.show);
    SetLastError(ERROR_SUCCESS);
    if (ShellExecuteExW(&info) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot run %1 as administrator: %2")
                         .arg(spec.program, winErrorMessage(GetLastError()));
        }
        return false;
    }

    QString summary = QStringLiteral("started through UAC");
    if (info.hProcess != nullptr) {
        if (spec.wait) {
            WaitForSingleObject(info.hProcess, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeProcess(info.hProcess, &exitCode);
            summary = QStringLiteral("exit code %1").arg(exitCode);
        } else {
            summary = QStringLiteral("pid %1").arg(GetProcessId(info.hProcess));
        }
        CloseHandle(info.hProcess);
    }
    if (detail != nullptr) {
        *detail = summary + QStringLiteral(" (elevated)");
    }
    return true;
}

/// 代理进程失败时的错误文本：优先用代理写回来的那一句（里面有真正的
/// `ShellExecuteExW` 错误），没有就给一个带退出码的兜底。
QString helperFailure(const ShellHelperOutcome &outcome, const QString &what)
{
    if (!outcome.output.isEmpty()) {
        return outcome.output;
    }
    return QStringLiteral("the shell helper could not %1 (exit code %2)").arg(what).arg(outcome.exitCode);
}

} // namespace

QString quoteForCmd(const QString &arg)
{
    if (arg.contains(QLatin1Char(' ')) && !arg.startsWith(QLatin1Char('"'))) {
        return QStringLiteral("\"") + arg + QStringLiteral("\"");
    }
    return arg;
}

int showCode(core::ShowMode show)
{
    switch (show) {
    case core::ShowMode::Hidden:
        return SW_HIDE;
    case core::ShowMode::Minimized:
        return SW_SHOWMINIMIZED;
    case core::ShowMode::Maximized:
        return SW_SHOWMAXIMIZED;
    case core::ShowMode::Normal:
        break;
    }
    return SW_SHOWNORMAL;
}

std::vector<wchar_t> buildEnvironmentBlock(const QMap<QString, QString> &overrides)
{
    std::vector<QString> entries = environmentEntries();
    // 覆盖项：先删掉同名条目（大小写无关），再加进去。
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const QString &entry) {
                          return entryName(entry).compare(it.key(), Qt::CaseInsensitive) == 0;
                      }),
                      entries.end());
        entries.push_back(QStringLiteral("%1=%2").arg(it.key(), it.value()));
    }
    // Windows 要求环境块按名称不区分大小写排序。这里的“不区分大小写”指
    // Windows（`RtlCompareUnicodeString(..., TRUE)`）那种**先转成大写**再按
    // 码元比较的排序，而**不是** `Qt::CaseInsensitive`（它折成小写）：
    // `NU_VERSION` 与 `NUMBER_OF_PROCESSORS` 在两种折叠下顺序正好相反，
    // 而 Windows 自己的环境块把 `NUMBER_OF_PROCESSORS` 排在前面。
    std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) {
        return entryName(a).toUpper() < entryName(b).toUpper();
    });

    std::vector<wchar_t> block;
    for (const QString &entry : entries) {
        block.insert(block.end(), entry.utf16(), entry.utf16() + entry.size());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

bool runCommand(const RunCommandSpec &spec, QString *detail, QString *error)
{
    const LaunchCommandLine command = buildCommandLine(spec);

    if (spec.elevated && !isElevated()) {
        return runElevatedViaShell(spec, command, detail, error);
    }

    if (!spec.elevated && isElevated()) {
        // 默认路径：把请求降到当前登录用户的普通权限。拿不到令牌、或者
        // `CreateProcessWithTokenW` 自己失败时**退回继承当前令牌**并写一条
        // warning —— 快捷键按下去什么都不发生比“这次提权了”更糟。
        QString tokenError;
        if (HANDLE token = loggedOnUserToken(&tokenError)) {
            QString userError;
            const bool ok = createProcess(spec, command, token, QStringLiteral(" (as user)"), detail,
                                          &userError);
            CloseHandle(token);
            if (ok) {
                return true;
            }
            logWarn(QStringLiteral("could not start %1 as the logged-on user (%2); falling back to "
                                   "the elevated token")
                        .arg(spec.program, userError));
        } else {
            logWarn(QStringLiteral("could not start %1 as the logged-on user (%2); falling back to "
                                   "the elevated token")
                        .arg(spec.program, tokenError));
        }
    }
    return createProcess(spec, command, nullptr, QString(), detail, error);
}

bool openTargetInProcess(const QString &target,
                         const std::optional<QString> &args,
                         const std::optional<QString> &cwd,
                         core::ShowMode show,
                         QString *error)
{
    const std::wstring operation = L"open";
    const std::wstring file = target.toStdWString();
    const std::wstring params = args.value_or(QString()).toStdWString();
    const std::wstring directory = cwd.value_or(QString()).toStdWString();
    const HINSTANCE result = ShellExecuteW(nullptr,
                                           operation.c_str(),
                                           file.c_str(),
                                           args.has_value() ? params.c_str() : nullptr,
                                           cwd.has_value() ? directory.c_str() : nullptr,
                                           showCode(show));
    const auto code = reinterpret_cast<INT_PTR>(result);
    if (code <= 32) {
        if (error != nullptr) {
            *error = QStringLiteral("ShellExecuteW failed (code %1) for %2")
                         .arg(static_cast<qint64>(code))
                         .arg(target);
        }
        return false;
    }
    return true;
}

bool openTarget(const QString &target,
                const std::optional<QString> &args,
                const std::optional<QString> &cwd,
                core::ShowMode show,
                QString *error)
{
    if (!isElevated()) {
        // 自己就是普通权限：不必为一次 ShellExecute 再起一个进程。
        return openTargetInProcess(target, args, cwd, show, error);
    }
    ShellHelperRequest request;
    request.mode = ShellHelperRequest::Mode::Open;
    request.target = target;
    request.args = args;
    request.cwd = cwd;
    request.show = show;
    const ShellHelperOutcome outcome = runShellHelperAsUser(request);
    if (!outcome.started) {
        if (error != nullptr) {
            *error = outcome.error;
        }
        return false;
    }
    if (outcome.exitCode != 0) {
        if (error != nullptr) {
            *error = helperFailure(outcome, QStringLiteral("open %1").arg(target));
        }
        return false;
    }
    return true;
}

int runShellHelper(const ShellHelperRequest &request)
{
    QString error;
    bool ok = false;
    if (request.mode == ShellHelperRequest::Mode::App) {
        ok = apps::launchApp(request.target, &error);
    } else {
        ok = openTargetInProcess(request.target, request.args, request.cwd, request.show, &error);
    }
    if (request.resultPath.has_value()) {
        QFile result(*request.resultPath);
        if (result.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (!ok) {
                result.write(error.toUtf8());
            }
            result.close();
        }
    }
    return ok ? 0 : 1;
}

ShellHelperOutcome runShellHelperAsUser(const ShellHelperRequest &request,
                                        const QString &helperExecutable)
{
    ShellHelperOutcome outcome;
    const QString executable =
        helperExecutable.isEmpty() ? currentExecutablePath() : helperExecutable;
    if (executable.isEmpty()) {
        outcome.error = QStringLiteral("cannot determine the flowkeyd executable path");
        return outcome;
    }

    const QString resultPath = newShellHelperResultPath();
    QFile::remove(resultPath);

    QStringList arguments;
    if (request.mode == ShellHelperRequest::Mode::App) {
        arguments << QStringLiteral("--shell-app") << request.target;
    } else {
        arguments << QStringLiteral("--shell-open") << request.target;
        if (request.args.has_value()) {
            arguments << QStringLiteral("--shell-args") << *request.args;
        }
        if (request.cwd.has_value()) {
            arguments << QStringLiteral("--shell-cwd") << *request.cwd;
        }
        arguments << QStringLiteral("--shell-show") << core::showModeName(request.show);
    }
    arguments << QStringLiteral("--shell-result") << resultPath;

    QString tokenError;
    HANDLE token = loggedOnUserToken(&tokenError);
    if (token == nullptr) {
        outcome.error = QStringLiteral("cannot launch the shell helper as the logged-on user: %1")
                            .arg(tokenError);
        return outcome;
    }

    const QString commandLine = quoteArg(executable) + QLatin1Char(' ') + buildParameters(arguments);
    std::vector<wchar_t> commandBuffer(commandLine.utf16(),
                                       commandLine.utf16() + commandLine.size());
    commandBuffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    // 环境块显式给出去：`CreateProcessWithTokenW` 要经过 Secondary Logon 服务，
    // 不给 `lpEnvironment` 时子进程拿到的不是我们的环境（真机实测：连 `PATH` 都
    // 没了）。代理进程自己不需要 PATH（它的 Qt 就在 exe 旁边），但它调 shell
    // 启动的那些程序会读它。
    std::vector<wchar_t> environment = buildEnvironmentBlock({});
    PROCESS_INFORMATION process{};
    SetLastError(ERROR_SUCCESS);
    const BOOL ok = CreateProcessWithTokenW(token,
                                           0,
                                           nullptr,
                                           commandBuffer.data(),
                                           kCreateNoWindow | kCreateUnicodeEnvironment,
                                           environment.data(),
                                           nullptr,
                                           &startup,
                                           &process);
    const DWORD spawnError = GetLastError();
    CloseHandle(token);
    if (ok == 0) {
        outcome.error = QStringLiteral("cannot start the shell helper: %1")
                            .arg(winErrorMessage(spawnError));
        return outcome;
    }
    CloseHandle(process.hThread);

    const DWORD waited = WaitForSingleObject(process.hProcess, kShellHelperTimeoutMs);
    DWORD exitCode = 0;
    if (waited == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exitCode);
        outcome.exitCode = static_cast<int>(exitCode);
        outcome.started = true;
    } else {
        outcome.error = QStringLiteral("the shell helper (pid %1) did not finish within %2 ms")
                            .arg(process.dwProcessId)
                            .arg(kShellHelperTimeoutMs);
    }
    CloseHandle(process.hProcess);

    QFile result(resultPath);
    if (result.exists()) {
        if (result.open(QIODevice::ReadOnly)) {
            outcome.output = QString::fromUtf8(result.readAll()).trimmed();
            result.close();
        }
        QFile::remove(resultPath);
    }
    return outcome;
}

} // namespace flowkeyd::platform::win
