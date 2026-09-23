// 快捷键状态机：匹配、吞键、自动重复抑制、长按重复、挂起、重映射、菜单遮断。
#include "core/engine.h"

#include "helpers.h"

#include <QtTest>

using namespace flowkeyd::core;
using namespace flowkeyd::test;

namespace {

constexpr Vk CTRL = vk::LCONTROL;
constexpr Vk ALT = vk::LMENU;

Engine engineOf(const Config &config)
{
    return Engine(compileOrDie(config));
}

Reaction press(Engine *engine, const std::vector<Vk> &keys)
{
    Reaction last;
    for (const Vk key : keys) {
        last = engine->onKey(KeyEvent::keyDown(key), 0);
    }
    return last;
}

Reaction release(Engine *engine, const std::vector<Vk> &keys)
{
    Reaction last;
    for (const Vk key : keys) {
        last = engine->onKey(KeyEvent::keyUp(key), 0);
    }
    return last;
}

Trigger onPress(std::size_t index)
{
    return Trigger{index, Phase::Press};
}

Trigger onRelease(std::size_t index)
{
    return Trigger{index, Phase::Release};
}

} // namespace

class TestEngine : public QObject
{
    Q_OBJECT

private slots:
    void firesOnChordAndSwallows();
    void windowsChordsKeepTheShellFromOpeningStart();
    void theMenuMaskSurvivesAWindowsKeyAutoRepeat();
    void theMenuMaskWaitsForTheLastMenuKey();
    void aSwallowedAltChordMasksTheAltKeyup();
    void passthroughAndNoSwallowOptions();
    void blockingHotkeyWithoutActions();
    void explicitNoneActionStillCountsAsAnAction();
    void onReleaseFires();
    void mostSpecificChordWins();
    void winShiftChordWinsOverPlainWinChord();
    void extraModifiersAreToleratedButWildcardPrefersExact();
    void autoRepeatIsNotRematched();
    void injectedEventsAreIgnored();
    void longPressRepeat();
    void pressFiresOnceEvenWhileTheKeyIsHeld();
    void triggerRepeatUsesTheSettingsDefaults();
    void triggerReleaseFiresOnKeyUp();
    void remapHoldSemantics();
    void remapTapSemantics();
    void remapNestedChord();
    void hotkeysWinOverRemapsWithTheSameChord();
    void suspendDisablesEverythingButTheSuspendHotkey();
    void suspendReleasesKeysHeldByARemap();
    void reloadReleasesKeysHeldByARemap();
    void multipleKeysShareOneHotkey();
    void keyUpWithoutKeyDownIsHarmless();
    void modifierOnlyChordsDoNotFireTheirModifier();
    void modifierOnlyChordsWorkWithExactModifiers();
};

void TestEngine::firesOnChordAndSwallows()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("term"),
                                         QStringLiteral("Ctrl+Alt+t"),
                                         specOne(runAction(QStringLiteral("wt.exe")))));
    Engine engine = engineOf(config);

    // Ctrl 单独按下时不能触发。
    QVERIFY(press(&engine, {CTRL}).isEmpty());
    QVERIFY(press(&engine, {ALT}).isEmpty());
    const Reaction fired = press(&engine, {static_cast<Vk>(u'T')});
    QVERIFY(fired.swallow);
    QCOMPARE(fired.triggers, (std::vector<Trigger>{onPress(0)}));

    // 按键松开同样被吞掉，因此应用程序永远看不到一个孤立的 key-up。
    const Reaction up = release(&engine, {static_cast<Vk>(u'T')});
    QVERIFY(up.swallow);
    QVERIFY(up.triggers.empty());
    // 修饰键的 key-up 原样传递。
    QVERIFY(release(&engine, {ALT, CTRL}).isEmpty());
}

