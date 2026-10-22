// `app::ConfigEditor`（图形化配置界面的模型层）的测试。
//
// 这里盯的是「界面上的每一次改动 → 保存 → 文件里真的对」这条链路，以及几个
// 容易做错的细节：新增条目的默认值必须能通过校验、装饰出来的标题 / 副标题不会
// 在编辑之后变陈旧、`save()` 在校验不过时**绝不落盘**、以及 `.bak` 备份。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app/config_editor.h"
#include "core/config.h"
#include "core/config_writer.h"
#include "lua/lua_config.h"

using namespace flowkeyd;
using namespace flowkeyd::app;
using namespace flowkeyd::core;

namespace {

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

/// 一份最小的、能通过校验的配置脚本。
QString sampleScript()
{
    return QStringLiteral(
        "return {\n"
        "  settings = { log_level = \"warn\", swallow = false },\n"
        "  hotkeys = {\n"
        "    { name = \"term\", keys = { \"Ctrl+Alt+t\" }, action = { type = \"run\", "
        "program = \"wt.exe\" } },\n"
        "  },\n"
        "  remaps = { { from = \"CapsLock\", to = \"Esc\" } },\n"
        "  window_rules = { { process = \"wezterm\", desktop = 2 } },\n"
        "  apps = { { process = \"wps\", hotkeys = { { keys = { \"Win+3\" } } } } },\n"
        "}\n");
}

} // namespace

class TestConfigEditor : public QObject
{
    Q_OBJECT

private:
    /// 建一个已经载入了 `script` 的编辑器（`script` 为空串表示文件不存在）。
    static std::unique_ptr<ConfigEditor> makeEditor(const QTemporaryDir &dir,
                                                    const QString &script = sampleScript())
    {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
        if (!script.isNull()) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(script.toUtf8());
            }
        }
        auto editor = std::make_unique<ConfigEditor>();
        editor->setEvaluator(lua::makeLuaEvaluator());
        return editor;
    }

private slots:
    void loadMissingFileStartsFromDefaults();
    void loadKeepsChordsAndSettings();
    void roundTripsTheExampleConfig();
    void addAndRemoveEntries();
    void decorationsFollowEdits();
    void refusesToSaveAnInvalidConfig();
    void saveWritesParseableLuaAndBacksUp();
    void saveRemembersMonitorIndexAsANumber();
    void saveRemembersTrayBadgeMode();
    void saveRemembersNestedWorkspaceSettings();
    void pickFreeChordAvoidsUsedOnes();
    void startFromDefaultsSurvivesBrokenFile();
};

void TestConfigEditor::loadMissingFileStartsFromDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir, QString());
    QVERIFY(editor->load(QDir(dir.path()).filePath(QStringLiteral("config.lua"))));
    QVERIFY(editor->loaded());
    // 全新的文件不是「手写的」：保存时不该让用户确认会丢注释。
    QVERIFY(editor->editorGenerated());
    QVERIFY(!editor->dirty());
    QCOMPARE(editor->hotkeys().size(), 0);
    // 默认配置也要能直接保存（等于写出一个空配置）。
    QVERIFY(editor->save());
    QVERIFY(isEditorGenerated(readAll(QDir(dir.path()).filePath(QStringLiteral("config.lua")))));
}

void TestConfigEditor::loadKeepsChordsAndSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir);
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(editor->load(path));
    QVERIFY(editor->loaded());
    // 用户手写的文件：标记不在，界面要告诉用户保存会丢注释。
    QVERIFY(!editor->editorGenerated());

    QCOMPARE(editor->settings().value(QStringLiteral("log_level")).toString(),
             QStringLiteral("warn"));
    QCOMPARE(editor->settings().value(QStringLiteral("swallow")).toBool(), false);
    QCOMPARE(editor->hotkeys().size(), 1);
    QCOMPARE(editor->remaps().size(), 1);
    QCOMPARE(editor->windowRules().size(), 1);
    QCOMPARE(editor->apps().size(), 1);
    // 列表行上的标题是算出来的。
    QCOMPARE(editor->hotkeys().at(0).toMap().value(QStringLiteral("_title")).toString(),
             QStringLiteral("term"));
    QVERIFY(editor->hotkeys()
                .at(0)
                .toMap()
                .value(QStringLiteral("_subtitle"))
                .toString()
                .contains(QStringLiteral("Ctrl+Alt+t")));
}

