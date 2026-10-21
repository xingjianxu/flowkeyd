#include "app/config_editor.h"

#include "core/action.h"
#include "core/config_map.h"
#include "core/config_writer.h"
#include "core/keys.h"
#include "core/remote_desktop.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace flowkeyd::app {

namespace {

using core::ActionSpec;
using core::AppDef;
using core::Config;
using core::HotkeyDef;
using core::RemapDef;
using core::WindowRuleDef;

QString keyName(const char *name)
{
    return QString::fromLatin1(name);
}

/// 一条绑定里的全部动作（简写与嵌套列表都展开）。
QString actionSummary(const ActionSpec &spec)
{
    std::vector<core::Action> actions;
    if (spec.flatten(&actions).has_value()) {
        return QString();
    }
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(actions.size()));
    for (const core::Action &action : actions) {
        parts.append(action.summary());
    }
    return parts.join(QStringLiteral(" ; "));
}

QString nonEmptyText(const std::optional<QString> &value)
{
    return value.has_value() ? value->trimmed() : QString();
}

QString hotkeyTitle(const HotkeyDef &def)
{
    if (!nonEmptyText(def.name).isEmpty()) {
        return nonEmptyText(def.name);
    }
    if (!def.keys.isEmpty()) {
        return def.keys.join(QStringLiteral(" / "));
    }
    return QStringLiteral("(no chord)");
}

QString hotkeySubtitle(const HotkeyDef &def)
{
    QStringList parts;
    if (!nonEmptyText(def.name).isEmpty() && !def.keys.isEmpty()) {
        parts.append(def.keys.join(QStringLiteral(" / ")));
    }
    QString action = def.action.has_value() ? actionSummary(*def.action) : QString();
    if (action.isEmpty()) {
        action = QStringLiteral("屏蔽按键");
    }
    parts.append(action);
    if (def.onRelease.has_value()) {
        parts.append(QStringLiteral("松开时再执行一次"));
    }
    if (def.trigger.has_value() && *def.trigger != core::TriggerMode::Press) {
        parts.append(QStringLiteral("trigger = %1").arg(core::triggerModeName(*def.trigger)));
    }
    if (def.repeat.has_value()) {
        parts.append(QStringLiteral("按住重复"));
    }
    if (!def.enabled) {
        parts.append(QStringLiteral("已禁用"));
    }
    return parts.join(QStringLiteral(" · "));
}

QString remapTitle(const RemapDef &def)
{
    if (!nonEmptyText(def.name).isEmpty()) {
        return nonEmptyText(def.name);
    }
    return def.from;
}

QString remapSubtitle(const RemapDef &def)
{
    QStringList parts;
    parts.append(QStringLiteral("%1 → %2").arg(def.from, def.to));
    if (def.mode == core::RemapMode::Tap) {
        parts.append(QStringLiteral("按一下触发一次"));
    }
    if (def.enabled == false) {
        parts.append(QStringLiteral("已禁用"));
    }
    return parts.join(QStringLiteral(" · "));
}

QString windowRuleTitle(const WindowRuleDef &def)
{
    for (const std::optional<QString> &candidate : {def.name, def.process, def.title}) {
        if (!nonEmptyText(candidate).isEmpty()) {
            return nonEmptyText(candidate);
        }
    }
    return QStringLiteral("(no match)");
}

