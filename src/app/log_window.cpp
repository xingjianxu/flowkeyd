#include "app/log_window.h"

#include "app/log_model.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QVariant>

namespace flowkeyd::app {

LogWindow::LogWindow(QQmlEngine *engine, QString logPath, QObject *parent)
    : QObject(parent), m_engine(engine)
{
    m_model = new LogModel(this);
    m_model->setLogFile(logPath);
}

LogWindow::~LogWindow() = default;

void LogWindow::ensureWindow()
{
    if (m_window != nullptr) {
        return;
    }
    if (m_component == nullptr) {
        m_component = new QQmlComponent(m_engine, this);
        m_component->loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("LogWindow"));
        if (m_component->isError()) {
            return;
        }
    }
    QObject *object = m_component->create();
    m_window = qobject_cast<QQuickWindow *>(object);
    if (m_window == nullptr) {
        delete object;
        return;
    }
    // QML 侧声明了 `property var logModel`；把模型挂上去，ListView 直接吃它。
    m_window->setProperty("logModel", QVariant::fromValue(static_cast<QObject *>(m_model)));
}

void LogWindow::show()
{
    ensureWindow();
    if (m_window == nullptr) {
        return;
    }
    // 打开时立刻读一次，不然要等最多 250 ms 才有内容。
    m_model->refresh();
    m_window->setProperty("visible", true);
    m_window->raise();
    m_window->requestActivate();
}

void LogWindow::toggle()
{
    if (isVisible()) {
        hide();
    } else {
        show();
    }
}

void LogWindow::hide()
{
    if (m_window != nullptr) {
        m_window->setProperty("visible", false);
    }
}

bool LogWindow::isVisible() const
{
    return m_window != nullptr && m_window->isVisible();
}

} // namespace flowkeyd::app
