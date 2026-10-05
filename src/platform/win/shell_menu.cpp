// 见 `shell_menu.h` 的说明：弹出一个 shell 条目（`shell:AppsFolder\<AUMID>` /
// `.lnk` / 文件）的**原生 shell 右键菜单**并执行用户选中的那一条。
//
// 这一份是「自己弹 shell 菜单」的标准做法（Raymond Chen 的 Hosting the shell
// context menu 那一套），三件事按顺序做：
//   1. 用 `SHParseDisplayName` + `SHBindToParent` 把路径换成「父文件夹 + 子 PIDL」，
//      再 `IShellFolder::GetUIObjectOf(IID_IContextMenu)` 拿到那个 item 的菜单对象；
//   2. `QueryContextMenu` 把条目填进一个 `HMENU`，`TrackPopupMenuEx` 显示它；
//   3. 用户选了哪一条就 `InvokeCommand` 哪一条 —— 动作本身全部由 shell 执行，
//      我们连「有哪些条目」都不需要知道。
//
// 唯一真正麻烦的是第 2 步里的**消息转发**：`QueryContextMenu` 生成的是 shell 的
// 菜单，里面可能有子菜单和自绘条目，`TrackPopupMenuEx` 会把
// `WM_INITMENUPOPUP` / `WM_DRAWITEM` / `WM_MEASUREITEM` / `WM_MENUCHAR` 送给
// 菜单的**拥有窗口**（也就是启动器卡片那个窗口），必须把它们转给
// `IContextMenu2/3`，否则子菜单是空的、条目画不出来、加速键也不生效。卡片是
// **Qt 的**窗口，所以只能在菜单开着的那一小段时间里临时换掉它的窗口过程
// （`MenuMessageRouter`），用完之后还原。
#include "platform/win/shell_menu.h"

#include "platform/win/ffi.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstring>
#include <utility>

namespace flowkeyd::platform::win::shell_menu {

namespace {

/// 我们自己给 shell 菜单命令编的号：`QueryContextMenu` 从 `idCmdFirst` 开始往后
/// 排，返回的命令号减掉它才是「第几条」（`InvokeCommand` 要的就是这个偏移）。
constexpr UINT kCommandFirst = 1;
constexpr UINT kCommandLast = 0x7FFF;

/// `DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED`（Win10 1809+）。MinGW 的头文件里
/// 没有这个宏，而它只是一个约定好的负数句柄值，所以自己写一份。
constexpr std::intptr_t kContextUnawareGdiScaled = -5;

/// `SetThreadDpiAwarenessContext` 的函数指针类型（下面那个类里运行时解析它）。
using SetDpiAwarenessContextFn = DPI_AWARENESS_CONTEXT(WINAPI *)(DPI_AWARENESS_CONTEXT);

/// 菜单 DPI 的现场。本进程（Qt 6）是 **Per-Monitor V2** 感知的，而 shell 自己那套
/// `IContextMenu` 是按「宿主线程的 DPI 上下文」决定菜单尺寸的 —— 直接弹出来在
/// 200% 缩放下会明显偏小（而且本来就是这么设计的：菜单窗口归调用方的线程管）。
/// 微软给 PMv2 应用的做法是把**当前线程**临时降级成 `UNAWARE_GDISCALED`（系统按
/// 比例缩放菜单，就像 shell 自己弹菜单那样），菜单收掉之后再还原。
///
/// 降级期间 `GetCursorPos()` 之类的坐标也是「虚拟化的」那一套，与
/// `TrackPopupMenuEx` 要的一样 —— 所以取光标位置的那一句必须写在作用域**里面**。
/// 拿不到这个入口（Win10 1607 之前）时照样弹菜单，只是缩放下可能不好看，不算错误。
class ScopedGdiScaledDpiContext
{
public:
    ScopedGdiScaledDpiContext()
    {
        using Setter = SetDpiAwarenessContextFn;
        static Setter setter = resolveSetter();
        m_setter = setter;
        if (m_setter == nullptr) {
            return;
        }
        m_previous = m_setter(reinterpret_cast<DPI_AWARENESS_CONTEXT>(kContextUnawareGdiScaled));
        m_applied = true;
    }

    ~ScopedGdiScaledDpiContext()
    {
        if (m_applied) {
            m_setter(m_previous);
        }
    }

