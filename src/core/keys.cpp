#include "core/keys.h"

#include <algorithm>

namespace flowkeyd::core {

const Modifiers Modifiers::None{0};
const Modifiers Modifiers::Ctrl{1u << 0};
const Modifiers Modifiers::Alt{1u << 1};
const Modifiers Modifiers::Shift{1u << 2};
const Modifiers Modifiers::Win{1u << 3};

QString KeyError::message() const
{
    switch (kind) {
    case Kind::Empty:
        return QStringLiteral("empty key or chord");
    case Kind::UnknownKey:
        return QStringLiteral("unknown key name `%1` (see `flowkeyd --list-keys`)").arg(detail);
    case Kind::UppercaseLetter:
        return QStringLiteral("letter key names must be lowercase: write `%1` instead of `%2` "
                              "(for the uppercase key write `Shift+%1`)")
            .arg(detail.toLower(), detail);
    case Kind::Syntax:
        return detail;
    }
    return QStringLiteral("unknown key error");
}

// ---------------------------------------------------------------------------
// 名称表
// ---------------------------------------------------------------------------

namespace {

struct KeyName
{
    const char *name;
    Vk vk;
};

/// 名称表。同一个 VK 的第一个条目决定其规范显示名。
constexpr KeyName kKeyTable[] = {
    {"Backspace", vk::BACK},
    {"Tab", vk::TAB},
    {"Clear", vk::CLEAR},
    {"Enter", vk::RETURN},
    {"Return", vk::RETURN},
    {"Shift", vk::SHIFT},
    {"Ctrl", vk::CONTROL},
    {"Control", vk::CONTROL},
    {"Alt", vk::MENU},
    {"Pause", vk::PAUSE},
    {"Break", vk::PAUSE},
    {"CapsLock", vk::CAPITAL},
    {"Esc", vk::ESCAPE},
    {"Escape", vk::ESCAPE},
    {"Space", vk::SPACE},
    {"PageUp", vk::PRIOR},
    {"PgUp", vk::PRIOR},
    {"PageDown", vk::NEXT},
    {"PgDn", vk::NEXT},
    {"End", vk::END},
    {"Home", vk::HOME},
    {"Left", vk::LEFT},
    {"Up", vk::UP},
    {"Right", vk::RIGHT},
    {"Down", vk::DOWN},
    {"PrintScreen", vk::SNAPSHOT},
    {"PrtSc", vk::SNAPSHOT},
    {"Insert", vk::INSERT},
    {"Ins", vk::INSERT},
    {"Delete", vk::DELETE},
    {"Del", vk::DELETE},
    {"Help", vk::HELP},
    {"LWin", vk::LWIN},
    {"RWin", vk::RWIN},
    // `Win+S` 比 `#s` 更好读，而且任意一个 Windows 键都能满足该修饰键
    // （和弦匹配基于 Modifiers，而不是原始 VK）。
    {"Win", vk::LWIN},
    {"Windows", vk::LWIN},
    {"Apps", vk::APPS},
    {"Menu", vk::APPS},
    {"Sleep", vk::SLEEP},
    {"NumpadMult", vk::MULTIPLY},
    {"NumpadAdd", vk::ADD},
    {"NumpadPlus", vk::ADD},
    {"NumpadSub", vk::SUBTRACT},
    {"NumpadMinus", vk::SUBTRACT},
    {"NumpadDot", vk::DECIMAL},
    {"NumpadDel", vk::DECIMAL},
    {"NumpadDiv", vk::DIVIDE},
    // 小键盘的 Enter：与主键盘的 Enter 是物理上不同的键，见 vk::NUMPAD_ENTER。
    {"NumpadEnter", vk::NUMPAD_ENTER},
    {"NumLock", vk::NUMLOCK},
    {"ScrollLock", vk::SCROLL},
    {"LShift", vk::LSHIFT},
    {"RShift", vk::RSHIFT},
    {"LCtrl", vk::LCONTROL},
    {"RCtrl", vk::RCONTROL},
    {"LAlt", vk::LMENU},
    {"RAlt", vk::RMENU},
    {"Browser_Back", vk::BROWSER_BACK},
    {"Browser_Forward", vk::BROWSER_FORWARD},
    {"Browser_Refresh", vk::BROWSER_REFRESH},
    {"Browser_Stop", vk::BROWSER_STOP},
    {"Browser_Search", vk::BROWSER_SEARCH},
    {"Browser_Favorites", vk::BROWSER_FAVORITES},
    {"Browser_Home", vk::BROWSER_HOME},
    {"Volume_Mute", vk::VOLUME_MUTE},
    {"Volume_Down", vk::VOLUME_DOWN},
    {"Volume_Up", vk::VOLUME_UP},
    {"Media_Next", vk::MEDIA_NEXT_TRACK},
    {"Media_Prev", vk::MEDIA_PREV_TRACK},
    {"Media_Stop", vk::MEDIA_STOP},
    {"Media_Play_Pause", vk::MEDIA_PLAY_PAUSE},
    {"Launch_Mail", vk::LAUNCH_MAIL},
    {"Launch_Media", vk::LAUNCH_MEDIA_SELECT},
    {"Launch_App1", vk::LAUNCH_APP1},
    {"Launch_App2", vk::LAUNCH_APP2},
    {"Semicolon", vk::OEM_1},
    {"Equal", vk::OEM_PLUS},
    {"Comma", vk::OEM_COMMA},
    {"Minus", vk::OEM_MINUS},
    {"Period", vk::OEM_PERIOD},
    {"Slash", vk::OEM_2},
    {"Backtick", vk::OEM_3},
    {"Grave", vk::OEM_3},
    {"LBracket", vk::OEM_4},
    {"Backslash", vk::OEM_5},
    {"RBracket", vk::OEM_6},
    {"Quote", vk::OEM_7},
    {"OEM_8", vk::OEM_8},
    {"OEM_102", vk::OEM_102},
    {"ProcessKey", vk::PROCESSKEY},
    {"Packet", vk::PACKET},
    {"Attn", vk::ATTN},
    {"CrSel", vk::CRSEL},
    {"ExSel", vk::EXSEL},
    {"EraseEOF", vk::EREOF},
    {"Play", vk::PLAY},
    {"Zoom", vk::ZOOM},
    {"PA1", vk::PA1},
    {"OEM_Clear", vk::OEM_CLEAR},
};

/// `SendInput` 必须为其加上 `KEYEVENTF_EXTENDEDKEY` 标志的 VK 码。
constexpr Vk kExtendedKeys[] = {
    vk::RCONTROL,       vk::RMENU,          vk::INSERT,           vk::DELETE,
    vk::HOME,           vk::END,            vk::PRIOR,            vk::NEXT,
    vk::LEFT,           vk::UP,             vk::RIGHT,            vk::DOWN,
    vk::NUMLOCK,        vk::SNAPSHOT,       vk::DIVIDE,           vk::LWIN,
    vk::RWIN,           vk::APPS,           vk::VOLUME_MUTE,      vk::VOLUME_DOWN,
    vk::VOLUME_UP,      vk::MEDIA_NEXT_TRACK, vk::MEDIA_PREV_TRACK, vk::MEDIA_STOP,
    vk::MEDIA_PLAY_PAUSE, vk::LAUNCH_MAIL,  vk::LAUNCH_MEDIA_SELECT, vk::LAUNCH_APP1,
    vk::LAUNCH_APP2,    vk::BROWSER_BACK,   vk::BROWSER_FORWARD,  vk::BROWSER_REFRESH,
    vk::BROWSER_STOP,   vk::BROWSER_SEARCH, vk::BROWSER_FAVORITES, vk::BROWSER_HOME,
};

std::optional<Modifiers> modifierPrefix(QChar c)
{
    switch (c.unicode()) {
    case u'^':
        return Modifiers::Ctrl;
    case u'!':
        return Modifiers::Alt;
    case u'+':
        return Modifiers::Shift;
    case u'#':
        return Modifiers::Win;
    default:
        return std::nullopt;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 名称解析
// ---------------------------------------------------------------------------

QString normalizeKeyName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name) {
        if (ch == u' ' || ch == u'-' || ch == u'_') {
            continue;
        }
        out.append(ch.toUpper());
    }
    return out;
}

std::optional<QChar> uppercaseLetterKey(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.size() != 1) {
        return std::nullopt;
    }
    const char16_t c = trimmed.at(0).unicode();
    if (c >= u'A' && c <= u'Z') {
        return QChar(static_cast<char16_t>(c - u'A' + u'a'));
    }
    return std::nullopt;
}

std::optional<Vk> keyFromName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    // 单个字母必须是 小写。大写的 `"A"` 不是一个键名；想要大写键要写 `Shift+a`。
    // 这里返回 `nullopt`，让 `parseKeyOrScript()` 把大写的单个字母当成发送
    // 脚本文本（`"A"` = 打出大写 A）。
    if (uppercaseLetterKey(trimmed).has_value()) {
        return std::nullopt;
    }

