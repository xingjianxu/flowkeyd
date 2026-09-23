// AutoHotkey 风格的发送脚本解析（`^{c}`、`{Enter 3}`、`{Text}`……）。
#include "core/keys.h"

#include <QtTest>

using namespace flowkeyd::core;

namespace {

SendOp down(Vk vk)
{
    return SendOp::keyDown(vk);
}

SendOp up(Vk vk)
{
    return SendOp::keyUp(vk);
}

QVector<SendOp> scriptOf(const QString &text)
{
    QVector<SendOp> ops;
    const auto error = parseSendScript(text, &ops);
    if (error.has_value()) {
        qFatal("unexpected send script error for `%s`: %s",
               qPrintable(text),
               qPrintable(error->message()));
    }
    return ops;
}

QString textOf(const QVector<SendOp> &ops)
{
    QString out;
    for (const SendOp &op : ops) {
        if (op.kind == SendOp::Kind::Text) {
            out.append(QChar(op.text));
        }
    }
    return out;
}

/// `QCOMPARE` 的宏参数里不能出现裸的逗号，所以给这对类型起个名字。
using VkShift = std::optional<std::pair<Vk, bool>>;

} // namespace

class TestSendScript : public QObject
{
    Q_OBJECT

private slots:
    void parsesModifierSendScripts();
    void usesRealKeysForAscii();
    void tokens();
    void literalAndEscapes();
    void errors();
    void holdSplitRemaps();
    void danglingModifierIsAnError();
    void bracedUppercaseLetterIsRejected();
    void charToKeySpotChecks();
};

void TestSendScript::parsesModifierSendScripts()
{
    QCOMPARE(scriptOf(QStringLiteral("^{c}")),
             (QVector<SendOp>{down(vk::LCONTROL), down(static_cast<Vk>(u'C')),
                              up(static_cast<Vk>(u'C')), up(vk::LCONTROL)}));

    // 脚本已经按住了 Ctrl，稍后再显式松开。
    QCOMPARE(scriptOf(QStringLiteral("{Ctrl down}a{Ctrl up}")),
             (QVector<SendOp>{down(vk::CONTROL), down(static_cast<Vk>(u'A')),
                              up(static_cast<Vk>(u'A')), up(vk::CONTROL)}));

    // `{!}` 是能打出 `!` 的键，即 Shift+1。
    QCOMPARE(scriptOf(QStringLiteral("{!}")),
             (QVector<SendOp>{down(vk::LSHIFT), down(static_cast<Vk>(u'1')),
                              up(static_cast<Vk>(u'1')), up(vk::LSHIFT)}));
    QCOMPARE(scriptOf(QStringLiteral("{?}")),
             (QVector<SendOp>{down(vk::LSHIFT), down(vk::OEM_2), up(vk::OEM_2), up(vk::LSHIFT)}));

    // Shift 由大写字母隐含。
    QCOMPARE(scriptOf(QStringLiteral("A")),
             (QVector<SendOp>{down(vk::LSHIFT), down(static_cast<Vk>(u'A')),
                              up(static_cast<Vk>(u'A')), up(vk::LSHIFT)}));
    QCOMPARE(scriptOf(QStringLiteral("+a")),
             (QVector<SendOp>{down(vk::LSHIFT), down(static_cast<Vk>(u'A')),
                              up(static_cast<Vk>(u'A')), up(vk::LSHIFT)}));
}

void TestSendScript::usesRealKeysForAscii()
{
    // `hello` 必须是五次 VK 敲击，绝不使用 Unicode 注入。
    const QVector<SendOp> ops = scriptOf(QStringLiteral("hello"));
    QCOMPARE(ops.size(), 10);
    for (const SendOp &op : ops) {
        QVERIFY(op.kind == SendOp::Kind::Key);
    }
    QCOMPARE(ops.at(0), down(static_cast<Vk>(u'H')));

    // US 布局之外的字符退化为 Unicode。
    const QVector<SendOp> chinese = scriptOf(QStringLiteral("中文"));
    QCOMPARE(textOf(chinese), QStringLiteral("中文"));
    for (const SendOp &op : chinese) {
        QVERIFY(op.kind == SendOp::Kind::Text);
    }
}

void TestSendScript::tokens()
{
    QCOMPARE(scriptOf(QStringLiteral("{Enter}{Sleep 100}{F4}")),
             (QVector<SendOp>{down(vk::RETURN), up(vk::RETURN), SendOp::sleep(100),
                              down(0x73), up(0x73)}));

    const QVector<SendOp> escapes = scriptOf(QStringLiteral("{Esc 3}"));
    int downs = 0;
    for (const SendOp &op : escapes) {
        if (op.kind == SendOp::Kind::Key && op.down) {
            ++downs;
        }
    }
    QCOMPARE(downs, 3);

    QCOMPARE(scriptOf(QStringLiteral("{Down}{Up}")),
             (QVector<SendOp>{down(vk::DOWN), up(vk::DOWN), down(vk::UP), up(vk::UP)}));
}

