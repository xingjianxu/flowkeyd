#include "app/dispatcher.h"

#include "app/popup_host.h"
#include "app/runtime.h"
#include "core/keys.h"
#include "core/placement.h"
#include "core/template.h"
#include "core/window_match.h"
#include "platform/win/apps.h"
#include "platform/win/audio.h"
#include "platform/win/clipboard.h"
#include "platform/win/desktop.h"
#include "platform/win/hook.h"
#include "platform/win/input.h"
#include "platform/win/logging.h"
#include "platform/win/monitor.h"
#include "platform/win/power.h"
#include "platform/win/process.h"
#include "platform/win/window.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

#include <optional>
#include <utility>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

namespace {

/// 模板里 `{date}` / `{time}` 用的是本地墙上时钟。
core::LocalTime localTimeNow()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    core::LocalTime time;
    time.year = now.wYear;
    time.month = now.wMonth;
    time.day = now.wDay;
    time.hour = now.wHour;
    time.minute = now.wMinute;
    time.second = now.wSecond;
    return time;
}

/// 日志行里用的截断（保留前 `limit` 个字符，其余用省略号）。
QString truncateText(const QString &text, int limit)
{
    if (text.size() <= limit) {
        return text;
    }
    return text.left(limit) + QChar(0x2026);
}

/// 帮助窗口里那一行灰色小字：这个快捷键按下/松开时到底做什么。
///
/// 配置里的 `comment` 是给人看的一句话，而动作摘要是机器生成的；两者都列出来，
/// 用户才能既认出快捷键、又知道它到底会干什么。
std::optional<QString> bindingDetail(const core::Binding &binding)
{
    QStringList parts;
    for (const core::Action &action : binding.press) {
        parts.append(action.summary());
    }
    for (const core::Action &action : binding.release) {
        parts.append(action.summary());
    }
    switch (binding.trigger) {
    case core::TriggerMode::Repeat:
        parts.append(QStringLiteral("repeat"));
        break;
    case core::TriggerMode::Release:
        parts.append(QStringLiteral("on release"));
        break;
    case core::TriggerMode::Press:
        break;
    }
    if (parts.isEmpty()) {
        return std::nullopt;
    }
    return parts.join(QStringLiteral(" ; "));
}

/// 把重映射的目标渲染成 `Ctrl+C` 这样的可读形式（只看按下那半程，
/// 因为松开那半程就是它的逆序）。
QString describeRemapTarget(const core::CompiledRemap &remap)
{
    QStringList names;
    for (const core::SendOp &op : remap.press) {
        switch (op.kind) {
        case core::SendOp::Kind::Key:
            if (op.down) {
                names.append(core::nameFromKey(op.vk));
            }
            break;
        case core::SendOp::Kind::Text:
            names.append(QString(QChar(op.text)));
            break;
        case core::SendOp::Kind::Sleep:
            names.append(QStringLiteral("Sleep %1").arg(op.ms));
            break;
        }
    }
    if (names.isEmpty()) {
        return QStringLiteral("—");
    }
    return names.join(QLatin1Char('+'));
}

/// 弹出 `menu` 动作的选单。
///
/// 选单窗口在 GUI 线程上，用户选中后才把那一项的动作回投给**工作线程**：
/// 弹窗本身从不执行动作。
void openMenuAction(Runtime *runtime,
                    Dispatcher *dispatcher,
                    const std::shared_ptr<const core::Compiled> &config,
                    const QString &hotkey,
                    const core::Action &action)
{
    // 把每一项的动作展平。`--check` 已经做过同一件事，所以这里的失败只可能是
    // 配置在「校验」与「使用」之间被改过（极少见），照旧报出来。
    std::vector<std::vector<core::Action>> actions;
    actions.reserve(action.items.size());
    for (const core::MenuItemDef &item : action.items) {
        std::vector<core::Action> list;
        if (item.action) {
            if (const auto error = item.action->flatten(&list); error.has_value()) {
                win::logError(QStringLiteral("`%1` menu: %2").arg(hotkey, *error));
                return;
            }
        }
        actions.push_back(std::move(list));
    }

    MenuRequest request;
    request.title = action.menuTitle;
    request.items.reserve(action.items.size());
    for (const core::MenuItemDef &item : action.items) {
        request.items.push_back(MenuEntry{item.keyChar(), item.label.trimmed(), item.hint});
    }
    const int count = static_cast<int>(action.items.size());
    request.onChoose = [dispatcher, config, hotkey, actions = std::move(actions)](int index) {
        if (index < 0 || index >= static_cast<int>(actions.size())) {
            return;
        }
        const std::vector<core::Action> &list = actions[static_cast<std::size_t>(index)];
        // 没写 `action`（或写了 `none()`）的条目就是“关掉选单”。
        if (list.empty()) {
            return;
        }
        dispatcher->submitActions(config, hotkey, list);
    };
    runtime->showMenuFromAnyThread(std::move(request));
    win::logInfo(QStringLiteral("`%1` -> menu %2 (%3 item(s))")
                     .arg(hotkey)
                     .arg(action.menuTitle.has_value() ? core::rustDebug(*action.menuTitle)
                                                       : QStringLiteral("(untitled)"))
                     .arg(count));
}

