#include "core/switch_cover.h"

namespace flowkeyd::core {

namespace {

/// Chromium 系（Chrome / Edge / Electron / VSCode / Discord …）共用的顶层窗口类。
/// 类名一个就够：Electron 应用全都用它，`Chrome_WidgetWin_0` 是隐藏的消息窗口、
/// 不是主窗口，不必管。
constexpr char kChromiumClass[] = "Chrome_WidgetWin_1";

/// 类名不一样、但同样自带合成器的程序。**列表只放实测过会闪的**：M11 那次逐帧
/// 探针（`tmp/wsflicker/`）与 M12 的复测（`tmp/wsflashfix/`）里，Chrome / Edge /
/// VSCode / WezTerm 会闪，记事本这类纯 GDI 程序不会。发现新的加上来即可。
constexpr const char *kFlickerProcesses[] = {
    // Chromium 系（类名已经能认出来，放这里只是为了类名被改掉时还有一层兜底）。
    "chrome.exe",
    "msedge.exe",
    "msedgewebview2.exe",
    "brave.exe",
    "vivaldi.exe",
    "opera.exe",
    "chromium.exe",
    // Electron 系里不带 Chromium 类名的（一般都有，兜底而已）。
    "electron.exe",
    "code.exe",
    "wezterm-gui.exe",
    "wezterm.exe",
};

} // namespace

bool flickersWhenRevealed(const RevealTraits &traits)
{
    const QString className = traits.className.trimmed();
    if (className.compare(QString::fromLatin1(kChromiumClass), Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QString processName = traits.processName.trimmed();
    if (processName.isEmpty()) {
        return false;
    }
    for (const char *name : kFlickerProcesses) {
        if (processName.compare(QString::fromLatin1(name), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool needsCoverForReveal(const std::vector<RevealTraits> &windows)
{
    for (const RevealTraits &traits : windows) {
        if (flickersWhenRevealed(traits)) {
            return true;
        }
    }
    return false;
}

} // namespace flowkeyd::core
