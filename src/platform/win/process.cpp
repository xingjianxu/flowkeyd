#include "platform/win/process.h"

#include "platform/win/elevate.h"

#include <QDir>

#include <algorithm>

#include <shellapi.h>

namespace flowkeyd::platform::win {

namespace {

constexpr DWORD kCreateNoWindow = 0x08000000u;
constexpr DWORD kCreateNewProcessGroup = 0x00000200u;
constexpr DWORD kCreateUnicodeEnvironment = 0x00000400u;

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
    QString application;
    QStringList commandArgs;
    if (spec.shell) {
        QString line = spec.program;
        for (const QString &arg : spec.args) {
            line += QLatin1Char(' ');
            line += quoteForCmd(arg);
        }
        application = QStringLiteral("cmd.exe");
        commandArgs = QStringList{QStringLiteral("/C"), line};
    } else {
        application = spec.program;
        commandArgs = spec.args;
    }

    QString commandLine = quoteArg(application);
    for (const QString &arg : commandArgs) {
        commandLine += QLatin1Char(' ');
        commandLine += quoteArg(arg);
    }
    std::vector<wchar_t> commandBuffer(commandLine.utf16(),
                                       commandLine.utf16() + commandLine.size());
    commandBuffer.push_back(L'\0');

    std::vector<wchar_t> directoryBuffer;
    if (spec.cwd.has_value()) {
        const QString directory = QDir::toNativeSeparators(*spec.cwd);
        directoryBuffer.assign(directory.utf16(), directory.utf16() + directory.size());
        directoryBuffer.push_back(L'\0');
    }

    std::vector<wchar_t> environment;
    if (!spec.env.isEmpty()) {
        environment = buildEnvironmentBlock(spec.env);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = creationFlags(spec.show) | kCreateUnicodeEnvironment;
    const BOOL ok = CreateProcessW(nullptr,
                                   commandBuffer.data(),
                                   nullptr,
                                   nullptr,
                                   FALSE,
                                   flags,
                                   environment.empty() ? nullptr : environment.data(),
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
        *detail = summary;
    }
    return true;
}

bool openTarget(const QString &target,
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

} // namespace flowkeyd::platform::win
