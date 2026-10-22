#include "app/dispatcher.h"

#include "app/popup_host.h"
#include "app/runtime.h"
#include "core/keys.h"
#include "core/launcher_state.h"
#include "core/placement.h"
#include "core/template.h"
#include "core/window_match.h"
#include "core/window_monitor_state.h"
#include "core/workspace.h"
#include "core/workspace_event.h"
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
#include "platform/win/workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

/// `monitor = "last"` 那份持久记忆 + 它的落盘位置。
///
/// **只在真的用到 `"last"` 时才读/写磁盘**（`"cursor"` / `"none"` 完全不碰
/// 这个文件）；第一次用到时读一次，之后每次变化都写回。读写都在动作线程上，
/// 所以不需要加锁（与 `launcher.json` 同一条分工）。
///
/// 定义在具名命名空间里（不在下面的匿名命名空间）是因为 `Dispatcher` 用
/// `std::unique_ptr<MonitorMemory>` 持有它 —— 头文件里只能看到一条前置声明。
struct MonitorMemory
{
    core::WindowMonitorState state;
    /// `<配置目录>/windows.json`。
    QString path;
    bool loaded = false;
};

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
                       const std::shared_ptr<const core::Compiled> &config,
                       const QString &hotkey,
                       const core::Action &action)
{
    // 工作空间藏着的窗口 `IsWindowVisible` 是假的，但它们仍然是用户自己的程序
    // 窗口，所以要靠状态机的记录把它们纳进来（M9）。
    const std::vector<win::window::OpenWindow> windows =
        win::window::listOpenWindows(dispatcher->trackedWorkspaceWindows());
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
    request.onChoose =
        [dispatcher, config, hotkey, handles = std::move(handles)](int index) {
            if (index < 0 || index >= static_cast<int>(handles.size())) {
                return;
            }
            const HWND hwnd = handles[static_cast<std::size_t>(index)];
            dispatcher->submitCall([dispatcher, config, hwnd, hotkey]() {
                // 它在别的工作空间里时先切过去（**等切完**，窗口才真的可见），
                // 再去激活 —— 反过来对一个 `SW_HIDE` 着的窗口调 `raiseWindow()`
                // 是没用的。
                const QString revealed = dispatcher->revealWindowWorkspace(*config, hwnd);
                QString detail;
                QString error;
                if (!win::window::applyTo(hwnd, core::WindowOp::Activate, false, &detail, &error)) {
                    win::logWarn(QStringLiteral("`%1` window switcher: %2").arg(hotkey, error));
                } else if (revealed.isEmpty()) {
                    win::logInfo(QStringLiteral("`%1` -> %2 (from the window switcher)")
                                     .arg(hotkey, detail));
                } else {
                    win::logInfo(QStringLiteral("`%1` -> %2 (from the window switcher, %3)")
                                     .arg(hotkey, detail, revealed));
                }
            });
        };
    runtime->showSwitchFromAnyThread(std::move(request));
    win::logInfo(QStringLiteral("`%1` -> windows (%2 window(s))").arg(hotkey).arg(count));
}

