// 日志窗口（进程内的 QML 窗口，FluentWinUI3）。
//
// 与 oskeyd 的**独立进程 + 真控制台**不同：这里就是一个普通窗口，
// 关掉它绝不能退出应用（见 AGENTS.md 第 7 节第 20 条）。窗口里的
// `LogModel` 尾随同一个日志文件，因此不需要跨进程交接。
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QQmlComponent;
class QQmlEngine;
class QQuickWindow;

namespace flowkeyd::app {

class LogModel;

class LogWindow : public QObject
{
    Q_OBJECT

public:
    /// `logPath` 是 `platform/win/logging` 正在写的那个文件（可能是空的）。
    LogWindow(QQmlEngine *engine, QString logPath, QObject *parent = nullptr);
    ~LogWindow() override;

    /// 弹出并前置窗口；已经开着时就只是前置它。
    void show();
    /// 开着就关掉，关着就打开。
    void toggle();
    void hide();
    bool isVisible() const;

private:
    void ensureWindow();

    QQmlEngine *m_engine = nullptr;
    QQmlComponent *m_component = nullptr;
    QPointer<QQuickWindow> m_window;
    LogModel *m_model = nullptr;
};

} // namespace flowkeyd::app
