#include "platform/win/session.h"

#include "platform/win/logging.h"

#include <utility>

namespace flowkeyd::platform::win {

namespace {

/// 窗口类名。带前缀避免和任何别的窗口类撞上。
constexpr wchar_t kWindowClass[] = L"flowkeyd-session-end";

} // namespace

SessionEndWatcher::~SessionEndWatcher()
{
    stop();
}

bool SessionEndWatcher::start(std::function<void()> onSessionEnd, QString *error)
{
    if (m_window != nullptr) {
        m_onSessionEnd = std::move(onSessionEnd);
        return true;
    }
    m_onSessionEnd = std::move(onSessionEnd);

    const HINSTANCE instance = moduleHandle();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &SessionEndWatcher::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error != nullptr) {
            *error = lastErrorMessage("RegisterClassExW(session end)");
        }
        return false;
    }
    // 一个不可见的 `WS_POPUP`：不进任务栏、不抢焦点，纯粹用来收会话消息。
    m_window = CreateWindowExW(WS_EX_TOOLWINDOW,
                               kWindowClass,
                               L"flowkeyd session end",
                               WS_POPUP,
                               0,
                               0,
                               0,
                               0,
                               nullptr,
                               nullptr,
                               instance,
                               this);
    if (m_window == nullptr) {
        if (error != nullptr) {
            *error = lastErrorMessage("CreateWindowExW(session end)");
        }
        UnregisterClassW(kWindowClass, instance);
        return false;
    }
    return true;
}

void SessionEndWatcher::stop()
{
    // 先丢回调再销毁窗口：`DestroyWindow` 会同步派发 `WM_DESTROY` / `WM_NCDESTROY`，
    // 那条路上不该还留着一个可能已经失效的捕获（例如已经析构的 `Runtime`）。
    m_onSessionEnd = nullptr;
    if (m_window != nullptr) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    UnregisterClassW(kWindowClass, moduleHandle());
}

void SessionEndWatcher::notify()
{
    if (!m_onSessionEnd) {
        return;
    }
    m_onSessionEnd();
}

LRESULT CALLBACK SessionEndWatcher::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *self = reinterpret_cast<SessionEndWatcher *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        self = static_cast<SessionEndWatcher *>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self != nullptr) {
        if (message == WM_QUERYENDSESSION) {
            // 恢复被工作空间藏起来的窗口，然后放行（返回 FALSE 会拦住用户的关机）。
            logInfo(QStringLiteral("session is ending; restoring the windows hidden by workspaces"));
            self->notify();
            return TRUE;
        }
        if (message == WM_ENDSESSION && wParam != 0) {
            // 第二次机会：`WM_QUERYENDSESSION` 那一次如果被别人拖过了，这里再补一发
            // （恢复是幂等的：没有窗口藏着时什么都不做）。
            self->notify();
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace flowkeyd::platform::win
