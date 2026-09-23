// 配置结构体的校验与编译（stage 1 不经过 Lua，直接搭 C++ 结构体）。
#include "core/config.h"

#include "helpers.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace flowkeyd::core;
using namespace flowkeyd::test;

class TestConfig : public QObject
{
    Q_OBJECT

private slots:
    void minimalConfig();
    void emptyConfigIsValid();
    void actionListsAndShorthand();
    void summaries();
    void destructiveActionsAreFlagged();
    void unknownActionShorthandIsReported();
    void emptyActionListIsReported();
    void badChordIsReportedWithContext();
    void twoPlainKeysAreNotAChord();
    void disabledHotkeysAreDropped();
    void duplicateChordsWarn();
    void triggerReleaseMovesPressActions();
    void triggerAndRepeatCannotDisagree();
    void repeatUsesSettingsDefaults();
    void menuProblemsAreReportedAtLoadTime();
    void helpTitleMustNotBeEmpty();
    void windowToggleAndAnimateValidation();
    void windowRulesCompile();
    void windowRuleGeometryDefaults();
    void windowRulePinsAndTopmost();
    void windowRuleProblemsAreReported();
    void settingsValidation();
    void evaluationErrorsAreReported();
    void legacyTomlIsReportedInsteadOfParsed();
    void utf8BomIsStrippedBeforeEvaluation();
    void candidatesPreferHomeAndKeepOrder();
};

void TestConfig::minimalConfig()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("Ctrl+Alt+T"),
                                    specOne(runAction(QStringLiteral("wt.exe")))));
    const auto compiled = compileOrDie(config);
    QCOMPARE(compiled->bindings.size(), std::size_t(1));
    QCOMPARE(compiled->bindings.at(0).name, QStringLiteral("Ctrl+Alt+T"));
    QVERIFY(compiled->bindings.at(0).swallow);
    QCOMPARE(compiled->bindings.at(0).press.size(), std::size_t(1));
    QVERIFY(compiled->remaps.empty());
}

void TestConfig::emptyConfigIsValid()
{
    const auto compiled = compileOrDie(Config{});
    QVERIFY(compiled->bindings.empty());
    QCOMPARE(compiled->settings.logLevel, QStringLiteral("info"));
    QCOMPARE(compiled->settings.repeatIntervalMs, 50u);
    QVERIFY(compiled->settings.swallow);
    QVERIFY(compiled->settings.releaseModifiers);
    QVERIFY(compiled->settings.elevate);
}

void TestConfig::actionListsAndShorthand()
{
    Config config;
    config.settings.swallow = true;
    HotkeyDef def;
    def.name = QStringLiteral("combo");
    def.keys = QStringList{QStringLiteral("^!1"), QStringLiteral("^!2")};
    def.swallow = false;
    def.action = specList({specShort(QStringLiteral("send:^{c}")),
                           specOne(noneAction()),
                           specOne(runAction(QStringLiteral("cmd.exe"),
                                             QStringList{QStringLiteral("/c"),
                                                         QStringLiteral("echo hi")}))});
    def.onRelease = specOne(sendAction(QStringLiteral("{Esc}")));
    def.repeat = RepeatSpec::makeConfig(25, 100);
    config.hotkeys.push_back(def);

    const auto compiled = compileOrDie(config);
    const Binding &binding = compiled->bindings.at(0);
    QCOMPARE(binding.chords.size(), std::size_t(2));
    QCOMPARE(binding.chords.at(0).mods, Modifiers::Ctrl.unioned(Modifiers::Alt));
    QCOMPARE(binding.press.size(), std::size_t(3));
    QCOMPARE(binding.release.size(), std::size_t(1));
    QVERIFY(binding.repeat.has_value());
    QCOMPARE(binding.repeat->intervalMs, 25u);
    QVERIFY(!binding.swallow);
}

