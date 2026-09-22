#include "platform/win/autostart.h"

#include "platform/win/elevate.h"
#include "platform/win/logging.h"

#include <QDir>
#include <QFile>
#include <QStringConverter>
#include <QXmlStreamReader>

#include <algorithm>
#include <vector>

namespace flowkeyd::platform::win {

namespace {

constexpr DWORD kCreateNoWindow = 0x08000000u;

/// XML 文本转义（路径里可能有 `&` 或 `<`）。
QString xmlEscape(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case u'&':
            out += QLatin1String("&amp;");
            break;
        case u'<':
            out += QLatin1String("&lt;");
            break;
        case u'>':
            out += QLatin1String("&gt;");
            break;
        case u'"':
            out += QLatin1String("&quot;");
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

/// `schtasks.exe` 的完整路径（不依赖 `PATH`，GUI 进程的搜索路径不可靠）。
QString schtasksPath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const UINT length = GetSystemDirectoryW(buffer.data(),
                                                static_cast<UINT>(buffer.size()));
        if (length == 0) {
            break;
        }
        if (length < buffer.size()) {
            const QString dir = QString::fromWCharArray(buffer.data(),
                                                        static_cast<int>(length));
            return QDir::toNativeSeparators(dir + QStringLiteral("\\schtasks.exe"));
        }
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) {
            break;
        }
    }
    return QStringLiteral("schtasks.exe");
}

struct CommandResult
{
    bool started = false;
    DWORD exitCode = 0;
    QByteArray output;
    QString error;
};

/// 起一个隐藏的 `schtasks`，把 stdout+stderr 一起收回来。
///
/// 这里不用 `QProcess`：这段代码在 `QApplication` 构造之前就可能跑，
/// 而 `QProcess` 的同步路径依赖 Qt 事件机制。手写管道反而更确定。
CommandResult runSchtasks(const QStringList &args)
{
    CommandResult result;
    const QString exe = schtasksPath();
    QString commandLine = quoteArg(exe);
    for (const QString &arg : args) {
        commandLine += QLatin1Char(' ');
        commandLine += quoteArg(arg);
    }
    std::vector<wchar_t> commandBuffer(commandLine.utf16(),
                                       commandLine.utf16() + commandLine.size());
    commandBuffer.push_back(L'\0');

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (CreatePipe(&readPipe, &writePipe, &attributes, 0) == 0) {
        result.error = lastErrorMessage("CreatePipe");
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // 子进程的 stdin 要指向 NUL：用了 STARTF_USESTDHANDLES 就必须给三个句柄，
    // 而 GUI 进程自己通常没有可继承的标准输入。
    HANDLE nulInput = CreateFileW(L"NUL",
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &attributes,
                                  OPEN_EXISTING,
                                  0,
                                  nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nulInput;

    PROCESS_INFORMATION process{};
    const BOOL ok = CreateProcessW(exe.toStdWString().c_str(),
                                   commandBuffer.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   kCreateNoWindow,
                                   nullptr,
                                   nullptr,
                                   &startup,
                                   &process);
    CloseHandle(writePipe);
    if (nulInput != nullptr) {
        CloseHandle(nulInput);
    }
    if (ok == 0) {
        result.error = lastErrorMessage("CreateProcessW(schtasks)");
        CloseHandle(readPipe);
        return result;
    }

    char chunk[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, chunk, sizeof(chunk), &read, nullptr) != 0 && read > 0) {
        result.output.append(chunk, static_cast<int>(read));
    }
    const DWORD waited = WaitForSingleObject(process.hProcess, 30000);
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        result.error = QStringLiteral("schtasks timed out after 30 s");
    } else {
        GetExitCodeProcess(process.hProcess, &result.exitCode);
        result.started = true;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    return result;
}

/// 查询任务的 `<Command>`。`*exists` 为 false 表示任务不存在（或读不到）。
bool queryTaskCommand(const QString &taskName, bool *exists, QString *command, QString *error)
{
    *exists = false;
    command->clear();
    const CommandResult result = runSchtasks({QStringLiteral("/Query"),
                                              QStringLiteral("/TN"),
                                              taskName,
                                              QStringLiteral("/XML")});
    if (!result.started) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }
    // 非零退出码既可能是「任务不存在」，也可能是「没权限读」。两者都当作
    // 「读不出它指向谁」处理：调用方按需去注册（注册会给出真正的错误）。
    if (result.exitCode != 0) {
        return true;
    }
    *exists = true;
    if (const auto parsed = taskXmlCommand(decodeTaskOutput(result.output));
        parsed.has_value()) {
        *command = *parsed;
    }
    return true;
}

} // namespace

