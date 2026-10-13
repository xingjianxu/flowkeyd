#include "core/window_monitor_state.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

namespace flowkeyd::core {

namespace {

constexpr int kSchemaVersion = 1;

/// 版本号对我们的用处：将来真要做迁移时才有东西可判断。现在只写不读（读的时候
/// 容忍任何版本，多了的字段忽略、少了的字段当空）。
constexpr char kVersionKey[] = "version";
constexpr char kMonitorsKey[] = "monitors";

} // namespace

QString windowMonitorStatePath(const QString &configPath)
{
    const QFileInfo info(configPath);
    return info.absoluteDir().filePath(QStringLiteral("windows.json"));
}

QString windowMonitorKey(const std::optional<QString> &executableName)
{
    if (!executableName.has_value()) {
        return {};
    }
    return executableName->trimmed().toLower();
}

WindowMonitorState parseWindowMonitorState(const QByteArray &json, QString *error)
{
    WindowMonitorState state;
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
        // 空文件 = 还没有记忆（第一次用 `"last"`），不是错误。
        return state;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(QStringLiteral("window monitor state is not valid JSON: %1")
                 .arg(parseError.errorString()));
        return state;
    }
    if (!document.isObject()) {
        fail(QStringLiteral("window monitor state must be a JSON object"));
        return state;
    }

    const QJsonValue monitors = document.object().value(QLatin1String(kMonitorsKey));
    if (monitors.isUndefined() || monitors.isNull()) {
        return state;
    }
    if (!monitors.isObject()) {
        fail(QStringLiteral("`%1` must be a JSON object").arg(QLatin1String(kMonitorsKey)));
        return state;
    }
    const QJsonObject object = monitors.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        const QString key = it.key().trimmed().toLower();
        const QString device = it.value().toString().trimmed();
        if (key.isEmpty() || device.isEmpty()) {
            continue;
        }
        state.monitors.insert(key, device);
    }
    return state;
}

QByteArray serializeWindowMonitorState(const WindowMonitorState &state)
{
    QJsonObject monitors;
    for (auto it = state.monitors.constBegin(); it != state.monitors.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty() || it.value().trimmed().isEmpty()) {
            continue;
        }
        monitors.insert(it.key(), it.value());
    }
    QJsonObject root;
    root.insert(QLatin1String(kVersionKey), kSchemaVersion);
    root.insert(QLatin1String(kMonitorsKey), monitors);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool rememberWindowMonitor(WindowMonitorState *state,
                           const QString &programKey,
                           const QString &device)
{
    if (state == nullptr) {
        return false;
    }
    const QString key = programKey.trimmed().toLower();
    const QString value = device.trimmed();
    if (key.isEmpty() || value.isEmpty()) {
        return false;
    }
    const auto it = state->monitors.constFind(key);
    if (it != state->monitors.constEnd() && it.value() == value) {
        return false;
    }
    state->monitors.insert(key, value);
    return true;
}

std::optional<QString> rememberedWindowMonitor(const WindowMonitorState &state,
                                               const QString &programKey)
{
    const QString key = programKey.trimmed().toLower();
    if (key.isEmpty()) {
        return std::nullopt;
    }
    const auto it = state.monitors.constFind(key);
    if (it == state.monitors.constEnd() || it.value().isEmpty()) {
        return std::nullopt;
    }
    return it.value();
}

} // namespace flowkeyd::core
