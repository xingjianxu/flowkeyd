// 配置的模式定义、校验与编译。
//
// 配置文件的**语法**由 `src/lua/lua_config.cpp` 负责（真正的 Lua 5.5.1），
// 这里只保留「配置长什么样」：C++ 结构体、严格校验规则，以及编译成
// 引擎直接可用的形式。
//
// 与 oskeyd 一样，**未知字段一律报错**（C++ 里没有 serde 的
// `deny_unknown_fields`，所以由 Lua 层逐字段核对白名单实现，见 AGENTS.md 第 7 节第 6 条）。
// 唯一的例外是动作表的顶层字段，两边都做不到，保持一致。
#pragma once

#include "core/action.h"
#include "core/keys.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace flowkeyd::core {

/// 配置失败。
class ConfigError
{
public:
    enum class Kind {
        /// 读不出来（不存在、权限……）。
        Io,
        /// Lua 求值或者 DSL 用法出错。
        Parse,
        /// 指到了一个 TOML 时代的配置文件：Lua 配置用的不是那个后缀。
        LegacyToml,
        /// 校验错误（一次列全）。
        Validation,
    };

    Kind kind = Kind::Validation;
    QString path;
    /// 只用于 `LegacyToml`：应该搬到的首选位置。
    QString preferred;
    /// 只用于 `Io` / `Parse`。
    QString message;
    /// 只用于 `Validation`。
    QStringList errors;

    static ConfigError makeIo(const QString &path, const QString &message);
    static ConfigError makeParse(const QString &path, const QString &message);
    static ConfigError makeLegacyToml(const QString &path, const QString &preferred);
    static ConfigError makeValidation(const QStringList &errors);

    /// 多行渲染（`--check` 与日志用的就是它）。
    QString toString() const;
    /// 单行渲染，用于把该错误嵌入报告时。
    QString oneLine() const;
};

// ---------------------------------------------------------------------------
// 配置里书写的枚举
// ---------------------------------------------------------------------------

/// 一个快捷键在什么时候触发。
///
/// 默认是 `Press`：和弦的最后一个按键一到就派发，**不**等修饰键
/// （尤其是 Windows 键）松开，而且只派发一次。
enum class TriggerMode { Press, Release, Repeat };

QString triggerModeName(TriggerMode mode);
std::optional<TriggerMode> triggerModeFromName(const QString &name);

/// `remap` 的语义：`hold`（默认）在按住 `from` 期间一直按住目标；
/// `tap` 每次按下只触发一次目标。
enum class RemapMode { Hold, Tap };

// ---------------------------------------------------------------------------
// settings / hotkey / remap
// ---------------------------------------------------------------------------

struct Settings
{
    /// `trace|debug|info|warn|error|off`；可被 `--log-level` 覆盖。
    QString logLevel = QStringLiteral("info");
    /// 全局默认：快捷键是否吞掉它匹配到的按键。
    bool swallow = true;
    /// 要求修饰键集合完全一致，而不是“至少包含这些”。
    bool exactModifiers = false;
    /// 在执行 `send`/`type` 动作前先松开用户按住的修饰键。
    bool releaseModifiers = true;
    /// `repeat = true` 的快捷键被按住时的默认重复间隔。
    std::uint32_t repeatIntervalMs = 50;
    /// `repeat = true` 的快捷键开始重复前的默认延迟。
    std::uint32_t repeatDelayMs = 400;
    /// 用于重复处理的引擎滴答周期。
    std::uint32_t tickMs = 15;
    /// 注入 API：`auto` / `user32` / `ntuser`。
    QString inputBackend = QStringLiteral("auto");
    /// 另一个 flowkeyd 已占用同一配置时拒绝启动。
    bool singleInstance = true;
    /// 守护进程模式启动时，如果没有管理员权限就自动以管理员身份重启。
    bool elevate = true;
};

