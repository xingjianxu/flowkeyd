// 汉字读音表的**数据接口**（表本身是生成的：`src/core/pinyin_data.cpp`，
// 生成器 `tools/pinyin_gen.ps1`）。
//
// 这一层只有裸数据：怎么找、怎么用（组合成拼音、首字母）在 `core/pinyin.*`。
// `core/` 的规矩照旧 —— 不碰 Win32、不碰 Qt GUI，所以这张表也能被单测直接跑。
//
// 数据来源：mozillazg/pinyin-data（MIT License，读音源自 Unicode Unihan 的
// kMandarin 字段）。**只有 U+4E00–U+9FFF**：Ext A/B 这类生僻字不进表
// （开始菜单里不会出现，进了表体积要翻一倍多），查不到就当“没有拼音”。
#pragma once

#include <cstdint>

namespace flowkeyd::core::pinyin_data {

/// 表里的读音下标；没有读音时用它。
inline constexpr std::uint16_t kNoSyllable = 0xFFFF;

/// 读区间：`cp - kFirstCodePoint` 就是 `kPrimary` 的下标。
inline constexpr std::uint32_t kFirstCodePoint = 0x4E00;
inline constexpr std::uint32_t kLastCodePoint = 0x9FFF;

/// 去声调、去重、ASCII 升序的音节表（`"ji"`、`"zhong"`……）。
extern const char *const kSyllables[];
extern const int kSyllableCount;

/// U+4E00..U+9FFF 的主读音（多音字的第一个读音），`kNoSyllable` = 没有读音。
extern const std::uint16_t kPrimary[];
extern const int kPrimaryCount;

/// 主读音之外的那些读音（多音字才会出现，比如 乐 = `le` + `yue`）。
///
/// 按 `index` 升序排列（同一个汉字的几条挨在一起），可以二分之后顺序读完。
struct Extra
{
    /// 码点 - `kFirstCodePoint`。
    std::uint16_t index;
    /// `kSyllables` 的下标。
    std::uint16_t syllable;
};
extern const Extra kExtra[];
extern const int kExtraCount;

} // namespace flowkeyd::core::pinyin_data
