// 按键名 / 和弦 / 小键盘伪码 / 扩展标志。
#include "core/keys.h"

#include <QtTest>

using namespace flowkeyd::core;

namespace {

Chord chordOf(const QString &text)
{
    Chord chord;
    const auto error = parseChord(text, &chord);
    if (error.has_value()) {
        qFatal("unexpected chord error for `%s`: %s", qPrintable(text), qPrintable(error->message()));
    }
    return chord;
}

} // namespace

class TestKeys : public QObject
{
    Q_OBJECT

private slots:
    void parsesNamedChords();
    void parsesAhkPrefixChords();
    void winIsASpelledOutWindowsModifier();
    void rejectsBadChords();
    void rendersChords();
    void genericModifiersMatchEitherSide();
    void numpadEnterIsNotTheMainEnter();
    void numpadArithmeticKeysDifferFromTheMainKeyboard();
    void extendedFlagMatchesWindowsExpectations();
    void keyOrScriptPrefersKeyNames();
    void nameRoundTrip();
};

void TestKeys::parsesNamedChords()
{
    const Chord chord = chordOf(QStringLiteral("Ctrl+Alt+H"));
    QCOMPARE(chord.key, static_cast<Vk>(u'H'));
    QCOMPARE(chord.mods, Modifiers::Ctrl.unioned(Modifiers::Alt));
    QVERIFY(!chord.passthrough);
    QVERIFY(!chord.wildcard);

    const Chord f4 = chordOf(QStringLiteral("CTRL+SHIFT+F4"));
    QCOMPARE(f4.key, static_cast<Vk>(0x73));
    QCOMPARE(f4.mods, Modifiers::Ctrl.unioned(Modifiers::Shift));

    const Chord volume = chordOf(QStringLiteral("Volume_Up"));
    QCOMPARE(volume.key, vk::VOLUME_UP);
    QVERIFY(volume.mods.isEmpty());

    // 修饰键也可以作为和弦的按键部分。
    const Chord ctrlShift = chordOf(QStringLiteral("Ctrl+Shift"));
    QCOMPARE(ctrlShift.key, vk::SHIFT);
    QCOMPARE(ctrlShift.mods, Modifiers::Ctrl);
}

void TestKeys::parsesAhkPrefixChords()
{
    const Chord chord = chordOf(QStringLiteral("^!h"));
    QCOMPARE(chord.key, static_cast<Vk>(u'H'));
    QCOMPARE(chord.mods, Modifiers::Ctrl.unioned(Modifiers::Alt));

    const Chord tilde = chordOf(QStringLiteral("~#Space"));
    QCOMPARE(tilde.key, vk::SPACE);
    QCOMPARE(tilde.mods, Modifiers::Win);
    QVERIFY(tilde.passthrough);

    const Chord star = chordOf(QStringLiteral("*F1"));
    QVERIFY(star.wildcard);
    QVERIFY(star.mods.isEmpty());
}

void TestKeys::winIsASpelledOutWindowsModifier()
{
    // `Win+S` 与 `#s` 必须含义相同；只有 `LWin`/`RWin` 才是分侧的按键。
    for (const QString &spelling : {QStringLiteral("Win+S"),
                                    QStringLiteral("Windows+S"),
                                    QStringLiteral("#s")}) {
        const Chord chord = chordOf(spelling);
        QCOMPARE(chord.key, static_cast<Vk>(u'S'));
        QCOMPARE(chord.mods, Modifiers::Win);
    }
    const Chord lwin = chordOf(QStringLiteral("LWin"));
    QCOMPARE(lwin.key, vk::LWIN);
    QVERIFY(lwin.mods.isEmpty());
}

void TestKeys::rejectsBadChords()
{
    Chord chord;

    QCOMPARE(parseChord(QString(), &chord)->kind, KeyError::Kind::Empty);
    QCOMPARE(parseChord(QStringLiteral("   "), &chord)->kind, KeyError::Kind::Empty);
    QCOMPARE(parseChord(QStringLiteral("Ctrl+Nope"), &chord)->kind, KeyError::Kind::UnknownKey);
    QCOMPARE(parseChord(QStringLiteral("Ctrl++H"), &chord)->kind, KeyError::Kind::Syntax);
    QCOMPARE(parseChord(QStringLiteral("Ctrl+Ctrl"), &chord)->kind, KeyError::Kind::Syntax);
    QCOMPARE(parseChord(QStringLiteral("^"), &chord)->kind, KeyError::Kind::Syntax);
    // `-` 不是分隔符，因此这里是一个未知的按键名。
    QCOMPARE(parseChord(QStringLiteral("Ctrl-H"), &chord)->kind, KeyError::Kind::UnknownKey);
}

void TestKeys::rendersChords()
{
    QCOMPARE(chordOf(QStringLiteral("^!h")).render(), QStringLiteral("Ctrl+Alt+H"));
    QCOMPARE(chordOf(QStringLiteral("F4")).render(), QStringLiteral("F4"));
    QCOMPARE(chordOf(QStringLiteral("NumpadAdd")).render(), QStringLiteral("NumpadAdd"));
    QCOMPARE(chordOf(QStringLiteral("~*#Volume_Up")).render(), QStringLiteral("Win+Volume_Up"));
}

void TestKeys::genericModifiersMatchEitherSide()
{
    QVERIFY(sameKey(vk::SHIFT, vk::LSHIFT));
    QVERIFY(sameKey(vk::SHIFT, vk::RSHIFT));
    QVERIFY(sameKey(vk::CONTROL, vk::RCONTROL));
    QVERIFY(sameKey(vk::MENU, vk::LMENU));
    QVERIFY(sameKey(static_cast<Vk>(u'A'), static_cast<Vk>(u'A')));
    QVERIFY(!sameKey(vk::SHIFT, vk::CONTROL));
    QVERIFY(!sameKey(vk::LSHIFT, vk::RSHIFT));
}

