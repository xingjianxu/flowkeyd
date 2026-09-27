// 在线更新里的**纯逻辑**：查 GitHub Release、解析它的 JSON、挑升级包、比较版本。
//
// 只依赖 QtCore（`QJsonDocument` / `QUrl` 都在 QtCore 里），所以这一层能脱离
// 网络与桌面直接单测（`tst_update` 喂的是固定的 JSON 文本与构造出来的资产列表）。
//
// 为什么不自己拼 GitHub 的 GraphQL / 网页：`GET /repos/{owner}/{repo}/releases/latest`
// 是 GitHub 官方的稳定 REST 接口，返回的 `assets[].browser_download_url` 就指向
// 发布脚本传上去的那两个 zip，不需要任何鉴权（公开仓库、匿名请求，
// 本机实测 60 次/小时的限额对「手动点一下检查更新」绰绰有余）。
//
// 版本号约定与 `core/version.h` 一致：`yy-MM-dd-<git 短修订>`。发布日期（tag）
// 与本地构建版本用的是同一套格式，所以比较规则可以很简单（见 compareBuildVersions）。
#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QUrl>
#include <QVector>

#include <optional>

namespace flowkeyd::core {

/// GitHub 上一个发布资产（`assets[]` 里的一项）。
struct ReleaseAsset
{
    QString name;
    QUrl url;
    qint64 size = 0;
    /// 资产内容的 sha256（小写十六进制），来自 API 的 `digest` 字段
    /// （形如 `sha256:653e3839…`）。老版本 API 没有这个字段时为空。
    QString sha256;
};

/// `GET /releases/latest` 的响应里我们关心的部分。
struct ReleaseInfo
{
    /// 发布 tag，例如 `v26-09-27-95f40ac`。
    QString tag;
    /// 去掉前导 `v` 的构建版本，例如 `26-09-27-95f40ac`（资产名里用的就是它）。
    QString version;
    /// 发布的标题（`name`）。
    QString title;
    /// 发布说明正文（`body`，Markdown）—— 更新窗口里的「主要更新内容」。
    QString notes;
    /// 发布页地址（`html_url`），失败时给用户一个手动下载的入口。
    QUrl pageUrl;
    /// 全部资产。
    QVector<ReleaseAsset> assets;
};

/// 在线更新依赖的仓库（`owner/repo`）。本项目固定指向发布脚本上传的那个仓库。
QString updateRepository();

/// `https://api.github.com/repos/<owner>/<repo>/releases/latest`。
QUrl latestReleaseApiUrl();

/// 解析一个 `GET /releases/latest` 的响应体。失败时返回 `nullopt` 并填英文原因。
///
/// 只做「结构对不对」的检查：`tag_name` 必须有值；`assets` 缺失时当成空列表
/// （调用方随后会因为没有可下载的资产而报错）。
std::optional<ReleaseInfo> parseReleaseJson(const QByteArray &json, QString *error);

/// 从资产列表里挑一个升级包：优先 `*-slim-windows-x64.zip`（只含 exe，约 700 KB），
/// 其次 `*-windows-x64.zip`（完整包，约 25 MB）。
///
/// 没有可用的资产时返回 `nullopt` 并填英文原因（老版本发布脚本只传过一个包、
/// 或者仓库里手动建的 release 没有附件，都会走到这里）。
std::optional<ReleaseAsset> pickUpdateAsset(const QVector<ReleaseAsset> &assets, QString *error);

/// zip 里哪一条是新的可执行文件：`<目录>/flowkeyd.exe`（大小写无关）。
///
/// slim 包里的路径是 `flowkeyd-<版本>-slim/flowkeyd.exe`，完整包是
/// `flowkeyd-<版本>/flowkeyd.exe`；两种都能认出来，所以即使某次发布漏了 slim 包，
/// 在线更新也能退回到完整包（用户多下 25 MB，但不会失败）。
bool isUpdateArchiveEntry(const QString &entryName);

/// 比较两个 `yy-MM-dd-<git 短修订>` 形式的构建版本。
///
/// 返回值是「`candidate` 相对于 `current`」：**正数表示有更新可拿**（`candidate`
/// 更新），0 表示两边一样，负数表示本地这份比 `candidate` 新（自己编译的比发布
/// 的还新，不该提示更新）。
///
/// * 日期不同：日期大的算新。
/// * 日期相同、修订不同：算「候选更新」（返回正）—— 同一天的不同构建
///   （修补、重打）对用户来说就是一次更新。
/// * 完全相同：返回 0。
/// * 有一边解析不出来：字符串相等算 0，否则算「候选更新」（返回正）——
///   宁可多给用户一次「发现新版本」，也不要因为一个奇怪的版本串而永远不提示。
int compareBuildVersions(const QString &current, const QString &candidate);

/// 从构建版本里取日期段（`26-09-27-95f40ac` → `2026-09-27`）。解析不出来时返回
/// `nullopt`；`nullopt` 时无法把下载下来的 exe 的最后写入时间对齐到发布日。
std::optional<QDate> buildVersionDate(const QString &version);

} // namespace flowkeyd::core
