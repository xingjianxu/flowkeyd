#include "core/app_list.h"

#include "core/pinyin.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace flowkeyd::core {

namespace {

/// 去重用的键：名字 + 启动名（全部小写）。
///
/// `shell:AppsFolder` 本来就不会把同一个应用列两遍，这里只是第二层保险：
/// 同一个名字 + 同一个启动名一定是同一条。
QString dedupeKey(const AppEntry &entry)
{
    QString key = entry.name.toLower();
    key += QChar(0x1f);
    key += entry.launch.toLower();
    return key;
}

/// 名字归一化：去掉首尾空白。
///
/// 老版本这里会把 `.lnk` 后缀去掉（当时名字是快捷方式的文件名）；现在名字是 shell
/// 给的显示名，没有后缀这回事，但归一化这一步留着 —— 两边的空白都去掉，
/// 去重与匹配才不会被看不见的字符绊住。
QString normalizeName(const QString &name)
{
    return name.trimmed();
}

} // namespace

QString appIconKey(const QString &launchName)
{
    // 路径规范化：分隔符统一、小写（Windows 的路径与 AUMID 都不区分大小写）。
    QString normalized = launchName;
    normalized.replace(QLatin1Char('/'), QLatin1Char('\\'));
    normalized = normalized.toLower();

    // 64 位 FNV-1a。不用 Qt 的 qHash：它的种子在进程之间可能不同，
    // 而这个键要跨进程稳定（QML 的图片缓存、诊断脚本都会看到它）。
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    const QByteArray bytes = normalized.toUtf8();
    for (const char byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= kPrime;
    }
    return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

QString appIconUrl(const QString &launchName)
{
    return QStringLiteral("image://flowkeyd-app/") + appIconKey(launchName);
}

namespace {

/// 「设置」应用的启动名（`shell:AppsFolder\<AUMID>`）。
///
/// `ms-settings:` 页面在 shell 里取不到图标，所以借「设置」自己的那一张。这个
/// AppUserModelID（包家族名 `windows.immersivecontrolpanel_cw5n1h2txyewy` +
/// AppId `microsoft.windows.immersivecontrolpanel`）自 Windows 8 起就没变过，
/// 而且它一定在 `shell:AppsFolder` 里（真机上就有一条「设置」）；万一哪天变了，
/// 设置页的格子会退回占位方块 —— 只影响图标，不影响启动。
constexpr const char *kSettingsAppLaunch =
    "shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy"
    "!microsoft.windows.immersivecontrolpanel";

/// 前缀（`ms-settings:`，大小写无关）。
constexpr char kSettingsUriPrefix[] = "ms-settings:";

} // namespace

QString appIconLaunchName(const QString &launchName)
{
    if (launchName.startsWith(QLatin1String(kSettingsUriPrefix), Qt::CaseInsensitive)) {
        return QString::fromLatin1(kSettingsAppLaunch);
    }
    return launchName;
}

std::vector<AppEntry> prepareAppEntries(std::vector<AppEntry> entries)
{
    std::vector<AppEntry> kept;
    kept.reserve(entries.size());
    QHash<QString, std::size_t> seen;
    for (AppEntry &entry : entries) {
        entry.name = normalizeName(entry.name);
        if (entry.name.isEmpty() || entry.launch.trimmed().isEmpty()) {
            // 没有名字就没有可显示的东西（筛选也没法按名字匹配）；没有启动名
            // 既没法启动、也没法算图标键。
            continue;
        }
        const QString key = dedupeKey(entry);
        const auto found = seen.constFind(key);
        if (found == seen.constEnd()) {
            seen.insert(key, kept.size());
            kept.push_back(std::move(entry));
        }
        // 同一个（名字 + 启动名）的第二份：丢掉，保留先见到的那一条。
    }

    std::stable_sort(kept.begin(), kept.end(), [](const AppEntry &a, const AppEntry &b) {
        int order = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (order != 0) {
            return order < 0;
        }
        // 同名（只是大小写不同）时让大写在前，保证顺序确定。
        order = QString::compare(a.name, b.name);
        if (order != 0) {
            return order < 0;
        }
        return QString::compare(a.launch, b.launch, Qt::CaseInsensitive) < 0;
    });
    return kept;
}

std::vector<AppEntry> appendLauncherEntries(std::vector<AppEntry> base,
                                            const std::vector<AppEntry> &extra)
{
    QSet<QString> names;
    names.reserve(static_cast<int>(base.size() + extra.size()));
    for (const AppEntry &entry : base) {
        names.insert(entry.name.trimmed().toLower());
    }
    for (const AppEntry &entry : extra) {
        const QString name = entry.name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        if (names.contains(name.toLower())) {
            continue;
        }
        names.insert(name.toLower());
        base.push_back(entry);
    }
    return base;
}

namespace {

/// 搜索文本里三段之间的分隔符（`U+001F`，用户打不出来）。
constexpr QChar kSearchPartSeparator(0x1f);

/// 汉字（含 Ext A/B 这些**没进拼音表**的区段）：标出它们不属于「拉丁词」。
bool isCjkIdeograph(char32_t codePoint)
{
    return (codePoint >= 0x3400 && codePoint <= 0x4dbf)
        || (codePoint >= 0x4e00 && codePoint <= 0x9fff)
        || (codePoint >= 0xf900 && codePoint <= 0xfaff)
        || (codePoint >= 0x20000 && codePoint <= 0x2fa1f);
}

} // namespace

QString appSearchText(const QString &name)
{
    const QString lowered = name.toLower();

    // 「可读音单元 VS 字面文本」的扫描：汉字（认得出读音）是一个单元；
    // 拉丁字母/数字连成一段；其它（空格、标点、表外的生僻字）按字面粘回全拼。
    struct Piece
    {
        bool literal = false;
        QString text;
        std::vector<QString> fullForms;
        std::vector<QString> initials;
    };
    std::vector<Piece> pieces;

    int index = 0;
    while (index < lowered.size()) {
        const QChar first = lowered.at(index);
        int width = 1;
        char32_t codePoint = first.unicode();
        if (first.isHighSurrogate() && index + 1 < lowered.size()
            && lowered.at(index + 1).isLowSurrogate()) {
            codePoint = QChar::surrogateToUcs4(first, lowered.at(index + 1));
            width = 2;
        }

        const std::vector<QString> readings = pinyinReadings(codePoint);
        if (!readings.empty()) {
            Piece piece;
            piece.fullForms.reserve(readings.size());
            piece.initials.reserve(readings.size());
            for (const QString &reading : readings) {
                piece.initials.push_back(reading.left(1));
                piece.fullForms.push_back(reading);
            }
            pieces.push_back(std::move(piece));
            index += width;
            continue;
        }

        if (isCjkIdeograph(codePoint) || !first.isLetterOrNumber()) {
            // 表外的汉字与空格/标点一样：按字面粘回全拼，也不进首字母。
            Piece piece;
            piece.literal = true;
            piece.text = lowered.mid(index, width);
            pieces.push_back(std::move(piece));
            index += width;
            continue;
        }

        // 拉丁字母 / 数字：连成一段，整段进全拼，首字母是这一段的首字符。
        int end = index;
        while (end < lowered.size() && lowered.at(end).isLetterOrNumber()
               && !isCjkIdeograph(lowered.at(end).unicode())) {
            ++end;
        }
        Piece piece;
        piece.text = lowered.mid(index, end - index);
        piece.fullForms.push_back(piece.text);
        piece.initials.push_back(piece.text.left(1));
        pieces.push_back(std::move(piece));
        index = end;
    }

    // 读音组合数：每个单元的音节数之积；太多就只用主读音。
    std::size_t combinations = 1;
    for (const Piece &piece : pieces) {
        if (piece.literal || piece.fullForms.empty()) {
            continue;
        }
        combinations *= piece.fullForms.size();
        if (combinations > static_cast<std::size_t>(kMaxSearchVariants)) {
            break;
        }
    }
    const bool primaryOnly = combinations > static_cast<std::size_t>(kMaxSearchVariants);

    QStringList fullForms{QString()};
    QStringList initials{QString()};
    for (const Piece &piece : pieces) {
        if (piece.literal) {
            for (QString &form : fullForms) {
                form += piece.text;
            }
            continue;
        }
        const int options = primaryOnly ? 1 : static_cast<int>(piece.fullForms.size());
        if (options == 1) {
            for (QString &form : fullForms) {
                form += piece.fullForms.front();
            }
            for (QString &form : initials) {
                form += piece.initials.front();
            }
            continue;
        }
        QStringList nextFull;
        QStringList nextInitials;
        nextFull.reserve(fullForms.size() * options);
        nextInitials.reserve(initials.size() * options);
        for (int i = 0; i < fullForms.size(); ++i) {
            for (int option = 0; option < options; ++option) {
                const auto slot = static_cast<std::size_t>(option);
                nextFull.push_back(fullForms.at(i) + piece.fullForms[slot]);
                nextInitials.push_back(initials.at(i) + piece.initials[slot]);
            }
        }
        fullForms = std::move(nextFull);
        initials = std::move(nextInitials);
    }

    // 三段用 `U+001F` 隔开：用户打不出这个字符，段与段之间也就拼不出假匹配。
    QString text = lowered;
    text += kSearchPartSeparator;
    text += fullForms.join(kSearchPartSeparator);
    text += kSearchPartSeparator;
    text += initials.join(kSearchPartSeparator);
    return text;
}

namespace {

/// 从 `appSearchText()` 的三段里取出**主读音的全拼**（第二段的第一项）。
///
/// `appSearchText()` 的形状是「名字 \x1f 全拼(可能是几个变体) \x1f 首字母」，
/// 而变体是按主读音在前的顺序拼出来的，所以第一个变体就是「每个字只用主读音」
/// 的那一份 —— 排序与分组要的正是它。
///
/// （在这里解析同一个文件里定义的那个格式，比把那段扫描逻辑抄第二遍便宜：
/// 两者的行为天然一致，改 `appSearchText()` 的拼接方式时这里不会走偏。）
QString primaryFullForm(const QString &searchText)
{
    const int first = searchText.indexOf(kSearchPartSeparator);
    if (first < 0) {
        return searchText;
    }
    const int second = searchText.indexOf(kSearchPartSeparator, first + 1);
    const int from = first + 1;
    const int length = (second < 0 ? searchText.size() : second) - from;
    return searchText.mid(from, length);
}

} // namespace

AppSortInfo appSortInfo(const QString &name)
{
    QString full = primaryFullForm(appSearchText(name));
    // 名字带前导空白时（配置文件里手写的名字）别让它决定分组。
    int start = 0;
    while (start < full.size() && full.at(start).isSpace()) {
        ++start;
    }
    full = full.mid(start);

    AppSortInfo info;
    const bool startsWithLetter = !full.isEmpty() && full.at(0).isLetter();
    info.sortText = (startsWithLetter ? QStringLiteral("1") : QStringLiteral("0")) + full;
    // 表头就是首字符（`full` 的第一个字符）大写；它不是字母时统统归到「#」。
    info.letter = startsWithLetter ? QString(full.at(0).toUpper()) : QStringLiteral("#");
    return info;
}

QString appSortText(const QString &name)
{
    return appSortInfo(name).sortText;
}

QString appGroupLetter(const QString &name)
{
    return appSortInfo(name).letter;
}

bool appNameMatches(const QString &name, const QString &needle)
{
    const QString trimmed = needle.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    return appSearchText(name).contains(trimmed, Qt::CaseInsensitive);
}

bool appTargetIsProgram(const QString &target, const QString &appUserModelId)
{
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        // 商店应用没有目标路径，只有一个 `<包家族名>!<AppId>` 形状的 AUMID。
        return appUserModelId.contains(QLatin1Char('!'));
    }
    const QString lowered = trimmed.toLower();
    // shell 的虚拟项（「文件资源管理器」`::{52205FD8-…}`、「控制面板」、「运行」）：
    // 它不是一个文件，但确实是开始菜单里的一条程序入口。
    if (lowered.startsWith(QLatin1String("::"))) {
        return true;
    }
    // 网址（`.url` 快捷方式、Steam 游戏、网页文档）不是程序。
    // 先挡掉它们：`https://example.com` 的末尾也是 `.com`。
    if (lowered.startsWith(QLatin1String("http://"))
        || lowered.startsWith(QLatin1String("https://"))
        || lowered.startsWith(QLatin1String("steam://"))) {
        return false;
    }
    // 看**最后一段**的扩展名（目标可能是 `{已知文件夹 GUID}\相对\路径.exe`
    // 这种 shell 写法，不能当普通路径解析）。
    const QString file = lowered.section(QLatin1Char('\\'), -1).section(QLatin1Char('/'), -1);
    constexpr const char *kProgramExtensions[] = {".exe", ".bat", ".cmd", ".msc", ".cpl"};
    for (const char *extension : kProgramExtensions) {
        if (file.endsWith(QLatin1String(extension))) {
            return true;
        }
    }
    return false;
}