void TestEngine::windowsChordsKeepTheShellFromOpeningStart()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("wezterm"),
                                         QStringLiteral("Win+s"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    QVERIFY(press(&engine, {vk::LWIN}).isEmpty());
    const Reaction fired = press(&engine, {static_cast<Vk>(u'S')});
    QVERIFY(fired.swallow);
    // 此时还没有注入任何东西：外壳是在 Windows 键*松开*时做决定，
    // 而在按下时就遮断会被 Win 键的自动重复抹掉。
    QVERIFY2(fired.inject.empty(), "the mask belongs to the Win keyup");
    // 但动作不等修饰键松开：默认 `trigger = "press"` 在按下时就派发。
    QCOMPARE(fired.triggers, (std::vector<Trigger>{onPress(0)}));
    QVERIFY(release(&engine, {static_cast<Vk>(u'S')}).inject.empty());

    // 未分配的标记键与 Win 键的 key-up 一起发出，
    // 因此它是外壳在 Windows 键松开前看到的最后一件事。
    const Reaction winUp = engine.onKey(KeyEvent::keyUp(vk::LWIN), 0);
    QVERIFY2(!winUp.swallow, "modifier key-ups are never swallowed");
    QCOMPARE(winUp.inject,
             (std::vector<SendOp>{SendOp::keyDown(vk::UNASSIGNED), SendOp::keyUp(vk::UNASSIGNED)}));
    QVERIFY2(winUp.triggers.empty(), "the action was already dispatched on press");

    // 只遮断一次；而且被吞掉的按键并不需要自己是个和弦：
    // 这里 `S` 单独绑定，而用户恰好按着 Win。
    Config plain;
    plain.hotkeys.push_back(hotkeyNamed(QStringLiteral("plain"),
                                        QStringLiteral("s"),
                                        specOne(noneAction())));
    Engine plainEngine = engineOf(plain);
    press(&plainEngine, {vk::LWIN});
    QVERIFY(press(&plainEngine, {static_cast<Vk>(u'S')}).inject.empty());
    plainEngine.onKey(KeyEvent::keyUp(static_cast<Vk>(u'S')), 0);
    QCOMPARE(plainEngine.onKey(KeyEvent::keyUp(vk::LWIN), 0).inject.size(), std::size_t(2));
    // 第二次 Windows 键松开是干净的：遮断标记已经用掉了。
    press(&plainEngine, {vk::RWIN});
    QVERIFY(plainEngine.onKey(KeyEvent::keyUp(vk::RWIN), 0).inject.empty());

    // ……除此之外什么也不注入，因此普通的 `~Win+s` 不受影响。
    Config passthrough;
    passthrough.hotkeys.push_back(hotkeyNamed(QStringLiteral("wezterm"),
                                              QStringLiteral("~Win+s"),
                                              specOne(noneAction())));
    Engine passthroughEngine = engineOf(passthrough);
    press(&passthroughEngine, {vk::LWIN});
    const Reaction passthroughFired = press(&passthroughEngine, {static_cast<Vk>(u'S')});
    QVERIFY(!passthroughFired.swallow);
    QVERIFY(passthroughFired.inject.empty());
    QVERIFY(passthroughEngine.onKey(KeyEvent::keyUp(vk::LWIN), 0).inject.empty());

    // 不使用 Windows 键的和弦不得注入任何东西。
    Engine plainAgain = engineOf(plain);
    QVERIFY(press(&plainAgain, {static_cast<Vk>(u'S')}).inject.empty());
}

void TestEngine::theMenuMaskSurvivesAWindowsKeyAutoRepeat()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("wezterm"),
                                         QStringLiteral("Win+s"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    press(&engine, {vk::LWIN});
    QVERIFY(press(&engine, {static_cast<Vk>(u'S')}).swallow);
    engine.onKey(KeyEvent::keyUp(static_cast<Vk>(u'S')), 0);
    // 真实键盘会在 Windows 键成为最后一个物理按下的键时重复它；
    // 每次重复都会重新武装外壳的开始菜单。
    QVERIFY(engine.onKey(KeyEvent::keyDown(vk::LWIN), 0).inject.empty());
    QCOMPARE(engine.onKey(KeyEvent::keyUp(vk::LWIN), 0).inject.size(), std::size_t(2));
}

void TestEngine::theMenuMaskWaitsForTheLastMenuKey()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("wezterm"),
                                         QStringLiteral("Win+Alt+s"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    press(&engine, {vk::LWIN, ALT});
    QVERIFY(press(&engine, {static_cast<Vk>(u'S')}).swallow);
    engine.onKey(KeyEvent::keyUp(static_cast<Vk>(u'S')), 0);
    // Alt 还按着，所以遮断还没到期。
    QVERIFY(engine.onKey(KeyEvent::keyUp(vk::LWIN), 0).inject.empty());
    QCOMPARE(engine.onKey(KeyEvent::keyUp(ALT), 0).inject.size(), std::size_t(2));
}