QString windowRuleSubtitle(const WindowRuleDef &def)
{
    QStringList parts;
    if (def.desktop.has_value()) {
        parts.append(QStringLiteral("桌面 %1").arg(*def.desktop));
    }
    if (def.allDesktops.has_value()) {
        parts.append(*def.allDesktops ? QStringLiteral("所有桌面") : QStringLiteral("取消钉住"));
    }
    if (def.monitor.has_value()) {
        parts.append(QStringLiteral("显示器 %1").arg(def.monitor->describe()));
    }
    if (def.workspace.has_value()) {
        parts.append(QStringLiteral("工作空间 %1").arg(*def.workspace));
    }
    if (def.maximize.has_value() && *def.maximize) {
        parts.append(QStringLiteral("最大化"));
    }
    if (def.x.has_value() || def.y.has_value()) {
        parts.append(QStringLiteral("位置 %1,%2")
                         .arg(def.x.value_or(0))
                         .arg(def.y.value_or(0)));
    }
    if (def.width.has_value() || def.height.has_value()) {
        parts.append(QStringLiteral("大小 %1x%2")
                         .arg(def.width.value_or(0))
                         .arg(def.height.value_or(0)));
    }
    if (def.topmost.has_value()) {
        parts.append(*def.topmost ? QStringLiteral("置顶") : QStringLiteral("取消置顶"));
    }
    if (!def.enabled) {
        parts.append(QStringLiteral("已禁用"));
    }
    if (parts.isEmpty()) {
        parts.append(QStringLiteral("不做任何事"));
    }
    return parts.join(QStringLiteral(" · "));
}

QString appTitle(const AppDef &def)
{
    for (const std::optional<QString> &candidate : {def.name, def.process, def.title}) {
        if (!nonEmptyText(candidate).isEmpty()) {
            return nonEmptyText(candidate);
        }
    }
    return QStringLiteral("(no match)");
}

