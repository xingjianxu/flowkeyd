// 声明式动作的表示（`run`/`send`/`window`/`menu`……）与人类可读摘要。
//
// 动作刻意只能是数据：`--list` 要能把它显示出来，加载时就要能校验完，
// 而且它必须能安全地跨 钩子线程 → 工作线程 传递（见 AGENTS.md 第 7 节第 15 条）。
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace flowkeyd::core {

struct ActionSpec;

/// 把字符串渲染成 Rust `{:?}` 的样子（带引号、转义）。
///
/// `--list` 与帮助窗口显示的就是这些摘要，与 oskeyd 保持逐字一致，
/// 这样 e2e 脚本里的断言将来可以复用。
QString rustDebug(const QString &text);

// ---------------------------------------------------------------------------
// 动作相关的枚举
// ---------------------------------------------------------------------------

enum class ShowMode { Normal, Hidden, Minimized, Maximized };
enum class VolumeOp { Up, Down, Set, Mute, Unmute, Toggle };
enum class MediaOp { PlayPause, Next, Prev, Stop };
enum class ClipboardOp { Get, Set, Append, Clear };
/// `window` 动作能做的事。
///
/// 最后四个是 2026-09 新增的「把窗口挪到相邻的虚拟桌面 / 显示器」：
///   * `MovePrevDesktop` / `MoveNextDesktop` 只动虚拟桌面，窗口在显示器上的
///     几何完全不变，**首尾相接**（第一张再往前是最后一张）；
///   * `MoveLeftMonitor` / `MoveRightMonitor` 只动显示器（按 `sortedMonitors` 的
///     排列顺序），保留最大化状态，普通窗口保持大小并居中；没有更左/更右的
///     显示器时失败（不循环）。
/// 这四个都不套用 `toggle`，也不接受 `launch`；视图不会跟着窗口走（移动的是
/// 窗口，不是当前桌面）。
enum class WindowOp {
    Activate,
    Minimize,
    Maximize,
    Restore,
    Close,
    ToggleTopmost,
    MovePrevDesktop,
    MoveNextDesktop,
    MoveLeftMonitor,
    MoveRightMonitor,
};
enum class ToggleState { On, Off, Toggle };
enum class CapsLockState { Off };
enum class PowerOp { Sleep, Hibernate, Shutdown, Restart, Logoff, Lock, ScreenOff };

/// Rust `Debug` 风格的名字（摘要里用的就是它）。
QString showModeDebugName(ShowMode mode);
QString volumeOpDebugName(VolumeOp op);
QString mediaOpDebugName(MediaOp op);
QString clipboardOpDebugName(ClipboardOp op);
QString windowOpDebugName(WindowOp op);
QString toggleStateDebugName(ToggleState state);
QString capsLockStateDebugName(CapsLockState state);

/// 配置里书写的名字（snake_case）；`power` 用它，因为 oskeyd 的摘要也用它。
QString powerOpName(PowerOp op);

/// snake_case 名字 → 枚举；给 Lua 层与简写解析共用。
std::optional<ShowMode> showModeFromName(const QString &name);
std::optional<VolumeOp> volumeOpFromName(const QString &name);
std::optional<MediaOp> mediaOpFromName(const QString &name);
std::optional<ClipboardOp> clipboardOpFromName(const QString &name);
std::optional<WindowOp> windowOpFromName(const QString &name);
std::optional<ToggleState> toggleStateFromName(const QString &name);
std::optional<PowerOp> powerOpFromName(const QString &name);

// ---------------------------------------------------------------------------
// window 动作的辅助结构
// ---------------------------------------------------------------------------

/// `window` 动作如何选择目标窗口。
///
/// 空查询意味着“前台窗口”；否则出现的每个条件都必须匹配。
/// 匹配按 `EnumWindows` 的顺序进行（也就是 Z 序），因此最近使用过的
/// 匹配窗口会胜出。
struct WindowQuery
{
    /// 窗口标题的大小写无关子串。
    std::optional<QString> title;
    /// 可执行文件名的大小写无关子串。
    std::optional<QString> process;

    /// 按运行时的方式处理空白字符串（以及作为标题的 `"foreground"`/`"active"`）。
    static WindowQuery make(const std::optional<QString> &title, const std::optional<QString> &process);
    bool isForeground() const { return !title.has_value() && !process.has_value(); }
    /// 供日志行和校验错误使用的可读描述。
    QString describe() const;
};

/// `window` 动作里的 `launch = { ... }`：没有窗口匹配时要启动的程序。
/// 运行时会随后轮询等待新窗口并激活它。
struct LaunchSpec
{
    QString program;
    QStringList args;
    std::optional<QString> cwd;
    ShowMode show = ShowMode::Normal;
    /// 通过 `cmd.exe /C` 运行，并允许 shell 元字符。
    bool shell = false;
    QMap<QString, QString> env;
    /// 等待窗口出现的时间（默认 3000 ms）。
    std::optional<std::uint64_t> waitMs;
};

/// `launch = { ... }` 里**显式写了**哪些字段。
///
/// `app{...}` 的 `launch` 是默认值、动作里的 `launch` 是覆盖，两者逐字段合并
/// （见 `core/config.cpp` 的 `expandApps`）。但 `show` / `shell` / `args` 这些
/// 字段的“默认值”与“没写”在值上分不开，所以 Lua 层在转换时把写过的键记在这里。
/// C++ 里直接构造出来的 `LaunchSpec` 没有“没写”的概念，因此默认全为 true
/// （整份 `launch` 都算显式写的）。
struct LaunchFields
{
    bool program = true;
    bool args = true;
    bool cwd = true;
    bool show = true;
    bool shell = true;
    bool env = true;
    bool waitMs = true;
};

/// `menu` 动作里的一个条目。
struct MenuItemDef
{
    /// 单个字符的快捷键，例如 `"s"`；不区分大小写。
    std::optional<QString> key;
    /// 条目上显示的文本。
    QString label;
    /// 右侧的灰色副标题，例如英文名。
    std::optional<QString> hint;
    /// 选中后做什么；为空就只是把选单关掉。
    std::shared_ptr<ActionSpec> action;

    /// 规范化后的快捷键字符（小写）；校验通过后一定只有一个字符。
    std::optional<QChar> keyChar() const;
};

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

/// 一个具体动作。
///
/// 用「一个结构体 + Kind 判别式」而不是 `std::variant`：字段与 oskeyd 的
/// 同名枚举变体一一对应，但省掉了 20 个包装结构体与到处 `std::visit` 的噪音，
/// 而且 `menu` 的递归（Menu → MenuItemDef → ActionSpec → Action）天然成立。
struct Action
{
    enum class Kind {
        Run,
        Send,
        CapsLock,
        Type,
        Open,
        Volume,
        Media,
        Clipboard,
        Window,
        Notify,
        Desktop,
        Menu,
        Help,
        Power,
        Suspend,
        Reload,
        Quit,
        Noop,
    };

    Kind kind = Kind::Noop;

    // run
    QString program;
    QStringList args;
    std::optional<QString> cwd;
    ShowMode show = ShowMode::Normal;
    bool shell = false;
    bool wait = false;
    QMap<QString, QString> env;

    // send / type
    QString keys;
    QString text;
    std::optional<std::uint64_t> delayMs;
    std::optional<bool> releaseModifiers;

    // caps_lock
    CapsLockState capsState = CapsLockState::Off;

    // open
    std::optional<QString> openArgs;

    // volume
    VolumeOp volumeOp = VolumeOp::Up;
    std::optional<std::uint8_t> level;
    std::optional<std::uint8_t> step;

    // media
    MediaOp mediaOp = MediaOp::PlayPause;

    // clipboard
    ClipboardOp clipboardOp = ClipboardOp::Get;
    std::optional<QString> clipboardText;

    // window
    WindowOp windowOp = WindowOp::Activate;
    std::optional<QString> target;
    std::optional<QString> process;
    std::optional<LaunchSpec> launch;
    /// `launch` 表里显式写了哪些字段（与 app 的 `launch` 逐字段合并时用）。
    LaunchFields launchFields;
    /// `window` 动作顶层的 `wait_ms`：覆盖 `launch.wait_ms`。
    std::optional<std::uint64_t> waitMs;
    std::optional<bool> toggle;
    std::optional<bool> animate;
    /// 只对 `move_prev_desktop` / `move_next_desktop` 有意义的附加行为：搬完之后
    /// **把视图也切到目标桌面**并重新激活那个窗口（默认 `false`，即只搬窗口、
    /// 视图不动）。写在其它的 op 上会被 `--check` 拒绝。
    std::optional<bool> follow;

    // notify
    QString title;
    std::optional<QString> body;

    // desktop
    std::uint32_t desktopSwitch = 1;

    // menu / help
    std::vector<MenuItemDef> items;
    std::optional<QString> menuTitle;
    std::optional<QString> helpTitle;

    // power
    PowerOp powerOp = PowerOp::Sleep;

    // suspend
    ToggleState toggleState = ToggleState::Toggle;

    /// `--list` 与帮助窗口使用的人类可读摘要。
    QString summary() const;
};

/// 这个动作会不会造成「不该误触」的后果。
///
/// 判据只覆盖 `quit` / `suspend` / `power` 三类：从帮助窗口误触 `quit` 会让守护
/// 进程直接退出（得重新用管理员权限启动），误触 `power`（睡眠/关机/重启/注销/
/// 锁定/关屏）代价更高，`suspend` 会让之后所有快捷键都失灵（只剩 suspend 自己
/// 还能用）。`reload`、`window("close")` 这些不算 —— 它们顶多打断一下手头的事。
///
/// **`menu` 不算危险**：它只是把选单弹出来，真正的危险条目在选单里还有一次
/// 选择（用户的 `Win+X` 电源选单因此可以放心地从帮助窗口打开）。
bool isDestructive(const Action &action);
/// 列表里只要有一个危险动作就算危险（与 `--list` 一样按整条绑定看）。
bool isDestructive(const std::vector<Action> &actions);

// ---------------------------------------------------------------------------
// 简写
// ---------------------------------------------------------------------------

/// 解析 `action = "..."` 使用的 `"send:^{c}"` 简写。
///
/// 可识别的前缀：`run:`、`send:`、`type:`、`open:`、`notify:`、`volume:`、
/// `media:`、`clipboard:`、`window:`、`desktop:`、`power:`，以及裸关键字
/// `reload`、`quit`、`help`、`none`。
///
/// `menu:` 没有简写：一张选单至少要有条目，那写成一张表比塞进一个字符串清楚。
///
/// 出错时返回一条英文错误信息（与 oskeyd 的 `ConfigError::Validation` 一致）。
std::optional<QString> parseActionShorthand(const QString &input, Action *out);

} // namespace flowkeyd::core
