#include "core/engine.h"

#include <algorithm>

namespace flowkeyd::core {

namespace {

/// 阻止外壳弹出开始菜单的标记按键。
///
/// 被吞掉的 `Win+…` 快捷键会遮住外壳本应在 Windows 键按下与松开之间看到的
/// 唯一按键，于是 Windows 认为 Windows 键是“单独按下”的，在松开时弹出开始菜单
/// （对 `Win+S` 就是搜索框）。注入一个未分配的按键正好补上这个缺口；
/// 该注入携带 flowkeyd 自己的标记，因此我们自己会忽略它，也不会有程序对它
/// 做出反应。AutoHotkey 用 `#MenuMaskKey` 做同样的事。
///
/// **注入时机**才是关键（见 `Engine::m_maskMenuKeyUp`）：它必须是外壳在
/// Windows 键松开前看到的*最后*一件事，而不是和弦键按下后的第一件事。
std::vector<SendOp> winKeyDummy()
{
    return {SendOp::keyDown(vk::UNASSIGNED), SendOp::keyUp(vk::UNASSIGNED)};
}

bool containsValue(const std::vector<Vk> &values, Vk vk)
{
    return std::find(values.begin(), values.end(), vk) != values.end();
}

} // namespace

Engine::Engine(std::shared_ptr<const Compiled> config) : m_config(std::move(config))
{
}

std::vector<SendOp> Engine::drainReleases()
{
    std::vector<SendOp> ops;
    for (const auto &[vk, index] : m_activeRemaps) {
        Q_UNUSED(vk);
        if (index < m_config->remaps.size()) {
            const CompiledRemap &remap = m_config->remaps.at(index);
            ops.insert(ops.end(), remap.release.begin(), remap.release.end());
        }
    }
    m_activeRemaps.clear();
    return ops;
}

std::vector<SendOp> Engine::setConfig(std::shared_ptr<const Compiled> config)
{
    std::vector<SendOp> cleanup = drainReleases();
    m_config = std::move(config);
    m_physical.clear();
    m_suppressed.clear();
    m_activeBindings.clear();
    m_activeRemaps.clear();
    m_repeating.clear();
    m_maskMenuKeyUp = false;
    return cleanup;
}

std::vector<SendOp> Engine::setSuspended(bool suspended)
{
    m_suspended = suspended;
    m_repeating.clear();
    if (suspended) {
        return drainReleases();
    }
    return {};
}

Modifiers Engine::heldModifiers() const
{
    Modifiers mods;
    for (const Vk vk : m_physical) {
        if (const auto m = Modifiers::fromVk(vk); m.has_value()) {
            mods = mods.unioned(*m);
        }
    }
    return mods;
}

bool Engine::isSuspendControl(std::size_t index) const
{
    if (index >= m_config->bindings.size()) {
        return false;
    }
    const Binding &binding = m_config->bindings.at(index);
    const auto isSuspend = [](const Action &action) { return action.kind == Action::Kind::Suspend; };
    return std::any_of(binding.press.begin(), binding.press.end(), isSuspend)
        || std::any_of(binding.release.begin(), binding.release.end(), isSuspend);
}

std::optional<std::uint32_t> Engine::chordMatches(const Chord &chord,
                                                  Vk key,
                                                  Modifiers held,
                                                  bool exactModifiers)
{
    if (!sameKey(chord.key, key)) {
        return std::nullopt;
    }
    // 和弦自身的按键若本身就是修饰键，不能算作“额外按住”的修饰键，
    // 否则 `keys = "Ctrl+Shift"` 在 `exact_modifiers = true` 下永远不会触发。
    Modifiers effective = held;
    if (const auto own = Modifiers::fromVk(chord.key); own.has_value()) {
        effective = effective.without(*own);
    }
    if (!effective.contains(chord.mods)) {
        return std::nullopt;
    }
    const bool exact = effective.bits() == chord.mods.bits();
    if (exactModifiers && !exact && !chord.wildcard) {
        return std::nullopt;
    }
    // 排序：修饰键越多越优先；其次是修饰键集合完全一致；
    // 最后是非通配和弦优先于通配和弦。
    return static_cast<std::uint32_t>(chord.mods.count() * 4 + (exact ? 2 : 0)
                                      + (chord.wildcard ? 0 : 1));
}

std::optional<std::pair<std::size_t, Chord>> Engine::bestBinding(Vk key,
                                                                Modifiers held,
                                                                bool exactModifiers,
                                                                bool suspendedOnly) const
{
    std::optional<std::tuple<std::uint32_t, std::size_t, Chord>> best;
    for (std::size_t index = 0; index < m_config->bindings.size(); ++index) {
        if (suspendedOnly && !isSuspendControl(index)) {
            continue;
        }
        for (const Chord &chord : m_config->bindings.at(index).chords) {
            if (const auto score = chordMatches(chord, key, held, exactModifiers); score.has_value()) {
                // 严格更优者胜出；同分时保留配置中靠前的条目。
                if (!best.has_value() || *score > std::get<0>(*best)) {
                    best = std::make_tuple(*score, index, chord);
                }
            }
        }
    }
    if (!best.has_value()) {
        return std::nullopt;
    }
    return std::make_pair(std::get<1>(*best), std::get<2>(*best));
}

std::optional<std::pair<std::size_t, Chord>> Engine::bestRemap(Vk key,
                                                              Modifiers held,
                                                              bool exactModifiers) const
{
    std::optional<std::tuple<std::uint32_t, std::size_t, Chord>> best;
    for (std::size_t index = 0; index < m_config->remaps.size(); ++index) {
        const Chord &from = m_config->remaps.at(index).from;
        if (const auto score = chordMatches(from, key, held, exactModifiers); score.has_value()) {
            if (!best.has_value() || *score > std::get<0>(*best)) {
                best = std::make_tuple(*score, index, from);
            }
        }
    }
    if (!best.has_value()) {
        return std::nullopt;
    }
    return std::make_pair(std::get<1>(*best), std::get<2>(*best));
}

Reaction Engine::onKey(const KeyEvent &event, std::uint64_t nowMs)
{
    Reaction reaction;
    if (event.injected) {
        // 永远不要对注入输入作出反应：它不是我们自己的就是别的工具的，
        // 回应它会造成循环。
        return reaction;
    }

    if (event.down) {
        // 已经见过的按键的自动重复：不再重新匹配，
        // 但快捷键当初吞掉了按下事件的话，继续抑制 OS 的重复。
        if (containsValue(m_physical, event.vk)) {
            if (containsValue(m_suppressed, event.vk)) {
                reaction.swallow = true;
            }
            return reaction;
        }
        m_physical.push_back(event.vk);

        const Modifiers held = heldModifiers();
        const bool exact = m_config->settings.exactModifiers;

        if (const auto best = bestBinding(event.vk, held, exact, m_suspended); best.has_value()) {
            const std::size_t index = best->first;
            const Chord chord = best->second;
            const Binding &binding = m_config->bindings.at(index);
            const bool passthrough = chord.passthrough;
            const bool swallow = binding.swallow && !passthrough && !m_suspended;
            if (swallow) {
                reaction.swallow = true;
                m_suppressed.push_back(event.vk);
                // 外壳永远看不到这个按键，因此到它松开时 Win/Alt 会显得
                // 像是被单独按下。
                if (held.contains(Modifiers::Win) || held.contains(Modifiers::Alt)) {
                    m_maskMenuKeyUp = true;
                }
            }
            if (!binding.press.empty()) {
                // 按下就派发，不等到修饰键松开：外壳看到的东西由
                // `m_maskMenuKeyUp` 在 keyup 时补上，动作本身不再拖后。
                reaction.triggers.push_back(Trigger{index, Phase::Press});
            }
            m_activeBindings.emplace_back(event.vk, index);
            if (binding.repeat.has_value()) {
                m_repeating.push_back(RepeatState{index,
                                                  event.vk,
                                                  nowMs + binding.repeat->delayMs,
                                                  binding.repeat->intervalMs});
            }
            return reaction;
        }

        // 重映射仅在未挂起时生效。
        if (!m_suspended) {
            if (const auto best = bestRemap(event.vk, held, exact); best.has_value()) {
                const std::size_t index = best->first;
                const Chord chord = best->second;
                const CompiledRemap &remap = m_config->remaps.at(index);
                if (remap.swallow && !chord.passthrough) {
                    reaction.swallow = true;
                    m_suppressed.push_back(event.vk);
                }
                reaction.inject.insert(reaction.inject.end(), remap.press.begin(), remap.press.end());
                m_activeRemaps.emplace_back(event.vk, index);
                return reaction;
            }
        }

        return reaction;
    }

    // 按键松开 -----------------------------------------------------------------
    if (const auto pos = std::find(m_physical.begin(), m_physical.end(), event.vk);
        pos != m_physical.end()) {
        m_physical.erase(pos);
    }
    if (const auto pos = std::find(m_suppressed.begin(), m_suppressed.end(), event.vk);
        pos != m_suppressed.end()) {
        m_suppressed.erase(pos);
        reaction.swallow = true;
    }
    // 如果某个被吞掉的和弦让 Win/Alt 键看起来像被单独按下，就在这里伪装它的
    // 松开。此时 `m_physical` 已不再包含该键，因此 `heldModifiers()` 回答的是
    // “是否还有别的菜单键按着”。两者在同一批 `SendInput` 中按此顺序发出，
    // 这才能让标记成为外壳在 keyup 之前看到的最后一件事。
    if (m_maskMenuKeyUp) {
        if (const auto menuKey = Modifiers::fromVk(event.vk); menuKey.has_value()) {
            if (*menuKey == Modifiers::Win || *menuKey == Modifiers::Alt) {
                const Modifiers stillHeld = heldModifiers();
                if (!stillHeld.contains(Modifiers::Win) && !stillHeld.contains(Modifiers::Alt)) {
                    const std::vector<SendOp> dummy = winKeyDummy();
                    reaction.inject.insert(reaction.inject.end(), dummy.begin(), dummy.end());
                    m_maskMenuKeyUp = false;
                }
            }
        }
    }
    const auto remapPos = std::find_if(m_activeRemaps.begin(),
                                       m_activeRemaps.end(),
                                       [&event](const auto &entry) { return entry.first == event.vk; });
    if (remapPos != m_activeRemaps.end()) {
        const std::size_t index = remapPos->second;
        m_activeRemaps.erase(remapPos);
        if (index < m_config->remaps.size()) {
            const CompiledRemap &remap = m_config->remaps.at(index);
            reaction.inject.insert(reaction.inject.end(), remap.release.begin(), remap.release.end());
        }
    }

    std::vector<std::size_t> fired;
    for (const auto &[vk, index] : m_activeBindings) {
        if (vk == event.vk) {
            fired.push_back(index);
        }
    }
    m_activeBindings.erase(std::remove_if(m_activeBindings.begin(),
                                          m_activeBindings.end(),
                                          [&event](const auto &entry) { return entry.first == event.vk; }),
                           m_activeBindings.end());
    m_repeating.erase(std::remove_if(m_repeating.begin(),
                                     m_repeating.end(),
                                     [&event](const RepeatState &state) { return state.key == event.vk; }),
                      m_repeating.end());

    for (const std::size_t index : fired) {
        if (index < m_config->bindings.size() && !m_config->bindings.at(index).release.empty()) {
            reaction.triggers.push_back(Trigger{index, Phase::Release});
        }
    }
    return reaction;
}

Reaction Engine::tick(std::uint64_t nowMs)
{
    Reaction reaction;
    if (m_suspended) {
        return reaction;
    }
    for (RepeatState &state : m_repeating) {
        if (nowMs >= state.nextMs) {
            reaction.triggers.push_back(Trigger{state.binding, Phase::Press});
            state.nextMs = nowMs + state.intervalMs;
        }
    }
    return reaction;
}

QString Engine::stateSummary() const
{
    QStringList held;
    for (const Vk vk : m_physical) {
        held.append(nameFromKey(vk));
    }
    return QStringLiteral("suspended=%1 held=[%2] suppressed=%3 active_bindings=%4 active_remaps=%5 "
                          "repeating=%6")
        .arg(m_suspended ? QStringLiteral("true") : QStringLiteral("false"),
             held.join(QLatin1Char(',')),
             QString::number(m_suppressed.size()),
             QString::number(m_activeBindings.size()),
             QString::number(m_activeRemaps.size()),
             QString::number(m_repeating.size()));
}

} // namespace flowkeyd::core