/// 弹出程序启动器（`apps` 动作）。
///
/// 目录已经在动作线程上扫好（`refreshAppCatalog()`，枚举 `shell:AppsFolder`），
/// 这里只把它转成窗口要显示的数据结构；用户选中第几项之后，回调把「启动那一个」
/// 再投回动作线程执行。
///
/// **启动走 `apps::launchApp()`**（`shell:AppsFolder\<AUMID>`）：参数、工作目录、
/// `runas` 标记、商店/UWP 应用的激活全部交给 shell，与点开始菜单一致。
/// 以**当前登录用户的普通权限**启动（`launchAppAsUser()`）：提权实例会把它交给
/// 一个隐藏的代理进程，否则启动器拉起的浏览器 / 商店应用也会是管理员进程。
void openAppsAction(Runtime *runtime,
                    Dispatcher *dispatcher,
                    const std::vector<core::AppEntry> &catalog,
                    const QString &hotkey,
                    const core::Action &action)
{
    AppRequest request;
    request.title = action.appsTitle;
    // 固定 / 最近使用的状态文件就在配置文件旁边（`launcher.json`）：常驻实例的
    // 状态跟着用户自己的配置走，开发 / 验收实例（临时 `--config`）也各用各的。
    request.statePath = core::launcherStatePath(runtime->configPath());
    // 启动要的是解析名，而模型要的是名字；两者一一对应，所以按下标带过去。
    std::vector<QString> launchNames;
    launchNames.reserve(catalog.size());
    request.items.reserve(catalog.size());
    for (const core::AppEntry &entry : catalog) {
        request.items.push_back(AppLauncherItem{entry.name, entry.launch});
        launchNames.push_back(entry.launch);
    }
    const int count = static_cast<int>(launchNames.size());
    request.onChoose = [dispatcher, hotkey, launchNames = std::move(launchNames)](int index) {
        if (index < 0 || index >= static_cast<int>(launchNames.size())) {
            return;
        }
        const QString launchName = launchNames[static_cast<std::size_t>(index)];
        dispatcher->submitCall([launchName, hotkey]() {
            QString error;
            if (!win::apps::launchAppAsUser(launchName, &error)) {
                win::logError(QStringLiteral("`%1` app launcher: %2").arg(hotkey, error));
                return;
            }
            win::logInfo(QStringLiteral("`%1` -> launched %2 (from the app launcher)")
                             .arg(hotkey, launchName));
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

// ---------------------------------------------------------------------------
// 窗口摆到哪一块显示器上（`monitor = "cursor"` / `"last"` / `"none"`）
// ---------------------------------------------------------------------------

/// 读一个小状态文件；不存在或读不出来就返回空的（并写一条 debug 日志）。
QByteArray readStateFile(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        win::logDebug(QStringLiteral("could not read %1: %2").arg(path, file.errorString()));
        return {};
    }
    return file.readAll();
}

void loadMonitorMemory(MonitorMemory *memory)
{
    if (memory == nullptr || memory->loaded) {
        return;
    }
    memory->loaded = true;
    const QByteArray json = readStateFile(memory->path);
    if (json.trimmed().isEmpty()) {
        return;
    }
    QString error;
    memory->state = core::parseWindowMonitorState(json, &error);
    if (!error.isEmpty()) {
        win::logWarn(QStringLiteral("window monitor state: %1").arg(error));
    }
}

void saveMonitorMemory(MonitorMemory *memory)
{
    if (memory == nullptr || memory->path.isEmpty()) {
        return;
    }
    const QString directory = QFileInfo(memory->path).absolutePath();
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
    }
    QFile file(memory->path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        win::logWarn(QStringLiteral("could not write %1: %2").arg(memory->path, file.errorString()));
        return;
    }
    file.write(core::serializeWindowMonitorState(memory->state));
}

/// 记住一个程序落在了哪块显示器上（只有真的变了才落盘）。
void rememberMonitorDevice(MonitorMemory *memory, const QString &programKey, const QString &device)
{
    if (memory == nullptr) {
        return;
    }
    loadMonitorMemory(memory);
    if (core::rememberWindowMonitor(&memory->state, programKey, device)) {
        saveMonitorMemory(memory);
    }
}

/// 解析出来的目标显示器。
struct MonitorTarget
{
    /// **已排序**列表里的下标；`std::nullopt` = 这次不摆（`why` 里说明原因）。
    std::optional<std::size_t> index;
    /// 没摆成的原因（给 debug 日志用的一句话）。
    QString why;
};

/// 把 `MonitorRef` 解析成已排序列表里的下标。
///
/// * `"cursor"` 现查光标所在的那一块；
/// * `"last"` 查那份持久记忆；**第一次还没有记忆时记下窗口现在在哪一块屏上**
///   （于是下一次就摆到那儿），这次什么都不做；
/// * `"none"` 与具体的三种（序号 / `"primary"` / 设备名）照旧。
MonitorTarget resolveWindowMonitor(MonitorMemory *memory,
                                   const core::MonitorRef &ref,
                                   const std::vector<core::MonitorDescription> &monitors,
                                   HWND hwnd,
                                   const std::optional<QString> &process)
{
    MonitorTarget out;
    switch (ref.kind) {
    case core::MonitorRef::Kind::None:
        out.why = QStringLiteral("the target monitor is disabled (monitor = \"none\")");
        return out;
    case core::MonitorRef::Kind::Cursor:
        out.index = win::monitor::indexForCursor(monitors);
        if (!out.index.has_value()) {
            out.why = QStringLiteral("could not tell which monitor the cursor is on");
        }
        return out;
    case core::MonitorRef::Kind::Last: {
        const QString key = core::windowMonitorKey(process);
        if (key.isEmpty() || memory == nullptr) {
            out.why = QStringLiteral("could not tell which program the window belongs to");
            return out;
        }
        const std::optional<QString> device = rememberedWindowMonitor(memory->state, key);
        if (!device.has_value()) {
            // 还没有记忆：记下它现在在哪一块屏上（`windows.json` 里就有了身份），
            // 下一次再唤起它就摆到那儿。
            const std::optional<std::size_t> current = win::monitor::indexForWindow(monitors, hwnd);
            if (current.has_value()) {
                rememberMonitorDevice(memory, key, monitors[*current].device);
                out.why = QStringLiteral("no monitor remembered yet; noted monitor %1/%2")
                              .arg(*current + 1)
                              .arg(monitors.size());
            } else {
                out.why = QStringLiteral("no monitor remembered yet");
            }
            return out;
        }
        core::MonitorRef wanted;
        wanted.kind = core::MonitorRef::Kind::Device;
        wanted.device = *device;
        out.index = core::selectMonitor(monitors, wanted);
        if (!out.index.has_value()) {
            out.why = QStringLiteral("the remembered monitor %1 is not connected")
                          .arg(core::rustDebug(*device));
        }
        return out;
    }
    case core::MonitorRef::Kind::Index:
    case core::MonitorRef::Kind::Primary:
    case core::MonitorRef::Kind::Device:
        out.index = core::selectMonitor(monitors, ref);
        if (!out.index.has_value()) {
            out.why = QStringLiteral("no monitor matches %1").arg(ref.describe());
        }
        return out;
    }
    return out;
}

/// 一次「把窗口搬到目标显示器上」的结果。
struct MonitorMove
{
    /// 真的搬了时是给日志用的一行（`placed 1280x800 at 1920,320 on monitor 2/2`）。
    std::optional<QString> detail;
    /// 没搬时的原因（给 debug 日志用）。
    QString skipped;
};

/// 按 `ref` 把窗口搬到目标显示器上（`window` 动作的 `activate` 走这条）。
///
/// **已经在目标那一块屏上时什么都不做**：用户自己摆好的位置不该因为唤起一次就
/// 被重新居中（重新摆几何只能发生在 `window_rule` 那三个时机）。目标是 `"last"`
/// 时，搬完会把落点记进那份持久记忆。
MonitorMove moveWindowToTargetMonitor(MonitorMemory *memory,
                                      const core::MonitorRef &ref,
                                      HWND hwnd,
                                      const std::optional<QString> &process)
{
    MonitorMove out;
    if (ref.kind == core::MonitorRef::Kind::None) {
        out.skipped = QStringLiteral("the target monitor is disabled (monitor = \"none\")");
        return out;
    }
    if (hwnd == nullptr) {
        out.skipped = QStringLiteral("no window");
        return out;
    }
    const std::vector<core::MonitorDescription> monitors = core::sortedMonitors(win::monitor::list());
    if (monitors.empty()) {
        out.skipped = QStringLiteral("no monitors");
        return out;
    }
    const MonitorTarget target = resolveWindowMonitor(memory, ref, monitors, hwnd, process);
    if (!target.index.has_value()) {
        out.skipped = target.why;
        return out;
    }
    const std::optional<std::size_t> current = win::monitor::indexForWindow(monitors, hwnd);
    if (current.has_value() && *current == *target.index) {
        out.skipped = QStringLiteral("already on monitor %1/%2")
                          .arg(*target.index + 1)
                          .arg(monitors.size());
        return out;
    }
    QString detail;
    QString error;
    if (!win::monitor::moveToMonitor(hwnd, monitors, *target.index, &detail, &error)) {
        out.skipped = error;
        return out;
    }
    if (ref.kind == core::MonitorRef::Kind::Last) {
        rememberMonitorDevice(memory, core::windowMonitorKey(process), monitors[*target.index].device);
    }
    out.detail = detail;
    return out;
}

/// `window` 动作的 `activate`：把窗口摆到该去的那块显示器上，返回给日志用的一行
/// （空串 = 什么都没做）。
///
/// 目标 = 动作自己的 `monitor` → 命中它的第一条 `window_rule` 的 `monitor` →
/// `settings.window_monitor`（默认 = 光标所在的那一块），见
/// `core::windowActionMonitor()`。目标是 `"cursor"` / `"last"` 时，光标的当前位置
/// 与服务里那份记忆都是现查的。
QString bringWindowToActionMonitor(MonitorMemory *memory,
                                   const core::Compiled &config,
                                   const std::optional<core::MonitorRef> &actionMonitor,
                                   HWND hwnd,
                                   const QString &hotkey)
{
    const QString title = win::window::windowTitle(hwnd);
    const std::optional<QString> process = win::window::processName(hwnd);
    const core::WindowRule *rule = core::firstWindowRule(config.windowRules, title, process);
    const core::MonitorRef target = core::windowActionMonitor(actionMonitor, rule, config.settings);
    const MonitorMove move = moveWindowToTargetMonitor(memory, target, hwnd, process);
    if (!move.skipped.isEmpty()) {
        win::logDebug(QStringLiteral("`%1` monitor: %2").arg(hotkey, move.skipped));
    }
    if (move.detail.has_value()) {
        win::logDebug(QStringLiteral("`%1` monitor: %2").arg(hotkey, *move.detail));
    }
    return move.detail.value_or(QString());
}

/// 没声明固定桌面的程序：窗口在别的虚拟桌面上时把它拉到**当前**桌面。
///
/// `window` 动作的 `activate` 靠这一步避免 Windows 把视图切到窗口上一次所在的
/// 那张桌面上（`raiseWindow` 的默认行为）。判断“算不算声明了固定桌面”看的是
/// 命中这个窗口的第一条 `window_rule` 有没有 `desktop`：写了就把视图切过去，
/// 没写（或者压根没有规则）就把窗口搬过来。
///
/// 返回非空字符串表示**真的搬了**（内容是搬到了哪张桌面），供日志拼接；
/// 其它情况（声明了固定桌面 / 已经在当前桌面 / 问不出来 / 搬失败）都返回空。
std::optional<QString> bringWindowToCurrentDesktop(const core::Compiled &config, HWND hwnd)
{
    const QString title = win::window::windowTitle(hwnd);
    const std::optional<QString> process = win::window::processName(hwnd);
    if (core::windowRulePinsDesktop(config.windowRules, title, process)) {
        win::logDebug(QStringLiteral("window %1 is pinned to its own desktop by a window rule")
                          .arg(core::rustDebug(title)));
        return std::nullopt;
    }
    // 便宜的先问一句：已经在当前桌面就什么都不做（这一步只是
    // `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop`，不碰未公开接口）。
    QString onCurrentError;
    const std::optional<bool> onCurrent =
        win::desktop::isWindowOnCurrentDesktop(hwnd, &onCurrentError);
    if (!onCurrent.has_value()) {
        win::logDebug(QStringLiteral("could not tell whether %1 is on the current desktop: %2")
                          .arg(core::rustDebug(title), onCurrentError));
        return std::nullopt;
    }
    if (*onCurrent) {
        return std::nullopt;
    }
    QString detail;
    QString error;
    if (!win::desktop::moveWindowToCurrentDesktop(hwnd, &detail, &error)) {
        win::logWarn(QStringLiteral("could not bring %1 to the current desktop: %2")
                         .arg(core::rustDebug(title), error));
        return std::nullopt;
    }
    return detail;
}

/// 启动 `window` 动作指定的程序，然后等待它的窗口出现并激活它。
///
/// 运行在工作线程上，所以这个轮询循环允许 sleep；钩子回调永远不会走到这里。
///
/// 这里**不**套用 `toggle`：刚启动的窗口往往自己就抢到了前台，在这种时候
/// 把“启动它”变成“立刻收起它”毫无道理。
bool launchThenActivate(ExpandContext &ctx,
                        Dispatcher *dispatcher,
                        MonitorMemory *memory,
                        const core::Compiled &config,
                        const std::optional<core::MonitorRef> &actionMonitor,
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
    spec.elevated = launch.elevated;
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
    // 工作空间状态机记着的窗口（可能是被藏起来的）：`find()` 靠它们才能找到
    // 藏在别的空间里的窗口（M9）。它不会变，循环外面算一次。
    const std::vector<HWND> tracked = dispatcher->trackedWorkspaceWindows();
    while (true) {
        HWND hwnd = win::window::find(query, tracked);
        if (hwnd != nullptr) {
            const std::optional<QString> brought = bringWindowToCurrentDesktop(config, hwnd);
            // 刚启动的窗口可能出现在 Windows 自己挑的那一块屏上：按 `window_rule`
            // / `settings.window_monitor` 把它摆过去（默认 = 光标所在的那一块），
            // **再激活它**（这样它最终就在正确的位置上、而且是前台的）。
            const QString moved =
                bringWindowToActionMonitor(memory, config, actionMonitor, hwnd, QStringLiteral("launch"));
            // 摆到别的屏之后归属跟着走（M9）：否则它还会算在原屏的那个空间里。
            const QString rebound = dispatcher->followWindowMonitor(config, hwnd);
            QString placement;
            if (!win::window::applyTo(hwnd, core::WindowOp::Activate, animate, &placement, error)) {
                return false;
            }
            if (detail != nullptr) {
                *detail = QStringLiteral("started %1, then %2").arg(started, placement);
                if (brought.has_value()) {
                    *detail += QStringLiteral(" (brought to %1)").arg(*brought);
                }
                if (!moved.isEmpty()) {
                    *detail += QStringLiteral(" (%1)").arg(moved);
                }
                if (!rebound.isEmpty()) {
                    *detail += QStringLiteral(" (%1)").arg(rebound);
                }
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
                         Dispatcher *dispatcher,
                         MonitorMemory *memory,
                         const core::Compiled &config,
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

    // 工作空间藏着的窗口也要找得到（M9）：否则 `window("activate")` 会报
    // “no visible window …”，而用户明明有这个窗口，只是它在别的工作空间里。
    if (HWND hwnd = win::window::find(query, dispatcher->trackedWorkspaceWindows());
        hwnd != nullptr) {
        // 已经在前台的窗口再激活一次本来就是空操作。默认的 `toggle` 改成把它
        // 收起，于是同一个快捷键在“唤起”与“收起”之间切换。这里只处理
        // “窗口已经存在”这条路径，见 launchThenActivate。
        //
        // 被工作空间藏着的窗口不算“已经激活”（`SW_HIDE` 之后
        // `GetForegroundWindow()` 仍然指着它，M0 实测），否则唤起会变成收起。
        const bool hiddenByUs = dispatcher->workspaceSaysHidden(config, hwnd);
        const bool alreadyActive = !hiddenByUs && win::window::isActive(hwnd);
        const core::WindowPlan plan =
            core::planWindowAction(action.windowOp, action.toggle, alreadyActive, hiddenByUs);
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
        // 声明了固定桌面的程序不动窗口（用户按快捷键就是想过去，`raiseWindow`
        // 会切视图）；没声明的先把窗口拉到当前桌面，再激活它。
        QString brought;
        QString revealed;
        QString placed;
        QString rebound;
        const bool monitorMove = action.windowOp == core::WindowOp::MoveLeftMonitor
            || action.windowOp == core::WindowOp::MoveRightMonitor;
        // 被工作空间藏着的窗口只接受三类 op：
        //   * `activate`（下面会先把它那块屏切过去，窗口真的可见了再激活）；
        //   * `close` / `toggle_topmost`（不碰可见性）；
        //   * 其它 —— 最小化 / 最大化 / 还原 / 挪屏 —— 靠 `ShowWindow` /
        //     `SetWindowPlacement` 改显隐，会把 `SW_HIDE` 的窗口弹出来（M7 实测），
        //     那就与状态机的记录打架了。直接拒绝，并告诉用户先切回它那个空间。
        const bool changesVisibility = action.windowOp == core::WindowOp::Minimize
            || action.windowOp == core::WindowOp::Maximize
            || action.windowOp == core::WindowOp::Restore || monitorMove;
        if (changesVisibility && hiddenByUs) {
            win::logWarn(QStringLiteral("`%1` window: %2 is hidden in another workspace; "
                                        "switch to its workspace before running `%3` on it")
                             .arg(hotkey,
                                  core::rustDebug(win::window::windowTitle(hwnd)),
                                  core::windowOpDebugName(action.windowOp)));
            return;
        }
        if (action.windowOp == core::WindowOp::Activate) {
            if (const std::optional<QString> moved = bringWindowToCurrentDesktop(config, hwnd);
                moved.has_value()) {
                brought = *moved;
            }
            // 它在**别的工作空间**里时先切过去（在摆屏、激活之前）：
            // 1) 窗口得先真的可见，`raiseWindow()` 才能干活；
            // 2) 摆屏（`SetWindowPlacement`）对一个藏着的窗口会把它弹出来。
            revealed = dispatcher->revealWindowWorkspace(config, hwnd);
            // 然后在“该去的那块显示器”上出现（默认 = 光标所在的那一块）。
            // 搬屏必须在激活之前：激活之后再 `SetWindowPos` 会让它看起来“先跳
            // 一下”，而且最大化窗口重新进最大化时更干净。
            placed = bringWindowToActionMonitor(memory, config, action.monitor, hwnd, hotkey);
        }
        if (!win::window::applyTo(hwnd, action.windowOp, animate, &detail, &error,
                                  action.follow.value_or(false))) {
            win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
            return;
        }
        // 窗口换了显示器：四个「挪窗口」op 与 `activate` 的摆屏都算，
        // 归属得跟着它走（M9）。藏着的窗口保留空间号，看得见的并入目标屏的活动空间。
        if (action.windowOp == core::WindowOp::Activate
            || action.windowOp == core::WindowOp::MoveLeftMonitor
            || action.windowOp == core::WindowOp::MoveRightMonitor) {
            rebound = dispatcher->followWindowMonitor(config, hwnd);
        }
        QStringList notes;
        if (!brought.isEmpty()) {
            notes.append(QStringLiteral("brought to %1").arg(brought));
        }
        if (!revealed.isEmpty()) {
            notes.append(revealed);
        }
        if (!placed.isEmpty()) {
            notes.append(placed);
        }
        if (!rebound.isEmpty()) {
            notes.append(rebound);
        }
        if (notes.isEmpty()) {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2 (%3)").arg(hotkey, detail, notes.join(", ")));
        }
        return;
    }

    if (action.launch.has_value() && action.windowOp == core::WindowOp::Activate) {
        QString detail;
        QString error;
        if (!launchThenActivate(ctx, dispatcher, memory, config, action.monitor, *action.launch, query,
                                animate, &detail, &error)) {
            win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, error));
        } else {
            win::logInfo(QStringLiteral("`%1` -> %2").arg(hotkey, detail));
        }
        return;
    }
    win::logError(QStringLiteral("`%1` window: %2").arg(hotkey, win::window::missing(query)));
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
void placeWindowOnce(MonitorMemory *memory,
                     const std::shared_ptr<const core::Compiled> &config,
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
    const core::WindowRule *rule = core::firstWindowRule(config->windowRules, title, process);
    if (rule == nullptr) {
        return;
    }

    QStringList done;
    bool movedToAnotherDesktop = false;
    // 目标显示器：规则自己写了 `monitor` 就用它，否则用 `settings.window_monitor`
    // （默认 = 光标所在的那一块）；`monitor = "none"` 表示这条规则不碰显示器。
    // 解析不定（光标问不到、`"last"` 还没记忆、那块屏没接）时只记一条 warning，
    // 窗口留在原处 —— 与以前找不到目标显示器时的行为一致。
    const core::MonitorRef target = rule->monitor.value_or(config->settings.windowMonitor);
    const bool placesMonitor = target.kind != core::MonitorRef::Kind::None;
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

    if (rule->applyGeometry || placesMonitor) {
        const std::vector<core::MonitorDescription> monitors =
            core::sortedMonitors(win::monitor::list());
        // 动态目标（光标所在的那一块 / 上次记住的那一块）而且规则里没写具体几何时，
        // 这一步只负责“把它放对屏”：已经在目标那一块上就**不去碰它**（用户自己
        // 摆好的位置不该因为窗口再显示一次就被重新居中）。写死了显示器（`monitor = 2`
        // 这类）或写了 `maximize` / `x` / `y` / `width` / `height` 时照旧每次摆到位。
        const bool explicitGeometry = rule->maximize || rule->x.has_value()
            || rule->y.has_value() || rule->width.has_value() || rule->height.has_value();
        const bool placeOnly = core::monitorRefIsDynamic(target) && !explicitGeometry;
        std::optional<std::size_t> index;
        QString why;
        if (rule->monitor.has_value() && !placesMonitor) {
            // 显式关掉了显示器（`monitor = "none"`）却写了位置 / 大小：只改几何，
            // 窗口留在它现在所在的那一块屏上。
            index = win::monitor::indexForWindow(monitors, hwnd);
            if (!index.has_value()) {
                why = QStringLiteral("could not tell which monitor the window is on");
            }
        } else {
            const MonitorTarget resolved =
                resolveWindowMonitor(memory, target, monitors, hwnd, process);
            index = resolved.index;
            why = resolved.why;
        }
        if (index.has_value() && placeOnly) {
            const std::optional<std::size_t> current = win::monitor::indexForWindow(monitors, hwnd);
            if (current.has_value() && *current == *index) {
                // 已经在该在的那块屏上：什么都不做，也不是错误。
                index = std::nullopt;
                why.clear();
            }
        }
        if (!index.has_value()) {
            if (!why.isEmpty()) {
                win::logWarn(QStringLiteral("window rule `%1`: %2; leaving %3 where it is")
                                 .arg(rule->name, why, core::rustDebug(title)));
            }
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
                   MonitorMemory *memory,
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
        spec.elevated = action.elevated;
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
        executeWindowAction(ctx, dispatcher, memory, *config, action, hotkey);
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
        openWindowsAction(runtime, dispatcher, config, hotkey, action);
        break;
    case core::Action::Kind::Apps:
        openAppsAction(runtime, dispatcher, dispatcher->appCatalog(), hotkey, action);
        break;
    case core::Action::Kind::Workspace:
        dispatcher->executeWorkspaceAction(*config, action, hotkey);
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

Dispatcher::~Dispatcher() = default;

MonitorMemory *Dispatcher::monitorMemory()
{
    if (!m_monitorMemory) {
        m_monitorMemory = std::make_unique<MonitorMemory>();
        // 状态文件就在配置文件旁边（`windows.json`，见 core/window_monitor_state.h），
        // 所以常驻实例与开发/验收实例各用各的。**不在这里读盘**：只有真的用到
        // `"last"` 时才读（见 loadMonitorMemory）。
        m_monitorMemory->path = core::windowMonitorStatePath(m_runtime->configPath());
    }
    return m_monitorMemory.get();
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

    // 目录是三份来源接起来的：开始菜单（`shell:AppsFolder`）→ 控制面板
    // （「所有控制面板项」+「网络连接」）→ 内置的常用「Windows 设置」页面。
    // 后两份**与开始菜单同名的条目会被丢掉**（真机上只有「Windows 工具」一条），
    // 见 `core::appendLauncherEntries()`。
    const win::apps::StartMenuScan panel = win::apps::listControlPanelItems();
    if (!panel.error.isEmpty()) {
        // 控制面板读不出来只是少了一批条目：不能因此挡住开始菜单那一份。
        win::logWarn(QStringLiteral("app launcher (control panel): %1").arg(panel.error));
    }
    const int settingPages = static_cast<int>(core::windowsSettingsPages().size());
    std::vector<core::AppEntry> entries
        = core::appendLauncherEntries(scan.entries, panel.entries);
    entries = core::appendLauncherEntries(std::move(entries), core::builtinSettingsEntries());

    m_appCatalog = core::prepareAppEntries(std::move(entries));
    m_appScanned = true;
    m_appScannedAt = win::monotonicMs();
    win::logDebug(QStringLiteral("app launcher: %1 item(s) in shell:AppsFolder (%2 program(s)), "
                                 "%3 control panel item(s), %4 built-in setting page(s) -> "
                                 "%5 entry/entries listed (%6 hidden: %7 not a program, "
                                 "%8 uninstaller(s))")
                      .arg(scan.candidates)
                      .arg(static_cast<qulonglong>(scan.entries.size()))
                      .arg(panel.candidates)
                      .arg(settingPages)
                      .arg(static_cast<qulonglong>(m_appCatalog.size()))
                      .arg(scan.hiddenNotProgram + scan.hiddenUninstaller)
                      .arg(scan.hiddenNotProgram)
                      .arg(scan.hiddenUninstaller));
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
    core::TrayBadge badge = m_badge;
    badge.mode = m_config != nullptr ? m_config->settings.trayBadge : core::TrayBadgeMode::Both;

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
    } else {
        if (m_desktopWatchFailed) {
            m_desktopWatchFailed = false;
            win::logDebug(QStringLiteral("the current virtual desktop is readable again"));
        }
        const int number = static_cast<int>(index);
        const int total = static_cast<int>(count);
        if (number != m_badge.desktop || total != m_badge.desktopCount) {
            // 换了虚拟桌面：工作空间状态机是**按虚拟桌面分开记**的（Q1），不刷新
            // 上下文就会把上一张桌面的活动空间当成现在的。虚拟桌面切换不会给我们
            // 发事件，所以这个轮询是唯一能及时跟上的地方。
            if (m_config != nullptr && m_config->settings.workspace.enabled) {
                refreshWorkspaceDesktop();
            }
        }
        badge.desktop = number;
        badge.desktopCount = total;
    }

    if (const std::optional<core::MonitorSpaces> spaces = workspaceSpacesForBadge();
        spaces.has_value()) {
        badge.workspace = static_cast<int>(spaces->active);
        badge.workspaceCount = static_cast<int>(spaces->count);
    } else {
        badge.workspace = 0;
        badge.workspaceCount = 0;
    }

    if (badge == m_badge) {
        return;
    }
    m_badge = badge;
    const QString label = core::trayBadgeText(badge);
    win::logDebug(QStringLiteral("tray badge: %1 (workspace %2/%3, desktop %4/%5, %6)")
                      .arg(label.isEmpty() ? QStringLiteral("-") : label)
                      .arg(badge.workspace)
                      .arg(badge.workspaceCount)
                      .arg(badge.desktop)
                      .arg(badge.desktopCount)
                      .arg(core::trayBadgeModeName(badge.mode)));
    if (m_runtime != nullptr) {
        m_runtime->reportDesktop(badge);
    }
}

void Dispatcher::rememberConfig(const std::shared_ptr<const core::Compiled> &config)
{
    if (config != nullptr) {
        m_config = config;
    }
}

std::optional<core::MonitorSpaces> Dispatcher::workspaceSpacesForBadge()
{
    if (m_config == nullptr || !m_config->settings.workspace.enabled) {
        return std::nullopt; // 工作空间关着：没有“第几个空间”可言
    }
    const QString monitor = workspaceSwitchMonitor(*m_config, /*logFailures=*/false);
    if (monitor.isEmpty()) {
        return std::nullopt;
    }
    if (std::optional<core::MonitorSpaces> spaces = m_workspace.spaces(monitor);
        spaces.has_value()) {
        return spaces;
    }
    // 这块屏还没被任何一条命令碰过（没有窗口、也没切过）：活动空间的定义就是 1
    // （`WorkspaceState` 懒创建空间表时的默认值），照实报出来，别让托盘一直空着。
    core::MonitorSpaces fresh;
    fresh.count = m_config->settings.workspace.count;
    fresh.active = core::kMinWorkspaceCount;
    return fresh;
}

void Dispatcher::submit(std::shared_ptr<const core::Compiled> config, core::Trigger trigger)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), trigger]() {
            rememberConfig(config);
            execute(config, trigger);
        },
        Qt::QueuedConnection);
}

void Dispatcher::submitActions(std::shared_ptr<const core::Compiled> config,
                               QString name,
                               std::vector<core::Action> actions)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), name = std::move(name), actions = std::move(actions)]() {
            rememberConfig(config);
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
            rememberConfig(config);
            applyPlacementRules(config, event);
        },
        Qt::QueuedConnection);
}