    ScopedGdiScaledDpiContext(const ScopedGdiScaledDpiContext &) = delete;
    ScopedGdiScaledDpiContext &operator=(const ScopedGdiScaledDpiContext &) = delete;

private:
    static SetDpiAwarenessContextFn resolveSetter()
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 == nullptr) {
            return nullptr;
        }
        FARPROC proc = GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        if (proc == nullptr) {
            return nullptr;
        }
        // `FARPROC` → 具体函数指针直接 cast 会被 `-Wcast-function-type` 拦下
        // （本仓库 `-Werror`），所以按 AGENTS.md 第 10 节用 `std::memcpy` 绕开。
        SetDpiAwarenessContextFn setter = nullptr;
        static_assert(sizeof(setter) == sizeof(proc), "function pointer sizes");
        std::memcpy(&setter, &proc, sizeof(setter));
        return setter;
    }

    SetDpiAwarenessContextFn m_setter = nullptr;
    DPI_AWARENESS_CONTEXT m_previous = nullptr;
    bool m_applied = false;
};

/// 把 shell 菜单要的那几条消息转给 `IContextMenu2/3`。
///
/// 做法是临时替换拥有窗口的窗口过程（`SetWindowLongPtrW(GWLP_WNDPROC)`）：菜单
/// 开着的时候，凡是菜单相关的消息都先给 shell，剩下的原样交回 Qt 原来的过程。
/// 用 `thread_local` 找当前这一份而不是 `GWLP_USERDATA`：那张卡片是 **Qt 的**窗口，
/// 不该去动它留给自己用的字段；而弹菜单这个调用是**阻塞**的，一条线程同时只会有
/// 一份，所以一份 thread-local 就够。
class MenuMessageRouter
{
public:
    MenuMessageRouter(HWND owner, IContextMenu2 *menu2, IContextMenu3 *menu3)
        : m_owner(owner), m_menu2(menu2), m_menu3(menu3)
    {
    }

    ~MenuMessageRouter() { remove(); }

    MenuMessageRouter(const MenuMessageRouter &) = delete;
    MenuMessageRouter &operator=(const MenuMessageRouter &) = delete;

