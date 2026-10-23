# 决策详档（AGENTS.md 第 2 节）

> 摘要与「动手前必须知道的那一条」在 `AGENTS.md` 第 2 节，这里是每条的完整内容与理由。
> 每一条都是项目所有者明确选择的：**不确定要不要改就先问用户**，不要自作主张地「改进」。

## 1. 应用形态：CLI 守护进程 + QML 弹窗，没有主窗口

`main.cpp` 走 `QApplication` + `QQmlApplicationEngine` + `QSystemTrayIcon`；命令行参数是唯一的控制面。

## 2. Lua 用 `vendor/lua` 里现成的 5.5.1

不换成 5.4。与 5.4 的差异见 `AGENTS.md` 第 8 节。

## 3. 分阶段实施

阶段表见 `AGENTS.md` 第 9 节。自动化测试以 Qt Test 为主，桌面行为由 `scripts/acceptance.ps1` 覆盖，
脚本覆盖不到的靠手工冒烟（`reference.md`「手工冒烟清单」）。

## 4. 配置 schema 与 CLI 开关是 flowkeyd 稳定的公开接口

字段名、取值、动作字段、DSL 构造器名（`settings{}`/`hotkey{}`/`remap{}`/`window_rule{}`/`app{}`/
`run()`/`send()`/`type_text()`/`open()`/`notify()`/`volume()`/`media()`/`window()`/`clipboard()`/
`caps_lock()`/`suspend()`/`desktop()`/`power()`/`menu{}`/`help()`/`windows()`/`reload()`/`quit()`/
`none()`）都要在 `README.md` 里写清楚，改动当**破坏性变更**对待（同步更新示例配置与 README）。
脚本全局的注册表表名是 **`flowkeyd`**。

## 5. flowkeyd 最终接管这台机器

第 10 阶段已完成。

## 6. 不做 `--simulate` / `--selftest` / `--probe`

`--check` / `--list` / `--list-keys` 保留（既是产品功能，也是手工验证的主要工具）。阶段 9 用
**只给测试用的后门** `FLOWKEYD_ACCEPT_INJECTED=1` 抬升"丢弃注入输入"那道过滤，配合
`scripts/acceptance.ps1` 做验收。**不变量 2 本身没动**：flowkeyd 自己注入的事件带 `"FLOW"` 标记，
仍在钩子回调第一步被丢掉。

## 7. 依赖政策：默认只有 Qt + `vendor/lua`

需要新依赖时按需引入，但必须在提交信息里给出理由。JSON、CLI 解析、字符串工具都自己写或用 Qt 自带的；
不要引入 `sol2`、`nlohmann::json`、`CLI11`、`spdlog` 之类"顺手"的库。

## 8. 构建：CMake Presets + Ninja，debug 与 release 双 profile 都必须编译通过

debug 里构建单测（`FLOWKEYD_BUILD_TESTS=ON`），release 里不构建单测、只产出 `build/dist-release`。
单元测试只在 debug 上跑，验收脚本只跑 release 的产品 exe。

## 9. 弹窗界面用 Qt 自带的标准控件，不自绘

* 列表是 `ListView` + `ItemDelegate`；筛选框是真正的 `TextField`；**模型里要给 QML 调的方法一律加
  `Q_INVOKABLE`**（否则 QML 抛 TypeError，而且**处理器里后面的语句会被静默跳过**，看上去像"处理器没跑"）。
* **鼠标左键点一行 = 选中它 + 把它的按键复制走**；滚动（滚轮、拖滑块）只滚视图、**不动**选中项。
* **`Enter`（或双击一行）= 关掉窗口并执行那一行的动作**。执行前先关窗是刻意的：`send`/`type`/
  `window` 作用在**前台窗口**上，不关窗就会打回弹窗自己的筛选框。
* **`quit`/`suspend`/`power` 要按两次**（`core::isDestructive()`）：第一次只是"武装"（行变色 + 底部
  提示换确认文案），再按一次才真执行；`Esc`、上下换行、改筛选都取消。**`menu` 不算危险**。
* 列表只占行区域（`topMargin`/`bottomMargin` + 不透明底色盖住滚到边缘的行），键盘把选中项带进视野用
  `ListView.positionViewAtIndex(..., Contain)`。
* **滚轮方向跟着系统/Qt**（本机实测 `mouseData=-120` 往下、`+120` 往上）。
* 选单（`MenuPopup`）同一套：标准 `ListView` + `ItemDelegate`，**不滚动**（条目数决定卡片高度），
  左键点一行 = 执行它。区别只有悬停仍驱动高亮（`Enter` 执行光标下那一条，这是菜单的语义）。
* **中文字体用 `font.family: "Microsoft YaHei"`**：默认族 `Segoe UI Variable` 没有中文字形，不管的话
  中文会回退到宋体。`Window`/`Item` 没有 `font` 属性、不会往下传，每个会画字的控件都要写。

## 10. 开机自启：计划任务指向当前运行的 exe（不是安装目录）

任务 `flowkeyd` 在登录时以**最高权限**启动一个 exe（提权但不弹 UAC），路径由守护进程每次启动时自检/
刷新（`platform/win/autostart`，内部走 `schtasks /Create /XML`，XML 以 UTF-16LE + BOM 写到 `%TEMP%`）。
**不写任何安装目录、没有部署脚本。**

* 开发/测试实例跳过：`--no-elevate`、`--allow-multi`、未提权、显式的 `--no-autostart` 都不碰计划任务
  —— 否则临时实例会把用户的开机自启劫持到一个会被清理的构建目录（任务指向失效路径是**完全静默**的
  失败）。
* 删除自启用 `--remove-autostart`（需管理员；先 `--quit`）。
* 任务参数（都与当年 `install.ps1` 的 XML 逐字段一致）：`PT15S` 登录延迟、`ExecutionTimeLimit=PT0S`、
  `DisallowStartIfOnBatteries=false`、`StopIfGoingOnBatteries=false`、`InteractiveToken` + 指定
  `UserId`、`MultipleInstancesPolicy=IgnoreNew`、`RestartOnFailure` PT1M×3、`WorkingDirectory` = exe
  所在目录。**不用** BootTrigger（跑在 session 0，没桌面），不用 Task Scheduler COM（要手写十来个
  vtable）。
* **注册/刷新之前先问用户**（原生确认框）；同意才动任务，拒绝就保持原样。`--no-prompt` 跳过询问、
  按默认「注册/更新」处理（脚本用）。

## 11. 启动时的两个交互确认

都用**原生 `MessageBoxW`**（跑在 `QApplication` 构造之前）：

1. **已经在运行**：同一配置文件有实例在跑就弹 `flowkeyd 已在运行`，点确定后以**退出码 1** 退出。
   **这一步刻意放在 UAC 提权之前**（真正的互斥体获取仍在提权之后，兜住"两个进程同时启动"的竞态）。
2. **开机自启**：任务缺失或指向别的 exe 时弹 `flowkeyd 开机自启` 问要不要更新。

`--no-prompt` 跳过这两个提示。守护进程出错（配置错误）弹的是 `QMessageBox`（标题 `flowkeyd 配置错误`，
正文是英文错误）；离线命令那条路只打印、不弹窗、不提权。

## 12. 构建版本号 = `yy-MM-dd-<git 短修订>`

例如 `26-09-22-42900ad`（`core/version.*`）。

* **日期**取运行中这个 exe 自己的最后写入时间（本地时间 `yy-MM-dd`）——**不用编译期常量**（那会让每次
  构建都重新链接，`ninja: no work to do` 就不再成立）。
* **修订**是构建时由 CMake 用 `git rev-parse --short HEAD` 取到、经 `cmake/version_revision.h.in` 写进
  `build/<preset>/generated/flowkeyd_revision.h` 的编译期常量（只给 `core/version.cpp` 包含）。CMake 把
  `.git/HEAD` 与**当前分支的 ref 文件**登记成 `CMAKE_CONFIGURE_DEPENDS`（只在 configure 时那个 ref 确实
  以松散文件存在才会登记；在 `packed-refs` 里就会漏，提交后要显式 configure 一次）。
* 显示在四处：托盘右键一个**不可点**的*版本*信息项、守护进程启动日志第一行、`--version` 第一行、
  `--help` 表头。取不到 exe 时日期段是 `unknown`；不在 git 仓库里时修订段是 `unknown`。
* **工作流是「先提交再构建」**：哈希就是构建时的 HEAD（工作区脏时显示最近一次提交）。提交之后**懒一下
  也没关系**，但踩过一次「刚提交完直接 `cmake --build --preset release` 是 no work to do、生成的
  `flowkeyd_revision.h` 还是上一个提交」——为了不把旧哈希的包当成新的发布，**收尾时显式
  `cmake --preset windows-release` 一次（或者看一眼 `--version` 的哈希）再构建**是最稳的。