/// 帮助窗口里一行的「执行」目标。
///
/// 快捷键行是一串声明式动作（按下那半程 + 松开那半程，与真按一下那个键等价）；
/// `remap` 行没有动作，执行它就是**注入它的目标按键**（等价于按一下 `from`）。
struct HelpRunTarget
{
    QString name;
    std::vector<core::Action> actions;
    bool isRemap = false;
    std::vector<core::SendOp> remapPress;
    std::vector<core::SendOp> remapRelease;
};

/// 弹出 `help` 动作的快捷键帮助窗口。
///
/// 列表直接从当前编译好的配置生成（绑定 + 重映射），所以这个动作不需要任何
/// 参数。`onCopy` 只写一次剪贴板（单击某一行），`onRun` 才是执行那一行的动作
/// （`Enter` 或双击）—— 动作回投给工作线程，弹窗自己从不执行动作。
void openHelpAction(Runtime *runtime,
                    Dispatcher *dispatcher,
                    const std::shared_ptr<const core::Compiled> &config,
                    const QString &hotkey,
                    const core::Action &action)
{
    HelpRequest request;
    request.title = action.helpTitle;
    request.items.reserve(config->bindings.size() + config->remaps.size());
    std::vector<HelpRunTarget> targets;
    targets.reserve(config->bindings.size() + config->remaps.size());
    for (const core::Binding &binding : config->bindings) {
        HelpEntry entry;
        for (const core::Chord &chord : binding.chords) {
            entry.chords.append(chord.render());
        }
        entry.label = binding.comment.value_or(binding.name);
        entry.detail = bindingDetail(binding);

        HelpRunTarget target;
        target.name = binding.name;
        target.actions = binding.press;
        target.actions.insert(target.actions.end(), binding.release.begin(), binding.release.end());
        // `quit`/`suspend`/`power` 要再确认一次（模型只认这个布尔量）。
        entry.destructive = core::isDestructive(target.actions);

        request.items.push_back(std::move(entry));
        targets.push_back(std::move(target));
    }
    for (const core::CompiledRemap &remap : config->remaps) {
        HelpEntry entry;
        entry.chords.append(remap.from.render());
        entry.label = remap.name;
        entry.detail = QStringLiteral("remap → %1").arg(describeRemapTarget(remap));

        HelpRunTarget target;
        target.name = remap.name;
        target.isRemap = true;
        target.remapPress = remap.press;
        target.remapRelease = remap.release;

        request.items.push_back(std::move(entry));
        targets.push_back(std::move(target));
    }
    const int count = static_cast<int>(request.items.size());
    request.onCopy = [](const QString &text) {
        QString error;
        if (!win::clipboard::setText(text, &error)) {
            win::logWarn(QStringLiteral("could not copy %1 from the help window: %2")
                             .arg(core::rustDebug(text), error));
        }
    };
    request.onRun = [dispatcher, config, targets = std::move(targets)](int index) {
        if (index < 0 || index >= static_cast<int>(targets.size())) {
            return;
        }
        const HelpRunTarget &target = targets[static_cast<std::size_t>(index)];
        if (target.isRemap) {
            // 与钩子里内联注入重映射按键同一条路：先按下那半程，再松开那半程。
            QString error;
            if (!win::sendOps(target.remapPress, true, &error)
                || !win::sendOps(target.remapRelease, true, &error)) {
                win::logWarn(QStringLiteral("`%1`: remap injection from the help window failed: %2")
                                 .arg(target.name, error));
                return;
            }
            win::logInfo(QStringLiteral("`%1` -> remap (from the help window)").arg(target.name));
            return;
        }
        if (target.actions.empty()) {
            // 一个按键屏蔽器（没有任何动作）从帮助窗口触发时什么都不发生。
            win::logInfo(QStringLiteral("`%1` -> none (from the help window)").arg(target.name));
            return;
        }
        dispatcher->submitActions(config, target.name, target.actions);
    };
    runtime->showHelpFromAnyThread(std::move(request));
    win::logInfo(QStringLiteral("`%1` -> help (%2 entry(s))").arg(hotkey).arg(count));
}

/// 弹出窗口切换器（`windows` 动作）。
///
/// 窗口列表在**动作线程**上枚举（`EnumWindows` + 取进程名），随后经 `PopupHost`
/// 在 GUI 线程上显示；用户选中第几项之后，回调把「激活那个 HWND」再投回动作
/// 线程执行 —— 弹窗自己从不碰窗口后端（与 `menu` / `help` 同一条分工）。
void openWindowsAction(Runtime *runtime,
                       Dispatcher *dispatcher,
                       const QString &hotkey,
                       const core::Action &action)
{
    const std::vector<win::window::OpenWindow> windows = win::window::listOpenWindows();
    SwitchRequest request;
    request.title = action.windowsTitle;
    std::vector<HWND> handles;
    handles.reserve(windows.size());
    request.items.reserve(windows.size());
    for (const win::window::OpenWindow &entry : windows) {
        request.items.push_back(WindowListEntry{entry.title, entry.process});
        handles.push_back(entry.hwnd);
    }
    const int count = static_cast<int>(handles.size());
    request.onChoose = [dispatcher, hotkey, handles = std::move(handles)](int index) {
        if (index < 0 || index >= static_cast<int>(handles.size())) {
            return;
        }
        const HWND hwnd = handles[static_cast<std::size_t>(index)];
        dispatcher->submitCall([hwnd, hotkey]() {
            QString detail;
            QString error;
            if (!win::window::applyTo(hwnd, core::WindowOp::Activate, false, &detail, &error)) {
                win::logWarn(QStringLiteral("`%1` window switcher: %2").arg(hotkey, error));
            } else {
                win::logInfo(QStringLiteral("`%1` -> %2 (from the window switcher)")
                                 .arg(hotkey, detail));
            }
        });
    };
    runtime->showSwitchFromAnyThread(std::move(request));
    win::logInfo(QStringLiteral("`%1` -> windows (%2 window(s))").arg(hotkey).arg(count));
}

