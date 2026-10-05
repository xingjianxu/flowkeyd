// 汉字读音查询：程序启动器的拼音筛选靠它（「按汉语拼音搜索程序」）。
//
// 表在 `core/pinyin_data.*`（生成的）。这一层只做三件事：把码点映射到下标、
// 把主读音与多音字的补充读音拼成一个列表、把结果交给调用方。
//
// 只读、无状态、不碰 Win32 / Qt GUI —— 所以 `tst_app_list` 能直接把整张表跑一遍
// 做自检（读音必须是 `[a-z]+`、同一个字不能给出重复读音、覆盖率不能塌）。
#pragma once

#include <QString>

#include <vector>

namespace flowkeyd::core {

/// 一个汉字的**全部读音**（去声调、小写 ASCII；`ü` 记作 `v`，并且额外给出写成
/// `u` 的那一条，所以 女 → `{"nv", "nu"}`）。
///
/// 第一个是主读音（多音字里最常用的那个）。表外的码点返回空表：非汉字、
/// Ext A/B 这些没进表的生僻字、以及少数没有读音记录的汉字都走这条路 ——
/// 调用方按「这个字没有拼音」处理。
std::vector<QString> pinyinReadings(char32_t codePoint);

} // namespace flowkeyd::core