## 13. 窗口摆放规则 `window_rule{...}`

控制"某个程序启动时出现在哪个**虚拟桌面**和哪块**显示器**"（代码里的 `desktop` 就是 Windows 虚拟桌面），
且**显示器重新接入时按配置重新归位**。字段：
`process`/`title`（至少一个）、`desktop`、`all_desktops`、`topmost`、`monitor`、`maximize`、`x`/`y`、
`width`/`height`、`name`、`enabled`。

* `monitor` 只支持**序号**（1 起，左→右、上→下）、`"primary"`、**设备名**（`"DISPLAY2"` /
  `"\\.\DISPLAY2"`）；不支持按分辨率匹配。
* **默认最大化**（写了 `monitor` 又没写位置/大小时铺满工作区），位置与大小可配（`x`/`y` 相对目标工作区
  左上角）；`maximize = true` 与位置/大小互斥。只写 `desktop` 时**不动窗口几何**。
* 触发时机只有三个：**窗口第一次出现**、**显示器重新接入**（判据是设备名从无到有，350 ms 轮询）、
  **flowkeyd 启动时**。之后不再干预。
* `all_desktops`（`true` 钉在所有虚拟桌面 / `false` 显式取消，不写就不碰）走
  `IVirtualDesktopPinnedApps`（**不是** `IVirtualDesktopManagerInternal`），**与 `desktop` 互斥**。
* `topmost` 走已公开的 `SetWindowPos(HWND_TOPMOST)`。这两个开关**都算"设置一次"**、不是几何：只写它们
  时窗口大小/位置不动，也不会因为"默认最大化"突然变大。
* "主窗口"判据：可见、无属主、非 `WS_EX_TOOLWINDOW`、有标题、尺寸非零。
* 实现分散在 `core/placement`（纯几何/匹配）、`platform/win/monitor`、
  `platform/win/desktop::moveWindowToDesktop`、`platform/win/hook`（`SetWinEventHook` + 显示器轮询）与
  `app/dispatcher`。

## 14. 跨虚拟桌面的"唤起"与跟随

1. `window` 动作的 `activate`：目标窗口不在当前虚拟桌面上时不算"已经在前台"，按下去就是**切到它所在的
   那张桌面并激活它**；只有它已经在当前桌面并且真的在前台时才是 `toggle` 的收起。
2. `window_rule` **真的**把窗口搬到另一张桌面时，视图也跟过去并重新激活那个窗口。**只在"窗口第一次
   出现"那一遍做**（启动 / 显示器重新接入那两遍只重新摆放）。"真的搬动" = 窗口的桌面 GUID 变了。

原因：`MoveViewToDesktop` 把窗口搬走之后 shell 仍然把它当**前台窗口**，所以"是不是已经激活"的判定不带
虚拟桌面时，同一个快捷键会去*收起*用户看不见的窗口；而且这种情况 `SetForegroundWindow` /
`AttachThreadInput` / `BringWindowToTop` 全部返回 TRUE 却什么都不做，必须先显式 `SwitchDesktop`。

## 15. `app{...}`：把同一个程序的窗口规则与快捷键写在一起

只是书写上的合并，`compile()` 把它展开成一条普通 `WindowRuleDef` + 若干 `HotkeyDef`，展开出来的条目
**排在全局 `hotkey{}` / `window_rule{}` 之后**。

* 字段只有 `name`/`process`/`title`/`launch`/`window`/`hotkeys`/`enabled`；`process` 与 `title` 至少
  写一个。
* `window` 就是一条 `window_rule`（字段完全一样），`process`/`title`/`name` 自动继承，显式写的优先；
  不写 `window` 就只展开快捷键。
* `launch` 就是这个程序怎么启动，字段与 `window` 动作的 `launch` 一样（`program`/`args`/`cwd`/`show`/
  `shell`/`env`/`wait_ms`/`elevated`）。它也是**默认值**：动作不写 `launch` 就整份继承，写了就**逐字段
  合并**；动作顶层的 `wait_ms` 覆盖 `launch.wait_ms`（没有 `launch` 却写 `wait_ms` 要报错）。
* "写了哪些字段"由 Lua 层在转换时记进 `core::LaunchFields`（`show`/`shell`/`args` 的默认值与"没写"在
  值上分不开）；加新字段时照 `extension.md`「新的 app 字段」那一段的做法。
* `hotkeys` 里每一项就是一条 `hotkey`；其中的 `window` 动作自动补上 app 的 `process`/`title`/`launch`
  （显式写的优先），**嵌套在 `menu` 条目里的也算**；只对表/构造器形式的 `window` 动作生效。
* 名字默认：`name` → `process` → `title`；app 里**只有一个 hotkey** 时也给那条绑定当默认名（多个时
  保持"第一个和弦"的默认）。
* `enabled = false` 把整条 app（规则 + 全部快捷键）都丢掉并给一条 warning。
* **全局的 `hotkey{}` / `window_rule{}` 保留**。声明式写法在返回表里叫 `apps`；错误标签是
  ``app #1 (`wps`)``（不写 `name` 就用 `process`）。

## 16. `window` 动作的四个「挪窗口」op

复用同一套目标查询、`swallow` 与 `--list` 摘要：

* `move_prev_desktop` / `move_next_desktop`：只动**虚拟桌面**，窗口几何不变，**视图不跟着走**（与
  Windows 自己的 `Win+Ctrl+Shift+←/→` 同义）。两张桌面**首尾相接**。走
  `desktop::moveWindowToAdjacentDesktop`（复用 `MoveViewToDesktop`）。
* `move_left_monitor` / `move_right_monitor`：只动**显示器**，虚拟桌面不变。**保留最大化状态**；普通
  窗口保持原有大小并**居中**到目标工作区；最小化的只更新还原位置。没有更左/更右那块时失败并记日志，
  **不循环**。
* 四个 op 都**不套用 `toggle`**、**不接受 `launch`**；`core::windowOpHasTransition()` 对跨显示器移动
  返回 true、对跨虚拟桌面移动返回 false —— 写在不产生过渡的 op 上的 `animate` 会被 `--check` 报错。
* 相邻下标由纯函数 `core::stepIndex(count, current, delta, wrap)` 算（虚拟桌面 `wrap = true`、
  显示器 `wrap = false`），有单测（`tst_placement`）。

## 17. 字母键名一律小写

配置里凡是表示按键的单个字母（`keys` 的按键、`remap` 的 `from`/`to`、发送脚本 `{...}` 里的键名、选单
条目的 `key`）都写小写；`a` 就是 A 键，要按住 Shift 的"大写键"必须**显式**写 `Shift+a`，直接写大写的
`A` 会被 `--check` 拒绝（`letter key names must be lowercase: write ...`）。

* **例外：发送脚本里的裸字符保持 AutoHotkey 语义** —— `send("A")` 就是打出大写 A（等价于
  `send("+a")`），`send("Hello")` 照旧能打出 `Hello`；要按字面输入任意文本用 `type("...")` 或
  `send("{Text}...")`。花括号里的是**键名**，所以 `send("{S}")` 报错、要写 `send("{s}")`（要 Shift 就写
  `send("+s")`）。
* 实现：`core/keys.cpp` 的 `uppercaseLetterKey()`（只识别"单个大写字母"这一种形状）+ `parseChord` /
  `parseSendScript` 两处检查 + `core/config.cpp` 的 `validateMenu` 与 `remap` 的 `to` 检查。
  `parseKeyOrScript()` 本身保持宽松。`nameFromKey()` 与 `allKeyNames()`（`--list-keys`）一律输出小写。

## 18. `window` 的 `follow` 选项

只对 `move_prev_desktop`/`move_next_desktop` 有效，默认 `false`。`follow = true` 时搬完窗口**把视图也
切到目标桌面**并重新激活它。这就是 `Win+i`/`Win+u` 与 `Win+Shift+i`/`Win+Shift+u` 的区别。

* 配置层：`Action::follow`（`std::optional<bool>`）；`--check` 只允许它出现在这两个 op 上（写在别的 op
  上是静默空操作，所以报错）；`--list` 摘要里显示 `(follow)`。
* 搬窗口与切视图在**同一个 STA 会话**里做完：先 `MoveViewToDesktop` 再 `SwitchDesktop`，用**已知的
  目标下标**，不去读窗口的桌面 GUID（那个是异步的）。之后 `window::applyTo()` 再补一次 `raiseWindow()`。
* "保持激活"是尽力而为：`raiseWindow` 失败只写 warning，动作仍算成功（detail 里带 `, could not
  activate`）。
* **它不产生窗口过渡**（只切视图、不动几何），所以 `animate` 的规则不变。

## 19. 前台窗口是覆盖层时跳过它