void TestKeys::numpadEnterIsNotTheMainEnter()
{
    // 两个 Enter 在 VK 里是同一个码，钩子的扩展标志才是区别所在。
    QCOMPARE(keyFromName(QStringLiteral("Enter")), std::optional<Vk>(vk::RETURN));
    QCOMPARE(keyFromName(QStringLiteral("NumpadEnter")), std::optional<Vk>(vk::NUMPAD_ENTER));
    QVERIFY(vk::NUMPAD_ENTER != vk::RETURN);
    QCOMPARE(keyFromHook(vk::RETURN, false), vk::RETURN);
    QCOMPARE(keyFromHook(vk::RETURN, true), vk::NUMPAD_ENTER);
    // 其余按键的扩展标志不该改变它们的身份。
    QCOMPARE(keyFromHook(vk::LEFT, true), vk::LEFT);
    QCOMPARE(keyFromHook(vk::DIVIDE, true), vk::DIVIDE);

    // 注入时再翻译回去：小键盘 Enter = VK_RETURN + KEYEVENTF_EXTENDEDKEY。
    QCOMPARE(nativeKey(vk::NUMPAD_ENTER), std::make_pair(vk::RETURN, true));
    QCOMPARE(nativeKey(vk::RETURN), std::make_pair(vk::RETURN, false));
    QCOMPARE(nativeKey(vk::DIVIDE), std::make_pair(vk::DIVIDE, true));
    QCOMPARE(nativeKey(static_cast<Vk>(u'A')), std::make_pair(static_cast<Vk>(u'A'), false));

    // 和弦解析与渲染同样区分得开。
    QCOMPARE(chordOf(QStringLiteral("NumpadEnter")).key, vk::NUMPAD_ENTER);
    QCOMPARE(chordOf(QStringLiteral("NumpadEnter")).render(), QStringLiteral("NumpadEnter"));
    QCOMPARE(chordOf(QStringLiteral("Enter")).render(), QStringLiteral("Enter"));
    QVERIFY(!sameKey(vk::NUMPAD_ENTER, vk::RETURN));
}

void TestKeys::numpadArithmeticKeysDifferFromTheMainKeyboard()
{
    QCOMPARE(keyFromName(QStringLiteral("NumpadSub")), std::optional<Vk>(vk::SUBTRACT));
    QCOMPARE(keyFromName(QStringLiteral("NumpadMinus")), std::optional<Vk>(vk::SUBTRACT));
    QCOMPARE(keyFromName(QStringLiteral("NumpadAdd")), std::optional<Vk>(vk::ADD));
    QCOMPARE(keyFromName(QStringLiteral("NumpadPlus")), std::optional<Vk>(vk::ADD));
    QCOMPARE(keyFromName(QStringLiteral("Minus")), std::optional<Vk>(vk::OEM_MINUS));
    QCOMPARE(keyFromName(QStringLiteral("Equal")), std::optional<Vk>(vk::OEM_PLUS));
    QVERIFY(vk::SUBTRACT != vk::OEM_MINUS);
    QVERIFY(vk::ADD != vk::OEM_PLUS);
    QVERIFY(!isExtended(vk::SUBTRACT));
    QVERIFY(!isExtended(vk::ADD));
    QCOMPARE(nativeKey(vk::SUBTRACT), std::make_pair(vk::SUBTRACT, false));
    QCOMPARE(nativeKey(vk::ADD), std::make_pair(vk::ADD, false));
}

void TestKeys::extendedFlagMatchesWindowsExpectations()
{
    QVERIFY(isExtended(vk::RMENU));
    QVERIFY(isExtended(vk::LEFT));
    QVERIFY(isExtended(vk::VOLUME_UP));
    QVERIFY(!isExtended(static_cast<Vk>(u'A')));
    QVERIFY(!isExtended(vk::LSHIFT));
}

void TestKeys::keyOrScriptPrefersKeyNames()
{
    // 裸按键名是按键，而不是一串字符。
    QVector<SendOp> ops;
    QVERIFY(!parseKeyOrScript(QStringLiteral("Esc"), &ops).has_value());
    QCOMPARE(ops.size(), 2);
    QCOMPARE(ops.at(0), SendOp::keyDown(vk::ESCAPE));

    // 带大括号的记号与修饰键属于脚本。
    QVERIFY(!parseKeyOrScript(QStringLiteral("^{c}"), &ops).has_value());
    QCOMPARE(ops.size(), 4);
    QVERIFY(!parseKeyOrScript(QStringLiteral("{Enter}"), &ops).has_value());
    QCOMPARE(ops.size(), 2);
    // 普通文本仍是文本。
    QVERIFY(!parseKeyOrScript(QStringLiteral("hello"), &ops).has_value());
    QCOMPARE(ops.size(), 10);

    QVERIFY(!parseKeyOrScript(QString(), &ops).has_value());
    QVERIFY(ops.isEmpty());
}

void TestKeys::nameRoundTrip()
{
    for (const QString &name : allKeyNames()) {
        const std::optional<Vk> vk = keyFromName(name);
        QVERIFY2(vk.has_value(), qPrintable(name));
        const QString rendered = nameFromKey(*vk);
        QCOMPARE(keyFromName(rendered), std::optional<Vk>(*vk));
    }
}

QTEST_MAIN(TestKeys)
#include "tst_keys.moc"