    switch (trimmed.at(0).unicode()) {
    case u'-':
        if (trimmed.size() == 1) {
            return vk::OEM_MINUS;
        }
        break;
    case u'+':
        if (trimmed.size() == 1) {
            return vk::ADD;
        }
        break;
    case u'*':
        if (trimmed.size() == 1) {
            return vk::MULTIPLY;
        }
        break;
    case u'/':
        if (trimmed.size() == 1) {
            return vk::OEM_2;
        }
        break;
    case u'.':
        if (trimmed.size() == 1) {
            return vk::OEM_PERIOD;
        }
        break;
    case u',':
        if (trimmed.size() == 1) {
            return vk::OEM_COMMA;
        }
        break;
    case u';':
        if (trimmed.size() == 1) {
            return vk::OEM_1;
        }
        break;
    case u'=':
        if (trimmed.size() == 1) {
            return vk::OEM_PLUS;
        }
        break;
    case u'`':
        if (trimmed.size() == 1) {
            return vk::OEM_3;
        }
        break;
    case u'\'':
        if (trimmed.size() == 1) {
            return vk::OEM_7;
        }
        break;
    case u'[':
        if (trimmed.size() == 1) {
            return vk::OEM_4;
        }
        break;
    case u']':
        if (trimmed.size() == 1) {
            return vk::OEM_6;
        }
        break;
    case u'\\':
        if (trimmed.size() == 1) {
            return vk::OEM_5;
        }
        break;
    default:
        break;
    }

