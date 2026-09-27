#include "core/update_check.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace flowkeyd::core {

namespace {

/// `26-09-27-95f40ac` 里日期段固定占 8 个字符；第 9 个字符是分隔用的 `-`。
constexpr int kDateLength = 8;
constexpr int kRevisionOffset = 9;

constexpr QLatin1String kSlimSuffix("-slim-windows-x64.zip");
constexpr QLatin1String kFullSuffix("-windows-x64.zip");

} // namespace

QString updateRepository()
{
    // 只有这一个常量决定了在线更新去哪儿找发布：发布脚本（scripts/release.ps1）
    // 上传到的就是这个仓库。换成别的仓库只改这里。
    return QStringLiteral("xingjianxu/flowkeyd");
}

QUrl latestReleaseApiUrl()
{
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                    .arg(updateRepository()));
}

std::optional<ReleaseInfo> parseReleaseJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("the release response is not valid JSON (%1 at offset %2)")
                         .arg(parseError.errorString())
                         .arg(parseError.offset);
        }
        return std::nullopt;
    }
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("the release response is not a JSON object");
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    ReleaseInfo info;
    info.tag = root.value(QStringLiteral("tag_name")).toString().trimmed();
    if (info.tag.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the release response has no tag_name");
        }
        return std::nullopt;
    }
    // tag 形如 `v26-09-27-95f40ac`，资产名里用的是去掉 `v` 的版本。
    info.version = info.tag.startsWith(QLatin1Char('v')) ? info.tag.mid(1) : info.tag;
    info.title = root.value(QStringLiteral("name")).toString().trimmed();
    info.notes = root.value(QStringLiteral("body")).toString();
    info.pageUrl = QUrl(root.value(QStringLiteral("html_url")).toString());

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        ReleaseAsset asset;
        asset.name = object.value(QStringLiteral("name")).toString();
        asset.url = QUrl(object.value(QStringLiteral("browser_download_url")).toString());
        // `size` 是 JSON 数字；发布资产不可能超过 2^53 字节，用 double 取回没有问题。
        asset.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        const QString digest = object.value(QStringLiteral("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:"))) {
            asset.sha256 = digest.mid(7).toLower();
        }
        if (asset.name.isEmpty() || !asset.url.isValid()) {
            continue;
        }
        info.assets.append(asset);
    }
    return info;
}

std::optional<ReleaseAsset> pickUpdateAsset(const QVector<ReleaseAsset> &assets, QString *error)
{
    // slim 包优先。注意 slim 的名字**也**以 `-windows-x64.zip` 结尾，
    // 所以必须先检查更长的那个后缀、且分两趟走。
    for (const ReleaseAsset &asset : assets) {
        if (asset.name.endsWith(kSlimSuffix, Qt::CaseInsensitive)) {
            return asset;
        }
    }
    for (const ReleaseAsset &asset : assets) {
        if (asset.name.endsWith(kFullSuffix, Qt::CaseInsensitive)) {
            return asset;
        }
    }
    if (error != nullptr) {
        *error = QStringLiteral("the release has no `*-slim-windows-x64.zip` or "
                                "`*-windows-x64.zip` asset to download");
    }
    return std::nullopt;
}

bool isUpdateArchiveEntry(const QString &entryName)
{
    return entryName.endsWith(QLatin1String("/flowkeyd.exe"), Qt::CaseInsensitive)
        || entryName.compare(QLatin1String("flowkeyd.exe"), Qt::CaseInsensitive) == 0;
}

std::optional<QDate> buildVersionDate(const QString &version)
{
    if (version.size() < kDateLength) {
        return std::nullopt;
    }
    const QString text = version.left(kDateLength);
    if (text.at(2) != QLatin1Char('-') || text.at(5) != QLatin1Char('-')) {
        return std::nullopt;
    }
    // 自己拼年份，**不要**用 `QDate::fromString(text, "yy-MM-dd")`：Qt 对两位
    // 年份用的是一条“距当前年份 ±50 年”的启发式规则，本机实测 `26` 被解成了
    // **1926**（而同一个格式反过来输出的就是 `yy`）。构建版本里的日期一定是
    // 20xx，直接加 2000 才能保证往返一致。
    bool yearOk = false;
    bool monthOk = false;
    bool dayOk = false;
    const int year = text.left(2).toInt(&yearOk);
    const int month = text.mid(3, 2).toInt(&monthOk);
    const int day = text.mid(6, 2).toInt(&dayOk);
    if (!yearOk || !monthOk || !dayOk) {
        return std::nullopt;
    }
    const QDate date(2000 + year, month, day);
    return date.isValid() ? std::optional<QDate>(date) : std::nullopt;
}

int compareBuildVersions(const QString &current, const QString &candidate)
{
    const std::optional<QDate> currentDate = buildVersionDate(current);
    const std::optional<QDate> candidateDate = buildVersionDate(candidate);
    if (currentDate.has_value() && candidateDate.has_value()) {
        // 返回值是「candidate 比 current 新多少」：正数表示可以更新。
        if (*currentDate != *candidateDate) {
            return *currentDate < *candidateDate ? 1 : -1;
        }
        // 同一天的不同构建（修 bug 后重打）也算一次更新：日期段相同，
        // 只有 git 修订不同。
        const QString currentRevision = current.mid(kRevisionOffset);
        const QString candidateRevision = candidate.mid(kRevisionOffset);
        return currentRevision == candidateRevision ? 0 : 1;
    }
    // 有一边不是一个正常的构建版本（本地手拼的、或 `unknown-…`）：
    // 只敢说“同一个字符串才算一样”。
    return current == candidate ? 0 : 1;
}

} // namespace flowkeyd::core
