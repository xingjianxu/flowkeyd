# 工作空间（workspace）功能实现计划

> 范围：按显示器独立的工作空间机制（Hyprland 风格），详见需求说明（本文件第 1 节摘要）。
> 本文件是**实施计划**，不是设计决策记录；实现完成后，决策摘要进 `AGENTS.md` 第 2 节（新增第 36 条）、
> 完整理由进 `docs/notes/decisions.md`，然后删除本文件。
> 每个子任务结束都要满足 `AGENTS.md` 第 11 节 DoD（debug + release 双构建、debug 上 `ctest` 全绿、
> `--check flowkeyd.lua.example` 零警告）。

**当前进度：M0 ✅（结论在下文 M0 节 + `docs/notes/pitfalls.md`）、M1 ✅（`src/core/workspace.*` +
`tests/tst_workspace.cpp`）、M2 ✅、M3 ✅、M4 ✅（语义在下文各自的「→ 已产出」）、M5 ✅（事件管线已接线，
`src/core/workspace_event.*` + `platform/win/hook.*` + `Dispatcher::applyWorkspaceEvent`）、M6 ✅（动作执行：
`workspace("switch"|"move", N)` 已经能真的显隐窗口）、M7 ✅（显示器拔插 + 拖到别的屏：`relocateToMonitor()` +
`windowIsOffMonitor()` + `WindowMoved` 归属跟随）、**M8 ✅（安全与恢复：`core/workspace_log.*` 预写日志 +
启动恢复 + 退出/注销前恢复 + 托盘「恢复全部窗口」+ `--restore-windows`）**、**M9 ✅（与现有功能的联动：
唤起藏在别的空间里的窗口 / 切换器列出来 / 跨屏移动跟着改归属 / `window_rule.workspace` 接进三遍）**。
下一步：**M10（`new_window` 与规则归属的剩余语义）/ M11（收尾）**。

## 0. 已确认的决策（来自与项目所有者的问答）

| 项 | 结论 |
| -- | ---- |
| 中文术语 / 配置名 | 中文「**工作空间**」/ 配置名 `workspace`；原「工作区」（`availableGeometry` / `workArea`）保持不变；`AGENTS.md` 工作约定第 14 条要同步改写（见 M11） |
| 默认是否启用 | **默认开启**（`settings.workspace.enabled` 默认 `true`，可关）。没有任何 `workspace` 绑定时不会隐藏任何窗口，只做轻量跟踪 |
| 每显示器工作空间数量 | 固定 N 个，默认 **9**，可配置 **1–10** |
| 隐藏窗口的方式 | **已定（M0 实测）：`ShowWindow(SW_HIDE)`**。Cloak 对别的进程的窗口拿不到权限（`E_ACCESSDENIED`），`SWP_HIDEWINDOW` 隐藏后抢不到前台 → 两者都否决。后端仍封装成单一接口，便于以后切换 |
| 激活藏在别的工作空间里的窗口 | `windows()` 切换器 / `window("activate")` / 启动器：**自动切到它所在的工作空间再激活** |
| 文件位置 | 仓库根 `plan.md` |

## 1. 需求摘要（实现时对照）

* 每个显示器各有一组工作空间（1..N）与一个活动工作空间；切换只影响**鼠标所在显示器**（可选：**焦点窗口
  所在显示器**）。
* 每个被管理窗口同时属于一个显示器 + 一个工作空间；新窗口归入所在显示器的活动工作空间；拖到别的显示器
  则改归那边的活动工作空间。
* 切换：旧空间窗口隐藏、新空间窗口显示；焦点回到新空间最近用过的窗口，空空间则不抢焦点。
* 移动：把当前窗口发到同显示器其它工作空间；目标非活动空间时窗口立即隐藏，用户留在原空间。
* 窗口位置/大小/最大化/最小化在隐藏/显示后不变；每个空间记住 Z 序与最近焦点窗口。
* 只管理普通应用顶层窗口；排除任务栏/桌面/工具窗口/flowkeyd 自己的窗口；用户可配置排除名单。
* 显示器拔出 → 窗口迁到其它显示器的活动空间；重新接入 → 恢复原划分；分辨率/缩放变化不影响归属。
* 可选规则：指定程序默认进某个工作空间；新窗口默认归入哪个空间。
* 安全：隐藏的窗口落盘记录；异常退出后重启自动恢复；菜单项 + 命令行参数「恢复全部窗口」；正常退出 /
  关机前先恢复。
* 与 Windows 虚拟桌面互不干扰：不使用、不修改虚拟桌面，只管理**当前虚拟桌面**上的窗口。

## 2. 总体设计（先定骨架，各任务照着填）

### 2.1 分层（遵守 `AGENTS.md` 第 4 节分层铁律）

| 层 | 新文件 | 职责 |
| -- | ------ | ---- |
| `core`（纯逻辑，无 Win32/GUI） | `core/workspace.*` | 工作空间**状态机**：显示器 → 工作空间 → 窗口记录；每个操作返回「效果列表」（要隐藏 / 要显示 / 要重排 Z 序 / 要聚焦哪些窗口），不直接做事 |
| | `core/workspace_log.*` | 隐藏日志（落盘格式）的序列化 / 反序列化 / 校验（不要叫 `workspace_state`：
和 `core::WorkspaceState` 撞车） |
| | `core/workspace_match.*`（或并入 `window_match`） | 「这个窗口归不归我管」的纯判据（含排除名单） |
| `lua` / `config` | `core/config.*`、`lua/lua_config.cpp`、`lua_prelude.lua` | `settings.workspace`、动作 `workspace`、`window_rule`/`app` 的 `workspace` 字段 |
| `platform/win` | `platform/win/workspace.*` | 隐藏 / 显示 / 重排 Z 序 / 窗口身份校验 / 枚举可管理窗口；封装「隐藏方式」 |
| `app` | `app/dispatcher.*` 增加 `workspace` 相关槽 | **唯一**拥有 workspace 状态的线程 = 动作工作线程（无锁） |

### 2.2 线程与数据流

```
钩子线程：SetWinEventHook（出现/销毁/最小化/移动结束/前台）+ 显示器轮询
   └─ 只入队 WorkspaceEvent（不碰状态、不做慢事）
动作工作线程：Workspace 状态机（唯一所有者）→ 效果列表 → platform::win::workspace 执行
GUI 线程：只负责托盘菜单项「恢复全部窗口」→ 回投给工作线程
```

* 钩子回调仍必须极快返回（不变量 1）；所有 `ShowWindow`/`SetWindowPos`（可能因目标进程卡死而阻塞）只在
  工作线程上做。
* 状态机的键 = **（虚拟桌面 GUID，显示器设备名）**：见 Q1。显示器用**设备名**做稳定身份（与
  `window_rule` 一致），不用序号。
* 窗口记录身份 = `HWND` + 进程 ID + 创建时间 + 类名（防 HWND 复用，日志恢复时也用它校验）。
* 自己引起的 show/hide 事件要**抑制**（否则状态机把自己的隐藏当成用户操作、反复触发）。

### 2.3 与现有功能的接口点（实现时必须逐个核对）

| 现有功能 | 影响 |
| -------- | ---- |
| `hook.cpp` 的 `SetWinEventHook` | 目前范围是 `EVENT_OBJECT_DESTROY..SHOW`，要补 `HIDE`、`EVENT_SYSTEM_MINIMIZESTART/END`、`EVENT_SYSTEM_MOVESIZEEND`（检测拖到别的显示器），且只在启用时订阅 |
| 显示器轮询（`m_monitorDevices`） | 现在只识别「重新接入」；要补「拔出」事件 |
| `window_rule` 三遍 + `PlacementEvent` | 规则把窗口搬到别的显示器后归属要跟着变；规则里新增 `workspace` 字段，执行顺序：几何/显示器 → 工作空间归属 |
| `window` 动作 / `window_monitor`（决策 33）/ 四个 move op（决策 16） | `move_left/right_monitor` 后窗口改归目标显示器的活动空间 |
| `windows()` 切换器、`isSwitchableWindow()`、`window("activate")`、启动器 | 被隐藏窗口要仍能被找到（`SW_HIDE` 下 `IsWindowVisible` 为假）并先切空间再激活；见 M9 |
| 远程桌面放行（决策 26） | 与快捷键放行无关，但 `workspace` 动作也属于「放行时不触发」的快捷键，无需额外代码；写进验收即可 |
| `reload` / `suspend` / `quit` | `reload` 保留状态并按新的 `count` 迁移越界窗口；`suspend` 不改变已隐藏窗口；`quit` 先恢复 |
| `dwm.h` 的 `isCloaked`、`isSwitchableWindow` 的 cloaked 判据 | 不用 cloak 了（M0），但**被我们藏起来的窗口 `IsWindowVisible` 会是 0** → `isSwitchableWindow` 与 `enumerateManageable` 都得改成「靠记录纳入」而不是看可见性；UWP 那类 `cloak=2` 却在本桌面的窗口仍按现有判据处理 |

