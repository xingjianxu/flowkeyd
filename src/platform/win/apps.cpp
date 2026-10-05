// `core/app_list.h` 先于任何 `windows.h` 包含（本仓库的规矩，见 AGENTS.md 第 10 节）。
#include "core/app_list.h"

#include "platform/win/apps.h"

#include "platform/win/ffi.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace flowkeyd::platform::win::apps {

namespace {

/// `shell:AppsFolder` 下每个条目上要读的属性。
///
/// 自己写 GUID/PID 而不用 `<propkey.h>` 里的 `PKEY_*`：那些符号由 MinGW 的
/// `libuuid` 提供，而本仓库对「多一个链接期符号」很小心（见 AGENTS.md 第 3 节）。
/// 这三个就是文档里写的那个值，改不了。
const PROPERTYKEY kPkeyAppUserModelId{
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
const PROPERTYKEY kPkeyLinkTargetParsingPath{
    {0xB9B4B3FC, 0x2B51, 0x4A42, {0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25}}, 2};
const PROPERTYKEY kPkeyLinkArguments{
    {0x436F2667, 0x14E9, 0x4FEA, {0xB4, 0xF6, 0x9B, 0xC4, 0x25, 0xA9, 0xBA, 0x46}}, 100};

/// 启动器的解析名前缀（`shell:AppsFolder\<AUMID>`）。
constexpr wchar_t kAppsFolderName[] = L"shell:AppsFolder";

/// 「所有控制面板项」这个 known folder（`FOLDERID_ControlPanelFolder`）。
///
/// 自己写 GUID，理由同上（`FOLDERID_*` 那些符号在 MinGW 的 uuid 库里）。真机验过：
/// 它解析出来就是 `::{26EE0668-A00A-44D7-9371-BEB064C98683}\0`，也就是
/// `Shell.Application.Namespace(3)` 看到的那个文件夹（Win11 26200 上 36 条）。
/// **不要**换成 `{26EE0668-…}` 本身：那是分类视图（8 个分类），不是条目。
const GUID kControlPanelFolder{
    0x82A74AEB, 0xAEB4, 0x465C, {0xA0, 0x14, 0xD0, 0x97, 0xEE, 0x34, 0x6D, 0x63}};

/// 控制面板里没有、但用户经常要的 shell 条目（直接用解析名给）。
///
/// 目前只有「网络连接」（`{7007ACC7-…}`，启/禁网卡、改适配器属性那一页）：它是
/// 一个 delegate folder，不在「所有控制面板项」的枚举结果里。
constexpr const wchar_t *kExtraControlPanelItems[] = {
    L"::{7007ACC7-3202-11D1-AAD2-00805FC1270E}",
};

/// 拿一个 shell 交给我们的字符串（`CoTaskMemFree` 它）。
QString takeShellString(LPWSTR value)
{
    if (value == nullptr) {
        return QString();
    }
    const QString text = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return text;
}

/// 读一个属性（没有这个属性、或者值不是字符串时返回空串）。
QString shellString(IShellItem2 *item, const PROPERTYKEY &key)
{
    LPWSTR value = nullptr;
    if (item == nullptr || FAILED(item->GetString(key, &value))) {
        return QString();
    }
    return takeShellString(value);
}

/// 定下一条控制面板条目的**启动名**；拿不到能启动的名字就返回空串。
///
/// 优先**桌面绝对解析名**（`::{26EE0668-…}\0\::{025A5937-…}`）——shell
/// 虚拟项（大多数控制面板项）只有这一种形式；它解不回去时退回**文件系统路径**。
/// 真机上只有「字体」一条是这种情况：它的解析名里带的是**本地化显示名**
/// （`::{26EE0668-…}\0\字体`），`SHParseDisplayName` 解析不了，而
/// `C:\Windows\Fonts` 能正常打开（Explorer 打开的也是同一个文件夹）。
///
/// **只有能解析回 PIDL 的名字才会被采用**：一条按下去什么都不发生的条目比
/// 少一条更糟。
QString launchNameForPidl(PCIDLIST_ABSOLUTE idlist)
{
    constexpr SIGDN kForms[] = {SIGDN_DESKTOPABSOLUTEPARSING, SIGDN_FILESYSPATH};
    for (const SIGDN form : kForms) {
        LPWSTR value = nullptr;
        if (FAILED(SHGetNameFromIDList(idlist, form, &value))) {
            continue;
        }
        const QString name = takeShellString(value);
        if (name.isEmpty()) {
            continue;
        }
        const std::wstring wide = name.toStdWString();
        PIDLIST_ABSOLUTE roundTrip = nullptr;
        if (SUCCEEDED(SHParseDisplayName(wide.c_str(), nullptr, &roundTrip, 0, nullptr))) {
            if (roundTrip != nullptr) {
                CoTaskMemFree(roundTrip);
            }
            return name;
        }
    }
    return QString();
}

/// 把一个**绝对 PIDL** 变成启动器条目（控制面板那一路用）。
///
/// 显示名问 shell（`SIGDN_NORMALDISPLAY`，所以跟系统语言走）；启动名交给
/// `launchNameForPidl()`（拿不到就直接丢掉这条）。
std::optional<core::AppEntry> entryFromAbsolutePidl(PCIDLIST_ABSOLUTE idlist)
{
    if (idlist == nullptr) {
        return std::nullopt;
    }
    core::AppEntry entry;
    entry.launch = launchNameForPidl(idlist);
    IShellItem2 *item = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(idlist, IID_PPV_ARGS(&item)))) {
        LPWSTR display = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display))) {
            entry.name = takeShellString(display);
        }
        item->Release();
    }
    if (entry.name.isEmpty() || entry.launch.isEmpty()) {
        return std::nullopt;
    }
    // 真机上有一条没有显示名的（`{98F2AB62-…}`）：shell 把**解析名**当显示名给了
    // 回来（`::{26EE0668-…}\0\::{98F2AB62-…}`）。这种条目在列表里就是一行乱码，
    // 直接丢掉（与“没名字”一起数进 `hiddenNotProgram`）。
    if (entry.name.startsWith(QLatin1String("::"))) {
        return std::nullopt;
    }
    return entry;
}

