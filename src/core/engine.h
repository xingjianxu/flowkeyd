// 快捷键状态机。
//
// 刻意不含 FFI：`Engine::onKey()` 接收一个已解码的按键事件，返回一个描述
// “接下来该做什么”的 `Reaction`。这样匹配规则无需桌面环境就能测试，
// 而且钩子与将来的 `--simulate` 走的是同一条代码路径。
#pragma once

#include "core/config.h"
#include "core/keys.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace flowkeyd::core {

/// 一个已解码的键盘事件。
struct KeyEvent
{
    Vk vk = 0;
    bool down = false;
    /// 事件是否由注入产生（LLKHF_INJECTED）。
    bool injected = false;

    static KeyEvent keyDown(Vk vkCode) { return KeyEvent{vkCode, true, false}; }
    static KeyEvent keyUp(Vk vkCode) { return KeyEvent{vkCode, false, false}; }
};

/// 快捷键的哪一半被触发。
enum class Phase { Press, Release };

/// 运行时必须处理的事情。
///
/// 重映射不会产生 trigger：它们的按键由钩子内联注入。
struct Trigger
{
    std::size_t index = 0;
    Phase phase = Phase::Press;

    friend bool operator==(const Trigger &, const Trigger &) = default;
};

/// 引擎对单个事件（或单次定时器滴答）的答复。
struct Reaction
{
    /// 吞掉该事件：不要传递给它下面的窗口。
    bool swallow = false;
    /// 必须执行的快捷键。
    std::vector<Trigger> triggers;
    /// 需要立即注入的原始按键操作（重映射与清理）。
    std::vector<SendOp> inject;

    bool isEmpty() const { return !swallow && triggers.empty() && inject.empty(); }

    friend bool operator==(const Reaction &, const Reaction &) = default;
};

/// 快捷键引擎。
class Engine
{
public:
    explicit Engine(std::shared_ptr<const Compiled> config);

    const std::shared_ptr<const Compiled> &config() const { return m_config; }
    bool isSuspended() const { return m_suspended; }

    /// 替换配置，返回为避免遗留合成按键按住不放而需要的注入操作。
    std::vector<SendOp> setConfig(std::shared_ptr<const Compiled> config);

    /// 挂起/恢复，返回释放重映射当前按住的按键所需的注入操作。
    std::vector<SendOp> setSuspended(bool suspended);

    /// 送入一个键盘事件。
    Reaction onKey(const KeyEvent &event, std::uint64_t nowMs);

    /// 定时器滴答：驱动长按重复。
    Reaction tick(std::uint64_t nowMs);

    /// 内部状态的调试渲染，供日志（以及将来的 `--simulate`）使用。
    QString stateSummary() const;

private:
    struct RepeatState
    {
        std::size_t binding = 0;
        Vk key = 0;
        std::uint64_t nextMs = 0;
        std::uint32_t intervalMs = 0;
    };

    /// 当前按住的修饰键，基于钩子看到的物理按键。
    Modifiers heldModifiers() const;

    /// 绑定的任一动作是否是 suspend 控制动作；它在挂起期间也必须继续可用。
    bool isSuspendControl(std::size_t index) const;

    static std::optional<std::uint32_t> chordMatches(const Chord &chord,
                                                     Vk key,
                                                     Modifiers held,
                                                     bool exactModifiers);

    std::optional<std::pair<std::size_t, Chord>> bestBinding(Vk key,
                                                             Modifiers held,
                                                             bool exactModifiers,
                                                             bool suspendedOnly) const;

    std::optional<std::pair<std::size_t, Chord>> bestRemap(Vk key,
                                                           Modifiers held,
                                                           bool exactModifiers) const;

    std::vector<SendOp> drainReleases();

    std::shared_ptr<const Compiled> m_config;
    /// 当前按住不放的物理按键（注入事件永远不会被记录）。
    std::vector<Vk> m_physical;
    /// 事件被快捷键或重映射抑制住的物理按键。
    std::vector<Vk> m_suppressed;
    /// 已为某个仍按住的按键触发的绑定，便于把 `on_release` 路由回去。
    std::vector<std::pair<Vk, std::size_t>> m_activeBindings;
    /// 已为某个仍按住的按键触发的重映射，便于 hold 模式释放它。
    std::vector<std::pair<Vk, std::size_t>> m_activeRemaps;
    std::vector<RepeatState> m_repeating;
    bool m_suspended = false;

    /// 某个被吞掉的和弦让 Windows 或 Alt 键在外壳眼中显得“赤裸”，
    /// 因此它的松开必须用未分配的标记按键伪装。
    ///
    /// 对应 AutoHotkey 的 `sDisguiseNextMenu`；之所以延到此刻而不是在
    /// 和弦键按下时就发送：外壳是在 Win 键*松开*时才做决定，
    /// 而和弦键与那次松开之间的 Windows 键**自动重复**会让一个未被修饰的
    /// Windows 键重新生效。参见 AGENTS.md 第 7 节第 9 条。
    bool m_maskMenuKeyUp = false;
};

} // namespace flowkeyd::core
