#include "core/app_list.h"

#include <QHash>

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

bool appNameMatches(const QString &name, const QString &needle)
{
    const QString trimmed = needle.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    return name.contains(trimmed, Qt::CaseInsensitive);
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
