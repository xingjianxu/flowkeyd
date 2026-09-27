// 从 GitHub Release 的 zip 里取出新的 `flowkeyd.exe`。
//
// 这里用的是 Qt **私有**的 `QZipReader`（`<QtCore/private/qzipreader_p.h>`，
// 本机 Qt 6.11 的 `Qt6Core.dll` 确实导出了它，`Qt6::CorePrivate` 提供私有头）。
// 选它的理由（项目所有者 2026-09 拍板）：发布脚本已经会传一个「精简升级包」
// （slim zip，只含 exe，约 700 KB），在线更新直接下它最省流量；而为了解压一个
// 只有一个文件的 zip 去引第三方 zlib、或者自己写 inflate，都不值得。
// 代价是这一层绑在 Qt 的私有 API 上 —— 所以它被隔离在这一个文件里，
// 升级 Qt 时只需要看这里。（`AGENTS.md` 第 10 节记了这个取舍。）
//
// 只做「取出字节 + 像不像 PE」，不碰窗口、不碰网络：`Updater` 负责调用它。
#pragma once

#include <QByteArray>
#include <QString>

namespace flowkeyd::app {

/// 一个 Windows 可执行文件至少要有这么大（PE 头 + 一点内容）。低于这个尺寸
/// 的一律当成「下载下来的不是我们的 exe」——本项目的 exe 精简之后也有 1.6 MB。
bool looksLikeExecutable(const QByteArray &bytes);

/// 从发布 zip 的字节里取出 `flowkeyd.exe` 并写到 `destination`（覆盖已有的）。
///
/// zip 里的条目形状是 `<目录>/flowkeyd.exe`（slim 包是
/// `flowkeyd-<版本>-slim/flowkeyd.exe`，完整包是 `flowkeyd-<版本>/flowkeyd.exe`），
/// 由 `core::isUpdateArchiveEntry()` 认。
///
/// 失败时返回 false 并填**英文**原因（写日志用）：
///   * 不是 zip / 读不出中央目录；
///   * 里面没有 `flowkeyd.exe` 条目；
///   * 解压出来是空的，或者不像一个 Windows 可执行文件；
///   * 目标目录写不进去（安装目录只读、磁盘满……）。
bool extractUpdateExecutable(const QByteArray &archive,
                             const QString &destination,
                             QString *error);

} // namespace flowkeyd::app