## 3. 待确认问题（M0 已经用实测数据钓死一部分；其余需在 M2 之前拍板）

> **M0 之后的状态**：Q1 的"要不要按虚拟桌面分别记"仍需拍板，但实现细节已经确定（**窗口归属必须先查后
> 缓存**，因为隐藏后查不到；缓存一次约 45 µs/窗口）；Q5（规则落点）已按"给 `window_rule`/`app` 加字段"
> 写进下文；Q7 已得证（跳过 + warning）。**Q2 / Q3 / Q4 / Q6 / Q8 是产品口味，进 M2 前需要项目所有者拍
> 板**（表里的推荐默认值就是我的建议）。

| # | 问题 | 推荐默认 |
| - | ---- | -------- |
| Q1 | 活动工作空间是否**按虚拟桌面分别记**？需求说「只管理当前虚拟桌面」；若全局共享，在桌面 B 切换会让桌面 A 的窗口显隐与「活动空间」不一致 | 按（虚拟桌面，显示器）分别记；窗口所在桌面用 `desktop` 后端（COM，STA）取，并缓存 |
| Q2 | 动作形状。建议 `workspace{ op = "switch"/"move", index = 3 }`，简写 `workspace("switch", 3)`；是否同时提供 `next`/`prev`（循环）与 `move` 的 `follow`（搬完跟过去，对应 Hyprland 的 `movetoworkspace`） | 本期只做 `switch`/`move`（需求原文）；`next`/`prev`/`follow` 列入后续 |
| Q3 | `switch_target`：`"cursor"`（默认）/ `"focus"` 的配置位置 | `settings.workspace.switch_target`，动作上可局部覆盖 `monitor`-style 字段（`target = "cursor"/"focus"`）？默认先只做全局项 |
| Q4 | 「新窗口默认归入哪个工作空间」：需求 2.3 说归活动空间，3.6 说可配置默认空间 | `settings.workspace.new_window = "active"`（默认）或 `1..N`；程序级用规则 |
| Q5 | 规则落点：是新增独立的 `workspace_rule{}`，还是给 `window_rule`/`app` 加 `workspace` 字段 | 加字段（与决策 13/15 一致；`app{}` 自动展开）；不新增条目类型 |
| Q6 | 切换后是否给用户视觉反馈（托盘徽标 / OSD） | 本期不做；托盘徽标仍是虚拟桌面号（决策 20） |
| Q7 | 不提权运行（`--no-elevate`）时无法操作提权程序的窗口（UIPI） | 跳过这些窗口并写一条 warning；不做提权代理 |
| Q8 | 默认开启 + 默认无绑定：是否在示例配置里提供 `Alt+1..9` / `Alt+Shift+1..9` 的示例（注意与启动器 `Alt+数字` 快速启动键的冲突，决策 30） | 示例里给出，但默认注释掉，并写明冲突 |

## 4. 子任务（按顺序；依赖关系标在标题后）

> 每个任务给出：**目标 / 要改的文件 / 验证**。「三处清单」= README + `config_map.cpp` + QML
> （`AGENTS.md` 工作约定第 13 条）。

### M0　真机验证小实验　✅ 已完成（2026-10-21）

> 下面每条结论都是本机（Windows 10.0.26300 / 26H2 / UBR 9711，两块 3840x2160@200% 的屏）实测的；
> 详细数据、复现手法与附带发现的坑在 **`docs/notes/pitfalls.md` 的「工作空间：隐藏窗口的实测结论（M0）」**
> 与「测试 / 验证手法」两节。探针程序留在 `tmp/wsprobe/`（`wsprobe.cpp` + `a-safe.ps1` / `b-apps.ps1` /
> `c-misc.ps1` / `d-events.ps1` / `e-taskbar.ps1` / `f-alttab.ps1` / `gh-min-move.ps1`，输出是同目录的
> `out-*.txt` 与 `shot-alttab-*.png`）。

**结论表**

| 问题 | 实测结论 |
| ---- | -------- |
| 隐藏方式 | **只能用 `ShowWindow(SW_HIDE)`**。`DwmSetWindowAttribute(DWMWA_CLOAK)` 对**别的进程**的窗口返回 `E_ACCESSDENIED`（`0x80070005`，提权窗口是 `E_HANDLE`）→ cloak 方案对"隐藏别人的窗口"完全不可用（同进程可用，与本功能无关）。`SetWindowPos(SWP_HIDEWINDOW)` 不发 `WM_SHOWWINDOW`、单次更快（0.6–1.1 ms），**但隐藏之后 `SetForegroundWindow` 抢不到前台**（返回 FALSE、前台还停在隐藏窗口上）→ 不能用。 |
| 状态保持 | 记事本 / Chrome / VS Code（Electron）/ WezTerm（OpenGL）/ UWP 计算器 / Directory Opus 全部：位置、大小、最大化、最小化**完全保持**；显示回来后截图像素差 **0.00%**（无重绘丢失、无闪烁），隐藏期间差异 72–98%。 |
| UWP | 要藏的是 `ApplicationFrameHost` 的 `ApplicationFrameWindow`（藏/显同一个 HWND，内容跟着走）。注意 UWP 窗口可能带 `cloak=2` 却仍在当前桌面（真机的「设置」），`cloak != 0` 不能单独当"不在本桌面"的判据。 |
| 任务栏 / Alt+Tab | 两者**都会消失**：UIA 数 Win11 任务栏按钮 17 → 16 → 17（`记事本 - 1 个运行窗口` 那颗消失又回来）；Alt+Tab 抓图里记事本缩略图不在了（只剩 Chrome / 计算器 / 资源管理器）。 |
| 最小化窗口 | 隐藏再显示仍是 `SW_SHOWMINNOACTIVE`（`IsIconic`=1、几何不变），任务栏项按上面那条走。 |
| Z 序 | 不需要额外做也有顺带保障：`SW_HIDE` 不丢 Z 槽位，显示回来原地归位（三个窗口实测前后一致）；显式按记录顺序 `SetWindowPos(h, 前者, …, SWP_NOACTIVATE)` 也能稳定还原（`restack_order_ok=1`）。 |
| 虚拟桌面 ID | **窗口一被隐藏，`IVirtualDesktopManager::GetWindowDesktopId` 就失败**（`0x8002802B`），显示回来才恢复（UWP 的 `ApplicationFrameWindow` 是唯一例外）→ **归属必须先查后缓存**。单次 `GetWindowDesktopId`+`IsWindowOnCurrentVirtualDesktop` ≈ **45 µs**，9 个窗口一轮 0.45 ms（`CoCreateInstance` 一次性 1.4 ms）→ 缓存 + 每次切换批量刷新完全可行。 |
| 事件 | `SW_HIDE` / `SWP_HIDEWINDOW` 各产生一条 `EVENT_OBJECT_HIDE` + 一条 `EVENT_OBJECT_SHOW`（无 LOCATIONCHANGE、无 FOREGROUND），**分不出是谁干的** → 只能靠期望事件表抑制。`Win+Shift+←/→` 搬屏**只有 `EVENT_OBJECT_LOCATIONCHANGE`、没有 `EVENT_SYSTEM_MOVESIZEEND`**（鼠标拖动才有）→ M5/M7 的"窗口换显示器"检测要统一走 LOCATIONCHANGE + 防抖。 |
| 焦点 | **隐藏前台窗口后 `GetForegroundWindow()` 仍返回那个隐藏窗口**（`IsWindowVisible`=0）→ 必须显式两步：先 `SW_HIDE` 旧窗口，再 `AttachThreadInput` + `SetForegroundWindow(目标)`（实测能成功；换成 `SWP_HIDEWINDOW` 就失败）。 |
| UIPI（Q7） | 非提权进程对**提权窗口**的三种方法全是拒绝（`ShowWindow`/`SetWindowPos` = `ERROR_ACCESS_DENIED`，`DwmSetWindowAttribute` = `E_HANDLE`）→ 跳过 + warning，**不做提权代理**（得证）。 |
| 未验证 | ①目标进程**真的卡死**时 `SW_HIDE` 会不会把调用线程挂住（`SW_HIDE` 是跨线程 `SendMessage` 语义，按"会"设计：只在工作线程做 + 先写恢复日志）；②独占全屏（游戏）窗口；③WPS（本机当时没开文档窗口，行为应同普通 Win32 窗口，留在验收脚本里覆盖）；④Windows Terminal 本机没装（用 WezTerm 代表）；⑤微信夸这类「关窗口 = 藏皮盘」的皮盘程序本机没装 → 留在手工冒烟里看一眼（它们的窗口就是普通 Win32 窗口，`SW_HIDE` 不会触发它们的“关闭”逻辑）。 |

