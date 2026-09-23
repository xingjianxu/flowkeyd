// 虚拟键名、修饰键集合、和弦（chord）以及 AutoHotkey 风格的发送脚本。
//
// 本模块刻意不含任何 Win32 声明，以便单独做单元测试。这里只有纯数据 + 解析。
#pragma once

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>
#include <utility>

namespace flowkeyd::core {

/// 一个 Windows 虚拟键码（`VK_*`）。低级键盘钩子只会用到低字节，
/// 但媒体键位于 0xA0 以上，因此这里保留 16 位。
using Vk = std::uint16_t;

/// flowkeyd 全程使用的虚拟键码。该表刻意保持完整（含鼠标键与罕用键），
/// 以保证 `--list-keys` 与名称解析器保持一致。
namespace vk {

inline constexpr Vk LBUTTON = 0x01;
inline constexpr Vk RBUTTON = 0x02;
inline constexpr Vk CANCEL = 0x03;
inline constexpr Vk MBUTTON = 0x04;
inline constexpr Vk XBUTTON1 = 0x05;
inline constexpr Vk XBUTTON2 = 0x06;
inline constexpr Vk BACK = 0x08;
inline constexpr Vk TAB = 0x09;
inline constexpr Vk CLEAR = 0x0C;
inline constexpr Vk RETURN = 0x0D;
inline constexpr Vk SHIFT = 0x10;
inline constexpr Vk CONTROL = 0x11;
inline constexpr Vk MENU = 0x12;
inline constexpr Vk PAUSE = 0x13;
inline constexpr Vk CAPITAL = 0x14;
inline constexpr Vk ESCAPE = 0x1B;
inline constexpr Vk SPACE = 0x20;
inline constexpr Vk PRIOR = 0x21;
inline constexpr Vk NEXT = 0x22;
inline constexpr Vk END = 0x23;
inline constexpr Vk HOME = 0x24;
inline constexpr Vk LEFT = 0x25;
inline constexpr Vk UP = 0x26;
inline constexpr Vk RIGHT = 0x27;
inline constexpr Vk DOWN = 0x28;
inline constexpr Vk SNAPSHOT = 0x2C;
inline constexpr Vk INSERT = 0x2D;
inline constexpr Vk DELETE = 0x2E;
inline constexpr Vk HELP = 0x2F;
inline constexpr Vk LWIN = 0x5B;
inline constexpr Vk RWIN = 0x5C;
inline constexpr Vk APPS = 0x5D;
inline constexpr Vk SLEEP = 0x5F;
inline constexpr Vk NUMPAD0 = 0x60;
inline constexpr Vk MULTIPLY = 0x6A;
inline constexpr Vk ADD = 0x6B;
inline constexpr Vk SEPARATOR = 0x6C;
inline constexpr Vk SUBTRACT = 0x6D;
inline constexpr Vk DECIMAL = 0x6E;
inline constexpr Vk DIVIDE = 0x6F;
inline constexpr Vk NUMLOCK = 0x90;
inline constexpr Vk SCROLL = 0x91;
inline constexpr Vk LSHIFT = 0xA0;
inline constexpr Vk RSHIFT = 0xA1;
inline constexpr Vk LCONTROL = 0xA2;
inline constexpr Vk RCONTROL = 0xA3;
inline constexpr Vk LMENU = 0xA4;
inline constexpr Vk RMENU = 0xA5;
inline constexpr Vk BROWSER_BACK = 0xA6;
inline constexpr Vk BROWSER_FORWARD = 0xA7;
inline constexpr Vk BROWSER_REFRESH = 0xA8;
inline constexpr Vk BROWSER_STOP = 0xA9;
inline constexpr Vk BROWSER_SEARCH = 0xAA;
inline constexpr Vk BROWSER_FAVORITES = 0xAB;
inline constexpr Vk BROWSER_HOME = 0xAC;
inline constexpr Vk VOLUME_MUTE = 0xAD;
inline constexpr Vk VOLUME_DOWN = 0xAE;
inline constexpr Vk VOLUME_UP = 0xAF;
inline constexpr Vk MEDIA_NEXT_TRACK = 0xB0;
inline constexpr Vk MEDIA_PREV_TRACK = 0xB1;
inline constexpr Vk MEDIA_STOP = 0xB2;
inline constexpr Vk MEDIA_PLAY_PAUSE = 0xB3;
inline constexpr Vk LAUNCH_MAIL = 0xB4;
inline constexpr Vk LAUNCH_MEDIA_SELECT = 0xB5;
inline constexpr Vk LAUNCH_APP1 = 0xB6;
inline constexpr Vk LAUNCH_APP2 = 0xB7;
inline constexpr Vk OEM_1 = 0xBA;   // ';:'  Semicolon
inline constexpr Vk OEM_PLUS = 0xBB; // '=+'
inline constexpr Vk OEM_COMMA = 0xBC; // ',<'
inline constexpr Vk OEM_MINUS = 0xBD; // '-_'
inline constexpr Vk OEM_PERIOD = 0xBE; // '.>'
inline constexpr Vk OEM_2 = 0xBF;    // '/?'
inline constexpr Vk OEM_3 = 0xC0;    // '`~'
inline constexpr Vk OEM_4 = 0xDB;    // '[{'
inline constexpr Vk OEM_5 = 0xDC;    // '\|'
inline constexpr Vk OEM_6 = 0xDD;    // ']}'
inline constexpr Vk OEM_7 = 0xDE;    // ''"
inline constexpr Vk OEM_8 = 0xDF;
inline constexpr Vk OEM_102 = 0xE2;
inline constexpr Vk PROCESSKEY = 0xE5;
inline constexpr Vk PACKET = 0xE7;
inline constexpr Vk ATTN = 0xF6;
inline constexpr Vk CRSEL = 0xF7;
inline constexpr Vk EXSEL = 0xF8;
inline constexpr Vk EREOF = 0xF9;
inline constexpr Vk PLAY = 0xFA;
inline constexpr Vk ZOOM = 0xFB;
inline constexpr Vk PA1 = 0xFD;
inline constexpr Vk OEM_CLEAR = 0xFE;

/// 一个未分配的键（Windows 头文件中称为 `VK 0xE8`）。当被吞掉的
/// `Win+…` 和弦会让外壳（shell）以为 Windows 键是单独按下时，
/// 就注入它来遮断这种判断，否则松开时资源管理器会弹出开始菜单 /
/// 搜索框。参见 `Engine::maskMenuKeyUp`。
inline constexpr Vk UNASSIGNED = 0xE8;

/// flowkeyd 内部的**伪码**，表示小键盘上的 Enter。
///
/// 物理上它与主键盘的 Enter 是同一个 `VK_RETURN`（0x0D）：整个 VK 空间里
/// 没有第二个码，唯一的区别是 `KBDLLHOOKSTRUCT::flags` 里的
/// `LLKHF_EXTENDED`（小键盘的 Enter 带这个标志，主键盘的不带）。
/// 钩子用 `keyFromHook()` 把它换成本码，这样 `keys = "NumpadEnter"` 就绝不会
/// 匹配主键盘的 Enter（反之亦然）；注入时再由 `nativeKey()` 换回
/// `(VK_RETURN, 带扩展标志)`。
///
/// 取值刻意在 `0xFF` 之外：所有真实的 VK 都不超过 `0xFF`，
/// 因此它不可能与钩子报上来的键码撞车。
inline constexpr Vk NUMPAD_ENTER = 0x100;

} // namespace vk

/// 按键/和弦/发送脚本的解析错误。
class KeyError
{
public:
    enum class Kind {
        /// `parse_chord("")`
        Empty,
        /// 不认识的按键名。
        UnknownKey,
        /// 单个字母写成了大写。键名一律小写；想要大写键要显式写 `Shift+`。
        UppercaseLetter,
        /// 语法错（没有按键、悬空的修饰键……）。
        Syntax,
    };

