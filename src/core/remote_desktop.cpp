#include "core/remote_desktop.h"

#include "core/window_match.h"

namespace flowkeyd::core {

const QStringList &builtinRemoteDesktopProcesses()
{
    // 微软自己的 RDP 客户端，按“经典 → 新”的顺序：
    //   * `mstsc.exe`            —— 远程桌面连接（内置）
    //   * `msrdc.exe` / `msrdcw.exe` —— 「Windows App」（Remote Desktop，MSI 版）
    //   * `RdClient.Windows.exe` —— 商店版 Remote Desktop
    // 名字写小写只是为了和仓库里别处的进程名风格一致（匹配是大小写无关的）。
    static const QStringList names{
        QStringLiteral("mstsc.exe"),
        QStringLiteral("msrdc.exe"),
        QStringLiteral("msrdcw.exe"),
        QStringLiteral("rdclient.windows.exe"),
    };
    return names;
}

bool isRemoteDesktopProcess(const std::optional<QString> &executableName,
                            const QStringList &processes)
{
    for (const QString &process : processes) {
        // 空串在 `windowProcessMatches()` 里是「不限制」的意思，这里必须跳过它：
        // 否则名单里混进一个空串就会让**任何**前台窗口都算远程桌面。
        // （`settings.remote_desktop.processes` 里的空串在加载时就会报错，
        // 这一句只是不依赖校验的兜底。）
        if (process.isEmpty()) {
            continue;
        }
        if (windowProcessMatches(executableName, process)) {
            return true;
        }
    }
    return false;
}

} // namespace flowkeyd::core