**原始实验清单（已逐条跑过，留作下次复跑的对照）**

**目标**：用 `tmp/` 下一次性脚本 / 小 C++ 程序回答下面这些问题，之后的设计以结论为准。

1. **隐藏方式对比**：`SetWindowPos(SWP_HIDEWINDOW)`/`ShowWindow(SW_HIDE)` vs 跨进程 DWM Cloak（未公开路径：
   `SetWindowCompositionAttribute`/`DwmSetWindowAttribute(DWMWA_CLOAK)`/`IApplicationView` 相关）。比较：
   是否保留任务栏项与 Alt+Tab、对已最小化窗口的影响、对最大化/全屏窗口的影响、再显示后位置/大小/最大化
   是否保持、UIPI（提权窗口）、目标进程无响应时是否阻塞。
2. **典型程序样本**：记事本、Explorer 窗口、Chrome/Edge、VS Code（Electron）、Windows Terminal、UWP
   （设置 / 计算器）、WPS、带系统托盘的程序（微信/QQ，关窗口 = 藏托盘）。重点看 UWP 的 `ApplicationFrameHost`
   宿主窗口与真窗口、Chrome 隐藏后再显示是否闪烁/丢渲染。
3. **最小化窗口保持最小化**：隐藏再显示后是否仍是 `SW_SHOWMINNOACTIVE` 状态；任务栏是否仍有项。
4. **Z 序恢复**：用 `SetWindowPos(hwnd, hwndInsertAfter, …, NOACTIVATE)` 按记录栈重排，能否稳定还原。
5. **取虚拟桌面 ID**：被我们隐藏的窗口，`IVirtualDesktopManager::GetWindowDesktopId` 是否还能返回；
   一次查询耗时（用来判断是否每次切换都能对全部窗口查询，还是必须缓存）。
6. **事件行为**：隐藏/显示时 `EVENT_OBJECT_HIDE/SHOW`、`EVENT_SYSTEM_FOREGROUND` 触发情况（决定自触发事件的
   抑制策略）；`EVENT_SYSTEM_MOVESIZEEND` 能否覆盖「拖到另一块显示器」；Win+Shift+方向键搬屏是否触发。
7. **焦点**：隐藏前台窗口后系统自动把前台交给谁；`raiseWindow` 能否稳定接管。

**验证 / 产出**：结论表 + 选定方案 + 对每种样本的兼容清单；若 Cloak 不可行，则固定 `SW_HIDE`，并对 UWP 等
例外写出处理策略。**这一步结束先向项目所有者汇报结论再进入 M4。**

→ **已产出**：结论表见本节顶部；方案 = `SW_HIDE`（`SWP_HIDEWINDOW` 与 cloak 均被实测否决）；UWP 处理策略
= 藏/显 `ApplicationFrameWindow`；实现落点见 M4（后端封装成单一 `hide`/`show`，以后要换方案只改那一处）。

### M1　纯逻辑：工作空间状态机　✅ 已完成（2026-10-21）

**目标**：`core/workspace.*`，对外是「输入事件 / 命令 → 效果列表」的纯函数式接口。

* 数据：`MonitorKey`（设备名）、`DesktopKey`（GUID 字符串，纯逻辑里只当不透明字符串）、
  `WorkspaceId{desktop, monitor, index}`、`WindowRecord{id, workspace, lastFocusSeq, zOrder, minimized}`。
* 命令：`switchTo(monitor, index)`、`moveWindow(window, index)`、`windowAppeared(window, monitor, ruleIndex?)`、
  `windowDestroyed`、`windowMovedToMonitor`、`focusChanged`、`monitorsChanged(现存设备名列表)`、
  `reconfigure(count)`、`restoreAll()`。
* 效果：`Hide[]`、`Show[]`（带还原用的 Z 序栈）、`Focus(window|none)`；**状态机不知道 HWND 之外的任何
  Win32 概念**（窗口句柄用不透明 `uint64`）。
* **两条 M0 硬约束**：（a）窗口在不在由**状态机自己的记录**决定，**不可**用 `IsWindowVisible`（我们藏起来
  的窗口都是不可见的）；（b）`DesktopKey` 由调用方传入并缓存 —— 真机实测窗口一被隐藏
  `GetWindowDesktopId` 就失败，回切时只能靠缓存值判断归属。
* 规则：
  * `switchTo` 目标 = 当前活动空间时**无操作**（不要重复显隐）；
  * 空目标空间 → `Focus(none)`；
  * 切到非空空间：`Show` 新集合 → `Hide` 旧集合（先显后藏，减闪烁）→ `Focus(最近用过的那个)`；
    **`Focus` 不可省**：M0 实测隐藏前台窗口后 `GetForegroundWindow()` 仍指向那个隐藏窗口，系统不会自己移交。
  * `moveWindow` 到非活动空间 → `Hide` 且不改活动空间；到活动空间（同空间）→ 无操作；
  * 显示器拔出：其窗口并入**某块剩余显示器**的活动空间，同时**记住原归属**（`homeMonitor`/`homeWorkspace`），
    重新接入时把它们物归原主；剩余显示器的选取规则要确定（建议：已排序列表里第一块，或光标所在块）；
  * 索引越界（`count` 变小）→ 并入最后一个空间。
* 测试：`tst_workspace`（新增，加进 `CMakeLists.txt` 的测试清单）。覆盖上面每条规则 + 多显示器独立性
  （A 屏切换不影响 B 屏）+ 随机操作序列的不变量检查（每个窗口恰属一个空间；每个显示器恰一个活动空间；
  活动空间的窗口集 = 应显示集）。

→ **已产出**：`src/core/workspace.h` / `src/core/workspace.cpp` + `tests/tst_workspace.cpp`
（19 个用例，含 3000 步随机序列 + 不变量检查）。状态机叫 `core::WorkspaceState`（**不叫** `Workspace`：那个
名字留给 M2 的动作类型），配套类型是 `WorkspaceId` / `WindowRecord` / `MonitorSpaces` / `WorkspaceEffects`。
M2 之后要用到的语义（**都已有单测钉住**）：

* 效果结构是 `show`（**底 → 顶**，就是还原用的 Z 序栈）/ `hide`（**顶 → 底**）/ `focus`（`std::optional`，
  空 = 不抢焦点）。平台层按 `show` → `hide` → `focus` 的顺序做；
* `switchTo` / `moveWindow` / `windowAppeared` 的 `index` **一律夹到 `[1, count]`**（不报错：M2 的校验负责
  报错，这里只保证不丢窗口）；`switchTo` 目标 = 活动空间时**恒为空效果**（幂等，自触发事件不会打成环）；
* **「可见」= 「在它那块屏的活动空间里」**，是唯一判据（决不能用 `IsWindowVisible`）。因此凡是会改变归属的
  操作（`reconfigure` / 显示器拔出 / 物归原主）都必须顺手算出「本来不可见、现在在活动空间」的窗口并
  `show` 出来，否则它们会永久藏在活动空间里（而对活动空间切换是幂等无操作，救不回来）；
* `reconfigure` 要先快照**旧**活动空间再夹新值，否则夹活动空间会把本来藏着的窗口误判成「本来可见」；
* 焦点候选 = 该空间里**没最小化**、`lastFocusSeq` 最大的那个；都没有焦点记录（刚出现的一批）取 Z 序最上面
  的；全都最小化 → `focus` 为空。`moveWindow` 只有搬走的正是「最近拿到焦点的那一个」时才交接焦点
  （`m_lastFocused`，由 `focusChanged` 维护）；
* 显示器拔出后的**并入目标** = `monitorsChanged()` 收到的列表里的**第一块**（调用方决定优先级，传
  `sortedMonitors()` 的顺序即可）；重新接入按 `homeMonitor`/`homeIndex` 物归原主；拔插**永远不抢焦点**；
  空列表（一块屏都没有）= 整条命令无操作；