不写 `target`/`process` 的 `window` 动作作用于 `GetForegroundWindow()`；但按住 Win 约一秒会弹出
**PowerToys 的「快捷键指南」**（`PowerToys.ShortcutGuide.exe`，`WS_EX_TOOLWINDOW | WS_EX_TOPMOST`）并
成为前台窗口，于是动作会去操控那个覆盖层（跨桌面移动直接报 `could not read the desktop id of the
window`）。现在 `window::find(foreground)` 发现前台是别的工具窗口时，沿 Z 序往下找第一个"主窗口"
（判据同第 13 条）。

* 顺带把 `desktop` 层收紧了两处：`Session::windowDesktopId(hwnd, attempts)` 可重试（默认 1 次；
  `desktopIndexOfWindow` 用 3 次）——`GetWindowDesktopId` 在 `MoveViewToDesktop`/`SwitchDesktop` 之后、
  或窗口处于过渡状态时会返回 `TYPE_E_ELEMENTNOTFOUND`(0x8002802b)；`Session::moveWindowToAdjacent()`
  搬完**等桌面 GUID 真的变了再返回**（否则按住 Win 连按两次会读到旧桌面、把目标算错）。

## 20. 托盘图标的主体是「当前是第几号虚拟桌面」

* **画法**：蓝色渐变圆角方块（`logo.svg` 的 `#00d2ff` → `#3a7bd5`，圆角比例照抄 `rx = 56/256`）+
  白色粗体数字居中。数字按**字形墨迹**居中（按含行距的方框居中会明显偏高）。
* **两位数一律显示 `9+`**（16 px 上两位数挤成一团）。画什么字、用什么字号是纯逻辑
  （`core::desktop_badge`，`tst_desktop_badge` 盯着），图片是 `app::desktopIcon()`（9 个尺寸各自渲染，
  不缩放位图）。
* **查不到时退回应用图标**（锁屏、非交互会话、版本表对不上）：不画 `0`、不留空白图标；悬停提示里的
  `（桌面 N/M）` 也跟着消失。
* **来源是 500 ms 的轮询**，因为虚拟桌面没有任何"切换了"的通知。轮询放在**动作线程**上
  （`Dispatcher::startDesktopWatch()`，由 `Runtime::start()` 排队投进去——`QTimer` 必须在它自己那条
  线程上创建），查到的值经 `Runtime::reportDesktop()` 投回 GUI 线程换图标，**绝不在 GUI 线程上做
  COM**。查失败只在状态翻转时写一条 debug 日志，并**保留上一次的数字**，免得锁屏时图标来回闪。
* `desktop::currentDesktopIndex()` 是给这条轮询用的精简只读查询；`windowsVersion()` 的结果因此改成
  **进程内缓存**（否则每 500 ms 读一次注册表）。
* 与 `desktop` 动作共用同一张未公开的版本表，所以**只有能读到桌面时**才有数字。

## 21. 窗口切换器 `windows()` 与「轻碰 Win」

`windows()` 弹出一张卡片，列出当前打开的程序窗口；输入就按**进程名前缀**把窗口筛掉，筛选到**只剩一个
窗口时直接激活它**。

* 枚举判据是 `win::window::listOpenWindows()`：可见、无属主（或带 `WS_EX_APPWINDOW`）、非
  `WS_EX_TOOLWINDOW`、有标题、尺寸非零、不是 `Progman`，**跳过 flowkeyd 自己的进程**，而且**只列 shell
  真的会显示的窗口**（见下一条）。列表按 Z 序（最近使用在前），跨虚拟桌面的窗口也会列出来（激活时会把
  视图切过去）。
* **被 shell 藏起来的"假窗口"不进列表**（例如 `TextInputHost.exe` 的「Windows 输入体验」：
  `IsWindowVisible` 为真、尺寸正常、却点不到）。`win::window::isSwitchableWindow()` = `isMainWindow()`
  + 「没被 cloaked，或者只是被搬到别的虚拟桌面上了」：`DWMWA_CLOAKED` 非零 = 没显示出来，而
  `desktop::isWindowOnCurrentDesktop()` 又为真 = shell 藏在当前桌面上的宿主窗口。cloaked 但在**别的**
  虚拟桌面上的窗口要留着（`windows` 动作会切过去），所以判据里必须带上那个桌面查询；它也**只对 cloaked
  的窗口问**（COM 不便宜）。纯判据在 `core`（`TopLevelWindowFacts` + `isMainWindow()` /
  `isSwitchableWindow()`），`platform/win/window.cpp` 只负责取 Win32 值。`window_rule` 不走这一条。
* 筛选是**进程名的大小写无关前缀匹配**（标题只显示、不参与），**不是子串、也不是模糊搜索**：打 `chr`
  命中 `chrome.exe`，打 `hrome` 不命中。自动激活的判据是「只剩一个窗口」，不是「只剩一个进程」。
* **「轻碰 Win」= 单个修饰键 + `trigger = "release"`。** 引擎对这种和弦采用「tap」语义
  （`Engine::m_pendingTaps`）：按下修饰键本身**放行**（因此 `Win+E`/`Win+L` 这些没被接管的系统组合不受
  影响），期间只要有别的按键按下就作废，单独松开时才触发；触发时把既有的菜单遮断标记立起来、注入
  `VK_UNASSIGNED` 挡掉开始菜单。**单个修饰键配默认的 `trigger = "press"` 保持老行为**（按下即触发、
  可吞键）。

## 22. 窗口切换器的数字选择模式

筛选串命中的窗口**全属于同一个进程名**、而且不止一个时，卡片进入「窗口选择模式」：前 10 行依次分到
`1`..`9`、`0`，按一下数字直接跳过去；超过 10 个的不分配（`rowKey` 是空串，那一行不画徽标）。

* 触发判据：**筛选串非空 + 可见窗口全属于同一个进程名 + 至少两个窗口**。空筛选绝不编号；命中多个进程
  名时也不编号。与 `setFilter()` 的「只剩一个窗口就直接激活」互不冲突（那边是 1 个窗口）。
* 这种模式下 `handleKey()` 先把 `Qt::Key_1`..`Key_9`/`Key_0` 接掉，所以它们**不会跑进筛选框**；没有
  对应行的数字被吃掉但什么都不做。不在这种模式时数字键照旧放行给 `TextField`（`7zip` 这类进程名要能用
  数字筛）。
* 纯逻辑在 `WindowListModel`（`m_numbered`/`m_rowKeys`，角色 `rowKey`；`footerText` 在这种模式下换成
  「数字键直接切换」，所以从 `CONSTANT` 改成 `NOTIFY stateChanged`）。界面只是把 `rowKey` 画成行左侧的
  徽标（空串时不占位，标题与进程名跟着缩进 30 px）。

## 23. 五个弹窗不进任务栏，而且启动时就预热好

* **不进任务栏**：`MenuPopup.qml` / `HelpPopup.qml` / `SwitchPopup.qml` / `UpdatePopup.qml` 的 `flags`
  都加 `Qt.Tool`（Windows 上就是 `WS_EX_TOOLWINDOW`）。`Qt.Tool` 窗口仍然能被激活、能拿键盘焦点
  （验证过），只是不加任务栏按钮、不进 `Alt+Tab`。日志窗口**没有**改（它留在任务栏里是对的）。选
  `Qt.Tool` 而不是 `Qt.Popup`：后者要抓鼠标、点外面自动关。
* **首次弹出不再现场付钱**：`PopupHost::preload()` 在启动后的第一个事件循环回合被 `main` 排队调用，
  把五个窗口建出来、填一份假数据各渲染一帧（**透明度 0 + 屏幕之外**，用户看不到也点不到），首帧到了就
  藏起来。实测冷启动第一次弹出要 **224 ms** 才画出第一帧（~170 ms 是进程首次渲染的固定开销：QRhi/D3D11
  设备、交换链、Quick 材质着色器首次编译；~50 ms 是该窗口的 QML 加载与样式装配），预热之后第一次弹出
  **~30 ms**。
* 预热用的假数据**不需要**清理：每次真实弹出都会先 `setItems` 覆盖。行数写得多于一屏（menu 8 行、
  help/switch 14 行）是为了让 `ListView` 把一屏的 `ItemDelegate` 与 `ScrollBar` 也装配一遍。
* **屏幕之外是必须的**：透明度 0 的置顶窗口留在屏幕里万一赶上鼠标点击就会吃掉它。
* 真实弹出要先 `cancelPreload()`（用户可能在启动后的那 300 ms 里就按了快捷键）；用
  `QSet<QQuickWindow*> m_warming` 判断，**不要用全局"代"号**（会把另外几个还在预热的窗口的首帧回调一起
  废掉）。
