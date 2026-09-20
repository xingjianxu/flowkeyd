#include "platform/win/console.h"

#include <QByteArray>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

namespace flowkeyd::platform::win {

namespace {

bool handleIsUsable(HANDLE handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

void reopenStandardHandle(DWORD which)
{
    if (handleIsUsable(GetStdHandle(which))) {
        return;
    }
    HANDLE console = CreateFileW(L"CONOUT$",
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr,
                                 OPEN_EXISTING,
                                 0,
                                 nullptr);
    if (handleIsUsable(console)) {
        SetStdHandle(which, console);
    }
}

void write(HANDLE handle, const QString &text)
{
    if (!handleIsUsable(handle) || text.isEmpty()) {
        return;
    }
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) != 0) {
        // 真控制台：走宽字符接口，中文才不会变成乱码。
        const DWORD length = static_cast<DWORD>(text.size());
        DWORD written = 0;
        WriteConsoleW(handle,
                      reinterpret_cast<const wchar_t *>(text.utf16()),
                      length,
                      &written,
                      nullptr);
        return;
    }
    // 被重定向到文件/管道：写 UTF-8。
    const QByteArray utf8 = text.toUtf8();
    DWORD written = 0;
    WriteFile(handle, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

} // namespace

bool attachParentConsole()
{
    if (GetConsoleWindow() != nullptr) {
        return true;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return false;
    }
    reopenStandardHandle(STD_OUTPUT_HANDLE);
    reopenStandardHandle(STD_ERROR_HANDLE);
    return true;
}

void writeStdout(const QString &text)
{
    write(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void writeStderr(const QString &text)
{
    write(GetStdHandle(STD_ERROR_HANDLE), text);
}

uint consoleIsOwned()
{
    DWORD processes[4] = {};
    const DWORD count = GetConsoleProcessList(processes, 4);
    // 只有本进程时才归我们所有，否则隐藏它会动到启动我们的 shell。
    return count;
}

bool hideConsoleWindow()
{
    if (consoleIsOwned() > 1) {
        return false;
    }
    HWND window = GetConsoleWindow();
    if (window == nullptr) {
        return false;
    }
    ShowWindow(window, SW_HIDE);
    return true;
}

} // namespace flowkeyd::platform::win