    Kind kind = Kind::Empty;
    /// `UnknownKey`/`UppercaseLetter` 时是按键名；`Syntax` 时是完整错误信息。
    QString detail;

    KeyError() = default;
    KeyError(Kind kind, QString detail) : kind(kind), detail(std::move(detail)) {}

    static KeyError empty() { return KeyError(Kind::Empty, QString()); }
    static KeyError unknownKey(const QString &name) { return KeyError(Kind::UnknownKey, name); }
    static KeyError uppercaseLetter(const QString &name) { return KeyError(Kind::UppercaseLetter, name); }
    static KeyError syntax(const QString &message) { return KeyError(Kind::Syntax, message); }

    /// 与 oskeyd 一致的英文错误文案。
    QString message() const;
};

/// 修饰键集合：Ctrl / Alt / Shift / Win。和弦中刻意不区分左右。
class Modifiers
{
public:
    constexpr Modifiers() = default;
    explicit constexpr Modifiers(std::uint8_t bits) : m_bits(bits) {}

    static const Modifiers None;
    static const Modifiers Ctrl;
    static const Modifiers Alt;
    static const Modifiers Shift;
    static const Modifiers Win;
    constexpr bool contains(Modifiers other) const { return (m_bits & other.m_bits) == other.m_bits; }
    constexpr bool isEmpty() const { return m_bits == 0; }
    constexpr std::uint8_t bits() const { return m_bits; }
    /// 已置位的修饰键数量；用于挑选最具体的快捷键。
    constexpr int count() const;
    constexpr Modifiers unioned(Modifiers other) const { return Modifiers(static_cast<std::uint8_t>(m_bits | other.m_bits)); }
    /// 从集合中去掉若干修饰键；用于把和弦自身的按键排除在“已按住”之外。
    constexpr Modifiers without(Modifiers other) const { return Modifiers(static_cast<std::uint8_t>(m_bits & ~other.m_bits)); }