void TestConfig::summaries()
{
    QCOMPARE(runAction(QStringLiteral("notepad.exe")).summary(), QStringLiteral("run notepad.exe"));
    QCOMPARE(runAction(QStringLiteral("cmd.exe"), QStringList{QStringLiteral("/c"), QStringLiteral("x")})
                 .summary(),
             QStringLiteral("run cmd.exe /c x"));
    QCOMPARE(sendAction(QStringLiteral("^{c}")).summary(), QStringLiteral("send ^{c}"));

    Action capsLock = actionOf(Action::Kind::CapsLock);
    QCOMPARE(capsLock.summary(), QStringLiteral("caps lock Off"));

    QCOMPARE(powerAction(PowerOp::Hibernate).summary(), QStringLiteral("power hibernate"));
    QCOMPARE(powerAction(PowerOp::ScreenOff).summary(), QStringLiteral("power screen_off"));
    QCOMPARE(suspendAction().summary(), QStringLiteral("suspend Toggle"));
    QCOMPARE(noneAction().summary(), QStringLiteral("none"));
    QCOMPARE(reloadAction().summary(), QStringLiteral("reload"));

    Action help = actionOf(Action::Kind::Help);
    QCOMPARE(help.summary(), QStringLiteral("help"));
    help.helpTitle = QStringLiteral("快捷键");
    QCOMPARE(help.summary(), QStringLiteral("help \"快捷键\""));

    Action menu = actionOf(Action::Kind::Menu);
    menu.items.push_back(menuItem(QStringLiteral("a")));
    QCOMPARE(menu.summary(), QStringLiteral("menu (1 item(s))"));
    menu.menuTitle = QStringLiteral("电源");
    menu.items.push_back(menuItem(QStringLiteral("b")));
    QCOMPARE(menu.summary(), QStringLiteral("menu \"电源\" (2 item(s))"));

    Action window = windowAction(WindowOp::Activate);
    QCOMPARE(window.summary(), QStringLiteral("window Activate foreground"));
    window.process = QStringLiteral("wezterm");
    window.toggle = false;
    QCOMPARE(window.summary(),
             QStringLiteral("window Activate belonging to the process \"wezterm\" (no toggle)"));

    Action volume = actionOf(Action::Kind::Volume);
    volume.volumeOp = VolumeOp::Set;
    volume.level = 40;
    QCOMPARE(volume.summary(), QStringLiteral("volume Set 40"));

    Action media = actionOf(Action::Kind::Media);
    media.mediaOp = MediaOp::PlayPause;
    QCOMPARE(media.summary(), QStringLiteral("media PlayPause"));

    Action clipboard = actionOf(Action::Kind::Clipboard);
    clipboard.clipboardOp = ClipboardOp::Get;
    QCOMPARE(clipboard.summary(), QStringLiteral("clipboard Get"));

    Action desktop = actionOf(Action::Kind::Desktop);
    desktop.desktopSwitch = 3;
    QCOMPARE(desktop.summary(), QStringLiteral("desktop 3"));

    Action notify = actionOf(Action::Kind::Notify);
    notify.title = QStringLiteral("hi");
    QCOMPARE(notify.summary(), QStringLiteral("notify \"hi\""));
}

