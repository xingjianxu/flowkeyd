#include "core/app_list.h"

#include "core/pinyin.h"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cstdint>

namespace flowkeyd::core {

namespace {

/// 去重用的键：名字 + 目标 + 参数（全部小写）。
///
/// 目标为空（只有 IDList 的商店/UWP 条目）时只比名字：同一个应用在
/// 「全局开始菜单」与「当前用户开始菜单」里各一份是常态。
QString dedupeKey(const AppEntry &entry)
{
    QString key = entry.name.toLower();
    key += QChar(0x1f);
    key += entry.target.toLower();
    key += QChar(0x1f);
    key += entry.arguments.toLower();
    return key;
}

/// 子目录层数：根目录是 0。
int groupDepth(const QString &group)
{
    if (group.isEmpty()) {
        return 0;
    }
    int depth = 1;
    for (const QChar ch : group) {
        if (ch == QLatin1Char('\\')) {
            ++depth;
        }
    }
    return depth;
}

/// 文件名去掉 `.lnk`（大小写无关）。
QString stripLnkSuffix(const QString &fileName)
{
    if (fileName.size() > 4
        && fileName.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
        return fileName.left(fileName.size() - 4);
    }
    return fileName;
}

} // namespace

QString appIconKey(const QString &shortcutPath)
{
    // 路径规范化：分隔符统一、小写（Windows 的路径不区分大小写）。
    QString normalized = shortcutPath;
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

QString appIconUrl(const QString &shortcutPath)
{
    return QStringLiteral("image://flowkeyd-app/") + appIconKey(shortcutPath);
}

std::vector<AppEntry> prepareAppEntries(std::vector<AppEntry> entries)
{
    std::vector<AppEntry> kept;
    kept.reserve(entries.size());
    QHash<QString, std::size_t> seen;
    for (AppEntry &entry : entries) {
        entry.name = stripLnkSuffix(entry.name).trimmed();
        if (entry.name.isEmpty() || entry.shortcut.trimmed().isEmpty()) {
            // 没有名字就没有可显示的东西（筛选也没法按名字匹配）；没有快捷方式
            // 路径既没法启动、也没法算图标键。
            continue;
        }
        const QString key = dedupeKey(entry);
        const auto found = seen.constFind(key);
        if (found == seen.constEnd()) {
            seen.insert(key, kept.size());
            kept.push_back(std::move(entry));
            continue;
        }
        // 同一个程序的第二份：层级更浅的那一份更靠近开始菜单的顶层，留它。
        AppEntry &previous = kept[found.value()];
        if (groupDepth(entry.group) < groupDepth(previous.group)) {
            previous = std::move(entry);
        }
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
        order = QString::compare(a.group, b.group, Qt::CaseInsensitive);
        if (order != 0) {
            return order < 0;
        }
        return QString::compare(a.shortcut, b.shortcut, Qt::CaseInsensitive) < 0;
    });
    return kept;
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

bool appNameMatches(const QString &name, const QString &needle)
{
    const QString trimmed = needle.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    return appSearchText(name).contains(trimmed, Qt::CaseInsensitive);
}

bool appTargetIsProgram(const QString &target, bool hasIdList)
{
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return hasIdList;
    }
    return trimmed.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
}

} // namespace flowkeyd::core
