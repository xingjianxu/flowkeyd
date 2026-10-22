// 图形化配置界面的模型层。
//
// 它把一份 `core::Config` 摊成 QML 好用的几张表（`settings` + 四个条目列表），
// 并把界面上的每一次修改攒回一份配置；`save()` 时整体重新生成 `config.lua`。
//
// **单一真源是这个类里的 root 字典**（`core::configToMap()` 的产物），不是磁盘上的
// 文件：界面上的编辑直接改字典，保存时才由 `core::configFromMap()` 还原成
// `core::Config` 并序列化。这样界面里出现的每一个字段都能对回配置文档。
//
// 三个刻意的分工：
//   * **模型不碰磁盘以外的 IO**，也不碰 QML/QtGui（只依赖 QtCore）—— 于是
//     `tst_config_editor` 能直接驱动它；
//   * **保存前一定先走一遍真正的 Lua 层与校验**：写出去的文本先被
//     `core::loadConfig()` 那条路读回来（同一个求值器 + `core::compile()`），
//     不通过就**根本不落盘** —— 界面不可能把一份非法配置写进文件；
//   * `save()` 自己不做「要不要覆盖手写文件」的确认：那是界面的事
//     （`editorGenerated` 为 false 时由 `ConfigWindow.qml` 先问一句）。
#pragma once

#include "core/config.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace flowkeyd::app {

class ConfigEditor : public QObject
{
    Q_OBJECT

    // ---- 状态 ----
    Q_PROPERTY(QString path READ path NOTIFY loadedChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    /// 磁盘上那份文件是**界面生成的**吗？false 表示里面有手写的注释 / Lua 代码，
    /// 界面保存会整体重写它们（界面要先问用户一次）。
    Q_PROPERTY(bool editorGenerated READ editorGenerated NOTIFY loadedChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadedChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveErrorChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY contentChanged)

    // ---- 配置本体 ----
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList hotkeys READ hotkeys NOTIFY hotkeysChanged)
    Q_PROPERTY(QVariantList remaps READ remaps NOTIFY remapsChanged)
    Q_PROPERTY(QVariantList windowRules READ windowRules NOTIFY windowRulesChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)

    // ---- 下拉框取值（都是常量，界面只需要读） ----
    Q_PROPERTY(QStringList keyNames READ keyNames CONSTANT)
    Q_PROPERTY(QStringList actionKinds READ actionKinds CONSTANT)
    Q_PROPERTY(QStringList logLevels READ logLevels CONSTANT)
    Q_PROPERTY(QStringList inputBackends READ inputBackends CONSTANT)
    Q_PROPERTY(QStringList triggerModes READ triggerModes CONSTANT)
    Q_PROPERTY(QStringList remapModes READ remapModes CONSTANT)
    Q_PROPERTY(QStringList showModes READ showModes CONSTANT)
    Q_PROPERTY(QStringList volumeOps READ volumeOps CONSTANT)
    Q_PROPERTY(QStringList mediaOps READ mediaOps CONSTANT)
    Q_PROPERTY(QStringList clipboardOps READ clipboardOps CONSTANT)
    Q_PROPERTY(QStringList windowOps READ windowOps CONSTANT)
    Q_PROPERTY(QStringList powerOps READ powerOps CONSTANT)
    Q_PROPERTY(QStringList toggleStates READ toggleStates CONSTANT)
    Q_PROPERTY(QStringList workspaceOps READ workspaceOps CONSTANT)
    Q_PROPERTY(QStringList workspaceSwitchTargets READ workspaceSwitchTargets CONSTANT)
    Q_PROPERTY(QStringList workspaceSwitchEffects READ workspaceSwitchEffects CONSTANT)
    Q_PROPERTY(QStringList trayBadgeModes READ trayBadgeModes CONSTANT)
    Q_PROPERTY(QStringList monitorPresets READ monitorPresets CONSTANT)
    Q_PROPERTY(QStringList defaultRemoteDesktopProcesses READ defaultRemoteDesktopProcesses CONSTANT)

public:
    explicit ConfigEditor(QObject *parent = nullptr);

