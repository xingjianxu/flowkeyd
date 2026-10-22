#include "platform/win/freeze.h"

#include "platform/win/dwm.h"
#include "platform/win/logging.h"

#include <algorithm>

namespace flowkeyd::platform::win::freeze {

namespace {

constexpr const wchar_t *kClassName = L"FlowkeydFreezeWindow";

/// 注册窗口类（只成功注册一次；`ERROR_CLASS_ALREADY_EXISTS` 也算成功）。
bool ensureClass(HINSTANCE instance, QString *error)
{
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        if (message == WM_ERASEBKGND) {
            // 不擦背景：擦一下就是一闪白，正好是要避开的东西。
            return 1;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            auto *self = reinterpret_cast<FreezeFrame *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self != nullptr) {
                self->paint(dc);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    };
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = kClassName;
    if (RegisterClassExW(&cls) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error != nullptr) {
            *error = QStringLiteral("RegisterClassExW failed: %1")
                         .arg(winErrorMessage(GetLastError()));
        }
        return false;
    }
    registered = true;
    return true;
}

} // namespace

FreezeFrame::FreezeFrame() = default;

FreezeFrame::~FreezeFrame()
{
    release();
}

bool FreezeFrame::cover(const RECT &area, QString *error)
{
    const auto fail = [this, error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        release();
        return false;
    };
    release();
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (width <= 0 || height <= 0) {
        return fail(QStringLiteral("the area to freeze is empty"));
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!ensureClass(instance, error)) {
        return false;
    }

    // 先把屏幕内容抓进 DIB：抓的时候遮罩窗口还不存在，所以快照就是用户此刻看到的。
    const HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return fail(QStringLiteral("GetDC(nullptr) failed: %1")
                        .arg(winErrorMessage(GetLastError())));
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // 自上而下
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    m_bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    m_memory = CreateCompatibleDC(screen);
    if (m_bitmap == nullptr || m_memory == nullptr) {
        ReleaseDC(nullptr, screen);
        return fail(QStringLiteral("could not create the snapshot surface: %1")
                        .arg(winErrorMessage(GetLastError())));
    }
    SelectObject(m_memory, m_bitmap);
    m_width = width;
    m_height = height;
    // `CAPTUREBLT`：连分层窗口一起抓（否则快照里会缺一块，比如我们的卡片）。
    if (BitBlt(m_memory, 0, 0, width, height, screen, area.left, area.top,
               SRCCOPY | CAPTUREBLT)
        == 0) {
        ReleaseDC(nullptr, screen);
        return fail(QStringLiteral("BitBlt failed: %1").arg(winErrorMessage(GetLastError())));
    }
    ReleaseDC(nullptr, screen);

    m_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE
                                   | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                               kClassName, L"flowkeyd freeze", WS_POPUP, area.left, area.top,
                               width, height, nullptr, nullptr, instance, nullptr);
    if (m_window == nullptr) {
        return fail(QStringLiteral("CreateWindowExW failed: %1")
                        .arg(winErrorMessage(GetLastError())));
    }
    SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    // 分层窗口本身是“全透明”的，这里把它设成**不透明**：只是为了满足“点穿”需要
    // 分层样式（`WS_EX_TRANSPARENT` + 分层 = 鼠标事件直接落到下面的窗口上）。
    SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);

    // 遮罩自己也不能播过渡动画（否则刚盖上去的那一下会淡入，旧画面先糊一下）。
    dwm::TransitionGuard guard(m_window, false);
    if (SetWindowPos(m_window, HWND_TOPMOST, area.left, area.top, width, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW)
        == 0) {
        return fail(QStringLiteral("SetWindowPos(freeze) failed: %1")
                        .arg(winErrorMessage(GetLastError())));
    }
    // 别等腰下一次无效化：这条线程下面马上要显隐窗口，用户不能先看到一眼白。
    RedrawWindow(m_window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    return true;
}

bool FreezeFrame::coverWindows(const std::vector<HWND> &windows, QString *error)
{
    RECT area{};
    bool any = false;
    for (const HWND hwnd : windows) {
        if (hwnd == nullptr || IsWindow(hwnd) == 0) {
            continue;
        }
        RECT rect{};
        if (GetWindowRect(hwnd, &rect) == 0) {
            continue;
        }
        if (!any) {
            area = rect;
            any = true;
            continue;
        }
        area.left = std::min(area.left, rect.left);
        area.top = std::min(area.top, rect.top);
        area.right = std::max(area.right, rect.right);
        area.bottom = std::max(area.bottom, rect.bottom);
    }
    if (!any) {
        if (error != nullptr) {
            *error = QStringLiteral("none of the windows has a usable rectangle");
        }
        return false;
    }
    return cover(area, error);
}

void FreezeFrame::paint(void *dc)
{
    if (m_memory == nullptr || dc == nullptr) {
        return;
    }
    BitBlt(static_cast<HDC>(dc), 0, 0, m_width, m_height, m_memory, 0, 0, SRCCOPY);
}

void FreezeFrame::release()
{
    if (m_window != nullptr) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_memory != nullptr) {
        DeleteDC(m_memory);
        m_memory = nullptr;
    }
    if (m_bitmap != nullptr) {
        DeleteObject(m_bitmap);
        m_bitmap = nullptr;
    }
    m_bits = nullptr;
    m_width = 0;
    m_height = 0;
}

} // namespace flowkeyd::platform::win::freeze
