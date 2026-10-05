#include "core/launcher_state.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace flowkeyd::core {

namespace {

constexpr int kSchemaVersion = 1;

/// 版本号对我们的用处：将来真要做迁移时才有东西可判断。现在只写不读（读的时候
/// 容忍任何版本，多了的字段忽略、少了的字段当空）。
constexpr char kVersionKey[] = "version";
constexpr char kPinnedKey[] = "pinned";
constexpr char kRecentKey[] = "recent";

/// 从 JSON 里取一份图标键列表：只留非空字符串，去重（保留第一次出现的顺序）。
QStringList readKeyList(const QJsonObject &root, const char *name)
{
    const QJsonValue value = root.value(QLatin1String(name));
    if (value.isUndefined() || value.isNull()) {
        return {};
    }
    if (!value.isArray()) {
        return {};
    }
    QStringList keys;
    const QJsonArray array = value.toArray();
    keys.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (!item.isString()) {
            continue;
        }
        const QString key = item.toString().trimmed();
        if (key.isEmpty() || keys.contains(key)) {
            continue;
        }
        keys.push_back(key);
    }
    return keys;
}

} // namespace

QString launcherStatePath(const QString &configPath)
{
    const QFileInfo info(configPath);
    return info.absoluteDir().filePath(QStringLiteral("launcher.json"));
}

LauncherState parseLauncherState(const QByteArray &json, QString *error)
{
    LauncherState state;
    // 一进来就清掉：调用方复用同一个 `QString` 时不会拿到上一次的旧错误。
    if (error != nullptr) {
        error->clear();
    }
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
    };

    if (json.trimmed().isEmpty()) {
        // 空文件 = 还没有状态（第一次运行），不是错误。
        return state;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(QStringLiteral("launcher state is not valid JSON: %1").arg(parseError.errorString()));
        return state;
    }
    if (!document.isObject()) {
        fail(QStringLiteral("launcher state must be a JSON object"));
        return state;
    }

    const QJsonObject root = document.object();
    state.pinned = readKeyList(root, kPinnedKey);
    state.recent = readKeyList(root, kRecentKey);
    // 落盘的那一份可能比现在长（改小了 `kRecentLimit`）——这里也截一次。
    while (state.recent.size() > kRecentLimit) {
        state.recent.removeLast();
    }
    return state;
}

QByteArray serializeLauncherState(const LauncherState &state)
{
    QJsonObject root;
    root.insert(QLatin1String(kVersionKey), kSchemaVersion);
    QJsonArray pinned;
    for (const QString &key : state.pinned) {
        pinned.append(key);
    }
    root.insert(QLatin1String(kPinnedKey), pinned);
    QJsonArray recent;
    for (const QString &key : state.recent) {
        recent.append(key);
    }
    root.insert(QLatin1String(kRecentKey), recent);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QStringList touchRecent(const QStringList &recent, const QString &key)
{
    if (key.isEmpty()) {
        return recent;
    }
    QStringList next;
    next.reserve(recent.size() + 1);
    next.push_back(key);
    for (const QString &existing : recent) {
        if (existing == key) {
            continue;
        }
        if (next.size() > kRecentLimit) {
            break;
        }
        next.push_back(existing);
    }
    while (next.size() > kRecentLimit) {
        next.removeLast();
    }
    return next;
}

QStringList togglePinned(const QStringList &pinned, const QString &key)
{
    if (key.isEmpty()) {
        return pinned;
    }
    QStringList next = pinned;
    const int index = next.indexOf(key);
    if (index >= 0) {
        next.removeAt(index);
        return next;
    }
    next.push_back(key);
    return next;
}

} // namespace flowkeyd::core
