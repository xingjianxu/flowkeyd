// 注入层的纯逻辑单测：`INPUT` 构造标记、后端偏好解析、修饰键释放计划。
//
// 刻意**不**真的注入按键（那会让跑测试的人莫名其妙地掉修饰键）。
#include <QtTest>

#include "core/keys.h"
#include "platform/win/input.h"

using namespace flowkeyd;

class TestInput : public QObject
{
    Q_OBJECT

private slots:
    void keyInputSetsTheExpectedFlags();
    void unicodeInputSetsUnicodeFlag();
    void backendPreferenceParsing();
    void syntheticTagSpellsFlow();
    void modifierReleasePlanMasksMenuKeys();
    void modifierRestorePlanIsReversed();
};

void TestInput::keyInputSetsTheExpectedFlags()
{
    const INPUT down = platform::win::keyInput(static_cast<core::Vk>('A'), true);
    QCOMPARE(down.type, DWORD(INPUT_KEYBOARD));
    QCOMPARE(down.ki.wVk, static_cast<WORD>('A'));
    QVERIFY((down.ki.dwFlags & KEYEVENTF_KEYUP) == 0);
    QCOMPARE(down.ki.dwExtraInfo, platform::win::kSyntheticTag);

    const INPUT up = platform::win::keyInput(static_cast<core::Vk>('B'), false);
    QVERIFY((up.ki.dwFlags & KEYEVENTF_KEYUP) != 0);
    QCOMPARE(up.ki.dwExtraInfo, platform::win::kSyntheticTag);
}

void TestInput::unicodeInputSetsUnicodeFlag()
{
    const INPUT input = platform::win::unicodeInput(0x4F60, true);
    QCOMPARE(input.ki.wVk, WORD(0));
    QCOMPARE(input.ki.wScan, WORD(0x4F60));
    QVERIFY((input.ki.dwFlags & KEYEVENTF_UNICODE) != 0);
    QVERIFY((input.ki.dwFlags & KEYEVENTF_KEYUP) == 0);
    QCOMPARE(input.ki.dwExtraInfo, platform::win::kSyntheticTag);

    const INPUT up = platform::win::unicodeInput(0x4F60, false);
    QVERIFY((up.ki.dwFlags & KEYEVENTF_UNICODE) != 0);
    QVERIFY((up.ki.dwFlags & KEYEVENTF_KEYUP) != 0);
}

void TestInput::backendPreferenceParsing()
{
    using platform::win::BackendPreference;
    QVERIFY(platform::win::parseBackendPreference(QStringLiteral("auto")) == BackendPreference::Auto);
    QVERIFY(platform::win::parseBackendPreference(QStringLiteral("user32"))
            == BackendPreference::User32);
    QVERIFY(platform::win::parseBackendPreference(QStringLiteral("documented"))
            == BackendPreference::User32);
    QVERIFY(platform::win::parseBackendPreference(QStringLiteral("ntuser"))
            == BackendPreference::NtUser);
    QVERIFY(platform::win::parseBackendPreference(QStringLiteral("win32u"))
            == BackendPreference::NtUser);
    QVERIFY(!platform::win::parseBackendPreference(QStringLiteral("bogus")).has_value());
}

void TestInput::syntheticTagSpellsFlow()
{
    // 与 oskeyd 的 `"OSKE"` 同一套约定：按大端读出四个字节。
    const ULONG_PTR tag = platform::win::kSyntheticTag;
    const auto byte = [tag](int shift) { return static_cast<char>((tag >> shift) & 0xFF); };
    QCOMPARE(byte(24), 'F');
    QCOMPARE(byte(16), 'L');
    QCOMPARE(byte(8), 'O');
    QCOMPARE(byte(0), 'W');
}

void TestInput::modifierReleasePlanMasksMenuKeys()
{
    // Ctrl 直接松开；Win/Alt 之前要先插一次未分配按键。
    const std::vector<core::SendOp> plan = platform::win::modifierReleasePlan(
        {core::vk::LCONTROL, core::vk::LWIN, core::vk::LMENU});
    QCOMPARE(plan.size(), std::size_t(7));
    QVERIFY(plan[0] == core::SendOp::keyUp(core::vk::LCONTROL));
    QVERIFY(plan[1] == core::SendOp::keyDown(core::vk::UNASSIGNED));
    QVERIFY(plan[2] == core::SendOp::keyUp(core::vk::UNASSIGNED));
    QVERIFY(plan[3] == core::SendOp::keyUp(core::vk::LWIN));
    QVERIFY(plan[4] == core::SendOp::keyDown(core::vk::UNASSIGNED));
    QVERIFY(plan[5] == core::SendOp::keyUp(core::vk::UNASSIGNED));
    QVERIFY(plan[6] == core::SendOp::keyUp(core::vk::LMENU));
}

void TestInput::modifierRestorePlanIsReversed()
{
    // 最新松开的先按回去。
    const std::vector<core::SendOp> plan =
        platform::win::modifierRestorePlan({core::vk::LCONTROL, core::vk::LWIN});
    QCOMPARE(plan.size(), std::size_t(2));
    QVERIFY(plan[0] == core::SendOp::keyDown(core::vk::LWIN));
    QVERIFY(plan[1] == core::SendOp::keyDown(core::vk::LCONTROL));
}

QTEST_MAIN(TestInput)
#include "tst_input.moc"
