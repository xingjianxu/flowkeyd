#include "platform/win/monitor.h"

namespace flowkeyd::platform::win::monitor {

namespace {

core::Rect toRect(const RECT &rect)
{
    return core::Rect{static_cast<int>(rect.left),
                      static_cast<int>(rect.top),
                      static_cast<int>(rect.right - rect.left),
                      static_cast<int>(rect.bottom - rect.top)};
}

BOOL CALLBACK enumMonitorProc(HMONITOR handle, HDC, LPRECT, LPARAM data)
{
    auto *out = reinterpret_cast<std::vector<core::MonitorDescription> *>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(handle, &info) == 0) {
        return TRUE;
    }
    core::MonitorDescription description;
    description.device = QString::fromWCharArray(info.szDevice);
    description.bounds = toRect(info.rcMonitor);
    description.work = toRect(info.rcWork);
    description.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    out->push_back(std::move(description));
    return TRUE;
}

} // namespace

std::vector<core::MonitorDescription> list()
{
    std::vector<core::MonitorDescription> out;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorProc, reinterpret_cast<LPARAM>(&out));
    return out;
}

std::optional<std::size_t> indexForWindow(const std::vector<core::MonitorDescription> &sorted,
                                          HWND hwnd)
{
    const HMONITOR handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (handle == nullptr) {
        return std::nullopt;
    }
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(handle, &info) == 0) {
        return std::nullopt;
    }
    const QString device = core::normalizeDeviceName(QString::fromWCharArray(info.szDevice));
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (core::normalizeDeviceName(sorted[index].device) == device) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<core::Rect> windowRect(HWND hwnd)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement) != 0) {
        const RECT &rect = placement.rcNormalPosition;
        return core::Rect{static_cast<int>(rect.left),
                          static_cast<int>(rect.top),
                          static_cast<int>(rect.right - rect.left),
                          static_cast<int>(rect.bottom - rect.top)};
    }
    RECT rect{};
    if (GetWindowRect(hwnd, &rect) == 0) {
        return std::nullopt;
    }
    return toRect(rect);
}

bool applyPlacement(HWND hwnd, const core::Rect &rect, bool maximize, QString *error)
{
    if (IsWindow(hwnd) == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("the window is gone");
        }
        return false;
    }
    const bool minimized = IsIconic(hwnd) != 0;
    const HWND foreground = GetForegroundWindow();

    // 最大化的窗口不能直接 SetWindowPos：先还原，摆好位置再重新最大化。
    if (!minimized && IsZoomed(hwnd) != 0) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement) == 0) {
        if (error != nullptr) {
            *error = lastErrorMessage("GetWindowPlacement");
        }
        return false;
    }
    placement.rcNormalPosition =
        RECT{static_cast<LONG>(rect.x),
             static_cast<LONG>(rect.y),
             static_cast<LONG>(rect.x + rect.width),
             static_cast<LONG>(rect.y + rect.height)};
    if (minimized) {
        placement.showCmd = SW_SHOWMINIMIZED;
    } else if (maximize) {
        placement.showCmd = SW_SHOWMAXIMIZED;
    } else {
        placement.showCmd = SW_SHOWNORMAL;
    }
    if (SetWindowPlacement(hwnd, &placement) == 0) {
        if (error != nullptr) {
            *error = lastErrorMessage("SetWindowPlacement");
        }
        return false;
    }

    // `SetWindowPlacement` 用的是“工作区坐标”（历史上与屏幕坐标有过差异），
    // 非最大化时再用 `SetWindowPos`（明确是屏幕坐标、且不激活）钉一次。
    if (!minimized && !maximize) {
        SetWindowPos(hwnd, nullptr, rect.x, rect.y, rect.width, rect.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (!minimized && maximize && IsZoomed(hwnd) == 0) {
        // 某些固定大小的窗口拒绝进入最大化；退化成“铺满工作区”，至少位置是对的。
        SetWindowPos(hwnd, nullptr, rect.x, rect.y, rect.width, rect.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // `SetWindowPlacement` 一般不激活窗口；万一它把焦点抢走了就还回去。
    if (foreground != nullptr && foreground != hwnd && GetForegroundWindow() != foreground) {
        SetForegroundWindow(foreground);
    }
    return true;
}

} // namespace flowkeyd::platform::win::monitor