void TestConfigEditor::roundTripsTheExampleConfig()
{
    // 拿仓库里那份**参考配置**跑一遍「界面读进来 -> 保存 -> 再读回来」。
    // 它是文档上唯一一份覆盖全特性的配置，所以这是对 `core::config_map` 与
    // `core::config_writer` 覆盖面最广的一次检查：只要有一个字段在字典里丢了、
    // 或者写回 Lua 时名字/类型写错，这里就会看出来。
    const QString source = QStringLiteral(QT_TESTCASE_SOURCEDIR "/flowkeyd.lua.example");
    QVERIFY2(QFileInfo::exists(source), qPrintable(source));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(QFile::copy(source, path));

    ConfigEditor editor;
    editor.setEvaluator(lua::makeLuaEvaluator());
    QVERIFY2(editor.load(path), qPrintable(editor.loadError()));
    QVERIFY(editor.loaded());
    // 手写的文件（示例配置有注释与说明文字）。
    QVERIFY(!editor.editorGenerated());
    QVERIFY(editor.hotkeys().size() > 1);

    const QVariantMap beforeSettings = editor.settings();
    const QVariantList beforeHotkeys = editor.hotkeys();
    const QVariantList beforeRemaps = editor.remaps();
    const QVariantList beforeRules = editor.windowRules();
    const QVariantList beforeApps = editor.apps();

    QVERIFY2(editor.save(), qPrintable(editor.saveError()));
    QVERIFY(editor.editorGenerated());

    // 重新读一遍盘上那份，与保存前逐字段对比。
    ConfigEditor reloaded;
    reloaded.setEvaluator(lua::makeLuaEvaluator());
    QVERIFY2(reloaded.load(path), qPrintable(reloaded.loadError()));
    QCOMPARE(reloaded.settings(), beforeSettings);
    QCOMPARE(reloaded.hotkeys(), beforeHotkeys);
    QCOMPARE(reloaded.remaps(), beforeRemaps);
    QCOMPARE(reloaded.windowRules(), beforeRules);
    QCOMPARE(reloaded.apps(), beforeApps);

    // 再存一次必须逐字节一样（反复保存不会让文件漂移）。
    const QString once = readAll(path);
    QVERIFY2(reloaded.save(), qPrintable(reloaded.saveError()));
    QCOMPARE(readAll(path), once);
}

void TestConfigEditor::addAndRemoveEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir);
    QVERIFY(editor->load(QDir(dir.path()).filePath(QStringLiteral("config.lua"))));

    editor->appendHotkey(editor->newHotkey());
    QCOMPARE(editor->hotkeys().size(), 2);
    QVERIFY(editor->dirty());

    // 新条目的和弦不能和已有的撞（撞了就是一条非法配置）。
    const QStringList keys =
        editor->hotkeys().at(1).toMap().value(QStringLiteral("keys")).toStringList();
    QCOMPARE(keys.size(), 1);
    QVERIFY(keys.first() != QStringLiteral("Ctrl+Alt+t"));
    // 这条和弦现在“已经被用了”——被它自己。校验时要把它自己排掉。
    QVERIFY(editor->chordError(keys.first(), keys).isEmpty());
    QVERIFY(!editor->chordError(keys.first()).isEmpty());

    editor->duplicateHotkey(1);
    QCOMPARE(editor->hotkeys().size(), 3);
    const QStringList copyKeys =
        editor->hotkeys().at(2).toMap().value(QStringLiteral("keys")).toStringList();
    QVERIFY(!copyKeys.isEmpty());
    QVERIFY(copyKeys.first() != QStringLiteral("Ctrl+Alt+t"));
    QVERIFY(editor->chordError(copyKeys.first(), copyKeys).isEmpty());

    editor->moveHotkey(2, -1);
    editor->removeHotkey(0);
    QCOMPARE(editor->hotkeys().size(), 2);

    editor->appendRemap(editor->newRemap());
    QVariantMap rule = editor->newWindowRule();
    rule.insert(QStringLiteral("process"), QStringLiteral("code"));
    editor->appendWindowRule(rule);
    QVariantMap app = editor->newApp();
    app.insert(QStringLiteral("process"), QStringLiteral("wps"));
    editor->appendApp(app);
    QCOMPARE(editor->remaps().size(), 2);
    QCOMPARE(editor->windowRules().size(), 2);
    QCOMPARE(editor->apps().size(), 2);

    // 新增的重映射不能和已有的撞源键（两个 remap 用同一个源键没有意义）。
    QVERIFY(editor->remaps().at(1).toMap().value(QStringLiteral("from")).toString()
            != QStringLiteral("CapsLock"));

    // 全部新增条目都带上默认值，所以整份配置立刻就能保存。
    QVERIFY2(editor->save(), qPrintable(editor->saveError()));
}

