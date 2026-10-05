// 程序启动器（`apps` 动作）的**纯逻辑**：条目类型、整理（去重/排序）、
// 名字匹配与图标键。
//
// 与 `core/` 里别的东西一样，这里不碰 Win32、不碰 Qt GUI：开始菜单的扫描
// （`platform/win/apps.*`，枚举 `shell:AppsFolder`）只负责把 shell 的条目变成
// `AppEntry`，「什么该留下、按什么顺序」全在这里，`tst_app_list` 直接覆盖。
//
// 图标**不在**这里，也不在模型层：`QImage` 属于 QtGui（`flowkeyd_models` 只
// 链接 QtCore）。模型只给每一行一个 URL（`appIconUrl()`），真正的像素由
// `app::AppIconProvider` 按需要从 shell 里取（见 `src/app/app_icons.h`）。
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace flowkeyd::core {

/// 开始菜单里的一个程序。
///
/// 平台层扫出来的原始数据；「只保留程序、丢掉卸载程序」那两条过滤已经在扫描时
/// 做完（判据就在本文件末尾）。
struct AppEntry
{
    /// 显示名（`SIGDN_NORMALDISPLAY`）—— 与开始菜单里看到的那一个一致，
    /// 所以它跟系统语言走（`File Explorer` 在中文系统上是「文件资源管理器」）。
    QString name;
    /// 启动用的 **shell 解析名**：`shell:AppsFolder\<AppUserModelID>`。
    ///
    /// 启动（`platform/win/apps::launchApp()`）、取图标
    /// （`IShellItemImageFactory`）、右键菜单（`shell_menu::showItemMenu()`）
    /// 全都拿它当输入；`appIconKey()` 也是对**它**算的（所以同一个程序永远是
    /// 同一个图标 URL）。老版本这里存的是快捷方式路径，那张卡片只认「一个可以
    /// 交给 shell 的字符串」这一点没变。
    QString launch;
    /// 这个条目解析出来的目标（`PKEY_Link_TargetParsingPath`）。
    ///
    /// **只用于判据与诊断**（是不是程序 / 是不是卸载程序 / 日志）：启动走 `launch`，
    /// 不碰目标 —— 商店应用没有目标、`.msc` 与「以管理员身份运行」这些语义也只有
    /// shell 自己拿得住。空串是常态（商店应用、部分虚拟项）。
    QString target;
    /// 条目里写好的启动参数（`PKEY_Link_Arguments`，**原样的一整串**，不切分）。
    /// 同样只用于诊断：启动由 shell 负责，参数它自己会给目标。
    QString arguments;
};

/// 图标 URL 里用的稳定短键。
///
/// 为什么不用启动名（`shell:AppsFolder\<AUMID>`）本身：`Image.source` 是 URL，
/// 反斜杠 / 空格 / 中文都得转义，而 Qt 的 `image://` 提供者拿到的是**已经解码**
/// 的 id，双方对「编码了几次」很容易不一致。改成对它算一个 64 位 FNV-1a
/// （小写十六进制，16 个字符）之后，id 只有 `[0-9a-f]`，而且**同一个程序永远是
/// 同一个 URL**：列表重扫、条目换位置都不会让 QML 的图片缓存认错图标。
///
/// 大小写无关（Windows 的路径本来就不区分大小写），分隔符统一成 `\`。
QString appIconKey(const QString &launchName);

/// QML 里 `Image.source` 用的前缀：`image://flowkeyd-app/<appIconKey()>`。
QString appIconUrl(const QString &launchName);

/// 这一条**取图标**时该用哪个名字（默认就是启动名本身）。
///
/// 唯一的例外是内置的「Windows 设置」页面（`ms-settings:…`）：那些 URI 在 shell
/// 里既没有本地化名字、也取不到图标（`SIGDN_NORMALDISPLAY` 给回来的就是 URI
/// 本身），所以图标借「设置」应用自己的那一个 —— 一排齿轮总比一格空白强。
///
/// 注意它与 `appIconKey()` 的分工：**键（身份、图片缓存的 URL id）永远来自启动名
/// 本身**（每个设置页各一个键，固定 / 最近使用互不干扰），这里只决定「上哪儿去
/// 取像素」。
QString appIconLaunchName(const QString &launchName);

/// 整理一份扫描结果：丢掉没有名字/没有启动名的、按「名字 + 启动名」去重、
/// 按名字排序。
///
/// 去重只合并**同名同启动名**的条目（这只是第二层保险：`shell:AppsFolder`
/// 本来就不会把同一个应用列两遍）；名字不同的绝不合并 —— 两个名字不同、却指向
/// 同一个 exe 的条目是两条独立的入口（`Developer PowerShell for VS` 与
/// `Debuggable Package Manager` 就是这样）。
std::vector<AppEntry> prepareAppEntries(std::vector<AppEntry> entries);

/// 把 `extra` 里的条目接到 `base` 后面，**丢掉名字与 `base`（或前面已经接上的
/// 条目）重复的那些**。
///
/// 为什么需要它：启动器的目录来自好几个地方（`shell:AppsFolder` 的开始菜单、
/// 控制面板的「所有控制面板项」、内置的设置页），来源之间会有重名的入口
/// （真机上「Windows 工具」在开始菜单与控制面板里各有一条），两个一模一样的名字
/// 会让人以为列表坏了。开始菜单那一份接在**前面**，所以保留它。
///
/// 判据只有名字（大小写无关、去掉首尾空白）；**不碰 `prepareAppEntries()` 那条
/// 「名字 + 启动名」的去重规则** —— 那条是整理一份扫描结果用的，名字不同就是两条
/// 独立的入口，这里要的是「两个来源之间不要出现同名」。
std::vector<AppEntry> appendLauncherEntries(std::vector<AppEntry> base,
                                            const std::vector<AppEntry> &extra);

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

/// `appSortInfo()` 的结果：排序键与它对应的分组表头。
///
/// 两者是一次扫描出来的，模型装载一批程序时只要调一次 —— 分开调
/// `appSortText()` + `appGroupLetter()` 会把名字扫描两遍。
struct AppSortInfo
{
    /// 首字符是字母时前面加 `1`、否则加 `0`，后面接**主读音全拼**（小写）。
    ///
    /// 为什么排序键要带那个 `0`/`1` 前缀：分组是按首字符来的（`0` 开头的一律归
    /// 「#」那一组），前缀保证「数字 / 符号 / 表外汉字」开头的条目**排在一起**
    /// （`7-Zip`、`【小狼毫】…` 都在最前面那一组），而字母开头的按拼音 / 字母
    /// 顺序排在后面。少了这个前缀，`【` 这种码点比 `z` 还大的符号会把「#」组
    /// 撕成两段。
    ///
    /// 排序键本身已经包含了分组信息，所以「全部程序」列表只要按它排一遍，同一组
    /// 的条目天然是连续的。
    QString sortText;
    /// 分组表头：首字母大写（`A`–`Z`），或者 `"#"`。
    ///
    /// 与 Windows 10 开始菜单的「所有应用」一致：中文名字用拼音首字母
    /// （`微信` → `W`），拉丁名字用它的第一个字母（`Visual Studio Code` → `V`），
    /// 首字符不是字母的（数字、符号、拼音表外的生僻字）都归到 `"#"`
    /// （它们也总是排在字母组前面）。
    QString letter;
};

/// 算一个程序名的排序键与分组表头（见 `AppSortInfo`；为了效率一次算完）。
AppSortInfo appSortInfo(const QString &name);

/// `appSortInfo(name).sortText`。
QString appSortText(const QString &name);

/// `appSortInfo(name).letter`。
QString appGroupLetter(const QString &name);

/// 程序名匹配：大小写无关的**子串**（首尾空白先去掉；空串匹配一切）。
///
/// 用子串而不是前缀（窗口切换器那边用的是前缀）：启动器要的是「记得名字里的
/// 一部分就能找到它」（`code` 命中 `Visual Studio Code`）。
/// 判据就是 `appSearchText()` 里那三段（名字 / 全拼 / 首字母），所以 `jishiben`、
/// `jsb`、`vsc` 都能命中对应的程序。
bool appNameMatches(const QString &name, const QString &needle);

/// 一个开始菜单条目算不算「程序」。
///
/// 判据是**目标**（与商店应用的 AUMID）而不是名字 —— 名字里带“卸载”/“帮助”的
/// 仍然可能是程序，反过来也有名字完全看不出内容的情况：
///  * 目标是一个可以启动的程序 / 管理单元 / 控制面板项（`.exe`、`.bat`、`.cmd`、
///    `.msc`、`.cpl`，大小写无关、允许尾随空白）；
///  * 目标是 shell 的虚拟项（以 `::{` 开头的 GUID，例如「文件资源管理器」
///    `::{52205FD8-…}`、「控制面板」、「运行」）；
///  * 目标为空但 AUMID 是**商店应用**的（`<包家族名>!<AppId>` 这种形状）——
///    商店/UWP 应用根本没有目标路径。
///
/// 其它一律不算：文档与帮助（`.chm`/`.txt`/`.url`）、网址（`http(s)://`、
/// `steam://`）、文件夹、坏掉的条目。
///
/// 注意目标可能是「已知文件夹 GUID + 相对路径」这种 shell 写法
/// （`{1AC14E77-…}\services.msc`），所以判据只能看**末尾的扩展名**，
/// 不要拿去 `QFileInfo` 之类的地方当真路径。
bool appTargetIsProgram(const QString &target, const QString &appUserModelId);

/// 这个条目看起来是**卸载程序**吗（名字或目标）。
///
/// 开始菜单里混着「卸载微信」「Uninstall Qt」这类反向操作条目（它们确实是指向
/// exe 的程序，所以「是不是程序」那条过滤放它们过去），而启动器里列出来只会
/// 碍事、还容易误按，所以单独丢掉：
///  * 名字里出现「卸载」，或者是一个独立的 `uninstall` 词（`Uninstall Qt`、
///    `Uninstaller`、`uninstall foo`）；
///  * 目标文件名是安装器生成的反向操作程序（`unins000.exe`、`uninst.exe`、
///    `unwise.exe`、`uninstall*.exe`）。
///
/// 只看名字与目标文件名，**不猜**「Setup」「维护工具」这类 —— 那些常常也是正常
/// 入口（`Visual Studio Installer`、「配置工具」）。
bool appLooksLikeUninstaller(const QString &name, const QString &target);

// ---------------------------------------------------------------------------
// 内置的「Windows 设置」页面（`ms-settings:`）
// ---------------------------------------------------------------------------
//
// 控制面板里的条目（「网络连接」「电源选项」「程序和功能」……）**不在**这里：
// 那一份由 `platform/win/apps.cpp` 枚举「所有控制面板项」得到，名字、图标、启动
// 全都能交给 shell（因此自动跟着系统语言走）。
//
// 现代「设置」应用的那些页面（`ms-settings:`）在 shell 里**没有名字也没有图标**
// ——`SHCreateItemFromParsingName("ms-settings:display")` 能建出条目，但
// `SIGDN_NORMALDISPLAY` 返回的就是 URI 本身 —— 所以只能自己带一份中英对照。

/// 内置的一条「Windows 设置」页面。
struct SettingsPage
{
    /// `ms-settings:` URI：既是启动名，也是这一条的**稳定身份**（图标键）。
    const char *uri;
    /// 中文名（**不带**前缀，前缀由 `builtinSettingsEntries()` 加）。
    const char *nameZh;
    /// 英文名（同上，不带前缀）。
    const char *nameEn;
};

/// 内置设置页名字的前缀（中文 / 英文）。
///
/// 有前缀这些条目在「全部程序」列表里会**聚在同一组**（`设置…` 的拼音首字母都是
/// `S`），而且输入「设置」/`settings` 就能把它们一次全筛出来。
inline constexpr char kSettingsNamePrefixZh[] = "设置：";
inline constexpr char kSettingsNamePrefixEn[] = "Settings: ";

/// 内置的常用设置页表（顺序 = 显示时的顺序）。
const std::vector<SettingsPage> &windowsSettingsPages();

/// 界面语言列表（`QLocale::system().uiLanguages()` 的形状）里的**第一项**是不是
/// 中文。空列表按「不是中文」算（英文名是兜底）。
bool settingsNamesAreChinese(const QStringList &uiLanguages);

/// 把内置设置页编译成启动器条目：名字 = 前缀 + 按语言选的名字，启动名 = URI。
std::vector<AppEntry> builtinSettingsEntries(const QStringList &uiLanguages);
/// 同上，但用系统界面语言（`QLocale::system().uiLanguages()`）。
std::vector<AppEntry> builtinSettingsEntries();

} // namespace flowkeyd::core
