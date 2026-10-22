// `core::configToLua()` / `core::configToMap()` 的往返测试。
//
// 图形化配置界面保存时是「结构体 -> Lua 源码 -> 覆盖 config.lua」，所以这里最重要
// 的一条断言是：**写出去再读回来，与原来一模一样**。为了让这条断言真正有意义，
// 读回来用的是真正的 Lua 层（`lua::makeLuaEvaluator()`），而不是另一个 C++ 函数
// —— 字段名写错、类型写错（例如把显示器序号写成字符串）都会在这里立刻现形。
//
// 覆盖的范围：`settings` 的每一个字段（含嵌套的 `settings.workspace`）、全部 21 种动作、每种条目
// （hotkey / remap / window_rule / app）的全部字段，以及字符串转义、重复参数、`short` 简写展开、
// `remote_desktop` 名单的两种形状。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/config.h"
#include "core/config_map.h"
#include "core/config_writer.h"
#include "lua/lua_config.h"

using namespace flowkeyd;
using namespace flowkeyd::core;

namespace {

Action base(Action::Kind kind)
{
    Action action;
    action.kind = kind;
    return action;
}

/// 一个窗口查询动作（`launch` 的字段掩码全开：C++ 里直接构造的 `LaunchSpec`
/// 没有“没写”的概念）。
Action windowAction()
{
    Action action = base(Action::Kind::Window);
    action.windowOp = WindowOp::Activate;
    action.target = QStringLiteral("Notepad");
    action.process = QStringLiteral("notepad");
    action.toggle = false;
    action.animate = true;
    action.monitor = MonitorRef{MonitorRef::Kind::Index, 2, QString()};
    LaunchSpec launch;
    launch.program = QStringLiteral("C:\\tools\\notepad.exe");
    launch.args = {QStringLiteral("--flag"), QStringLiteral("a\"b")};
    launch.cwd = QStringLiteral("C:\\tools");
    launch.show = ShowMode::Minimized;
    launch.shell = true;
    launch.env = {{QStringLiteral("K"), QStringLiteral("V")}};
    launch.waitMs = 1500;
    launch.elevated = true;
    action.launch = launch;
    action.waitMs = 2000;
    return action;
}

/// 一个嵌套的 `menu` 动作：条目里既有动作，也有「只关掉选单」的空条目。
Action menuAction()
{
    Action action = base(Action::Kind::Menu);
    action.menuTitle = QStringLiteral("电源");

    MenuItemDef first;
    first.key = QStringLiteral("s");
    first.label = QStringLiteral("睡眠");
    first.hint = QStringLiteral("sleep");
    {
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = base(Action::Kind::Power);
        spec.action.powerOp = PowerOp::Sleep;
        first.action = std::make_shared<ActionSpec>(std::move(spec));
    }
    action.items.push_back(first);

    MenuItemDef second;
    second.label = QStringLiteral("什么都不做");
    action.items.push_back(second);

    MenuItemDef third;
    third.key = QStringLiteral("d");
    third.label = QStringLiteral("深层动作");
    {
        // 嵌套列表 + `short` 简写：界面里统一展开成动作表。
        ActionSpec nested;
        nested.kind = ActionSpec::Kind::One;
        nested.action = base(Action::Kind::Reload);
        ActionSpec shorthand;
        shorthand.kind = ActionSpec::Kind::Short;
        shorthand.shorthand = QStringLiteral("send:^{c}");
        ActionSpec list;
        list.kind = ActionSpec::Kind::List;
        list.list.push_back(nested);
        list.list.push_back(shorthand);
        third.action = std::make_shared<ActionSpec>(std::move(list));
    }
    action.items.push_back(third);
    return action;
}

/// 每种动作各一个，全部用非默认值。
std::vector<Action> allActions()
{
    std::vector<Action> out;

    {
        Action action = base(Action::Kind::Run);
        action.program = QStringLiteral("C:\\Program Files\\my app.exe");
        action.args = {QStringLiteral("--flag"), QStringLiteral("x\\y"), QStringLiteral("带 空格")};
        action.cwd = QStringLiteral("C:\\Program Files");
        action.show = ShowMode::Hidden;
        action.shell = true;
        action.wait = true;
        action.elevated = true;
        action.env = {{QStringLiteral("A"), QStringLiteral("1\\2")},
                      {QStringLiteral("B"), QStringLiteral("quote\"")}};
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Send);
        action.keys = QStringLiteral("^{c}");
        action.delayMs = 120;
        action.releaseModifiers = false;
        out.push_back(action);
    }
    out.push_back(base(Action::Kind::CapsLock));
    {
        Action action = base(Action::Kind::Type);
        action.text = QStringLiteral("Hello\nworld\t\"quoted\"\\tail");
        action.delayMs = 5;
        action.releaseModifiers = true;
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Open);
        action.target = QStringLiteral("C:\\docs");
        action.openArgs = QStringLiteral("--new");
        action.cwd = QStringLiteral("C:\\");
        action.show = ShowMode::Maximized;
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Volume);
        action.volumeOp = VolumeOp::Set;
        action.level = 42;
        action.step = 5;
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Media);
        action.mediaOp = MediaOp::Next;
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Clipboard);
        action.clipboardOp = ClipboardOp::Append;
        action.clipboardText = QStringLiteral("{clipboard}!");
        out.push_back(action);
    }
    out.push_back(windowAction());
    {
        Action action = base(Action::Kind::Notify);
        action.title = QStringLiteral("构建完成");
        action.body = QStringLiteral("42 个文件");
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Desktop);
        action.desktopSwitch = 3;
        out.push_back(action);
    }
    out.push_back(menuAction());
    {
        Action action = base(Action::Kind::Help);
        action.helpTitle = QStringLiteral("快捷键");
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Windows);
        action.windowsTitle = QStringLiteral("窗口");
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Apps);
        action.appsTitle = QStringLiteral("程序");
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Workspace);
        action.workspaceOp = WorkspaceOp::Move;
        action.workspaceIndex = 5;
        action.follow = true;
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Power);
        action.powerOp = PowerOp::ScreenOff;
        out.push_back(action);
    }
    {
        Action action = base(Action::Kind::Suspend);
        action.toggleState = ToggleState::Off;
        out.push_back(action);
    }
    out.push_back(base(Action::Kind::Reload));
    out.push_back(base(Action::Kind::Quit));
    out.push_back(base(Action::Kind::Noop));
    return out;
}