void TestConfig::destructiveActionsAreFlagged()
{
    // `quit` / `suspend` / `power` 是「不该误触」的三类（帮助窗口里要对它们
    // 再确认一次）。
    QCOMPARE(isDestructive(quitAction()), true);
    QCOMPARE(isDestructive(suspendAction()), true);
    QCOMPARE(isDestructive(powerAction(PowerOp::Sleep)), true);
    QCOMPARE(isDestructive(powerAction(PowerOp::ScreenOff)), true);

    // 其余动作都不算：`reload` 顶多打断一下手头的事；`window("close")` 也是。
    QCOMPARE(isDestructive(noneAction()), false);
    QCOMPARE(isDestructive(reloadAction()), false);
    QCOMPARE(isDestructive(runAction(QStringLiteral("notepad.exe"))), false);
    QCOMPARE(isDestructive(sendAction(QStringLiteral("^{c}"))), false);
    QCOMPARE(isDestructive(windowAction(WindowOp::Close)), false);
    QCOMPARE(isDestructive(actionOf(Action::Kind::Help)), false);

    // **`menu` 不算危险**：它只是把选单弹出来，真正的危险条目在选单里还有一次
    // 选择（用户的 `Win+X` 电源选单因此可以放心地从帮助窗口打开）。
    Action menu = actionOf(Action::Kind::Menu);
    menu.items.push_back(menuItem(QStringLiteral("sleep")));
    menu.items.back().action = std::make_shared<ActionSpec>(specOne(powerAction(PowerOp::Sleep)));
    QCOMPARE(isDestructive(menu), false);

    // 整条列表：只要有一个危险动作就算危险。
    QCOMPARE(isDestructive(std::vector<Action>{}), false);
    QCOMPARE(isDestructive(std::vector<Action>{noneAction(), reloadAction()}), false);
    QCOMPARE(isDestructive(std::vector<Action>{sendAction(QStringLiteral("^{c}")), quitAction()}),
             true);
}

void TestConfig::unknownActionShorthandIsReported()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"),
                                    specShort(QStringLiteral("frobnicate:now"))));
    const auto error = compileConfig(config);
    QVERIFY(error.has_value());
    const QString text = error->toString();
    QVERIFY2(text.contains(QStringLiteral("frobnicate")), qPrintable(text));
}

void TestConfig::emptyActionListIsReported()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specList({})));
    const auto error = compileConfig(config);
    QVERIFY(error.has_value());
    const QString text = error->toString();
    QVERIFY2(text.contains(QStringLiteral("an empty action list does nothing")), qPrintable(text));
}

void TestConfig::badChordIsReportedWithContext()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("weird"),
                                         QStringLiteral("Ctrl+Nope"),
                                         specOne(noneAction())));
    const auto error = compileConfig(config);
    QVERIFY(error.has_value());
    const QString text = error->toString();
    QVERIFY2(text.contains(QStringLiteral("weird")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Nope")), qPrintable(text));
}

void TestConfig::twoPlainKeysAreNotAChord()
{
    // 用户真实的笔误：想让小键盘的 `-` 与 `+` 同时按住做静音。
    // 和弦语法里只能有修饰键 + 一个按键，所以这必须在加载时就被拒绝，
    // 而不是静默地“永不触发”。
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("numpad-mute"),
                                         QStringLiteral("NumpadSub+NumpadAdd"),
                                         specOne(noneAction())));
    const auto error = compileConfig(config);
    QVERIFY(error.has_value());
    const QString text = error->toString();
    QVERIFY2(text.contains(QStringLiteral("NumpadSub")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("is not a modifier")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("no usable keys")), qPrintable(text));
}

void TestConfig::disabledHotkeysAreDropped()
{
    Config config;
    HotkeyDef def = hotkey(QStringLiteral("F1"), specOne(noneAction()));
    def.enabled = false;
    config.hotkeys.push_back(def);
    const auto compiled = compileOrDie(config);
    QVERIFY(compiled->bindings.empty());
    QVERIFY(std::any_of(compiled->warnings.begin(), compiled->warnings.end(), [](const QString &w) {
        return w.contains(QStringLiteral("disabled"));
    }));
}

void TestConfig::duplicateChordsWarn()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    const auto compiled = compileOrDie(config);
    QCOMPARE(compiled->bindings.size(), std::size_t(2));
    QVERIFY(std::any_of(compiled->warnings.begin(), compiled->warnings.end(), [](const QString &w) {
        return w.contains(QStringLiteral("F1"));
    }));
}

