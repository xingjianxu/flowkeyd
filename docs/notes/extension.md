# 在哪里扩展（AGENTS.md 第 13 节）

> 每一类改动该动哪几个文件、哪几处必须一起改、拿什么验证。**动手前先看 `AGENTS.md` 第 7 节不变量与
> 工作约定第 13 条的「三处清单」。**

## 新动作类型

`core/action.h` 加一个变体 → `Action::summary` 加摘要 → `app/dispatcher` 里处理它 → 带参数的话加进
`core/config` 的 `validate_action`（顶层动作与 `menu` 条目共用）→ `lua_prelude.lua` 里加构造器并挂进
`flowkeyd` 表（构造器如果是"原样返回用户表"的那种，**记得自己补 `type`**）→ 写进 `README.md` 的动作表与
`flowkeyd.lua.example` → **在 `scripts/acceptance.ps1` 里加一条能自动验证的检查**。

界面那一侧同时接三处：`core::actionToMap()` / `core::actionFromMap()` 与 `core::actionKindNames()` /
`core::defaultActionMap()`，再在 `ActionEditor.qml` 里加一段 `visible: root.type === "…"` 的表单
（`menu` 的 `items` 与 `window` 的 `launch` 是现成的例子），并在 `ConfigText.qml` 的 `action` 表里给它
一个中文名。忘了接界面不会报错，只是"这一种动作在界面里改不了"（字段被默默丢掉）。

## 新选单条目字段（例如图标）

`config` 加字段 → `MenuModel` 与 `MenuPopup.qml` 里画出来 → 校验（重名、空标签）→ README 表格。条目是在
委托的 `contentItem` 里用锚点摆的，**不要再把行几何往模型里塞**。

## 帮助窗口的新内容或新交互

条目在 `app/dispatcher` 的 `openHelpAction()` 里从 `Compiled` 的 `bindings`/`remaps` 生成（与 `--list`
看同一批数据，所以 `help` 没有配置参数），纯逻辑在 `HelpModel`，界面在 `HelpPopup.qml`。模型层与 `core`
之间只靠一个 `destructive` 布尔量连着；要让一行能执行动作就在 `openHelpAction()` 的 `targets` 里给它一个
条目（绑定用 `press`+`release`，`remap` 用两串 `SendOp`），**不要往 `HelpEntry` 里塞 `core::Action`**。

## 窗口切换器的新行为

纯逻辑在 `WindowListModel`（筛选/选中/按键决定，`tst_window_list_model` 直接测），行里画什么在
`SwitchPopup.qml`，**窗口本身的开/关在 `PopupHost::showSwitch()`**（它只能看到窗口、看不到按键，所以像
「再按一次同一个快捷键 = 关掉」这类判断要放在那里，而且要想清楚「触发发生在按键的哪一半」——「轻碰
Win」是**松开**时触发的）。每加一条可从外面观察到的行为，就在 `scripts/acceptance.ps1` 的「窗口切换器」
那一段加一条检查：那是这条路径唯一的自动化覆盖。

## 程序启动器的新行为

分层看：

* `core/app_list.*`：「算不算程序」/「算不算卸载程序」、去重排序、名字匹配、图标键、取图标的名字、跨来源
  合并、排序键与分组表头、内置 `ms-settings:` 页面表。
* `platform/win/apps.*`：两份扫描（**别把名字匹配往平台层堆**）。
* `core/launcher_state.*`：固定/最近使用的顺序、截断、JSON 解析序列化。
* `app/app_list_model.*`：行式的三个视图与选中项/卡片高度、三种快速启动标识。
* `app/app_icons.*`：异步图标的线程/缓存/尺寸。卡片 `AppPopup.qml`。
* 喂条目、**启动那一步**（`apps::launchApp()`）与状态文件路径在 `Dispatcher::openAppsAction()`；读写
  `launcher.json` 的是 `PopupHost`（`loadAppState()` / `saveAppState()`，由 `stateEdited` 驱动）。

改完跑 `tst_app_list` / `tst_app_list_model` / `tst_launcher_state`；能从外面观察到的行为在
`scripts/acceptance.ps1` 的「程序启动器」那一段加检查（**别在网格里按 `Enter`**，那会真的启动程序；
`End` + `Enter` 落在「全部程序」按钮上是安全的）；纯行几何 / 卡片外观用 `tmp/preview/` 抓图看。

