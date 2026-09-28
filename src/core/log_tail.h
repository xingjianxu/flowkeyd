// 日志文件的增量尾随（纯逻辑，不碰 Win32、不碰 GUI）。
//
// 日志窗口尾随守护进程写的日志文件，每 250 ms 读一次新增的字节。
// 这里只做「把新增字节变成完整行」这件事，因此可以被 Qt Test 直接覆盖。
//
// 三个必须小心的地方：
//
// 1. **按字节读，不按文本读。** 日志器可能正好写到多字节 UTF-8 字符的中间，
//    那时按文本解码会因非法 UTF-8 直接报错。
// 2. **末尾不完整的 UTF-8 序列不消费**，留到下一轮——它属于还没写完的那一行。
// 3. **半行不输出**，留在 `pending` 里等换行，否则会把一个词拆成两行显示。
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace flowkeyd::core {

/// 日志文件里从头开始可以安全解码的字节数。
///
/// 末尾处一个被截断的多字节 UTF-8 序列（日志器正好写到一半）会被排除，
/// 留到下一次读取。非法字节不插手，照常交给 `QString::fromUtf8` 用替换字符表示。
int completeUtf8PrefixLen(const QByteArray &bytes);

/// 日志文件的增量读取器。
class LogTailer
{
public:
    /// 一次最多返回多少行（追上很久以前的日志时只显示最后一段）。
    static constexpr int kMaxBacklogLines = 1000;

    LogTailer() = default;
    explicit LogTailer(QString path) : m_path(std::move(path)) {}

    void setPath(const QString &path)
    {
        m_path = path;
        reset();
    }
    const QString &path() const { return m_path; }

    /// 读出上次之后新增的**完整**行。文件不存在不算错误（守护进程刚启动）。
    /// 读失败时返回空列表，并把英文原因写进 `error`。
    QStringList poll(QString *error = nullptr);

    /// 因为积压太多而丢掉了行（调用方可以据此提示一句）。
    bool truncated() const { return m_truncated; }

    /// 丢弃内部状态（下一次从头开始读）。
    void reset();

private:
    QString m_path;
    /// 已经读到文件里的第几个字节。
    qint64 m_offset = 0;
    /// 上一轮读到的、还没有以换行结尾的那半行。
    QString m_pending;
    bool m_truncated = false;
};

} // namespace flowkeyd::core
