// 构建版本号（纯逻辑，不碰 Win32、不碰 GUI）。
//
// **机制**：版本号形如 `yy-MM-dd-<git 短修订>`（例如 `26-09-22-42900ad`），
// 它显示在托盘右键菜单、守护进程启动日志的第一行、`--version` 与 `--help` 的
// 第一行 —— 这样一眼就能确认正在跑的是哪一次构建、来自哪个提交。
//
// * **日期** 取**正在运行的这个 exe 自己的最后写入时间**（本地时间），也就是它
//   被链接到磁盘的时刻，所以每次重新构建它都会跟着变。
// * **修订** 是构建时由 CMake 用 `git rev-parse --short HEAD` 取到、写进
//   `flowkeyd_revision.h` 的编译期常量（见 CMakeLists.txt 与第 10 节）。
//   构建时拿不到 git 时是 `unknown`。
//
// 为什么日期用 exe 的时间而不是再生成一个编译期常量：那样每次 `cmake --build`
// 都要重新生成头文件、重新编译并重新链接 exe（还要跑两遍 `windeployqt`），于是
// 「什么都没改」的构建也不再是 `ninja: no work to do`；更糟的是常驻实例正在跑的
// 那个 exe 是锁住的，连一次「检查是否最新」的构建都会失败。git 修订只在提交 /
// 切分支时才变，所以它做成编译期常量是合适的（CMake 把 .git 的元数据登记成了
// configure 依赖，提交之后重新构建会自动刷新）。
//
// 项目所有者 2026-09 拍板：版本号先取构建的时间戳（`yyyyMMddHHmm`），随后改成
// `yy-MM-dd-<git 短修订>`。要再改格式，只改这个文件。
#pragma once

#include <QString>

namespace flowkeyd::core {

/// 运行中可执行文件的构建版本号：`yy-MM-dd-<git 短修订>`（例如
/// `26-09-22-42900ad`）。日期读不到时该段是 `unknownValue()`。
QString buildVersion(const QString &executablePath);

/// 从一个文件的最后写入时间推导构建日期（本地时间，`yy-MM-dd`）。
///
/// 路径为空、文件不存在、或时间不可读时返回 `unknownValue()`。取本地时间
/// 是为了让人一眼能和自己动手构建的时刻对上。
QString buildDateFromFile(const QString &path);

/// 编译时嵌入的 git 短修订（`git rev-parse --short HEAD`，例如 `42900ad`）。
/// 构建时拿不到 git 时返回 `unknownValue()`。
QString sourceRevision();

/// 某一段取不到时的占位词（`buildVersion` 认它，测试也用它）。
QString unknownValue();

} // namespace flowkeyd::core