* 状态按 **（虚拟桌面，显示器）** 分开记，`setDesktop()` 本身不产生效果（虚拟桌面的显隐归 Windows）；
* 命令全部**幂等**（重复的 `windowAppeared` / `switchTo` / `focusChanged` / `restoreAll` 都是空效果），
  不认识的窗口一律忽略；`windowDestroyed` / `focusChanged` / `windowMinimized` **恒为空效果**。

### M2　配置面（依赖 M1 的类型；只做 core + lua + 文档）　✅ 已完成

**目标**：把需求落成稳定的公开 schema（决策 4：当破坏性变更对待）。

* `core/config.h`：`settings.workspace`
  `{ enabled=true, count=9, switch_target="cursor"|"focus", new_window="active"|1..N, exclude={进程名…} }`
  （`exclude` 的空串报错，同决策 26 的理由）；`WindowRule` / `AppDef` 加 `workspace = 1..N`；动作
  `Action::Workspace{ op = switch|move, index }`；`core/action.*` 的 `summary()` 与 `isDestructive()`（不算危险）。
* `lua/lua_config.cpp`：`checkFields` 白名单、范围校验（`count` 1–10、`index` ∈ 1..count，**错误信息指出
  条目与取值**，不变量 6）、`convertAction`；`lua_prelude.lua` 构造器与 `window_rule`/`app` 注释；
  `expandApps` 里决定 `app.workspace` 的继承。
* 文档：`README.md`（新增「工作空间」一章 + 动作表 + `settings` 表 + `window_rule`/`app` 表 + CLI 表）、
  `flowkeyd.lua.example`（Q8）。
* 测试：`tst_config`（校验与展开）、`tst_lua`（转换、错误文本）；`--check` 示例零警告。

→ **已产出**：`Settings::WorkspaceSettings`（`core/config.h`，含 `kMin/kMax/kDefaultWorkspaceCount`，
取自 `core/workspace.h`）、`WorkspaceSwitchTarget` + 名字互转；`WindowRuleDef::workspace` /
`AppDef::workspace` / 编译后的 `WindowRule::workspace`；`Action::Kind::Workspace` +
`WorkspaceOp{Switch, Move}` + `workspaceIndex`（`summary()` = `workspace switch 3`，
**不算危险动作**）。Lua 层：`settings.workspace` 支持**布尔简写**与表两种形状，`new_window` 接
`"active"` 或数字，未知键报错；`workspace(op, index)` 构造器（也在 `flowkeyd.*` 下）。
校验（`compile()`）：`count` 1–10、`new_window` ∈ {0} ∪ [1,count]、`exclude` 空串报错、
`window_rule.workspace` 与 **workspace 动作的 `index`**（含嵌在 `menu` 里的）都必须在 1..count ——
为此 `validateAction()` 多了一个 `workspaceCount` 参数（不传时只查 `>= 1`）。`expandApps`：
`app.workspace` 是 `window.workspace` 的简写（两处都写时 `window` 优先），**只写它时也会
展开出一条只带归属的规则**。“只匹配窗口、什么都不做” 的警告把 `workspace` 也算作“做了点事”。
文档：README 新增「工作空间」一章 + `settings`/动作/`window_rule`/`app` 表；
`flowkeyd.lua.example`（Q8：`Alt+1..9` / `Alt+Shift+1..9` 写出来但**默认注释**，并写明与启动器
`Alt+数字` 冲突）。

### M3　图形化配置界面（工作约定第 13 条的第三处；依赖 M2）　✅ 已完成

* `core/config_map.cpp`：`settingsToMap`/`settingsFromMap`（嵌套 `workspace` 表）、`ruleToMap`/`appToMap`
  的新字段、`actionToMap`/`actionFromMap`、`actionKindNames()`、`defaultActionMap()`；`config_writer` 的
  `configToLua()`（`workspace` 设置有「简写/嵌套」两种书写形状时参照 `remote_desktop`）。
* QML：`ActionEditor.qml` 增加 `visible: root.type === "workspace"` 分支；`ConfigGeneralPage`（或高级页）
  增加工作空间设置卡（含 `exclude` 的字符串列表编辑，复用现有列表行）；`WindowRuleFields.qml` 与 app 编辑
  弹层加 `workspace` 一行；`ConfigText.qml` 的中文名；`CMakeLists.txt` 里新 QML 登两处。
* 测试：`tst_config_map` / `tst_config_writer` 整份往返（真 Lua 读回逐字段对比）；`tst_config_editor`。
* 手工：打开配置窗口走一遍每个新控件（界面漏接不报错，只能人肉核对）。

→ **已产出**：`config_map`：`settingsToMap/FromMap` 的嵌套 `workspace` 表（`new_window` 写 `"active"`
或**数字**，`exclude` 为空时不写）、`windowRuleToMap/FromMap` 与 `appToMap/FromMap` 的 `workspace`、
`actionToMap/FromMap` 的 `workspace`、`actionKindNames()`/`defaultActionMap()`、`workspaceOpNames()` /
`workspaceSwitchTargetNames()`；`ConfigEditor::setWorkspaceSetting()`（只改嵌套那一层）与
`workspaceOps`/`workspaceSwitchTargets` 两个属性。QML：`ActionEditor.qml` 的 `workspace` 分支（切到这个
类型时自动补上 `op`/`index`，否则“什么都不改就保存”会得到 `missing field index`）、
`ConfigAdvancedPage.qml` 的工作空间卡片、`WindowRuleFields.qml` 与 `AppOverlay.qml` 的
「指定工作空间」、`ConfigText.qml` 的中文名。无新 QML 文件，所以 `CMakeLists.txt` 不需要登记。
测试：`tst_config_writer`（整份往返里已经覆盖所有新字段 + 一条专门验嵌套表/new_window 形状的）、
`tst_config_editor`（`setWorkspaceSetting` → 保存 → 用真 Lua 读回逐字段对比）。
**待人眼过一遍**：打开配置界面看五个新控件（`tst_*` 只覆盖模型层）。

### M4　平台后端：隐藏 / 显示 / 重排（依赖 M0 的结论　✅ 结论已就绪）　✅ 已完成

**目标**：`platform/win/workspace.*`，一个文件一件事。**隐藏方式已由 M0 定为 `ShowWindow(SW_HIDE)`**
（Cloak 跨进程无权、`SWP_HIDEWINDOW` 抢不到前台，见 `pitfalls.md`）。

* 接口（隐藏方式藏在它后面，已由 M0 定为 `SW_HIDE`）：
  * `hide(hwnd)` / `show(hwnd, zAfter)`：保持位置/大小/最大化/最小化；对卡死窗口不得无限阻塞
    （异步版本或超时，结果用 bool + 英文错误返回）；**`SW_HIDE` 是跨线程 `SendMessage` 语义（实测
    0.6–2.2 ms/窗口），目标进程卡死时可能把调用线程挂住 → 只允许在工作线程做**（两种方法都实测会触发
    外部 `EVENT_OBJECT_HIDE/SHOW`，所以"自己引起的事件"要靠期望表抑制）。恢复必须用
    `ShowWindow(SW_SHOWNOACTIVATE)` + `SetWindowPlacement`（最小化窗口用 `SW_SHOWMINNOACTIVE`），实测
    位置/大小/最大化/最小化全部保持；
  * `restack(std::vector<HWND>)`：按记录栈还原 Z 序，不激活；
  * `identityOf(hwnd)` / `stillSame(identity)`：进程 ID + 创建时间 + 类名；
  * `enumerateManageable()`：`isMainWindow` 为基础 + 本期额外排除（任务栏、`Progman`/`WorkerW`、
    工具窗口、无标题、flowkeyd 自己的窗口、`exclude` 名单命中），并包含**已被我们隐藏**的窗口（靠记录而非
    `IsWindowVisible`）；
  * 对 UWP 宿主窗口的特殊处理（M0 结论）。
* 所有「被我们隐藏」的窗口在隐藏**之前**先写日志（M8 的预写日志要求）。
* **隐藏前先查并缓存虚拟桌面归属**（隐藏后 `GetWindowDesktopId` 就失败）。
* **`enumerateManageable()` 不能用 `IsWindowVisible` 当"它在不在"的判据**（我们藏起来的窗口都变成不可见）
  → 已记录在状态机里的窗口靠记录纳入；同时注意 UWP 的 `ApplicationFrameWindow`/`CoreWindow` 可能带
  `cloak=2` 却仍在当前桌面。