* **显示器配置 / 缩放一变就把窗口缓存丢掉重建**（`discardPopupWindows()` + 重新 `preload()`，模型留着）：
  隐藏 + 摆在屏幕外的窗口收不到 `WM_DPICHANGED`，缩放变过之后它们会停在旧缩放上，弹出时只有正确大小的
  一半（细节与两层修法见 `pitfalls.md`「弹窗缩放过期」）。

## 24. 窗口切换器卡片的几处调整

* **没有标题行**：卡片里只剩筛选框、列表与底部提示；筛选框就是第一行，`listTop` = **50**（`kPad 12 +
  筛选框 30 + 间隙 8`），卡片高度 238（3 行时）。计数挪到了**底部提示**里；`countText()` 保留。
* **每一行是「进程名在上、窗口标题在下」**：主行是进程名（`pointSize 10.5` + `palette.text`），窗口
  标题退成副行的浅色小字（`8.5` + `palette.placeholderText`）。两个字符串还是只有都非空时才分两行。
  理由：筛选匹配的本来就是进程名，标题只用来分辨同一个程序的多个窗口。这只是 QML 里两个 `Label` 的
  上下关系，**模型层不知道也不管**。
* **卡片开着时再按一次同一个快捷键 = 关掉它**（与 `Esc` 同义）。判断在 `PopupHost::showSwitch()`：
  `window->isVisible() && !warming` 时直接 `switchDismiss()` 返回，**不要在那里重新弹一次** ——「轻碰
  Win」的触发发生在 Win 键**松开**时，所以第二次轻碰进来时卡片正开着；重新弹就是「关掉又立刻重开」。
  `windows()` 动作因此是「开/关」切换。
* `windows()` 的 `title` 参数现在只用作**窗口标题**（`caption()` = `<title> — N 个`），验收/诊断脚本
  靠它读"现在列了几个窗口"。
* **列表宽度 = 筛选框宽度**：QML 里 `ListView` 的 `x`/`width` 直接用 `filterRect`。
* **打开时切英文输入法**：筛选框匹配的是**进程名**（ASCII），而用户经常开着中文输入法。新模块
  `platform/win/ime.*`（运行时解析 `imm32.dll`）：读 `ImmGetConversionStatus`，`NATIVE`
  （`IME_CMODE_NATIVE`）置位时把它清掉（其余标志与句模式原样保留，等价于用户按一下 `Shift`）。Qt 在
  Windows 上**不看** `Qt.ImhPreferLatin`（`QWindowsInputContext` 只用 `ImEnabled` 决定要不要
  `ImmAssociateContext`），所以 QML 里写提示没用。调用点两处：`PopupHost::showSwitch()` 弹出后一次，
  以及筛选框 `onActiveFocusChanged` 时一次；**预热期间不调**。**关掉卡片时把打开前那份模式写回去**
  （`ime::readMode()` 快照、`ime::restoreMode()` 写回；调用点在 `switchChoose`/`switchDismiss`/
  `closeAll`）。快照只在真正弹出时记一次。只影响 flowkeyd 自己这个进程的输入模式，**但本进程的几个窗口
  会互相看见**。

## 25. 在线更新：托盘菜单里点一下，自己换 exe 并重启

* **下载的是发布脚本已经传上去的精简升级包**（`*-slim-windows-x64.zip`，约 700 KB，里面只有
  `flowkeyd.exe`），没有第三种资产。解压用 Qt **私有**的 `QZipReader`（`Qt6::CorePrivate`）；某次发布
  万一没有 slim 包就退回到完整包。
* **只换 `flowkeyd.exe`，不动 Qt/MinGW 运行时。** 新版本如果新增了运行时依赖，替换会**回滚**（新 exe
  活不过 2.5 秒 → 把旧的那份改回来）并提醒用户手动下载完整包。
* **触发方式是手动的**：只有点托盘菜单才联网，启动时不检查。匿名 `GET /releases/latest`，请求带
  `User-Agent` 与 `Accept: application/vnd.github+json`。
* **替换顺序**：旧 exe 改名成 `<exe>.old` → 新的改名就位 → 启动新实例 → 等 2.5 秒确认它还活着。运行中
  的映像可以改名（不能删/覆盖）。任何一步失败都回滚；`.old` 由**新实例**启动时删（旧进程还在退出时删
  不掉就隔 0.7 秒重试，最多 20 次）。
* **`--updated-from <旧版本>` 是重启时唯一的额外参数**（内部标记）：新实例靠它知道"我刚被更新过"，
  于是弹一条托盘通知，并且**只认它**（用户自己重启不会重复提示）。
* **下载下来的 exe 的 mtime 被对齐到发布日**（版本号里的日期段取 exe 的 mtime，见第 12 条）：不对齐的
  话更新完重启，版本号会变成"下载那一天"。
* 替换需要能写安装目录（默认提权运行，正常路径没影响）；更新**不碰**配置、日志与开机自启任务。更新
  失败时的模态框在 `--no-prompt` 下不弹（自动化用）。
* 新增静态库 **`flowkeyd_update`** 与 `platform/win/update.*`；一张 QML 卡片 `UpdatePopup.qml`
  （同样预热、同样 `Qt.Tool`）；`Qt6::Network` 进入依赖，所以 `cmake/PruneRuntime.cmake` **必须留下
  `tls/qschannelbackend.dll`**。

## 26. 远程桌面放行（`settings.remote_desktop`，默认开）

前台窗口的属主进程命中名单（默认只有微软的 RDP 客户端：`mstsc.exe`、`msrdc.exe`、`msrdcw.exe`、
`RdClient.Windows.exe`）时，**所有快捷键与重映射一律放行** —— 不拦截、不触发，键原样送给对面那台
机器；单条例外是条目自己的 `remote_desktop = true`（在远程桌面里也照常拦、照常执行）。

* 判据只有「**前台窗口的属主进程名**」（大小写无关的子串，与 `window_rule.process` 同一套），不看标题、
  不看是否全屏。`GetSystemMetrics(SM_REMOTESESSION)` 那条路（"flowkeyd 自己跑在远程会话里"）**没做**。
* 检测全在**钩子线程**上：`SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` 立刻更新（故意**不带**
  `WINEVENT_SKIPOWNPROCESS`：自家弹窗拿到前台就算"不在远程桌面里"）、启动时查一次、350 ms 的 placement
  tick 再兜一次。按键路径上没有额外开销。
* 名单空表 = 谁都不算（等于关掉）；`processes` 里的**空串是错误** —— 空串在
  `windowProcessMatches()` 里表示"不限制"，会让**所有**窗口都算远程桌面。运行时也跳过空条目（双层
  保险），见 `pitfalls.md`。
* 进入这个状态时清掉长按重复与待定的「轻碰 Win」，并把**已经被放行**的重映射按住的**目标键松开**
  （`remote_desktop = true` 的例外不动）；已经吞掉、还按着的键在松开时仍然吞掉。
* 纯逻辑在 `core/remote_desktop.{h,cpp}`；引擎侧是 `Engine::setRemoteDesktop()`，平台侧只有
  `platform/win/hook.cpp` 的 `noteForegroundWindow()`。

## 27. 程序启动器 `apps()`：开始菜单里的程序，摆成一张图标网格

* **数据源 = `shell:AppsFolder`**（「扫开始菜单」的结论：两个 `Start Menu\Programs` 目录里只有 `.lnk`
  —— 商店应用与 `.msc` 系统工具根本不在那里）。它就是开始菜单「所有应用」列的**同一份**
  （`Get-StartApps` 也是它）：经典程序快捷方式、**商店/UWP 应用**、系统工具（`.msc`）全在里面。枚举 =
  `SHParseDisplayName("shell:AppsFolder")` + `SHBindToObject(IShellFolder)` + `EnumObjects`，每条读
  `PKEY_AppUserModel_ID` / `PKEY_Link_TargetParsingPath` / `PKEY_Link_Arguments`（PKEY 的 GUID/PID
  **手写在 `apps.cpp`** 里，不用 `<propkey.h>`：那些符号在 MinGW 的 `libuuid` 里）。真机：159 条 →
  138 个程序（21 条被过滤），枚举 + 解析约 80 ms。
* **过滤两道**（判据都在 `core::app_list`，纯逻辑、`tst_app_list` 盯着）：`appTargetIsProgram()` 留
  `.exe`/`.bat`/`.cmd`/`.msc`/`.cpl`、shell 虚拟项（`::{…}`）与目标为空但有 `<包家族名>!<AppId>` 的
  商店应用，丢掉文档 / 帮助 / 网址（`.chm`/`.txt`/`.url`/`http(s)://`/`steam://`）；
  `appLooksLikeUninstaller()` 再丢掉**卸载程序**（名字含「卸载」或独立的 `uninstall`，或者目标文件名是
  `unins000.exe` 这类）。**不按名字猜"这算不算噪音"**：`Visual Studio Installer`、`Qt Maintenance
  Tool`、「【小狼毫】输入法设定」都留着。