namespace {

/// 目标文件名（最后一段路径，小写；目标为空时返回空串）。
QString targetFileName(const QString &target)
{
    return target.trimmed().toLower().section(QLatin1Char('\\'), -1).section(QLatin1Char('/'), -1);
}

} // namespace

bool appLooksLikeUninstaller(const QString &name, const QString &target)
{
    const QString loweredName = name.toLower();
    if (loweredName.contains(QStringLiteral("卸载"))) {
        return true;
    }
    // 名字里的 `uninstall` 必须是独立的一个词：`Uninstall Qt`、`Uninstaller`、
    // `uninstall foo` 都算，而名字里恰好含有这几个字母的正常程序不算。
    static const QRegularExpression kUninstallName(QStringLiteral("\\buninstall(er)?\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
    if (kUninstallName.match(loweredName).hasMatch()) {
        return true;
    }

    const QString file = targetFileName(target);
    if (file.isEmpty()) {
        return false;
    }
    // 安装器生成的反向操作程序：Inno Setup 的 `unins000.exe`、MSI 风格的
    // `uninst.exe`/`uninstall.exe`、InstallShield 的 `unwise.exe`。
    static const QRegularExpression kUninstallFile(
        QStringLiteral("^(unins\\d*|uninst|uninstall|unwise)\\.exe$"),
        QRegularExpression::CaseInsensitiveOption);
    return kUninstallFile.match(file).hasMatch();
}

// ---------------------------------------------------------------------------
// 内置的「Windows 设置」页面
// ---------------------------------------------------------------------------

namespace {

/// 常用设置页。URI 取自微软文档里列的那一份（`Launch Windows Settings` / 社区
/// 维护的 `ms-settings:` 列表），中文名就是中文系统上「设置」应用里那一页的标题。
///
/// 只收**常用**的（显示 / 声音 / 网络 / 电源 / 应用 / 时间语言 / 隐私安全 ……）；
/// 具体到某一页的开关仍然要进设置里点，这里给的是入口。
const SettingsPage kSettingsPages[] = {
    {"ms-settings:display", "显示", "Display"},
    {"ms-settings:sound", "声音", "Sound"},
    {"ms-settings:notifications", "通知", "Notifications"},
    {"ms-settings:powersleep", "电源和睡眠", "Power & sleep"},
    {"ms-settings:storagesense", "存储", "Storage"},
    {"ms-settings:bluetooth", "蓝牙和其他设备", "Bluetooth & devices"},
    {"ms-settings:network", "网络和 Internet", "Network & internet"},
    {"ms-settings:network-wifi", "Wi-Fi", "Wi-Fi"},
    {"ms-settings:network-ethernet", "以太网", "Ethernet"},
    {"ms-settings:network-vpn", "VPN", "VPN"},
    {"ms-settings:network-proxy", "代理", "Proxy"},
    {"ms-settings:printers", "打印机和扫描仪", "Printers & scanners"},
    {"ms-settings:personalization", "个性化", "Personalization"},
    {"ms-settings:appsfeatures", "应用和功能", "Apps & features"},
    {"ms-settings:defaultapps", "默认应用", "Default apps"},
    {"ms-settings:dateandtime", "日期和时间", "Date & time"},
    {"ms-settings:regionlanguage", "语言和区域", "Language & region"},
    {"ms-settings:clipboard", "剪贴板", "Clipboard"},
    {"ms-settings:multitasking", "多任务", "Multitasking"},
    {"ms-settings:easeofaccess", "辅助功能", "Accessibility"},
    {"ms-settings:privacy", "隐私和安全性", "Privacy & security"},
    {"ms-settings:windowsupdate", "Windows 更新", "Windows Update"},
    {"ms-settings:remotedesktop", "远程桌面", "Remote Desktop"},
    {"ms-settings:developers", "开发者选项", "For developers"},
    {"ms-settings:windowsinsider", "Windows 预览体验计划", "Windows Insider Program"},
    {"ms-settings:about", "关于", "About"},
};

} // namespace

const std::vector<SettingsPage> &windowsSettingsPages()
{
    static const std::vector<SettingsPage> pages(std::begin(kSettingsPages),
                                                 std::end(kSettingsPages));
    return pages;
}

bool settingsNamesAreChinese(const QStringList &uiLanguages)
{
    if (uiLanguages.isEmpty()) {
        return false;
    }
    // 只看第一项：`uiLanguages()` 本来就是“优先级从高到低”，第一项就是它当时
    // 真会用的那一种。别的语言（日语、德语……）一律走英文名 —— 名字是内置的，
    // 不可能给每种语言都准备一份。
    return QLocale(uiLanguages.first()).language() == QLocale::Chinese;
}

std::vector<AppEntry> builtinSettingsEntries(const QStringList &uiLanguages)
{
    const bool chinese = settingsNamesAreChinese(uiLanguages);
    const QString prefix = chinese ? QString::fromUtf8(kSettingsNamePrefixZh)
                                   : QString::fromUtf8(kSettingsNamePrefixEn);
    const std::vector<SettingsPage> &pages = windowsSettingsPages();
    std::vector<AppEntry> entries;
    entries.reserve(pages.size());
    for (const SettingsPage &page : pages) {
        AppEntry entry;
        entry.name = prefix + QString::fromUtf8(chinese ? page.nameZh : page.nameEn);
        entry.launch = QString::fromLatin1(page.uri);
        // `target` / `arguments` 留空：这一条不是一个文件，启动走 `ms-settings:`
        // 协议（shell 自己把它交给「设置」应用）。
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<AppEntry> builtinSettingsEntries()
{
    return builtinSettingsEntries(QLocale::system().uiLanguages());
}

} // namespace flowkeyd::core