/// 弹出程序启动器（`apps` 动作）。
///
/// 目录已经在动作线程上扫好（`refreshAppCatalog()`），这里只把它转成窗口要显示的
/// 数据结构；用户选中第几项之后，回调把「启动那一个快捷方式」再投回动作线程执行。
///
/// **启动用的是快捷方式本身**（`ShellExecuteW("open", <lnk>)`）：参数、工作
/// 目录、`runas` 标记、商店/UWP 应用的激活全部交给 shell，与点开始菜单一致。
void openAppsAction(Runtime *runtime,
                    Dispatcher *dispatcher,
                    const std::vector<core::AppEntry> &catalog,
                    const QString &hotkey,
                    const core::Action &action)
{
    AppRequest request;
    request.title = action.appsTitle;
    // 启动要的是快捷方式路径，而模型要的是名字；两者一一对应，所以按下标带过去。
    std::vector<QString> shortcuts;
    shortcuts.reserve(catalog.size());
    request.items.reserve(catalog.size());
    for (const core::AppEntry &entry : catalog) {
        request.items.push_back(AppLauncherItem{entry.name, entry.shortcut});
        shortcuts.push_back(entry.shortcut);
    }
    const int count = static_cast<int>(shortcuts.size());
    request.onChoose = [dispatcher, hotkey, shortcuts = std::move(shortcuts)](int index) {
        if (index < 0 || index >= static_cast<int>(shortcuts.size())) {
            return;
        }
        const QString shortcut = shortcuts[static_cast<std::size_t>(index)];
        dispatcher->submitCall([shortcut, hotkey]() {
            QString error;
            if (!win::openTarget(shortcut, std::nullopt, std::nullopt, core::ShowMode::Normal,
                                 &error)) {
                win::logError(QStringLiteral("`%1` app launcher: %2").arg(hotkey, error));
                return;
            }
            win::logInfo(QStringLiteral("`%1` -> launched %2 (from the app launcher)")
                             .arg(hotkey, QDir::toNativeSeparators(shortcut)));
        });
    };
    runtime->showAppsFromAnyThread(std::move(request));
    win::logInfo(QStringLiteral("`%1` -> apps (%2 program(s))").arg(hotkey).arg(count));
}

/// 一次触发的模板展开上下文：把剪贴板/选中文本的读取缓存起来。
///
/// 只有在字符串里真的出现 `{clipboard}` / `{selection}` 时才去读，
/// 而 `{selection}` 会合成一次 Ctrl+C——绝不能因为一条 `run` 动作就顺手做掉。
class ExpandContext
{
public:
    ExpandContext(Runtime *runtime, QString hotkey)
        : m_runtime(runtime), m_hotkey(std::move(hotkey))
    {
    }

    QString expand(const QString &text)
    {
        core::Vars vars = baseVars();
        if (core::needsClipboard(text)) {
            if (core::needsSelection(text)) {
                vars.selection = selectionText();
            }
            vars.clipboard = clipboardText();
        }
        return core::expand(text, vars);
    }

private:
    core::Vars baseVars() const
    {
        core::Vars vars;
        vars.hotkey = m_hotkey;
        vars.configDir = QFileInfo(m_runtime->configPath()).absolutePath();
        const QString exeDir = QCoreApplication::applicationDirPath();
        if (!exeDir.isEmpty()) {
            vars.exeDir = exeDir;
        }
        const QString profile = qEnvironmentVariable("USERPROFILE");
        if (!profile.isEmpty()) {
            vars.userProfile = profile;
        }
        vars.localTime = localTimeNow();
        return vars;
    }

    /// 剪贴板内容，在单次触发期间缓存。
    QString clipboardText()
    {
        if (m_clipboard.has_value()) {
            return *m_clipboard;
        }
        QString value;
        QString error;
        if (!win::clipboard::getText(&value, &error)) {
            win::logWarn(QStringLiteral("clipboard read failed: %1").arg(error));
        }
        m_clipboard = value;
        return value;
    }

    /// 当前选中的文本：合成一次 Ctrl+C，然后读剪贴板。
    QString selectionText()
    {
        if (m_selection.has_value()) {
            return *m_selection;
        }
        QString value;
        QString error;
        if (!win::copySelection(150, &error)) {
            win::logWarn(QStringLiteral("copying the selection failed: %1").arg(error));
        } else if (!win::clipboard::getText(&value, &error)) {
            win::logWarn(QStringLiteral("clipboard read failed: %1").arg(error));
        }
        m_clipboard = value;
        m_selection = value;
        return value;
    }

    Runtime *m_runtime = nullptr;
    QString m_hotkey;
    std::optional<QString> m_clipboard;
    std::optional<QString> m_selection;
};

/// `window` 动作等待刚启动的程序开窗的时间。
constexpr std::uint64_t kDefaultLaunchWaitMs = 3000;
/// 等待该窗口时的轮询间隔。
constexpr unsigned long kPollIntervalMs = 50;