Config sampleConfig()
{
    Config config;
    config.settings.logLevel = QStringLiteral("debug");
    config.settings.swallow = false;
    config.settings.exactModifiers = true;
    config.settings.releaseModifiers = false;
    config.settings.repeatIntervalMs = 33;
    config.settings.repeatDelayMs = 222;
    config.settings.tickMs = 8;
    config.settings.inputBackend = QStringLiteral("ntuser");
    config.settings.singleInstance = false;
    config.settings.elevate = false;
    config.settings.remoteDesktop = true;
    config.settings.remoteDesktopProcesses = {QStringLiteral("mstsc.exe"),
                                              QStringLiteral("my rdp.exe")};
    config.settings.windowMonitor = MonitorRef{MonitorRef::Kind::Last, 1, QString()};
    config.settings.trayBadge = TrayBadgeMode::Workspace;
    // 工作空间：故意把每一格都写成非默认值（默认是开 / 9 个 / 光标 / active / 空名单）。
    config.settings.workspace.enabled = true;
    config.settings.workspace.count = 6;
    config.settings.workspace.switchTarget = WorkspaceSwitchTarget::Focus;
    config.settings.workspace.newWindow = 4;
    config.settings.workspace.exclude = {QStringLiteral("game.exe"), QStringLiteral("obs")};

    // 每种动作一条快捷键：和弦必须互不相同（否则 compile() 会报重复）。
    const std::vector<Action> actions = allActions();
    QStringList chords;
    for (const Action &action : actions) {
        const QChar letter = QChar(u'a' + static_cast<char16_t>(chords.size()));
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = action;
        HotkeyDef def;
        def.name = QStringLiteral("hk-%1").arg(action.summary().left(3));
        def.keys = {QStringLiteral("Ctrl+Alt+") + letter};
        def.action = spec;
        chords.append(def.keys.first());
        config.hotkeys.push_back(std::move(def));
    }

    // 每种「可选形状」各来一条。
    {
        HotkeyDef def;
        def.keys = {QStringLiteral("Ctrl+Alt+1")};
        def.trigger = TriggerMode::Release;
        def.swallow = false;
        config.hotkeys.push_back(def);
    }
    {
        HotkeyDef def;
        def.keys = {QStringLiteral("Ctrl+Alt+2")};
        def.repeat = RepeatSpec::makeFlag(true);
        config.hotkeys.push_back(def);
    }
    {
        HotkeyDef def;
        def.keys = {QStringLiteral("Ctrl+Alt+3")};
        def.repeat = RepeatSpec::makeConfig(30, 300);
        config.hotkeys.push_back(def);
    }
    {
        HotkeyDef def;
        def.keys = {QStringLiteral("Ctrl+Alt+4"), QStringLiteral("Ctrl+Alt+5")};
        def.enabled = false;
        def.comment = QStringLiteral("中文注释 · 带 \"引号\"");
        def.remoteDesktop = true;
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = base(Action::Kind::Quit);
        def.action = spec;
        config.hotkeys.push_back(def);
    }
    {
        // `on_release` + `short` 简写。
        HotkeyDef def;
        def.keys = {QStringLiteral("LWin")};
        def.trigger = TriggerMode::Release;
        ActionSpec press;
        press.kind = ActionSpec::Kind::Short;
        press.shorthand = QStringLiteral("send:^{esc}");
        def.action = press;
        ActionSpec release;
        release.kind = ActionSpec::Kind::One;
        release.action = base(Action::Kind::Noop);
        def.onRelease = release;
        config.hotkeys.push_back(def);
    }

    {
        // 相对工作空间切换（`prev` / `next`）：没有 `index` 字段，写成
        // `{ type = "workspace", op = "prev" }`，读回来必须还是同一条动作。
        HotkeyDef def;
        def.name = QStringLiteral("hk-workspace-prev");
        def.keys = {QStringLiteral("Ctrl+Alt+6")};
        Action action = base(Action::Kind::Workspace);
        action.workspaceOp = WorkspaceOp::Prev;
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = action;
        def.action = spec;
        config.hotkeys.push_back(def);
    }

    {
        // 相对**搬移** + `follow`：写成
        // `{ type = "workspace", op = "move_next", follow = true }`，同样没有 `index`。
        HotkeyDef def;
        def.name = QStringLiteral("hk-workspace-move-next");
        def.keys = {QStringLiteral("Win+i")};
        Action action = base(Action::Kind::Workspace);
        action.workspaceOp = WorkspaceOp::MoveNext;
        action.follow = true;
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = action;
        def.action = spec;
        config.hotkeys.push_back(def);
    }

    {
        RemapDef def;
        def.name = QStringLiteral("caps");
        def.from = QStringLiteral("CapsLock");
        def.to = QStringLiteral("Esc");
        def.swallow = true;
        config.remaps.push_back(def);
    }
    {
        RemapDef def;
        def.from = QStringLiteral("a");
        def.to = QStringLiteral("^{c}");
        def.mode = RemapMode::Tap;
        def.swallow = false;
        def.enabled = false;
        def.remoteDesktop = true;
        config.remaps.push_back(def);
    }

    {
        WindowRuleDef rule;
        rule.name = QStringLiteral("wezterm");
        rule.process = QStringLiteral("wezterm");
        rule.title = QStringLiteral("— wezterm");
        rule.desktop = 2;
        rule.monitor = MonitorRef{MonitorRef::Kind::Device, 1, QStringLiteral("DISPLAY2")};
        rule.maximize = true;
        config.windowRules.push_back(rule);
    }
    {
        WindowRuleDef rule;
        rule.process = QStringLiteral("code");
        rule.monitor = MonitorRef{MonitorRef::Kind::Primary, 1, QString()};
        rule.x = -10;
        rule.y = 40;
        rule.width = 1280;
        rule.height = 720;
        config.windowRules.push_back(rule);
    }
    {
        WindowRuleDef rule;
        rule.title = QStringLiteral("Timer");
        rule.allDesktops = true;
        rule.topmost = true;
        rule.enabled = false;
        config.windowRules.push_back(rule);
    }
    {
        WindowRuleDef rule;
        rule.process = QStringLiteral("spotify");
        rule.monitor = MonitorRef{MonitorRef::Kind::Index, 2, QString()};
        rule.maximize = false;
        config.windowRules.push_back(rule);
    }
    {
        // 只写 `workspace` 的规则（不碰几何，也不碰虚拟桌面）。
        WindowRuleDef rule;
        rule.process = QStringLiteral("alacritty");
        rule.workspace = 3;
        config.windowRules.push_back(rule);
    }

    {
        AppDef app;
        app.name = QStringLiteral("wps");
        app.process = QStringLiteral("wps");
        app.workspace = 2;
        app.window = WindowRuleDef{};
        app.window->desktop = 3;
        app.window->monitor = MonitorRef{MonitorRef::Kind::Cursor, 1, QString()};
        LaunchSpec launch;
        launch.program = QStringLiteral("C:\\wps\\wps.exe");
        launch.args = {QStringLiteral("--open")};
        launch.show = ShowMode::Normal;
        launch.waitMs = 800;
        launch.elevated = true;
        app.launch = launch;

        HotkeyDef hotkey;
        hotkey.keys = {QStringLiteral("Win+3")};
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = windowAction();
        hotkey.action = spec;
        app.hotkeys.push_back(hotkey);
        config.apps.push_back(std::move(app));
    }
    {
        AppDef app;
        app.process = QStringLiteral("foo");
        HotkeyDef hotkey;
        hotkey.keys = {QStringLiteral("Ctrl+Alt+6")};
        ActionSpec spec;
        spec.kind = ActionSpec::Kind::One;
        spec.action = [](QString program) {
            Action action = base(Action::Kind::Run);
            action.program = std::move(program);
            return action;
        }(QStringLiteral("foo.exe"));
        hotkey.action = spec;
        app.hotkeys.push_back(hotkey);
        config.apps.push_back(std::move(app));
    }
    {
        AppDef app;
        app.name = QStringLiteral("disabled");
        app.process = QStringLiteral("bar");
        app.enabled = false;
        config.apps.push_back(app);
    }
    {
        AppDef app;
        app.title = QStringLiteral("Some App");
        config.apps.push_back(app);
    }
    return config;
}

} // namespace