void Dispatcher::submitWorkspace(std::shared_ptr<const core::Compiled> config,
                                 core::WorkspaceEvent event)
{
    QMetaObject::invokeMethod(
        this,
        [this, config = std::move(config), event = std::move(event)]() {
            rememberConfig(config);
            applyWorkspaceEvent(config, event);
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
            placeWindowOnce(monitorMemory(), config, hwnd, true);
        }
        // 摆完位置再定工作空间归属（M9）：规则可能刚把它搬到了另一块屏，
        // 归属要按**摆完之后**的位置算。
        applyWorkspaceRules(*config, event.windows);
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
        placeWindowOnce(monitorMemory(), config, hwnd, false);
    }
    applyWorkspaceRules(*config, windows);
}

// ---------------------------------------------------------------------------
// 工作空间：事件管线（钩子线程 → 状态机 → 平台后端）
//
// 状态机的唯一所有者就是这条线程（`plan.md` 2.2）：钩子线程只把事件入队，
// `ShowWindow` 这类可能阻塞的外部调用全在这里做（也是硬要求：`SW_HIDE` 是
// 跨线程 `SendMessage` 语义，目标进程卡死时会把调用线程挂住）。
// ---------------------------------------------------------------------------

namespace {

/// 窗口所在显示器的设备名（已规范化）；拿不到时是空串。
///
/// 工作空间用**设备名**做显示器的稳定身份（与 `window_rule` 的 `monitor` 一致），
/// 不用序号：序号会随排列顺序变化，设备名跟着那块屏走。
QString monitorKeyOf(HWND hwnd, const std::vector<core::MonitorDescription> &monitors)
{
    const std::optional<std::size_t> index = win::monitor::indexForWindow(monitors, hwnd);
    if (!index.has_value()) {
        return QString();
    }
    return core::normalizeDeviceName(monitors[*index].device);
}

} // namespace

