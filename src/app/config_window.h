// 图形化配置界面的宿主：在 Qt GUI 线程上创建/前置那个窗口，并把「保存成功」这件事
// 转成一次配置重载。
//
// 与 `LogWindow` 同一套骨架（`QQmlComponent::loadFromModule` + 复用窗口实例），
// 区别只有两点：
//   * 窗口是**普通窗口**（可缩放、进任务栏），不是无边框卡片 —— 配置项很多，需要
//     能最大化、能滚；
//   * 重开窗口时**不会**把用户没保存的改动丢掉：只有当编辑器里还没有配置、或者
//     打开了另一个配置文件时才真的重新读盘。
//
// 模型（`app::ConfigEditor`）不归它所有：`main` 建一个、交给它和界面共用。
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QQmlComponent;
class QQmlEngine;
class QQuickWindow;

namespace flowkeyd::app {

class ConfigEditor;

class ConfigWindow : public QObject
{
    Q_OBJECT

public:
    ConfigWindow(QQmlEngine *engine, ConfigEditor *editor, QObject *parent = nullptr);
    ~ConfigWindow() override;

    /// 打开并前置窗口。`path` 与编辑器当前那份不一致（或者还没载入过）时重新读盘。
    void open(const QString &path);
    void hide();
    bool isVisible() const;

    /// 配置文件的路径（托盘菜单「打开配置文件」用的是 `Runtime` 的那一份，
    /// 这里只是让调用方能确认两个地方指的是同一个文件）。
    const QString &path() const { return m_path; }

signals:
    /// 保存成功：守护进程应当重载配置。
    void reloadRequested();

private:
    void ensureWindow();

    QQmlEngine *m_engine = nullptr;
    ConfigEditor *m_editor = nullptr;
    QQmlComponent *m_component = nullptr;
    QPointer<QQuickWindow> m_window;
    QString m_path;
};

} // namespace flowkeyd::app
