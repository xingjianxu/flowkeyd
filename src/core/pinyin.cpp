#include "core/pinyin.h"

#include "core/pinyin_data.h"

#include <algorithm>

namespace flowkeyd::core {

namespace {

/// 音节表里的第 `index` 个音节；越界（表坏了）时给空串，绝不去读数组之外。
QString syllableAt(std::uint16_t index)
{
    if (index == pinyin_data::kNoSyllable
        || index >= static_cast<std::uint16_t>(pinyin_data::kSyllableCount)) {
        return {};
    }
    return QString::fromLatin1(pinyin_data::kSyllables[index]);
}

} // namespace

std::vector<QString> pinyinReadings(char32_t codePoint)
{
    using namespace pinyin_data;

    if (codePoint < kFirstCodePoint || codePoint > kLastCodePoint) {
        return {};
    }
    const std::uint32_t slot = codePoint - kFirstCodePoint;
    const std::uint16_t primary = kPrimary[slot];
    if (primary == kNoSyllable) {
        return {};
    }

    std::vector<QString> readings;
    readings.push_back(syllableAt(primary));

    // `kExtra` 按 index 升序：二分到第一条，然后顺序读完这个字的那几条。
    const Extra *const begin = kExtra;
    const Extra *const end = kExtra + kExtraCount;
    const Extra *it = std::lower_bound(
        begin, end, slot, [](const Extra &entry, std::uint32_t value) {
            return entry.index < value;
        });
    for (; it != end && it->index == slot; ++it) {
        readings.push_back(syllableAt(it->syllable));
    }
    return readings;
}

} // namespace flowkeyd::core