**快速启动键**只在模型里算：加一种就动 `rebuildRows()` 里的 `m_itemKeys`/`m_recentKeys`/`m_pinKeys`、
标签函数（`digitLabelForLine()` / `recentKeyLabelForLine()` / `pinKeyLabelForLine()`）、`itemModels()`
的优先级、`handleKey()` 的一条分支、`footerText()` 的一句提示 —— 别做成 QML 里的按键表。
**新的一行形状**：模型加一个 `Row::Kind`、`data()` 的 `RowKindRole` 多一个字符串、`AppPopup.qml` 多一个
内联组件（并加进 `preload()` 的假数据好让它被预热）。

**过滤与身份**：凡「某个东西算不算程序 / 卸载程序」的判据都写在 `core/app_list.*`（`tst_app_list` 直接
测），**不要往平台层里堆名字匹配**。「哪些条目进列表」目前只有一个开关：`platform/win/apps.cpp` 里那两次
调用。要做成可配置（例如给 `apps()` 加排除表）就得同步改 `core/config` 校验、README 的 schema 表与示例
配置 —— 那是公开接口，当破坏性变更对待。

**拼音表 / 匹配语义**：表是生成的（`tools/pinyin_gen.ps1` → `src/core/pinyin_data.*`），**别手改生成物**；
改覆盖范围（例如把 Ext A 也纳入）就改脚本再跑一遍，`pinyin_data.h` 的区间常量跟着改。匹配语义全在
`core::appSearchText()`，测试是 `tst_app_list`（表完整性也在那里）。

## 新的 QML 弹窗

`import QtQuick.Controls.FluentWinUI3`；颜色一律从 `palette`（`base`/`text`/`placeholderText`/
`highlight`/`highlightedText`/`alternateBase`/`mid`）取；字号用现在这套 `pointSize`（12.5 标题 / 11 正文
/ 10.5 帮助正文 / 9 副标题与徽标 / 8.5 细节）；**中文一律 `font.family: "Microsoft YaHei"`**；需要滚动的
列表用真正的 `ListView` + `ScrollBar`；固定表头/底部提示看 `HelpPopup.qml` 的
「`topMargin`/`bottomMargin` + 不透明底色」三件套；**避开 FluentWinUI3 不支持的那些控件**；文件开头写
`pragma ComponentBehavior: Bound`，并用 `qmllint -I …` 确认零警告；别忘了加进 `PopupHost::preload()` 与
`flags: … | Qt.Tool`。

**给弹窗/日志窗口加新的 QML `import` 时，先看 `cmake/PruneRuntime.cmake` 的保留名单**：用了别的东西
（比如 `QtQuick.Dialogs`）就要从清单里拿掉对应的删除项，否则弹窗在启动预热那一步就会报
`could not load ….qml: module "…" is not installed`（`scripts/acceptance.ps1` 有一条哨兵检查盯着；开发期
跑 `build/windows-debug` 也会报）。改了清单会自动重新链接（`LINK_DEPENDS`）。

## 新的电源操作

`PowerOp` 加变体 → `platform/win/power` 里处理（需要特权的先调 `enable_shutdown_privilege()`；不需要的
要放在它**之前** return）→ `as_str` 与简写 → README 表格。**不给它加自动化测试。**

## 改配置模式（新字段 / 新取值）

1. `core/config` 加字段（**字段名不要用 Lua 关键字** `repeat`/`end`/`for`/`local`/`function`/`then`/
   `until`……，需要的话给一个 Lua 友好的别名）。
2. 需要的话在 `lua_prelude.lua` 里加构造器。
3. `flowkeyd.lua.example` 里加一条（`--check` 会立刻告诉你它能不能过校验）。
4. `README.md` 的表格 **在同一个任务里改完**。
5. 图形界面：`core::settingsToMap()`/`settingsFromMap()`（或对应的条目转换函数）、
   `core::configToLua()`（新字段有"两种书写形状"时，像 `remote_desktop`）、再在 `ConfigGeneralPage` /
   `ConfigAdvancedPage` / 对应弹层里加一行。