* 新增未公开 API 要 `LoadLibraryW + GetProcAddress` 运行时解析（第 3 节依赖政策）。
* 测试：纯逻辑部分进 `core`；真机行为进 `tst_interactive`（开一个记事本：隐藏→`IsWindowVisible`/最小化状态
  →显示→几何与最大化不变；**不碰系统电源动作**）。

→ **已产出**：`src/platform/win/workspace.{h,cpp}`（已加进 `flowkeyd_platform`）：
`identityOf` / `stillSame`（PID + **进程创建时间** + 类名；窗口自己的创建时间没有公开 API）、
`hide`（`SW_HIDE`，先存 `WINDOWPLACEMENT`；目标进程 `IsHungAppWindow` 时直接失败）、
`show`（`SW_SHOWNOACTIVATE`，最小化的用 `SW_SHOWMINNOACTIVE`，`showCmd` 真的变了才 `SetWindowPlacement`
摆回去）、`restack`（底 → 顶逐对 `SetWindowPos(…, SWP_NOACTIVATE)`）、`manageTarget`（UWP →
`ApplicationFrameWindow`）、`enumerateManageable(tracked, exclude)`。「窗口在不在」不看
`IsWindowVisible`：判据拆成新的纯函数 `core::workspaceExcluded()` / `core::isManageableWindow()`，
并新增 `window::isMainWindowIgnoringVisibility()`。真机用例：
`tst_interactive::hidesAndShowsAWindowWithoutTouchingIt`（几何 / 最大化 / 最小化 / 身份 / 伪身份）与
`enumeratesManageableWindowsByTheirRecord`（本机已实跑通过）。

### M5　事件管线：从钩子线程到状态机（依赖 M1、M4）　✅ 已完成（→ 已产出见下）

* `platform/win/hook.*`：新增 `WorkspaceEvent`（出现 / 销毁 / 最小化 / 还原 / 移动结束 / 前台变化 / 显示器
  拔出 / 重新接入 / 启动）与 `WorkspaceSink`；仅在配置启用时订阅（避免默认开启后给所有用户加开销时也只有
  轻量回调）；回调只入队。
* 自触发事件抑制：状态机执行效果前登记「期望事件集合」（HWND + 种类 + 过期时间），事件到达时命中即吞
  （实测：`SW_HIDE` 只产生 `EVENT_OBJECT_HIDE` / `EVENT_OBJECT_SHOW` 各一条，很好认）。
* `EVENT_OBJECT_LOCATIONCHANGE` 要做防抖（见 M7），否则连续拖动会淹死状态机。
* 启动时「收编」已有窗口：对 `enumerateManageable()` 的每个窗口按它所在显示器归入当前活动空间。
* 显示器轮询补「拔出」：沿用 `m_monitorDevices` 的基线对比，差集 = 拔出、新增 = 接入。
* 验证：`tst_interactive` 起一个窗口 → 收到出现事件 → 归入活动空间；销毁 → 记录被清理（防 HWND 复用）。

→ **已产出**：

* `src/core/workspace_event.{h,cpp}`：`WorkspaceEvent`（`Startup` / `WindowAppeared` / `WindowDestroyed` /
  `WindowMinimized` / `WindowRestored` / `WindowMoved` / `FocusChanged` / `MonitorsChanged`）+
  `ExpectedWorkspaceEvents`（（种类，窗口）→ 过期时间，命中一次即吞，自带一把锁 —— 动作线程登记、
  钩子线程命中）。事件类型放在 `core` 而不是 `hook.h`：这样它能被 `tst_workspace_event` 直接单测。
* `platform/win/hook.*`：新增 `WorkspaceSink`；**只在 `settings.workspace.enabled` 时**装两个窄范围的
  监听（`EVENT_OBJECT_LOCATIONCHANGE`、`MINIMIZESTART..MINIMIZEEND`，`reload` 时跟着装/卸）；位置变化
  放进 `m_pendingMoves`，由 placement tick（350 ms）防抖后再发；显示器轮询补「拔出」（与「接入」合成一条
  `MonitorsChanged`，带**现存**设备名列表）；前台变化除了远程桌面判定还发 `FocusChanged`。
* `platform/win/workspace.*`：新增 `idOf()` / `handleOf()`（`HWND` ↔ `core::WindowId`）与单窗口版本的
  `isManageable(hwnd, tracked, exclude)`（枚举与事件两条路径共用同一份判据）。
* `platform/win/desktop.*`：新增 `currentDesktopId()`（未公开的 `GetCurrentDesktop` + `IVirtualDesktop::
  GetID`），状态机按（虚拟桌面，显示器）分组的那个不透明键由它提供。
* `app/dispatcher.*`：`submitWorkspace()` + `applyWorkspaceEvent()`（启动收编、出现归入活动空间、销毁清
  记录、最小化/还原、前台、显示器集合、`count` 变化走 `reconfigure`）+ `applyWorkspaceEffects()`
  （先登记期望事件 → `show` 底→顶 → `hide` 顶→底 → `focus`）。`app/runtime.cpp` 组装 sink、拥有并共享
  期望事件表、启动时投 `Startup`（排在 `window_rule` 的 Startup 之后，于是归属按规则的落点算）。
* 测试：`tests/tst_workspace_event.cpp`（事件名、吞一次、过期、键、两线程并发）；`tst_workspace` 补
  `trackedWindows()`；`tst_interactive::hookThreadReportsWorkspaceEventsForForeignWindows`（启动一个
  `charmap.exe` → 收到出现事件 → 登记期望后最小化**收不到** → 不登记时收得到 → 关闭收得到销毁事件）。
  真机手工确认：开发实例启动时 `workspace: tracking 9 window(s) on 2 monitor(s), 9 space(s) each`。
* **M6 接手的地方**：`WindowMoved` 事件已经在防抖后到达工作线程，`Dispatcher::applyWorkspaceEvent()` 里那
  一条 `case` 目前是空的（M7 要在那里比较显示器归属）。`workspace(...)` 动作已在 M6 接上。

### M6　核心动作：切换与移动（依赖 M2、M5）　✅ 已完成

* `app/dispatcher.*`：执行 `workspace` 动作。
  * 目标显示器 = 光标所在（`monitor::indexForCursor`）或前台窗口所在（`switch_target = "focus"`）；
  * `switch`：状态机出效果 → 先 `Show` 新空间窗口（按 Z 序栈）再 `Hide` 旧空间窗口（减少闪烁，M0 确认顺序）
    → 聚焦最近用过的窗口（`raiseWindow`）或不抢焦点；
  * `move`：对**当前前台窗口**（复用决策 19 的「跳过覆盖层」找法，只管理范围内窗口）；
  * 只处理**当前虚拟桌面**上的窗口：用 `desktop` 后端过滤（决策 13：每次调用丢进一次性 STA 线程，所以批量
    查询并缓存，不要逐窗口多次进 COM）。
* 记录「最近焦点」：前台变化事件更新 `lastFocusSeq`。
* 验证：`tst_interactive`（两窗口、两空间）；`scripts/acceptance.ps1` 新增检查（开若干记事本 → 切换 →
  断言可见集合 → 移动 → 再切回 → 位置/最大化/最小化保持）。

→ **已产出**：

* `app/dispatcher.*`：`executeWorkspaceAction()`（动作线程上跑）+ `workspaceSwitchMonitor()` +
  `windowsOnThisDesktop()`。`switch` 的目标屏按 `settings.workspace.switch_target` 取（`"focus"` 用
  「前台窗口、覆盖层跳过」那个找法，问不出来就退回光标）；`move` 的目标 = 当前前台窗口（同一找法），
  先过 `isManageable()`（排除名单 / 非主窗口 / flowkeyd 自己的窗口一律拒绝），没收编的先按它现在所在的
  显示器记账再搬。动作里已没有 `is not implemented yet`。
* **当前虚拟桌面**：`executeWorkspaceAction()` 每次先 `refreshWorkspaceDesktop()`（切虚拟桌面是 DWM
  cloak，钩子收不到事件）；显隐前把 `hide` 列表过一次新的批量查询
  `desktop::windowsOnCurrentDesktop()`（一条 STA 线程 + 一次 `CoCreateInstance`，向量与入参一一对应，
  `std::nullopt` = 问不出来 = 当成“在当前桌面”），把记录过时的窗口（`Win+Ctrl+Shift+←/→` 送走的）
  跳过去。`adoptExistingWindows()` 用同一个查询，不再把别的桌面上的窗口收编进来。