void Dispatcher::applyWorkspaceEvent(const std::shared_ptr<const core::Compiled> &config,
                                     const core::WorkspaceEvent &event)
{
    if (!config) {
        return;
    }
    const core::WorkspaceSettings &settings = config->settings.workspace;
    // 先把空间划分与开关同步好（`enabled = false` 时也要同步：重启才打开时
    // 不会拿着一份过时的空间数）。
    syncWorkspaceLayout(*config);
    if (!settings.enabled) {
        // 关掉时当没这回事：已经藏起来的窗口由 M8 的「恢复全部窗口」兜底，
        // 而默认开启 + 没绑定的情况下这里从来不会藏着任何窗口。
        return;
    }

    switch (event.kind) {
    case core::WorkspaceEvent::Kind::Startup:
        adoptExistingWindows(*config);
        return;
    case core::WorkspaceEvent::Kind::WindowAppeared: {
        const HWND hwnd = win::workspace::handleOf(event.window);
        if (!win::workspace::isManageable(hwnd, m_workspace.tracks(event.window),
                                          settings.exclude)) {
            return;
        }
        refreshWorkspaceDesktop();
        const QString monitor = monitorKeyOf(hwnd, core::sortedMonitors(win::monitor::list()));
        if (monitor.isEmpty()) {
            win::logDebug(QStringLiteral("workspace: cannot tell which monitor %1 is on")
                              .arg(core::rustDebug(win::window::windowTitle(hwnd))));
            return;
        }
        // 归属：命中的第一条 `window_rule`（`app.workspace` 展开成它）写了 `workspace`
        // 就用它，否则看 `settings.workspace.new_window`（`0` = 那块屏的活动空间）。
        // 有规则时这一步通常已经由 `applyWorkspaceRules()` 在摆放那一遍里做过（钩子
        // **先**发 `WindowsShown`、**后**发这个事件），那时窗口已被跟踪 → 这里幂等无操作；
        // 没命中规则、或者压根没写规则的窗口由这里按 `new_window` 收编。
        // 落到非活动空间 → `windowAppeared()` 的效果列表里就带着「立刻藏起来」，
        // 而且**不抢焦点**（新窗口出现不是用户要求换窗口的时机）。
        const std::optional<std::uint32_t> target =
            core::workspaceForNewWindow(config->windowRules,
                                        win::window::windowTitle(hwnd),
                                        win::window::processName(hwnd),
                                        config->settings);
        applyWorkspaceEffects(m_workspace.windowAppeared(event.window, monitor, target),
                              QStringLiteral("window appeared"));
        if (const std::optional<core::WindowRecord> record = m_workspace.record(event.window);
            record.has_value()) {
            win::logDebug(QStringLiteral("workspace: tracking %1 on %2 in space %3")
                              .arg(core::rustDebug(win::window::windowTitle(hwnd)), monitor)
                              .arg(record->workspace.index));
        }
        return;
    }
    case core::WorkspaceEvent::Kind::WindowDestroyed: {
        // 永远不产生效果（不能去显示一个已经没了的窗口），它只是防止 `HWND` 复用把
        // 别人的窗口当成它。
        m_workspace.windowDestroyed(event.window);
        // 恢复日志里的那一份也要去掉：窗口没了，留着只会在下次恢复时白试一次。
        ensureWorkspaceLogLoaded();
        bool removed = core::removeHiddenWindow(m_workspaceLog, event.window);
        const core::WindowId target = win::workspace::idOf(
            win::workspace::manageTarget(win::workspace::handleOf(event.window)));
        if (target != event.window) {
            removed = core::removeHiddenWindow(m_workspaceLog, target) || removed;
        }
        if (removed) {
            persistWorkspaceLog(QStringLiteral("window destroyed"));
        }
        return;
    }
    case core::WorkspaceEvent::Kind::WindowMinimized:
    case core::WorkspaceEvent::Kind::WindowRestored:
        m_workspace.windowMinimized(event.window,
                                    event.kind == core::WorkspaceEvent::Kind::WindowMinimized);
        return;
    case core::WorkspaceEvent::Kind::FocusChanged:
        // 只更新「最近用过」（切回去时聚焦谁），绝不引起显隐。
        m_workspace.focusChanged(event.window);
        return;
    case core::WorkspaceEvent::Kind::MonitorsChanged: {
        // 拔出 / 接入：归并、物归原主、重算显隐全在状态机里。
        const core::WorkspaceEffects effects = m_workspace.monitorsChanged(event.monitors);
        // 先把物理位置向记录看齐，再执行显隐：被并进活动空间的窗口要在**新位置**上
        // 露出来，而不是先出现在旧坐标、再被搬一次。
        // `window_rule` 的重新归位（决策 13）排在后面（钩子线程先发这一条、再发
        // `PlacementEvent::MonitorConnected`），因此规则真的把窗口另放了时，规则赢。
        relocateDisplacedWindows();
        applyWorkspaceEffects(effects, QStringLiteral("monitors changed"));
        return;
    }
    case core::WorkspaceEvent::Kind::WindowMoved: {
        // 位置变化（已由钩子线程按 350 ms 防抖，每个窗口每个 tick 最多一条）。
        // 这里只回答一个问题：它是不是换了一块屏？
        //
        // 为什么不拿 `IsWindowVisible` 筛：被我们藏起来的窗口不会动，而拖得动的
        // 本来就是可见的。
        const std::optional<core::WindowRecord> record = m_workspace.record(event.window);
        if (!record.has_value()) {
            return; // 还没收编的窗口：等 `WindowAppeared` 先记账
        }
        const std::vector<core::MonitorDescription> monitors =
            core::sortedMonitors(win::monitor::list());
        if (monitors.empty()) {
            return;
        }
        QStringList present;
        present.reserve(static_cast<int>(monitors.size()));
        for (const core::MonitorDescription &description : monitors) {
            present.append(core::normalizeDeviceName(description.device));
        }
        // 记录里那块屏已经不在了：这一条事件是 Windows 替我们把窗口搬走的，归属交给
        // `monitorsChanged()`（它会记下 `homeMonitor`，重新接入时才能物归原主）。
        // 不跳过的话，窗口在重接之后就会留在剩下的那块屏上。
        if (!present.contains(core::normalizeDeviceName(record->workspace.monitor))) {
            return;
        }
        const HWND hwnd = win::workspace::handleOf(event.window);
        if (!win::workspace::isAlive(hwnd)) {
            return;
        }
        const QString monitor = monitorKeyOf(hwnd, monitors);
        if (monitor.isEmpty() || monitor == record->workspace.monitor) {
            return; // 没换屏（或者问不出来）：归属不动
        }
        // 归属怎么跟：
        //   * 看得见的窗口 = 用户拖过来的（或被 `Win+Shift+←/→` 送过来的）→
        //     归入目标屏的**活动空间**（决策 16 / M7）；
        //   * 藏着的窗口不可能被拖（`SW_HIDE` 之后拖不动）→ 只可能是我们自己搬的
        //     （`window_rule` 摆屏）→ **保留空间号**，否则规则刚设好的归属会被
        //     这一条事件抹掉。
        const bool hidden = !m_workspace.isVisible(event.window);
        const core::WorkspaceEffects effects =
            hidden ? m_workspace.windowReboundToMonitor(event.window, monitor)
                   : m_workspace.windowMovedToMonitor(event.window, monitor);
        applyWorkspaceEffects(effects,
                              QStringLiteral("window moved to %1").arg(monitor));
        win::logDebug(QStringLiteral("workspace: %1 moved from %2 to %3")
                          .arg(core::rustDebug(win::window::windowTitle(hwnd)),
                               record->workspace.monitor,
                               monitor));
        return;
    }
    }
}