/// 启动 `window` 动作指定的程序，然后等待它的窗口出现并激活它。
///
/// 运行在工作线程上，所以这个轮询循环允许 sleep；钩子回调永远不会走到这里。
///
/// 这里**不**套用 `toggle`：刚启动的窗口往往自己就抢到了前台，在这种时候
/// 把“启动它”变成“立刻收起它”毫无道理。
bool launchThenActivate(ExpandContext &ctx,
                        const core::LaunchSpec &launch,
                        const core::WindowQuery &query,
                        bool animate,
                        QString *detail,
                        QString *error)
{
    win::RunCommandSpec spec;
    spec.program = ctx.expand(launch.program);
    for (const QString &arg : launch.args) {
        spec.args.append(ctx.expand(arg));
    }
    if (launch.cwd.has_value()) {
        spec.cwd = ctx.expand(*launch.cwd);
    }
    spec.show = launch.show;
    spec.shell = launch.shell;
    spec.wait = false;
    for (auto it = launch.env.constBegin(); it != launch.env.constEnd(); ++it) {
        spec.env.insert(ctx.expand(it.key()), ctx.expand(it.value()));
    }

    QString started;
    if (!win::runCommand(spec, &started, error)) {
        return false;
    }

    const std::uint64_t timeout = launch.waitMs.value_or(kDefaultLaunchWaitMs);
    const std::uint64_t deadline = win::monotonicMs() + timeout;
    while (true) {
        HWND hwnd = win::window::find(query);
        if (hwnd != nullptr) {
            QString placement;
            if (!win::window::applyTo(hwnd, core::WindowOp::Activate, animate, &placement, error)) {
                return false;
            }
            if (detail != nullptr) {
                *detail = QStringLiteral("started %1, then %2").arg(started, placement);
            }
            return true;
        }
        if (win::monotonicMs() >= deadline) {
            if (error != nullptr) {
                *error = QStringLiteral("started %1 but no window %2 appeared within %3 ms")
                             .arg(started, query.describe())
                             .arg(timeout);
            }
            return false;
        }
        QThread::msleep(kPollIntervalMs);
    }
}

void executeWindowAction(ExpandContext &ctx,
                         const core::Action &action,
                         const QString &hotkey)
{
    const std::optional<QString> title = action.target.has_value()
                                             ? std::optional<QString>(ctx.expand(*action.target))
                                             : std::nullopt;
    const std::optional<QString> process = action.process.has_value()
                                               ? std::optional<QString>(ctx.expand(*action.process))
                                               : std::nullopt;
    const core::WindowQuery query = core::WindowQuery::make(title, process);
    const bool animate = action.animate.value_or(false);

    if (HWND hwnd = win::window::find(query); hwnd != nullptr) {
        // 已经在前台的窗口再激活一次本来就是空操作。默认的 `toggle` 改成把它
        // 收起，于是同一个快捷键在“唤起”与“收起”之间切换。这里只处理
        // “窗口已经存在”这条路径，见 launchThenActivate。
        const bool alreadyActive = win::window::isActive(hwnd);
        const core::WindowPlan plan =
            core::planWindowAction(action.windowOp, action.toggle, alreadyActive);
        QString detail;
        QString error;
        if (plan == core::WindowPlan::MinimizeBecauseActive) {
            if (!win::window::applyTo(hwnd, core::WindowOp::Minimize, animate, &detail, &error)) {
                win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
            } else {
                win::logInfo(QStringLiteral("`%1` -> %2 (already active)").arg(hotkey, detail));
            }
            return;
        }
        if (!win::window::applyTo(hwnd, action.windowOp, animate, &detail, &error,
                                  action.follow.value_or(false))) {
            win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        }
        return;
    }

    if (action.launch.has_value() && action.windowOp == core::WindowOp::Activate) {
        QString detail;
        QString error;
        if (!launchThenActivate(ctx, *action.launch, query, animate, &detail, &error)) {
            win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        }
        return;
    }
    win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, win::window::missing(query)));
}

/// `window_rule` 里第一条命中这个窗口的规则。
const core::WindowRule *firstWindowRule(const core::Compiled &config,
                                        const QString &title,
                                        const std::optional<QString> &process)
{
    for (const core::WindowRule &rule : config.windowRules) {
        if (core::windowMatchesRule(rule, title, process)) {
            return &rule;
        }
    }
    return nullptr;
}

/// 窗口类名，只用于日志诊断。
QString windowClassName(HWND hwnd)
{
    wchar_t buffer[256];
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 0) {
        return QStringLiteral("<unknown>");
    }
    return QString::fromWCharArray(buffer, length);
}

/// 这个窗口是不是 `window_rule` 该摆的“主窗口”。
///
/// 判据（可见、没有属主、非工具窗口、有标题、尺寸非零、不是 Program Manager）
/// 与窗口切换器共用，见 `platform/win/window.h` 的 `isMainWindow`。
bool isPlaceableWindow(HWND hwnd)
{
    return win::window::isMainWindow(hwnd);
}