* **名字就是 shell 给的显示名**（`SIGDN_NORMALDISPLAY`）—— 与开始菜单逐字一致，跟系统语言走（中文系统
  上是「文件资源管理器」「命令提示符」）。
* **启动走 `shell:AppsFolder\<AUMID>`**（`apps::launchApp()`）：先把解析名当 `lpFile` 交给
  `ShellExecuteExW`，失败再走 `SHParseDisplayName` + `SEE_MASK_IDLIST`（两条真机都验过）。参数、工作
  目录、`runas` 标记、商店应用的激活全部交给 shell。**代价是提权实例启动的程序也是提权的**（与 `run`
  同一个已知限制）—— 之后由 `apps::launchAppAsUser()`（必要时经普通权限的代理进程）修正，见第 35 条。
* **列表按需重扫**：缓存在动作线程上，最多 30 秒；`Runtime::start()` 还会排队扫一次，所以装完程序不用
  重启 flowkeyd。
* **网格是 6 列 × 6 行**（`AppListModel` 的 `columns()` / `cellWidth()` / `cellHeight()`），格子
  126 × 88、图标 40，**卡片 800 × 622**（宽度 = 两边内边距与缩进 + 6 × 126）；屏幕不够高时按高度自动收
  行（`setMaxRows` / `rowsForAvailableHeight`）；`↑`/`↓` 走一整行、`←`/`→` 走一格、`PgUp`/`PgDn` 翻页、
  `Home`/`End` 到头尾，到边界夹住不回绕。筛选是**名字的子串**（与窗口切换器的前缀匹配不同）。卡片尺寸
  是定死的：**宽度必须从"内边距 + 列数 × 格宽"算出来**，不要只改宽度不改格子。改这几组数字时用
  `tmp/preview/` 那套（真 `PopupHost` + 真 QML + 真开始菜单扫描）抓一张图看过：这次得到 1600 × 1244
  设备像素 = 800 × 622 逻辑像素 @200%，图标与两行名字都在格子里、没有被裁切。
* **筛选到只剩一个也不自动启动**（那是"切窗口"的语义；这里会真的拉起一个进程）。卡片开着时再按一次
  同一个快捷键 = 关掉它。**不切输入法**（与窗口切换器相反）：名字可能是中文。
* **图标必须异步取**：真机实测每个图标约 **3 ms**（首次冷启动那一个可达 148 ms，159 个条目连取一遍约
  400 ms）——同步做会让卡片等小半秒，而且 `QQuickImageProvider` 的同步回调跑在渲染线程上（那里没有 COM
  单元）。所以 `app::AppIconProvider` 是一条常驻 STA 线程 + `QQuickAsyncImageProvider`：模型给每一行一个
  `image://flowkeyd-app/<core::appIconKey(启动名)>`，图标取好就 `finished()`，用户看到图标一格格"长
  出来"。键是启动名的 64 位 FNV-1a（小写十六进制），所以 URL 里没有反斜杠/空格/中文，而且同一个程序
  永远是同一个 URL。
* **尺寸按屏幕缩放**：QML 的 `Image.sourceSize` 给设备像素（逻辑 40 × `Window.devicePixelRatio`；用
  `Window.devicePixelRatio` 而不是 `Screen.devicePixelRatio`，后者要额外 `import QtQuick.Window`，
  发布包会多一个 QML 模块）。
* 与 `app{...}` 区分：那个是**花括号**的注册构造器（窗口规则 + 快捷键），这个是**圆括号**的动作；简写
  `apps`（别名 `launcher`/`programs`）。

## 28. 程序启动器的右键菜单就是 Windows 自己的那一份

右键点一格 = 弹那个条目的**原生 shell 菜单**（已公开的 `IContextMenu`/`IContextMenu2`/`IContextMenu3`，
实现在 `src/platform/win/shell_menu.*`）：条目与资源管理器、开始菜单逐条一致（打开 / 以管理员身份运行 /
打开文件位置 / 固定到"开始"屏幕 / 属性 / 卸载……），而且**动作也由 shell 自己执行**。

* `QueryContextMenu` 用 `CMF_NORMAL | CMF_EXTENDEDVERBS`：带上"扩展动词"，所以「以管理员身份运行」
  直接就有（资源管理器里那是 Shift+右键才多出来的那批）。
* 菜单出在**光标处**（`GetCursorPos`），不从 QML 传坐标：Qt 的全局坐标是设备无关的**逻辑**像素，而
  `TrackPopupMenuEx` 要的是**物理**像素。
* **菜单就在本进程自己的 Per-Monitor V2 上下文里弹，不要去动 DPI 上下文**：win32k 对 PMv2 线程弹的菜单
  按显示器逐个缩放，这正是资源管理器那份的尺寸。曾经照微软「宿主 shell 菜单」的建议把线程临时降级成
  `UNAWARE_GDISCALED`，结果菜单**高一倍** —— 根因与实测数据见 `pitfalls.md`。
* 每次弹出都在 debug 日志里留一条 `shell menu: N item(s) for <启动名>`：验收脚本靠它把「菜单窗口有多
  高」换算成「每条多少逻辑像素」。**这条日志不要删。**
* 每一格的委托里挂一个 `TapHandler { acceptedButtons: Qt.RightButton }`（只吃右键，左键仍然是
  `ItemDelegate` 自己的 `clicked`），并且是在**松开**时（`onTapped`）弹菜单——按下就弹的话，那次右键的
  key-up 会把它自己关掉。
* **选中条目就把卡片收掉，取消（`Esc` / 点菜单外面）则留着**。收卡片发生在执行命令**之前**（平台层
  `showItemMenu()` 的 `beforeInvoke` 回调）—— 除了产品语义，还有一个实打实的理由：「属性」开出来的
  对话框以卡片为属主，卡片后收就会连对话框一起被藏掉。
* 这条规则本身在 `AppListModel::afterContextMenu(invoked)` 里（纯逻辑、`tst_app_list_model` 盯着），
  QML 只负责执行。预热用的假数据没有快捷方式，右键直接什么都不做。

## 29. 程序启动器的筛选认汉语拼音

输入串按「名字 / 全拼 / 首字母」三段做**子串**匹配（三段之间用 `U+001F` 隔开，段与段拼不出假匹配）。

* 三段由 `core::appSearchText(name)` 一次算好：`记事本` → `jishiben` + `jsb`；`Visual Studio Code` →
  全拼那段就是它自己 + `vsc`；中文与拉丁混排的 `QQ音乐` → `qqyinyue` + `qyy`；空格与标点留进全拼、
  不进首字母。
* **多音字的每一种读音都进搜索串**（`乐` → `le`/`yue`），读音组合数是每个字读音数的乘积，超过
  `core::kMaxSearchVariants`（8）就退回「只用主读音」。
* 表是**离线生成后提交进仓库**的（`src/core/pinyin_data.*`，生成器 `tools/pinyin_gen.ps1`）：数据来自
  mozillazg/pinyin-data（MIT，读音源自 Unihan 的 `kMandarin`），**只覆盖 U+4E00–U+9FFF**（20992 个码点
  里 20924 个有读音，多音字 5009 个、额外读音 6499 条）。Ext A/B 等生僻字没有拼音。**`ü` 记作 `v`，
  同时额外认写成 `u` 的那一条**（女 → `nv` 与 `nu`）。
* 因为模型要算这个串，**`flowkeyd_models` 现在链 `flowkeyd_core`**（两个都只依赖 QtCore，没有新增
  运行时依赖）。
* 验证：`tst_app_list`（含整张表的自检：读音形状 / 无重复 / 覆盖率）与 `tst_app_list_model`；
  `tmp/preview/` 那套在真开始菜单（138 个程序）上跑过一遍，挑「Directory Opus (启动)」打首字母 `doqd`
  能筛到它。
* **备选方案（没选）**：Windows 的中文排序确实把汉字按拼音排（`LCMapStringW` 的 sort key 主权重，实测
  乐 落在 `le`、长 落在 `zhang`、行 落在 `xing`），但它只给出「相对顺序」而不是音节边界 —— 要用它就得
  自己维护约 410 个锚字再做二分，而且权重顺序没有任何文档承诺。

## 30. 程序启动器分三段：已固定 / 最近使用 / 全部程序

* **概览**：从上到下是「已固定」网格、「最近使用」网格（最多两行 = 12 个，`core::kRecentLimit`）、最后
  一个「全部程序（N）」按钮。**还什么都没固定、也没启动过任何程序时**概览直接就是全部程序的网格 ——
  比让用户对着空卡片去点按钮友好。