void Dispatcher::adoptExistingWindows(const core::Compiled &config)
{
    const core::WorkspaceSettings &settings = config.settings.workspace;
    std::vector<HWND> tracked;
    for (const core::WindowId id : m_workspace.trackedWindows()) {
        tracked.push_back(win::workspace::handleOf(id));
    }
    // `enumerateManageable` 不能用 `IsWindowVisible` 当“在不在”的判据（M0），所以把
    // 已记录的窗口一起传进去。
    const std::vector<HWND> windows = win::workspace::enumerateManageable(tracked, settings.exclude);
    refreshWorkspaceDesktop();
    const std::vector<core::MonitorDescription> monitors = core::sortedMonitors(win::monitor::list());

    // 只收编**当前虚拟桌面**上的窗口：`EnumWindows` 会连别的桌面上的窗口一起列出来
    // （虚拟桌面用 DWM cloak 藏窗口，`IsWindowVisible` 仍然是真）。收编错了会在切空间
    // 时把一个别的桌面上的窗口藏掉。逐个问 COM 太贵，一次批量问完（决策 13）。
    std::vector<core::WindowId> candidates;
    candidates.reserve(windows.size());
    for (HWND hwnd : windows) {
        const core::WindowId id = win::workspace::idOf(hwnd);
        if (!m_workspace.tracks(id)) {
            candidates.push_back(id);
        }
    }

    int adopted = 0;
    for (const core::WindowId id : windowsOnThisDesktop(candidates)) {
        const HWND hwnd = win::workspace::handleOf(id);
        const QString monitor = monitorKeyOf(hwnd, monitors);
        if (monitor.isEmpty()) {
            continue;
        }
        // 收编只记账：它们本来就在那块屏的活动空间里，不隐藏也不抢焦点。
        // **`settings.workspace.new_window` 不在这里生效**：它管的是「flowkeyd 起来之后
        // 新出现的窗口」。对已经在的窗口套一遍会把用户现有的窗口全部藏起来（重启 /
        // reload 一次就「全没了」），而「用户自己的安排」本来就靠 `workspace.json`
        // 恢复可见性、不恢复归属。
        applyWorkspaceEffects(m_workspace.windowAppeared(id, monitor), QStringLiteral("startup"));
        ++adopted;
    }
    win::logInfo(QStringLiteral("workspace: tracking %1 window(s) on %2 monitor(s), %3 "
                                "space(s) each%4")
                     .arg(adopted)
                     .arg(monitors.size())
                     .arg(static_cast<int>(m_workspace.count()))
                     .arg(m_workspaceDesktop.isEmpty()
                              ? QStringLiteral(" (the virtual desktop is unknown)")
                              : QString()));
}

void Dispatcher::syncWorkspaceLayout(const core::Compiled &config)
{
    const std::uint32_t count = config.settings.workspace.count;
    if (!m_workspaceConfigured) {
        m_workspace = core::WorkspaceState(count);
        m_workspaceConfigured = true;
        return;
    }
    if (m_workspace.count() == count) {
        return;
    }
    win::logInfo(QStringLiteral("workspace: %1 space(s) per monitor now").arg(static_cast<int>(count)));
    // 比新数量大的空间并入最后一个；本来藏着、现在落进活动空间的窗口必须露出来
    // （否则它们会永久藏在“活动空间”里，而对活动空间切换是幂等无操作）。
    applyWorkspaceEffects(m_workspace.reconfigure(count), QStringLiteral("workspace count changed"));
}

void Dispatcher::relocateDisplacedWindows()
{
    // 只在显示器集合真的变了的时候叫（`MonitorsChanged`）：这里会逐个窗口问注意
    // 「它在哪儿」，没必要在每次事件上都做。
    const std::vector<core::MonitorDescription> monitors = core::sortedMonitors(win::monitor::list());
    if (monitors.empty()) {
        return;
    }
    QStringList present;
    present.reserve(static_cast<int>(monitors.size()));
    for (const core::MonitorDescription &description : monitors) {
        present.append(core::normalizeDeviceName(description.device));
    }

    int moved = 0;
    for (const core::WindowId id : m_workspace.trackedWindows()) {
        const std::optional<core::WindowRecord> record = m_workspace.record(id);
        const HWND hwnd = win::workspace::handleOf(id);
        if (!record.has_value() || !win::workspace::isAlive(hwnd)) {
            continue;
        }
        // 显隐与归属以**记录**为准，所以目标是记录里那块屏。
        const int index = present.indexOf(core::normalizeDeviceName(record->workspace.monitor));
        if (index < 0) {
            // 记录里那块屏已经不在了，本来应该由 `monitorsChanged()` 先把归属改掉。
            // 真走到这里（例如状态机还没听说过那块屏）就什么都别动：宁可不动窗口，
            // 也不要按一个猜出来的目标搬。
            continue;
        }
        const core::MonitorDescription &target = monitors[static_cast<std::size_t>(index)];
        const std::optional<core::Rect> rect = win::monitor::windowRect(hwnd);
        if (!rect.has_value() || !core::windowIsOffMonitor(*rect, target)) {
            continue; // 它就好端端地在那块屏上（含跨屏的窗口）：不动
        }
        const QString title = win::window::windowTitle(hwnd);
        QString error;
        if (!win::workspace::relocateToMonitor(hwnd, target, &error)) {
            win::logWarn(QStringLiteral("workspace: cannot move %1 back to %2: %3")
                             .arg(core::rustDebug(title), record->workspace.monitor, error));
            continue;
        }
        ++moved;
        win::logDebug(QStringLiteral("workspace: moved %1 back to %2 (it was elsewhere)")
                          .arg(core::rustDebug(title), record->workspace.monitor));
    }
    if (moved > 0) {
        win::logInfo(QStringLiteral("workspace: moved %1 window(s) back onto their monitor")
                         .arg(moved));
    }
}

void Dispatcher::refreshWorkspaceDesktop()
{
    QString error;
    const std::optional<QString> id = win::desktop::currentDesktopId(&error);
    if (!id.has_value()) {
        // 锁屏 / 版本表对不上：保留上一次的键。猜一个比不知道更糟。
        win::logDebug(QStringLiteral("workspace: could not read the current virtual desktop: %1")
                          .arg(error));
        return;
    }
    if (*id == m_workspaceDesktop) {
        return;
    }
    m_workspaceDesktop = *id;
    // 状态机本身不产生效果：虚拟桌面的显隐归 Windows（它按 `WS_VISIBLE` 决定这张
    // 桌面上露不露面），而我们藏起来的窗口 `WS_VISIBLE` 已经是假的，回来时仍然藏着。
    m_workspace.setDesktop(*id);
}