QString appSubtitle(const AppDef &def)
{
    QStringList parts;
    if (def.process.has_value() || def.title.has_value()) {
        QStringList match;
        if (def.process.has_value()) {
            match.append(QStringLiteral("process %1").arg(*def.process));
        }
        if (def.title.has_value()) {
            match.append(QStringLiteral("title %1").arg(*def.title));
        }
        parts.append(match.join(QStringLiteral(", ")));
    }
    if (def.launch.has_value()) {
        parts.append(QStringLiteral("启动 %1").arg(def.launch->program));
    }
    if (def.workspace.has_value()) {
        parts.append(QStringLiteral("工作空间 %1").arg(*def.workspace));
    }
    if (def.window.has_value()) {
        parts.append(windowRuleSubtitle(*def.window));
    }
    if (!def.hotkeys.empty()) {
        parts.append(QStringLiteral("%1 个快捷键").arg(static_cast<int>(def.hotkeys.size())));
    }
    if (!def.enabled) {
        parts.append(QStringLiteral("已禁用"));
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

ConfigEditor::ConfigEditor(QObject *parent) : QObject(parent)
{
    applyConfig(Config{});
    m_editorGenerated = true;
}

// ---------------------------------------------------------------------------
// 读
// ---------------------------------------------------------------------------

QString ConfigEditor::summary() const
{
    return QStringLiteral("%1 个快捷键 · %2 条重映射 · %3 条窗口规则 · %4 个程序")
        .arg(static_cast<int>(entryList(Category::Hotkeys).size()))
        .arg(static_cast<int>(entryList(Category::Remaps).size()))
        .arg(static_cast<int>(entryList(Category::WindowRules).size()))
        .arg(static_cast<int>(entryList(Category::Apps).size()));
}

QVariantMap ConfigEditor::settings() const
{
    return m_root.value(keyName("settings")).toMap();
}

QVariantList ConfigEditor::hotkeys() const
{
    return entryList(Category::Hotkeys);
}

QVariantList ConfigEditor::remaps() const
{
    return entryList(Category::Remaps);
}

QVariantList ConfigEditor::windowRules() const
{
    return entryList(Category::WindowRules);
}

QVariantList ConfigEditor::apps() const
{
    return entryList(Category::Apps);
}

QStringList ConfigEditor::keyNames() const
{
    return core::allKeyNames();
}

QStringList ConfigEditor::actionKinds() const
{
    return core::actionKindNames();
}

QStringList ConfigEditor::logLevels() const
{
    return core::logLevelNames();
}

QStringList ConfigEditor::inputBackends() const
{
    return core::inputBackendNames();
}

QStringList ConfigEditor::triggerModes() const
{
    return core::triggerModeNames();
}

QStringList ConfigEditor::remapModes() const
{
    return core::remapModeNames();
}

QStringList ConfigEditor::showModes() const
{
    return core::showModeNames();
}

QStringList ConfigEditor::volumeOps() const
{
    return core::volumeOpNames();
}

QStringList ConfigEditor::mediaOps() const
{
    return core::mediaOpNames();
}

QStringList ConfigEditor::clipboardOps() const
{
    return core::clipboardOpNames();
}

QStringList ConfigEditor::windowOps() const
{
    return core::windowOpNames();
}

QStringList ConfigEditor::powerOps() const
{
    return core::powerOpNames();
}

QStringList ConfigEditor::toggleStates() const
{
    return core::toggleStateNames();
}

QStringList ConfigEditor::workspaceOps() const
{
    return core::workspaceOpNames();
}

QStringList ConfigEditor::workspaceSwitchTargets() const
{
    return core::workspaceSwitchTargetNames();
}

QStringList ConfigEditor::trayBadgeModes() const
{
    return core::trayBadgeModeNames();
}

QStringList ConfigEditor::monitorPresets() const
{
    return {QStringLiteral("cursor"), QStringLiteral("last"),  QStringLiteral("none"),
            QStringLiteral("primary"), QStringLiteral("1"),    QStringLiteral("2"),
            QStringLiteral("3")};
}

QStringList ConfigEditor::defaultRemoteDesktopProcesses() const
{
    return core::builtinRemoteDesktopProcesses();
}

// ---------------------------------------------------------------------------
// 装载 / 保存
// ---------------------------------------------------------------------------

void ConfigEditor::applyConfig(const Config &config)
{
    m_root = core::configToMap(config);
    redecorate();
    emit settingsChanged();
    emit hotkeysChanged();
    emit remapsChanged();
    emit windowRulesChanged();
    emit appsChanged();
    emit contentChanged();
}

void ConfigEditor::redecorate()
{
    for (const Category category :
         {Category::Hotkeys, Category::Remaps, Category::WindowRules, Category::Apps}) {
        QVariantList list = entryList(category);
        for (int i = 0; i < list.size(); ++i) {
            QVariantMap entry = list.at(i).toMap();
            decorateEntry(category, &entry);
            list[i] = entry;
        }
        setEntryList(category, list);
    }
}

void ConfigEditor::decorateEntry(Category category, QVariantMap *entry) const
{
    QStringList errors;
    switch (category) {
    case Category::Hotkeys: {
        const HotkeyDef def = core::hotkeyFromMap(*entry, &errors, QString());
        entry->insert(keyName("_title"), hotkeyTitle(def));
        entry->insert(keyName("_subtitle"), hotkeySubtitle(def));
        break;
    }
    case Category::Remaps: {
        const RemapDef def = core::remapFromMap(*entry);
        entry->insert(keyName("_title"), remapTitle(def));
        entry->insert(keyName("_subtitle"), remapSubtitle(def));
        break;
    }
    case Category::WindowRules: {
        const WindowRuleDef def = core::windowRuleFromMap(*entry);
        entry->insert(keyName("_title"), windowRuleTitle(def));
        entry->insert(keyName("_subtitle"), windowRuleSubtitle(def));
        break;
    }
    case Category::Apps: {
        const AppDef def = core::appFromMap(*entry, &errors, QString());
        entry->insert(keyName("_title"), appTitle(def));
        entry->insert(keyName("_subtitle"), appSubtitle(def));
        break;
    }
    }
}

bool ConfigEditor::load(const QString &path)
{
    m_path = path;
    m_loadError.clear();
    if (!m_evaluator) {
        m_loadError = QStringLiteral("the configuration evaluator is not available");
        m_loaded = false;
        emit loadedChanged();
        return false;
    }
    if (!QFileInfo(path).isFile()) {
        // 还没有配置文件：从默认值开始，保存时不算“覆盖手写文件”。
        applyConfig(Config{});
        m_editorGenerated = true;
        m_loaded = true;
        m_dirty = false;
        emit loadedChanged();
        emit dirtyChanged();
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_loadError = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        m_loaded = false;
        emit loadedChanged();
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    const QString text = core::stripUtf8Bom(QString::fromUtf8(core::stripUtf8Bom(bytes)));
    m_editorGenerated = core::isEditorGenerated(text);

    core::EvalResult evaluated;
    if (const auto failure = m_evaluator(text, path, &evaluated); failure.has_value()) {
        // 连结构都读不出来（Lua 语法错、未知字段）：界面没有任何可以编辑的东西。
        m_loadError = failure->toString();
        m_loaded = false;
        emit loadedChanged();
        return false;
    }
    // 结构读出来了，但校验可能不过（和弦重复之类）。照样载入界面，把错误留成一条
    // 提示 —— 不然用户没有别的办法修它。
    core::Compiled compiled;
    if (const auto failure = core::compile(evaluated.config, path, evaluated.errors,
                                           evaluated.warnings, &compiled);
        failure.has_value()) {
        m_loadError = failure->toString();
    }
    applyConfig(evaluated.config);
    m_loaded = true;
    m_dirty = false;
    emit loadedChanged();
    emit dirtyChanged();
    return true;
}

void ConfigEditor::startFromDefaults()
{
    applyConfig(Config{});
    m_loaded = true;
    m_loadError.clear();
    m_dirty = true;
    emit loadedChanged();
    emit dirtyChanged();
}

bool ConfigEditor::save()
{
    if (!m_loaded) {
        return false;
    }
    if (!m_evaluator) {
        m_saveError = QStringLiteral("the configuration evaluator is not available");
        emit saveErrorChanged();
        return false;
    }
    if (m_saveError.isEmpty() == false) {
        m_saveError.clear();
        emit saveErrorChanged();
    }
    QStringList errors;
    const Config config = core::configFromMap(m_root, &errors);
    const QString text = core::configToLua(config);

    // 写盘之前先走一遍与守护进程**完全相同**的路径（求值 + 校验）：不通过就
    // 根本不落盘，界面不可能把一份非法配置写进文件。
    core::EvalResult evaluated;
    if (const auto failure = m_evaluator(text, m_path, &evaluated); failure.has_value()) {
        m_saveError = failure->toString();
        emit saveErrorChanged();
        return false;
    }
    QStringList allErrors = errors;
    allErrors += evaluated.errors;
    core::Compiled compiled;
    if (const auto failure = core::compile(evaluated.config, m_path, allErrors,
                                           evaluated.warnings, &compiled);
        failure.has_value()) {
        m_saveError = failure->toString();
        emit saveErrorChanged();
        return false;
    }
    QString ioError;
    if (!core::writeConfigLuaFile(m_path, text, &ioError)) {
        m_saveError = ioError;
        emit saveErrorChanged();
        return false;
    }
    m_editorGenerated = true;
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged();
    }
    emit loadedChanged();
    emit saved(m_path);
    return true;
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

void ConfigEditor::setSetting(const QString &key, const QVariant &value)
{
    QVariantMap current = settings();
    if (value.isValid()) {
        current.insert(key, value);
    } else {
        current.remove(key);
    }
    m_root.insert(keyName("settings"), current);
    emit settingsChanged();
    emit contentChanged();
    markDirty();
}

void ConfigEditor::resetRemoteDesktopProcesses()
{
    setSetting(keyName("remote_desktop_processes"), QVariant());
}

void ConfigEditor::setWorkspaceSetting(const QString &key, const QVariant &value)
{
    QVariantMap current = settings();
    QVariantMap workspace = current.value(keyName("workspace")).toMap();
    if (value.isValid()) {
        workspace.insert(key, value);
    } else {
        workspace.remove(key);
    }
    current.insert(keyName("workspace"), workspace);
    m_root.insert(keyName("settings"), current);
    emit settingsChanged();
    emit contentChanged();
    markDirty();
}

// ---------------------------------------------------------------------------
// 条目
// ---------------------------------------------------------------------------

QVariantList ConfigEditor::entryList(Category category) const
{
    return m_root.value(keyOf(category)).toList();
}

void ConfigEditor::setEntryList(Category category, const QVariantList &entries)
{
    m_root.insert(keyOf(category), entries);
}

QString ConfigEditor::keyOf(Category category) const
{
    switch (category) {
    case Category::Hotkeys:
        return keyName("hotkeys");
    case Category::Remaps:
        return keyName("remaps");
    case Category::WindowRules:
        return keyName("window_rules");
    case Category::Apps:
        return keyName("apps");
    }
    return keyName("hotkeys");
}

void ConfigEditor::notifyOf(Category category)
{
    switch (category) {
    case Category::Hotkeys:
        emit hotkeysChanged();
        break;
    case Category::Remaps:
        emit remapsChanged();
        break;
    case Category::WindowRules:
        emit windowRulesChanged();
        break;
    case Category::Apps:
        emit appsChanged();
        break;
    }
    emit contentChanged();
}

void ConfigEditor::markDirty()
{
    if (m_dirty) {
        return;
    }
    m_dirty = true;
    emit dirtyChanged();
}

QVariantMap ConfigEditor::newEntry(Category category) const
{
    switch (category) {
    case Category::Hotkeys: {
        QVariantMap entry;
        entry.insert(keyName("name"), QString());
        QVariantList keys;
        keys.append(pickFreeChord());
        entry.insert(keyName("keys"), keys);
        entry.insert(keyName("actions"), QVariantList());
        entry.insert(keyName("release_actions"), QVariantList());
        entry.insert(keyName("repeatable"), false);
        entry.insert(keyName("enabled"), true);
        return entry;
    }
    case Category::Remaps: {
        QVariantMap entry;
        entry.insert(keyName("from"), pickFreeRemapSource());
        entry.insert(keyName("to"), QStringLiteral("Esc"));
        entry.insert(keyName("mode"), QStringLiteral("hold"));
        entry.insert(keyName("enabled"), true);
        return entry;
    }
    case Category::WindowRules: {
        QVariantMap entry;
        // 留空：`window_rule` / `app` 必须至少有一个匹配条件，界面在用户填完
        // 之后才把条目追加进列表（`appendXxx()`），所以这里不需要假占位值。
        entry.insert(keyName("process"), QString());
        entry.insert(keyName("monitor"), QStringLiteral("cursor"));
        entry.insert(keyName("enabled"), true);
        return entry;
    }
    case Category::Apps: {
        QVariantMap entry;
        entry.insert(keyName("process"), QString());
        entry.insert(keyName("enabled"), true);
        entry.insert(keyName("window"), QVariantMap());
        entry.insert(keyName("launch"), QVariantMap());
        entry.insert(keyName("hotkeys"), QVariantList());
        return entry;
    }
    }
    return {};
}

int ConfigEditor::appendEntry(Category category, const QVariantMap &entry)
{
    QVariantList list = entryList(category);
    QVariantMap stored = entry;
    decorateEntry(category, &stored);
    list.append(stored);
    setEntryList(category, list);
    notifyOf(category);
    markDirty();
    return static_cast<int>(list.size()) - 1;
}

void ConfigEditor::duplicateEntry(Category category, int index)
{
    QVariantList list = entryList(category);
    if (index < 0 || index >= list.size()) {
        return;
    }
    QVariantMap copy = list.at(index).toMap();
    // 复制出来的条目不能顶着同一个和弦（`--check` 会报重复）。
    if (category == Category::Hotkeys) {
        QVariantList keys = copy.value(keyName("keys")).toList();
        if (!keys.isEmpty()) {
            keys[0] = pickFreeChord();
        }
        copy.insert(keyName("keys"), keys);
    }
    if (category == Category::Apps) {
        QVariantList keys = copy.value(keyName("hotkeys")).toList();
        for (int i = 0; i < keys.size(); ++i) {
            QVariantMap hotkey = keys.at(i).toMap();
            QVariantList chords = hotkey.value(keyName("keys")).toList();
            if (!chords.isEmpty()) {
                chords[0] = pickFreeChord();
            }
            hotkey.insert(keyName("keys"), chords);
            keys[i] = hotkey;
        }
        copy.insert(keyName("hotkeys"), keys);
    }
    decorateEntry(category, &copy);
    list.insert(index + 1, copy);
    setEntryList(category, list);
    notifyOf(category);
    markDirty();
}

void ConfigEditor::removeEntry(Category category, int index)
{
    QVariantList list = entryList(category);
    if (index < 0 || index >= list.size()) {
        return;
    }
    list.removeAt(index);
    setEntryList(category, list);
    notifyOf(category);
    markDirty();
}

void ConfigEditor::moveEntry(Category category, int index, int delta)
{
    QVariantList list = entryList(category);
    const int target = index + delta;
    if (index < 0 || index >= list.size() || target < 0 || target >= list.size()) {
        return;
    }
    list.move(index, target);
    setEntryList(category, list);
    notifyOf(category);
    markDirty();
}

void ConfigEditor::updateEntry(Category category, int index, const QVariantMap &entry)
{
    QVariantList list = entryList(category);
    if (index < 0 || index >= list.size()) {
        return;
    }
    QVariantMap updated = entry;
    decorateEntry(category, &updated);
    list[index] = updated;
    setEntryList(category, list);
    notifyOf(category);
    markDirty();
}

QVariantMap ConfigEditor::newHotkey() const
{
    return newEntry(Category::Hotkeys);
}

int ConfigEditor::appendHotkey(const QVariantMap &entry)
{
    return appendEntry(Category::Hotkeys, entry);
}

void ConfigEditor::duplicateHotkey(int index)
{
    duplicateEntry(Category::Hotkeys, index);
}

void ConfigEditor::removeHotkey(int index)
{
    removeEntry(Category::Hotkeys, index);
}

void ConfigEditor::moveHotkey(int index, int delta)
{
    moveEntry(Category::Hotkeys, index, delta);
}

void ConfigEditor::updateHotkey(int index, const QVariantMap &entry)
{
    updateEntry(Category::Hotkeys, index, entry);
}

QVariantMap ConfigEditor::newRemap() const
{
    return newEntry(Category::Remaps);
}

int ConfigEditor::appendRemap(const QVariantMap &entry)
{
    return appendEntry(Category::Remaps, entry);
}

void ConfigEditor::removeRemap(int index)
{
    removeEntry(Category::Remaps, index);
}

void ConfigEditor::moveRemap(int index, int delta)
{
    moveEntry(Category::Remaps, index, delta);
}

void ConfigEditor::updateRemap(int index, const QVariantMap &entry)
{
    updateEntry(Category::Remaps, index, entry);
}

QVariantMap ConfigEditor::newWindowRule() const
{
    return newEntry(Category::WindowRules);
}

int ConfigEditor::appendWindowRule(const QVariantMap &entry)
{
    return appendEntry(Category::WindowRules, entry);
}

void ConfigEditor::removeWindowRule(int index)
{
    removeEntry(Category::WindowRules, index);
}

void ConfigEditor::moveWindowRule(int index, int delta)
{
    moveEntry(Category::WindowRules, index, delta);
}

void ConfigEditor::updateWindowRule(int index, const QVariantMap &entry)
{
    updateEntry(Category::WindowRules, index, entry);
}

QVariantMap ConfigEditor::newApp() const
{
    return newEntry(Category::Apps);
}

int ConfigEditor::appendApp(const QVariantMap &entry)
{
    return appendEntry(Category::Apps, entry);
}

void ConfigEditor::removeApp(int index)
{
    removeEntry(Category::Apps, index);
}

void ConfigEditor::moveApp(int index, int delta)
{
    moveEntry(Category::Apps, index, delta);
}

void ConfigEditor::updateApp(int index, const QVariantMap &entry)
{
    updateEntry(Category::Apps, index, entry);
}

QVariantMap ConfigEditor::newAction(const QString &type) const
{
    return core::defaultActionMap(type);
}

// ---------------------------------------------------------------------------
// 即时校验
// ---------------------------------------------------------------------------

QStringList ConfigEditor::usedChords() const
{
    QStringList out;    QStringList errors;
    const auto collect = [&out](const HotkeyDef &def) {
        for (const QString &chord : def.keys) {
            core::Chord parsed;
            if (core::parseChord(chord, &parsed).has_value()) {
                out.append(core::normalizeKeyName(chord));
            } else {
                out.append(parsed.render());
            }
        }
    };
    for (const QVariant &item : entryList(Category::Hotkeys)) {
        collect(core::hotkeyFromMap(item.toMap(), &errors, QString()));
    }
    for (const QVariant &item : entryList(Category::Apps)) {
        const AppDef def = core::appFromMap(item.toMap(), &errors, QString());
        for (const HotkeyDef &hotkey : def.hotkeys) {
            collect(hotkey);
        }
    }
    return out;
}

QString ConfigEditor::pickFreeChord() const
{
    const QStringList used = usedChords();
    const QStringList modifiers = {QStringLiteral("Ctrl+Alt"), QStringLiteral("Ctrl+Shift"),
                                   QStringLiteral("Alt+Shift"),
                                   QStringLiteral("Ctrl+Alt+Shift")};
    const QString letters = QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789");
    for (const QString &modifier : modifiers) {
        for (const QChar letter : letters) {
            const QString text = modifier + QLatin1Char('+') + letter;
            core::Chord chord;
            if (core::parseChord(text, &chord).has_value()) {
                continue;
            }
            if (!used.contains(chord.render())) {
                return text;
            }
        }
    }
    return QStringLiteral("Ctrl+Alt+F12");
}

QString ConfigEditor::pickFreeRemapSource() const
{
    QStringList used;
    for (const QVariant &item : entryList(Category::Remaps)) {
        used.append(core::remapFromMap(item.toMap()).from);
    }
    const QStringList candidates = {QStringLiteral("CapsLock"), QStringLiteral("ScrollLock"),
                                    QStringLiteral("Pause"),    QStringLiteral("Insert"),
                                    QStringLiteral("F13"),      QStringLiteral("F14"),
                                    QStringLiteral("F15"),      QStringLiteral("F16")};
    for (const QString &candidate : candidates) {
        if (!used.contains(candidate)) {
            return candidate;
        }
    }
    return QStringLiteral("F24");
}

QString ConfigEditor::chordError(const QString &text, const QVariant &ignore) const
{
    core::Chord chord;
    if (const auto error = core::parseChord(text, &chord); error.has_value()) {
        return error->message();
    }
    // 编辑一条已有的绑定时，它自己的和弦当然“已经被用了”：调用方把原来那几个
    // 和弦传进来，这里先排掉。
    QStringList skipped;
    if (ignore.metaType().id() == QMetaType::QString) {
        skipped.append(ignore.toString());
    } else if (ignore.canConvert<QVariantList>()) {
        for (const QVariant &value : ignore.toList()) {
            skipped.append(value.toString());
        }
    }
    const QStringList used = usedChords();
    const QString target = chord.render();
    int occurrences = 0;
    for (const QString &candidate : used) {
        if (candidate == target) {
            ++occurrences;
        }
    }
    int skippedCount = 0;
    for (const QString &candidate : skipped) {
        core::Chord parsed;
        if (!core::parseChord(candidate, &parsed).has_value() && parsed.render() == target) {
            ++skippedCount;
        }
    }
    if (occurrences > skippedCount) {
        return QStringLiteral("this chord is already used by another hotkey");
    }
    return {};
}

QString ConfigEditor::keyOrScriptError(const QString &text) const
{
    QVector<core::SendOp> ops;
    if (const auto error = core::parseKeyOrScript(text, &ops); error.has_value()) {
        return error->message();
    }
    return {};
}

} // namespace flowkeyd::app
