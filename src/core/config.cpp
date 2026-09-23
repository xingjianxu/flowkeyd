#include "core/config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace flowkeyd::core {

// ---------------------------------------------------------------------------
// ConfigError
// ---------------------------------------------------------------------------

ConfigError ConfigError::makeIo(const QString &path, const QString &message)
{
    ConfigError error;
    error.kind = Kind::Io;
    error.path = path;
    error.message = message;
    return error;
}

ConfigError ConfigError::makeParse(const QString &path, const QString &message)
{
    ConfigError error;
    error.kind = Kind::Parse;
    error.path = path;
    error.message = message;
    return error;
}

ConfigError ConfigError::makeLegacyToml(const QString &path, const QString &preferred)
{
    ConfigError error;
    error.kind = Kind::LegacyToml;
    error.path = path;
    error.preferred = preferred;
    return error;
}

ConfigError ConfigError::makeValidation(const QStringList &errors)
{
    ConfigError error;
    error.kind = Kind::Validation;
    error.errors = errors;
    return error;
}

QString ConfigError::toString() const
{
    switch (kind) {
    case Kind::Io:
        return QStringLiteral("cannot read %1: %2").arg(path, message);
    case Kind::Parse:
        return QStringLiteral("invalid Lua in %1:\n%2").arg(path, message);
    case Kind::LegacyToml:
        return QStringLiteral(
                   "%1 is an old TOML config; flowkeyd reads Lua configs now — port it to %2 "
                   "(see the “从 TOML 迁移到 Lua” section of README.md) or point --config at "
                   "your .lua file")
            .arg(path, preferred);
    case Kind::Validation: {
        QString out = QStringLiteral("configuration problems:\n");
        for (const QString &error : errors) {
            out += QStringLiteral("  - %1\n").arg(error);
        }
        return out;
    }
    }
    return QString();
}

QString ConfigError::oneLine() const
{
    if (kind == Kind::Validation) {
        return errors.join(QLatin1String("; "));
    }
    QString text = toString();
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text;
}

// ---------------------------------------------------------------------------
// 配置里的枚举
// ---------------------------------------------------------------------------

QString triggerModeName(TriggerMode mode)
{
    switch (mode) {
    case TriggerMode::Press:
        return QStringLiteral("press");
    case TriggerMode::Release:
        return QStringLiteral("release");
    case TriggerMode::Repeat:
        return QStringLiteral("repeat");
    }
    return QStringLiteral("press");
}

std::optional<TriggerMode> triggerModeFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("press")) {
        return TriggerMode::Press;
    }
    if (key == QLatin1String("release")) {
        return TriggerMode::Release;
    }
    if (key == QLatin1String("repeat")) {
        return TriggerMode::Repeat;
    }
    return std::nullopt;
}

RepeatSpec RepeatSpec::makeFlag(bool value)
{
    RepeatSpec spec;
    spec.kind = Kind::Flag;
    spec.flag = value;
    return spec;
}

RepeatSpec RepeatSpec::makeConfig(std::optional<std::uint32_t> intervalMs,
                                  std::optional<std::uint32_t> delayMs)
{
    RepeatSpec spec;
    spec.kind = Kind::Config;
    spec.intervalMs = intervalMs;
    spec.delayMs = delayMs;
    return spec;
}

// ---------------------------------------------------------------------------
// window_rule
// ---------------------------------------------------------------------------

QString MonitorRef::describe() const
{
    switch (kind) {
    case Kind::Index:
        return QStringLiteral("monitor %1").arg(index);
    case Kind::Primary:
        return QStringLiteral("primary monitor");
    case Kind::Device:
        return QStringLiteral("monitor %1").arg(rustDebug(device));
    }
    return QStringLiteral("monitor");
}