    /// 注入真正的 Lua 求值器（`lua::makeLuaEvaluator()`）。
    /// **必须在 `load()` / `save()` 之前注入**；没注入时 `load()` 直接失败。
    void setEvaluator(core::Evaluator evaluator) { m_evaluator = std::move(evaluator); }

    QString path() const { return m_path; }
    bool loaded() const { return m_loaded; }
    bool dirty() const { return m_dirty; }
    bool editorGenerated() const { return m_editorGenerated; }
    QString loadError() const { return m_loadError; }
    QString saveError() const { return m_saveError; }
    QString summary() const;

    QVariantMap settings() const;
    QVariantList hotkeys() const;
    QVariantList remaps() const;
    QVariantList windowRules() const;
    QVariantList apps() const;

    QStringList keyNames() const;
    QStringList actionKinds() const;
    QStringList logLevels() const;
    QStringList inputBackends() const;
    QStringList triggerModes() const;
    QStringList remapModes() const;
    QStringList showModes() const;
    QStringList volumeOps() const;
    QStringList mediaOps() const;
    QStringList clipboardOps() const;
    QStringList windowOps() const;
    QStringList powerOps() const;
    QStringList toggleStates() const;
    QStringList workspaceOps() const;
    QStringList workspaceSwitchTargets() const;
    /// `settings.workspace.switch_effect` 的取值（`auto` / `instant` / `freeze`）。
    QStringList workspaceSwitchEffects() const;
    /// `settings.tray_badge` 的取值（`both` / `desktop` / `workspace`）。
    QStringList trayBadgeModes() const;
    QStringList monitorPresets() const;
    QStringList defaultRemoteDesktopProcesses() const;

    // ---- 读写（可从 QML 调） ----

    /// 读 `path`。文件不存在 → 用一份默认配置，`loaded` 为 true（用户第一次打开
    /// 设置界面就是这个状态）。文件在 Lua 层读不出来（语法错、未知字段）→ 返回
    /// false、`loaded` 保持 false，`loadError` 里是原因。
    ///
    /// **校验不过**（能读出结构、但 `compile()` 报错，例如和弦重复）的配置仍然会
    /// 载入界面：不然用户就没有别的办法修它了。那种情况下 `loaded` 为 true，同时
    /// `loadError` 里留着那条错误，界面把它显示成一条提示。
    Q_INVOKABLE bool load(const QString &path);
    /// 磁盘上那份文件读不出来时用：从一份空白配置开始（保存时会覆盖它，并留 `.bak`）。
    Q_INVOKABLE void startFromDefaults();
    /// 保存：序列化 -> 用真 Lua 层读回来校验 -> 写盘。
    Q_INVOKABLE bool save();
    /// 丢弃内存里的修改，重新从磁盘读（`load()` 的同义词，为了界面上好读）。
    Q_INVOKABLE bool revert() { return load(m_path); }

    // ---- settings ----
    /// 改一个设置项；`value` 无效（QML 里的 `undefined`）时删掉这个键（回到默认）。
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    /// 改 `settings.workspace` 里的一个字段（嵌套表）；`value` 无效时删掉它。
    Q_INVOKABLE void setWorkspaceSetting(const QString &key, const QVariant &value);
    /// 恢复内置的远程桌面进程名单（删掉 `remote_desktop_processes`）。
    Q_INVOKABLE void resetRemoteDesktopProcesses();

    // ---- 条目（hotkey / remap / window_rule / app） ----
    //
    // 界面的流程是「点新增 → 弹出编辑框（初始值来自 `newXxx()`）→ 确定」：
    // 条目只在用户填完的那一刻才进列表，这样列表里永远不会出现一条半成品。
    Q_INVOKABLE QVariantMap newHotkey() const;
    /// 追加一条条目，返回它所在的下标。
    Q_INVOKABLE int appendHotkey(const QVariantMap &entry);
    Q_INVOKABLE void duplicateHotkey(int index);
    Q_INVOKABLE void removeHotkey(int index);
    Q_INVOKABLE void moveHotkey(int index, int delta);
    Q_INVOKABLE void updateHotkey(int index, const QVariantMap &entry);