    /// 某个具体 VK 对应的通用修饰键（如果有）。
    static std::optional<Modifiers> fromVk(Vk vk);

    /// 为了探测 / 合成某个修饰键集合而使用的 VK（合成时使用物理“左侧”变体，
    /// AutoHotkey 也是这么做的）。
    QVector<Vk> vks() const;

    /// `Ctrl`、`Alt`……（按固定顺序）。
    QStringList names() const;

    friend constexpr bool operator==(Modifiers a, Modifiers b) { return a.m_bits == b.m_bits; }
    friend constexpr bool operator!=(Modifiers a, Modifiers b) { return a.m_bits != b.m_bits; }

private:
    std::uint8_t m_bits = 0;
};

constexpr int Modifiers::count() const
{
    int n = 0;
    for (std::uint8_t bits = m_bits; bits != 0; bits >>= 1) {
        n += bits & 1u;
    }
    return n;
}

/// 一个按键，以及它触发时必须按住的修饰键。
struct Chord
{
    Modifiers mods;
    Vk key = 0;
    /// `~` 前缀：即使快捷键触发，也放行原始按键。
    bool passthrough = false;
    /// `*` 前缀：即使额外按住了其它修饰键也触发。
    bool wildcard = false;

    /// 人类可读的渲染，例如 `Ctrl+Alt+h`。
    QString render() const;

    friend bool operator==(const Chord &a, const Chord &b) = default;
};

/// 发送脚本中的一个原始步骤。
struct SendOp
{
    enum class Kind {
        /// 按下或松开单个按键。
        Key,
        /// 把一个 UTF-16 码元作为 Unicode 按键输入。
        Text,
        /// 步骤之间的停顿。
        Sleep,
    };

    Kind kind = Kind::Key;
    Vk vk = 0;
    bool down = false;
    char16_t text = 0;
    std::uint32_t ms = 0;

    static SendOp keyDown(Vk vk);
    static SendOp keyUp(Vk vk);
    static SendOp unicode(char16_t unit);
    static SendOp sleep(std::uint32_t ms);

