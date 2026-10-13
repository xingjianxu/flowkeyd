#include "app/config_window.h"

#include "app/config_editor.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QVariant>

namespace flowkeyd::app {

ConfigWindow::ConfigWindow(QQmlEngine *engine, ConfigEditor *editor, QObject *parent)
    : QObject(parent), m_engine(engine), m_editor(editor)
{
    if (m_editor != nullptr) {
        // 保存成功之后要求守护进程重载配置：`Runtime::reloadFromAnyThread()` 接它。
        // 配置界面**自己不去碰钩子线程**，也让「保存」与「重载」这两件事各自只有
        // 一个入口。
        connect(m_editor, &ConfigEditor::saved, this, [this](const QString &) {
            emit reloadRequested();
        });
    }
}

ConfigWindow::~ConfigWindow() = default;

void ConfigWindow::ensureWindow()
{
    if (m_window != nullptr) {
        return;
    }
    if (m_component == nullptr) {
        m_component = new QQmlComponent(m_engine, this);
        m_component->loadFromModule(QStringLiteral("Flowkeyd"), QStringLiteral("ConfigWindow"));
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
    // QML 侧声明了 `property var editor`；把模型挂上去。
    m_window->setProperty("editor", QVariant::fromValue(static_cast<QObject *>(m_editor)));
}

void ConfigWindow::open(const QString &path)
{
    ensureWindow();
    if (m_window == nullptr) {
        return;
    }
    if (m_editor != nullptr) {
        // 已经有配置、而且就是同一个文件时不重新读盘：用户可能正拿着没保存的改动。
        if (!m_editor->loaded() || m_path != path) {
            m_editor->load(path);
        }
        m_path = path;
    }
    m_window->setProperty("visible", true);
    m_window->raise();
    m_window->requestActivate();
}

void ConfigWindow::hide()
{
    if (m_window != nullptr) {
        m_window->setProperty("visible", false);
    }
}

bool ConfigWindow::isVisible() const
{
    return m_window != nullptr && m_window->isVisible();
}

} // namespace flowkeyd::app
