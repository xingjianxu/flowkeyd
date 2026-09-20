#include "core/log_tail.h"

#include <QFile>

namespace flowkeyd::core {

int completeUtf8PrefixLen(const QByteArray &bytes)
{
    // 从末尾往前找多字节序列的首字节，最多回退 3 个字节。
    const qsizetype length = bytes.size();
    const qsizetype start = std::max<qsizetype>(0, length - 3);
    for (qsizetype index = length - 1; index >= start; --index) {
        const auto byte = static_cast<unsigned char>(bytes.at(index));
        if ((byte & 0b1100'0000) == 0b1000'0000) {
            // 续字节：继续往前找首字节。
            continue;
        }
        int needed = 0;
        if ((byte & 0b1000'0000) == 0) {
            needed = 1;
        } else if ((byte & 0b1110'0000) == 0b1100'0000) {
            needed = 2;
        } else if ((byte & 0b1111'0000) == 0b1110'0000) {
            needed = 3;
        } else if ((byte & 0b1111'1000) == 0b1111'0000) {
            needed = 4;
        } else {
            // 非法首字节：不插手，交给调用方用替换字符表示。
            return static_cast<int>(length);
        }
        return (length - index >= needed) ? static_cast<int>(length) : static_cast<int>(index);
    }
    return static_cast<int>(length);
}

void LogTailer::reset()
{
    m_offset = 0;
    m_pending.clear();
    m_truncated = false;
}

QStringList LogTailer::poll(QString *error)
{
    QStringList lines;
    QFile file(m_path);
    if (!file.exists()) {
        // 文件还没出现（守护进程刚启动）不算错误。
        return lines;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1: %2").arg(m_path, file.errorString());
        }
        return lines;
    }
    const qint64 length = file.size();
    if (length < m_offset) {
        // 文件被截断或轮转了：从头再来，并丢掉可能残留的半行。
        m_offset = 0;
        m_pending.clear();
    }
    if (length == m_offset) {
        return lines;
    }
    if (!file.seek(m_offset)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot seek %1: %2").arg(m_path, file.errorString());
        }
        return lines;
    }
    QByteArray bytes = file.readAll();
    file.close();

    // 结尾那个不完整的 UTF-8 序列先不消费：它属于还没写完的那一行。
    const int complete = completeUtf8PrefixLen(bytes);
    bytes.truncate(complete);
    m_offset += complete;
    m_pending += QString::fromUtf8(bytes);

    while (true) {
        const qsizetype index = m_pending.indexOf(QLatin1Char('\n'));
        if (index < 0) {
            break;
        }
        QString line = m_pending.left(index);
        m_pending.remove(0, index + 1);
        while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        lines.append(line);
    }

    // 追上很久以前的日志时只显示最后一段，不然用户要等它刷完。
    if (lines.size() > kMaxBacklogLines) {
        const qsizetype dropped = lines.size() - kMaxBacklogLines;
        for (qsizetype i = 0; i < dropped; ++i) {
            lines.removeFirst();
        }
        m_truncated = true;
    }
    return lines;
}

} // namespace flowkeyd::core