void TestEngine::aSwallowedAltChordMasksTheAltKeyup()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("menu"),
                                         QStringLiteral("Alt+f"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    press(&engine, {ALT});
    QVERIFY(press(&engine, {static_cast<Vk>(u'F')}).swallow);
    engine.onKey(KeyEvent::keyUp(static_cast<Vk>(u'F')), 0);
    QCOMPARE(engine.onKey(KeyEvent::keyUp(ALT), 0).inject.size(), std::size_t(2));
}

void TestEngine::passthroughAndNoSwallowOptions()
{
    Config tilde;
    tilde.hotkeys.push_back(hotkey(QStringLiteral("~F1"), specOne(noneAction())));
    Engine tildeEngine = engineOf(tilde);
    QVERIFY(!press(&tildeEngine, {0x70}).swallow);

    Config noSwallow;
    HotkeyDef def = hotkey(QStringLiteral("F1"), specOne(noneAction()));
    def.swallow = false;
    noSwallow.hotkeys.push_back(def);
    Engine noSwallowEngine = engineOf(noSwallow);
    QVERIFY(!press(&noSwallowEngine, {0x70}).swallow);
}

void TestEngine::blockingHotkeyWithoutActions()
{
    // 完全没有动作：该快捷键存在的唯一目的就是吞掉这个按键。
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("block-win"), QStringLiteral("LWin")));
    Engine engine = engineOf(config);

    const Reaction fired = press(&engine, {vk::LWIN});
    QVERIFY(fired.swallow);
    QVERIFY(fired.triggers.empty());
    QVERIFY(release(&engine, {vk::LWIN}).swallow);
}

void TestEngine::explicitNoneActionStillCountsAsAnAction()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F3"), specOne(noneAction())));
    Engine engine = engineOf(config);
    QCOMPARE(press(&engine, {0x72}).triggers, (std::vector<Trigger>{onPress(0)}));
}

void TestEngine::onReleaseFires()
{
    Config config;
    HotkeyDef def = hotkeyNamed(QStringLiteral("ptt"), QStringLiteral("F2"));
    def.onRelease = specShort(QStringLiteral("send:{Esc}"));
    config.hotkeys.push_back(def);
    Engine engine = engineOf(config);

    QVERIFY(press(&engine, {0x71}).triggers.empty());
    QCOMPARE(release(&engine, {0x71}).triggers, (std::vector<Trigger>{onRelease(0)}));
}

void TestEngine::mostSpecificChordWins()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("plain"), QStringLiteral("h"),
                                         specOne(noneAction())));
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("ctrl"), QStringLiteral("Ctrl+h"),
                                         specOne(noneAction())));
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("ctrl-alt"), QStringLiteral("Ctrl+Alt+h"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    QCOMPARE(press(&engine, {static_cast<Vk>(u'H')}).triggers, (std::vector<Trigger>{onPress(0)}));
    release(&engine, {static_cast<Vk>(u'H')});

    QCOMPARE(press(&engine, {CTRL, static_cast<Vk>(u'H')}).triggers,
             (std::vector<Trigger>{onPress(1)}));
    release(&engine, {static_cast<Vk>(u'H'), CTRL});

    QCOMPARE(press(&engine, {CTRL, ALT, static_cast<Vk>(u'H')}).triggers,
             (std::vector<Trigger>{onPress(2)}));
    release(&engine, {static_cast<Vk>(u'H'), ALT, CTRL});
}

void TestEngine::winShiftChordWinsOverPlainWinChord()
{
    // 本机配置把 `Win+u` / `Win+i`（带 `follow`）与 `Win+Shift+u` / `Win+Shift+i`
    // 绑成两件事：默认 `exact_modifiers = false`，所以按住 Shift 时**两个和弦都
    // 会匹配**，只能靠打分选更具体的那一条。这条测试盯住它：按 Win+Shift+u
    // 只能触发带 Shift 的那条，不能把不带 Shift 的那条也触发（也不能吞错键）。
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("plain"), QStringLiteral("Win+u"),
                                         specOne(noneAction())));
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("shift"), QStringLiteral("Win+Shift+u"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    QCOMPARE(press(&engine, {vk::LWIN, static_cast<Vk>(u'U')}).triggers,
             (std::vector<Trigger>{onPress(0)}));
    release(&engine, {static_cast<Vk>(u'U'), vk::LWIN});

    QCOMPARE(press(&engine, {vk::LWIN, vk::LSHIFT, static_cast<Vk>(u'U')}).triggers,
             (std::vector<Trigger>{onPress(1)}));
    release(&engine, {static_cast<Vk>(u'U'), vk::LSHIFT, vk::LWIN});
}