void TestConfigEditor::decorationsFollowEdits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir);
    QVERIFY(editor->load(QDir(dir.path()).filePath(QStringLiteral("config.lua"))));

    QVariantMap entry = editor->hotkeys().at(0).toMap();
    entry.insert(QStringLiteral("name"), QStringLiteral("renamed"));
    QVariantList actions = entry.value(QStringLiteral("actions")).toList();
    entry.insert(QStringLiteral("actions"), actions);
    editor->updateHotkey(0, entry);

    const QVariantMap stored = editor->hotkeys().at(0).toMap();
    // 副标题必须跟着改，不能还写着改动之前的摘要。
    QCOMPARE(stored.value(QStringLiteral("_title")).toString(), QStringLiteral("renamed"));
    QVERIFY(!stored.value(QStringLiteral("_subtitle")).toString().isEmpty());
}

void TestConfigEditor::refusesToSaveAnInvalidConfig()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir);
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(editor->load(path));

    // 把和弦改成 `Ctrl+Alt+t` 已经用的那一个：校验必须挡住这次保存。
    QVariantMap entry = editor->hotkeys().at(0).toMap();
    entry.insert(QStringLiteral("keys"), QStringList{QStringLiteral("bogus+key")});
    editor->updateHotkey(0, entry);
    QVERIFY(!editor->save());
    QVERIFY(!editor->saveError().isEmpty());
    // 文件必须一个字节都没动（还是原来那份手写的脚本）。
    QCOMPARE(readAll(path), sampleScript());

    // 改回一个合法值就能存下去。
    entry.insert(QStringLiteral("keys"), QStringList{QStringLiteral("Ctrl+Alt+y")});
    editor->updateHotkey(0, entry);
    QVERIFY2(editor->save(), qPrintable(editor->saveError()));
    QVERIFY(editor->saveError().isEmpty());
    QVERIFY(editor->editorGenerated());
    QVERIFY(!editor->dirty());
}

void TestConfigEditor::saveWritesParseableLuaAndBacksUp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir);
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(editor->load(path));

    editor->setSetting(QStringLiteral("log_level"), QStringLiteral("debug"));
    QCOMPARE(editor->settings().value(QStringLiteral("log_level")).toString(),
             QStringLiteral("debug"));
    // `undefined`（QML 里的空值）等于把这个键删掉、回到默认。
    editor->setSetting(QStringLiteral("log_level"), QVariant());
    QVERIFY(!editor->settings().contains(QStringLiteral("log_level")));

    QVariantMap entry = editor->hotkeys().at(0).toMap();
    entry.insert(QStringLiteral("keys"),
                 QStringList{QStringLiteral("Ctrl+Alt+t"), QStringLiteral("Ctrl+Alt+T")});
    entry.remove(QStringLiteral("keys"));
    entry.insert(QStringLiteral("keys"), QStringList{QStringLiteral("Ctrl+Alt+t")});
    editor->updateHotkey(0, entry);
    QVERIFY2(editor->save(), qPrintable(editor->saveError()));

    // 写出来的东西必须真的能被 Lua 层读回来，而且值对得上。
    Compiled compiled;
    const auto failure = loadConfig(lua::makeLuaEvaluator(), path, &compiled);
    if (failure.has_value()) {
        QFAIL(qPrintable(failure->toString()));
    }
    // 示例脚本里有 1 条全局 hotkey，app 里还有 1 条 → 编译出 2 条绑定。
    QCOMPARE(compiled.bindings.size(), 2);
    QCOMPARE(compiled.remaps.size(), 1);
    QCOMPARE(compiled.windowRules.size(), 1);
    // 用户手写的那一份被改名成了 `.bak`。
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));
    QCOMPARE(readAll(path + QStringLiteral(".bak")), sampleScript());
}