`tst_config_writer` 的**整份配置往返**会把接错的地方直接暴露出来（拿真 Lua 层读回自己写的东西、逐字段
对比）。三处清单见 `AGENTS.md` 工作约定第 13 条。

## 新的窗口摆放字段

`core/config` 加字段 → `lua_config.cpp` 的 `convertWindowRule` 加白名单与读取 → `lua_prelude.lua` 的
`window_rule` 注释 → `core/placement` 里影响几何/匹配（或只影响 `summary()`）→ `app/dispatcher` 的
`placeWindowOnce` → README 的 `window_rule` 一节与示例 → `tst_placement`/`tst_config`/`tst_lua`。要调新的
Win32/COM 后端就放在 `platform/win/*`，并在 `tst_interactive` 里加一条真机验证。

## 新的 `monitor` 取值 / 新的显示器算法

取值加在 `core/monitor_ref.h` 的 `Kind` 上 → `lua_config.cpp` 的 `convertMonitorRef` 与
`selectMonitor()` / `describe()` / `monitorRefIsConcrete()` / `monitorRefIsDynamic()` 四处都要跟。解析成
"哪一块"只有两个地方：`dispatcher.cpp` 的 `resolveWindowMonitor()`（问系统 / 查记忆）与
`placeWindowOnce`（摆几何）；真要调新的 Win32/COM 后端就放给 `platform/win/monitor.*`（像
`indexForCursor` 那样）→ README 的「窗口摆到哪块显示器上」一节与示例配置 → `tst_placement`（纯逻辑）+
需要真桌面的放 `tst_interactive`。**`Kind::None` 一定要单独处理**：它不是"摆到某一块"，而是"这次不碰
位置"。

## 新的 app 字段

`core/config.h` 的 `AppDef` 加字段 → `lua_config.cpp` 的 `convertApp` 加白名单与读取 → 在
`core/config.cpp` 的 `expandApps` 里决定怎么继承/忽略（纯逻辑，`tst_config` 直接测）→ `lua_prelude.lua`
的 `app` 注释 → README 与示例 → `tst_config`（展开）/`tst_lua`（转换）。如果新字段是"可被动作局部覆盖
的一整份值"（像 `launch`），照 `LaunchFields` 的做法把"写了哪些键"记下来。

## 新的工作空间能力（`workspace`）

三层，别跳墙：

1. **纯逻辑**：`core/workspace.*`（`WorkspaceState` 的命令 → `WorkspaceEffects`）。新语义加在这里，并在
   `tst_workspace` 加一条用例（那里已经有 3000 步随机序列 + 不变量检查）。
2. **平台层**：`platform/win/workspace.*`。隐藏方式只在这一个文件里（目前唯一可用的是
   `ShowWindow(SW_HIDE)`）；**新增任何 hide/show 路径都要保持“先写恢复日志、再隐藏”**（M8），而且
   只能在工作线程上做。枚举可管理窗口的判据在 `core::isManageableWindow()`（纯，`tst_window_match`）。
   **跨显示器搬窗口用 `relocateToMonitor()`**（保持显隐 / 最大化 / 最小化），不要用
   `monitor::moveToMonitor()` —— 后者会把藏起来的窗口弹出来。
3. **动作与规则**：`Action::Kind::Workspace`（`workspaceOp` + `workspaceIndex`）/ `WindowRuleDef.workspace` /
   `AppDef.workspace`。动作要同时改 `actionToMap`/`actionFromMap`、`actionKindNames()`、
   `defaultActionMap()`、`ActionEditor.qml` 的 `visible: root.type === "workspace"` 分支、`ConfigText.qml`
   （工作约定第 13 条）；规则字段要同时改 `config_map.cpp` 的 `windowRuleToMap/FromMap` 与
   `WindowRuleFields.qml`。再加一项：`settings.workspace` 的嵌套字段要同时改 `settingsToMap/FromMap`、
   `ConfigEditor::setWorkspaceSetting()` 的调用点（`ConfigAdvancedPage.qml` 的工作空间卡片）与 README
   的「工作空间」一章。
