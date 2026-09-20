// 动作工作线程：执行已经校验过的声明式动作。
//
// `run`/`open`/`wait` 都会阻塞几十毫秒到几秒，绝不能在钩子线程上做。
// 钩子线程把「哪个快捷键的哪一半触发了」排到这里，这里再决定做什么。
#pragma once

#include "core/config.h"
#include "core/engine.h"

#include <QObject>

#include <memory>

namespace flowkeyd::app {

class Runtime;

/// 跑在 `QThread` 上的执行器。
///
/// 自己不是线程：对象被 `moveToThread()` 到工作线程，`submit()` 从钩子线程
/// 用 `Qt::QueuedConnection` 投递（这是 AGENTS.md 第 6 节选定并写定的方案）。
class Dispatcher : public QObject
{
    Q_OBJECT

public:
    explicit Dispatcher(Runtime *runtime, QObject *parent = nullptr);

    /// 线程安全：把一个已触发的快捷键排入工作线程队列。
    void submit(std::shared_ptr<const core::Compiled> config, core::Trigger trigger);

signals:
    /// 挂起状态改变（托盘提示用）。
    void suspendedChanged(bool suspended);

private:
    void execute(const std::shared_ptr<const core::Compiled> &config, const core::Trigger &trigger);
    void executeAction(const core::Compiled &config, const QString &hotkey, const core::Action &action);

    Runtime *m_runtime = nullptr;
};

} // namespace flowkeyd::app
