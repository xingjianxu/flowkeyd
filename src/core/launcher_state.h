// 程序启动器（`apps` 动作）的**持久状态**：哪些程序被「固定」了、最近启动过哪些。
//
// 纯逻辑（只用 QtCore），所以 `tst_launcher_state` 直接覆盖解析 / 序列化与两个
// 列表操作。真正的读写（`QFile`）在 `app::PopupHost` 里 —— 模型层不碰磁盘。
//
// 条目的身份是**图标键**（`core::appIconKey()`，快捷方式路径的 64 位 FNV-1a）。
// 为什么不存快捷方式路径本身：路径的大小写与分隔符会把同一个程序写成两个身份；
// 而这个键与 `image://flowkeyd-app/…` 用的是同一个，列表重扫、目录搬家之后
// 认得出同一份开始菜单条目。
//
// 状态文件与配置文件同目录（`launcher.json`）。**它不进仓库、也不用用户手写**：
// 用户在启动器里按 `Space` 固定，启动时自动记一笔「最近使用」。
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace flowkeyd::core {

/// 「最近使用」列表最多记几条。
///
/// 与卡片里那两行网格对应（`AppListModel` 一次画 6 列 × 2 行 = 12 个）。
/// 多留几条也没有地方显示，所以落盘时就截到这里。
inline constexpr int kRecentLimit = 12;

/// 启动器里「固定」与「最近使用」两份**图标键**列表。
struct LauncherState
{
    /// 固定住的程序，按用户固定的顺序（后固定的排在后面）。
    QStringList pinned;
    /// 最近启动过的程序，**最近的在前**（最多 `kRecentLimit` 条）。
    QStringList recent;
};

/// 状态文件的路径：与配置文件同一个目录下的 `launcher.json`。
///
/// `configPath` 是相对路径时按当前目录解析（与 `--config` 的行为一致）。
QString launcherStatePath(const QString &configPath);

/// 解析一份状态文件。**容错**：坏 JSON、字段类型不对、条目不是非空字符串……
/// 一律当作「那部分为空」，`error` 里（非空时）给一句英文说明。
///
/// 为什么容错而不是报错：这个文件是程序自己写的运行状态，坏掉时最合理的反应是
/// 当作空的重新开始，而不是让启动器打不开。
LauncherState parseLauncherState(const QByteArray &json, QString *error = nullptr);

/// 序列化成稳定的、人能看懂的 JSON（两个空格缩进、字段顺序固定、带版本号）。
QByteArray serializeLauncherState(const LauncherState &state);

/// 把 `key` 挪到 `recent` 的最前面（已经在里面就只是提前），并截到
/// `kRecentLimit`。空 `key` 原样返回。
QStringList touchRecent(const QStringList &recent, const QString &key);

/// 切换固定状态：在 `pinned` 里就把那一条去掉，否则**追加到末尾**。
/// 空 `key` 原样返回。
QStringList togglePinned(const QStringList &pinned, const QString &key);

} // namespace flowkeyd::core