class TestConfigWriter : public QObject
{
    Q_OBJECT

private slots:
    void writesAMarkedLuaFile();
    void roundTripsThroughRealLua();
    void escapesStringsThatNeedIt();
    void backsUpAnExistingFile();
    void writesMonitorIndexAsANumber();
    void writesTrayBadgeAsAString();
    void writesWorkspaceAsANestedTable();
};

void TestConfigWriter::writesAMarkedLuaFile()
{
    const Config config = sampleConfig();
    const QString text = configToLua(config);
    QVERIFY(text.startsWith(QStringLiteral("-- flowkeyd-config-editor:generated")));
    QVERIFY(isEditorGenerated(text));
    // 手写的配置不能被当成界面生成的（否则界面的“会丢注释”那一步确认会被跳过）。
    QVERIFY(!isEditorGenerated(QStringLiteral("settings{ swallow = true }\n")));
    QVERIFY(isEditorGenerated(QStringLiteral("\xEF\xBB\xBF") + text));
}

void TestConfigWriter::roundTripsThroughRealLua()
{
    const Config original = sampleConfig();
    const QString text = configToLua(original);

    // 真的落一次盘再读：`loadConfig()` 走的是与守护进程完全一样的那条路径。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("config.lua"));
    QString error;
    QVERIFY2(writeConfigLuaFile(path, text, &error), qPrintable(error));

    Compiled compiled;
    const auto failure = loadConfig(lua::makeLuaEvaluator(), path, &compiled);
    if (failure.has_value()) {
        QFAIL(qPrintable(failure->toString()));
    }
    // 警告是允许的（示例配置里有一个 `enabled = false` 的 app），但打印出来便于
    // 发现意料之外的那一条。
    for (const QString &warning : compiled.warnings) {
        qInfo().noquote() << "warning:" << warning;
    }

    // 数目形状对上（真正的比较在下面）。
    QCOMPARE(static_cast<int>(compiled.bindings.size()), 29);
    // 禁用掉的那条 remap 不会进编译结果（`compile()` 只把有效的那条交给引擎）。
    QCOMPARE(static_cast<int>(compiled.remaps.size()), 1);
    QCOMPARE(static_cast<int>(compiled.windowRules.size()), 5);

    // 结构与值必须逐字段一致。
    EvalResult evaluated;
    const auto evalFailure = lua::makeLuaEvaluator()(
        text, path, &evaluated);
    QVERIFY(!evalFailure.has_value());
    QVERIFY(evaluated.errors.isEmpty());

    const QVariantMap before = configToMap(original);
    const QVariantMap after = configToMap(evaluated.config);
    if (before != after) {
        // 打印第一处不同，不然 QVariantMap 的比较失败信息里什么都看不出来。
        const QStringList keys = {QStringLiteral("settings"), QStringLiteral("hotkeys"),
                                  QStringLiteral("remaps"), QStringLiteral("window_rules"),
                                  QStringLiteral("apps")};
        for (const QString &key : keys) {
            if (before.value(key) != after.value(key)) {
                qWarning().noquote() << "mismatch in" << key;
                qWarning().noquote() << "  before:" << before.value(key);
                qWarning().noquote() << "  after: " << after.value(key);
            }
        }
    }
    QCOMPARE(before, after);

    // 再写一遍必须逐字节一样（幂等：界面反复保存不会让文件漂移）。
    QCOMPARE(configToLua(evaluated.config), text);
}