* **`Space` 固定 / 取消固定**当前高亮的条目（固定住的图标右上角一个小圆点）。**筛选框非空时 `Space`
  放行**给标准 `TextInput`：否则用户打不出 `visual studio` 这种带空格的名字。
* **「全部程序」是同一张卡片里的另一种视图**（换的是 `ListView` 的行，不是第二个窗口）：一行一个程序、
  按**名字 / 拼音首字母分组**（`A`–`Z`；数字、符号、拼音表外的生僻字开头的一律 `#`）、带滚动条；
  `Esc`（或最上面那个「← 返回」按钮）回概览。分组键由 `core::appSortInfo()` 给出（`sortText` 带
  `0`/`1` 前缀，保证「#」那一组是**连续**的）。
* **持久状态在配置文件旁边的 `launcher.json`**（`core/launcher_state`，手写 JSON、容错解析、坏文件当
  空的）。条目的身份是**图标键**（`core::appIconKey`，与 `image://flowkeyd-app/` 同一个键）：目录重扫、
  改名字、路径大小写差别都认得出来。读写（`QFile`）在 `app::PopupHost` 里 —— **模型层不碰磁盘**，所以
  `tst_app_list_model` 是纯逻辑。启动一个程序时记一笔「最近使用」：`PopupHost::appChoose()` 在把
  `onChoose` 交出去**之前**记（那时 `m_appRequest` 还没被清空）。
* **模型是「行」式的**（`rowKind` ∈ `header`/`grid`/`button`/`list`），不是「格」式的：分段、分组、
  按钮在一张 `ListView` 里天然共存；键盘选中项只是（行, 列），`↑`/`↓` 自动**跳过表头**，
  `positionViewAtIndex(selectedRow)` 就是「把选中项带进视野」。卡片高度**恒定** = `maxRows ×
  cellHeight` + 上下占位（本机 800 × 622）：概览 / 筛选 / 「全部程序」列表三个视图一样高，**不跟内容
  缩** —— 卡片"过小"那次修复（按各行高度之和算出来的 248 高卡片太小）。
* 对外的接口没变：还是 `apps([title])`，固定与最近使用是**运行时状态**，配置 schema 里没有对应的字段。
* **滚动位置只在「换视图」时回到顶部**，不跟 `ListView.contentHeight` 走：那个属性在滚动中会因为
  「还没创建出来的委托按估算高度算」而抖几像素，跟着它把选中行摆进视野，就会把滚轮 / 拖滑块刚滚出来的
  位置立刻拽回第 0 行（现象是「有滚动条，但滚不动」）。
* **筛选之后的数字快速启动键**（easymotion 风格）：筛选串非空时，命中的前 10 条各分到一个数字，号码
  就是**显示序号**（第 1 个 `0`、第 2 个 `1`……第 10 个 `9`；与窗口切换器的 `1`..`9`、`0` **不同**），
  画在那一格**图标右上角**（`rowItems` 每一项的 `key` 字段，模型侧是 `m_itemKeys` + `numberedMode`），
  按一下直接启动它。只在筛选那一种扁平网格里生效。**没有对应条目的号码被吃掉但什么都不做**（漏给筛选框
  会把列表筛空）；筛选框为空时不编号，数字照常打进筛选框（`7-Zip` 这类名字要用数字筛）。号码出现时那格
  原来的「已固定」小圆点让到图标左上角。
* **最近使用程序的跳转键 = `Alt` + 数字**（按「最近使用」顺序，第 1 个 `Alt+1`……第 9 个 `Alt+9`、
  第 10 个 `Alt+0`，最多 10 个）。**已固定程序的固定快捷键 = `Alt` + 功能键**（第 1 个 `Alt+F1`、
  第 2 个 `Alt+F2`……最多 12 个，第 13 个起没有）。
  * **为什么是 `Alt`**：裸字母 / 数字是筛选框的主要输入；功能键本身单独按还有别的含义（`F1` 是帮助、
    `Alt+F4` 是关窗），所以都带上 `Alt` 并只在自己的低层钩子之外、**卡片开着时**才有效。
  * 模型侧：`m_recentKeys`（`m_recentShown` 顺序）与 `m_pinKeys`（`m_pinnedShown` 顺序），都在
    `rebuildRows()` 里重算；徽标文字由 `recentKeyLabelForLine()` / `pinKeyLabelForLine()` 给出
    （`Alt+数字` / `Alt+F数字`）。
  * 与筛选号码**不同**：它们不随筛选变化，卡片开着的任何视图里都有效。所以筛选之后它们能启动一个
    **根本没显示出来**的固定 / 最近项 —— `activateItem()` 因此不再要求条目在当前视图里。
  * `modifiers` 必须**恰好**等于 `Qt::AltModifier`：`Ctrl`/`Shift` 的组合、`AltGr`（`Ctrl+Alt`）都放行
    给 `TextInput`。靠 `handleKey(int key, int modifiers)` 的第二个参数（QML 传 `event.modifiers`）。
    已固定列表非空时，`Alt` + 没分配到的功能键也被吃掉；最近列表非空时同理吃掉没分配到的数字。
  * **筛选号码优先显示**：同一格既是固定 / 最近项、又在前 10 条结果里时画号码，但 `Alt` + 它的数字 /
    功能键照样有效。徽标上写的是完整按键（`Alt+3` / `Alt+F1`）。「全部程序」列表里固定在右端画的是同
    一个徽标（没有键时才是小圆点）。

## 31. 启动器的目录是三份来源拼的：开始菜单 + 控制面板 + 内置的系统设置页

三份都喂给同一个模型，**筛选 / 固定 / 最近使用 / 数字快速启动键 / `Alt` 键 / 右键菜单一视同仁**。

* **第一份还是 `shell:AppsFolder`**（第 27 条），一个字没改。
* **第二份：控制面板的「所有控制面板项」**（`platform/win/apps.cpp` 的 `listControlPanelItems()`）。
  真机 37 个候选 → 35 条：名字（`SIGDN_NORMALDISPLAY`）、图标、启动全交给 shell，所以跟系统语言走；
  **开始菜单里只有「设置」与「控制面板」两个总入口**，「网络连接」「电源选项」「程序和功能」
  「设备管理器」「声音」「鼠标」这些单项只在控制面板里。
  * 用 `SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder)` 拿文件夹 —— 真机验过它就是
    `::{26EE0668-A00A-44D7-9371-BEB064C98683}\0`（`Shell.Application.Namespace(3)` 看到的那个，36 条），
    **不是 `{26EE0668-…}` 本身**（那是 8 个分类的分类视图）。
  * 启动名 = `SIGDN_DESKTOPABSOLUTEPARSING`。**必须验证它能解析回去**（`SHParseDisplayName` 成功）才
    采用，否则退回 `SIGDN_FILESYSPATH`；两个都不行就丢掉这条。真机上有两条被丢掉：没显示名的那个
    （shell 把解析名当名字给回来）与**「字体」**（解析名里带的是本地化显示名）。这两个规则分别在
    `entryFromAbsolutePidl()` 与 `launchNameForPidl()` 里，**宁可少一条也不给一个按下去没反应的格子**。
  * 另外硬编码了一个解析名：**「网络连接」**（`::{7007ACC7-3202-11D1-AAD2-00805FC1270E}`）。它是
    delegate folder，不在「所有控制面板项」的枚举结果里，但要点名要它。
* **第三份：内置的 26 个 `ms-settings:` 页面**（`core/app_list.*` 的 `windowsSettingsPages()` /
  `builtinSettingsEntries()`）。这些页在 shell 里**既没有本地化名字也取不到图标**
  （`SIGDN_NORMALDISPLAY` 返回的就是 URI 本身），所以名字是内置的中英对照（`QLocale` 第一项是 `zh*`
  就用中文，否则英文）、图标借「设置」应用自己那一张（`core::appIconLaunchName()`：`ms-settings:` 前缀
  → 设置应用的 AUMID，**键仍然是 URI** —— 每个页面各一个身份）。名字带 `设置：` / `Settings: ` 前缀，
  所以在「全部程序」里聚在 `S` 那一组，输入「设置」/`shezhi`/`sz` 能一次全筛出来。
* 合并只在 `Dispatcher::refreshAppCatalog()` 里做（不在平台层）：三份接起来时**同名条目丢掉后面的**
  （`core::appendLauncherEntries()`，开始菜单那份在前）。真机上只撞一条（「Windows 工具」），结果
  138 + 35 + 26 → **198 条**。日志行也扩成了「三份各多少 → 合计多少」。
* 验收：`scripts/acceptance.ps1` 的启动器那一段加了 3 条（用拼音首字母 `dyxx` / `sz` 筛，纯 ASCII
  注入得了）。

## 32. 没声明固定桌面的程序：`window("activate")` 把窗口搬到当前桌面，而不是把视图切走