void TestEngine::extraModifiersAreToleratedButWildcardPrefersExact()
{
    // `Ctrl+h` 在 Alt 也按着时依旧触发（AutoHotkey 行为）。
    Config tolerant;
    tolerant.hotkeys.push_back(hotkey(QStringLiteral("Ctrl+h"), specOne(noneAction())));
    Engine tolerantEngine = engineOf(tolerant);
    QCOMPARE(press(&tolerantEngine, {CTRL, ALT, static_cast<Vk>(u'H')}).triggers.size(),
             std::size_t(1));

    // 启用 exact_modifiers 后就不触发了。
    Config exact;
    exact.settings.exactModifiers = true;
    exact.hotkeys.push_back(hotkey(QStringLiteral("Ctrl+h"), specOne(noneAction())));
    Engine exactEngine = engineOf(exact);
    QVERIFY(press(&exactEngine, {CTRL, ALT, static_cast<Vk>(u'H')}).isEmpty());

    // 通配和弦无视额外的修饰键，总是触发。
    Config wildcard;
    wildcard.hotkeys.push_back(hotkey(QStringLiteral("*h"), specOne(noneAction())));
    Engine wildcardEngine = engineOf(wildcard);
    QCOMPARE(press(&wildcardEngine, {CTRL, ALT, vk::LSHIFT, static_cast<Vk>(u'H')}).triggers.size(),
             std::size_t(1));
}

void TestEngine::autoRepeatIsNotRematched()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    Engine engine = engineOf(config);

    QCOMPARE(press(&engine, {0x70}).triggers.size(), std::size_t(1));
    // OS 的重复以同一个 VK 的另一次 key-down 到达。
    const Reaction repeat = engine.onKey(KeyEvent::keyDown(0x70), 0);
    QVERIFY2(repeat.swallow, "a swallowed key must stay swallowed while repeated");
    QVERIFY(repeat.triggers.empty());
}

void TestEngine::injectedEventsAreIgnored()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    Engine engine = engineOf(config);

    const KeyEvent injected{0x70, true, true};
    QVERIFY(engine.onKey(injected, 0).isEmpty());
    QVERIFY(engine.stateSummary().contains(QStringLiteral("held=[]")));
}

void TestEngine::longPressRepeat()
{
    Config config;
    HotkeyDef def = hotkey(QStringLiteral("F1"), specOne(sendAction(QStringLiteral("a"))));
    def.repeat = RepeatSpec::makeConfig(100, 500);
    config.hotkeys.push_back(def);
    Engine engine = engineOf(config);

    press(&engine, {0x70});
    QVERIFY(engine.tick(100).triggers.empty());
    QVERIFY(engine.tick(499).triggers.empty());
    QCOMPARE(engine.tick(500).triggers.size(), std::size_t(1));
    QVERIFY(engine.tick(550).triggers.empty());
    QCOMPARE(engine.tick(600).triggers.size(), std::size_t(1));
    QCOMPARE(engine.tick(700).triggers.size(), std::size_t(1));
    // 松开按键会停止重复。
    release(&engine, {0x70});
    QVERIFY(engine.tick(2000).triggers.empty());
}

void TestEngine::pressFiresOnceEvenWhileTheKeyIsHeld()
{
    // “默认只触发一次”：按住不放时操作系统会不断重发 key-down，
    // 但既不会重新匹配，也不会自己重复。
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    Engine engine = engineOf(config);

    QCOMPARE(press(&engine, {0x70}).triggers.size(), std::size_t(1));
    QVERIFY(engine.onKey(KeyEvent::keyDown(0x70), 0).triggers.empty());
    QVERIFY(engine.onKey(KeyEvent::keyDown(0x70), 0).triggers.empty());
    QVERIFY2(engine.tick(10000).triggers.empty(), "不配置 repeat 就不重复");
    QVERIFY(release(&engine, {0x70}).triggers.empty());
}