void Dispatcher::applyWorkspaceEffects(const core::WorkspaceEffects &effects, const QString &reason)
{
    // 只处理**当前虚拟桌面**上的窗口：记录可能已经过时（窗口被 `Win+Ctrl+Shift+←/→` 送到
    // 别的桌面去了），藏错窗口会让用户回到那张桌面时「窗口不见了」。要藏的窗口此刻都是
    // 可见的、问得出来；要显示的是我们自己藏起来的、问不出来（M0）—— 所以只过滤 `hide`。
    const std::vector<core::WindowId> hide = windowsOnThisDesktop(effects.hide);
    if (effects.show.empty() && hide.empty() && !effects.focus.has_value()) {
        return;
    }
    ensureWorkspaceLogLoaded();
    // 日志里的键 = **真正要藏 / 显的那个句柄**（UWP 是宿主窗口 `ApplicationFrameWindow`），
    // 与 `manageTarget()` 处处保持同一个口径。
    const auto logKey = [](core::WindowId id) {
        return win::workspace::idOf(win::workspace::manageTarget(win::workspace::handleOf(id)));
    };

    // --- 预写日志（M8）-----------------------------------------------------
    //
    // **先写日志再隐藏**：进程要是死在这之后（或是被 `taskkill /F` 杀掉），下次启动
    // 能靠它把窗口全部放回来。所以写不进去就干脆不藏 —— 宁可让“切换工作空间”这件事
    // 失败，也不要造出一批永远消失的窗口。
    std::vector<core::WindowId> hideKeys;
    hideKeys.reserve(hide.size());
    bool mayHide = true;
    bool logChanged = false;
    if (!hide.empty()) {
        core::WorkspaceLog pending = m_workspaceLog;
        bool added = false;
        for (const core::WindowId id : hide) {
            const core::WindowId key = logKey(id);
            hideKeys.push_back(key);
            if (key == 0 || core::hasHiddenWindow(pending, key)) {
                continue;
            }
            core::HiddenWindow entry;
            entry.window = key;
            entry.identity = win::workspace::identityOf(win::workspace::handleOf(key));
            if (const std::optional<core::WindowRecord> record = m_workspace.record(id);
                record.has_value()) {
                entry.workspace = record->workspace;
            }
            entry.title = win::window::windowTitle(win::workspace::handleOf(key));
            core::addHiddenWindow(pending, entry);
            added = true;
        }
        if (added) {
            QString error;
            if (m_workspaceConfigPath.isEmpty()
                || win::workspace::saveWorkspaceLog(m_workspaceConfigPath, pending, &error)) {
                m_workspaceLog = std::move(pending);
            } else {
                mayHide = false;
                win::logWarn(QStringLiteral(
                                "workspace: cannot write the recovery log for %1: %2")
                                .arg(reason, error));
                win::logWarn(QStringLiteral("workspace: leaving %1 window(s) visible: without "
                                            "the recovery log a crash would hide them for good")
                                .arg(hide.size()));
            }
        }
    }

    const std::shared_ptr<core::ExpectedWorkspaceEvents> expected =
        m_runtime != nullptr ? m_runtime->workspaceEvents() : nullptr;
    const std::uint64_t now = win::monotonicMs();
    // 先登记期望事件再动手：`SW_HIDE` 会额外产生一条 `EVENT_OBJECT_SHOW`（M0 实测），
    // 不吞掉就等于让状态机把自己的操作当成用户操作（自触发循环）。
    if (expected != nullptr) {
        for (const core::WindowId id : effects.show) {
            expected->expect(core::WorkspaceEvent::Kind::WindowAppeared, id, now);
        }
        for (const core::WindowId id : hide) {
            expected->expect(core::WorkspaceEvent::Kind::WindowAppeared, id, now);
        }
    }

    int shown = 0;
    int hidden = 0;
    int failed = 0;
    // 底 → 顶显示，然后按同一份记录栈还原 Z 序。
    //
    // **不用 `show()` 的 `insertAfter`**：`SetWindowPos(hwnd, 前一个)` 把 `hwnd` 摆在
    // 前一个**下面**，而这里要的是上面（M0 的探针是「顶 → 底」记录的，方向正好反）；
    // 一条 `restack()` 比逐对摆位置更不容易错。
    std::vector<HWND> shownWindows;
    shownWindows.reserve(effects.show.size());
    for (const core::WindowId id : effects.show) {
        const HWND hwnd = win::workspace::manageTarget(win::workspace::handleOf(id));
        if (hwnd == nullptr) {
            ++failed;
            continue;
        }
        QString error;
        if (!win::workspace::show(hwnd, nullptr, &error)) {
            ++failed;
            win::logWarn(QStringLiteral("workspace: cannot show %1: %2")
                             .arg(core::rustDebug(win::window::windowTitle(hwnd)), error));
            continue;
        }
        shownWindows.push_back(hwnd);
        ++shown;
        // 真的露出来了才从日志里去掉（显示失败的那条留着，下次 / 手动恢复再试）。
        if (core::removeHiddenWindow(m_workspaceLog, win::workspace::idOf(hwnd))) {
            logChanged = true;
        }
    }
    if (shownWindows.size() > 1) {
        QString error;
        if (!win::workspace::restack(shownWindows, &error)) {
            win::logWarn(QStringLiteral("workspace: could not restore the Z order after %1: %2")
                             .arg(reason, error));
        }
    }
    // 顶 → 底：先藏最上面那个，剩下的窗口不会先闪一下。
    for (std::size_t i = 0; i < hide.size(); ++i) {
        if (!mayHide) {
            // 日志写不进去：整个 hide 都不会发生（上面已经 warning 过，不再逐窗口刷屏）。
            ++failed;
            continue;
        }
        const core::WindowId id = hide.at(i);
        const core::WindowId key = i < hideKeys.size() ? hideKeys.at(i) : logKey(id);
        const HWND hwnd = win::workspace::manageTarget(win::workspace::handleOf(id));
        const auto dropLogEntry = [this, key, &logChanged]() {
            if (core::removeHiddenWindow(m_workspaceLog, key)) {
                logChanged = true;
            }
        };
        if (hwnd == nullptr) {
            // 预写的时候还在、现在没了：把那条记下来就永远放不回去了。
            ++failed;
            dropLogEntry();
            continue;
        }
        QString error;
        if (!win::workspace::hide(hwnd, &error)) {
            // 目标进程卡死 / UIPI（Q7：不提权的实例管不了提权窗口）：跳过 + warning。
            ++failed;
            dropLogEntry();
            win::logWarn(QStringLiteral("workspace: cannot hide %1: %2")
                             .arg(core::rustDebug(win::window::windowTitle(hwnd)), error));
            continue;
        }
        ++hidden;
    }
    if (logChanged) {
        persistWorkspaceLog(reason);
    }
    // 焦点不可省：M0 实测隐藏前台窗口之后 `GetForegroundWindow()` 仍然指着那个隐藏
    // 窗口，系统不会自己把前台交给别人。
    if (effects.focus.has_value()) {
        const HWND hwnd = win::workspace::manageTarget(win::workspace::handleOf(*effects.focus));
        if (hwnd != nullptr && !win::window::raiseWindow(hwnd)) {
            win::logWarn(QStringLiteral("workspace: could not focus %1 after %2")
                             .arg(core::rustDebug(win::window::windowTitle(hwnd)), reason));
        }
    }
    win::logInfo(QStringLiteral("workspace: %1 -> %2 shown, %3 hidden%4")
                     .arg(reason)
                     .arg(shown)
                     .arg(hidden)
                     .arg(failed > 0 ? QStringLiteral(", %1 failed").arg(failed) : QString()));
}

void Dispatcher::submitWorkspaceRestore()
{
    QMetaObject::invokeMethod(
        this,
        [this]() { restoreWorkspaceWindows(QStringLiteral("restore requested")); },
        Qt::QueuedConnection);
}

bool Dispatcher::restoreWorkspaceNow(int timeoutMs)
{
    auto done = std::make_shared<QSemaphore>();
    QMetaObject::invokeMethod(
        this,
        [this, done]() {
            restoreWorkspaceWindows(QStringLiteral("shutting down"));
            done->release();
        },
        Qt::QueuedConnection);
    if (timeoutMs <= 0) {
        return true;
    }
    if (done->tryAcquire(1, timeoutMs)) {
        return true;
    }
    // 动作线程正卡在某个长动作里（`run` / `wait`）。继续退出：日志里还留着记录，
    // 下次启动会再恢复一次。
    win::logWarn(QStringLiteral("workspace: the action thread did not restore the hidden windows "
                                "within %1 ms; the recovery log keeps them for the next start")
                     .arg(timeoutMs));
    return false;
}

void Dispatcher::ensureWorkspaceLogLoaded()
{
    if (m_workspaceLogLoaded) {
        return;
    }
    m_workspaceLogLoaded = true;
    const QString configPath = m_runtime != nullptr ? m_runtime->configPath() : QString();
    if (configPath.isEmpty()) {
        // 没有配置路径（单测 / 嵌入式用法）：不落盘，只在内存里记。
        win::logDebug(QStringLiteral("workspace: no configuration path; the recovery log is off"));
        return;
    }
    m_workspaceConfigPath = configPath;
    m_workspaceLog = win::workspace::loadWorkspaceLog(configPath);
    if (!m_workspaceLog.isEmpty()) {
        // 启动恢复（`main.cpp`）已经跑过一遍：剩下的那些它没能恢复（进程卡住之类），
        // 接着用它们，而不是直接覆盖成空。
        win::logWarn(QStringLiteral("workspace: %1 window(s) are still marked as hidden; they "
                                    "will be restored")
                         .arg(m_workspaceLog.windows.size()));
    }
}

bool Dispatcher::persistWorkspaceLog(const QString &reason)
{
    if (m_workspaceConfigPath.isEmpty()) {
        return true;
    }
    QString error;
    if (!win::workspace::saveWorkspaceLog(m_workspaceConfigPath, m_workspaceLog, &error)) {
        win::logWarn(QStringLiteral("workspace: cannot update the recovery log after %1: %2")
                         .arg(reason, error));
        return false;
    }
    return true;
}

void Dispatcher::restoreWorkspaceWindows(const QString &reason)
{
    ensureWorkspaceLogLoaded();
    // 状态机里的窗口：全部落回各自显示器的活动空间并显示出来（`restoreAll()`）。
    applyWorkspaceEffects(m_workspace.restoreAll(), reason);
    // 日志里可能还有状态机不认识的窗口（上一轮留下的、启动时没能恢复的）：也试一次，
    // 能放回来的就从日志里去掉，剩下的是真的还失败的。
    if (!m_workspaceLog.isEmpty()) {
        const win::workspace::RestoreResult result =
            win::workspace::restoreHiddenWindows(m_workspaceLog);
        m_workspaceLog = result.remaining;
        persistWorkspaceLog(reason);
        win::logInfo(QStringLiteral("workspace: recovery log -> %1 restored, %2 gone, %3 still "
                                    "failing")
                         .arg(result.report.restored)
                         .arg(result.report.dropped)
                         .arg(result.report.failed));
    }
}