    friend bool operator==(const SendOp &a, const SendOp &b) = default;
};

/// 把按键名规范化，使其对大小写、空格、短横线和下划线不敏感。
///
/// 这只是查表用的规范化（`Enter`、`capslock`、`volume up` 都能找到）。
/// **单个字母作为键名仍必须小写**，由 `keyFromName()` 负责。
QString normalizeKeyName(const QString &name);

/// 一个“键名”是不是被写成了单个大写 ASCII 字母（`A`..`Z`）？
///
/// 键名一律小写；是这种形状时返回它的小写形式（报错文案用得到），否则返回
/// `std::nullopt`。`parseChord`/`parseSendScript` 与 `remap` 的 `to` 用它做检查；
/// **`send` 脚本里的裸字符不走这里**（`send("A")` 仍然是“打出大写 A”）。
std::optional<QChar> uppercaseLetterKey(const QString &name);

/// 把按键名（`"Enter"`、`"F4"`、`"volume_up"`、`"a"`）解析为 VK 码。
///
/// **单个字母必须是 小写**：`"a"` 是 A 键，而 `"A"` 不是一个键名（返回
/// `std::nullopt`）—— 想要大写键请写成 `Shift+a`。多字母的名字（`Enter`、
/// `CapsLock`、`Volume_Up`……）照旧对大小写不敏感。
std::optional<Vk> keyFromName(const QString &name);

/// VK 码的规范显示名（`"F4"`、`"Volume_Up"`、`"a"`）。
QString nameFromKey(Vk vk);

/// 该 VK 是否需要 `SendInput` 的 `KEYEVENTF_EXTENDEDKEY`。
///
/// 只对真实的 VK 有意义；内部的伪码（目前只有 `vk::NUMPAD_ENTER`）
/// 请用 `nativeKey()`。
bool isExtended(Vk vk);

/// 把低级钩子报告的一对 `(vkCode, LLKHF_EXTENDED)` 翻译成 flowkeyd 的键码。
///
/// 只有小键盘的 Enter 需要翻译：它与主键盘的 Enter 共用 `VK_RETURN`，
/// 唯一的区别就是这个扩展标志。
Vk keyFromHook(Vk vk, bool extended);

/// 注入一个键码时 `SendInput` 需要的真实 `(VK, 是否带扩展标志)`。
std::pair<Vk, bool> nativeKey(Vk vk);

/// 用户可以书写的所有名称，已排序，供 `--list-keys` 使用。
QStringList allKeyNames();

/// 解析和弦，例如 `"Ctrl+Alt+h"`、`"^!h"`、`"~F4"`、`"*NumpadAdd"`。
///
/// 和弦里的单个字母必须是 小写；写成大写会返回 `KeyError::Kind::UppercaseLetter`
/// （想表达大写键要写 `Shift+h`）。
std::optional<KeyError> parseChord(const QString &input, Chord *out);

/// 把可打印字符映射为在 US 布局下产生它的 `(VK, 是否需要 Shift)`。
///
/// `send` 脚本用它，使 `^a` 表现得像 Ctrl+A，而不是“Ctrl 加上字面字符 a”。
/// `type` 动作刻意不使用它，而是直接注入 Unicode。
std::optional<std::pair<Vk, bool>> charToKey(QChar c);

/// 解析 AutoHotkey 风味的发送脚本。
std::optional<KeyError> parseSendScript(const QString &input, QVector<SendOp> *out);

/// 把一段短字符串解释为单个按键名或一个发送脚本。
///
/// `"Esc"` 表示 Escape 键（如同 AutoHotkey 的 `CapsLock::Esc`），绝不会被
/// 当成字母 E、s、c。任何含有脚本语法（`{}^!+#`）的字符串都交给
/// `parseSendScript()`。
///
/// 单个字母按**键名规则**处理：小写（`"a"`）才是键名；大写的单个字母不是
/// 键名，于是整串按发送脚本文本解释（`"A"` 就是“打出大写 A”，即 `Shift+a`）。
std::optional<KeyError> parseKeyOrScript(const QString &input, QVector<SendOp> *out);

/// 把发送脚本拆成“要按下什么”和“要松开什么”，`remap` 正是用它实现
/// hold 语义（按住 `CapsLock` 等同于按住 `Esc`）。
///
/// 末尾连续的 key-up 操作从“按下”部分切出，作为“松开”部分。它们本身已经是
/// 正确的松开顺序（`^{c}` 展开为 Ctrl↓ C↓ C↑ Ctrl↑），因此**不能反转**——
/// 反转会先松开 Ctrl 再松开 C。
void splitHold(const QVector<SendOp> &ops, QVector<SendOp> *press, QVector<SendOp> *release);

/// 物理按键 `vk` 是否满足写成 `chordKey` 的和弦？
///
/// 通用修饰键 VK（`VK_SHIFT`、`VK_CONTROL`、`VK_MENU`）表示“左右任意一侧”，
/// 这正是用户写 `Ctrl+Shift` 时的意图；而低级钩子只会报告
/// `VK_LSHIFT`/`VK_RSHIFT` 之类的具体键。
bool sameKey(Vk chordKey, Vk vk);

} // namespace flowkeyd::core