* **Z 序修好了一处方向错误**：`applyWorkspaceEffects()` 原来把 `effects.show`（底 → 顶）的每个窗口用
  `show(hwnd, 前一个)` 摆位置，而 `SetWindowPos(hwnd, 前一个)` 是把 `hwnd` 摆在它**下面** —— 顺序会整个
  反过来。现在改成 `show(hwnd, nullptr)` 逐个露出来 + 一条 `restack(shownWindows)`（底 → 顶逐对，
  M4 那个函数的方向是对的）。
* 测试：`tst_interactive::switchesWorkspacesWithRealWindows`（两个真窗口 → 空空间 `hide` 两个且不抢焦点
  → 切回 `show` 两个（底 `b`、顶 `a`，焦点 = 最近用过的 `a`）→ 几何、`IsWindowVisible` 与 Z 序全部不变）；
  顺带在那里验了 `windowsOnCurrentDesktop()`（包括 `nullptr` 句柄得到 `std::nullopt`）。纯逻辑那一半拆成了
  `core::filterToCurrentDesktop()`（“问不出来就保留”、长度不一致不过滤），由
  `tst_workspace::filterToCurrentDesktopKeepsUnknownWindows` 钉住。本机实跑通过。
* **`scripts/acceptance.ps1` 的检查推后到 M9/M11**：整个特性还没接完（M7/M8/M9），现在加进去的项在 M9
  之前会一直失败（也说明了 `--check` 以外的验收要等动作链闭环）。

### M7　多显示器变化（依赖 M5、M6）　✅ 已完成

* 拔出：窗口迁入剩余显示器的活动空间（物理上也需要摆到那块屏上；**实际做法**：没用
  `monitor::moveToMonitor`，它按 `SetWindowPlacement` 的 `showCmd` 收放窗口，会把被我们藏起来的窗口
  弹出来，改用新的 `workspace::relocateToMonitor`，见下文「→ 已产出」）；重新接入：按「原归属」恢复
  空间划分，并接 `window_rule` 的重新归位（决策 13）顺序。
* 分辨率/缩放变化：归属键用设备名，**不受影响**；只在 `QScreen` 变化时确认没有误触发「拔出/接入」。
* 拖窗口到另一块显示器：**统一用顶层窗口的 `EVENT_OBJECT_LOCATIONCHANGE` + 防抖检测**（真机实测：
  `Win+Shift+←/→` 搬屏**只有 LOCATIONCHANGE、没有 `EVENT_SYSTEM_MOVESIZEEND`**；鼠标拖动两种都有）→
  判断所在显示器是否变化 → `windowMovedToMonitor`（归入目标显示器的活动空间；若该空间当前是被隐藏的
  状态则不可能发生，因为只有可见窗口能被拖）。
* 测试：`tst_workspace` 补「拔/插」序列；真机多显示器不可自动化部分写进 `reference.md` 手工冒烟清单
  （两块 1920x1080@200% 的本机可以做）。

→ **已产出**：

* **拖到另一块屏**：`Dispatcher::applyWorkspaceEvent()` 的 `WindowMoved` 分支不再是空的 —— 拿记录里的
  显示器跟窗口现在所在的比，变了就 `windowMovedToMonitor()`（归入目标屏的**活动空间**）。
  关键的一条：**记录里那块屏已经不在时直接跳过**（拔屏之后 Windows 会替我们把窗口搬走，那条事件
  （`LOCATIONCHANGE`）比 `MonitorsChanged` 先入队（同一个 tick 里先发 pending moves、最后才盘点显示器），
  照它记账就会把 `homeMonitor` 丢掉，重新接入时窗口再也回不到原屏）。
* **物理位置向记录看齐**：新纯函数 `core::windowIsOffMonitor(rect, monitor)`（`core/placement.*`，判据 =
  「还原矩形与那块屏的 `bounds` 没有非空重叠」）+ `platform/win/workspace::relocateToMonitor(hwnd, monitor)`
  （**保持显隐 / 最大化 / 最小化**：藏着的窗口用 `SW_HIDE` 收尾，最大化的先 `SW_RESTORE` 再重新最大化，
  并把 `savedPlacements()` 里的 `rcNormalPosition` 一起挪过去）+ `Dispatcher::relocateDisplacedWindows()`
  （只在 `MonitorsChanged` 里跑，逐个被跟踪的窗口比对）。拔屏与重新接入两头是**同一条规则**：记录说它
  在 A 屏、它其实不在 → 搬回 A 屏。
* **顺序**：`MonitorsChanged` → 状态机改归属 → `relocateDisplacedWindows()` → 执行显隐效果（被并进活动
  空间的窗口要在**新位置**上露出来）；`window_rule` 的重新归位（决策 13）在同一个 tick 里**排在后面**
  （钩子先发 `MonitorsChanged`、再发 `PlacementEvent::MonitorConnected`），规则真把窗口另放了时，随之
  而来的 `LOCATIONCHANGE` 会把归属再跟过去 —— 规则优先。
* **分辨率 / 缩放变化**：归属键就是设备名，钩子盘点的也是「规范化后的设备名集合」，所以这类变化
  不会误报成拔出 / 接入（已在 `hook.cpp` 写死注释）。`tst_workspace::monitorChurnWithTheSameSetChangesNothing`
  钉住另一半：**同一份集合重发一次（含换序 / 重复 / 空串）必须零效果**，否则改一次分辨率就会把藏着的
  窗口弹出来、还会把前台拽走。
* 测试：`tst_interactive::relocatesDisplacedWindowsWithoutChangingState`（真机两块屏：藏着的窗口搬过去
  仍然藏着、显回来落在那块屏上；最大化窗口搬回去仍然最大化；最小化窗口搬过去仍然最小化）、
  `tst_placement::windowIsOffMonitorComparesTheRestoreRectWithTheBounds`、
  `tst_workspace::monitorChurnWithTheSameSetChangesNothing` —— 本机实跑通过。
  顺带修掉了一条真的会让用例闪失败的老问题：`switchesWorkspacesWithRealWindows` 里那个批量虚拟桌面
  查询在「窗口刚创建、shell 还没把它登记到任何虚拟桌面」（`GetWindowDesktopId` 给全零 GUID）时会得到
  `std::nullopt`，现在允许重试一秒。
* **待人眼过一遍**（两块屏在本机就能做）：拔掉 / 禁用一块屏 → 那上面的窗口出现在剩下那块屏上；接回 →
  物归原主；`Win+Shift+←/→` 搬屏后归属跟着变。

### M8　安全与恢复（依赖 M4；与 M6 并行，**必须在发布前完成**）　✅ 已完成

* **预写日志**：`<配置目录>\workspace.json`（与 `launcher.json`/`windows.json` 同目录同风格），记录
  「当前被我们隐藏的窗口」的身份（M4 的 `identityOf`）与所属空间；**先写日志再隐藏**，显示后移除。
* **启动恢复**：守护进程启动最早期（读取配置之前也行）读日志 → 校验 `stillSame` → 全部 `show`；日志格式
  错误 / 窗口已不存在要容忍且不崩。
* **正常退出**：`quit`、托盘退出、`--quit` 事件、`aboutToQuit`；**关机/注销**：处理会话结束消息
  （`WM_QUERYENDSESSION`/`WM_ENDSESSION`，Qt 的 `commitDataRequest`）；`reload` 失败回滚时不丢状态。
* **手动兜底**：托盘菜单项「恢复全部窗口」（GUI 线程只回投，工作线程执行）；CLI `--restore-windows`
  （离线命令：不提权、不装钩子；若已有守护进程在跑则通过 `--quit` 同款事件机制通知它执行，没有则直接按
  日志恢复）；`cli.*` 中文帮助、`AGENTS.md` CLI 列表同步。
* **崩溃防护**：工作线程所有 Win32 调用的失败路径都要能走到「至少把窗口放回可见」。
* 验证：`tst_workspace_state`（日志序列化 / 损坏容忍）；`tst_interactive`（隐藏后模拟「异常退出」= 直接
  丢弃状态机对象，再走启动恢复路径，窗口全部可见）；`acceptance.ps1` 增加「`taskkill /F` 后重启 → 窗口
  恢复」检查（先按工作约定第 6 条提醒用户，且只拿测试窗口，不动用户真实窗口）。

→ **已产出**（命名偏离一处：日志核心层叫 `core/workspace_log.*`，**不叫** `workspace_state` ——
那个名字会和 `core/workspace.h` 里的 `WorkspaceState` 撞车；测试相应地叫 `tst_workspace_log`）：