/// 一个或多个动作。
///
/// 列表形式由 `List` 处理，字符串简写由 `Short` 处理，
/// 这样错误信息才能指名道姓。
struct ActionSpec
{
    enum class Kind {
        /// 完整的动作表，例如 `{ type = "run", program = "notepad.exe" }`。
        One,
        /// `action = "send:^{c}"` 风格的简写。
        Short,
        /// 一串动作。
        List,
    };

    Kind kind = Kind::One;
    Action action;
    QString shorthand;
    std::vector<ActionSpec> list;

    /// 把嵌套列表展平为将要执行的动作序列。
    /// 出错时返回一条英文错误信息。
    std::optional<QString> flatten(std::vector<Action> *out) const;
};

/// `repeatable = true` / `repeatable = { interval_ms = 40, delay_ms = 300 }`。
struct RepeatSpec
{
    enum class Kind { Flag, Config };

    Kind kind = Kind::Flag;
    bool flag = false;
    std::optional<std::uint32_t> intervalMs;
    std::optional<std::uint32_t> delayMs;

    static RepeatSpec makeFlag(bool value);
    static RepeatSpec makeConfig(std::optional<std::uint32_t> intervalMs,
                                 std::optional<std::uint32_t> delayMs);
};

/// 一条 `hotkey{...}`。
struct HotkeyDef
{
    /// 可选的标签，用于日志和 `{name}` 模板。
    std::optional<QString> name;
    /// 一个和弦或一组和弦，例如 `"Ctrl+Alt+H"` 或 `{"^!h", "^!j"}`。
    QStringList keys;
    /// 何时触发。
    std::optional<TriggerMode> trigger;
    /// 为该快捷键覆盖 `settings.swallow`。
    std::optional<bool> swallow;
    /// 按下时运行。也可写作 `press` 或 `on_press`。
    std::optional<ActionSpec> action;
    /// 松开时运行。
    std::optional<ActionSpec> onRelease;
    /// 正式名字是 `repeatable`，因为 `repeat` 是 Lua 关键字。
    std::optional<RepeatSpec> repeat;
    bool enabled = true;
    std::optional<QString> comment;
};

/// 一条 `remap{...}`。
struct RemapDef
{
    std::optional<QString> name;
    /// 源按键或和弦，例如 `"CapsLock"`。
    QString from;
    /// 目标，例如 `"Esc"` 或 `"^{c}"`。
    QString to;
    RemapMode mode = RemapMode::Hold;
    std::optional<bool> swallow;
    bool enabled = true;
};

/// `window_rule{...}` 里的目标显示器。
///
/// 三种写法：
///   * `monitor = 2`         —— 1 起，按「先左后右、再上后下」的排列顺序；
///   * `monitor = "primary"` —— 主显示器；
///   * `monitor = "DISPLAY2"` —— `EnumDisplayMonitors` 的设备名（可写全名
///     `\\.\DISPLAY2`，比较时会剥掉 `\\.\` 前缀并忽略大小写）。
struct MonitorRef
{
    enum class Kind { Index, Primary, Device };

    Kind kind = Kind::Index;
    /// `Kind::Index`：1 起的序号。
    std::uint32_t index = 1;
    /// `Kind::Device`：设备名。
    QString device;

    /// 供日志与摘要使用的可读形式。
    QString describe() const;
};

/// 一条 `window_rule{...}`：某个程序的窗口出现时放到哪个虚拟桌面 / 显示器。
///
/// 触发时机只有三个：窗口第一次出现、之前断开的显示器重新接上、以及 flowkeyd
/// 启动时对已有窗口过一遍。**之后不再干预**：用户自己移动/缩放窗口不会被纠正。
struct WindowRuleDef
{
    std::optional<QString> name;
    /// 窗口标题的大小写无关子串。
    std::optional<QString> title;
    /// 可执行文件名的大小写无关子串（与 `window` 动作的 `process` 一致）。
    std::optional<QString> process;
    /// 目标虚拟桌面序号（1 起，Task View 顺序）。
    std::optional<std::uint32_t> desktop;
    /// 把窗口钉在**所有**虚拟桌面上（`true`）或取消钉住（`false`）。
    /// 不写就是不去碰它。与 `desktop` 互斥。
    std::optional<bool> allDesktops;
    /// 让窗口始终在最上层（`true`）或取消置顶（`false`）。不写就是不去碰它。
    std::optional<bool> topmost;
    /// 目标显示器；不写就是窗口当前所在的那一个。
    std::optional<MonitorRef> monitor;
    /// 是否铺满目标显示器的工作区。默认：给了 `monitor`、又没写位置/大小时为 true。
    std::optional<bool> maximize;
    /// 相对目标显示器工作区左上角的偏移（像素）。
    std::optional<std::int32_t> x;
    std::optional<std::int32_t> y;
    /// 窗口大小（像素）。
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    bool enabled = true;
};

