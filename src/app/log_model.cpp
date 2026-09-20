#include "app/log_model.h"

namespace flowkeyd::app {

namespace {

/// 尾随日志文件的轮询间隔（与 oskeyd 一致）。
constexpr int kPollIntervalMs = 250;

} // namespace

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent)
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &LogModel::refresh);
}

void LogModel::setLogFile(const QString &path)
{
    m_tailer.setPath(path);
    beginResetModel();
    m_lines.clear();
    rebuildVisible();
    endResetModel();
    emit countsChanged();
    if (path.isEmpty()) {
        m_timer.stop();
        return;
    }
    refresh();
    m_timer.start();
}

QString LogModel::levelOf(const QString &line)
{
    // 日志器写的格式固定是 `HH:MM:SS LEVEL message`：第 9 个字符起是级别词。
    if (line.size() < 14) {
        return QString();
    }
    return line.mid(9, 5).trimmed();
}

void LogModel::refresh()
{
    // 读失败（文件被删、被独占、正在轮转）不该让日志窗口退出：下一轮再试。
    const QStringList fresh = m_tailer.poll(nullptr);
    if (fresh.isEmpty()) {
        return;
    }
    // 先 beginResetModel 再改数据：这是 Qt 的模型契约（重置期间视图不得
    // 访问模型）。
    beginResetModel();
    for (const QString &line : fresh) {
        m_lines.append(Line{line, levelOf(line)});
    }
    const int limit = core::LogTailer::kMaxBacklogLines;
    if (m_lines.size() > limit) {
        m_lines.remove(0, m_lines.size() - limit);
    }
    rebuildVisible();
    endResetModel();
    emit countsChanged();
    emit appended();
}

void LogModel::rebuildVisible()
{
    m_visible.clear();
    m_visible.reserve(m_lines.size());
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_filter.isEmpty() || m_lines.at(i).text.contains(m_filter, Qt::CaseInsensitive)) {
            m_visible.append(i);
        }
    }
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_visible.size());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return QVariant();
    }
    const Line &line = m_lines.at(m_visible.at(index.row()));
    switch (role) {
    case LineRole:
        return line.text;
    case LevelRole:
        return line.level;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names.insert(LineRole, QByteArrayLiteral("line"));
    names.insert(LevelRole, QByteArrayLiteral("level"));
    return names;
}

void LogModel::setFilter(const QString &filter)
{
    if (m_filter == filter) {
        return;
    }
    m_filter = filter;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countsChanged();
}

} // namespace flowkeyd::app