std::vector<core::WindowId> Dispatcher::windowsOnThisDesktop(const std::vector<core::WindowId> &windows)
{
    if (windows.empty()) {
        return windows;
    }
    std::vector<HWND> handles;
    handles.reserve(windows.size());
    for (const core::WindowId id : windows) {
        // UWP 要问的是宿主窗口（`ApplicationFrameWindow`）—— 与显隐用的是同一个句柄。
        handles.push_back(win::workspace::manageTarget(win::workspace::handleOf(id)));
    }
    QString error;
    const std::vector<std::optional<bool>> onCurrent =
        win::desktop::windowsOnCurrentDesktop(handles, &error);
    if (onCurrent.size() != windows.size()) {
        // 后端的契约是“一一对应”。真出了偏差就**不过滤**：宁可不藏，也不要藏错。
        win::logDebug(QStringLiteral("workspace: cannot check which virtual desktop %1 window(s) "
                                     "are on: %2")
                          .arg(windows.size())
                          .arg(error.isEmpty() ? QStringLiteral("unexpected result") : error));
        return windows;
    }
    // `std::nullopt`（问不出来：被我们藏起来的窗口、打包应用宿主、COM 失败）当成“在当前
    // 桌面”（M0：不然回切时会把该显示的窗口漏掉）。
    std::vector<core::WindowId> out = core::filterToCurrentDesktop(windows, onCurrent);
    if (out.size() != windows.size()) {
        win::logDebug(QStringLiteral("workspace: skipping %1 window(s) on another virtual desktop")
                          .arg(windows.size() - out.size()));
    }
    return out;
}

QString Dispatcher::workspaceSwitchMonitor(const core::Compiled &config, bool logFailures)
{
    const std::vector<core::MonitorDescription> monitors =
        core::sortedMonitors(win::monitor::list());
    const auto deviceAt = [&monitors](const std::optional<std::size_t> index) {
        if (!index.has_value() || *index >= monitors.size()) {
            return QString();
        }
        return core::normalizeDeviceName(monitors[*index].device);
    };
    if (config.settings.workspace.switchTarget == core::WorkspaceSwitchTarget::Focus) {
        // 「焦点窗口所在的那块屏」：与 `window` 动作同一个找法 —— 前台是覆盖层
        // （例如 PowerToys 的「快捷键指南」）时跳过它（决策 19）。
        const HWND hwnd = win::window::find(core::WindowQuery::make(std::nullopt, std::nullopt));
        if (hwnd == nullptr) {
            if (logFailures) {
                win::logDebug(QStringLiteral("workspace: there is no foreground window; "
                                             "switching on the cursor's monitor instead"));
            }
        } else {
            const QString device = deviceAt(win::monitor::indexForWindow(monitors, hwnd));
            if (!device.isEmpty()) {
                return device;
            }
            if (logFailures) {
                win::logDebug(QStringLiteral("workspace: cannot tell which monitor %1 is on; "
                                             "switching on the cursor's monitor instead")
                                  .arg(core::rustDebug(win::window::windowTitle(hwnd))));
            }
        }
    }
    const QString device = deviceAt(win::monitor::indexForCursor(monitors));
    if (device.isEmpty() && logFailures) {
        win::logDebug(QStringLiteral("workspace: cannot tell which monitor the cursor is on"));
    }
    return device;
}

std::vector<HWND> Dispatcher::trackedWorkspaceWindows() const
{
    std::vector<HWND> handles;
    const std::vector<core::WindowId> ids = m_workspace.trackedWindows();
    handles.reserve(ids.size());
    for (const core::WindowId id : ids) {
        // UWP 要问 / 要列的是宿主窗口（`ApplicationFrameWindow`），与显隐同一个口径。
        const HWND hwnd = win::workspace::manageTarget(win::workspace::handleOf(id));
        if (hwnd != nullptr) {
            handles.push_back(hwnd);
        }
    }
    return handles;
}

bool Dispatcher::workspaceSaysHidden(const core::Compiled &config, HWND hwnd) const
{
    if (!config.settings.workspace.enabled || hwnd == nullptr) {
        return false;
    }
    const core::WindowId id = win::workspace::idOf(hwnd);
    if (!m_workspace.tracks(id)) {
        return false;
    }
    // “在不在”只看记录（M0：`IsWindowVisible` 在 `SW_HIDE` 之后是假的）。
    return !m_workspace.isVisible(id);
}

QString Dispatcher::revealWindowWorkspace(const core::Compiled &config, HWND hwnd)
{
    if (!config.settings.workspace.enabled || hwnd == nullptr) {
        return QString();
    }
    syncWorkspaceLayout(config);
    const core::WindowId id = win::workspace::idOf(hwnd);
    if (!m_workspace.tracks(id)) {
        return QString(); // 还没被收编（刚启动 / 规则还没跑）：没有状态可切
    }
    // 虚拟桌面切来切去不会给我们发事件（Windows 用的是 DWM cloak），所以切之前先
    // 刷新一下上下文：`revealWindow()` 要拿它判断「记录里那张桌面是不是当前这张」。
    refreshWorkspaceDesktop();
    const core::WorkspaceEffects effects = m_workspace.revealWindow(id);
    const std::optional<core::WindowRecord> after = m_workspace.record(id);
    if (effects.isEmpty()) {
        return QString();
    }
    applyWorkspaceEffects(effects, QStringLiteral("revealing %1")
                                      .arg(core::rustDebug(win::window::windowTitle(hwnd))));
    if (!after.has_value()) {
        return QString();
    }
    return QStringLiteral("workspace %1 on %2")
        .arg(after->workspace.index)
        .arg(after->workspace.monitor);
}

QString Dispatcher::followWindowMonitor(const core::Compiled &config, HWND hwnd)
{
    if (!config.settings.workspace.enabled || hwnd == nullptr) {
        return QString();
    }
    const core::WindowId id = win::workspace::idOf(hwnd);
    if (!m_workspace.tracks(id)) {
        return QString(); // 没收编：等 `WindowAppeared` 先记账
    }
    const std::optional<core::WindowRecord> before = m_workspace.record(id);
    if (!before.has_value()) {
        return QString();
    }
    const QString monitor = monitorKeyOf(hwnd, core::sortedMonitors(win::monitor::list()));
    if (monitor.isEmpty() || monitor == before->workspace.monitor) {
        return QString();
    }
    // 看得见的窗口并入目标屏的活动空间（与拖动一致）；藏着的窗口保留空间号
    // （它是被我们搬的，空间号是规则 / 用户给的）。
    const bool hidden = !m_workspace.isVisible(id);
    const core::WorkspaceEffects effects = hidden
        ? m_workspace.windowReboundToMonitor(id, monitor)
        : m_workspace.windowMovedToMonitor(id, monitor);
    applyWorkspaceEffects(effects, QStringLiteral("workspace follows the monitor"));
    const std::optional<core::WindowRecord> after = m_workspace.record(id);
    if (!after.has_value()) {
        return QString();
    }
    win::logDebug(QStringLiteral("workspace: %1 follows %2 -> %3, space %4 (%5)")
                      .arg(core::rustDebug(win::window::windowTitle(hwnd)),
                           before->workspace.monitor,
                           monitor,
                           QString::number(after->workspace.index),
                           hidden ? QStringLiteral("hidden, kept its space")
                                  : QStringLiteral("visible")));
    return QStringLiteral("workspace %1 on %2").arg(after->workspace.index).arg(monitor);
}

void Dispatcher::applyWorkspaceRules(const core::Compiled &config,
                                      const std::vector<HWND> &windows)
{
    const core::WorkspaceSettings &settings = config.settings.workspace;
    if (!settings.enabled || windows.empty() || config.windowRules.empty()) {
        return;
    }
    syncWorkspaceLayout(config);

    // 先解析「命中哪条规则、规则要它去哪个空间」。绝大多数窗口没有规则、或者规则里
    // 没写 `workspace`，这一步与 `placeWindowOnce()` 用的是同一个匹配。
    struct RuleTarget
    {
        core::WindowId id = 0;
        std::uint32_t index = 1;
    };
    std::vector<RuleTarget> targets;
    bool needsDesktop = false;
    for (HWND hwnd : windows) {
        if (hwnd == nullptr || !win::workspace::isAlive(hwnd)) {
            continue;
        }
        const core::WindowId id = win::workspace::idOf(hwnd);
        const bool tracked = m_workspace.tracks(id);
        if (!win::workspace::isManageable(hwnd, tracked, settings.exclude)) {
            continue;
        }
        const core::WindowRule *rule =
            core::firstWindowRule(config.windowRules,
                                  win::window::windowTitle(hwnd),
                                  win::window::processName(hwnd));
        if (rule == nullptr || !rule->workspace.has_value()) {
            continue;
        }
        needsDesktop = needsDesktop || !tracked;
        targets.push_back(RuleTarget{id, *rule->workspace});
    }
    if (targets.empty()) {
        return;
    }
    if (needsDesktop) {
        refreshWorkspaceDesktop();
    }

    // 只处理**当前虚拟桌面**上的窗口：`topLevelWindows()` 会连别的虚拟桌面上的窗口
    // 一起列出来（虚拟桌面用 DWM cloak 藏窗口，`IsWindowVisible` 仍然是真）。
    std::vector<core::WindowId> ids;
    ids.reserve(targets.size());
    for (const RuleTarget &target : targets) {
        ids.push_back(target.id);
    }
    const std::vector<core::WindowId> current = windowsOnThisDesktop(ids);
    const std::vector<core::MonitorDescription> monitors =
        core::sortedMonitors(win::monitor::list());

    int applied = 0;
    for (const RuleTarget &target : targets) {
        if (std::find(current.begin(), current.end(), target.id) == current.end()) {
            continue;
        }
        const HWND hwnd = win::workspace::handleOf(target.id);
        const QString title = win::window::windowTitle(hwnd);
        core::WorkspaceEffects effects;
        if (m_workspace.tracks(target.id)) {
            // 已经跟踪着（启动时先收编过，或者显示器重接入）：只改归属。
            const std::optional<core::WindowRecord> before = m_workspace.record(target.id);
            if (before.has_value() && before->workspace.index == target.index) {
                continue; // 规则要的就是它现在这个空间：无操作
            }
            effects = m_workspace.moveWindow(target.id, target.index);
        } else {
            const QString monitor = monitorKeyOf(hwnd, monitors);
            if (monitor.isEmpty()) {
                continue;
            }
            effects = m_workspace.windowAppeared(target.id, monitor, target.index);
        }
        // 规则归位**不抢焦点**（与 `window_rule` 一贯的行为一致）：启动 / 显示器
        // 重接入都不是用户要求换窗口的时机。
        effects.focus.reset();
        applyWorkspaceEffects(effects, QStringLiteral("window rule put %1 in space %2")
                                          .arg(core::rustDebug(title))
                                          .arg(target.index));
        ++applied;
    }
    if (applied > 0) {
        win::logInfo(QStringLiteral("workspace: %1 window(s) placed by a window rule")
                         .arg(applied));
    }
}

