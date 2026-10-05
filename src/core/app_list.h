// 程序启动器（`apps` 动作）的**纯逻辑**：条目类型、整理（去重/排序）、
// 名字匹配与图标键。
//
// 与 `core/` 里别的东西一样，这里不碰 Win32、不碰 Qt GUI：开始菜单的扫描
// （`platform/win/apps.*`）只负责把目录里的 `.lnk` 变成 `AppEntry`，
// 「什么该留下、按什么顺序」全在这里，`tst_app_list` 直接覆盖。
//
// 图标**不在**这里，也不在模型层：`QImage` 属于 QtGui（`flowkeyd_models` 只
// 链接 QtCore）。模型只给每一行一个 URL（`appIconUrl()`），真正的像素由
// `app::AppIconProvider` 按需要从 shell 里取（见 `src/app/app_icons.h`）。
#pragma once

#include <QString>

#include <vector>

namespace flowkeyd::core {

/// 开始菜单里的一个程序。
///
/// 平台层扫出来的原始数据；「只保留程序」那条过滤已经在扫描时做完
/// （目标是以 `.exe` 结尾的，或者只有 IDList 的商店/UWP 条目）。
struct AppEntry
{
    /// 显示名：快捷方式的文件名去掉 `.lnk`（开始菜单里看到的就是它）。
    QString name;
    /// 相对 `...\Start Menu\Programs` 的子目录（根目录下的是空串）。
    /// 只用来做去重时的偏好（浅的那一份优先）与日志，不显示。
    QString group;
    /// 快捷方式自己的完整路径；启动就是 `ShellExecute` 它（与开始菜单一致，
    /// 所以参数、工作目录、`runas` 标记都照旧生效）。
    QString shortcut;
    /// 解析出来的目标程序；只有 IDList 的条目是空串。
    QString target;
    /// 快捷方式里写好的启动参数（**原样的一整串**，不切分）。只用于去重与诊断：
    /// 启动走的是快捷方式本身，shell 会自己把参数交给目标。
    QString arguments;
};

/// 图标 URL 里用的稳定短键。
///
/// 为什么不用快捷方式路径本身：`Image.source` 是 URL，反斜杠 / 空格 / 中文
/// 都得转义，而 Qt 的 `image://` 提供者拿到的是**已经解码**的 id，双方对
/// 「编码了几次」很容易不一致。改成对路径算一个 64 位 FNV-1a（小写十六进制，
/// 16 个字符）之后，id 只有 `[0-9a-f]`，而且**同一个程序永远是同一个 URL**：
/// 列表重扫、条目换位置都不会让 QML 的图片缓存认错图标。
///
/// 大小写无关（Windows 的路径本来就不区分大小写），分隔符统一成 `\`。
QString appIconKey(const QString &shortcutPath);

/// QML 里 `Image.source` 用的前缀：`image://flowkeyd-app/<appIconKey()>`。
QString appIconUrl(const QString &shortcutPath);

/// 整理一份扫描结果：丢掉没有名字/没有快捷方式的、按「名字 + 目标」去重、
/// 按名字排序。
///
/// 去重只合并**同名同目标**的条目（同一个程序在「全局开始菜单」与「当前用户
/// 开始菜单」里各一份是常态）；名字不同的绝不合并 —— 两个名字不同、却指向
/// 同一个 exe 的条目是两条独立的入口（`Developer PowerShell for VS` 与
/// `Debuggable Package Manager` 就是这样）。
/// 同名同目标时保留**层级更浅**的那一份（根目录优先于子目录）。
std::vector<AppEntry> prepareAppEntries(std::vector<AppEntry> entries);

/// 拼音/首字母搜索最多展开几种读音组合：超过就退回「每个字只用主读音」。
///
/// 常见名字的组合数是个位数（多音字本来就少）；8 是为了挡住那种“名字里恰好
/// 塞了好几个多音字”的极端情况，免得一次筛选要拼上百个字符串。
inline constexpr int kMaxSearchVariants = 8;

/// 程序名的**搜索文本**（小写），筛选就是「拿输入串去 `contains()` 它」。
///
/// 由三段拼成，中间用 `U+001F` 隔开（用户打不出这个字符，所以段与段之间
/// 不会拼出假匹配）：
///   1. 名字本身（与加拼音之前的行为完全一致：`code` 命中 `Visual Studio Code`）；
///   2. **全拼**的几种写法（`记事本` → `jishiben`，`网易云音乐` → 同时给出
///      `wangyiyunyinyue` 与 `wangyiyunyinle`）；
///   3. **首字母缩写**（`记事本` → `jsb`，`Visual Studio Code` → `vsc`，
///      `QQ音乐` → `qyy`；空格与标点不进这一项）。
///
/// 汉字读音来自 `core/pinyin.*`（去声调；多音字的每一种读音都进第 2/3 段）。
/// 读音组合数是每个字读音数的乘积，**超过 `kMaxSearchVariants` 就只用主读音**
/// —— 长名字不该为了一两个多音字炸出几百个变体。
///
/// 表外的汉字（Ext A/B 等生僻字）没有拼音，与空格、标点一样按字面进第 1/2 段。
QString appSearchText(const QString &name);

/// 程序名匹配：大小写无关的**子串**（首尾空白先去掉；空串匹配一切）。
///
/// 用子串而不是前缀（窗口切换器那边用的是前缀）：启动器要的是「记得名字里的
/// 一部分就能找到它」（`code` 命中 `Visual Studio Code`）。
/// 判据就是 `appSearchText()` 里那三段（名字 / 全拼 / 首字母），所以 `jishiben`、
/// `jsb`、`vsc` 都能命中对应的程序。
bool appNameMatches(const QString &name, const QString &needle);

/// 一个开始菜单快捷方式算不算「程序」。
///
/// 判据是**目标**而不是名字（名字里带 “卸载”/“帮助” 的仍然是程序，反过来也有
/// 名字看不出内容的情况）：
///  * 目标以 `.exe` 结尾（大小写无关，允许尾随空白）—— 绝大多数快捷方式；
///  * 目标为空但**有 IDList** —— 商店/UWP 应用用它（`explorer.exe shell:AppsFolder\…`
///    是另一种写法，落到上一条）。
/// 除此之外（文件夹、文档、网址、坏掉的快捷方式）都不算。
bool appTargetIsProgram(const QString &target, bool hasIdList);

} // namespace flowkeyd::core