/// 按 `window_rule` 摆放一个窗口。
///
/// 只在三个时机被调用（窗口出现 / 显示器重新接入 / 启动），**之后不再干预**：
/// 用户自己移动或缩放窗口不会被纠正。
///
/// `followDesktop` 只对“窗口第一次出现”那一遍为真：规则**真的**把窗口搬到了别的
/// 虚拟桌面时，把视图也切过去并重新激活它（`window_rule` 的语义是“这个程序属于
/// 那张桌面”，而用户刚刚把它弄出来）。启动 / 显示器重新接入那两遍只是重新摆放
/// 已经在位的窗口，跟着走只会把视图无谓地切来切去。
void placeWindowOnce(const std::shared_ptr<const core::Compiled> &config,
                     HWND hwnd,
                     bool followDesktop)
{
    if (!config || config->windowRules.empty()) {
        return;
    }
    if (!isPlaceableWindow(hwnd)) {
        return;
    }
    // 不碰 flowkeyd 自己的窗口（选单 / 帮助 / 日志）。
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {
        return;
    }

    const QString title = win::window::windowTitle(hwnd);
    const std::optional<QString> process = win::window::processName(hwnd);
    const core::WindowRule *rule = firstWindowRule(*config, title, process);
    if (rule == nullptr) {
        return;
    }

    QStringList done;
    bool movedToAnotherDesktop = false;
    if (rule->desktop.has_value()) {
        QString detail;
        QString error;
        // `changed` 只在**真的**换了桌面时为真：窗口本来就在目标桌面上（例如一个已经
        // 在第 3 个桌面的程序又开了一个窗口）不算“搬迁”，视图不该跟着走。
        if (!win::desktop::moveWindowToDesktop(hwnd, *rule->desktop, &detail, &error,
                                              &movedToAnotherDesktop)) {
            win::logWarn(QStringLiteral("window rule `%1`: %2: %3")
                             .arg(rule->name, core::rustDebug(title), error));
        } else {
            done.append(detail);
        }
    }

    if (rule->applyGeometry) {
        const std::vector<core::MonitorDescription> monitors =
            core::sortedMonitors(win::monitor::list());
        std::optional<std::size_t> index;
        if (rule->monitor.has_value()) {
            index = core::selectMonitor(monitors, *rule->monitor);
        } else {
            index = win::monitor::indexForWindow(monitors, hwnd);
        }
        if (!index.has_value()) {
            win::logWarn(QStringLiteral("window rule `%1`: no monitor matches %2; leaving %3 where it is")
                             .arg(rule->name,
                                  rule->monitor.has_value()
                                      ? rule->monitor->describe()
                                      : QStringLiteral("the current monitor"),
                                  core::rustDebug(title)));
        } else {
            const core::Rect current =
                win::monitor::windowRect(hwnd).value_or(core::Rect{0, 0, 800, 600});
            const core::Rect rect = core::placementRect(current, monitors[*index], rule->maximize,
                                                        rule->x, rule->y, rule->width, rule->height);
            QString error;
            if (!win::monitor::applyPlacement(hwnd, rect, rule->maximize, &error)) {
                win::logWarn(QStringLiteral("window rule `%1`: %2: %3")
                                 .arg(rule->name, core::rustDebug(title), error));
            } else {
                done.append(QStringLiteral("%1 %2x%3 at %4,%5")
                                .arg(rule->maximize ? QStringLiteral("maximized")
                                                    : QStringLiteral("placed"),
                                     QString::number(rect.width),
                                     QString::number(rect.height),
                                     QString::number(rect.x),
                                     QString::number(rect.y)));
            }
        }
    }

    // 钉在所有虚拟桌面 / 始终在最上层：两个与几何无关的开关。
    if (rule->allDesktops.has_value()) {
        QString detail;
        QString error;
        bool changed = false;
        if (!win::desktop::setWindowPinned(hwnd, *rule->allDesktops, &detail, &error, &changed)) {
            win::logWarn(QStringLiteral("window rule `%1`: %2: %3")
                             .arg(rule->name, core::rustDebug(title), error));
        } else if (changed) {
            done.append(detail);
        }
    }
    if (rule->topmost.has_value()) {
        QString error;
        if (!win::window::setTopmost(hwnd, *rule->topmost, &error)) {
            win::logWarn(QStringLiteral("window rule `%1`: %2: %3")
                             .arg(rule->name, core::rustDebug(title), error));
        } else {
            done.append(*rule->topmost ? QStringLiteral("topmost") : QStringLiteral("not topmost"));
        }
    }

    // 视图跟着窗口走：让用户跟着它到那张桌面，并且它要重新拿到前台（`SwitchDesktop`
    // 本身会激活目标桌面上“上次用过”的那个窗口，不一定是它）。
    if (followDesktop && movedToAnotherDesktop && rule->desktop.has_value()) {
        QString detail;
        QString error;
        if (!win::desktop::switchTo(*rule->desktop, &detail, &error)) {
            win::logWarn(QStringLiteral("window rule `%1`: could not follow %2: %3")
                             .arg(rule->name, core::rustDebug(title), error));
        } else {
            done.append(QStringLiteral("view -> %1").arg(detail));
            if (!win::window::raiseWindow(hwnd)) {
                win::logWarn(QStringLiteral("window rule `%1`: could not activate %2 after "
                                            "switching to its desktop")
                                 .arg(rule->name, core::rustDebug(title)));
            }
        }
    }

    if (!done.isEmpty()) {
        win::logInfo(QStringLiteral("window rule `%1` -> %2 [%3]: %4")
                         .arg(rule->name, core::rustDebug(title), windowClassName(hwnd),
                              done.join(QStringLiteral(", "))));
    }
}