/// 真正干活的那一遍（必须在已经进入 STA 的线程上跑）。
StartMenuScan scanOnCurrentThread()
{
    StartMenuScan scan;

    // `shell:AppsFolder` 就是开始菜单「所有应用」那个虚拟文件夹（`FOLDERID_AppsFolder`）。
    // 用解析名而不是 `SHGetKnownFolderIDList(FOLDERID_AppsFolder, …)`：后者要把那个
    // GUID 符号引进来，而它在 MinGW 的 uuid 库里（同一个顾虑）。
    PIDLIST_ABSOLUTE root = nullptr;
    HRESULT hr = SHParseDisplayName(kAppsFolderName, nullptr, &root, 0, nullptr);
    if (FAILED(hr) || root == nullptr) {
        scan.error = hresultMessage(hr, "SHParseDisplayName(shell:AppsFolder)");
        return scan;
    }
    IShellFolder *folder = nullptr;
    hr = SHBindToObject(nullptr, root, nullptr, IID_IShellFolder, reinterpret_cast<void **>(&folder));
    if (FAILED(hr) || folder == nullptr) {
        scan.error = hresultMessage(hr, "SHBindToObject(AppsFolder)");
        CoTaskMemFree(root);
        return scan;
    }

    IEnumIDList *enumerator = nullptr;
    hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumerator);
    if (FAILED(hr) || enumerator == nullptr) {
        scan.error = hresultMessage(hr, "IShellFolder::EnumObjects(AppsFolder)");
        folder->Release();
        CoTaskMemFree(root);
        return scan;
    }

    LPITEMIDLIST child = nullptr;
    ULONG fetched = 0;
    while (enumerator->Next(1, &child, &fetched) == S_OK) {
        ++scan.candidates;

        // 「父文件夹 + 子 PIDL」拼成绝对 PIDL，再换成 `IShellItem2`（属性都在它上面）。
        const PIDLIST_ABSOLUTE absolute = ILCombine(root, child);
        IShellItem2 *item = nullptr;
        if (absolute != nullptr
            && SUCCEEDED(SHCreateItemFromIDList(absolute, IID_PPV_ARGS(&item)))) {
            LPWSTR display = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display))) {
                const QString name = takeShellString(display);
                const QString appId = shellString(item, kPkeyAppUserModelId);
                const QString target = shellString(item, kPkeyLinkTargetParsingPath);
                const QString arguments = shellString(item, kPkeyLinkArguments);
                // 没有 AUMID 就没法启动、也没法算图标键（真机上没碰到过）：跳过。
                if (name.isEmpty() || appId.isEmpty()) {
                    ++scan.hiddenNotProgram;
                } else if (!core::appTargetIsProgram(target, appId)) {
                    ++scan.hiddenNotProgram;
                } else if (core::appLooksLikeUninstaller(name, target)) {
                    ++scan.hiddenUninstaller;
                } else {
                    core::AppEntry entry;
                    entry.name = name;
                    // 启动 / 图标 / 右键菜单都用它（见头文件）。AUMID 里可能有
                    // `\`（`{已知文件夹 GUID}\相对\路径.exe`）或 `:`（目标路径），
                    // 拼在后面照样能解析 —— 真机上 159 个条目 159 个都验过。
                    entry.launch = QString::fromWCharArray(kAppsFolderName) + QLatin1Char('\\')
                                   + appId;
                    entry.target = target;
                    entry.arguments = arguments;
                    scan.entries.push_back(std::move(entry));
                }
            } else {
                ++scan.hiddenNotProgram;
            }
            item->Release();
        }
        if (absolute != nullptr) {
            CoTaskMemFree(absolute);
        }
        CoTaskMemFree(child);
    }

    enumerator->Release();
    folder->Release();
    CoTaskMemFree(root);
    return scan;
}