/// 一条 `app{...}`：把「同一个程序」的窗口摆放规则与快捷键写在一起。
///
/// 它本身不是新能力，只是**书写上的合并**：`compile()` 会把它展开成一条普通的
/// `WindowRuleDef` 与若干条 `HotkeyDef`，`process` / `title` / `name` 与 `launch`
/// 自动继承，不用在 `window_rule{}` 与 `window()` 动作里各写一遍。展开出来的条目
/// 排在全局 `hotkey{}` / `window_rule{}` 之后（先注册者仍然先匹配）。
struct AppDef
{
    /// 可选的标签；`process` / `title` 都省略时用于日志与 `--list`。
    std::optional<QString> name;
    /// 可执行文件名子串（与 `window_rule` / `window` 动作的 `process` 一致）。
    std::optional<QString> process;
    /// 窗口标题子串。
    std::optional<QString> title;
    /// 可选的窗口摆放规则；`process` / `title` / `name` 留空时由 app 补齐。
    std::optional<WindowRuleDef> window;
    /// 这个程序怎么启动：`hotkeys` 里的 `window()` 动作自动继承它，
    /// 于是动作只需要写 `window("activate")`（或只写要覆盖的 `wait_ms`）。
    std::optional<LaunchSpec> launch;
    /// 属于这个程序的快捷键；其中的 `window` 动作自动继承 app 的 `process` / `title`。
    std::vector<HotkeyDef> hotkeys;
    bool enabled = true;
};

/// 一个配置脚本收集到的原始内容（尚未校验）。
struct Config
{
    Settings settings;
    std::vector<HotkeyDef> hotkeys;
    std::vector<RemapDef> remaps;
    std::vector<WindowRuleDef> windowRules;
    std::vector<AppDef> apps;
};

// ---------------------------------------------------------------------------
// 编译结果
// ---------------------------------------------------------------------------

/// 按住不放时的重复行为。
struct Repeat
{
    std::uint32_t intervalMs = 50;
    std::uint32_t delayMs = 400;

    friend bool operator==(const Repeat &, const Repeat &) = default;
};

/// 解析并校验后的快捷键。
struct Binding
{
    QString name;
    std::vector<Chord> chords;
    bool swallow = true;
    TriggerMode trigger = TriggerMode::Press;
    std::vector<Action> press;
    std::vector<Action> release;
    std::optional<Repeat> repeat;
    std::optional<QString> comment;
};

/// 编译后的重映射：按下/松开两半已经预先切分好。
struct CompiledRemap
{
    QString name;
    Chord from;
    std::vector<SendOp> press;
    std::vector<SendOp> release;
    bool swallow = true;
};

/// 编译后的 `window_rule`。
///
/// `applyGeometry` 区分「这条规则只挪虚拟桌面」与「还要摆到某个显示器上」：
/// 只写 `desktop` 时窗口的大小/位置保持不动，不会因为默认最大化而突然被放大。
/// `allDesktops` / `topmost` 是另外两个与几何无关的开关（见 `WindowRuleDef`）。
struct WindowRule
{
    QString name;
    std::optional<QString> title;
    std::optional<QString> process;
    std::optional<std::uint32_t> desktop;
    std::optional<MonitorRef> monitor;
    /// 钉在所有虚拟桌面上（`true`）/ 取消钉住（`false`）；不写就是不去碰它。
    std::optional<bool> allDesktops;
    /// 始终在最上层（`true`）/ 取消置顶（`false`）；不写就是不去碰它。
    std::optional<bool> topmost;
    /// 是否调整窗口的几何（写了 `monitor` / `maximize` / `x` / `y` / `width` / `height`）。
    bool applyGeometry = false;
    /// 铺满目标显示器的工作区（`applyGeometry` 为 false 时无意义）。
    bool maximize = false;
    std::optional<std::int32_t> x;
    std::optional<std::int32_t> y;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;