    const QString want = normalizeKeyName(trimmed);

    // A..Z
    if (want.size() == 1) {
        const char16_t c = want.at(0).unicode();
        if (c >= u'A' && c <= u'Z') {
            return static_cast<Vk>(c);
        }
        // 0..9（主键盘区数字行）
        if (c >= u'0' && c <= u'9') {
            return static_cast<Vk>(c);
        }
    }

    // F1..F24
    if (want.size() >= 2 && want.at(0) == u'F') {
        bool ok = false;
        const uint n = want.mid(1).toUInt(&ok);
        if (ok && n >= 1 && n <= 24) {
            return static_cast<Vk>(0x70 + (n - 1));
        }
    }

    // Numpad0..Numpad9
    if (want.startsWith(QLatin1String("NUMPAD")) && want.size() == 7) {
        const char16_t c = want.at(6).unicode();
        if (c >= u'0' && c <= u'9') {
            return static_cast<Vk>(vk::NUMPAD0 + (c - u'0'));
        }
    }

    for (const KeyName &entry : kKeyTable) {
        if (normalizeKeyName(QString::fromLatin1(entry.name)) == want) {
            return entry.vk;
        }
    }
    return std::nullopt;
}

QString nameFromKey(Vk vkCode)
{
    for (const KeyName &entry : kKeyTable) {
        if (entry.vk == vkCode) {
            return QString::fromLatin1(entry.name);
        }
    }
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        // 字母键的规范名是小写（配置里也一律写小写）。
        return QString(QChar(static_cast<char16_t>(vkCode - 0x41 + u'a')));
    }
    if (vkCode >= 0x30 && vkCode <= 0x39) {
        return QString(QChar(static_cast<char16_t>(vkCode)));
    }
    if (vkCode >= 0x70 && vkCode <= 0x87) {
        return QStringLiteral("F%1").arg(vkCode - 0x70 + 1);
    }
    if (vkCode >= vk::NUMPAD0 && vkCode <= vk::NUMPAD0 + 9) {
        return QStringLiteral("Numpad%1").arg(vkCode - vk::NUMPAD0);
    }
    return QStringLiteral("VK_%1").arg(vkCode, 2, 16, QLatin1Char('0')).toUpper();
}