    /// 装不上（窗口句柄不对）时返回 false —— 菜单照样能弹，只是子菜单 / 自绘
    /// 条目可能不对劲，所以调用方只把它当“降级”。
    bool install()
    {
        if (m_owner == nullptr) {
            return false;
        }
        // 先把当前这份挂上再换窗口过程：反过来的话，换完之后到 `active` 就位之前
        // 进来的消息会落到窗口过程里的空指针分支上。
        active = this;
        previous = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(m_owner, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(&MenuMessageRouter::windowProc)));
        if (previous == nullptr) {
            active = nullptr;
            return false;
        }
        m_installed = true;
        return true;
    }

    void remove()
    {
        if (!m_installed) {
            return;
        }
        // 只有现在挂着的还是我们那一个才还原：窗口要是被 Qt 重建过（新窗口过程
        // 与我们无关），把别人的过程盖回去会更糟。还原完再清 `active` —— 反过来
        // 的话，换回去之前进来的最后几条消息就没人接了。
        WNDPROC current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(m_owner, GWLP_WNDPROC));
        if (current == &MenuMessageRouter::windowProc) {
            SetWindowLongPtrW(m_owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        }
        active = nullptr;
        m_installed = false;
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        MenuMessageRouter *router = active;
        if (router != nullptr && router->m_owner == hwnd) {
            LRESULT result = 0;
            if (router->forward(message, wparam, lparam, &result)) {
                return result;
            }
            return CallWindowProcW(router->previous, hwnd, message, wparam, lparam);
        }
        // 理论上到不了这儿（装的过程只挂在 `m_owner` 上）；真到了就交给默认处理，
        // 至少不会把消息吞掉。
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    /// 转发成功（shell 处理了这条消息）时返回 true，结果写进 `*result`。
    bool forward(UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
    {
        switch (message) {
        case WM_INITMENUPOPUP: // 子菜单展开（shell 在这里填条目）
        case WM_DRAWITEM:      // 自绘条目（图标那一栏）
        case WM_MEASUREITEM:   // 自绘条目的尺寸
        case WM_MENUCHAR:      // 菜单里的字母加速键
        case WM_COMPAREITEM:
            break;
        default:
            return false;
        }
        if (m_menu3 != nullptr) {
            LRESULT value = 0;
            if (SUCCEEDED(m_menu3->HandleMenuMsg2(message, wparam, lparam, &value))) {
                *result = value;
                return true;
            }
            return false;
        }
        if (m_menu2 != nullptr && SUCCEEDED(m_menu2->HandleMenuMsg(message, wparam, lparam))) {
            *result = 0;
            return true;
        }
        return false;
    }

    HWND m_owner = nullptr;
    IContextMenu2 *m_menu2 = nullptr;
    IContextMenu3 *m_menu3 = nullptr;
    WNDPROC previous = nullptr;
    bool m_installed = false;

    /// 当前线程正在弹的那一份（见类注释里为什么用 thread_local）。
    static thread_local MenuMessageRouter *active;
};

thread_local MenuMessageRouter *MenuMessageRouter::active = nullptr;

/// 一个小小的 RAII：把一路上拿到的 COM / PIDL / HMENU 在函数退出时都放掉。
///
/// 单独写一个是因为这里的清理点有六七个，散着写很容易在某个提前 return 上漏一个
/// （`IContextMenu` 漏了 Release 就是一次泄漏，PIDL 漏了就是一块 CoTaskMem）。
struct ShellMenuResources
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    IShellFolder *folder = nullptr;
    IContextMenu *menu = nullptr;
    IContextMenu2 *menu2 = nullptr;
    IContextMenu3 *menu3 = nullptr;
    HMENU hmenu = nullptr;

    ~ShellMenuResources()
    {
        if (hmenu != nullptr) {
            DestroyMenu(hmenu);
        }
        if (menu3 != nullptr) {
            menu3->Release();
        }
        if (menu2 != nullptr) {
            menu2->Release();
        }
        if (menu != nullptr) {
            menu->Release();
        }
        if (folder != nullptr) {
            folder->Release();
        }
        if (pidl != nullptr) {
            CoTaskMemFree(pidl);
        }
    }

    ShellMenuResources() = default;
    ShellMenuResources(const ShellMenuResources &) = delete;
    ShellMenuResources &operator=(const ShellMenuResources &) = delete;
};

/// `QueryInterface` 拿 `IContextMenu3`（拿不到再退 `IContextMenu2`）：3 是 2 的
/// 超集（`HandleMenuMsg2` 能回结果，`WM_MENUCHAR` 需要它），有就用它。
void fetchMenuInterfaces(IContextMenu *menu, ShellMenuResources *resources)
{
    if (SUCCEEDED(menu->QueryInterface(IID_IContextMenu3,
                                       reinterpret_cast<void **>(&resources->menu3)))) {
        return;
    }
    menu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void **>(&resources->menu2));
}

/// 执行用户选中的那一条。
HRESULT invokeCommand(IContextMenu *menu, HWND owner, UINT command, const POINT &point)
{
    CMINVOKECOMMANDINFOEX info = {};
    info.cbSize = sizeof(info);
    // `CMIC_MASK_PTINVOKE`：把「点在哪里」一起告诉 shell（有些条目要用它）；
    // `CMIC_MASK_UNICODE`：命令名走 `lpVerbW`（两个都填，老的实现会用 ANSI 的）。
    info.fMask = CMIC_MASK_PTINVOKE | CMIC_MASK_UNICODE;
    info.hwnd = owner;
    info.lpVerb = MAKEINTRESOURCEA(command - kCommandFirst);
    info.lpVerbW = MAKEINTRESOURCEW(command - kCommandFirst);
    info.nShow = SW_SHOWNORMAL;
    info.ptInvoke = point;
    return menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
}

} // namespace