void TestConfigEditor::saveRemembersMonitorIndexAsANumber()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir, QString());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(editor->load(path));

    editor->setSetting(QStringLiteral("window_monitor"), QStringLiteral("2"));
    QVariantMap rule = editor->newWindowRule();
    rule.insert(QStringLiteral("process"), QStringLiteral("code"));
    rule.insert(QStringLiteral("monitor"), 3);
    editor->appendWindowRule(rule);
    QVERIFY2(editor->save(), qPrintable(editor->saveError()));

    Compiled compiled;
    QVERIFY(!loadConfig(lua::makeLuaEvaluator(), path, &compiled).has_value());
    QCOMPARE(compiled.settings.windowMonitor.kind, MonitorRef::Kind::Index);
    QCOMPARE(compiled.settings.windowMonitor.index, 2u);
    QCOMPARE(compiled.windowRules.at(0).monitor->kind, MonitorRef::Kind::Index);
    QCOMPARE(compiled.windowRules.at(0).monitor->index, 3u);
}

void TestConfigEditor::saveRemembersTrayBadgeMode()
{
    // 托盘徽标的显示方式也是普通设置项：界面改一次就该原样存回文件
    // （`settings.tray_badge`，默认 `"both"` = 工作空间号 / 虚拟桌面号）。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir, QString());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(editor->load(path));
    QCOMPARE(editor->settings().value(QStringLiteral("tray_badge")).toString(),
             QStringLiteral("both"));

    editor->setSetting(QStringLiteral("tray_badge"), QStringLiteral("workspace"));
    QVERIFY2(editor->save(), qPrintable(editor->saveError()));

    Compiled compiled;
    QVERIFY(!loadConfig(lua::makeLuaEvaluator(), path, &compiled).has_value());
    QCOMPARE(compiled.settings.trayBadge, TrayBadgeMode::Workspace);
}

