// 结构体布局是契约：`INPUT` 在 x64 上必须 40 字节（AGENTS.md 第 7 节第 8 条）。
#include <QtTest>

#include "core/keys.h"
#include "platform/win/ffi.h"
#include "platform/win/input.h"

using namespace flowkeyd;

class TestLayout : public QObject
{
    Q_OBJECT

private slots:
    void inputLayoutMatchesTheWindowsHeaders();
    void numpadEnterIsTheExtendedReturn();
    void mainKeyboardEnterIsNotExtended();
    void arrowKeysAreExtended();
};

void TestLayout::inputLayoutMatchesTheWindowsHeaders()
{
    QCOMPARE(static_cast<int>(sizeof(INPUT)), 40);
    QCOMPARE(static_cast<int>(sizeof(KEYBDINPUT)), 24);
    QCOMPARE(static_cast<int>(sizeof(MOUSEINPUT)), 32);
    QCOMPARE(static_cast<int>(offsetof(INPUT, type)), 0);
    QCOMPARE(static_cast<int>(sizeof(ULONG_PTR)), 8);
}

void TestLayout::numpadEnterIsTheExtendedReturn()
{
    // 小键盘的 Enter 只能靠扩展标志合成：`wVk` 仍是 `VK_RETURN`。
    for (const bool down : {true, false}) {
        const INPUT input = platform::win::keyInput(core::vk::NUMPAD_ENTER, down);
        QCOMPARE(input.type, DWORD(INPUT_KEYBOARD));
        QCOMPARE(input.ki.wVk, static_cast<WORD>(core::vk::RETURN));
        QVERIFY((input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0);
        if (down) {
            QVERIFY((input.ki.dwFlags & KEYEVENTF_KEYUP) == 0);
        } else {
            QVERIFY((input.ki.dwFlags & KEYEVENTF_KEYUP) != 0);
        }
    }
}

void TestLayout::mainKeyboardEnterIsNotExtended()
{
    const INPUT input = platform::win::keyInput(core::vk::RETURN, true);
    QCOMPARE(input.ki.wVk, static_cast<WORD>(core::vk::RETURN));
    QVERIFY((input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) == 0);
}

void TestLayout::arrowKeysAreExtended()
{
    QVERIFY(core::isExtended(core::vk::LEFT));
    QVERIFY(core::isExtended(core::vk::RIGHT));
    QVERIFY(core::isExtended(core::vk::HOME));
    QVERIFY(!core::isExtended(core::vk::RETURN));
    QVERIFY(!core::isExtended(static_cast<core::Vk>('A')));
}

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