void TestEngine::triggerRepeatUsesTheSettingsDefaults()
{
    Config config;
    config.settings.repeatIntervalMs = 40;
    config.settings.repeatDelayMs = 300;
    HotkeyDef def = hotkey(QStringLiteral("F1"), specOne(noneAction()));
    def.trigger = TriggerMode::Repeat;
    config.hotkeys.push_back(def);
    Engine engine = engineOf(config);

    // 默认时机的名字叫 press：按下先派发一次……
    QCOMPARE(press(&engine, {0x70}).triggers.size(), std::size_t(1));
    QVERIFY(engine.tick(299).triggers.empty());
    // ……然后在 delay 之后按 interval 重复。
    QCOMPARE(engine.tick(300).triggers.size(), std::size_t(1));
    QCOMPARE(engine.tick(340).triggers.size(), std::size_t(1));
}

void TestEngine::triggerReleaseFiresOnKeyUp()
{
    Config config;
    HotkeyDef def = hotkeyNamed(QStringLiteral("ptt"),
                                QStringLiteral("F2"),
                                specOne(sendAction(QStringLiteral("{Esc}"))));
    def.trigger = TriggerMode::Release;
    config.hotkeys.push_back(def);
    Engine engine = engineOf(config);

    // 按下被吞掉，但动作要等到松开。
    const Reaction down = press(&engine, {0x71});
    QVERIFY(down.swallow);
    QVERIFY2(down.triggers.empty(), "按下时不派发");
    QVERIFY(engine.tick(10000).triggers.empty());
    QCOMPARE(release(&engine, {0x71}).triggers, (std::vector<Trigger>{onRelease(0)}));
}

void TestEngine::remapHoldSemantics()
{
    Config config;
    config.remaps.push_back(remap(QStringLiteral("CapsLock"), QStringLiteral("Esc")));
    Engine engine = engineOf(config);

    const Reaction down = press(&engine, {vk::CAPITAL});
    QVERIFY(down.swallow);
    QCOMPARE(down.inject, (std::vector<SendOp>{SendOp::keyDown(vk::ESCAPE)}));
    const Reaction up = release(&engine, {vk::CAPITAL});
    QVERIFY(up.swallow);
    QCOMPARE(up.inject, (std::vector<SendOp>{SendOp::keyUp(vk::ESCAPE)}));
}

void TestEngine::remapTapSemantics()
{
    Config config;
    config.remaps.push_back(remap(QStringLiteral("F9"), QStringLiteral("{Enter}"), RemapMode::Tap));
    Engine engine = engineOf(config);

    const Reaction down = press(&engine, {0x78});
    QCOMPARE(down.inject.size(), std::size_t(2));
    QVERIFY(release(&engine, {0x78}).inject.empty());
}

void TestEngine::remapNestedChord()
{
    Config config;
    config.remaps.push_back(remap(QStringLiteral("CapsLock"), QStringLiteral("^{c}")));
    Engine engine = engineOf(config);

    const Reaction down = press(&engine, {vk::CAPITAL});
    QCOMPARE(down.inject, (std::vector<SendOp>{SendOp::keyDown(CTRL), SendOp::keyDown(static_cast<Vk>(u'C'))}));
    const Reaction up = release(&engine, {vk::CAPITAL});
    QCOMPARE(up.inject, (std::vector<SendOp>{SendOp::keyUp(static_cast<Vk>(u'C')), SendOp::keyUp(CTRL)}));
}

void TestEngine::hotkeysWinOverRemapsWithTheSameChord()
{
    Config config;
    config.remaps.push_back(remap(QStringLiteral("F1"), QStringLiteral("F2")));
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    Engine engine = engineOf(config);

    const Reaction fired = press(&engine, {0x70});
    QCOMPARE(fired.triggers, (std::vector<Trigger>{onPress(0)}));
    QVERIFY(fired.inject.empty());
}

void TestEngine::suspendDisablesEverythingButTheSuspendHotkey()
{
    Config config;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("toggle"),
                                         QStringLiteral("Ctrl+F12"),
                                         specOne(suspendAction())));
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    Engine engine = engineOf(config);

    QVERIFY(engine.setSuspended(true).empty());
    QVERIFY(engine.isSuspended());
    // 普通快捷键不再触发，也不再吞掉按键。
    QVERIFY(press(&engine, {0x70}).isEmpty());
    release(&engine, {0x70});
    QVERIFY(engine.tick(100000).triggers.empty());

    // suspend 快捷键仍然可用。
    QCOMPARE(press(&engine, {CTRL, 0x7B}).triggers, (std::vector<Trigger>{onPress(0)}));
}