void Dispatcher::executeWorkspaceAction(const core::Compiled &config,
                                       const core::Action &action,
                                       const QString &hotkey)
{
    const core::WorkspaceSettings &settings = config.settings.workspace;
    if (!settings.enabled) {
        win::logWarn(QStringLiteral("`%1` workspace %2: the feature is disabled "
                                    "(settings.workspace.enabled = false)")
                         .arg(hotkey, core::workspaceOpName(action.workspaceOp)));
        return;
    }
    syncWorkspaceLayout(config);
    // 只处理**当前虚拟桌面**上的窗口（Q1）：切虚拟桌面不会给工作空间发事件（Windows
    // 用的是 DWM cloak，不是显隐），所以每次动作前都刷新一次上下文 —— 否则人在桌面 B
    // 时会把桌面 A 的窗口藏掉。
    refreshWorkspaceDesktop();
    const std::vector<core::MonitorDescription> monitors =
        core::sortedMonitors(win::monitor::list());
    if (monitors.empty()) {
        win::logWarn(QStringLiteral("`%1` workspace %2: no monitors")
                         .arg(hotkey, core::workspaceOpName(action.workspaceOp)));
        return;
    }

    const bool moveOp = action.workspaceOp == core::WorkspaceOp::Move
                        || action.workspaceOp == core::WorkspaceOp::MovePrev
                        || action.workspaceOp == core::WorkspaceOp::MoveNext;
    if (moveOp) {
        const QString op = core::workspaceOpName(action.workspaceOp);
        // 目标 = **当前前台窗口**（决策 19 的「跳过覆盖层」找法）。
        const HWND hwnd = win::window::find(core::WindowQuery::make(std::nullopt, std::nullopt));
        if (hwnd == nullptr) {
            win::logWarn(QStringLiteral("`%1` workspace %2: there is no foreground window")
                             .arg(hotkey, op));
            return;
        }
        const core::WindowId id = win::workspace::idOf(hwnd);
        const QString title = win::window::windowTitle(hwnd);
        const bool tracked = m_workspace.tracks(id);
        if (!win::workspace::isManageable(hwnd, tracked, settings.exclude)) {
            win::logWarn(QStringLiteral("`%1` workspace %2: %3 is not managed by the workspace "
                                        "feature (not a main window, an excluded program, or one "
                                        "of flowkeyd's own windows)")
                             .arg(hotkey, op, core::rustDebug(title)));
            return;
        }
        if (!tracked) {
            // 还没收编（工作空间关着时启动、或者事件漏了）：先按它现在所在的显示器记账，
            // 否则 `moveWindow()` 会把「不认识的窗口」直接忽略。
            const QString monitor = monitorKeyOf(hwnd, monitors);
            if (monitor.isEmpty()) {
                win::logWarn(QStringLiteral("`%1` workspace %2: cannot tell which monitor %3 is on")
                                 .arg(hotkey, op, core::rustDebug(title)));
                return;
            }
            applyWorkspaceEffects(m_workspace.windowAppeared(id, monitor),
                                  QStringLiteral("window appeared"));
        }
        // 相对搬移（`workspace("move_prev")` / `workspace("move_next")`）：目标 =
        // 这个窗口**自己那个空间** ± 1；到头了就是什么都不做（**不循环**，与相对
        // 切换的口径一致）。状态机只认绝对空间号，相对那一步在这里算 ——
        // `core::stepIndex()` 是纯函数，所以"不循环"的语义仍然只有一处实现。
        std::uint32_t target = action.workspaceIndex;
        const std::optional<core::WindowRecord> before = m_workspace.record(id);
        const core::MonitorKey monitorOfWindow = before.has_value() ? before->workspace.monitor : QString();
        if (action.workspaceOp != core::WorkspaceOp::Move) {
            const std::uint32_t current = before.has_value() ? before->workspace.index : 1;
            const std::optional<std::size_t> stepped = core::stepIndex(
                m_workspace.count(),
                current > 0 ? current - 1 : 0,
                action.workspaceOp == core::WorkspaceOp::MovePrev ? -1 : 1,
                /*wrap=*/false);
            if (!stepped.has_value()) {
                win::logInfo(QStringLiteral("`%1` -> workspace %2: already at space %3 on %4")
                                 .arg(hotkey, op)
                                 .arg(current)
                                 .arg(monitorOfWindow));
                return;
            }
            target = static_cast<std::uint32_t>(*stepped) + 1;
        }
        std::uint32_t activeBefore = 0; // 0 = 不知道
        if (const std::optional<core::MonitorSpaces> spaces = m_workspace.spaces(monitorOfWindow)) {
            activeBefore = spaces->active;
        }
        const bool follow = action.follow.value_or(false);
        const core::WorkspaceEffects effects = m_workspace.moveWindow(id, target, follow);
        const std::optional<core::WindowRecord> record = m_workspace.record(id);
        const std::uint32_t space = record.has_value() ? record->workspace.index : target;
        applyWorkspaceEffects(effects,
                             QStringLiteral("moving %1 to space %2")
                                 .arg(core::rustDebug(title))
                                 .arg(space));
        // 视图真的跟着切了：那块屏的活动空间变了（只可能是 `follow` 干的）。
        std::uint32_t activeAfter = activeBefore;
        if (const std::optional<core::MonitorSpaces> spaces = m_workspace.spaces(monitorOfWindow)) {
            activeAfter = spaces->active;
        }
        const bool followed = follow && activeBefore != 0 && activeAfter != activeBefore;
        QString detail = m_workspace.isVisible(id) ? QStringLiteral("visible")
                                                    : QStringLiteral("hidden");
        if (followed) {
            detail += QStringLiteral(", view followed");
        }
        win::logInfo(QStringLiteral("`%1` -> workspace %2 %3 (%4 on %5, %6)")
                         .arg(hotkey, op)
                         .arg(space)
                         .arg(core::rustDebug(title), monitorOfWindow)
                         .arg(detail));
        if (followed) {
            // 托盘上的徽标现在就该变成新的空间号（与 `switch` 一致）。
            pollDesktop();
        }
        return;
    }

    const QString monitor = workspaceSwitchMonitor(config);
    if (monitor.isEmpty()) {
        win::logWarn(QStringLiteral("`%1` workspace %2: cannot tell which monitor to use")
                         .arg(hotkey, core::workspaceOpName(action.workspaceOp)));
        return;
    }
    // 相对切换（`workspace("prev")` / `workspace("next")`）：目标 = 这块屏的**当前
    // 活动空间** ± 1；到头了就是什么都不做（**不循环**，与 window 的
    // move_left/right_monitor 一致）。状态机只认绝对空间号，相对那一步在这里算 ——
    // `core::stepIndex()` 是纯函数（tst_placement 有测试），所以"不循环"的语义
    // 仍然只有一处实现。
    std::uint32_t target = action.workspaceIndex;
    if (action.workspaceOp == core::WorkspaceOp::Prev
        || action.workspaceOp == core::WorkspaceOp::Next) {
        const std::optional<core::MonitorSpaces> current = m_workspace.spaces(monitor);
        const std::uint32_t active = current.has_value() ? current->active : 1;
        const int delta = action.workspaceOp == core::WorkspaceOp::Prev ? -1 : 1;
        const std::optional<std::size_t> stepped =
            core::stepIndex(m_workspace.count(), active - 1, delta, /*wrap=*/false);
        if (!stepped.has_value()) {
            // 已经是最前 / 最后一个空间：不循环，也不去动任何窗口。
            win::logInfo(QStringLiteral("`%1` -> workspace %2: already at space %3 on %4")
                             .arg(hotkey, core::workspaceOpName(action.workspaceOp))
                             .arg(active)
                             .arg(monitor));
            return;
        }
        target = static_cast<std::uint32_t>(*stepped) + 1;
    }
    const core::WorkspaceEffects effects = m_workspace.switchTo(monitor, target);
    const std::optional<core::MonitorSpaces> spaces = m_workspace.spaces(monitor);
    const std::uint32_t space = spaces.has_value() ? spaces->active : target;
    applyWorkspaceEffects(effects, QStringLiteral("switching %1 to space %2").arg(monitor).arg(space));
    win::logInfo(QStringLiteral("`%1` -> workspace %2 %3 on %4%5")
                     .arg(hotkey, core::workspaceOpName(action.workspaceOp))
                     .arg(space)
                     .arg(monitor)
                     .arg(effects.isEmpty() ? QStringLiteral(" (already there)") : QString()));
    // 托盘上的徽标现在就该变成新的空间号（不等下一次 500 ms 轮询）：用户刚按完
    // 快捷键，数字化晚半秒会让人怀疑到底切没切。
    pollDesktop();
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
    MonitorMemory *memory = monitorMemory();
    for (const core::Action &action : actions) {
        executeAction(m_runtime, this, memory, config, name, action, ctx);
    }
}

} // namespace flowkeyd::app