bool isExtended(Vk vkCode)
{
    return std::find(std::begin(kExtendedKeys), std::end(kExtendedKeys), vkCode) != std::end(kExtendedKeys);
}

bool isModifierKey(Vk vkCode)
{
    return Modifiers::fromVk(vkCode).has_value();
}

Vk keyFromHook(Vk vkCode, bool extended)
{
    if (extended && vkCode == vk::RETURN) {
        return vk::NUMPAD_ENTER;
    }
    return vkCode;
}

std::pair<Vk, bool> nativeKey(Vk vkCode)
{
    if (vkCode == vk::NUMPAD_ENTER) {
        return {vk::RETURN, true};
    }
    return {vkCode, isExtended(vkCode)};
}

QStringList allKeyNames()
{
    QStringList names;
    for (const KeyName &entry : kKeyTable) {
        names.append(QString::fromLatin1(entry.name));
    }
    for (char16_t c = u'a'; c <= u'z'; ++c) {
        names.append(QString(QChar(c)));
    }
    for (char16_t c = u'0'; c <= u'9'; ++c) {
        names.append(QString(QChar(c)));
    }
    for (int n = 1; n <= 24; ++n) {
        names.append(QStringLiteral("F%1").arg(n));
    }
    for (int n = 0; n <= 9; ++n) {
        names.append(QStringLiteral("Numpad%1").arg(n));
    }
    std::stable_sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.toUpper() < b.toUpper();
    });
    return names;
}

// ---------------------------------------------------------------------------
// 修饰键
// ---------------------------------------------------------------------------

std::optional<Modifiers> Modifiers::fromVk(Vk vkCode)
{
    switch (vkCode) {
    case vk::SHIFT:
    case vk::LSHIFT:
    case vk::RSHIFT:
        return Modifiers::Shift;
    case vk::CONTROL:
    case vk::LCONTROL:
    case vk::RCONTROL:
        return Modifiers::Ctrl;
    case vk::MENU:
    case vk::LMENU:
    case vk::RMENU:
        return Modifiers::Alt;
    case vk::LWIN:
    case vk::RWIN:
        return Modifiers::Win;
    default:
        return std::nullopt;
    }
}

QVector<Vk> Modifiers::vks() const
{
    QVector<Vk> out;
    if (contains(Ctrl)) {
        out.append(vk::LCONTROL);
    }
    if (contains(Alt)) {
        out.append(vk::LMENU);
    }
    if (contains(Shift)) {
        out.append(vk::LSHIFT);
    }
    if (contains(Win)) {
        out.append(vk::LWIN);
    }
    return out;
}

QStringList Modifiers::names() const
{
    QStringList out;
    if (contains(Ctrl)) {
        out.append(QStringLiteral("Ctrl"));
    }
    if (contains(Alt)) {
        out.append(QStringLiteral("Alt"));
    }
    if (contains(Shift)) {
        out.append(QStringLiteral("Shift"));
    }
    if (contains(Win)) {
        out.append(QStringLiteral("Win"));
    }
    return out;
}

QString Chord::render() const
{
    QStringList parts = mods.names();
    parts.append(nameFromKey(key));
    return parts.join(QLatin1Char('+'));
}

