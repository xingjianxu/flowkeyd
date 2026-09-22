// 构建版本信息（纯逻辑，不碰 Win32、不碰 GUI）。
//
// **机制**：版本 = 项目版本号 + 构建时间戳，两者合成一个字符串给托盘菜单、
// 启动日志与 `--version` 用。时间戳取**正在运行的这个 exe 自己的最后写入时间**
// —— 也就是链接产物被写到磁盘的那一刻，所以每次重新构建它都会跟着变。
//
// 为什么不用编译期常量（CMake 在构建时生成一个 `FLOWKEYD_BUILD_TIMESTAMP`）：
// 那样每次 `cmake --build` 都要重新生成头文件、重新编译并重新链接 exe，
// 于是「什么都没改」的构建也不再是 `ninja: no work to do`；更糟的是常驻实例
// 正在跑的那个 exe 会被锁住，连一次「检查是否最新」的构建都会失败。
// 读 exe 的时间戳则只在 exe 真的被重新链接时才变，而且不需要任何构建脚本
// （实测过 MinGW 的 PE 头 `TimeDateStamp` 不是链接时间，是个固定的小数值，
// 所以只能读文件时间）。
//
// 项目所有者 2026-09 拍板：**版本号先采用构建的时间戳表示**。以后要换成
// git 描述之类的来源，只改这个文件（其它地方只消费 `buildVersion()`）。
#pragma once

#include <QString>

namespace flowkeyd::core {

/// 编译时定下的项目版本号（`CMakeLists.txt` 的 `project(... VERSION ...)`）。
QString projectVersion();

/// 从一个文件的最后写入时间推导构建时间戳（本地时间，`yyyy-MM-dd HH:mm:ss`）。
///
/// 路径为空、文件不存在、或时间不可读时返回 `unknown`。取本地时间是为了让人
/// 一眼能和自己动手构建的时刻对上。
QString buildTimestampFromFile(const QString &path);

/// 把版本号与时间戳合成一行：`0.1.0 (build 2026-09-22 17:02:55)`。
/// 时间戳缺失（空或 `unknown`）时只返回版本号，不会留下半个括号。
QString formatBuildVersion(const QString &version, const QString &timestamp);

/// 运行中可执行文件的构建版本字符串，等价于
/// `formatBuildVersion(projectVersion(), buildTimestampFromFile(executablePath))`。
QString buildVersion(const QString &executablePath);

/// 时间戳缺失时的占位词（`formatBuildVersion` 认它，测试也用它）。
QString unknownTimestamp();

} // namespace flowkeyd::core