MenuResult showItemMenu(HWND owner,
                        const QString &path,
                        const std::function<void()> &beforeInvoke)
{
    MenuResult result;
    if (owner == nullptr) {
        result.error = QStringLiteral("no owner window for the shell menu");
        return result;
    }
    if (path.isEmpty()) {
        result.error = QStringLiteral("no path for the shell menu");
        return result;
    }

    // 这台线程必须是 STA：shell 的文件夹对象与 `IContextMenu` 都按 STA 用。
    // Qt 已经为 OLE/拖放把主线程初始化成 STA 了（AGENTS.md 第 7 节第 13 条），
    // 这里再喊一次只是保险；`RPC_E_CHANGED_MODE` 说明已经是别的单元模型，
    // 对象照样能用，只是别去反初始化。
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coOwned = SUCCEEDED(coInit);

    ShellMenuResources resources;
    const std::wstring wide = path.toStdWString();
    HRESULT hr = SHParseDisplayName(wide.c_str(), nullptr, &resources.pidl, 0, nullptr);
    if (FAILED(hr) || resources.pidl == nullptr) {
        result.error = hresultMessage(hr, "SHParseDisplayName");
        if (coOwned) {
            CoUninitialize();
        }
        return result;
    }

    PCUITEMID_CHILD child = nullptr;
    hr = SHBindToParent(resources.pidl, IID_PPV_ARGS(&resources.folder), &child);
    if (FAILED(hr) || resources.folder == nullptr || child == nullptr) {
        result.error = hresultMessage(hr, "SHBindToParent");
        if (coOwned) {
            CoUninitialize();
        }
        return result;
    }

    hr = resources.folder->GetUIObjectOf(owner, 1, &child, IID_IContextMenu, nullptr,
                                        reinterpret_cast<void **>(&resources.menu));
    if (FAILED(hr) || resources.menu == nullptr) {
        result.error = hresultMessage(hr, "IShellFolder::GetUIObjectOf(IContextMenu)");
        if (coOwned) {
            CoUninitialize();
        }
        return result;
    }
    fetchMenuInterfaces(resources.menu, &resources);

    {
        // 菜单从创建到执行都在降级过的 DPI 上下文里（见那个类的注释）；
        // 取光标位置也在里面，这样两者用的是同一套坐标。
        ScopedGdiScaledDpiContext dpi;

        resources.hmenu = CreatePopupMenu();
        if (resources.hmenu == nullptr) {
            result.error = lastErrorMessage("CreatePopupMenu");
            if (coOwned) {
                CoUninitialize();
            }
            return result;
        }
        // `CMF_EXTENDEDVERBS`：把「扩展动词」也列出来 —— 也就是资源管理器里
        // 按住 Shift 右键才多出来的那一批（「以管理员身份运行」「以其他用户身份
        // 运行」……）。Windows 11 开始菜单的磁贴菜单里就有「以管理员身份运行」，
        // 所以这里默认带上。
        hr = resources.menu->QueryContextMenu(resources.hmenu, 0, kCommandFirst, kCommandLast,
                                              CMF_NORMAL | CMF_EXTENDEDVERBS);
        if (FAILED(hr)) {
            result.error = hresultMessage(hr, "IContextMenu::QueryContextMenu");
            if (coOwned) {
                CoUninitialize();
            }
            return result;
        }
        if (GetMenuItemCount(resources.hmenu) <= 0) {
            // 理论上不该发生（开始菜单里的条目总会有点什么），但真发生了就报出来，
            // 总比弹一个空白菜单好。
            result.error = QStringLiteral("the shell returned an empty menu");
            if (coOwned) {
                CoUninitialize();
            }
            return result;
        }

        MenuMessageRouter router(owner, resources.menu2, resources.menu3);
        router.install();

        POINT point{};
        GetCursorPos(&point);
        // `TrackPopupMenuEx` 要求调用之前拥有前台窗口，否则「点菜单外面就关掉」
        // 会失效（窗口已经在前台时这一句是空操作）。
        SetForegroundWindow(owner);
        const UINT command = TrackPopupMenuEx(
            resources.hmenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y,
            owner, nullptr);
        router.remove();
        // MSDN 的收尾动作：让菜单真的从屏幕上消失（不做的话它可能还挂在那里）。
        PostMessageW(owner, WM_NULL, 0, 0);

        if (command >= kCommandFirst) {
            // 用户真的选了：先把卡片收起来再让 shell 干活（理由见头文件）。
            if (beforeInvoke) {
                beforeInvoke();
            }
            const HRESULT invoked = invokeCommand(resources.menu, owner, command, point);
            if (FAILED(invoked)) {
                result.error = hresultMessage(invoked, "IContextMenu::InvokeCommand");
            } else {
                result.invoked = true;
                result.command = command - kCommandFirst;
            }
        }
    }

    if (coOwned) {
        CoUninitialize();
    }
    return result;
}

} // namespace flowkeyd::platform::win::shell_menu