void TestConfig::triggerReleaseMovesPressActions()
{
    Config config;
    HotkeyDef def = hotkeyNamed(QStringLiteral("ptt"),
                                QStringLiteral("F2"),
                                specOne(sendAction(QStringLiteral("a"))));
    def.trigger = TriggerMode::Release;
    def.onRelease = specOne(sendAction(QStringLiteral("b")));
    config.hotkeys.push_back(def);

    const auto compiled = compileOrDie(config);
    const Binding &binding = compiled->bindings.at(0);
    QCOMPARE(binding.trigger, TriggerMode::Release);
    QVERIFY(binding.press.empty());
    QCOMPARE(binding.release.size(), std::size_t(2));
    QCOMPARE(binding.release.at(0).summary(), QStringLiteral("send a"));
    QCOMPARE(binding.release.at(1).summary(), QStringLiteral("send b"));
    QVERIFY(std::any_of(compiled->warnings.begin(), compiled->warnings.end(), [](const QString &w) {
        return w.contains(QStringLiteral("release"));
    }));
}

void TestConfig::triggerAndRepeatCannotDisagree()
{
    Config config;
    HotkeyDef def = hotkeyNamed(QStringLiteral("confused"),
                                QStringLiteral("F1"),
                                specOne(sendAction(QStringLiteral("a"))));
    def.trigger = TriggerMode::Press;
    def.repeat = RepeatSpec::makeFlag(true);
    config.hotkeys.push_back(def);

    const auto error = compileConfig(config);
    QVERIFY(error.has_value());
    const QString text = error->toString();
    QVERIFY2(text.contains(QStringLiteral("confused")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("repeat")), qPrintable(text));

    // 两种写法指向同一件事时是合法的。
    Config agreeing;
    HotkeyDef same = hotkey(QStringLiteral("F1"), specOne(sendAction(QStringLiteral("a"))));
    same.trigger = TriggerMode::Repeat;
    same.repeat = RepeatSpec::makeConfig(25, std::nullopt);
    agreeing.hotkeys.push_back(same);
    const auto compiled = compileOrDie(agreeing);
    QVERIFY(compiled->bindings.at(0).repeat.has_value());
    QCOMPARE(compiled->bindings.at(0).repeat->intervalMs, 25u);
}

void TestConfig::repeatUsesSettingsDefaults()
{
    Config config;
    HotkeyDef def = hotkey(QStringLiteral("F1"), specOne(sendAction(QStringLiteral("a"))));
    def.repeat = RepeatSpec::makeFlag(true);
    config.hotkeys.push_back(def);

    const auto compiled = compileOrDie(config);
    QCOMPARE(compiled->bindings.at(0).trigger, TriggerMode::Repeat);
    QCOMPARE(compiled->bindings.at(0).repeat->intervalMs, 50u);
    QCOMPARE(compiled->bindings.at(0).repeat->delayMs, 400u);
}