    Q_INVOKABLE QVariantMap newRemap() const;
    Q_INVOKABLE int appendRemap(const QVariantMap &entry);
    Q_INVOKABLE void removeRemap(int index);
    Q_INVOKABLE void moveRemap(int index, int delta);
    Q_INVOKABLE void updateRemap(int index, const QVariantMap &entry);

    Q_INVOKABLE QVariantMap newWindowRule() const;
    Q_INVOKABLE int appendWindowRule(const QVariantMap &entry);
    Q_INVOKABLE void removeWindowRule(int index);
    Q_INVOKABLE void moveWindowRule(int index, int delta);
    Q_INVOKABLE void updateWindowRule(int index, const QVariantMap &entry);

    Q_INVOKABLE QVariantMap newApp() const;
    Q_INVOKABLE int appendApp(const QVariantMap &entry);
    Q_INVOKABLE void removeApp(int index);
    Q_INVOKABLE void moveApp(int index, int delta);
    Q_INVOKABLE void updateApp(int index, const QVariantMap &entry);

    /// 一种动作的空字典（`defaultActionMap()` 的转发，界面「新增动作」用）。
    Q_INVOKABLE QVariantMap newAction(const QString &type) const;
    /// 一个没有被占用的和弦（新增快捷键时的起点，免得刚建出来就是一条错配置）。
    Q_INVOKABLE QString pickFreeChord() const;

    // ---- 即时校验（界面上一格填错就能看到，不用等到保存） ----
    /// 检查一个和弦（`"Ctrl+Alt+h"`）：没问题返回空串，否则返回英文原因。
    /// `ignore` 是「编辑原有绑定时，它自己已经在用的那几个和弦」（一个字符串或
    /// 一串字符串）：它们当然已经在配置里了，不该算重复。
    Q_INVOKABLE QString chordError(const QString &text,
                                   const QVariant &ignore = QVariant()) const;
    /// 检查 `remap.to` / `send` 的按键或发送脚本。
    Q_INVOKABLE QString keyOrScriptError(const QString &text) const;

signals:
    void loadedChanged();
    void dirtyChanged();
    void saveErrorChanged();
    void settingsChanged();
    void hotkeysChanged();
    void remapsChanged();
    void windowRulesChanged();
    void appsChanged();
    void contentChanged();
    /// 保存成功（参数是写出去的路径）。守护进程收到之后重载配置。
    void saved(const QString &path);

private:
    enum class Category { Hotkeys, Remaps, WindowRules, Apps };

    /// 用一份配置整体替换当前状态（装饰键也一并算好）。
    void applyConfig(const core::Config &config);
    /// 把 root 字典里的四个列表重新装饰一遍（标题 / 副标题）。
    void redecorate();

    QVariantList entryList(Category category) const;
    void setEntryList(Category category, const QVariantList &entries);
    QString keyOf(Category category) const;
    void notifyOf(Category category);
    void markDirty();

    QVariantMap newEntry(Category category) const;
    int appendEntry(Category category, const QVariantMap &entry);
    void duplicateEntry(Category category, int index);
    void removeEntry(Category category, int index);
    void moveEntry(Category category, int index, int delta);
    void updateEntry(Category category, int index, const QVariantMap &entry);
    /// 给一条条目算好列表行上显示的 `_title` / `_subtitle`（每次改动都重算，
    /// 否则列表里会一直显示改动之前的摘要）。
    void decorateEntry(Category category, QVariantMap *entry) const;

    /// 所有已经用到的和弦（渲染后的文本），`pickFreeChord()` 与重复提示用。
    QStringList usedChords() const;
    /// 一个还没被别的 `remap` 用作源键的按键（新增重映射时的起点）。
    QString pickFreeRemapSource() const;

    core::Evaluator m_evaluator;
    QVariantMap m_root;
    QString m_path;
    bool m_loaded = false;
    bool m_dirty = false;
    bool m_editorGenerated = false;
    QString m_loadError;
    QString m_saveError;
};

} // namespace flowkeyd::app
