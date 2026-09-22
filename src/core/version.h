// 构建版本号（纯逻辑，不碰 Win32、不碰 GUI）。
//
// **机制**：版本号就是**构建这个 exe 的时间戳**，格式 `yyyyMMddHHmm`
// （例如 `202609221718`）。它显示在托盘右键菜单、守护进程启动日志的第一行、
// `--version` 与 `--help` 的第一行 —— 这样一眼就能确认正在跑的是不是最新构建。
//
// 时间戳取的是**正在运行的这个 exe 自己的最后写入时间**，也就是链接产物被写到
// 磁盘的那一刻，所以每次重新构建它都会跟着变。
//
// 为什么不用编译期常量（CMake 在构建时生成一个版本头文件）：那样每次
// `cmake --build` 都要重新生成头文件、重新编译并重新链接 exe（还要跑两遍
// `windeployqt`），于是「什么都没改」的构建也不再是 `ninja: no work to do`；
// 更糟的是常驻实例正在跑的那个 exe 是锁住的，连一次「检查是否最新」的构建都会
// 失败。读 exe 的时间戳则只在 exe 真的被重新链接时才变，而且不需要任何构建脚本。
// （实测过 MinGW 的 PE 头 `TimeDateStamp` 不是链接时间，是个固定的小数值，
// 所以只能读文件时间。）
//
// 项目所有者 2026-09 拍板：版本号先用构建的时间戳表示。以后要换成 git 描述之类
// 的来源，只改这个文件（其它地方只消费 `buildVersion()`）。
#pragma once

#include <QString>

namespace flowkeyd::core {

/// 运行中可执行文件的构建版本号，格式 `yyyyMMddHHmm`（本地时间，例如
/// `202609221718`）。读不到 exe 的最后写入时间时返回 `unknownTimestamp()`。
QString buildVersion(const QString &executablePath);

/// 从一个文件的最后写入时间推导构建时间戳（本地时间，`yyyyMMddHHmm`）。
///
/// 路径为空、文件不存在、或时间不可读时返回 `unknownTimestamp()`。取本地时间
/// 是为了让人一眼能和自己动手构建的时刻对上。
QString buildTimestampFromFile(const QString &path);

/// 时间戳缺失时的占位词（`buildVersion` 认它，测试也用它）。
QString unknownTimestamp();

} // namespace flowkeyd::core