不这样做，Windows 会把视图切到那个窗口上一次所在的那张桌面上。

* 判据是「**命中这个窗口的第一条 `window_rule` 有没有 `desktop`**」（规则按书写顺序匹配，先写的赢）：
  `core::windowRulePinsDesktop()` / `core::firstWindowRule()` 是纯逻辑，`tst_placement` 盯着。
  * 写了 `desktop` —— 用户按快捷键就是想过去，保持老行为：`raiseWindow` 切到它所在的那张桌面并激活它
    （视图跟着走）。
  * 没写（或者压根没有规则）—— 窗口跟着用户走：`win::desktop::moveWindowToCurrentDesktop()` 先把它搬到
    **当前**桌面，再激活；视图不动。
* 只对 `op = "activate"` 生效；`launch` 回退那条路径也算（新窗口找到之后同样处理）。其余 op 行为一个字
  没改。
* **搬必须在激活之前、而且必须等它搬完**：虚拟桌面的移动是异步的，立刻激活的话 `window::raiseWindow`
  会读到旧的桌面 GUID、把视图切过去 —— 那正是要避免的事。所以 `moveWindowToCurrentDesktop()` 内部等
  桌面 GUID 真的变了再返回（`waitForDesktopChange()`）。
* "要不要搬"先问一句**便宜**的 `isWindowOnCurrentDesktop()`（公开的 `IVirtualDesktopManager`，不碰版本
  表那套未公开接口）：已经在当前桌面上就什么都不做。问不出来（锁屏 / 非交互会话 / 窗口不属于任何桌面）
  时**不动窗口**，交给 `raiseWindow` 的老行为处理。
* 日志里会多一段 `(brought to desktop 2/4)`（`window` 动作那条 INFO 行）。
* `all_desktops = true` **不算**"声明了固定桌面"：窗口本来就到处都是。

## 33. 没写 `monitor` 的窗口摆到哪块显示器上：`settings.window_monitor`，默认 = 光标所在的那一块

唤起一个**没配 monitor** 的程序时，让它出现在鼠标所在的那块屏上；这个行为可配，能改成"出现在之前
active 后所在的 monitor 上"。

* `settings.window_monitor` 取值与 `monitor` 完全一样（`2` / `"primary"` / 设备名 / `"cursor"` /
  `"last"` / `"none"`），默认 `"cursor"`。`MonitorRef` 因此多了 `Cursor` / `Last` / `None` 三个 `Kind`
  （`core/monitor_ref.{h,cpp}`，从 `config.h` 里拆出来是因为 `action.h` 也要用它）。
  `monitorRefIsConcrete()`（序号 / 主显示器 / 设备名）与 `monitorRefIsDynamic()`（光标 / 上次那块）是
  纯逻辑，`selectMonitor()` 对这三种返回 `nullopt`。
* `"last"` 是**flowkeyd 自己记住的**：`<配置目录>/windows.json`，键是**可执行文件名的小写**（与
  `process` 同一套），值是**设备名**（序号会随排列变）。只有真的用到 `"last"` 时才读/写这个文件。第一次
  还没有记忆时**不动窗口、只把落点记下来**。纯逻辑在 `core/window_monitor_state.*`
  （`tst_window_monitor_state` 盯着），落盘在 `app::Dispatcher` 的 `MonitorMemory` 上。
* **优先级**（`core::windowActionMonitor()`，纯逻辑、`tst_placement` 盯着）：`window` 动作自己的
  `monitor` → 命中这个窗口的第一条 `window_rule` 的 `monitor` → `settings.window_monitor`。
  `monitor = "none"` 逐层都能"显式关掉"。
* **生效时机**（所有 `window_rule` 匹配到的窗口）：`window_rule` 的三个时机与 `window` 动作的
  `op = "activate"`（含 `launch` 回退那条路）。于是**双击图标开出来的窗口**也会被搬到光标那块屏 ——
  写 `monitor = "none"`（或全局 `window_monitor = "none"`）关掉。
* **几何怎么处理**（"保持大小并居中"，与 `move_left_monitor` / `move_right_monitor` 一致）：目标是动态
  的而且规则里没写 `maximize` / `x` / `y` / `width` / `height` 时，只"把它放对屏"；已经在目标那一块上
  就**不去碰它**。写死的三种仍然照旧（写 `monitor` 又没写位置大小时默认最大化）。为此
  `WindowRule::maximize` 的默认值只对 `monitorRefIsConcrete()` 为真时才为 true。
* 平台层只多两个函数：`win::monitor::indexForCursor()`（`GetCursorPos` + `MonitorFromPoint`）与
  `win::monitor::moveToMonitor()`（`moveToAdjacentMonitor` 重构后调的其实是它）。
* `--list` 多打一行 `window monitor: … (for rules and window actions that do not name one)`。
* 一个副带效果：以前 `window_rule{ process = "x" }`（什么都没写）会被 `--check` 以「only matches windows
  and does nothing」丢掉；现在它有意义了（摆到缺省显示器），只有在 `monitor` 写了 `"none"` 或全局也是
  `"none"` 时才会再报那条 warning。

## 34. 图形化配置界面：托盘菜单的「配置...」打开一个普通窗口，不用自己写 Lua 也能改配置

四个方向都是当场拍板的（依次：保存怎么落地、覆盖到哪一层、什么窗口形态、按键怎么录入）。

* **形态**：Windows 11「设置」那种**可缩放普通窗口**（不是那五个无边框小卡片）：左边导航（常规 / 快捷键
  / 按键重映射 / 窗口规则 / 程序 / 高级），右边分组卡片，底部是状态行 + 还原 / 保存 / 关闭。**不加
  `Qt.Tool`**：它就该进任务栏、能最大化。
* **保存 = 整体重写 `config.lua`**（而不是双文件）：界面的单一真源是 `core::configToMap()` 那份字典，
  保存时由 `core::configToLua()` 写成一份字段与文档逐字对应的 Lua。代价是界面之外的内容（注释、循环、
  变量）会没——所以**写之前总留一份 `config.lua.bak`**，而覆盖一份**不是**界面生成的文件之前先问一句。
  生成的 Lua 第一行是 `-- flowkeyd-config-editor:generated`（`isEditorGenerated()` 看的就是它）；把这行
  删掉，界面下一次就又会问。字段含义与取值一律不变，所以配置 schema 这个公开接口**没动**。
* **覆盖全量**：`settings` 的全部字段 + 四个条目（`hotkey` / `remap` / `window_rule` / `app`）的全部
  字段 + 全部 20 种动作。界面里不存在的写法只有一种：**动作简写字符串**（`send("^{c}")`）与动作列表在
  界面上都归一到「一串动作」，加载时简写就被展开成等价的动作表（行为一模一样，只是写回文件时不再是
  简写）。
* **保存前一定先自己读一遍**：`ConfigEditor::save()` 把拼好的文本交给**守护进程用的同一个** Lua 求值器
  + `core::compile()`，不过就根本不落盘（所以界面不可能写出非法配置）；报错情况下文件一个字节都不动。
* **条目编辑走弹层**（`HotkeyOverlay` 等，盖在窗口上的自建遮罩）：不是 `Dialog`（那要跟 FluentWinUI3
  的样式对齐，而且里面要放一整套表单）。弹层**每次新建**，于是每个输入框都从当前值初始化一次；「新增」
  的流程是「点新增 → 弹层里填 → 确定才追加」（列表里永远不会出现半成品）。
* **和弦用文本录入 + 即时校验**（不做「按一下录入」）：录制要在录制期间把引擎挂起，那几秒里全局快捷键
  集体失效。校验向 `ConfigEditor::chordError()` 要结果，用的是与 `--check` 同一个 `core::parseChord()`；
  编辑已有条目时把「它自己那几个和弦」当作例外传进去，否则会一直报「已经被用了」。
* **中文显示、英文存储**：下拉框里的取值一律经 `ConfigText`（QML 单例）翻成中文，写进配置的还是
  `"release"` / `"move_next_desktop"` 这种公开取值。三态（`跟随默认` / `是` / `否`）用 `TriRow`：配置里
  有一批**可选**布尔量，一个开关表达不出「没写过」。
* **保存成功就重载**：`ConfigEditor::saved` → `ConfigWindow::reloadRequested()` →
  `Runtime::reloadFromAnyThread()`。界面**自己不碰钩子线程**，也不弹一个「要重载吗」。

## 35. 启动的程序默认是「当前登录用户的普通权限」，`elevated = true` 才提权

* 理由：守护进程默认提权运行，继承令牌会让**每一条**快捷键启动的东西都变成管理员进程 —— 拖放失效、
  配置写到奇怪的地方、有些程序直接拒绝启动。