4. **事件管线**：钩子线程（`platform/win/hook.*`）只把 `core::WorkspaceEvent` 入队，动作工作线程
   （`app/dispatcher.cpp` 的 `applyWorkspaceEvent()`）是**状态机的唯一所有者**，效果由
   `applyWorkspaceEffects()` 交给平台层。新增一种事件的面积固定是三处：`core/workspace_event.h`
   的 `Kind`（+`workspaceEventName()`）、钩子里产生它的那一处、`Dispatcher::applyWorkspaceEvent()`
   的 `switch`。**自己引起的显隐**要在 `applyWorkspaceEffects()` 里先 `expect()` 再动手（`SW_HIDE`
   会额外产生一条 `EVENT_OBJECT_SHOW`），否则状态机把自己的操作当成用户操作。纯逻辑部分在
   `tst_workspace_event`，真机管线在 `tst_interactive::hookThreadReportsWorkspaceEventsForForeignWindows`
   （**要“新窗口出现”就别用记事本**，见 `pitfalls.md`）。
5. 实施计划与剩余任务（恢复日志、与 `windows()`/`window("activate")` 的联动）在仓库根 `plan.md` 的
   M8–M11。

## 新的窗口条件 / 跨桌面语义

* **新的窗口条件**：`core/window_match`（纯逻辑）+ `platform/win/window` 的枚举适配 +
  `launchThenActivate` 回退 + 手工冒烟清单里加一条用例。
* **「窗口在别的虚拟桌面上时怎么办」的其它规则**：决定这一步的是 `core::windowRulePinsDesktop()`
  （纯逻辑）+ `dispatcher.cpp` 的 `bringWindowToCurrentDesktop()`（两个调用点：`executeWindowAction` 与
  `launchThenActivate`）+ `platform/win/desktop::moveWindowToCurrentDesktop()`。要加第三种语义就照这三层
  拆，别把配置查询塞进平台层；纯逻辑改完在 `tst_placement` 加断言，真机行为在 `tst_interactive` 里加一条
  （参照 `bringsAWindowOnAnotherDesktopToTheCurrentOne` 的窗口登记等待）。

## 启动器右键菜单的新行为

平台层在 `platform/win/shell_menu.*`（换条目只改 `QueryContextMenu` 的 flags；某一条该不该出现 / 变灰是
`IContextMenu` 自己的事），「执行之前先干点什么」走 `showItemMenu()` 的 `beforeInvoke` 回调，「选完关不关
卡片」在 `AppListModel::afterContextMenu()`（`tst_app_list_model` 盯着）。能从外面观察到的行为在
`scripts/acceptance.ps1` 的「程序启动器」那一段加检查（**别在菜单里按 `Enter`**：那一格可能是「卸载」或
「以管理员身份运行」）。**动到菜单尺寸 / DPI 时先看 `pitfalls.md` 那一段**：不要给菜单套 DPI 上下文（会
把菜单放大一倍），改完在 `tmp/dpimenu/` 与 `tmp/preview/` 的 `checkAppsMenu()` 上量一遍（每条约 22 逻辑
像素）。

## 按键 / 未公开 API / 虚拟桌面能力

* **新按键或别名**：扩展 `core/keys` 的键表并加一个往返用例（要有一个测试遍历表里每个名字）。靠扩展标志
  才能区分的键（像小键盘 Enter）还要在 `key_from_hook`/`native_key` 里加一条翻译。
* **新的 Windows 版本的虚拟桌面接口**：往 `platform/win/desktop` 的版本表里加一条（生效的
  `build.revision`、两个 IID、vtable 布局），再在真机上确认选中的条目、桌面数量与序号。
* **新的虚拟桌面能力**：先在 `platform/win/desktop` 里手写那个接口的 vtable 结构体（下标以 VD.ahk /
  MScholtes 的实现为参考），并尽量用**已公开**的 API 做交叉检查（`GetID` 只认"有且只有一个匹配"；
  `PinView`/`UnpinView` 用 `IsViewPinned` 验证状态真的变了）。失败时只报错、不要去做可能是错的事。
  **先判断那个 IID 是否随版本变化**：变的（`IVirtualDesktopManagerInternal`）进版本表；不变的（如
  `IVirtualDesktopPinnedApps`）只用一个常量。