QString WindowRule::summary() const
{
    QStringList parts;
    if (process.has_value()) {
        parts.append(QStringLiteral("process %1").arg(rustDebug(*process)));
    }
    if (title.has_value()) {
        parts.append(QStringLiteral("title %1").arg(rustDebug(*title)));
    }
    if (desktop.has_value()) {
        parts.append(QStringLiteral("desktop %1").arg(*desktop));
    }
    if (allDesktops.has_value()) {
        parts.append(*allDesktops ? QStringLiteral("all desktops")
                                  : QStringLiteral("single desktop"));
    }
    if (topmost.has_value()) {
        parts.append(*topmost ? QStringLiteral("topmost") : QStringLiteral("not topmost"));
    }
    if (monitor.has_value()) {
        parts.append(monitor->describe());
    }
    if (applyGeometry) {
        if (maximize) {
            parts.append(QStringLiteral("maximize"));
        } else {
            if (width.has_value() || height.has_value()) {
                parts.append(QStringLiteral("size %1x%2")
                                 .arg(width.has_value() ? QString::number(*width)
                                                       : QStringLiteral("?"),
                                      height.has_value() ? QString::number(*height)
                                                         : QStringLiteral("?")));
            }
            if (x.has_value() || y.has_value()) {
                parts.append(QStringLiteral("at %1,%2")
                                 .arg(x.has_value() ? QString::number(*x) : QStringLiteral("?"),
                                      y.has_value() ? QString::number(*y) : QStringLiteral("?")));
            }
        }
    }
    if (parts.isEmpty()) {
        return QStringLiteral("(no-op)");
    }
    return parts.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------------------
// ActionSpec
// ---------------------------------------------------------------------------

std::optional<QString> ActionSpec::flatten(std::vector<Action> *out) const
{
    switch (kind) {
    case Kind::One:
        out->push_back(action);
        return std::nullopt;
    case Kind::Short: {
        Action parsed;
        if (const auto error = parseActionShorthand(shorthand, &parsed); error.has_value()) {
            return error;
        }
        out->push_back(parsed);
        return std::nullopt;
    }
    case Kind::List: {
        // 空列表在 Lua 里就是 `action = {}`。它什么都不做，但看起来像是
        // 「配了点什么」，所以宁可报错——`none()` 才是明说的写法。
        if (list.empty()) {
            return QStringLiteral("an empty action list does nothing; use `none()` or drop the field");
        }
        for (const ActionSpec &item : list) {
            if (const auto error = item.flatten(out); error.has_value()) {
                return error;
            }
        }
        return std::nullopt;
    }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// 编译
// ---------------------------------------------------------------------------

namespace {

/// 报告被重复声明的和弦时用的比较键。
struct SeenChord
{
    Chord chord;
    QString owner;
};

} // namespace

std::optional<ConfigError> compile(const Config &config,
                                   const QString &path,
                                   QStringList errors,
                                   QStringList warnings,
                                   Compiled *out)
{
    std::vector<Binding> bindings;
    std::vector<CompiledRemap> remaps;
    std::vector<WindowRule> windowRules;

    if (config.settings.tickMs == 0 || config.settings.tickMs > 1000) {
        errors.append(QStringLiteral("settings.tick_ms must be between 1 and 1000 (got %1)")
                          .arg(config.settings.tickMs));
    }
    if (config.settings.repeatIntervalMs == 0) {
        errors.append(QStringLiteral("settings.repeat_interval_ms must be greater than 0"));
    }
    const QString logLevel = config.settings.logLevel.toLower();
    if (logLevel != QLatin1String("trace") && logLevel != QLatin1String("debug")
        && logLevel != QLatin1String("info") && logLevel != QLatin1String("warn")
        && logLevel != QLatin1String("error") && logLevel != QLatin1String("off")) {
        errors.append(QStringLiteral("settings.log_level `%1` is not one of "
                                     "trace|debug|info|warn|error|off")
                          .arg(config.settings.logLevel));
    }
    const QString backend = config.settings.inputBackend.toLower();
    if (backend != QLatin1String("auto") && backend != QLatin1String("user32")
        && backend != QLatin1String("ntuser") && backend != QLatin1String("win32u")
        && backend != QLatin1String("documented") && backend != QLatin1String("undocumented")) {
        errors.append(QStringLiteral("settings.input_backend `%1` is not one of auto|user32|ntuser")
                          .arg(config.settings.inputBackend));
    }

    for (std::size_t i = 0; i < config.hotkeys.size(); ++i) {
        const HotkeyDef &def = config.hotkeys.at(i);
        const QString label = def.name.has_value()
                                  ? *def.name
                                  : QStringLiteral("hotkey #%1").arg(i + 1);

        std::vector<Chord> chords;
        for (const QString &raw : def.keys) {
            Chord chord;
            if (const auto error = parseChord(raw, &chord); error.has_value()) {
                errors.append(QStringLiteral("%1: keys = %2: %3").arg(label, rustDebug(raw), error->message()));
                continue;
            }
            const bool duplicate = std::any_of(chords.begin(), chords.end(), [&chord](const Chord &existing) {
                return existing == chord;
            });
            if (duplicate) {
                warnings.append(QStringLiteral("%1: duplicate chord `%2` ignored").arg(label, raw));
            } else {
                chords.push_back(chord);
            }
        }
        if (chords.empty()) {
            errors.append(QStringLiteral("%1: no usable keys").arg(label));
            continue;
        }

        std::vector<Action> press;
        std::vector<Action> release;
        if (def.action.has_value()) {
            if (const auto error = def.action->flatten(&press); error.has_value()) {
                errors.append(QStringLiteral("%1: action: %2").arg(label, *error));
            }
        }
        if (def.onRelease.has_value()) {
            if (const auto error = def.onRelease->flatten(&release); error.has_value()) {
                errors.append(QStringLiteral("%1: on_release: %2").arg(label, *error));
            }
        }

        // 在加载时就把发送脚本、参数与选单结构的问题抓出来，
        // 而不是等到凌晨三点那个快捷键终于被按下的时候。
        for (const Action &action : press) {
            validateAction(label, action, &errors);
        }
        for (const Action &action : release) {
            validateAction(label, action, &errors);
        }

        std::optional<Repeat> repeat;
        if (def.repeat.has_value()) {
            const RepeatSpec &spec = *def.repeat;
            if (spec.kind == RepeatSpec::Kind::Flag) {
                if (spec.flag) {
                    repeat = Repeat{config.settings.repeatIntervalMs, config.settings.repeatDelayMs};
                }
            } else {
                const Repeat candidate{spec.intervalMs.value_or(config.settings.repeatIntervalMs),
                                       spec.delayMs.value_or(config.settings.repeatDelayMs)};
                if (candidate.intervalMs == 0) {
                    errors.append(QStringLiteral("%1: repeat.interval_ms must be greater than 0").arg(label));
                } else {
                    repeat = candidate;
                }
            }
        }

        // `trigger` 和 `repeat` 是同一件事的两种写法：`repeatable = true` 就是
        // `trigger = "repeat"`，而只写 `trigger = "repeat"` 则用 `settings` 里的
        // 默认参数。两者不一致时不能猜，报错。
        TriggerMode trigger = def.trigger.value_or(TriggerMode::Press);
        if (repeat.has_value()) {
            if (def.trigger.has_value() && *def.trigger != TriggerMode::Repeat) {
                errors.append(QStringLiteral("%1: trigger = \"%2\" cannot be combined with `repeat`")
                                  .arg(label, triggerModeName(trigger)));
            }
            trigger = TriggerMode::Repeat;
        } else if (trigger == TriggerMode::Repeat) {
            repeat = Repeat{config.settings.repeatIntervalMs, config.settings.repeatDelayMs};
        }
        if (repeat.has_value() && press.empty()) {
            warnings.append(QStringLiteral("%1: repeat is set but there is no press action").arg(label));
        }

        // `trigger = "release"` 只是把按下那半的动作挪到松开时执行。
        // 这样引擎就仍然只有“按下派发”和“松开派发”两条路，
        // `--list` 与干跑也不必知道第三种时机的存在。
        if (trigger == TriggerMode::Release && !press.empty()) {
            if (!release.empty()) {
                warnings.append(QStringLiteral(
                                    "%1: trigger = \"release\" already runs the press actions on key up; "
                                    "`on_release` adds more actions at the same moment")
                                    .arg(label));
            }
            release.insert(release.begin(), press.begin(), press.end());
            press.clear();
        }

        const QString name = def.name.has_value() ? *def.name : chords.at(0).render();
        const bool swallow = def.swallow.value_or(config.settings.swallow);

        if (!def.enabled) {
            warnings.append(QStringLiteral("%1: disabled").arg(label));
            continue;
        }
        if (press.empty() && release.empty() && !swallow) {
            warnings.append(QStringLiteral("%1: no action and no swallow, this hotkey does nothing")
                                .arg(label));
        }

        Binding binding;
        binding.name = name;
        binding.chords = chords;
        binding.swallow = swallow;
        binding.trigger = trigger;
        binding.press = std::move(press);
        binding.release = std::move(release);
        binding.repeat = repeat;
        binding.comment = def.comment;
        bindings.push_back(std::move(binding));
    }

    for (std::size_t i = 0; i < config.remaps.size(); ++i) {
        const RemapDef &def = config.remaps.at(i);
        const QString label = def.name.has_value()
                                  ? *def.name
                                  : QStringLiteral("remap #%1").arg(i + 1);
        if (!def.enabled) {
            warnings.append(QStringLiteral("%1: disabled").arg(label));
            continue;
        }
        Chord from;
        if (const auto error = parseChord(def.from, &from); error.has_value()) {
            errors.append(QStringLiteral("%1: from = %2: %3").arg(label, rustDebug(def.from), error->message()));
            continue;
        }
        if (!from.mods.isEmpty()) {
            warnings.append(QStringLiteral(
                                "%1: `from` has modifiers; consider a `hotkey` with a send action instead")
                                .arg(label));
        }
        // `to = "Esc"` 表示 Escape 键，完全等同于 AutoHotkey 的
        // `CapsLock::Esc`；而 `to = "^{c}"` 是一个发送脚本。
        QVector<SendOp> ops;
        if (const auto error = parseKeyOrScript(def.to, &ops); error.has_value()) {
            errors.append(QStringLiteral("%1: to = %2: %3").arg(label, rustDebug(def.to), error->message()));
            continue;
        }

        CompiledRemap remap;
        if (def.mode == RemapMode::Hold) {
            QVector<SendOp> pressOps;
            QVector<SendOp> releaseOps;
            splitHold(ops, &pressOps, &releaseOps);
            remap.press.assign(pressOps.begin(), pressOps.end());
            remap.release.assign(releaseOps.begin(), releaseOps.end());
        } else {
            remap.press.assign(ops.begin(), ops.end());
        }

        const bool hasSleep = std::any_of(remap.press.begin(), remap.press.end(), [](const SendOp &op) {
                                  return op.kind == SendOp::Kind::Sleep;
                              })
            || std::any_of(remap.release.begin(), remap.release.end(), [](const SendOp &op) {
                   return op.kind == SendOp::Kind::Sleep;
               });
        if (hasSleep) {
            warnings.append(QStringLiteral(
                                "%1: `{Sleep}` in a remap is ignored — the hook callback must return immediately")
                                .arg(label));
        }

        remap.name = def.name.has_value() ? *def.name
                                          : QStringLiteral("%1 -> %2").arg(from.render(), def.to);
        remap.from = from;
        remap.swallow = def.swallow.value_or(config.settings.swallow);
        remaps.push_back(std::move(remap));
    }

    for (std::size_t i = 0; i < config.windowRules.size(); ++i) {
        const WindowRuleDef &def = config.windowRules.at(i);
        const QString label = def.name.has_value()
                                  ? QStringLiteral("window rule #%1 (`%2`)").arg(i + 1).arg(*def.name)
                                  : QStringLiteral("window rule #%1").arg(i + 1);
        if (!def.enabled) {
            warnings.append(QStringLiteral("%1: disabled").arg(label));
            continue;
        }
        const bool hasTitle = def.title.has_value() && !def.title->trimmed().isEmpty();
        const bool hasProcess = def.process.has_value() && !def.process->trimmed().isEmpty();
        if (!hasTitle && !hasProcess) {
            errors.append(QStringLiteral("%1: needs `process` or `title` to know which windows it "
                                         "applies to")
                              .arg(label));
            continue;
        }
        const bool hasGeometry =
            def.x.has_value() || def.y.has_value() || def.width.has_value() || def.height.has_value();
        if (def.maximize.value_or(false) && hasGeometry) {
            errors.append(QStringLiteral("%1: `maximize = true` cannot be combined with "
                                         "`x`/`y`/`width`/`height`; drop one of them")
                              .arg(label));
            continue;
        }
        if (def.desktop.has_value() && *def.desktop == 0) {
            errors.append(QStringLiteral(
                              "%1: `desktop` must be 1 or greater (desktops are numbered from 1)")
                              .arg(label));
            continue;
        }
        if (def.allDesktops.value_or(false) && def.desktop.has_value()) {
            errors.append(QStringLiteral("%1: `all_desktops = true` cannot be combined with `desktop`; "
                                         "a window on every desktop has no single desktop to move "
                                         "to")
                              .arg(label));
            continue;
        }
        if (def.monitor.has_value() && def.monitor->kind == MonitorRef::Kind::Index
            && def.monitor->index == 0) {
            errors.append(QStringLiteral("%1: `monitor` must be 1 or greater (monitors are numbered "
                                         "from 1, left to right)")
                              .arg(label));
            continue;
        }
        if (def.monitor.has_value() && def.monitor->kind == MonitorRef::Kind::Device
            && def.monitor->device.trimmed().isEmpty()) {
            errors.append(QStringLiteral("%1: `monitor` device name must not be empty").arg(label));
            continue;
        }
        if ((def.width.has_value() && *def.width == 0)
            || (def.height.has_value() && *def.height == 0)) {
            errors.append(QStringLiteral("%1: `width`/`height` must be greater than 0").arg(label));
            continue;
        }

        WindowRule rule;
        rule.name = def.name.value_or(QStringLiteral("window rule #%1").arg(i + 1));
        rule.title = hasTitle ? def.title : std::nullopt;
        rule.process = hasProcess ? def.process : std::nullopt;
        rule.desktop = def.desktop;
        rule.monitor = def.monitor;
        rule.allDesktops = def.allDesktops;
        rule.topmost = def.topmost;
        rule.maximize = def.maximize.value_or(def.monitor.has_value() && !hasGeometry);
        rule.applyGeometry = def.monitor.has_value() || hasGeometry || rule.maximize;
        rule.x = def.x;
        rule.y = def.y;
        rule.width = def.width;
        rule.height = def.height;
        if (!rule.desktop.has_value() && !rule.applyGeometry && !rule.allDesktops.has_value()
            && !rule.topmost.has_value()) {
            warnings.append(QStringLiteral("%1: only matches windows and does nothing").arg(label));
            continue;
        }
        windowRules.push_back(std::move(rule));
    }

    // 两条规则匹配同一批窗口时先写的赢；这种重复几乎总是笔误。
    for (std::size_t i = 0; i < windowRules.size(); ++i) {
        for (std::size_t j = i + 1; j < windowRules.size(); ++j) {
            if (windowRules[i].title == windowRules[j].title
                && windowRules[i].process == windowRules[j].process) {
                warnings.append(QStringLiteral("`%1` and `%2` match the same windows; the first "
                                               "match wins")
                                    .arg(windowRules[i].name, windowRules[j].name));
            }
        }
    }

    // 报告被重复声明的和弦：引擎会静默地选第一个。
    // 这里用 `sameKey`，因为通用修饰键 VK（`Shift`）与具体 VK（`LShift`）
    // 实际上会匹配同一次按键。
    std::vector<SeenChord> seen;
    for (const Binding &binding : bindings) {
        for (const Chord &chord : binding.chords) {
            const auto it = std::find_if(seen.begin(), seen.end(), [&chord](const SeenChord &entry) {
                return sameKey(entry.chord.key, chord.key) && entry.chord.mods == chord.mods;
            });
            if (it != seen.end()) {
                warnings.append(QStringLiteral("`%1` is bound by both `%2` and `%3`; the first match wins")
                                    .arg(chord.render(), it->owner, binding.name));
            } else {
                seen.push_back(SeenChord{chord, binding.name});
            }
        }
    }

    if (!errors.isEmpty()) {
        return ConfigError::makeValidation(errors);
    }

    out->settings = config.settings;
    out->bindings = std::move(bindings);
    out->remaps = std::move(remaps);
    out->windowRules = std::move(windowRules);
    out->source = path;
    out->warnings = warnings;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// 动作校验
// ---------------------------------------------------------------------------

namespace {

void validateMenu(const QString &label, const std::vector<MenuItemDef> &items, QStringList *errors)
{
    if (items.empty()) {
        errors->append(QStringLiteral(
                           "%1: `menu` needs at least one item; each item looks like "
                           "{ key = 's', label = '睡眠' }")
                           .arg(label));
        return;
    }
    // 已经用过的快捷键：`(字符, 第几个条目)`。
    std::vector<std::pair<QChar, std::size_t>> used;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const MenuItemDef &item = items.at(index);
        const QString itemLabel = QStringLiteral("%1: menu item #%2").arg(label).arg(index + 1);
        if (item.label.trimmed().isEmpty()) {
            errors->append(QStringLiteral("%1: `label` must not be empty").arg(itemLabel));
        }
        if (item.key.has_value()) {
            const std::optional<QChar> ch = item.keyChar();
            const bool usable = ch.has_value() && ch->isLetterOrNumber();
            const bool punctuation = ch.has_value() && ch->isPunct();
            if (ch.has_value() && (usable || punctuation)) {
                const auto it = std::find_if(used.begin(), used.end(), [&ch](const auto &entry) {
                    return entry.first == *ch;
                });
                if (it != used.end()) {
                    errors->append(QStringLiteral("%1: key %2 is already used by menu item #%3")
                                       .arg(itemLabel, rustDebug(*item.key))
                                       .arg(it->second));
                } else {
                    used.emplace_back(*ch, index + 1);
                }
            } else {
                errors->append(QStringLiteral(
                                   "%1: key %2 must be a single ASCII letter, digit or punctuation "
                                   "mark (it is matched against the physical keyboard); drop it to "
                                   "make the item mouse/arrow-only")
                                   .arg(itemLabel, rustDebug(*item.key)));
            }
        }
        if (item.action) {
            std::vector<Action> nested;
            if (const auto error = item.action->flatten(&nested); error.has_value()) {
                errors->append(QStringLiteral("%1: action: %2").arg(itemLabel, *error));
            } else {
                for (const Action &nestedAction : nested) {
                    if (nestedAction.kind == Action::Kind::Menu) {
                        errors->append(QStringLiteral("%1: menus cannot be nested").arg(itemLabel));
                    } else {
                        validateAction(QStringLiteral("%1 (`%2`)").arg(itemLabel, item.label),
                                       nestedAction,
                                       errors);
                    }
                }
            }
        }
    }
}

} // namespace

void validateAction(const QString &label, const Action &action, QStringList *errors)
{
    switch (action.kind) {
    case Action::Kind::Send: {
        QVector<SendOp> ops;
        if (const auto error = parseKeyOrScript(action.keys, &ops); error.has_value()) {
            errors->append(QStringLiteral("%1: send %2: %3").arg(label, rustDebug(action.keys), error->message()));
        }
        break;
    }
    case Action::Kind::Volume:
        if (action.volumeOp == VolumeOp::Set && !action.level.has_value()) {
            errors->append(QStringLiteral("%1: volume op = \"set\" needs `level = 0..100`").arg(label));
        }
        if (action.level.has_value() && *action.level > 100) {
            errors->append(QStringLiteral("%1: volume level must be between 0 and 100 (got %2)")
                               .arg(label)
                               .arg(*action.level));
        }
        break;
    case Action::Kind::Clipboard:
        if ((action.clipboardOp == ClipboardOp::Set || action.clipboardOp == ClipboardOp::Append)
            && !action.clipboardText.has_value()) {
            errors->append(QStringLiteral(
                               "%1: clipboard op needs `text` (optional `{clipboard}`/`{selection}` templates)")
                               .arg(label));
        }
        break;
    case Action::Kind::Desktop:
        if (action.desktopSwitch == 0) {
            errors->append(QStringLiteral(
                               "%1: desktop switch must be 1 or greater (desktops are numbered from 1)")
                               .arg(label));
        }
        break;
    case Action::Kind::Window: {
        if (action.launch.has_value()) {
            if (action.launch->program.trimmed().isEmpty()) {
                errors->append(QStringLiteral("%1: launch.program must not be empty").arg(label));
            }
            if (action.windowOp != WindowOp::Activate) {
                errors->append(QStringLiteral("%1: `launch` only works with op = \"activate\", not %2")
                                   .arg(label, windowOpDebugName(action.windowOp)));
            }
            if (WindowQuery::make(action.target, action.process).isForeground()) {
                errors->append(QStringLiteral(
                                   "%1: `launch` needs `target` or `process`, otherwise the foreground "
                                   "window already matches")
                                   .arg(label));
            }
        }
        if (action.toggle.has_value() && action.windowOp != WindowOp::Activate) {
            errors->append(QStringLiteral("%1: `toggle` only works with op = \"activate\", not %2")
                               .arg(label, windowOpDebugName(action.windowOp)));
        }
        if (action.animate.has_value()
            && (action.windowOp == WindowOp::Close || action.windowOp == WindowOp::ToggleTopmost)) {
            errors->append(
                QStringLiteral("%1: `animate` has no effect on %2 (no window transition is involved); "
                               "drop it or use activate/minimize/maximize/restore")
                    .arg(label, windowOpDebugName(action.windowOp)));
        }
        break;
    }
    case Action::Kind::Menu:
        validateMenu(label, action.items, errors);
        break;
    case Action::Kind::Help:
        if (action.helpTitle.has_value() && action.helpTitle->trimmed().isEmpty()) {
            errors->append(QStringLiteral(
                               "%1: `help` title must not be empty; drop it to use the default")
                               .arg(label));
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 文件装载
// ---------------------------------------------------------------------------

bool isToml(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return !suffix.isEmpty() && suffix.compare(QLatin1String("toml"), Qt::CaseInsensitive) == 0;
}

QString stripUtf8Bom(const QString &text)
{
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF)) {
        return text.mid(1);
    }
    return text;
}

QByteArray stripUtf8Bom(const QByteArray &bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return bytes.mid(3);
    }
    return bytes;
}

std::optional<ConfigError> loadConfig(const Evaluator &evaluator,
                                      const QString &path,
                                      Compiled *out)
{
    // 指到旧版的 TOML 文件时直接说清楚，别让用户对着一屏 Lua 语法错误发呆。
    if (isToml(path)) {
        return ConfigError::makeLegacyToml(path, preferredConfigPath());
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ConfigError::makeIo(path, file.errorString());
    }
    // 记事本、以及 PowerShell 的 `Set-Content -Encoding UTF8` 都会写出 UTF-8 BOM，
    // 而 Lua 的词法分析器不认识它（第 1 行报 `unexpected symbol near '<\239>'`）。
    const QString text = stripUtf8Bom(QString::fromUtf8(stripUtf8Bom(file.readAll())));
    file.close();

    EvalResult evaluated;
    if (const auto error = evaluator(text, path, &evaluated); error.has_value()) {
        return error;
    }
    return compile(evaluated.config, path, evaluated.errors, evaluated.warnings, out);
}

// ---------------------------------------------------------------------------
// 位置
// ---------------------------------------------------------------------------

std::optional<QString> homeDir()
{
    for (const char *name : {"USERPROFILE", "HOME"}) {
        const QString value = qEnvironmentVariable(name);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return std::nullopt;
}

namespace {

std::optional<QString> preferredDir()
{
    const std::optional<QString> home = homeDir();
    if (!home.has_value()) {
        return std::nullopt;
    }
    return QDir(*home).filePath(QStringLiteral(".config/flowkeyd"));
}

QStringList legacyTomlCandidates()
{
    QStringList all;
    if (const auto home = homeDir(); home.has_value()) {
        all.append(QDir::toNativeSeparators(QDir(*home).filePath(QStringLiteral(".config/flowkeyd/config.toml"))));
        all.append(QDir::toNativeSeparators(QDir(*home).filePath(QStringLiteral(".config/flowkeyd.toml"))));
    }
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (!exeDir.isEmpty()) {
        all.append(QDir::toNativeSeparators(QDir(exeDir).filePath(QStringLiteral("config.toml"))));
        all.append(QDir::toNativeSeparators(QDir(exeDir).filePath(QStringLiteral("flowkeyd.toml"))));
    }
    const QString appdata = qEnvironmentVariable("APPDATA");
    if (!appdata.isEmpty()) {
        const QString dir = QDir(appdata).filePath(QStringLiteral("flowkeyd"));
        all.append(QDir::toNativeSeparators(QDir(dir).filePath(QStringLiteral("config.toml"))));
        all.append(QDir::toNativeSeparators(QDir(dir).filePath(QStringLiteral("flowkeyd.toml"))));
    }
    all.append(QStringLiteral("config.toml"));
    all.append(QStringLiteral("flowkeyd.toml"));
    return all;
}

} // namespace

QString preferredConfigPath()
{
    const auto dir = preferredDir();
    return dir.has_value()
               ? QDir::toNativeSeparators(QDir(*dir).filePath(QStringLiteral("config.lua")))
               : QStringLiteral("config.lua");
}

QString preferredLogPath()
{
    const auto dir = preferredDir();
    return dir.has_value()
               ? QDir::toNativeSeparators(QDir(*dir).filePath(QStringLiteral("flowkeyd.log")))
               : QStringLiteral("flowkeyd.log");
}

std::optional<QString> staleTomlConfig()
{
    for (const QString &path : legacyTomlCandidates()) {
        if (QFileInfo(path).isFile()) {
            return path;
        }
    }
    return std::nullopt;
}

QStringList candidateList(const QString &preferred,
                          const std::optional<QString> &exeDir,
                          const std::optional<QString> &appdata)
{
    QStringList all;
    all.append(preferred);
    if (exeDir.has_value()) {
        all.append(QDir(*exeDir).filePath(QStringLiteral("config.lua")));
    }
    if (appdata.has_value()) {
        all.append(QDir(QDir(*appdata).filePath(QStringLiteral("flowkeyd")))
                       .filePath(QStringLiteral("config.lua")));
    }
    all.append(QStringLiteral("config.lua"));

    // 主目录恰好是 exe 目录或当前目录时会出现重复项；去重让日志更干净。
    // 同时统一用本机的分隔符，这样错误信息里看到的是 `C:\Users\…\.config\flowkeyd\config.lua`
    // 而不是混着 `/` 的样子。
    QStringList unique;
    for (const QString &path : all) {
        const QString native = QDir::toNativeSeparators(path);
        if (!unique.contains(native)) {
            unique.append(native);
        }
    }
    return unique;
}

QStringList configPathCandidates()
{
    const QString appdata = qEnvironmentVariable("APPDATA");
    const QString exeDir = QCoreApplication::applicationDirPath();
    return candidateList(preferredConfigPath(),
                         exeDir.isEmpty() ? std::nullopt : std::optional<QString>(exeDir),
                         appdata.isEmpty() ? std::nullopt : std::optional<QString>(appdata));
}

QString pickConfigPath(const QStringList &candidates)
{
    for (const QString &path : candidates) {
        if (QFileInfo(path).isFile()) {
            return path;
        }
    }
    if (!candidates.isEmpty()) {
        return candidates.first();
    }
    return QStringLiteral("config.lua");
}

QString defaultConfigPath()
{
    return pickConfigPath(configPathCandidates());
}

} // namespace flowkeyd::core