* 配置面：`run` 的 `elevated`、`launch` 的 `elevated`（`window` 的 `launch` 与 `app{}` 的 `launch`
  共用同一份字段块，所以也进 `core::LaunchFields` 做逐字段合并）、`--list` 摘要里只给显式的提权项的尾巴
  加 `(elevated)`。只有 `run` / `launch` 有这一项：`open` 没有参数（它总是普通权限），程序启动器的例外
  走右键菜单自带的「以管理员身份运行」。
* **怎么做到**（`platform/win/process.cpp`）：从登录 shell（`GetShellWindow()` 的属主进程，兜底 = 本
  会话第一个 `explorer.exe`）拿一份主令牌，`CreateProcessWithTokenW` 启动 → 子进程落在 Medium；
  `elevated = true` 而自己没提权时用 `ShellExecuteW("runas")`（会弹 UAC，而且**没法转交 `env`**，写了
  `env` 只给一条 warning）。拿不到令牌 / Secondary Logon 被禁用时**退回继承当前令牌**并写 warning
  （"按键按下去什么都不发生"比"这次提权了"更糟）。
* **`ShellExecuteExW` 没有令牌参数**，所以 `open` 与程序启动器（`apps::launchAppAsUser()`）把请求交给
  一个**普通权限的代理进程**：flowkeyd 自己的隐藏副本，内部命令 `--shell-open` / `--shell-app`
  （+ `--shell-args`/`--shell-cwd`/`--shell-show`/`--shell-result`，新的
  `platform/win/ffi::currentExecutablePath()`）。代理进程在 `main` 的离线命令那一类里：**不提权、不读
  配置、不装钩子、不碰单实例锁**，结果写到 `%TEMP%` 的一个小文件里（失败时里面是错误文本）。代价是每次
  多起一个隐藏进程（实测总体 ~120 ms）。没提权时直接在本进程里调 shell。
* **右键菜单不在这一层**：`shell_menu` 的命令由 shell 在 flowkeyd 自己的进程里执行，所以从那里选「打开」
  开出来的程序会提权。改成普通权限要把菜单宿主（含 `#32768` 消息转发）整个搬到代理进程，风险与代价都
  不划算。
* 验证：`tst_interactive::launchesProgramsWithTheRequestedPrivilege`（读**目标进程令牌**的完整性 SID：
  默认必须是 `S-1-16-8192`；本进程已提权时再验 `elevated = true` 是 `S-1-16-12288`）、
  `opensTargetsAsTheLoggedOnUser`（`open` 与 `app` 两种模式各跑一遍）、
  `reportsALauncherFailureFromTheShellHelper`；断言用**中文 SID** 而不是 `whoami` 的本地化文本，而且
  **不写 `whoami` 那种一次性文件**以外的任何东西。

## 36. 「工作空间」（按显示器独立的一套虚拟空间）试做后整体移除，系统回到 Windows 虚拟桌面

**结论：不做。** 2026-10 完整实现过一版（`settings.workspace`、`workspace(...)` 动作的
switch/move/prev/next/move_prev/move_next、`window_rule.workspace`、切空间时的冻结快照遮罩
`switch_effect`、恢复日志 `workspace.json`、`--restore-windows`、托盘徽标里的工作空间号），
实机用下来**可用性不达标**：

* 被 `ShowWindow(SW_HIDE)` 藏起来的窗口会**同时**从任务栏与 `Alt+Tab` 消失（这是写死的默认，
  没有开关），于是“这个程序还在吗”完全靠记性；
* 浏览器 / Electron 类程序被 `SW_HIDE` 过之后，揭示那一瞬会先给一帧空白的页面区（实测
  35–48 ms 白帧）；用冻结快照遮罩（`freeze`）压住又变成每次切换多 ~100 ms 延迟，还要多一张全屏
  置顶窗口；
* 归属规则（`new_window` / `window_rule.workspace`）与“窗口搬屏后归哪个空间”的组合语义很难
  预期，出问题时的表现是“窗口不见了”。

**取舍怎么调都不舒服，所以整条路放弃。** 实现完整保留在分支 `backup/workspace`（末提交
`42c4351`）；master 上全部删除，本机配置的绑定改回 Windows 虚拟桌面
（`desktop(i)` / `window("move_prev_desktop"|"move_next_desktop", { follow = true })`）。

* **不要再悄悄把它加回来**：这是明确的“试过、放弃”的决策。真要做新方案（例如换一种隐藏机制）
  要先问项目所有者。
* 删除范围：`core/workspace*`、`core/workspace_event*`、`core/workspace_log*`、
  `core/switch_cover*`、`platform/win/workspace*`、`platform/win/freeze*`、
  `platform/win/session*`（`SessionEndWatcher` 只为恢复日志存在）、`settings.workspace`、
  `settings.tray_badge`、`workspace` 动作、`window_rule.workspace`、`switch_effect`、
  `--restore-windows`、托盘「恢复全部窗口」，以及 `tst_workspace*` / `tst_switch_cover`。
  动作种类回到 **20 种**；托盘图标回到只显示**虚拟桌面号**（`core::desktopBadge*`）。
* **保留下来的一条独立成果**：`TopLevelWindowFacts.child`（`WS_CHILD` 的子窗口一律不是“主窗口”）。
  那轮排查发现 `SetWinEventHook` 连子窗口一起报、Chrome 的 `Chrome_RenderWidgetHostHWND`
  （标题 `Chrome Legacy Window`）会被当成程序窗口收进 `window()` 匹配与切换器里。
  修法与工作空间无关，继续有效；细节见 `pitfalls.md`。

## 37. 切虚拟桌面时把前台窗口带过去：`settings.desktop_follow_foreground`，默认开

双屏 + 虚拟桌面时的一个具体痛点：你看着左边那块屏（鼠标也停在那儿）上的编辑器，用快捷键切到
桌面 2；编辑器跟着视图走了，而**右边那块屏**上的浏览器被留在了桌面 1，凭空消失。

所以 `desktop()` 动作切桌面时多一步：如果**前台窗口不在光标所在的那块显示器上**，就把它一起搬到
目标桌面上。**位置与显示器都不变**，只有虚拟桌面这一维跟着走。

* 开关是 `settings.desktop_follow_foreground`，**默认 true**（用户 2026-10 拍板：默认就要这个行为，
  不配也生效）。写成 `false` 就回到以前的行为。
* 判据是纯逻辑 `core::shouldCarryWindowToDesktop(settings, windowMonitor, cursorMonitor)`
  （`tst_placement` 盯着）：开关打开 **且** 两个序号都有值 **且** 不相等。任一边是 `nullopt`
  （没有前台窗口 / 询不到光标）都**不搬** —— 未知就别动手。
* **只接 `desktop()` 动作**，不做切桌面的全局监听：用户明确选了「只有 flowkeyd 的 desktop 动作」
  （不接那 500 ms 的桌面轮询），所以系统自己的 `Win+Ctrl+←/→`、任务视图点选都不触发。
  这样既没有延迟，也不会在用户手动切桌面时擅自搬窗口。
* 平台层是 `win::desktop::switchToCarrying(index, hwnd, …)`：一个 STA 会话里先 `MoveViewToDesktop`、
  等 GUID 真的变了、再 `SwitchDesktop`；`Session::goToCarryingIndex()` 是共用的实现体。
  `hwnd` 为空 / 已失效 / 本来就在目标桌面时行为退化成普通的 `switchTo()`。
* **几何校对**：`MoveViewToDesktop` 在真机上会顺带把最大化的窗口从最大化里踢出来。所以切之前拍一份
  `CarryGeometry`（还原矩形 + 最大化 + 最小化），切完**比一比**，一模一样就什么都不做，不一致才
  `applyPlacement()` 摆回原样。不能无脑摆一遍 —— 那会把用户自己摆好的位置重新居中。
* 前台窗口用新增的 `win::window::foregroundMainWindow()` 取（与 `window()` 的空查询同一条逻辑：
  PowerToys 的「快捷键指南」那种 `WS_EX_TOOLWINDOW` 覆盖层会沿 Z 序跳过）。
* **不记状态**：不记“它原本属于哪张桌面”，以后每次切桌面都重新判断同一个条件。想让某个窗口永久钉在
  自己那张桌面上，仍然用 `window_rule{ desktop = n }`。
* `--list` 多打一行 `desktop follow foreground: true (…)`；图形界面的「高级」页有一张「切换虚拟
  桌面」卡片。
* **没搬时日志写原因**（INFO 级：`desktop 2/2 (no window carried: the foreground window … is
  already on the cursor's monitor (2))`）。起初只有 `--log-level debug` 才看得到 `not carrying`，
  用户对着默认日志分不清“没生效”与“条件不满足”：**真机验证时两次按键都因为前台窗口本来就与
  鼠标同屏而没搬，看起来就像新功能没装**。