void TestConfigEditor::saveRemembersNestedWorkspaceSettings()
{
    // 界面把 `settings.workspace` 当成一张嵌套的表改（`setWorkspaceSetting()`），
    // 保存后必须真的落成一张表，并且每一项都能被 Lua 层读回原值。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir, QString());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(editor->load(path));

    // 默认就开着，且是一张完整的表（界面每一格都能读到值）。
    const QVariantMap defaults = editor->settings().value(QStringLiteral("workspace")).toMap();
    QCOMPARE(defaults.value(QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(defaults.value(QStringLiteral("count")).toUInt(), 9u);
    QCOMPARE(defaults.value(QStringLiteral("switch_target")).toString(),
             QStringLiteral("cursor"));
    QCOMPARE(defaults.value(QStringLiteral("switch_effect")).toString(),
             QStringLiteral("instant"));
    QCOMPARE(defaults.value(QStringLiteral("new_window")).toString(), QStringLiteral("active"));

    editor->setWorkspaceSetting(QStringLiteral("count"), 4);
    editor->setWorkspaceSetting(QStringLiteral("switch_target"), QStringLiteral("focus"));
    editor->setWorkspaceSetting(QStringLiteral("switch_effect"), QStringLiteral("freeze"));
    editor->setWorkspaceSetting(QStringLiteral("new_window"), 3);
    editor->setWorkspaceSetting(QStringLiteral("exclude"),
                                QStringList{QStringLiteral("game.exe")});
    // `undefined` 等于把这个键删掉（回到默认）。
    editor->setWorkspaceSetting(QStringLiteral("enabled"), QVariant());
    const QVariantMap edited = editor->settings().value(QStringLiteral("workspace")).toMap();
    QVERIFY(!edited.contains(QStringLiteral("enabled")));

    // 再加一条带工作空间的窗口规则与一个 workspace 动作，整份跑一次往返。
    QVariantMap rule = editor->newWindowRule();
    rule.insert(QStringLiteral("process"), QStringLiteral("code"));
    rule.insert(QStringLiteral("workspace"), 2);
    editor->appendWindowRule(rule);

    QVariantMap hotkey = editor->hotkeys().isEmpty() ? editor->newHotkey()
                                                    : editor->hotkeys().at(0).toMap();
    QVariantMap action = editor->newAction(QStringLiteral("workspace"));
    QCOMPARE(action.value(QStringLiteral("op")).toString(), QStringLiteral("switch"));
    action.insert(QStringLiteral("op"), QStringLiteral("move"));
    action.insert(QStringLiteral("index"), 3);
    QVariantList actions;
    actions.append(action);
    hotkey.insert(QStringLiteral("actions"), actions);
    if (editor->hotkeys().isEmpty()) {
        editor->appendHotkey(hotkey);
    } else {
        editor->updateHotkey(0, hotkey);
    }

    QVERIFY2(editor->save(), qPrintable(editor->saveError()));

    Compiled compiled;
    const auto failure = loadConfig(lua::makeLuaEvaluator(), path, &compiled);
    QVERIFY2(!failure.has_value(), failure.has_value() ? qPrintable(failure->toString()) : "");
    QCOMPARE(compiled.settings.workspace.count, 4u);
    QVERIFY(compiled.settings.workspace.enabled);
    QVERIFY(compiled.settings.workspace.switchTarget == WorkspaceSwitchTarget::Focus);
    QCOMPARE(compiled.settings.workspace.newWindow, 3u);
    QCOMPARE(compiled.settings.workspace.exclude, QStringList{QStringLiteral("game.exe")});
    QCOMPARE(compiled.windowRules.at(0).workspace, std::optional<std::uint32_t>(2));
    QCOMPARE(compiled.bindings.at(0).press.at(0).kind, Action::Kind::Workspace);
    QCOMPARE(compiled.bindings.at(0).press.at(0).workspaceOp, WorkspaceOp::Move);
    QCOMPARE(compiled.bindings.at(0).press.at(0).workspaceIndex, 3u);
}

void TestConfigEditor::pickFreeChordAvoidsUsedOnes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto editor = makeEditor(dir);
    QVERIFY(editor->load(QDir(dir.path()).filePath(QStringLiteral("config.lua"))));

    const QString free = editor->pickFreeChord();
    QVERIFY(!free.isEmpty());
    QVERIFY(editor->chordError(free).isEmpty());
    // `Ctrl+Alt+t` 已经在配置里了：校验必须报重复，但把原来那几个和弦传进来
    // 之后就不该再报（编辑一条已有绑定时就是这个场景）。
    QVERIFY(!editor->chordError(QStringLiteral("Ctrl+Alt+t")).isEmpty());
    QVERIFY(editor->chordError(QStringLiteral("Ctrl+Alt+t"),
                               QStringList{QStringLiteral("Ctrl+Alt+t")})
                .isEmpty());
}

void TestConfigEditor::startFromDefaultsSurvivesBrokenFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 语法坏的 Lua：连结构都读不出来，界面只能从空白开始。
    auto editor = makeEditor(dir, QStringLiteral("settings{ swallow = "));
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QVERIFY(!editor->load(path));
    QVERIFY(!editor->loaded());
    QVERIFY(!editor->loadError().isEmpty());

    editor->startFromDefaults();
    QVERIFY(editor->loaded());
    QVERIFY(editor->dirty());
    QVERIFY2(editor->save(), qPrintable(editor->saveError()));
    // 坏文件也要留底。
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));
}

QTEST_MAIN(TestConfigEditor)
#include "tst_config_editor.moc"