void TestConfig::menuProblemsAreReportedAtLoadTime()
{
    Action menu = actionOf(Action::Kind::Menu);

    // 一个条目都没有的选单什么也做不了。
    Config empty;
    empty.hotkeys.push_back(hotkeyNamed(QStringLiteral("empty"), QStringLiteral("F1"), specOne(menu)));
    auto error = compileConfig(empty);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("at least one item")),
             qPrintable(error->toString()));

    // `label` 是必填的。
    Action missingLabel = actionOf(Action::Kind::Menu);
    missingLabel.items.push_back(menuItem(QString(), QStringLiteral("s")));
    Config bad;
    bad.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(missingLabel)));
    error = compileConfig(bad);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("label")), qPrintable(error->toString()));

    // 按键只能是一个字符，而且不能重复。
    Action longKey = actionOf(Action::Kind::Menu);
    longKey.items.push_back(menuItem(QStringLiteral("a"), QStringLiteral("esc")));
    Config longKeyConfig;
    longKeyConfig.hotkeys.push_back(hotkeyNamed(QStringLiteral("k"), QStringLiteral("F1"),
                                                specOne(longKey)));
    error = compileConfig(longKeyConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("single ASCII letter")),
             qPrintable(error->toString()));

    Action duplicate = actionOf(Action::Kind::Menu);
    duplicate.items.push_back(menuItem(QStringLiteral("a"), QStringLiteral("s")));
    duplicate.items.push_back(menuItem(QStringLiteral("b"), QStringLiteral("S")));
    Config duplicateConfig;
    duplicateConfig.hotkeys.push_back(hotkeyNamed(QStringLiteral("dup"), QStringLiteral("F1"),
                                                  specOne(duplicate)));
    error = compileConfig(duplicateConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("already used")),
             qPrintable(error->toString()));

    // 嵌套的选单。
    Action nested = actionOf(Action::Kind::Menu);
    MenuItemDef outer = menuItem(QStringLiteral("a"));
    outer.action = std::make_shared<ActionSpec>(specOne(nested));
    Action parent = actionOf(Action::Kind::Menu);
    parent.items.push_back(outer);
    Config nestedConfig;
    nestedConfig.hotkeys.push_back(hotkeyNamed(QStringLiteral("nested"), QStringLiteral("F1"),
                                               specOne(parent)));
    error = compileConfig(nestedConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("menus cannot be nested")),
             qPrintable(error->toString()));

    // 条目里的坏动作按“哪个条目”报出来。
    Action badItem = actionOf(Action::Kind::Menu);
    badItem.items.push_back(menuItem(QStringLiteral("a")));
    MenuItemDef second = menuItem(QStringLiteral("b"));
    second.action = std::make_shared<ActionSpec>(specOne(sendAction(QStringLiteral("{Nope}"))));
    badItem.items.push_back(second);
    Config badItemConfig;
    badItemConfig.hotkeys.push_back(hotkeyNamed(QStringLiteral("bad"), QStringLiteral("F1"),
                                                specOne(badItem)));
    error = compileConfig(badItemConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("menu item #2")), qPrintable(error->toString()));
    QVERIFY2(error->toString().contains(QStringLiteral("Nope")), qPrintable(error->toString()));

    // 正常情况：大小写不敏感，key 统一按小写存。
    Action ok = actionOf(Action::Kind::Menu);
    ok.items.push_back(menuItem(QStringLiteral("睡眠"), QStringLiteral("S")));
    ok.items.push_back(menuItem(QStringLiteral("取消")));
    Config okConfig;
    okConfig.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(ok)));
    const auto compiled = compileOrDie(okConfig);
    const Action &compiledMenu = compiled->bindings.at(0).press.at(0);
    QCOMPARE(compiledMenu.items.at(0).keyChar(), std::optional<QChar>(QChar(u's')));
    QCOMPARE(compiledMenu.items.at(1).keyChar(), std::optional<QChar>());
}

void TestConfig::helpTitleMustNotBeEmpty()
{
    Action help = actionOf(Action::Kind::Help);
    help.helpTitle = QStringLiteral("  ");
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("blank"), QStringLiteral("F1"), specOne(help)));
    const auto error = compileConfig(config);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("title must not be empty")),
             qPrintable(error->toString()));
}

void TestConfig::windowToggleAndAnimateValidation()
{
    // `toggle` 只对 activate 有意义。
    Action minimize = windowAction(WindowOp::Minimize);
    minimize.toggle = true;
    Config toggleConfig;
    toggleConfig.hotkeys.push_back(hotkeyNamed(QStringLiteral("shrink"), QStringLiteral("F1"),
                                               specOne(minimize)));
    auto error = compileConfig(toggleConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("toggle")), qPrintable(error->toString()));

    // `animate` 在不产生过渡的 op 上没有意义。
    Action close = windowAction(WindowOp::Close);
    close.animate = true;
    Config animateConfig;
    animateConfig.hotkeys.push_back(hotkeyNamed(QStringLiteral("bye"), QStringLiteral("F1"),
                                                specOne(close)));
    error = compileConfig(animateConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("animate")), qPrintable(error->toString()));

    // `launch` 需要 target 或 process。
    Action launch = windowAction(WindowOp::Activate);
    LaunchSpec spec;
    spec.program = QStringLiteral("wezterm.exe");
    launch.launch = spec;
    Config launchConfig;
    launchConfig.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(launch)));
    error = compileConfig(launchConfig);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("`launch` needs")),
             qPrintable(error->toString()));
}