void TestSendScript::literalAndEscapes()
{
    QCOMPARE(scriptOf(QStringLiteral("{{}")),
             (QVector<SendOp>{down(vk::LSHIFT), down(vk::OEM_4), up(vk::OEM_4), up(vk::LSHIFT)}));
    QCOMPARE(scriptOf(QStringLiteral("{}}")),
             (QVector<SendOp>{down(vk::LSHIFT), down(vk::OEM_6), up(vk::OEM_6), up(vk::LSHIFT)}));

    // `{Text}` 停止解释修饰键和记号。
    const QVector<SendOp> ops = scriptOf(QStringLiteral("^v{Text}^{c}"));
    qsizetype first = -1;
    for (qsizetype i = 0; i < ops.size(); ++i) {
        if (ops.at(i).kind == SendOp::Kind::Text) {
            first = i;
            break;
        }
    }
    QVERIFY(first >= 0);
    QVector<SendOp> tail;
    for (qsizetype i = first; i < ops.size(); ++i) {
        tail.append(ops.at(i));
    }
    QCOMPARE(textOf(tail), QStringLiteral("^{c}"));
}

void TestSendScript::errors()
{
    QVector<SendOp> ops;
    QCOMPARE(parseSendScript(QStringLiteral("{Nope}"), &ops)->kind, KeyError::Kind::UnknownKey);
    QCOMPARE(parseSendScript(QStringLiteral("{Enter"), &ops)->kind, KeyError::Kind::Syntax);
    QCOMPARE(parseSendScript(QStringLiteral("^"), &ops)->kind, KeyError::Kind::Syntax);
    QCOMPARE(parseSendScript(QStringLiteral("{Sleep abc}"), &ops)->kind, KeyError::Kind::Syntax);
    QCOMPARE(parseSendScript(QStringLiteral("{}"), &ops)->kind, KeyError::Kind::Syntax);
    QCOMPARE(parseSendScript(QStringLiteral("{Esc 0}"), &ops)->kind, KeyError::Kind::Syntax);
}

void TestSendScript::holdSplitRemaps()
{
    QVector<SendOp> press;
    QVector<SendOp> release;

    splitHold(scriptOf(QStringLiteral("{Esc}")), &press, &release);
    QCOMPARE(press, (QVector<SendOp>{down(vk::ESCAPE)}));
    QCOMPARE(release, (QVector<SendOp>{up(vk::ESCAPE)}));

    splitHold(scriptOf(QStringLiteral("^{c}")), &press, &release);
    QCOMPARE(press, (QVector<SendOp>{down(vk::LCONTROL), down(static_cast<Vk>(u'C'))}));
    // 松开的顺序与按下相反：先松开 C，再松开 Ctrl。
    QCOMPARE(release, (QVector<SendOp>{up(static_cast<Vk>(u'C')), up(vk::LCONTROL)}));

    // 没有需要松开的内容：整个脚本在按下时一次执行完。
    splitHold(scriptOf(QStringLiteral("{Enter down}")), &press, &release);
    QVERIFY(release.isEmpty());
}

void TestSendScript::danglingModifierIsAnError()
{
    QVector<SendOp> ops;
    const auto error = parseSendScript(QStringLiteral("^"), &ops);
    QVERIFY(error.has_value());
    QCOMPARE(error->message(), QStringLiteral("send script ends with a dangling modifier"));
}

void TestSendScript::bracedUppercaseLetterIsRejected()
{
    // 大括号里的是**键名**，所以单个字母必须小写；要 Shift+S 就写 `{+s}`。
    // 脚本里的裸字符不受影响（上面的 `scriptOf("A")` 仍然是 Shift+A）。
    QVector<SendOp> ops;
    const auto error = parseSendScript(QStringLiteral("{S}"), &ops);
    QVERIFY(error.has_value());
    QCOMPARE(error->kind, KeyError::Kind::UppercaseLetter);
    QVERIFY2(error->message().contains(QStringLiteral("lowercase")), qPrintable(error->message()));

    QVERIFY(!parseSendScript(QStringLiteral("{s}"), &ops).has_value());
    QCOMPARE(ops, (QVector<SendOp>{down(static_cast<Vk>(u'S')), up(static_cast<Vk>(u'S'))}));
    // 显式的 Shift 仍然可以（前缀写在花括号外）：`+s` 就是 Shift+S。
    QVERIFY(!parseSendScript(QStringLiteral("+s"), &ops).has_value());
    QCOMPARE(ops, (QVector<SendOp>{down(vk::LSHIFT), down(static_cast<Vk>(u'S')),
                                   up(static_cast<Vk>(u'S')), up(vk::LSHIFT)}));
}

void TestSendScript::charToKeySpotChecks()
{
    QCOMPARE(charToKey(u'a'), VkShift(std::make_pair(static_cast<Vk>(u'A'), false)));
    QCOMPARE(charToKey(u'A'), VkShift(std::make_pair(static_cast<Vk>(u'A'), true)));
    QCOMPARE(charToKey(u'7'), VkShift(std::make_pair(static_cast<Vk>(u'7'), false)));
    QCOMPARE(charToKey(u'!'), VkShift(std::make_pair(static_cast<Vk>(u'1'), true)));
    QVERIFY(!charToKey(u'中').has_value());
}

QTEST_MAIN(TestSendScript)
#include "tst_send_script.moc"