/// 枚举控制面板的那一遍（必须在已经进入 STA 的线程上跑）。
StartMenuScan scanControlPanelOnCurrentThread()
{
    StartMenuScan scan;

    // `FOLDERID_ControlPanelFolder` = 「所有控制面板项」。用已知文件夹而不是
    // 硬写解析名，是为了不依赖 `…\0` 那个下标在别的 Windows 版本上还是同一个。
    PIDLIST_ABSOLUTE root = nullptr;
    HRESULT hr = SHGetKnownFolderIDList(kControlPanelFolder, 0, nullptr, &root);
    if (FAILED(hr) || root == nullptr) {
        scan.error = hresultMessage(hr, "SHGetKnownFolderIDList(ControlPanel)");
        return scan;
    }
    IShellFolder *folder = nullptr;
    hr = SHBindToObject(nullptr, root, nullptr, IID_IShellFolder, reinterpret_cast<void **>(&folder));
    if (FAILED(hr) || folder == nullptr) {
        scan.error = hresultMessage(hr, "SHBindToObject(ControlPanel)");
        CoTaskMemFree(root);
        return scan;
    }
    IEnumIDList *enumerator = nullptr;
    hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumerator);
    if (FAILED(hr) || enumerator == nullptr) {
        scan.error = hresultMessage(hr, "IShellFolder::EnumObjects(ControlPanel)");
        folder->Release();
        CoTaskMemFree(root);
        return scan;
    }

    LPITEMIDLIST child = nullptr;
    ULONG fetched = 0;
    while (enumerator->Next(1, &child, &fetched) == S_OK) {
        ++scan.candidates;
        const PIDLIST_ABSOLUTE absolute = ILCombine(root, child);
        const std::optional<core::AppEntry> entry = entryFromAbsolutePidl(absolute);
        if (entry.has_value()) {
            scan.entries.push_back(*entry);
        } else {
            // 真机上确实有无名条目（`{98F2AB62-…}`）与解不出解析名的：它们
            // 既显示不了也启动不了，只能丢掉。
            ++scan.hiddenNotProgram;
        }
        if (absolute != nullptr) {
            CoTaskMemFree(absolute);
        }
        CoTaskMemFree(child);
    }

    enumerator->Release();
    folder->Release();
    CoTaskMemFree(root);

    // 「所有控制面板项」里没有、但用户经常要的那几个。
    for (const wchar_t *name : kExtraControlPanelItems) {
        ++scan.candidates;
        PIDLIST_ABSOLUTE idlist = nullptr;
        if (SUCCEEDED(SHParseDisplayName(name, nullptr, &idlist, 0, nullptr))) {
            const std::optional<core::AppEntry> entry = entryFromAbsolutePidl(idlist);
            if (entry.has_value()) {
                scan.entries.push_back(*entry);
            } else {
                ++scan.hiddenNotProgram;
            }
            CoTaskMemFree(idlist);
        } else {
            ++scan.hiddenNotProgram;
        }
    }
    return scan;
}

} // namespace

StaThread::StaThread()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        // 这个线程已经是别的单元模型（动作线程装了 MTA 的音频后端就是这种情况）：
        // 对象照样能用，只是别去反初始化。
        m_ok = true;
        return;
    }
    if (FAILED(hr)) {
        m_error = hresultMessage(hr, "CoInitializeEx(STA)");
        return;
    }
    m_ok = true;
    m_owned = true;
}

StaThread::~StaThread()
{
    if (m_owned) {
        CoUninitialize();
    }
}

StartMenuScan listStartMenuApps()
{
    // 自己开一条一次性的 STA 线程，而不是在当前线程上 `CoInitializeEx`：动作线程
    // 可能已经被音频后端初始化成 MTA 了（AGENTS.md 第 7 节第 13 条），而 shell 的
    // 文件夹对象与图像工厂按 STA 用最稳。
    StartMenuScan scan;
    std::thread worker([&scan]() {
        StaThread apartment;
        if (!apartment.ok()) {
            scan.error = apartment.error();
            return;
        }
        scan = scanOnCurrentThread();
    });
    worker.join();
    return scan;
}