void TestConfig::windowRulesCompile()
{
    Config config;
    WindowRuleDef def;
    def.name = QStringLiteral("wezterm");
    def.process = QStringLiteral("wezterm");
    def.desktop = 2;
    MonitorRef ref;
    ref.kind = MonitorRef::Kind::Index;
    ref.index = 2;
    def.monitor = ref;
    config.windowRules.push_back(def);

    const auto compiled = compileOrDie(config);
    QCOMPARE(compiled->windowRules.size(), std::size_t(1));
    const WindowRule &rule = compiled->windowRules.at(0);
    QCOMPARE(rule.name, QStringLiteral("wezterm"));
    QCOMPARE(rule.desktop, std::optional<std::uint32_t>(2));
    QVERIFY(rule.applyGeometry);
    QVERIFY(rule.maximize);
    QVERIFY(compiled->warnings.isEmpty());
}

void TestConfig::windowRuleGeometryDefaults()
{
    // 只写 desktop：不碰窗口几何（否则“挪到另一个桌面”会顺手把窗口最大化）。
    Config desktopOnly;
    WindowRuleDef onlyDesktop;
    onlyDesktop.process = QStringLiteral("chrome");
    onlyDesktop.desktop = 3;
    desktopOnly.windowRules.push_back(onlyDesktop);
    auto compiled = compileOrDie(desktopOnly);
    QVERIFY(!compiled->windowRules.at(0).applyGeometry);
    QVERIFY(!compiled->windowRules.at(0).maximize);

    // 只写 monitor：默认最大化。
    Config monitorOnly;
    WindowRuleDef onlyMonitor;
    onlyMonitor.process = QStringLiteral("code");
    onlyMonitor.monitor = MonitorRef{};
    monitorOnly.windowRules.push_back(onlyMonitor);
    compiled = compileOrDie(monitorOnly);
    QVERIFY(compiled->windowRules.at(0).applyGeometry);
    QVERIFY(compiled->windowRules.at(0).maximize);

    // monitor + 大小：不再最大化，按给定大小摆放。
    Config sized;
    WindowRuleDef sizedRule;
    sizedRule.process = QStringLiteral("code");
    sizedRule.monitor = MonitorRef{};
    sizedRule.width = 1280;
    sizedRule.height = 800;
    sized.windowRules.push_back(sizedRule);
    compiled = compileOrDie(sized);
    QVERIFY(compiled->windowRules.at(0).applyGeometry);
    QVERIFY(!compiled->windowRules.at(0).maximize);
    QCOMPARE(compiled->windowRules.at(0).width, std::optional<std::uint32_t>(1280));
}

namespace {

/// 编译一条 window_rule，返回错误渲染文本（成功时为空串）。
QString windowRuleError(const WindowRuleDef &def)
{
    Config config;
    config.windowRules.push_back(def);
    const auto error = compileConfig(config);
    return error.has_value() ? error->toString() : QString();
}

} // namespace

void TestConfig::windowRulePinsAndTopmost()
{
    Config config;
    WindowRuleDef def;
    def.process = QStringLiteral("wezterm");
    def.allDesktops = true;
    def.topmost = true;
    config.windowRules.push_back(def);

    const auto compiled = compileOrDie(config);
    QCOMPARE(compiled->windowRules.size(), std::size_t(1));
    const WindowRule &rule = compiled->windowRules.at(0);
    QCOMPARE(rule.allDesktops, std::optional<bool>(true));
    QCOMPARE(rule.topmost, std::optional<bool>(true));
    // 钉桌面 / 置顶都不是几何，所以不该顺手把窗口最大化。
    QVERIFY(!rule.applyGeometry);
    QVERIFY(!rule.maximize);
    QVERIFY(compiled->warnings.isEmpty());
}