void executeClipboardAction(ExpandContext &ctx,
                            const core::Action &action,
                            const QString &hotkey)
{
    QString error;
    switch (action.clipboardOp) {
    case core::ClipboardOp::Get: {
        QString value;
        if (!win::clipboard::getText(&value, &error)) {
            win::logError(QStringLiteral("`%1` clipboard get failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard %2 char(s): %3")
                         .arg(hotkey)
                         .arg(value.size())
                         .arg(core::rustDebug(truncateText(value, 60))));
        return;
    }
    case core::ClipboardOp::Set: {
        const QString value = ctx.expand(action.clipboardText.value_or(QString()));
        if (!win::clipboard::setText(value, &error)) {
            win::logError(QStringLiteral("`%1` clipboard set failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard set (%2 char(s))").arg(hotkey).arg(value.size()));
        return;
    }
    case core::ClipboardOp::Append: {
        const QString value = ctx.expand(action.clipboardText.value_or(QString()));
        if (!win::clipboard::appendText(value, &error)) {
            win::logError(QStringLiteral("`%1` clipboard append failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard append (%2 char(s))").arg(hotkey).arg(value.size()));
        return;
    }
    case core::ClipboardOp::Clear:
        if (!win::clipboard::clear(&error)) {
            win::logError(QStringLiteral("`%1` clipboard clear failed: %2").arg(hotkey, error));
            return;
        }
        win::logInfo(QStringLiteral("`%1` -> clipboard cleared").arg(hotkey));
        return;
    }
}

/// 执行一个动作。`ctx` 在单次触发期间共享，所以剪贴板只读一次。
void executeAction(Runtime *runtime,
                   Dispatcher *dispatcher,
                   const std::shared_ptr<const core::Compiled> &config,
                   const QString &hotkey,
                   const core::Action &action,
                   ExpandContext &ctx)
{
    switch (action.kind) {
    case core::Action::Kind::Noop:
        break;
    case core::Action::Kind::Send: {
        const QString script = ctx.expand(action.keys);
        if (action.delayMs.has_value()) {
            QThread::msleep(static_cast<unsigned long>(*action.delayMs));
        }
        QVector<core::SendOp> ops;
        if (const auto error = core::parseKeyOrScript(script, &ops); error.has_value()) {
            win::logError(QStringLiteral("`%1` send %2: %3")
                              .arg(hotkey, core::rustDebug(action.keys), error->message()));
            return;
        }
        const bool release =
            action.releaseModifiers.value_or(config->settings.releaseModifiers);
        win::ModifierGuard guard =
            release ? win::ModifierGuard::release() : win::ModifierGuard::none();
        QString error;
        const bool ok = win::sendOps(ops, true, &error);
        guard.restore();
        if (!ok) {
            win::logError(QStringLiteral("`%1` send failed: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2 (%3 step(s))")
                             .arg(hotkey, core::rustDebug(script))
                             .arg(ops.size()));
        }
        break;
    }
    case core::Action::Kind::Type: {
        if (action.delayMs.has_value()) {
            QThread::msleep(static_cast<unsigned long>(*action.delayMs));
        }
        const QString text = ctx.expand(action.text);
        QVector<core::SendOp> ops;
        ops.reserve(text.size());
        for (int i = 0; i < text.size(); ++i) {
            ops.append(core::SendOp::unicode(text.at(i).unicode()));
        }
        const bool release =
            action.releaseModifiers.value_or(config->settings.releaseModifiers);
        win::ModifierGuard guard =
            release ? win::ModifierGuard::release() : win::ModifierGuard::none();
        QString error;
        const bool ok = win::sendOps(ops, true, &error);
        guard.restore();
        if (!ok) {
            win::logError(QStringLiteral("`%1` type failed: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> typed %2 char(s)").arg(hotkey).arg(text.size()));
        }
        break;
    }
    case core::Action::Kind::CapsLock: {
        if (win::capsLockOn()) {
            QString error;
            if (!win::tapKey(core::vk::CAPITAL, &error)) {
                win::logWarn(QStringLiteral("`%1` caps_lock failed: %2").arg(hotkey, error));
            } else {
                win::logInfo(QStringLiteral("`%1` -> caps lock off").arg(hotkey));
            }
        } else {
            win::logDebug(QStringLiteral("`%1` caps lock already off").arg(hotkey));
        }
        break;
    }
    case core::Action::Kind::Open: {
        const QString target = ctx.expand(action.target.value_or(QString()));
        const std::optional<QString> args =
            action.openArgs.has_value() ? std::optional<QString>(ctx.expand(*action.openArgs))
                                        : std::nullopt;
        const std::optional<QString> cwd =
            action.cwd.has_value() ? std::optional<QString>(ctx.expand(*action.cwd))
                                   : std::nullopt;
        QString error;
        if (!win::openTarget(target, args, cwd, action.show, &error)) {
            win::logError(QStringLiteral("`%1` open: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> opened %2").arg(hotkey, target));
        }
        break;
    }
    case core::Action::Kind::Run: {
        win::RunCommandSpec spec;
        spec.program = ctx.expand(action.program);
        for (const QString &arg : action.args) {
            spec.args.append(ctx.expand(arg));
        }
        if (action.cwd.has_value()) {
            spec.cwd = ctx.expand(*action.cwd);
        }
        spec.show = action.show;
        spec.shell = action.shell;
        spec.wait = action.wait;
        for (auto it = action.env.constBegin(); it != action.env.constEnd(); ++it) {
            spec.env.insert(ctx.expand(it.key()), ctx.expand(it.value()));
        }
        QString detail;
        QString error;
        if (!win::runCommand(spec, &detail, &error)) {
            win::logError(QStringLiteral("`%1` run: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> run %2 (%3)").arg(hotkey, spec.program, detail));
        }
        break;
    }
    case core::Action::Kind::Volume: {
        QString detail;
        QString error;
        if (!win::audio::apply(action.volumeOp, action.level, action.step, &detail, &error)) {
            win::logError(QStringLiteral("`%1` volume: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> volume %2").arg(hotkey, detail));
        }
        break;
    }
    case core::Action::Kind::Media: {
        core::Vk vk = core::vk::MEDIA_PLAY_PAUSE;
        switch (action.mediaOp) {
        case core::MediaOp::PlayPause:
            vk = core::vk::MEDIA_PLAY_PAUSE;
            break;
        case core::MediaOp::Next:
            vk = core::vk::MEDIA_NEXT_TRACK;
            break;
        case core::MediaOp::Prev:
            vk = core::vk::MEDIA_PREV_TRACK;
            break;
        case core::MediaOp::Stop:
            vk = core::vk::MEDIA_STOP;
            break;
        }
        QString error;
        if (!win::tapKey(vk, &error)) {
            win::logError(QStringLiteral("`%1` media failed: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> media %2")
                             .arg(hotkey, core::mediaOpDebugName(action.mediaOp)));
        }
        break;
    }
    case core::Action::Kind::Clipboard:
        executeClipboardAction(ctx, action, hotkey);
        break;
    case core::Action::Kind::Window:
        executeWindowAction(ctx, action, hotkey);
        break;
    case core::Action::Kind::Notify: {
        const QString title = ctx.expand(action.title);
        const QString body = ctx.expand(action.body.value_or(QString()));
        win::logInfo(QStringLiteral("`%1` -> notify %2").arg(hotkey, core::rustDebug(title)));
        runtime->notifyFromAnyThread(title, body);
        break;
    }
    case core::Action::Kind::Suspend: {
        win::ControlCmd cmd = win::ControlCmd::ToggleSuspend;
        bool target = false;
        switch (action.toggleState) {
        case core::ToggleState::On:
            cmd = win::ControlCmd::Suspend;
            target = true;
            break;
        case core::ToggleState::Off:
            cmd = win::ControlCmd::Resume;
            target = false;
            break;
        case core::ToggleState::Toggle:
            target = !runtime->isSuspended();
            break;
        }
        runtime->postControl(cmd);
        runtime->reportSuspended(target);
        win::logInfo(QStringLiteral("`%1` -> suspend (%2)").arg(hotkey).arg(target ? "on" : "off"));
        break;
    }
    case core::Action::Kind::Reload:
        win::logInfo(QStringLiteral("`%1` -> reload").arg(hotkey));
        runtime->reloadFromAnyThread();
        break;
    case core::Action::Kind::Quit:
        win::logInfo(QStringLiteral("`%1` -> quit").arg(hotkey));
        runtime->requestShutdownFromAnyThread();
        break;
    case core::Action::Kind::Desktop: {
        QString detail;
        QString error;
        if (!win::desktop::switchTo(action.desktopSwitch, &detail, &error)) {
            win::logError(QStringLiteral("`%1` desktop: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        }
        break;
    }
    case core::Action::Kind::Power: {
        QString detail;
        QString error;
        if (!win::power::execute(action.powerOp, &detail, &error)) {
            win::logError(QStringLiteral("`%1` power: %2").arg(hotkey, error));
            // 失败最常见的原因是没提权（托盘模式又看不见控制台日志），
            // 所以补一条气泡提示，不能让用户对着一片安静发呆。
            runtime->notifyFromAnyThread(
                QStringLiteral("flowkeyd"),
                QStringLiteral("power %1 failed: %2")
                    .arg(core::powerOpName(action.powerOp), error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> power %2").arg(hotkey, detail));
        }
        break;
    }
    case core::Action::Kind::Menu:
        openMenuAction(runtime, dispatcher, config, hotkey, action);
        break;
    case core::Action::Kind::Help:
        openHelpAction(runtime, dispatcher, config, hotkey, action);
        break;
    case core::Action::Kind::Windows:
        openWindowsAction(runtime, dispatcher, hotkey, action);
        break;
    case core::Action::Kind::Apps:
        openAppsAction(runtime, dispatcher, dispatcher->appCatalog(), hotkey, action);
        break;
    default:
        win::logWarn(QStringLiteral("`%1`: action `%2` is not implemented yet")
                         .arg(hotkey, action.summary()));
        break;
    }
}

} // namespace

Dispatcher::Dispatcher(Runtime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

/// 启动器目录的缓存有效期：超过它就在下一次弹出前重扫一遍。
///
/// 30 秒足够短（刚装完程序按下去就能看到），也足够长（连着按几次不必重复扫、
/// 不会每次都多花那 80 ms）。
namespace {
constexpr std::uint64_t kAppCatalogTtlMs = 30000;
} // namespace

void Dispatcher::startAppScan()
{
    refreshAppCatalog();
}

const std::vector<core::AppEntry> &Dispatcher::appCatalog()
{
    const std::uint64_t now = win::monotonicMs();
    if (m_appScanned && now - m_appScannedAt < kAppCatalogTtlMs) {
        return m_appCatalog;
    }
    refreshAppCatalog();
    return m_appCatalog;
}

void Dispatcher::refreshAppCatalog()
{
    const win::apps::StartMenuScan scan = win::apps::listStartMenuApps();
    if (!scan.error.isEmpty()) {
        win::logWarn(QStringLiteral("app launcher: %1").arg(scan.error));
        if (m_appScanned) {
            // 上一份目录还能用：保留它（顺便让下一次弹出再试一遍）。
            return;
        }
    }
    m_appCatalog = core::prepareAppEntries(scan.entries);
    m_appScanned = true;
    m_appScannedAt = win::monotonicMs();
    win::logDebug(QStringLiteral("app launcher: %1 shortcut(s) found, %2 program(s) listed")
                      .arg(scan.shortcuts)
                      .arg(static_cast<qulonglong>(m_appCatalog.size())));
}

void Dispatcher::startDesktopWatch()
{
    if (m_desktopTimer != nullptr) {
        return;
    }
    // 定时器要在它自己的线程上创建并启动（Qt 的要求）：`startDesktopWatch()` 正是由
    // `Runtime::start()` 排队投进动作线程的，所以这里已经是那条线程。
    m_desktopTimer = new QTimer(this);
    m_desktopTimer->setInterval(500);
    connect(m_desktopTimer, &QTimer::timeout, this, &Dispatcher::pollDesktop);
    m_desktopTimer->start();
    // 启动时立刻问一次（不等第一个 500 ms），托盘上的数字一下就到位。
    pollDesktop();
}

void Dispatcher::pollDesktop()
{
    std::uint32_t index = 0;
    std::uint32_t count = 0;
    QString error;
    if (!win::desktop::currentDesktopIndex(&index, &count, &error)) {
        // 锁屏 / shell 正忙 / 版本表对不上时查不到是正常的：保留上一次的数字，
        // 不要退回应用图标后再来回闪。日志只在状态翻转时写一行。
        if (!m_desktopWatchFailed) {
            m_desktopWatchFailed = true;
            win::logDebug(QStringLiteral("cannot read the current virtual desktop: %1")
                              .arg(error));
        }
        return;
    }
    if (m_desktopWatchFailed) {
        m_desktopWatchFailed = false;
        win::logDebug(QStringLiteral("the current virtual desktop is readable again"));
    }
    const int number = static_cast<int>(index);
    const int total = static_cast<int>(count);
    if (number == m_desktopNumber && total == m_desktopCount) {
        return;
    }
    m_desktopNumber = number;
    m_desktopCount = total;
    win::logDebug(QStringLiteral("virtual desktop %1/%2; updating the tray icon")
                      .arg(number)
                      .arg(total));
    if (m_runtime != nullptr) {
        m_runtime->reportDesktop(number, total);
    }
}

void Dispatcher::submit(std::shared_ptr<const core::Compiled> config, core::Trigger trigger)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), trigger]() { execute(config, trigger); },
        Qt::QueuedConnection);
}

void Dispatcher::submitActions(std::shared_ptr<const core::Compiled> config,
                               QString name,
                               std::vector<core::Action> actions)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), name = std::move(name), actions = std::move(actions)]() {
            runActions(config, name, actions);
        },
        Qt::QueuedConnection);
}

