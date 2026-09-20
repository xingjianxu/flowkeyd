// 日志窗口（进程内的 QML 窗口，FluentWinUI3）。
//
// stage 4 会往里面接上尾随日志文件的模型；stage 0/1 只有一个空窗口，
// 用来验证「窗口能弹出、关掉不退出进程」（见 AGENTS.md 第 7 节第 20 条）。
#pragma once

#include <QObject>
#include <QPointer>

class QQmlComponent;
class QQmlEngine;
class QQuickWindow;

namespace flowkeyd::app {

class LogWindow : public QObject
{
    Q_OBJECT

public:
    explicit LogWindow(QQmlEngine *engine, QObject *parent = nullptr);
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
};

} // namespace flowkeyd::app