void TestConfig::windowRuleProblemsAreReported()
{
    WindowRuleDef noMatch;
    noMatch.desktop = 2;
    QVERIFY2(windowRuleError(noMatch).contains(QStringLiteral("needs `process` or `title`")),
             qPrintable(windowRuleError(noMatch)));

    WindowRuleDef conflict;
    conflict.process = QStringLiteral("code");
    conflict.monitor = MonitorRef{};
    conflict.maximize = true;
    conflict.width = 1280;
    QVERIFY2(windowRuleError(conflict).contains(QStringLiteral("maximize = true")),
             qPrintable(windowRuleError(conflict)));

    WindowRuleDef zeroDesktop;
    zeroDesktop.process = QStringLiteral("code");
    zeroDesktop.desktop = 0;
    QVERIFY2(windowRuleError(zeroDesktop).contains(QStringLiteral("`desktop` must be 1")),
             qPrintable(windowRuleError(zeroDesktop)));

    // 钉在所有桌面与“挪到第 N 个桌面”是矛盾的。
    WindowRuleDef pinnedAndMoved;
    pinnedAndMoved.process = QStringLiteral("wezterm");
    pinnedAndMoved.allDesktops = true;
    pinnedAndMoved.desktop = 2;
    QVERIFY2(windowRuleError(pinnedAndMoved).contains(QStringLiteral("all_desktops = true")),
             qPrintable(windowRuleError(pinnedAndMoved)));

    WindowRuleDef zeroMonitor;
    zeroMonitor.process = QStringLiteral("code");
    MonitorRef zero;
    zero.index = 0;
    zeroMonitor.monitor = zero;
    QVERIFY2(windowRuleError(zeroMonitor).contains(QStringLiteral("`monitor` must be 1")),
             qPrintable(windowRuleError(zeroMonitor)));

    WindowRuleDef zeroSize;
    zeroSize.process = QStringLiteral("code");
    zeroSize.monitor = MonitorRef{};
    zeroSize.width = 0;
    QVERIFY2(windowRuleError(zeroSize).contains(QStringLiteral("greater than 0")),
             qPrintable(windowRuleError(zeroSize)));

    // 两条规则匹配同一批窗口：先写的赢，但要有 warning。
    Config duplicates;
    WindowRuleDef first;
    first.process = QStringLiteral("code");
    first.desktop = 1;
    WindowRuleDef second;
    second.process = QStringLiteral("code");
    second.desktop = 2;
    duplicates.windowRules.push_back(first);
    duplicates.windowRules.push_back(second);
    const auto compiled = compileOrDie(duplicates);
    QCOMPARE(compiled->windowRules.size(), std::size_t(2));
    QVERIFY(std::any_of(compiled->warnings.begin(), compiled->warnings.end(),
                        [](const QString &warning) {
                            return warning.contains(QStringLiteral("first"));
                        }));
}

void TestConfig::settingsValidation()
{
    Config config;
    config.settings.tickMs = 0;
    auto error = compileConfig(config);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("tick_ms")), qPrintable(error->toString()));

    Config logLevel;
    logLevel.settings.logLevel = QStringLiteral("chatty");
    error = compileConfig(logLevel);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("log_level")), qPrintable(error->toString()));

    Config backend;
    backend.settings.inputBackend = QStringLiteral("magic");
    error = compileConfig(backend);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("input_backend")), qPrintable(error->toString()));
}

void TestConfig::evaluationErrorsAreReported()
{
    // 求值阶段攒下的条目级错误要和校验错误一起报出来。
    Config config;
    Compiled out;
    const auto error = compile(config,
                               QStringLiteral("test.lua"),
                               QStringList{QStringLiteral("hotkey #1 (`x`): boom")},
                               {},
                               &out);
    QVERIFY(error.has_value());
    QVERIFY2(error->toString().contains(QStringLiteral("boom")), qPrintable(error->toString()));
}