* **新的未公开 API**：在 `platform/win/nt` 里用 `GetProcAddress` 解析，使用前先用一次无害调用校验，并永远
  保留一个已公开的回退。已公开但不在静态链接集合里的库走同一条路（`dwmapi`、`imm32` 是范例）。
* **远程桌面检测的新信号**（例如 `GetSystemMetrics(SM_REMOTESESSION)`）：纯逻辑放
  `core/remote_desktop.*`，判定点只有 `platform/win/hook.cpp` 的 `updateRemoteDesktop()`（钩子线程），
  引擎只接一个 bool。加信号时记住三件事：名单要可配（`settings.remote_desktop`）、默认名单要窄、状态翻转
  时由引擎负责释放被放行的重映射按键。只加一个内置进程名就改
  `builtinRemoteDesktopProcesses()` + README + 示例配置 + `tst_remote_desktop` 四处。

## 新动作后端 / 在线更新 / 图标 / 日志窗口

* **新的动作后端**：在 `src/platform/win/` 下新建模块，从 `dispatcher` 调用。
* **在线更新的新行为**（启动时自动检查、签名校验、把运行时也一并升级）：纯逻辑在
  `src/core/update_check.*`，状态与文案在 `src/app/update_model.*`，联网/下载/校验/落盘在
  `src/app/updater.*`，换文件与重启在 `src/platform/win/update.*`，卡片在 `UpdatePopup.qml`（按钮通过
  `PopupHost::updateXxx()` 转给 `Updater`）。改"去哪儿查"只改 `core::updateRepository()` /
  `latestReleaseApiUrl()`；新增一个状态就在 `UpdateModel::Phase`/`phaseName()`/`statusForPhase()`/几个
  `canXxx()` 里各加一条（`tst_update_model` 盯着）。**不要在 `main` 里提前做替换**：换 exe 必须在运行时
  停干净之后。验证走三个 update 单测、`tst_interactive` 的联网用例，以及 `pitfalls.md` 里"本地假
  GitHub"的沙箱 E2E。
* **换图标**：改仓库根目录的 `logo.svg`（唯一真源），然后 `cmake --build --preset debug --target icons`
  重新生成 `assets/flowkeyd.ico` 与 `assets/icons/flowkeyd-<n>.png`（**两者都要提交**）；尺寸列表写在
  `tools/icon_gen/main.cpp`（`kSizes`）、`CMakeLists.txt` 的 `qt_add_resources` 与
  `src/app/app_icon.cpp` 里，**三处要一起改**。exe 图标走 windres、托盘图标走 qrc，用的是同一张源图。
* **新的日志窗口行为**：尾随逻辑在 `app/log_model`（纯逻辑、可单测），渲染在 `LogWindow.qml`。加命令行
  参数就改 `cli.cpp` 并更新 README 的命令行表格。
* **构建版本号的来源**：全部在 `src/core/version.*`；显示侧（托盘、启动日志、`--version`、`--help`）
  不用动。

## 发布包里新增 / 删除文件

`scripts/release.ps1` 的 `$SlimFiles` / `$DependencyPatterns` 是"哪个文件进哪个包"的**唯一清单**；
`dist` 里出现两边都不认识的文件时脚本直接失败。新的部署产物按"每次构建都会变吗"分类：会变的写进
`$SlimFiles`（进完整包与精简包），不变的（新加的运行时 dll / 插件 / QML 模块）写进
`$DependencyPatterns`（只进完整包）。

**发布说明的正文**：`scripts/release.ps1` 的 `Get-ChangeSummaryLines`（分类）/
`Get-PreviousReleaseTag` / `Get-RepositorySlug` / `Write-GeneratedNotes`（拼起来 + 写文件）。分类靠
**提交信息的前缀**，表就是函数上面的 `$script:NoteCategories`（认新前缀就加一行；不想逐条列出的前缀加进
`$script:NoteSkippedPrefixes`）。**它只读提交信息、不看 diff** —— 所以写提交信息时的前缀（`新增：` /
`修复：` / `变更：` / `配置：`）就是发布页的分类依据。**正文只有 changelog**（不要加回「下载哪个包」
那一段）。
