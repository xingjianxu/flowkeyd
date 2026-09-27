#include "platform/win/update.h"

#include "platform/win/elevate.h"
#include "platform/win/ffi.h"

#include <QDir>
#include <QFileInfo>

#include <vector>

namespace flowkeyd::platform::win {

namespace {

constexpr DWORD kCreateNoWindow = 0x08000000u;
constexpr DWORD kCreateNewProcessGroup = 0x00000200u;

/// 新实例启动之后等这么久。缺 DLL、命令行不对的进程会在这之前就退出，
/// 那时候必须回滚；正常的守护进程（装钩子、建托盘、跑事件循环）不会。
constexpr DWORD kStartupGraceMs = 2500;

QString backupPath(const QString &executablePath)
{
    return executablePath + QStringLiteral(".old");
}

bool moveFile(const QString &from, const QString &to, QString *error)
{
    const std::wstring fromText = from.toStdWString();
    const std::wstring toText = to.toStdWString();
    if (MoveFileExW(fromText.c_str(), toText.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("could not rename %1 -> %2: %3")
                     .arg(QDir::toNativeSeparators(from),
                          QDir::toNativeSeparators(to),
                          winErrorMessage(GetLastError()));
    }
    return false;
}

/// 把已经动过的文件放回去：新 exe 挪回 staged，`.old` 挪回目标路径。
/// 尽力而为 —— 回滚失败也要把真正的失败原因留给调用方。
void rollback(const QString &target, const QString &staged)
{
    const QString backup = backupPath(target);
    if (QFileInfo::exists(target)) {
        const std::wstring fromText = target.toStdWString();
        const std::wstring toText = staged.toStdWString();
        MoveFileExW(fromText.c_str(), toText.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    if (QFileInfo::exists(backup)) {
        const std::wstring fromText = backup.toStdWString();
        const std::wstring toText = target.toStdWString();
        MoveFileExW(fromText.c_str(), toText.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

} // namespace

bool applyExecutableUpdate(const UpdateInstallPlan &plan, QString *error)
{
    const QFileInfo targetInfo(plan.targetExecutable);
    if (!targetInfo.exists()) {
        if (error != nullptr) {
            *error = QStringLiteral("the running executable %1 does not exist")
                         .arg(QDir::toNativeSeparators(plan.targetExecutable));
        }
        return false;
    }
    if (!QFileInfo::exists(plan.stagedExecutable)) {
        if (error != nullptr) {
            *error = QStringLiteral("the downloaded executable %1 does not exist")
                         .arg(QDir::toNativeSeparators(plan.stagedExecutable));
        }
        return false;
    }

    const QString backup = backupPath(plan.targetExecutable);
    // 上一次更新留下的备份（新实例没能删掉时）：现在删，否则下一步的改名会失败。
    if (QFileInfo::exists(backup)) {
        const std::wstring backupText = backup.toStdWString();
        DeleteFileW(backupText.c_str());
    }

    // 1) 正在运行的 exe → `.old`。运行中的映像可以改名（不能删/覆盖）。
    if (!moveFile(plan.targetExecutable, backup, error)) {
        return false;
    }
    // 2) 下载好的新 exe → 目标路径。
    if (!moveFile(plan.stagedExecutable, plan.targetExecutable, error)) {
        const QString message = error != nullptr ? *error : QString();
        rollback(plan.targetExecutable, plan.stagedExecutable);
        if (error != nullptr) {
            *error = QStringLiteral("%1 (the previous executable was restored)").arg(message);
        }
        return false;
    }

    // 3) 启动新实例。参数原样转发，只多一个 `--updated-from <旧版本>`：
    //    新进程用它来决定重启之后给用户弹一条「更新成功」的通知。
    QString commandLine = quoteArg(plan.targetExecutable);
    for (const QString &arg : plan.restartArgs) {
        commandLine += QLatin1Char(' ');
        commandLine += quoteArg(arg);
    }
    if (!plan.currentVersion.isEmpty()) {
        commandLine += QLatin1String(" --updated-from ");
        commandLine += quoteArg(plan.currentVersion);
    }
    std::vector<wchar_t> commandBuffer(commandLine.utf16(),
                                       commandLine.utf16() + commandLine.size());
    commandBuffer.push_back(L'\0');

    const QString directory =
        QDir::toNativeSeparators(QFileInfo(plan.targetExecutable).absolutePath());
    std::vector<wchar_t> directoryBuffer(directory.utf16(),
                                         directory.utf16() + directory.size());
    directoryBuffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr,
                                        commandBuffer.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        kCreateNewProcessGroup | kCreateNoWindow,
                                        nullptr,
                                        directoryBuffer.data(),
                                        &startup,
                                        &process);
    if (started == 0) {
        const QString message =
            QStringLiteral("could not start %1: %2")
                .arg(QDir::toNativeSeparators(plan.targetExecutable), winErrorMessage(GetLastError()));
        rollback(plan.targetExecutable, plan.stagedExecutable);
        if (error != nullptr) {
            *error = QStringLiteral("%1 (the previous executable was restored)").arg(message);
        }
        return false;
    }

    // 4) 等一会儿看它有没有当场退出：新 exe 少了某个 DLL 时 CreateProcess 照样
    //    成功，但进程会立刻死掉。这时必须回滚，否则用户手上就没有能用的程序了。
    const DWORD wait = WaitForSingleObject(process.hProcess, kStartupGraceMs);
    if (wait == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        rollback(plan.targetExecutable, plan.stagedExecutable);
        if (error != nullptr) {
            *error = QStringLiteral("the restarted flowkeyd exited immediately (code %1); the "
                                    "previous executable was restored")
                         .arg(exitCode);
        }
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool removeExecutableBackup(const QString &executablePath)
{
    const QString backup = backupPath(executablePath);
    if (!QFileInfo::exists(backup)) {
        return true;
    }
    const std::wstring backupText = backup.toStdWString();
    return DeleteFileW(backupText.c_str()) != 0;
}

} // namespace flowkeyd::platform::win
