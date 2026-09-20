#include "app/log_window.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

namespace flowkeyd::app {

LogWindow::LogWindow(QQmlEngine *engine, QObject *parent) : QObject(parent), m_engine(engine)
{
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
}

void LogWindow::show()
{
    ensureWindow();
    if (m_window == nullptr) {
        return;
    }
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