* `src/core/workspace_log.{h,cpp}`：`WindowIdentity` / `HiddenWindow` / `WorkspaceLog` +
  `workspaceLogPath()` / `parseWorkspaceLog()` / `serializeWorkspaceLog()` / `addHiddenWindow()` /
  `removeHiddenWindow()` / `hasHiddenWindow()`。JSON 里 `hwnd` 与 `process_started` 写**字符串**
  （`QJsonValue` 的数字是 `double`，超过 2^53 会静默丢精度）；身份缺一半时 `valid = false`（恢复时不校验；
  宁可多试一次，也别漏掉该放的窗口）。坏 JSON / 坏条目一律只是丢掉那一条。
* `platform/win/workspace.*`：`Identity` 改成 `core::WindowIdentity` 的别名（恢复日志要序列化它，而
  `core` 不能依赖平台层）；新增 `loadWorkspaceLog()` / `saveWorkspaceLog()`（`QSaveFile` 原子替换，
  空日志 = 删文件）/ `restoreHiddenWindows()`（逐条 `isAlive` + `stillSame` 校验，卡死的窗口不碰，
  失败的留着）/ `restoreFromLog()`（读 → 恢复 → 写回剩下 + 报告；空/坏文件顺手删掉）。
* `Dispatcher`（动作线程，唯一的日志写者）：`ensureWorkspaceLogLoaded()`（启动恢复剩下的那些接着用，
  **不覆盖**成空）、`applyWorkspaceEffects()` 里**预写日志**（写不进去就整个 hide 都不做）、
  显示成功后删条目、hide 失败/窗口消失时撤掉预写条目（否则下次启动会去 show 一个本来就可见的窗口）、
  `windowDestroyed` 时也清日志、`submitWorkspaceRestore()` / `restoreWorkspaceNow(ms)`（异步 / 有上限的
  阻塞版）、`restoreWorkspaceWindows()`（状态机 `restoreAll()` + 日志里剩下的那些）。
* `platform/win/single_instance.*`：新增 `-restore` 请求事件（与 `-quit` 同一套 Low 完整性描述符，
  抽出 `createAskEvent()` / `signalAskEvent()` 两个共用实现）：`requestRestore()` 让 `--restore-windows`
  能请正在跑的实例干活。`Runtime` 拥有它并在 `QWinEventNotifier` 上回投。
* `platform/win/session.{h,cpp}`：一个隐藏的 `WS_POPUP` 顶层窗口，收 `WM_QUERYENDSESSION`（先恢复再返回
  `TRUE`）与 `WM_ENDSESSION`（第二次机会）。为什么不用 `QAbstractNativeEventFilter`：那要先假设“此刻恰好
  有一个 Qt 窗口活着”，而关机这条路径必须确定。
* `Runtime`：`shutdown()` 在**卸完钩子、退线程之前**调 `restoreWorkspaceNow(3000)`；`requestWorkspaceRestore()`
  给托盘 / `-restore` 事件用。`main.cpp`：启动最早期（读配置之前）`recoverWorkspaceWindows()`（已有实例在跑
  就跳过，判据 = `-quit` 事件对象还在）、`--restore-windows` 分支、`SessionEndWatcher`（2 s 上限）、
  托盘信号连线。`platform/win/tray.*`：新增「恢复全部窗口(&W)」。`cli.*`：`--restore-windows` 与中文帮助。
* 测试：`tst_workspace_log`（9 条：路径、空/缺文件、坏 JSON / 坏条目、64 位整数往返、去重、
  `addHiddenWindow` 替换语义）；`tst_interactive::restoresHiddenWindowsFromTheRecoveryLog`（真机：藏两个窗口
  → 写日志（其中一条身份对不上）→ 丢掉内存状态 → `restoreFromLog()`：对的那个回来、错的那个仍是藏着的、
  日志被删掉；再跑一次是空操作；坏文件被清掉）—— 本机已实跑通过（`ctest` 里默认 skip）。
* **`scripts/acceptance.ps1` 的 `taskkill /F` 检查推到 M11**：它要起一个真实实例 + 真的杀进程，靠工作
  约定第 6 条的提醒守着，留给验收脚本整体串起来时加。

### M9　与现有功能的联动（依赖 M6、M8）　✅ 已完成

* `window("activate")` / `windows()` 切换器 / 启动器启动后激活：目标窗口在**非活动工作空间**时，先 `switch`
  到它的空间再激活（**必须等切换完成再激活**，避免决策 32 说过的“又把视图切走”那一类问题）；切换器列表
  要把被我们隐藏的窗口列进来（`isSwitchableWindow` 要识别「被我们隐藏」≠「shell 假窗口」；M0 已确认
  我们不用 cloak，所以真正的判据是**状态机记录里有它**，而不是 `IsWindowVisible`，否则内藏窗口会漏进来
  或漏出去）。
* `window` 动作里 `move_left/right_monitor`：搬完后改归目标显示器的活动空间。
* `launch` / `run` 新启动的程序：出现事件到达时按规则/活动空间归属；`launchThenActivate` 的先启动再激活流程里，
  新窗口可能出现在别的空间——规则优先。
* `window_rule`：`workspace` 字段的执行时机并入现有三遍（窗口首次出现 / 显示器重接入 / 启动）。
* `settings.workspace.exclude` 与 `window_rule` 的 `ignore`（若有）关系：`exclude` 的窗口完全不进状态机。
* 验证：`acceptance.ps1` 增加「藏在空间 2 的记事本被 `window("activate")` 唤起 → 自动切到空间 2」。

→ **已产出**：

* **纯逻辑**（`core/window_match.*`、`core/workspace.*`）：
  * `TopLevelWindowFacts::hiddenByUs` + `isSwitchableWindow()`：“被工作空间藏起来”的窗口仍然可切换
    （判据只能看记录，因为 `SW_HIDE` 之后 `IsWindowVisible` 是假的）；
    `planWindowAction(op, toggle, alreadyActive, hiddenByUs)` 多一个参数：被藏着的窗口**永远不收起**
    （M0：隐藏前台窗口之后 `GetForegroundWindow()` 仍指着它，不加这一条“唤起”会变成“最小化”）。
  * `WorkspaceState::revealWindow(window)`：把那块屏切到窗口所在的空间（效果列表里就带着它），
    已经是活动空间就无操作；记录里的虚拟桌面**过时**时（刚被从别的虚拟桌面搬过来）就地归入当前
    桌面的活动空间、必要时显式显示。
  * `WorkspaceState::windowReboundToMonitor(window, monitor)`：**我们自己**把窗口搬到别的屏时只改显示器、
    **保留空间号**（`windowMovedToMonitor()` 仍给“用户拖动”用：并入目标屏的活动空间）。两条分工的
    理由是“藏起来的窗口不可能被用户拖动，它换屏一定是我们干的”，否则 `window_rule` 刚设好的归属会被
    晚到的 `LOCATIONCHANGE` 抹掉。
* **平台层**（`platform/win/window.*`）：`find(query, tracked)` 与 `listOpenWindows(tracked)` 接受一份
  “状态机记着的窗口”，让被藏起来的窗口仍然能被找到 / 列出来（优先级：可见 > 被我们藏着 > 最小化）；
  `isSwitchableWindow(hwnd, hiddenByUs)` 把 `hiddenByUs` 折成可见之后交给纯函数。
* **接线**（`app/dispatcher.*`）：`trackedWorkspaceWindows()` / `workspaceSaysHidden()` /
  `revealWindowWorkspace()` / `followWindowMonitor()` / `applyWorkspaceRules()`。
  * `window("activate")`：`bringWindowToCurrentDesktop()` → **`revealWindowWorkspace()`**（切空间，先显）
    → `bringWindowToActionMonitor()`（摆屏）→ `applyTo(Activate)` → `followWindowMonitor()`（归属跟屏）。
    顺序不能反：对一个 `SW_HIDE` 着的窗口 `raiseWindow()` 没用，而 `SetWindowPlacement` 会把它弹出来。
  * 窗口切换器：列表用 `listOpenWindows(tracked)`，选中后同样先 `revealWindowWorkspace()` 再激活。
  * `move_left/right_monitor` 与 `activate` 的摆屏：做完调 `followWindowMonitor()`（看得见的并入目标屏活动
    空间，藏着的保留空间号）。
  * 对**藏着的**窗口做最小化 / 最大化 / 还原 / 挪屏直接拒绝 + warning（这些 op 靠 `ShowWindow` /
    `SetWindowPlacement` 改显隐，会与记录打架）；`close` / `toggle_topmost` 不受影响。
  * `window_rule.workspace` / `app.workspace`：`applyWorkspaceRules()` 在 `applyPlacementRules()` 的同一遍里
    跑，于是与 `window_rule` 的三个时机**完全一致**（首次出现 / 显示器重接入 / 启动），用户后来自己
    `workspace("move", N)` 搬走的窗口不会被规则拉回去。未跟踪的窗口走 `windowAppeared(..., index)`
    （落到非活动空间 → 立即隐藏、不抢焦点），已跟踪的走 `moveWindow()`；两种都把 `focus` 清掉。
  * `WindowMoved`：看得见的窗口按“拖动”处理，**藏着的按 `windowReboundToMonitor()`**（保留空间号）。