void Dispatcher::submitPlacement(std::shared_ptr<const core::Compiled> config,
                                 win::PlacementEvent event)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), event = std::move(event)]() {
            applyPlacementRules(config, event);
        },
        Qt::QueuedConnection);
}

void Dispatcher::submitCall(std::function<void()> work)
{
    if (!work) {
        return;
    }
    QMetaObject::invokeMethod(
        this, [work = std::move(work)]() { work(); }, Qt::QueuedConnection);
}

void Dispatcher::applyPlacementRules(const std::shared_ptr<const core::Compiled> &config,
                                     const win::PlacementEvent &event)
{
    if (!config || config->windowRules.empty()) {
        return;
    }
    if (event.kind == win::PlacementEvent::Kind::WindowsShown) {
        win::logDebug(QStringLiteral("window rules: checking %1 shown window(s)")
                          .arg(event.windows.size()));
        for (HWND hwnd : event.windows) {
            // 窗口第一次出现：规则把它搬到别的桌面时，视图也跟着过去。
            placeWindowOnce(config, hwnd, true);
        }
        return;
    }
    const std::vector<HWND> windows = win::window::topLevelWindows();
    win::logInfo(
        QStringLiteral("window rules: %1 (%2 window(s) to check)")
            .arg(event.kind == win::PlacementEvent::Kind::Startup
                     ? QStringLiteral("applying at startup")
                     : QStringLiteral("re-applying after a monitor reconnected"))
            .arg(windows.size()));
    for (HWND hwnd : windows) {
        // 启动与显示器重新接入：只重新摆放，不动视图（见 `placeWindowOnce`）。
        placeWindowOnce(config, hwnd, false);
    }
}

void Dispatcher::execute(const std::shared_ptr<const core::Compiled> &config,
                         const core::Trigger &trigger)
{
    if (!config || trigger.index >= config->bindings.size()) {
        return;
    }
    const core::Binding &binding = config->bindings[trigger.index];
    const std::vector<core::Action> &actions =
        trigger.phase == core::Phase::Press ? binding.press : binding.release;
    runActions(config, binding.name, actions);
}

void Dispatcher::runActions(const std::shared_ptr<const core::Compiled> &config,
                            const QString &name,
                            const std::vector<core::Action> &actions)
{
    if (!config || actions.empty()) {
        return;
    }
    ExpandContext ctx(m_runtime, name);
    for (const core::Action &action : actions) {
        executeAction(m_runtime, this, config, name, action, ctx);
    }
}

} // namespace flowkeyd::app