// ---------------------------------------------------------------------------
// 和弦解析
// ---------------------------------------------------------------------------

std::optional<KeyError> parseChord(const QString &input, Chord *out)
{
    const QString raw = input.trimmed();
    if (raw.isEmpty()) {
        return KeyError::empty();
    }

    Modifiers mods;
    bool passthrough = false;
    bool wildcard = false;
    QString rest = raw;

    // AutoHotkey 风格的前缀。
    while (!rest.isEmpty()) {
        const QChar c = rest.at(0);
        if (c == u'~') {
            passthrough = true;
            rest = rest.mid(1);
        } else if (c == u'*') {
            wildcard = true;
            rest = rest.mid(1);
        } else if (const auto prefix = modifierPrefix(c); prefix.has_value()) {
            mods = mods.unioned(*prefix);
            rest = rest.mid(1);
        } else {
            break;
        }
        if (rest.isEmpty()) {
            return KeyError::syntax(QStringLiteral("chord `%1` has no key").arg(raw));
        }
    }

    // `Ctrl+Alt+H` 形式；最后一个以 `+` 分隔的片段是按键本身。
    QStringList segments = rest.split(QLatin1Char('+'));
    const QString keySegment = segments.takeLast();
    for (const QString &rawSegment : segments) {
        const QString segment = rawSegment.trimmed();
        if (segment.isEmpty()) {
            return KeyError::syntax(QStringLiteral("chord `%1` has an empty modifier").arg(raw));
        }
        if (uppercaseLetterKey(segment).has_value()) {
            return KeyError::uppercaseLetter(segment);
        }
        const std::optional<Vk> asKey = keyFromName(segment);
        if (!asKey.has_value()) {
            return KeyError::unknownKey(segment);
        }
        if (const auto m = Modifiers::fromVk(*asKey); m.has_value()) {
            mods = mods.unioned(*m);
        } else {
            // `Ctrl+Foo+Bar` 不是和弦，而是笔误。
            return KeyError::syntax(
                QStringLiteral("`%1` in chord `%2` is not a modifier").arg(segment, raw));
        }
    }

    if (uppercaseLetterKey(keySegment).has_value()) {
        return KeyError::uppercaseLetter(keySegment.trimmed());
    }
    const std::optional<Vk> key = keyFromName(keySegment);
    if (!key.has_value()) {
        return KeyError::unknownKey(keySegment.trimmed());
    }

    // `Ctrl+Ctrl` 没有意义但无害：把作为按键的修饰键并入集合。
    if (const auto m = Modifiers::fromVk(*key); m.has_value() && mods.contains(*m)) {
        return KeyError::syntax(QStringLiteral("chord `%1` repeats modifier `%2` as its key")
                                    .arg(raw, nameFromKey(*key)));
    }

    out->mods = mods;
    out->key = *key;
    out->passthrough = passthrough;
    out->wildcard = wildcard;
    return std::nullopt;
}

