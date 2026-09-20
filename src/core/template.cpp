#include "core/template.h"

#include <QDateTime>
#include <QDir>

namespace flowkeyd::core {

QString LocalTime::date() const
{
    return QStringLiteral("%1-%2-%3")
        .arg(year, 4, 10, QLatin1Char('0'))
        .arg(month, 2, 10, QLatin1Char('0'))
        .arg(day, 2, 10, QLatin1Char('0'));
}

QString LocalTime::time() const
{
    return QStringLiteral("%1:%2:%3")
        .arg(hour, 2, 10, QLatin1Char('0'))
        .arg(minute, 2, 10, QLatin1Char('0'))
        .arg(second, 2, 10, QLatin1Char('0'));
}

QString LocalTime::stamp() const
{
    return QStringLiteral("%1%2%3-%4%5%6")
        .arg(year, 4, 10, QLatin1Char('0'))
        .arg(month, 2, 10, QLatin1Char('0'))
        .arg(day, 2, 10, QLatin1Char('0'))
        .arg(hour, 2, 10, QLatin1Char('0'))
        .arg(minute, 2, 10, QLatin1Char('0'))
        .arg(second, 2, 10, QLatin1Char('0'));
}

QString LocalTime::dateTime() const
{
    return date() + QLatin1Char(' ') + time();
}

void civilFromDays(qint64 z, qint64 *year, std::uint32_t *month, std::uint32_t *day)
{
    z += 719468;
    const qint64 era = (z >= 0 ? z : z - 146096) / 146097;
    const qint64 doe = z - era * 146097;                                            // [0, 146096]
    const qint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;       // [0, 399]
    const qint64 y = yoe + era * 400;
    const qint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                     // [0, 365]
    const qint64 mp = (5 * doy + 2) / 153;                                          // [0, 11]
    const qint64 d = doy - (153 * mp + 2) / 5 + 1;                                  // [1, 31]
    const qint64 m = mp < 10 ? mp + 3 : mp - 9;                                     // [1, 12]
    *year = m <= 2 ? y + 1 : y;
    *month = static_cast<std::uint32_t>(m);
    *day = static_cast<std::uint32_t>(d);
}

LocalTime LocalTime::fromUnix(qint64 secs)
{
    qint64 days = secs / 86400;
    qint64 rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    LocalTime result;
    civilFromDays(days, &result.year, &result.month, &result.day);
    result.hour = static_cast<std::uint32_t>(rem / 3600);
    result.minute = static_cast<std::uint32_t>((rem % 3600) / 60);
    result.second = static_cast<std::uint32_t>(rem % 60);
    return result;
}

LocalTime LocalTime::nowUtc()
{
    return LocalTime::fromUnix(unixSeconds());
}

qint64 unixSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

namespace {

std::optional<QString> lookup(const QString &name, const Vars &vars)
{
    const QString key = name.trimmed();
    const QString lower = key.toLower();

    const qsizetype colon = key.indexOf(QLatin1Char(':'));
    if (colon > 0 && key.left(colon).compare(QLatin1String("env"), Qt::CaseInsensitive) == 0) {
        // 环境变量名保留用户书写的大小写（Windows 环境变量不区分大小写，
        // 但保留原样更不容易让人困惑）。
        const QString envName = key.mid(colon + 1).trimmed();
        return qEnvironmentVariable(envName.toUtf8().constData());
    }

    const auto time = [&vars]() { return vars.localTime.value_or(LocalTime::nowUtc()); };

    if (lower == QLatin1String("clipboard")) {
        return vars.clipboard.value_or(QString());
    }
    if (lower == QLatin1String("selection")) {
        return vars.selection.value_or(QString());
    }
    if (lower == QLatin1String("hotkey") || lower == QLatin1String("name")) {
        return vars.hotkey;
    }
    if (lower == QLatin1String("date")) {
        return time().date();
    }
    if (lower == QLatin1String("time")) {
        return time().time();
    }
    if (lower == QLatin1String("timestamp")) {
        return time().stamp();
    }
    if (lower == QLatin1String("datetime")) {
        return time().dateTime();
    }
    if (lower == QLatin1String("unix")) {
        return QString::number(unixSeconds());
    }
    if (lower == QLatin1String("config_dir")) {
        return vars.configDir;
    }
    if (lower == QLatin1String("exe_dir")) {
        return vars.exeDir.value_or(QString());
    }
    if (lower == QLatin1String("cwd")) {
        return QDir::currentPath();
    }
    if (lower == QLatin1String("userprofile") || lower == QLatin1String("user_profile")) {
        return vars.userProfile.value_or(QString());
    }
    if (lower == QLatin1String("temp")) {
        return QDir::tempPath();
    }

    const auto exact = vars.extra.constFind(key);
    if (exact != vars.extra.constEnd()) {
        return exact.value();
    }
    const auto folded = vars.extra.constFind(lower);
    if (folded != vars.extra.constEnd()) {
        return folded.value();
    }
    return std::nullopt;
}

} // namespace

QString expand(const QString &input, const Vars &vars)
{
    QString out;
    out.reserve(input.size());
    const qsizetype length = input.size();
    qsizetype i = 0;

    while (i < length) {
        const QChar c = input.at(i);
        if (c == u'{') {
            if (i + 1 < length && input.at(i + 1) == u'{') {
                out.append(QLatin1Char('{'));
                i += 2;
                continue;
            }
            qsizetype j = i + 1;
            QString name;
            while (j < length && input.at(j) != u'}') {
                name.append(input.at(j));
                ++j;
            }
            if (j >= length) {
                // 未闭合：把剩余部分原样输出。
                out.append(input.mid(i));
                break;
            }
            if (const auto value = lookup(name, vars); value.has_value()) {
                out.append(*value);
            } else {
                // 不是已知变量：保留原文。
                out.append(input.mid(i, j - i + 1));
            }
            i = j + 1;
            continue;
        }
        if (c == u'}' && i + 1 < length && input.at(i + 1) == u'}') {
            out.append(QLatin1Char('}'));
            i += 2;
            continue;
        }
        out.append(c);
        ++i;
    }
    return out;
}

bool needsClipboard(const QString &input)
{
    const QString lower = input.toLower();
    return lower.contains(QLatin1String("{clipboard}")) || lower.contains(QLatin1String("{selection}"));
}

bool needsSelection(const QString &input)
{
    return input.toLower().contains(QLatin1String("{selection}"));
}

} // namespace flowkeyd::core