void TestConfigWriter::escapesStringsThatNeedIt()
{
    Config config;
    HotkeyDef def;
    def.keys = {QStringLiteral("Ctrl+Alt+z")};
    ActionSpec spec;
    spec.kind = ActionSpec::Kind::One;
    spec.action = base(Action::Kind::Type);
    spec.action.text = QStringLiteral("line1\nline2\ttab \"quoted\" back\\slash \x01 中文");
    def.action = spec;
    config.hotkeys.push_back(def);

    const QString text = configToLua(config);
    // 换行与制表符必须以转义形式出现（否则 Lua 会读到真实的控制字符，还会
    // 把一行拆成两行）。
    QVERIFY(text.contains(QStringLiteral("\\n")));
    QVERIFY(text.contains(QStringLiteral("\\t")));
    QVERIFY(text.contains(QStringLiteral("\\\\")));

    EvalResult evaluated;
    const auto failure = lua::makeLuaEvaluator()(text, QStringLiteral("mem"), &evaluated);
    QVERIFY(!failure.has_value());
    QCOMPARE(configToMap(evaluated.config), configToMap(config));
}

void TestConfigWriter::backsUpAnExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("sub/config.lua"));

    QString error;
    QVERIFY2(writeConfigLuaFile(path, QStringLiteral("-- first\n"), &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak")));
    QVERIFY2(writeConfigLuaFile(path, QStringLiteral("-- second\n"), &error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("-- second\n"));
    file.close();

    QFile backup(path + QStringLiteral(".bak"));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(backup.readAll()), QStringLiteral("-- first\n"));
}

void TestConfigWriter::writesMonitorIndexAsANumber()
{
    // 序号必须是 Lua 数字：写成字符串 `"2"` 会被 Lua 层读成“设备名 2”，
    // 那是完全不同的意思（而且永远不会匹配到任何显示器）。
    Config config;
    config.settings.windowMonitor = MonitorRef{MonitorRef::Kind::Index, 2, QString()};
    WindowRuleDef rule;
    rule.process = QStringLiteral("x");
    rule.monitor = MonitorRef{MonitorRef::Kind::Index, 3, QString()};
    config.windowRules.push_back(rule);

    const QString text = configToLua(config);
    QVERIFY(text.contains(QStringLiteral("window_monitor = 2")));
    QVERIFY(text.contains(QStringLiteral("monitor = 3")));

    EvalResult evaluated;
    const auto failure = lua::makeLuaEvaluator()(text, QStringLiteral("mem"), &evaluated);
    QVERIFY(!failure.has_value());
    QCOMPARE(evaluated.config.settings.windowMonitor.kind, MonitorRef::Kind::Index);
    QCOMPARE(evaluated.config.settings.windowMonitor.index, 2u);
    QCOMPARE(evaluated.config.windowRules.at(0).monitor->index, 3u);
}

void TestConfigWriter::writesTrayBadgeAsAString()
{
    // `settings.tray_badge` 是字符串枚举（`both` / `desktop` / `workspace`），
    // 写回文件时不能被当成布尔或数字。
    Config config;
    config.settings.trayBadge = TrayBadgeMode::Desktop;
    const QString text = configToLua(config);
    QVERIFY(text.contains(QStringLiteral("tray_badge = \"desktop\"")));

    EvalResult evaluated;
    const auto failure = lua::makeLuaEvaluator()(text, QStringLiteral("mem"), &evaluated);
    QVERIFY2(!failure.has_value(), failure.has_value() ? qPrintable(failure->toString()) : "");
    QCOMPARE(evaluated.config.settings.trayBadge, TrayBadgeMode::Desktop);

    // 默认值（`both` = 1/2 的写法）也要写出来：界面保存一次就固定下来，
    // 以后改默认值不会让老文件“漂移”（与其他枚举设置一致）。
    QVERIFY(configToLua(Config()).contains(QStringLiteral("tray_badge = \"both\"")));
}

void TestConfigWriter::writesWorkspaceAsANestedTable()
{    // `settings.workspace` 在配置里是**一张嵌套的表**（不是扁平键），里面每一项
    // 都要能被 Lua 层读回原值：`new_window` 尤其容易写错（`"active"` 是字符串，
    // 空间号是数字）。
    Config config;
    config.settings.workspace.enabled = true;
    config.settings.workspace.count = 4;
    config.settings.workspace.switchTarget = WorkspaceSwitchTarget::Focus;
    config.settings.workspace.switchEffect = WorkspaceSwitchEffect::Freeze;
    config.settings.workspace.newWindow = 3;
    config.settings.workspace.exclude = {QStringLiteral("game.exe")};

    const QString text = configToLua(config);
    QVERIFY(text.contains(QStringLiteral("workspace = {")));
    QVERIFY(text.contains(QStringLiteral("count = 4")));
    QVERIFY(text.contains(QStringLiteral("switch_target = \"focus\"")));
    QVERIFY(text.contains(QStringLiteral("switch_effect = \"freeze\"")));
    QVERIFY(text.contains(QStringLiteral("new_window = 3")));

    EvalResult evaluated;
    const auto failure = lua::makeLuaEvaluator()(text, QStringLiteral("mem"), &evaluated);
    QVERIFY2(!failure.has_value(), failure.has_value() ? qPrintable(failure->toString()) : "");
    QVERIFY(evaluated.config.settings.workspace.enabled);
    QCOMPARE(evaluated.config.settings.workspace.count, 4u);
    QVERIFY(evaluated.config.settings.workspace.switchTarget
            == WorkspaceSwitchTarget::Focus);
    QVERIFY(evaluated.config.settings.workspace.switchEffect
            == WorkspaceSwitchEffect::Freeze);
    QCOMPARE(evaluated.config.settings.workspace.newWindow, 3u);
    QCOMPARE(evaluated.config.settings.workspace.exclude, QStringList{QStringLiteral("game.exe")});

    // 默认值（`"active"`）写成字符串，且排除了空名单。
    Config defaults;
    const QString defaultText = configToLua(defaults);
    QVERIFY(defaultText.contains(QStringLiteral("new_window = \"active\"")));
    // 默认值也要写出来（界面保存一次就固定下来），包括 `auto`。
    QVERIFY(defaultText.contains(QStringLiteral("switch_effect = \"auto\"")));
    QVERIFY(!defaultText.contains(QStringLiteral("exclude")));
    QCOMPARE(configToMap(evaluated.config).value(QStringLiteral("settings")),
             configToMap(config).value(QStringLiteral("settings")));
}

QTEST_MAIN(TestConfigWriter)
#include "tst_config_writer.moc"