void TestEngine::suspendReleasesKeysHeldByARemap()
{
    Config config;
    config.remaps.push_back(remap(QStringLiteral("CapsLock"), QStringLiteral("Esc")));
    Engine engine = engineOf(config);

    press(&engine, {vk::CAPITAL});
    QCOMPARE(engine.setSuspended(true), (std::vector<SendOp>{SendOp::keyUp(vk::ESCAPE)}));
}

void TestEngine::reloadReleasesKeysHeldByARemap()
{
    Config config;
    config.remaps.push_back(remap(QStringLiteral("CapsLock"), QStringLiteral("Esc")));
    Engine engine = engineOf(config);

    press(&engine, {vk::CAPITAL});
    Config next;
    next.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(noneAction())));
    QCOMPARE(engine.setConfig(compileOrDie(next)),
             (std::vector<SendOp>{SendOp::keyUp(vk::ESCAPE)}));
}

void TestEngine::multipleKeysShareOneHotkey()
{
    Config config;
    HotkeyDef def = hotkeyNamed(QStringLiteral("screenshot"),
                                QStringLiteral("Ctrl+F1"),
                                specOne(noneAction()));
    def.keys = QStringList{QStringLiteral("Ctrl+F1"), QStringLiteral("Ctrl+F2")};
    config.hotkeys.push_back(def);
    Engine engine = engineOf(config);

    QCOMPARE(press(&engine, {CTRL, 0x70}).triggers, (std::vector<Trigger>{onPress(0)}));
    release(&engine, {0x70, CTRL});
    QCOMPARE(press(&engine, {CTRL, 0x71}).triggers, (std::vector<Trigger>{onPress(0)}));
}

void TestEngine::keyUpWithoutKeyDownIsHarmless()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("F1"), specOne(sendAction(QStringLiteral("a")))));
    Engine engine = engineOf(config);
    QVERIFY(engine.onKey(KeyEvent::keyUp(0x70), 0).isEmpty());
}

void TestEngine::modifierOnlyChordsDoNotFireTheirModifier()
{
    Config config;
    config.hotkeys.push_back(hotkey(QStringLiteral("Ctrl+Shift"), specOne(noneAction())));
    Engine engine = engineOf(config);

    // Ctrl 然后 Shift：和弦的按键是 Shift，Ctrl 是修饰键。
    QVERIFY(press(&engine, {CTRL}).isEmpty());
    const Reaction fired = press(&engine, {vk::RSHIFT});
    QCOMPARE(fired.triggers.size(), std::size_t(1));
    QVERIFY2(fired.swallow, "通用 Shift 和弦匹配左右任意一侧");
}

void TestEngine::modifierOnlyChordsWorkWithExactModifiers()
{
    // 和弦自身的按键是修饰键时，它不能算作“额外按住”的修饰键；
    // 否则 `exact_modifiers = true` 会让所有仅修饰键的和弦失效。
    Config config;
    config.settings.exactModifiers = true;
    config.hotkeys.push_back(hotkeyNamed(QStringLiteral("both"),
                                         QStringLiteral("Ctrl+Shift"),
                                         specOne(noneAction())));
    Engine engine = engineOf(config);

    QVERIFY(press(&engine, {CTRL}).isEmpty());
    QCOMPARE(press(&engine, {vk::LSHIFT}).triggers.size(), std::size_t(1));
    release(&engine, {vk::LSHIFT, CTRL});

    // 而真正多出来的修饰键仍然必须被拒绝。
    QVERIFY(press(&engine, {ALT, CTRL, vk::LSHIFT}).isEmpty());

    // 同理，单独一个修饰键作为和弦的按键也应当可用。
    Config single;
    single.settings.exactModifiers = true;
    single.hotkeys.push_back(hotkeyNamed(QStringLiteral("lwin"),
                                         QStringLiteral("LWin"),
                                         specOne(noneAction())));
    Engine singleEngine = engineOf(single);
    QCOMPARE(press(&singleEngine, {vk::LWIN}).triggers.size(), std::size_t(1));
}

QTEST_MAIN(TestEngine)
#include "tst_engine.moc"