QString autostartTaskName()
{
    return QStringLiteral("flowkeyd");
}

QString currentExecutablePath()
{
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr,
                                                buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return QString();
        }
        if (length < buffer.size()) {
            return QString::fromWCharArray(buffer.data(), static_cast<int>(length));
        }
        // Win10 起截断时返回缓冲区大小并置 ERROR_INSUFFICIENT_BUFFER。
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) {
            return QString();
        }
    }
}

QString currentUserAccount()
{
    const QString domain = qEnvironmentVariable("USERDOMAIN").trimmed();
    const QString name = qEnvironmentVariable("USERNAME").trimmed();
    if (!domain.isEmpty() && !name.isEmpty()) {
        return domain + QLatin1Char('\\') + name;
    }
    // 退路：`GetUserNameW` 自己就返回 `DOMAIN\user`。
    std::vector<wchar_t> buffer(256);
    DWORD size = static_cast<DWORD>(buffer.size());
    if (GetUserNameW(buffer.data(), &size) == 0) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return QString();
        }
        buffer.resize(size);
        if (GetUserNameW(buffer.data(), &size) == 0) {
            return QString();
        }
    }
    return QString::fromWCharArray(buffer.data());
}

QString buildTaskXml(const AutostartSpec &spec)
{
    const QString exe = xmlEscape(QDir::toNativeSeparators(spec.executable));
    const QString directory = xmlEscape(QDir::toNativeSeparators(spec.workingDirectory));
    const QString user = xmlEscape(spec.userId);
    const int delay = spec.logonDelaySeconds < 0 ? 0 : spec.logonDelaySeconds;

    // 与当年 `scripts/install.ps1` 的 `New-TaskXml()` 逐字段一致：
    //  * `ExecutionTimeLimit=PT0S` —— 默认的 `PT72H` 会让任务计划程序三天后
    //    亲手停掉守护进程（现象：用了三天快捷键突然全失效）；
    //  * 电池那两项显式关掉（默认是开的，笔记本上会「登录后没反应」）；
    //  * 触发器只能是 `LogonTrigger` + `InteractiveToken`：`BootTrigger` 跑在
    //    session 0，那里没有桌面；
    //  * 登录后延迟 15 秒，等 explorer 就绪（本版本不处理 `TaskbarCreated`）；
    //  * `IgnoreNew` + 守护进程自己的单实例互斥体是双保险；
    //  * `RestartOnFailure` 只在异常退出时触发（退出码 0 不会被拉起来）。
    return QStringLiteral(R"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>flowkeyd: Lua-configured keyboard hook daemon, started at logon with the highest privileges (no UAC prompt). Registered by flowkeyd itself.</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>%1</UserId>
      <Delay>PT%2S</Delay>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>%1</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>false</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <WakeToRun>false</WakeToRun>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>5</Priority>
    <RestartOnFailure>
      <Interval>PT1M</Interval>
      <Count>3</Count>
    </RestartOnFailure>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>%3</Command>
      <WorkingDirectory>%4</WorkingDirectory>
    </Exec>
  </Actions>
</Task>
)")
        .arg(user, QString::number(delay), exe, directory);
}

std::optional<QString> taskXmlCommand(const QString &xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("Command")) {
            continue;
        }
        const QString text = reader.readElementText();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return std::nullopt;
}

QString decodeTaskOutput(const QByteArray &bytes)
{
    if (bytes.size() >= 2) {
        const auto first = static_cast<unsigned char>(bytes.at(0));
        const auto second = static_cast<unsigned char>(bytes.at(1));
        if (first == 0xFF && second == 0xFE) {
            const qsizetype units = (bytes.size() - 2) / 2;
            return QString::fromUtf16(
                reinterpret_cast<const char16_t *>(bytes.constData() + 2), units);
        }
        if (first == 0xFE && second == 0xFF) {
            QByteArray swapped = bytes.mid(2);
            for (qsizetype i = 0; i + 1 < swapped.size(); i += 2) {
                std::swap(swapped[i], swapped[i + 1]);
            }
            return QString::fromUtf16(
                reinterpret_cast<const char16_t *>(swapped.constData()),
                swapped.size() / 2);
        }
    }
    if (bytes.startsWith(QByteArray("\xEF\xBB\xBF", 3))) {
        return QString::fromUtf8(bytes.mid(3));
    }
    QStringDecoder utf8(QStringConverter::Utf8);
    const QString text = utf8(bytes);
    if (!utf8.hasError()) {
        return text;
    }
    // 既不是 UTF-8 也不是 BOM 开头的：schtasks 写的是控制台代码页。
    return QString::fromLocal8Bit(bytes);
}

