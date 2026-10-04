// `core/app_list.h` 先于任何 `windows.h` 包含（本仓库的规矩，见 AGENTS.md 第 10 节）。
#include "core/app_list.h"

#include "platform/win/apps.h"

#include "platform/win/ffi.h"

#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace flowkeyd::platform::win::apps {

namespace {

/// 一个待解析的快捷方式：完整路径 + 相对开始菜单根目录的子目录。
struct Shortcut
{
    QString path;
    QString group;
};

/// 「全局开始菜单」与「当前用户开始菜单」的 `…\Start Menu\Programs` 两个根目录。
///
/// 为什么用环境变量而不是 `SHGetKnownFolderPath`：`ProgramData` / `APPDATA` 在两
/// 个根目录上永远是设置好的，而已知文件夹 API 要把 `FOLDERID_*` 的 GUID 引进来
/// （MinGW 的 uuid 库里不一定有，本仓库对这类符号很小心）。
std::vector<QString> startMenuRoots()
{
    std::vector<QString> roots;
    const auto append = [&roots](const QString &base) {
        if (base.isEmpty()) {
            return;
        }
        roots.push_back(QDir::toNativeSeparators(
            base + QStringLiteral("\\Microsoft\\Windows\\Start Menu\\Programs")));
    };
    append(qEnvironmentVariable("ProgramData"));
    append(qEnvironmentVariable("APPDATA"));
    return roots;
}

/// 递归收集 `root` 下的全部 `.lnk`。目录不存在时什么也不做（不是错误）。
void collectShortcuts(const QString &root, std::vector<Shortcut> *out)
{
    if (!QFileInfo(root).isDir()) {
        return;
    }
    QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = QDir::toNativeSeparators(iterator.next());
        if (!path.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
            continue;
        }
        Shortcut shortcut;
        shortcut.path = path;
        // `QDir::relativeFilePath` 用 `/`，而 `group` 只给别人当字符串看，
        // 统一成 `\` 更符合 Windows 的直觉。
        QString relative = QDir(root).relativeFilePath(path);
        relative.replace(QLatin1Char('/'), QLatin1Char('\\'));
        const qsizetype slash = relative.lastIndexOf(QLatin1Char('\\'));
        if (slash > 0) {
            shortcut.group = relative.left(slash);
        }
        out->push_back(std::move(shortcut));
    }
}

/// 把 `%windir%\system32\notepad.exe` 这类写法展开成真实路径。
///
/// 展开只为了去重与诊断（启动走的是快捷方式本身，不需要目标路径）：同一个程序
/// 在两条快捷方式里一条写环境变量、一条写绝对路径时，展开之后才能认出是同一个。
QString expandEnvironment(const QString &value)
{
    if (!value.contains(QLatin1Char('%')) || value.isEmpty()) {
        return value;
    }
    const std::wstring wide = value.toStdWString();
    const DWORD needed = ExpandEnvironmentStringsW(wide.c_str(), nullptr, 0);
    if (needed == 0 || needed > 32768) {
        return value;
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(wide.c_str(), buffer.data(), needed);
    if (written == 0 || written > needed) {
        return value;
    }
    buffer.resize(written > 0 ? written - 1 : 0); // 去掉结尾的 NUL
    return QString::fromStdWString(buffer);
}

/// 用 `IShellLink` 解析一个快捷方式。
///
/// 失败（文件被删、COM 出错、快捷方式坏掉）时返回 false —— 这类条目直接丢掉，
/// 反正也启动不了。
bool resolveShortcut(const QString &shortcut, QString *target, QString *arguments, bool *hasIdList)
{
    IShellLinkW *link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void **>(&link));
    if (FAILED(hr) || link == nullptr) {
        return false;
    }
    IPersistFile *file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file));
    if (FAILED(hr) || file == nullptr) {
        link->Release();
        return false;
    }

    const std::wstring wide = shortcut.toStdWString();
    const bool loaded = SUCCEEDED(file->Load(wide.c_str(), STGM_READ));
    if (loaded) {
        // `SLGP_RAWPATH`：**不**做 shell 的“规范化”，拿到的就是快捷方式里存的
        // 那一条（可能是 `%windir%\...`），展开环境变量是下一步的事。
        std::vector<wchar_t> buffer(4096, L'\0');
        if (SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr,
                                    SLGP_RAWPATH))) {
            *target = expandEnvironment(QString::fromWCharArray(buffer.data()));
        }
        std::fill(buffer.begin(), buffer.end(), L'\0');
        if (SUCCEEDED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size())))) {
            *arguments = QString::fromWCharArray(buffer.data());
        }
        // 商店/UWP 应用的快捷方式可能**只有 IDList**（没有目标路径）。
        PIDLIST_ABSOLUTE idlist = nullptr;
        if (SUCCEEDED(link->GetIDList(&idlist))) {
            *hasIdList = idlist != nullptr;
            CoTaskMemFree(idlist);
        }
    }

    file->Release();
    link->Release();
    return loaded;
}

/// 真正干活的那一遍（必须在已经进入 STA 的线程上跑）。
StartMenuScan scanOnCurrentThread()
{
    StartMenuScan scan;
    for (const QString &root : startMenuRoots()) {
        std::vector<Shortcut> shortcuts;
        collectShortcuts(root, &shortcuts);
        for (const Shortcut &shortcut : shortcuts) {
            ++scan.shortcuts;
            QString target;
            QString arguments;
            bool hasIdList = false;
            if (!resolveShortcut(shortcut.path, &target, &arguments, &hasIdList)) {
                continue;
            }
            if (!core::appTargetIsProgram(target, hasIdList)) {
                continue;
            }
            core::AppEntry entry;
            // 名字就是文件名（`core::prepareAppEntries` 会把 `.lnk` 去掉）：
            // 真机实测 `SHGFI_DISPLAYNAME` / `IShellLink::GetDescription` 都不可靠
            // （前者有一半是空的或被截断，后者常是“Open Visual Studio …”这种
            // 冗长的提示语）。
            entry.name = QFileInfo(shortcut.path).fileName();
            entry.group = shortcut.group;
            entry.shortcut = shortcut.path;
            entry.target = target;
            entry.arguments = arguments;
            scan.entries.push_back(std::move(entry));
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
    // 可能已经被音频后端初始化成 MTA 了（AGENTS.md 第 7 节第 13 条），而
    // `IShellLink` / shell 的图像工厂按 STA 用最稳。
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
    // `SIIGBF_ICONONLY`：只要图标，不要让 shell 去生成缩略图（快捷方式也走这条，
    // 拿到的是它自己的图标 —— 与开始菜单显示的一致）。
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