bool sameKey(Vk chordKey, Vk vk)
{
    if (chordKey == vk) {
        return true;
    }
    if (chordKey == vk::SHIFT) {
        return vk == vk::LSHIFT || vk == vk::RSHIFT;
    }
    if (chordKey == vk::CONTROL) {
        return vk == vk::LCONTROL || vk == vk::RCONTROL;
    }
    if (chordKey == vk::MENU) {
        return vk == vk::LMENU || vk == vk::RMENU;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 发送脚本
// ---------------------------------------------------------------------------

SendOp SendOp::keyDown(Vk vkCode)
{
    SendOp op;
    op.kind = Kind::Key;
    op.vk = vkCode;
    op.down = true;
    return op;
}

SendOp SendOp::keyUp(Vk vkCode)
{
    SendOp op;
    op.kind = Kind::Key;
    op.vk = vkCode;
    op.down = false;
    return op;
}

SendOp SendOp::unicode(char16_t unit)
{
    SendOp op;
    op.kind = Kind::Text;
    op.text = unit;
    return op;
}

SendOp SendOp::sleep(std::uint32_t ms)
{
    SendOp op;
    op.kind = Kind::Sleep;
    op.ms = ms;
    return op;
}

std::optional<std::pair<Vk, bool>> charToKey(QChar c)
{
    const char16_t u = c.unicode();
    constexpr bool shift = true;
    constexpr bool plain = false;

    if (u >= u'a' && u <= u'z') {
        return std::make_pair(static_cast<Vk>(u - 32), plain);
    }
    if (u >= u'A' && u <= u'Z') {
        return std::make_pair(static_cast<Vk>(u), shift);
    }
    if (u >= u'0' && u <= u'9') {
        return std::make_pair(static_cast<Vk>(u), plain);
    }

    switch (u) {
    case u' ':
        return std::make_pair(vk::SPACE, plain);
    case u'\n':
    case u'\r':
        return std::make_pair(vk::RETURN, plain);
    case u'\t':
        return std::make_pair(vk::TAB, plain);
    case u'`':
        return std::make_pair(vk::OEM_3, plain);
    case u'~':
        return std::make_pair(vk::OEM_3, shift);
    case u'!':
        return std::make_pair(static_cast<Vk>(u'1'), shift);
    case u'@':
        return std::make_pair(static_cast<Vk>(u'2'), shift);
    case u'#':
        return std::make_pair(static_cast<Vk>(u'3'), shift);
    case u'$':
        return std::make_pair(static_cast<Vk>(u'4'), shift);
    case u'%':
        return std::make_pair(static_cast<Vk>(u'5'), shift);
    case u'^':
        return std::make_pair(static_cast<Vk>(u'6'), shift);
    case u'&':
        return std::make_pair(static_cast<Vk>(u'7'), shift);
    case u'*':
        return std::make_pair(static_cast<Vk>(u'8'), shift);
    case u'(':
        return std::make_pair(static_cast<Vk>(u'9'), shift);
    case u')':
        return std::make_pair(static_cast<Vk>(u'0'), shift);
    case u'-':
        return std::make_pair(vk::OEM_MINUS, plain);
    case u'_':
        return std::make_pair(vk::OEM_MINUS, shift);
    case u'=':
        return std::make_pair(vk::OEM_PLUS, plain);
    case u'+':
        return std::make_pair(vk::OEM_PLUS, shift);
    case u'[':
        return std::make_pair(vk::OEM_4, plain);
    case u'{':
        return std::make_pair(vk::OEM_4, shift);
    case u']':
        return std::make_pair(vk::OEM_6, plain);
    case u'}':
        return std::make_pair(vk::OEM_6, shift);
    case u'\\':
        return std::make_pair(vk::OEM_5, plain);
    case u'|':
        return std::make_pair(vk::OEM_5, shift);
    case u';':
        return std::make_pair(vk::OEM_1, plain);
    case u':':
        return std::make_pair(vk::OEM_1, shift);
    case u'\'':
        return std::make_pair(vk::OEM_7, plain);
    case u'"':
        return std::make_pair(vk::OEM_7, shift);
    case u',':
        return std::make_pair(vk::OEM_COMMA, plain);
    case u'<':
        return std::make_pair(vk::OEM_COMMA, shift);
    case u'.':
        return std::make_pair(vk::OEM_PERIOD, plain);
    case u'>':
        return std::make_pair(vk::OEM_PERIOD, shift);
    case u'/':
        return std::make_pair(vk::OEM_2, plain);
    case u'?':
        return std::make_pair(vk::OEM_2, shift);
    default:
        return std::nullopt;
    }
}

namespace {

void pressMods(QVector<SendOp> *ops, Modifiers mods)
{
    for (const Vk m : mods.vks()) {
        ops->append(SendOp::keyDown(m));
    }
}

void releaseMods(QVector<SendOp> *ops, Modifiers mods)
{
    QVector<Vk> keys = mods.vks();
    for (auto it = keys.crbegin(); it != keys.crend(); ++it) {
        ops->append(SendOp::keyUp(*it));
    }
}

void tap(QVector<SendOp> *ops, Modifiers mods, Vk vkCode, std::uint32_t times)
{
    const std::uint32_t count = times == 0 ? 1 : times;
    for (std::uint32_t i = 0; i < count; ++i) {
        pressMods(ops, mods);
        ops->append(SendOp::keyDown(vkCode));
        ops->append(SendOp::keyUp(vkCode));
        releaseMods(ops, mods);
    }
}

void typeUnicode(QVector<SendOp> *ops, QChar c)
{
    ops->append(SendOp::unicode(c.unicode()));
}

void emitChar(QVector<SendOp> *ops, Modifiers pending, QChar c)
{
    if (const auto mapped = charToKey(c); mapped.has_value()) {
        Modifiers mods = pending;
        if (mapped->second && !mods.contains(Modifiers::Shift)) {
            mods = mods.unioned(Modifiers::Shift);
        }
        tap(ops, mods, mapped->first, 1);
    } else {
        // US 布局之外的字符按 Unicode 输入。
        typeUnicode(ops, c);
    }
}

/// 按空白切分（等价于 Rust 的 `split_whitespace`）。
QStringList splitWhitespace(const QString &text)
{
    QStringList words;
    QString current;
    for (const QChar c : text) {
        if (c.isSpace()) {
            if (!current.isEmpty()) {
                words.append(current);
                current.clear();
            }
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty()) {
        words.append(current);
    }
    return words;
}

} // namespace

std::optional<KeyError> parseSendScript(const QString &input, QVector<SendOp> *out)
{
    out->clear();
    const qsizetype length = input.size();
    Modifiers pending;
    qsizetype i = 0;

    while (i < length) {
        const QChar c = input.at(i);

        if (const auto prefix = modifierPrefix(c); prefix.has_value()) {
            pending = pending.unioned(*prefix);
            ++i;
            continue;
        }

        if (c == u'{') {
            // AutoHotkey 中用于字面大括号的转义写法。
            if (i + 2 < length && input.at(i + 1) == u'{' && input.at(i + 2) == u'}') {
                emitChar(out, pending, u'{');
                pending = Modifiers();
                i += 3;
                continue;
            }
            if (i + 2 < length && input.at(i + 1) == u'}' && input.at(i + 2) == u'}') {
                emitChar(out, pending, u'}');
                pending = Modifiers();
                i += 3;
                continue;
            }

            qsizetype j = i + 1;
            QString body;
            while (j < length && input.at(j) != u'}') {
                body.append(input.at(j));
                ++j;
            }
            if (j >= length) {
                return KeyError::syntax(QStringLiteral("unterminated `{...}` in send script"));
            }
            i = j + 1;

            const QStringList words = splitWhitespace(body);
            const QString name = words.isEmpty() ? QString() : words.at(0);
            const std::optional<QString> arg = words.size() >= 2 ? std::optional<QString>(words.at(1))
                                                                 : std::nullopt;
            if (name.isEmpty()) {
                return KeyError::syntax(QStringLiteral("empty `{}` in send script"));
            }

            // `{S}` 里大括号中的字母是**键名**，所以必须小写。这与脚本里的裸
            // 字符不同 —— `send("A")` 里的 `A` 是文本，照旧表示“打出大写 A”。
            // 要显式按下 Shift 就把前缀写在花括号**外面**：`send("+s")`
            // （`+s` 是 Shift+S；`{+s}` 不是合法的键名）。
            if (uppercaseLetterKey(name).has_value()) {
                return KeyError::uppercaseLetter(name.trimmed());
            }

            if (name.compare(QLatin1String("text"), Qt::CaseInsensitive) == 0) {
                for (qsizetype k = i; k < length; ++k) {
                    typeUnicode(out, input.at(k));
                }
                i = length;
                pending = Modifiers();
                continue;
            }

            if (name.compare(QLatin1String("sleep"), Qt::CaseInsensitive) == 0) {
                if (!arg.has_value()) {
                    return KeyError::syntax(QStringLiteral("`{Sleep}` needs a millisecond count"));
                }
                bool ok = false;
                const std::uint32_t ms = arg->toUInt(&ok);
                if (!ok) {
                    return KeyError::syntax(QStringLiteral("invalid sleep duration `%1`").arg(*arg));
                }
                out->append(SendOp::sleep(ms));
                pending = Modifiers();
                continue;
            }

            // `{Esc}` 是按键名；`{!}` 是能“打出” `!` 的那个键，在 US 布局下
            // 即 Shift+1。AutoHotkey 也是这个行为。
            Vk vkCode = 0;
            bool symbolShift = false;
            if (const auto named = keyFromName(name); named.has_value()) {
                vkCode = *named;
            } else {
                if (name.size() != 1) {
                    return KeyError::unknownKey(name);
                }
                if (const auto mapped = charToKey(name.at(0)); mapped.has_value()) {
                    vkCode = mapped->first;
                    symbolShift = mapped->second;
                } else {
                    return KeyError::unknownKey(name);
                }
            }
            if (symbolShift && !pending.contains(Modifiers::Shift)) {
                // 修改外层的绑定，让待生效集合照常为下一个记号清空。
                pending = pending.unioned(Modifiers::Shift);
            }

            if (!arg.has_value()) {
                tap(out, pending, vkCode, 1);
                pending = Modifiers();
            } else {
                const QString dir = arg->toLower();
                if (dir == QLatin1String("down")) {
                    pressMods(out, pending);
                    out->append(SendOp::keyDown(vkCode));
                    pending = Modifiers();
                } else if (dir == QLatin1String("up")) {
                    out->append(SendOp::keyUp(vkCode));
                    releaseMods(out, pending);
                    pending = Modifiers();
                } else {
                    bool ok = false;
                    const std::uint32_t times = arg->toUInt(&ok);
                    if (!ok) {
                        return KeyError::syntax(
                            QStringLiteral("invalid repeat count `%1` for `{%2}`").arg(*arg, name));
                    }
                    if (times == 0) {
                        return KeyError::syntax(
                            QStringLiteral("repeat count for `{%1}` is zero").arg(name));
                    }
                    tap(out, pending, vkCode, times);
                    pending = Modifiers();
                }
            }
            continue;
        }

        emitChar(out, pending, c);
        pending = Modifiers();
        ++i;
    }

    if (!pending.isEmpty()) {
        return KeyError::syntax(QStringLiteral("send script ends with a dangling modifier"));
    }
    return std::nullopt;
}

std::optional<KeyError> parseKeyOrScript(const QString &input, QVector<SendOp> *out)
{
    const QString trimmed = input.trimmed();
    bool hasSyntax = false;
    for (const QChar c : trimmed) {
        const char16_t u = c.unicode();
        if (u == u'{' || u == u'}' || u == u'^' || u == u'!' || u == u'+' || u == u'#') {
            hasSyntax = true;
            break;
        }
    }
    if (!trimmed.isEmpty() && !hasSyntax) {
        if (const auto vkCode = keyFromName(trimmed); vkCode.has_value()) {
            out->clear();
            out->append(SendOp::keyDown(*vkCode));
            out->append(SendOp::keyUp(*vkCode));
            return std::nullopt;
        }
    }
    return parseSendScript(trimmed, out);
}

void splitHold(const QVector<SendOp> &ops, QVector<SendOp> *press, QVector<SendOp> *release)
{
    qsizetype cut = ops.size();
    while (cut > 0 && ops.at(cut - 1).kind == SendOp::Kind::Key && !ops.at(cut - 1).down) {
        --cut;
    }
    press->clear();
    release->clear();
    for (qsizetype i = 0; i < cut; ++i) {
        press->append(ops.at(i));
    }
    for (qsizetype i = cut; i < ops.size(); ++i) {
        release->append(ops.at(i));
    }
}

} // namespace flowkeyd::core