StartMenuScan listControlPanelItems()
{
    // 与 `listStartMenuApps()` 同一套：自己开一条一次性的 STA 线程。
    StartMenuScan scan;
    std::thread worker([&scan]() {
        StaThread apartment;
        if (!apartment.ok()) {
            scan.error = apartment.error();
            return;
        }
        scan = scanControlPanelOnCurrentThread();
    });
    worker.join();
    return scan;
}

bool launchApp(const QString &launchName, QString *error)
{
    // COM 要在这条线程上初始化过（`SHParseDisplayName` / `ShellExecuteEx`）：
    // 动作线程可能是 MTA，这里声明一次（已经是别的单元模型时它就不管了）。
    StaThread apartment;
    if (!apartment.ok()) {
        if (error != nullptr) {
            *error = apartment.error();
        }
        return false;
    }
    const std::wstring wide = launchName.toStdWString();

    // 第一条路：把解析名直接当 `lpFile` 交给 shell（真机验过能拉起「运行」对话框）。
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    // `SEE_MASK_FLAG_NO_UI`：失败也不要弹系统对话框 —— 错误由我们写进日志。
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = wide.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info) != 0) {
        return true;
    }
    const DWORD firstError = GetLastError();

    // 第二条路：`SHParseDisplayName` + `SEE_MASK_IDLIST`（微软给商店应用写的那条）。
    PIDLIST_ABSOLUTE idlist = nullptr;
    const HRESULT hr = SHParseDisplayName(wide.c_str(), nullptr, &idlist, 0, nullptr);
    if (FAILED(hr) || idlist == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot launch %1: %2 (shell error %3)")
                         .arg(launchName, hresultText(hr))
                         .arg(firstError);
        }
        return false;
    }
    SHELLEXECUTEINFOW idlistInfo = {};
    idlistInfo.cbSize = sizeof(idlistInfo);
    idlistInfo.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
    idlistInfo.lpVerb = L"open";
    idlistInfo.lpIDList = idlist;
    idlistInfo.nShow = SW_SHOWNORMAL;
    const BOOL ok = ShellExecuteExW(&idlistInfo);
    const DWORD secondError = ok != 0 ? 0 : GetLastError();
    CoTaskMemFree(idlist);
    if (ok != 0) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("cannot launch %1: %2")
                     .arg(launchName, winErrorMessage(secondError));
    }
    return false;
}

ShellIcon shellIcon(const QString &path, int size)
{
    ShellIcon icon;
    if (size < 1) {
        size = 32;
    }
    const std::wstring wide = path.toStdWString();
    IShellItemImageFactory *factory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || factory == nullptr) {
        icon.error = hresultMessage(hr, "SHCreateItemFromParsingName");
        return icon;
    }
    HBITMAP bitmap = nullptr;
    const SIZE requested{size, size};
    // `SIIGBF_ICONONLY`：只要图标，不要让 shell 去生成缩略图（商店应用的条目
    // 也走这条，拿到的是它自己的图标 —— 与开始菜单显示的一致）。
    // `SIIGBF_BIGGERSIZEOK`：允许 shell 从更大的源尺寸缩下来，比小图放大清楚。
    hr = factory->GetImage(requested, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
    factory->Release();
    if (FAILED(hr) || bitmap == nullptr) {
        icon.error = hresultMessage(hr, "IShellItemImageFactory::GetImage");
        return icon;
    }

    BITMAP header{};
    if (GetObjectW(bitmap, sizeof(header), &header) == 0 || header.bmWidth <= 0
        || header.bmHeight <= 0) {
        DeleteObject(bitmap);
        icon.error = lastErrorMessage("GetObjectW(HBITMAP)");
        return icon;
    }
    const int width = header.bmWidth;
    const int height = header.bmHeight;
    if (header.bmBitsPixel != 32) {
        DeleteObject(bitmap);
        icon.error = QStringLiteral("the shell returned a %1-bit icon, expected 32")
                         .arg(header.bmBitsPixel);
        return icon;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    // 负高度 = 自顶向下（不写负号的话第一行是**左下角**，图标会上下颠倒）。
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    HDC screen = CreateCompatibleDC(nullptr);
    if (screen == nullptr) {
        DeleteObject(bitmap);
        icon.error = lastErrorMessage("CreateCompatibleDC");
        return icon;
    }
    const int lines = GetDIBits(screen, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info,
                                DIB_RGB_COLORS);
    DeleteDC(screen);
    DeleteObject(bitmap);
    if (lines == 0) {
        icon.error = lastErrorMessage("GetDIBits");
        return icon;
    }

    icon.ok = true;
    icon.width = width;
    icon.height = height;
    icon.pixels = std::move(pixels);
    return icon;
}

} // namespace flowkeyd::platform::win::apps