    /// 一行人类可读的摘要（`--list` 与日志用）。
    QString summary() const;
};

/// 完全校验通过、可直接交给引擎的配置。
///
/// **与 Lua 完全无关**：钩子回调与工作线程永远碰不到 `lua_State`
/// （见 AGENTS.md 第 7 节第 14 条）。
struct Compiled
{
    Settings settings;
    std::vector<Binding> bindings;
    std::vector<CompiledRemap> remaps;
    std::vector<WindowRule> windowRules;
    QString source;
    /// 值得告知用户的非致命问题。
    QStringList warnings;
};

/// 校验并编译已经收集好的配置。
///
/// `errors` / `warnings` 是脚本求值阶段攒下的条目级错误与提醒；
/// 它们和这里的校验错误一起报告，这样用户改一次就能看到全部问题。
std::optional<ConfigError> compile(const Config &config,
                                   const QString &path,
                                   QStringList errors,
                                   QStringList warnings,
                                   Compiled *out);

/// 一个动作的加载期校验。
void validateAction(const QString &label, const Action &action, QStringList *errors);

// ---------------------------------------------------------------------------
// 文件装载
// ---------------------------------------------------------------------------

/// Lua 层的求值结果：原始配置加上条目级错误/提醒。
struct EvalResult
{
    Config config;
    QStringList errors;
    QStringList warnings;
};

/// Lua 层注入的求值回调。`text` 已经剥掉了 UTF-8 BOM。
using Evaluator = std::function<std::optional<ConfigError>(const QString &text,
                                                           const QString &path,
                                                           EvalResult *out)>;

/// 读取、求值并校验一个配置文件。
///
/// 提权/日志一类的副作用都不在这里：`--check` / `--list` 直接调用它即可。
std::optional<ConfigError> loadConfig(const Evaluator &evaluator,
                                      const QString &path,
                                      Compiled *out);

/// 路径看起来像旧版的 TOML 配置吗？
bool isToml(const QString &path);

/// 剥掉 UTF-8 BOM（字节层的 `EF BB BF`，或解码后的 U+FEFF）。
QString stripUtf8Bom(const QString &text);
QByteArray stripUtf8Bom(const QByteArray &bytes);

// ---------------------------------------------------------------------------
// 位置
// ---------------------------------------------------------------------------

/// 用户主目录：优先 `%USERPROFILE%`，其次 `HOME`。
std::optional<QString> homeDir();

/// 首选（也是默认的）配置位置：`.config\flowkeyd\config.lua`。
QString preferredConfigPath();

/// 隐藏控制台（托盘模式）时默认使用的日志文件：`.config\flowkeyd\flowkeyd.log`。
QString preferredLogPath();

/// 还留在磁盘上的旧版 TOML 配置（拿来提示迁移）。
std::optional<QString> staleTomlConfig();

/// `configPathCandidates()` 的纯函数实现：首选位置在前，
/// 之后依次是 exe 同目录、`%APPDATA%\flowkeyd`，最后是当前目录。
QStringList candidateList(const QString &preferred,
                          const std::optional<QString> &exeDir,
                          const std::optional<QString> &appdata);

/// 缺省 `--config` 时尝试的配置位置，按优先级排列。
QStringList configPathCandidates();

/// 取候选列表中第一个存在的文件；都不存在时返回首选位置，
/// 这样报错信息会指向用户应该创建的那个文件。
QString pickConfigPath(const QStringList &candidates);

/// 缺省 `--config` 时使用的配置文件。
QString defaultConfigPath();

} // namespace flowkeyd::core