void TestConfig::legacyTomlIsReportedInsteadOfParsed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.toml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[settings]\nswallow = true\n");
    file.close();

    bool called = false;
    const auto evaluator = [&called](const QString &, const QString &, EvalResult *) {
        called = true;
        return std::optional<ConfigError>();
    };
    Compiled out;
    const auto error = loadConfig(evaluator, path, &out);
    QVERIFY(error.has_value());
    QVERIFY(error->kind == ConfigError::Kind::LegacyToml);
    const QString text = error->toString();
    QVERIFY2(text.contains(QStringLiteral("old TOML config")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("port it to")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("config.lua")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("syntax error")), qPrintable(text));
    QVERIFY2(!called, "TOML 文件绝不能被喂给 Lua");

    // 大小写不敏感，且只认后缀。
    QVERIFY(isToml(QStringLiteral("C:\\x\\flowkeyd.TOML")));
    QVERIFY(!isToml(QStringLiteral("config.lua")));
    QVERIFY(!isToml(QStringLiteral("flowkeyd.toml.example")));
}

void TestConfig::utf8BomIsStrippedBeforeEvaluation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.lua"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\xEF\xBB\xBFhotkey{ keys = \"F1\" }\n");
    file.close();

    QString seen;
    const auto evaluator = [&seen](const QString &text, const QString &, EvalResult *out) {
        seen = text;
        out->config = Config{};
        return std::optional<ConfigError>();
    };
    Compiled out;
    const auto error = loadConfig(evaluator, path, &out);
    QVERIFY(!error.has_value());
    QVERIFY(!seen.isEmpty());
    QVERIFY2(seen.at(0) != QChar(0xFEFF), "BOM 必须被剥掉，否则 Lua 会在第 1 行报错");
    QVERIFY(seen.startsWith(QStringLiteral("hotkey{")));
}

void TestConfig::candidatesPreferHomeAndKeepOrder()
{
    const QString preferred = QStringLiteral("C:\\home\\me\\.config\\flowkeyd\\config.lua");
    const QStringList list = candidateList(preferred,
                                           QStringLiteral("C:\\tools"),
                                           QStringLiteral("C:\\Users\\me\\AppData\\Roaming"));
    QCOMPARE(list.first(), preferred);
    QVERIFY(list.contains(QStringLiteral("C:\\tools\\config.lua")));
    QVERIFY(list.contains(QStringLiteral("C:\\Users\\me\\AppData\\Roaming\\flowkeyd\\config.lua")));
    QCOMPARE(list.last(), QStringLiteral("config.lua"));

    // 去重：主目录恰好就是 exe 目录时，同一个路径只出现一次。
    const QStringList deduped = candidateList(QStringLiteral("C:\\tools\\config.lua"),
                                              QStringLiteral("C:\\tools"),
                                              std::nullopt);
    int count = 0;
    for (const QString &path : deduped) {
        if (path == QStringLiteral("C:\\tools\\config.lua")) {
            ++count;
        }
    }
    QCOMPARE(count, 1);
    QCOMPARE(deduped.size(), 2);

    // 都不存在时，报错指向首选位置。
    QCOMPARE(pickConfigPath(QStringList{preferred, QStringLiteral("C:\\nope\\config.lua")}), preferred);

    // 配置与日志共用一个目录。
    QCOMPARE(QFileInfo(preferredConfigPath()).path(), QFileInfo(preferredLogPath()).path());
    QCOMPARE(QFileInfo(preferredConfigPath()).fileName(), QStringLiteral("config.lua"));
    QCOMPARE(QFileInfo(preferredLogPath()).fileName(), QStringLiteral("flowkeyd.log"));
}

QTEST_MAIN(TestConfig)
#include "tst_config.moc"
