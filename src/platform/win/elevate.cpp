#include "platform/win/elevate.h"

#include <QDir>

#include <shellapi.h>

namespace flowkeyd::platform::win {

bool isElevated()
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size) != 0;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

QString quoteArg(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    const bool needsQuotes = arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t'))
        || arg.contains(QLatin1Char('"'));
    if (!needsQuotes) {
        return arg;
    }
    QString out;
    out.reserve(arg.size() + 2);
    out += QLatin1Char('"');
    int backslashes = 0;
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            // 反斜杠要加倍，再补一个以转义引号本身。
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            out += QLatin1Char('"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            out += QString(backslashes, QLatin1Char('\\'));
            backslashes = 0;
        }
        out += ch;
    }
    // 收尾：成对的反斜杠，保证末尾的引号不被吞掉。
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

QString buildParameters(const QStringList &args)
{
    QStringList quoted;
    quoted.reserve(args.size());
    for (const QString &arg : args) {
        quoted.append(quoteArg(arg));
    }
    return quoted.join(QLatin1Char(' '));
}

QStringList commandLineArguments()
{
    QStringList args;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.append(QString::fromWCharArray(argv[i]));
    }
    LocalFree(argv);
    return args;
}

bool relaunchElevated(QString *error)
{
    QStringList args = commandLineArguments();
    if (args.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot determine the executable path");
        }
        return false;
    }
    const QString executable = args.takeFirst();
    if (!args.contains(QLatin1String("--elevated"))) {
        args.append(QStringLiteral("--elevated"));
    }
    const QString parameters = buildParameters(args);
    const QString workingDirectory = QDir::currentPath();

    const std::wstring operation = L"runas";
    const std::wstring file = executable.toStdWString();
    const std::wstring params = parameters.toStdWString();
    const std::wstring directory = workingDirectory.toStdWString();

    SetLastError(ERROR_SUCCESS);
    HINSTANCE result = ShellExecuteW(nullptr,
                                     operation.c_str(),
                                     file.c_str(),
                                     params.c_str(),
                                     directory.c_str(),
                                     SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(result);
    if (code <= 32) {
        if (error != nullptr) {
            const DWORD last = GetLastError();
            *error = QStringLiteral("ShellExecuteW(runas) failed with code %1: %2")
                         .arg(static_cast<qint64>(code))
                         .arg(last != ERROR_SUCCESS ? winErrorMessage(last)
                                                    : QStringLiteral("permission denied"));
        }
        return false;
    }
    return true;
}

} // namespace flowkeyd::platform::win