* 测试：`tst_window_match`（`hiddenByUs` 的可切换判据、`hiddenByUs` 时 `toggle` 不收起）、
  `tst_workspace::reboundToAnotherMonitorKeepsItsSpace` / `revealingAHiddenWindowSwitchesToItsSpace`、
  `tst_interactive::revealsAWindowHiddenInAnotherWorkspace`（真机：藏起来 → `find`/判据认得它 →
  `revealWindow()` 的效果真的把它显示回来）。本机实跑通过。
* **推给 M11 的两件事**：`acceptance.ps1` 的「唤起藏在空间 2 的窗口」检查（与整份验收脚本一起加），
  以及跨进程那一半（切换器列表里真的出现被我们藏起来的**别进程**窗口 —— 单测进程的窗口会被
  `listOpenWindows()` 的“不列自己”规则挡掉）。
* **留给 M10**：`settings.workspace.new_window`（规则没写 `workspace` 时的默认归属）。现在没写规则的
  新窗口仍然一律落在它那块屏的活动空间（= `new_window = "active"` 的语义，其余取值还没接）。

### M10　规则与默认归属（依赖 M2、M6、M9）

* `new_window = "active"|N`、`window_rule.workspace`、`app.workspace` 的运行时语义；规则命中顺序沿用
  `window_rule`（先匹配到的规则赢）；规则要求的空间在目标显示器上若不是活动空间 → 新窗口出现后立即隐藏、
  **不抢焦点**。
* 用户手动 `move` 后，该窗口不应被规则「拉回去」（规则只在窗口首次出现 / 显示器重接入 / 启动 三个时机
  触发，与决策 13 一致）。
* 验证：`tst_config` / `tst_workspace`（规则优先级）+ `acceptance.ps1`（带规则的记事本首次出现即落在指定
  空间且被隐藏）。

### M11　文档、验收与收尾（依赖全部）

* `README.md` 终稿；`flowkeyd.lua.example`（`--check` 零警告）。
* `AGENTS.md`：新增**决策第 36 条**摘要；**改写工作约定第 14 条**（「工作空间」现在是新功能的名词，虚拟
  桌面仍写「虚拟桌面」，`workArea` 仍写「工作区」；清理所有与「不要再用 workspace 这个词」冲突的措辞）；
  第 4 节代码地图 / CMake 目标划分 / 第 6 节线程模型 / 第 14 节 CLI 开关 / 动作列表（动作从 20 种变成
  21 种，**同步修改所有写着「20 种动作」的地方**，包括 `AGENTS.md` 与 `README`）。
* `docs/notes/decisions.md` 第 36 条完整理由（**必须包含 M0 的隐藏方式结论**：为什么只剩 `SW_HIDE`、
  为什么 `SWP_HIDEWINDOW` 不行、为什么放弃 cloak）；`pitfalls.md`（M0 结论已在里面，还要补实现中的坑）；
  `extension.md`（「新的工作空间能力」一节）；`reference.md`（schema 速查 + 验收覆盖 + 手工冒烟清单）。
* `scripts/acceptance.ps1` 项数、`AGENTS.md` 第 1/5 节「166 项 / 实跑 163」同步更新。
* 全量 DoD：debug + release 构建 → `ctest` 全绿 → 先提醒用户再跑 `acceptance.ps1` → 手工冒烟（见下）→
  `build\dist-release` 最新 → 提交信息写明兼容性（新增字段与动作，**无破坏性变更**）。
* 完成后删除本 `plan.md`。

## 5. 依赖关系一览

```
M0 ───────────────┐
M1 ─┬─ M2 ─ M3    │
    └───────────┐ ├─ M4 ─┬─ M5 ─ M6 ─┬─ M7
                │ │      │           ├─ M9 ─ M10 ─ M11
                └─┴──────┴─ M8 ──────┘
```

* 可并行：M1 ∥ M2/M3；M4 只等 M0（已完成）；M8 ∥ M6。
* **发布闸门**：M8（安全与恢复）未完成不得发布，因为默认开启 + 隐藏窗口意味着出错后用户的窗口会
  「消失」。
* 建议提交粒度：每个 M 一个提交（M2/M3 可合并为一个以满足「三处同时到位」）；提交信息说明为什么。

## 6. 风险与应对

| 风险 | 应对 |
| ---- | ---- |
| `SW_HIDE` 隐藏后任务栏项消失、个别程序（UWP / Chrome / 托盘程序）行为异常 | **M0 已验：会消失（这正是要的），且显隐后状态与像素完全一致**；UWP 藏 `ApplicationFrameWindow`；托盘程序与 WPS 留手工冒烟 |
| Cloak 是未公开路径，Windows 版本更新可能失效 | **M0 已排除**：跨进程 cloak 直接 `E_ACCESSDENIED`，没有后期维护风险（但也就没有这个能力） |
| **隐藏后窗口的虚拟桌面归属查不到**（M0 新发现） | 先查后缓存（45 µs/窗口）；缓存只存不刷，刷新只对本桌面可见窗口做 |
| **隐藏前台窗口不会自动移交前台**（M0 新发现） | 显式 `SW_HIDE` → `AttachThreadInput` + `SetForegroundWindow`（只用 `SW_HIDE`，不用 `SWP_HIDEWINDOW`） |
| HWND 复用导致误恢复 / 误隐藏别人的窗口 | 身份 = HWND + PID + 创建时间 + 类名；`EVENT_OBJECT_DESTROY` 清理 |
| 目标进程卡死使 `SW_HIDE`（跨线程 `SendMessage`）阻塞工作线程 | 只在动作工作线程上做（绝不上钩子线程）；超时 / 异步变体 + 先写恢复日志；恢复手段是不依赖目标响应的日志重放 |
| 自触发事件造成死循环 | 期望事件抑制表 + 状态机幂等（目标态 = 当前态时无操作） |
| 虚拟桌面 COM 查询慢 / 必须 STA | 批量 + 缓存；绝不在 GUI/钩子线程做（决策 20 / 不变量 13） |
| 默认开启后升级用户被突然「接管」窗口 | 无绑定时不隐藏任何窗口；README 写清；`enabled = false` 一键关闭 |
| 提权程序窗口（UIPI）无法管理 | Q7：跳过 + warning；常驻实例通常是提权的，问题只出现在 `--no-elevate` 开发实例 |
| 与启动器 `Alt+数字` 快捷键冲突 | Q8：示例默认注释并说明 |

## 7. 手工冒烟清单（M11 用；完成后并入 `reference.md`）

1. 双屏：A 屏切到空间 2，B 屏窗口不动；鼠标移到 B 屏再切，只动 B 屏。
2. `switch_target = "focus"`：鼠标在 A、焦点在 B 时切换作用于 B。
3. 最大化 / 最小化 / 普通窗口各一个，来回切换后状态与位置不变；最小化的窗口切回仍是最小化。
4. 空空间不抢焦点；非空空间焦点回到最近用过的窗口；Z 序还原。
5. `move` 到非活动空间：窗口立即消失、用户留在原空间。
6. 拖窗口到另一块屏 → 归属改变；拔掉/接上一块屏（或禁用一块显示器）→ 迁移与复位。
7. 切换系统虚拟桌面：另一桌面的窗口不受影响；两边工作空间互不串。
8. 规则：浏览器固定空间 2；排除名单里的程序永不被隐藏。
9. 异常退出（`taskkill /F`）后重启 → 全部窗口恢复可见；「恢复全部窗口」菜单项与 `--restore-windows`；
   注销/关机前窗口已恢复。
10. 通过 `windows()` 切换器 / `window("activate")` 唤起藏在别的空间里的窗口 → 自动切到对应空间。
11. 远程桌面前台时快捷键放行（决策 26），不触发工作空间切换。
