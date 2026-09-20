// 在动作字符串中运行时展开 `{placeholder}` 模板。
//
// 动作在执行时能引用的一切都在 `Vars` 中。未知占位符保持原样，
// 因此命令行或路径里的一个字面 `{` 不会被吃掉。
#pragma once

#include <QMap>
#include <QString>

#include <cstdint>
#include <optional>

namespace flowkeyd::core {

/// 日期/时间占位符使用的已拆分本地时间。
struct LocalTime
{
    qint64 year = 1970;
    std::uint32_t month = 1;
    std::uint32_t day = 1;
    std::uint32_t hour = 0;
    std::uint32_t minute = 0;
    std::uint32_t second = 0;

    QString date() const;      // 2026-09-18
    QString time() const;      // 13:46:07
    QString stamp() const;     // 20260918-134607
    QString dateTime() const;  // 2026-09-18 13:46:07

    /// 把 UNIX 时间戳（UTC）转换为已拆分时间。
    static LocalTime fromUnix(qint64 secs);
    static LocalTime nowUtc();
};

/// Howard Hinnant 的 `civil_from_days`（自 1970-01-01 起的天数 -> 年/月/日）。
void civilFromDays(qint64 days, qint64 *year, std::uint32_t *month, std::uint32_t *day);

/// 当前 UNIX 时间戳（秒），系统中所有时间读取的唯一入口。
qint64 unixSeconds();

/// `{placeholder}` 展开时可用的值。
struct Vars
{
    /// 触发该动作的快捷键名，例如 `Ctrl+Alt+T`。
    QString hotkey;
    /// 剪贴板文本，仅在模板需要时才读取。
    std::optional<QString> clipboard;
    /// 当前选中的文本（合成复制之后的剪贴板内容）。
    std::optional<QString> selection;
    /// 配置文件所在的目录。
    QString configDir;
    /// 正在运行的可执行文件所在的目录。
    std::optional<QString> exeDir;
    /// `%USERPROFILE%` 的值。
    std::optional<QString> userProfile;
    /// 本地墙上时钟（当平台能提供时）。
    std::optional<LocalTime> localTime;
    /// 由动作提供的额外变量（例如捕获到的命令输出）。
    QMap<QString, QString> extra;
};

/// 展开 `input` 中的 `{name}` 占位符。
QString expand(const QString &input, const Vars &vars);

/// `input` 中是否至少含有一个 `{clipboard}` / `{selection}`；
/// 用于避免不必要的剪贴板读取。
bool needsClipboard(const QString &input);

/// `input` 是否引用了 `{selection}`。
bool needsSelection(const QString &input);

} // namespace flowkeyd::core