bool sameExecutablePath(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    const QString left = QDir::cleanPath(QDir::toNativeSeparators(a));
    const QString right = QDir::cleanPath(QDir::toNativeSeparators(b));
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool queryAutostartTask(const QString &taskName,
                        const QString &executable,
                        AutostartState *state,
                        QString *error)
{
    if (state != nullptr) {
        *state = AutostartState::Absent;
    }
    bool exists = false;
    QString command;
    if (!queryTaskCommand(taskName, &exists, &command, error)) {
        return false;
    }
    if (!exists) {
        return true;
    }
    if (state != nullptr) {
        *state = sameExecutablePath(command, executable) ? AutostartState::Matches
                                                        : AutostartState::Different;
    }
    return true;
}

bool registerAutostartTask(const AutostartSpec &spec, QString *error)
{
    if (spec.executable.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the executable path is empty");
        }
        return false;
    }
    const QString xml = buildTaskXml(spec);

    // `schtasks /Create /XML` 只认文件，不接受 stdin，所以过一手临时文件。
    // 编码照抄 install.ps1：UTF-16LE + BOM（XML 声明里写的就是 UTF-16）。
    QByteArray encoded;
    encoded.reserve(xml.size() * 2 + 2);
    encoded.append(static_cast<char>(0xFF));
    encoded.append(static_cast<char>(0xFE));
    for (const QChar ch : xml) {
        const ushort unit = ch.unicode();
        encoded.append(static_cast<char>(unit & 0xFF));
        encoded.append(static_cast<char>((unit >> 8) & 0xFF));
    }

    const QString path = QDir::tempPath()
        + QStringLiteral("/flowkeyd-task-%1.xml").arg(GetCurrentProcessId());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    const bool written = file.write(encoded) == encoded.size();
    file.close();
    if (!written) {
        QFile::remove(path);
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    const CommandResult result = runSchtasks({QStringLiteral("/Create"),
                                              QStringLiteral("/TN"),
                                              spec.taskName,
                                              QStringLiteral("/XML"),
                                              path,
                                              QStringLiteral("/F")});
    QFile::remove(path);
    if (!result.started) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }
    if (result.exitCode != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("schtasks /Create failed (exit %1): %2")
                         .arg(result.exitCode)
                         .arg(decodeTaskOutput(result.output).trimmed());
        }
        return false;
    }
    return true;
}

bool removeAutostartTask(const QString &taskName, QString *error)
{
    const CommandResult result = runSchtasks({QStringLiteral("/Delete"),
                                              QStringLiteral("/TN"),
                                              taskName,
                                              QStringLiteral("/F")});
    if (!result.started) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }
    if (result.exitCode == 0) {
        return true;
    }
    // 删除失败可能只是「本来就没有这个任务」。查一次：真不存在就当成功。
    bool exists = false;
    QString command;
    QString queryError;
    if (queryTaskCommand(taskName, &exists, &command, &queryError) && !exists) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("schtasks /Delete failed (exit %1): %2")
                     .arg(result.exitCode)
                     .arg(decodeTaskOutput(result.output).trimmed());
    }
    return false;
}

void ensureAutostart(const AutostartSpec &spec)
{
    AutostartState state = AutostartState::Absent;
    QString error;
    if (!queryAutostartTask(spec.taskName, spec.executable, &state, &error)) {
        logWarn(QStringLiteral("could not read the logon autostart task `%1`: %2")
                    .arg(spec.taskName, error));
        return;
    }
    if (state == AutostartState::Matches) {
        logDebug(QStringLiteral("logon autostart task `%1` already points at %2")
                     .arg(spec.taskName, QDir::toNativeSeparators(spec.executable)));
        return;
    }
    const bool wasAbsent = state == AutostartState::Absent;
    if (!registerAutostartTask(spec, &error)) {
        logWarn(QStringLiteral("could not register the logon autostart task `%1` (%2): %3")
                    .arg(spec.taskName,
                         wasAbsent ? QStringLiteral("not registered yet")
                                   : QStringLiteral("pointed at a different executable"),
                         error));
        return;
    }
    logInfo(QStringLiteral("logon autostart task `%1` %2: %3 starts at every logon with "
                           "the highest privileges")
                .arg(spec.taskName,
                     wasAbsent ? QStringLiteral("registered") : QStringLiteral("updated"),
                     QDir::toNativeSeparators(spec.executable)));
}

} // namespace flowkeyd::platform::win
