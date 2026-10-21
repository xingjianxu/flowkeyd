#include "core/workspace_log.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <optional>

namespace flowkeyd::core {

namespace {

constexpr int kSchemaVersion = 1;

constexpr char kVersionKey[] = "version";
constexpr char kWindowsKey[] = "windows";

constexpr char kHwndKey[] = "hwnd";
constexpr char kPidKey[] = "pid";
constexpr char kProcessStartedKey[] = "process_started";
constexpr char kClassKey[] = "class";
constexpr char kDesktopKey[] = "desktop";
constexpr char kMonitorKey[] = "monitor";
constexpr char kSpaceKey[] = "space";
constexpr char kTitleKey[] = "title";

/// 64 位整数既接受字符串（我们自己写的形状）也接受数字（手改出来的）：
/// 数字超过 2^53 时 `QJsonValue::toDouble()` 已经丢了精度，所以那种情况当没读到。
std::uint64_t readUInt64(const QJsonValue &value)
{
    if (value.isString()) {
        bool ok = false;
        const qulonglong parsed = value.toString().toULongLong(&ok);
        return ok ? static_cast<std::uint64_t>(parsed) : 0;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number < 0.0) {
            return 0;
        }
        return static_cast<std::uint64_t>(number);
    }
    return 0;
}

/// 读一条；字段不够或自相矛盾时返回 `std::nullopt`（那一条被跳过）。
std::optional<HiddenWindow> readEntry(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    HiddenWindow entry;
    entry.window = readUInt64(object.value(QLatin1String(kHwndKey)));
    if (entry.window == 0) {
        return std::nullopt;
    }
    const QJsonValue pid = object.value(QLatin1String(kPidKey));
    if (pid.isDouble() && pid.toDouble() > 0.0) {
        entry.identity.pid = static_cast<std::uint32_t>(pid.toDouble());
    }
    entry.identity.processStarted = readUInt64(object.value(QLatin1String(kProcessStartedKey)));
    entry.identity.className = object.value(QLatin1String(kClassKey)).toString();
    // 只有在「进程号 + 类名」都拿得到时才声称身份有效（与 `identityOf()` 一致）；
    // 否则恢复时不做校验，照常尝试把窗口放回来。
    entry.identity.valid = entry.identity.pid != 0 && !entry.identity.className.isEmpty();

    entry.workspace.desktop = object.value(QLatin1String(kDesktopKey)).toString();
    entry.workspace.monitor = object.value(QLatin1String(kMonitorKey)).toString();
    const QJsonValue space = object.value(QLatin1String(kSpaceKey));
    if (space.isDouble() && space.toDouble() >= 1.0) {
        entry.workspace.index = static_cast<std::uint32_t>(space.toDouble());
    }
    entry.title = object.value(QLatin1String(kTitleKey)).toString();
    return entry;
}

} // namespace

QString workspaceLogPath(const QString &configPath)
{
    const QFileInfo info(configPath);
    return info.absoluteDir().filePath(QStringLiteral("workspace.json"));
}

WorkspaceLog parseWorkspaceLog(const QByteArray &json, QString *error)
{
    WorkspaceLog log;
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
        // 空文件 = 没有窗口被藏着（正常情况下我们就把它删掉了）。
        return log;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(QStringLiteral("the workspace log is not valid JSON: %1")
                 .arg(parseError.errorString()));
        return log;
    }
    if (!document.isObject()) {
        fail(QStringLiteral("the workspace log must be a JSON object"));
        return log;
    }
    const QJsonValue windows = document.object().value(QLatin1String(kWindowsKey));
    if (windows.isUndefined() || windows.isNull()) {
        return log;
    }
    if (!windows.isArray()) {
        fail(QStringLiteral("`windows` in the workspace log must be an array"));
        return log;
    }
    for (const QJsonValue &item : windows.toArray()) {
        const std::optional<HiddenWindow> entry = readEntry(item);
        if (!entry.has_value()) {
            continue; // 坏了的那一条丢掉，其余照旧
        }
        addHiddenWindow(log, *entry); // 顺带去重
    }
    return log;
}

QByteArray serializeWorkspaceLog(const WorkspaceLog &log)
{
    QJsonObject root;
    root.insert(QLatin1String(kVersionKey), kSchemaVersion);
    QJsonArray windows;
    for (const HiddenWindow &entry : log.windows) {
        QJsonObject object;
        object.insert(QLatin1String(kHwndKey), QString::number(entry.window));
        object.insert(QLatin1String(kPidKey), static_cast<double>(entry.identity.pid));
        object.insert(QLatin1String(kProcessStartedKey),
                      QString::number(entry.identity.processStarted));
        object.insert(QLatin1String(kClassKey), entry.identity.className);
        object.insert(QLatin1String(kDesktopKey), entry.workspace.desktop);
        object.insert(QLatin1String(kMonitorKey), entry.workspace.monitor);
        object.insert(QLatin1String(kSpaceKey), static_cast<double>(entry.workspace.index));
        object.insert(QLatin1String(kTitleKey), entry.title);
        windows.append(object);
    }
    root.insert(QLatin1String(kWindowsKey), windows);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void addHiddenWindow(WorkspaceLog &log, const HiddenWindow &entry)
{
    if (entry.window == 0) {
        return;
    }
    const auto existing = std::find_if(log.windows.begin(), log.windows.end(),
                                       [&entry](const HiddenWindow &item) {
                                           return item.window == entry.window;
                                       });
    if (existing != log.windows.end()) {
        *existing = entry;
        return;
    }
    log.windows.push_back(entry);
}

bool removeHiddenWindow(WorkspaceLog &log, WindowId window)
{
    const auto existing = std::find_if(log.windows.begin(), log.windows.end(),
                                       [window](const HiddenWindow &item) {
                                           return item.window == window;
                                       });
    if (existing == log.windows.end()) {
        return false;
    }
    log.windows.erase(existing);
    return true;
}

bool hasHiddenWindow(const WorkspaceLog &log, WindowId window)
{
    return std::any_of(log.windows.begin(), log.windows.end(),
                       [window](const HiddenWindow &item) { return item.window == window; });
}

} // namespace flowkeyd::core
