// 单元测试里构造 `core::Config` 的小工具。
//
// stage 1 还没有 Lua 层（那是 stage 2 的事），所以测试直接搭 C++ 结构体；
// 有了这些 helper，测试读起来仍然接近配置文件。
#pragma once

#include "core/action.h"
#include "core/config.h"
#include "core/keys.h"

#include <QtGlobal>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace flowkeyd::test {

inline core::ActionSpec specOne(const core::Action &action)
{
    core::ActionSpec spec;
    spec.kind = core::ActionSpec::Kind::One;
    spec.action = action;
    return spec;
}

inline core::ActionSpec specShort(const QString &text)
{
    core::ActionSpec spec;
    spec.kind = core::ActionSpec::Kind::Short;
    spec.shorthand = text;
    return spec;
}

inline core::ActionSpec specList(std::vector<core::ActionSpec> items)
{
    core::ActionSpec spec;
    spec.kind = core::ActionSpec::Kind::List;
    spec.list = std::move(items);
    return spec;
}

inline core::Action actionOf(core::Action::Kind kind)
{
    core::Action action;
    action.kind = kind;
    return action;
}

inline core::Action noneAction()
{
    return actionOf(core::Action::Kind::Noop);
}

inline core::Action reloadAction()
{
    return actionOf(core::Action::Kind::Reload);
}

inline core::Action quitAction()
{
    return actionOf(core::Action::Kind::Quit);
}

inline core::Action sendAction(const QString &keys)
{
    core::Action action;
    action.kind = core::Action::Kind::Send;
    action.keys = keys;
    return action;
}

inline core::Action runAction(const QString &program, const QStringList &args = {})
{
    core::Action action;
    action.kind = core::Action::Kind::Run;
    action.program = program;
    action.args = args;
    return action;
}

inline core::Action suspendAction(core::ToggleState state = core::ToggleState::Toggle)
{
    core::Action action;
    action.kind = core::Action::Kind::Suspend;
    action.toggleState = state;
    return action;
}

inline core::Action powerAction(core::PowerOp op)
{
    core::Action action;
    action.kind = core::Action::Kind::Power;
    action.powerOp = op;
    return action;
}

inline core::Action windowAction(core::WindowOp op)
{
    core::Action action;
    action.kind = core::Action::Kind::Window;
    action.windowOp = op;
    return action;
}

inline core::HotkeyDef hotkey(const QString &keys,
                              const std::optional<core::ActionSpec> &action = std::nullopt)
{
    core::HotkeyDef def;
    def.keys = QStringList{keys};
    def.action = action;
    return def;
}

inline core::HotkeyDef hotkeyNamed(const QString &name,
                                   const QString &keys,
                                   const std::optional<core::ActionSpec> &action = std::nullopt)
{
    core::HotkeyDef def = hotkey(keys, action);
    def.name = name;
    return def;
}

inline core::RemapDef remap(const QString &from,
                            const QString &to,
                            core::RemapMode mode = core::RemapMode::Hold)
{
    core::RemapDef def;
    def.from = from;
    def.to = to;
    def.mode = mode;
    return def;
}

inline core::MenuItemDef menuItem(const QString &label,
                                  const std::optional<QString> &key = std::nullopt)
{
    core::MenuItemDef item;
    item.label = label;
    item.key = key;
    return item;
}

/// 校验并返回编译结果；出错时直接终止测试进程（配置写错了就是测试的 bug）。
inline std::shared_ptr<const core::Compiled> compileOrDie(const core::Config &config)
{
    core::Compiled out;
    const auto error = core::compile(config, QStringLiteral("test.lua"), {}, {}, &out);
    if (error.has_value()) {
        qFatal("unexpected config error: %s", qPrintable(error->toString()));
    }
    return std::make_shared<const core::Compiled>(std::move(out));
}

inline std::optional<core::ConfigError> compileConfig(const core::Config &config)
{
    core::Compiled out;
    return core::compile(config, QStringLiteral("test.lua"), {}, {}, &out);
}

} // namespace flowkeyd::test
