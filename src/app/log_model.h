// 日志窗口的模型：把 `core::LogTailer` 尾随到的行做成 QML 能直接吃的列表。
//
// 只有 QtCore（`QAbstractListModel` / `QTimer`），不碰 Win32、不碰 QML 控件，
// 所以「读文件 → 成行 → 上限 → 过滤」这条链可以单独想清楚；
// 真正的渲染在 `LogWindow.qml`。
#pragma once

#include "core/log_tail.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QVector>

namespace flowkeyd::app {

class LogModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)

public:
    enum Role {
        /// 整行文本（`HH:MM:SS LEVEL message`）。
        LineRole = Qt::UserRole + 1,
        /// 级别词（`ERROR`/`WARN`/`INFO`/`DEBUG`/`TRACE`），QML 据此配色。
        LevelRole,
    };

    explicit LogModel(QObject *parent = nullptr);

    /// 尾随哪个文件；空路径表示不尾随。
    void setLogFile(const QString &path);

    /// 立即读一次文件（打开窗口时立刻看到内容，测试也用它）。
    void refresh();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int visibleCount() const { return static_cast<int>(m_visible.size()); }
    int totalCount() const { return static_cast<int>(m_lines.size()); }

signals:
    void filterChanged();
    void countsChanged();
    /// 有新行进入模型（QML 用它自动滚到底）。
    void appended();

private:
    struct Line
    {
        QString text;
        QString level;
    };

    /// 从 `HH:MM:SS LEVEL message` 里取出级别词；取不到时返回空串。
    static QString levelOf(const QString &line);

    void rebuildVisible();

    QVector<Line> m_lines;
    /// `m_lines` 中通过过滤的下标，顺序不变。
    QVector<int> m_visible;
    core::LogTailer m_tailer;
    QTimer m_timer;
    QString m_filter;
};

} // namespace flowkeyd::app
