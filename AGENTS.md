# AGENTS.md

给 AI agent（以及人类）的 **flowkeyd** 开发笔记。**先读这个文件。**

> **本文件是工程规则与索引**（每次会话全量进上下文，所以只放「规则、不变量、决策摘要、索引」）。
> 细节按需读：
>
> | 文档 | 内容 | 什么时候读 |
> | ---- | ---- | ---------- |
> | `docs/notes/decisions.md` | 每条决策的**完整内容与理由**（与第 2 节同编号） | 要改某项行为、想知道为什么 |
> | `docs/notes/pitfalls.md` | 已踩过的坑（现象 → 原因 → 做法） | 碰怪现象、动 COM/vtable/QML/DPI 之前 |
> | `docs/notes/extension.md` | 各类改动该动哪几个文件、怎么验证 | 加动作 / 字段 / 弹窗 / 平台后端之前 |
> | `docs/notes/reference.md` | schema 逐字段速查、验收脚本覆盖、手工冒烟清单 | 改配置面、跑验收、发布前 |
> | `README.md` | **唯一的用户文档**；配置 schema 的权威定义 | 面向用户的说法 |
> | `flowkeyd.lua.example` | 覆盖全特性的参考配置（`--check` 零警告） | 写示例 |
>
> 任何不一致以 `README.md` 为准，并同时修本文件。

## 工作约定（项目所有者设定）

1. 每个任务结束后更新本文件（规则/决策摘要变了写这里，新经验补进 `docs/notes/` 对应文件）。
2. **每个任务结束后构建 debug 与 release 两个 profile**（两条优化路径的警告都要挡住）；**单元测试只在
   debug 上构建和运行**（`ctest --test-dir build/windows-debug`）；`scripts/acceptance.ps1` 只用 release
   产物。**release 构建 + debug 测试全绿才算完成**，纯文档任务同样适用。分工固定：**debug = 开发**（构建
   全部测试目标）、**release = 发布**（`FLOWKEYD_BUILD_TESTS=OFF`）+ 自动产出干净的
   `build/dist-release/`。里面出现测试产物或构建系统文件（`*.a`、`CMakeCache.txt`、`Temp`）就算 bug。
3. 目标平台是 **Windows**；可以使用未公开的 Win32 API。
4. **代码注释、本文件、`docs/notes/`、README、示例配置一律用中文；日志与错误信息保持英文**（配置校验、
   `--check` 输出也一样 —— 它们是机器可断言的字符串）。用户可见的 CLI 帮助文本用中文。
5. 可以自行结束本项目正在运行的进程，不必事先征求许可。停法：`taskkill /PID <pid>`（**不带** `/F`）让它
   走干净退出；`/F` 只在杀不掉时兜底（代价是幽灵托盘图标）。这条只管 flowkeyd 自己。
6. 需要用户暂时别用这台机器时（抢前台/焦点、注入按键的冒烟验证），用提问形式提醒但**不等回答**，约 3 秒
   后自行继续；提醒里写清预计时长与影响范围。
7. **依赖按需引入，每次引入都要在提交信息里给出理由。** 默认只有 **Qt + `vendor/lua`**。
8. **agent 的 bash 是极简 WSL，`/bin` 下只有 `bash`/`mount`/`login`。** 看文件用 read/edit 工具；跑命令
   一律走 `powershell.exe -NoProfile -Command "…"` 或绝对路径的外部程序（如
   `C:\Users\xingjian\scoop\apps\git\current\cmd\git.exe`）。**不要在命令行上直接拼中文**（代码页会变
   乱码）。给 `powershell.exe -File` 做重定向时**不要写 `*>`**（bash 会拆成 `*` + `>`，还会在仓库根建出
   名字带反斜杠的垃圾文件）；用 `/mnt/d/…` 路径 + 单个 `>`。⚠️ 带 `$变量` 的命令要用**单引号**包住
   `-Command` 参数，否则被 bash 吞掉（踩过）。
9. **接管已完成（2026-09）：现在常驻的就是 flowkeyd**，不要再去拉起别的实现。常驻由计划任务 `flowkeyd`
   （登录时 + 最高权限 + 15 s 延迟）拉起，任务指向**当前正在运行的 exe**（守护进程每次启动自检并重新
   注册）。停止用 `--quit`（不要 `taskkill /F`），删除自启用 `--remove-autostart`（先 `--quit`）。
   `scripts\install.ps1` / `uninstall.ps1` **已删除** —— 不要写部署脚本，也不要把常驻指向某个预设目录。
   * **例外**：仓库根的 `install.ps1` 是对外一键安装器（下载完整包 + sha256 + 解压到
     `%LOCALAPPDATA%\Programs\flowkeyd`），不参与自启任务，README 最上面那条 `irm … | iex` 用的就是它。
     **必须纯 ASCII、无 UTF-8 BOM**（`irm` 会把 BOM 留成 `U+FEFF`，`iex` 随即在 `param()` 上报
     `ParserError`）；带参数跑用 `curl -o` 落盘 + `powershell -File`。
   * **本机 agent 的 `powershell.exe` 是不是提权的会变，每次都自己测**（`IsInRole(Administrator)`）：提权
     时 `Start-Process`；**不提权时千万不要**（守护进程自己会 `ShellExecuteW("runas")`，没人点 UAC 会卡
     死在里面）→ 用 `& build\dist-release\flowkeyd.exe --quit` 然后 `schtasks /Run /TN flowkeyd`。先停再
     拉起很重要：不先停，新实例会弹一个「已在运行」原生框卡住。**不要用 `taskkill /IM`** 停常驻（不带
     `/F` 时它发的是 `WM_CLOSE`，而 `--quit` 事件那条路才算干净退出）。
   * 收尾脚本里 `--quit` / `schtasks /Run` 那两行**不要紧跟中文注释**：`tmp/` 的一次性 `.ps1` 无 BOM，
     PS 5.1 按 GBK 解码时中文注释会**静默吞掉下一行**（现象是「`--quit` 什么都不打印、常驻还在跑」）。
     **`tmp/` 的脚本一律纯 ASCII。**
   * 开发期起 flowkeyd 一律 `--no-elevate --allow-multi` + 一次性配置，并且**不要**占用用户真实配置里已有
     的和弦（看那份配置文件本身或 `flowkeyd --list`；真实配置不进仓库，见第 12 条）。
10. **任何自动化测试都不得触发真实的系统电源动作。** `shutdown`/`restart`/`logoff`/`sleep`/`hibernate`/
    `lock`/`screen_off` 一个都不许真执行；测试代码里不出现 `platform::win::power::execute()`（
    `tst_power_table` 只测纯逻辑表）。这些动作只有用户自己按键、或点选单条目时才允许发生。
11. **每个任务收尾时 release 产物必须是最新的**（`cmake --build --preset release` 会自动把干净发布包写到
    `build/dist-release/`）。常驻实例会锁住它自己那个 exe，所以收尾按这个顺序：

    ```powershell
    & build\dist-release\flowkeyd.exe --quit
    & 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
    Start-Process build\dist-release\flowkeyd.exe   # 不提权时改用 schtasks /Run /TN flowkeyd
    ```

    * 只有当常驻确实从 `build/dist-release` 跑时才需要先 `--quit`；它跑在用户自己拷走的路径时不要动，
      只要保证 `build/dist-release` 是最新的。纯文档任务通常 `ninja: no work to do`。
    * 不想动二进制就别在提交之后再构建（提交会改 ref、触发重新配置 + 重新链接）。
    * **刚发过版、之后只改了文档 / 脚本的提交，不要重新构建**：HEAD 已离开已发布的修订，再构建出来的
      `dist-release` 就不是发布页上那一份了。等下次真要发版时由 `scripts\release.ps1` 一起构建。
12. **不要把某台机器上真实使用的配置文件写进仓库**：只放产品文档与参考配置。真实配置的绑定清单、
    `--check` 计数、路径，以及“本机现在跑的是哪一个 exe”都不记录。**改真实配置不需要更新 README 或本文件。**
13. **可配置的东西必须三处同时到位：`README.md`（权威定义，必要时连 `flowkeyd.lua.example`）、
    `src/core/config_map.cpp` 的 `*ToMap`/`*FromMap`、以及 `src/qml/` 里对应的界面**（页 / 弹层 /
    `ActionEditor` 的 `visible: root.type === "…"` 分支 + `ConfigText.qml` 的中文名）。新增/修改/删除任何
    字段或取值（含默认值与范围）都要三处一起改；`settings`/`hotkey`/`remap`/`window_rule`/`app`/
    `launch`/选单条目与全部 21 种动作一视同仁。
    * **界面那一侧漏接不报错**：字段被默默丢掉、或用户在界面里根本改不了它。`tst_config_writer` /
      `tst_config_editor` 的往返测试只盯住中间那一处；第 1、3 处没有自动检查，只能靠这份清单人肉过一遍。
    * 查缺口**不能只 grep 字段名**（`name`/`enabled` 这种名字在别处也出现）：要按「每一节的字段表」逐行
      对照 `src/lua/lua_config.cpp` 的 `checkFields`/`convertAction` 与界面（`config_map.cpp` + QML）。
14. **术语三个词分清楚（别混）**：
    * 「**虚拟桌面**」= Windows 自己的那个（代码 `desktop` / `IVirtualDesktop*`、配置字段
      `desktop`/`all_desktops`）；“切过去 / 搬过去 / 钉在所有桌面”说的都是它。**不要用 workspace
      指它**（早先那种写法已清理干净）。
    * 「**工作区**」= 一块显示器扣掉任务栏后的可用区域（`availableGeometry()` / `workArea`），
      不要拿来指虚拟桌面。
    * 「**工作空间**」= 决策 36 那个新功能（Hyprland 风格、每块显示器独立的一套空间；配置字段
      `workspace`、动作 `workspace("switch"|"move", N)`、`settings.workspace`）。它**不是**虚拟
      桌面（两者互不干扰），也不是工作区。
    * 「远程桌面 / 交互式桌面 / 桌面会话」是第四个概念，照旧区分。

---

## 1. 这个项目是什么

`flowkeyd` 是一个由 **Lua 脚本**配置的 Windows 键盘钩子守护进程（**C++20 + Qt 6**）。它装一个
`WH_KEYBOARD_LL` 钩子，匹配和弦，按需把匹配到的按键从前台应用那里隐藏掉（AutoHotkey 的行为），然后执行
绑定的动作（启动进程、发送按键、音量/媒体、窗口、虚拟桌面、剪贴板、通知、选单、帮助、窗口切换器、
程序启动器、电源）。也能做按键重映射。一句话：**用 Lua 配置、用 Qt/C++ 写的 AutoHotkey。**

UI 只有托盘图标 + 日志窗口、`menu`/`help`/`windows`/`apps`/`update` 五张 QML 卡片与图形化配置窗口，
全部 Qt Quick + FluentWinUI3，跑在**同一个进程**里。**非目标**：鼠标钩子、把 Lua 函数当动作。

| 方面 | 值 |
| ---- | -- |
| Lua | **5.5.1**（`vendor/lua`，静态编进二进制）；DSL 全局表 **`flowkeyd.*`** |
| 配置/日志 | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log` |
| 注入标记 / 互斥体 | `dwExtraInfo` 里的 `"FLOW"` / `Local\flowkeyd-<配置路径散列>` |
| 开机自启 / 在线更新 | 计划任务 `flowkeyd` → **当前运行 exe** / 托盘菜单手动查 GitHub `releases/latest` |
| 自动化测试 | Qt Test + `scripts/acceptance.ps1`（192 项，实跑 189；需交互式桌面） |

---

## 2. 已拍板的决策（不要再重新讨论）

**每一条都是项目所有者明确选择的：不确定要不要改就先问用户，不要自作主张地“改进”。**
下面是「动手前必须知道的那一条」；完整内容与理由见 `docs/notes/decisions.md` 的**同编号**。

1. **应用形态：CLI 守护进程 + QML 弹窗，没有主窗口**；命令行参数是唯一的控制面。
2. **Lua 用 `vendor/lua` 里现成的 5.5.1**，不换 5.4（差异见第 8 节）。
3. **分阶段实施**（第 9 节）；单测为主，桌面行为由验收脚本覆盖。
4. **配置 schema 与 CLI 开关是稳定的公开接口**：字段名、取值、动作字段、DSL 构造器名都要写进
   `README.md`，改动当**破坏性变更**对待。脚本全局表名是 **`flowkeyd`**。
5. **flowkeyd 最终接管这台机器**（已完成）。
6. **不做 `--simulate`/`--selftest`/`--probe`**；`--check`/`--list`/`--list-keys` 保留。验收用**只给测试的
   后门** `FLOWKEYD_ACCEPT_INJECTED=1` 抬高“丢弃注入输入”那道过滤（**不变量 2 没动**）。
7. **依赖政策：默认只有 Qt + `vendor/lua`**，新依赖在提交信息里给理由；JSON/CLI 解析/字符串工具自己写
   或用 Qt，不引 `sol2`/`nlohmann::json`/`CLI11`/`spdlog`。
8. **双 profile 都要绿**：debug 构建单测、release 只出 `dist-release`；单测只在 debug 跑，验收只跑
   release 的 exe。
9. **弹窗用 Qt 标准控件、不自绘**：`ListView` + `ItemDelegate` + 真 `TextField`；给 QML 的方法一律
   `Q_INVOKABLE`（否则 TypeError 且**处理器后续语句被静默跳过**）；左键点一行 = 选中 + 复制按键、
   `Enter`/双击 = **先关窗再执行**；`quit`/`suspend`/`power` 要按两次（`core::isDestructive()`）、
   `menu` 不算危险；滚轮只滚视图；**中文字体一律 `font.family: "Microsoft YaHei"`**。
10. **开机自启 = 计划任务指向当前运行的 exe**（登录 + 最高权限 + 15 s 延迟，`schtasks /Create /XML`，
    每次启动自检刷新）。开发实例的 `--no-elevate`/`--allow-multi`/未提权/`--no-autostart` 都**不碰**任务
    （否则会劫持用户自启到会被删的目录）。注册/刷新前先问用户（`--no-prompt` 跳过）。**不写部署脚本。**
11. **启动时两个原生确认框**（`QApplication` 之前）：已在运行（在**提权之前**判，退出码 1）、自启要不要
    更新。`--no-prompt` 跳过；配置错误用 `QMessageBox`（英文正文）。
12. **版本号 = `yy-MM-dd-<git 短修订>`**：日期取 exe 自己的 mtime（**不用编译期常量**，否则每次重链接）、
    修订是 CMake 写进 `generated/flowkeyd_revision.h` 的常量。**先提交再构建**，收尾时显式 configure 一次
    或核对 `--version` 的哈希。
13. **`window_rule{...}`** 控制「程序出现在哪个虚拟桌面 / 显示器」，显示器重接入时重新归位。字段见
    `decisions.md` 第 13 条。要点：`monitor` 只认序号（1 起）/`"primary"`/设备名；写了 `monitor` 又没写
    位置大小 = **默认最大化**；只写 `desktop` 不动几何；`all_desktops` 与 `desktop` 互斥、走
    `IVirtualDesktopPinnedApps`；触发时机只有三个（窗口首次出现 / 显示器重接入 / 启动时）；“主窗口”判据
    = 可见 + 无属主 + 非 `WS_EX_TOOLWINDOW` + 有标题 + 尺寸非零。
14. **跨虚拟桌面的唤起与跟随**：`activate` 在目标不在当前桌面时**切过去并激活**（只有“在当前桌面且真在
    前台”才是 `toggle` 收起）；`window_rule` 真搬了桌面时视图跟着走（只在“窗口首次出现”那一遍）。
15. **`app{...}`** 把同一程序的窗口规则与快捷键写在一起，`compile()` 展开成普通定义（排在全局
    `hotkey{}`/`window_rule{}` 之后）。`launch` 是 `window` 动作 `launch` 的**默认值 + 逐字段合并**；
    `hotkeys` 里的 `window` 动作自动继承 `process`/`title`/`launch`（嵌套在 `menu` 里的也算）。错误标签
    形如 ``app #1 (`wps`)``。
16. **`window` 四个「挪窗口」op**：`move_prev`/`move_next_desktop`（只动桌面、视图不跟、首尾相接）、
    `move_left`/`move_right_monitor`（只动显示器、保留最大化、普通窗口保持大小居中、不循环）。都不套
    `toggle`、不接受 `launch`；`animate` 写在不产生过渡的 op 上会报错。
17. **字母键名一律小写**（`a` 就是 A 键，要大写键写 `Shift+a`），大写单个字母会被 `--check` 拒绝。
    **例外**：发送脚本里的裸字符保持 AutoHotkey 语义（`send("A")` 打大写 A、花括号里是键名）。
18. **`window` 的 `follow`**（只对两个跨桌面 op 有效，默认 `false`）：搬完把视图也切过去并重新激活，两步在
    **同一个 STA 会话**里做完；不产生窗口过渡；保持激活是尽力而为（失败只记 warning）。
19. **前台是覆盖层时跳过它**：不写 `target`/`process` 的 `window` 动作沿 Z 序往下找第一个“主窗口”（否则会
    去操控 PowerToys 的「快捷键指南」）。`windowDesktopId()` 可重试；`moveWindowToAdjacent()` 搬完**等
    桌面 GUID 真的变了再返回**。
20. **托盘图标主体 = 当前的工作空间号 / 虚拟桌面号**：默认是**两个号各占一个角** —— 左上工作空间号
    （绿底）、右下虚拟桌面号（蓝底），底色沿对角线分开、中间一条 `/` 方向的白线（字号能到图标边长的
    一半，因为不再把 `1/2` 挤成一行）；由 `settings.tray_badge` 选 `"both"`（默认）/ `"desktop"`
    （只有虚拟桌面号，蓝底居中）/ `"workspace"`（只有工作空间号，绿底居中）；两个号都按字形墨迹居中、
    两位数一律 `9+`（再降一档字号并往角里挪）；一个号都查不到时**退回应用图标**（不画 0），
    `both` 只剩一个号时不画对角线。工作空间号取 `switch_target` 那块屏的活动空间。来源是动作线程上
    500 ms 的轮询（`QTimer` 必须在它那条线程建，轮询要的配置由每个进来的任务顺手记住），结果（整份
    `core::TrayBadge`）投回 GUI 线程换图标，**绝不在 GUI 线程做 COM**；`workspace("switch")` 执行完
    立刻补一次轮询。
21. **窗口切换器 `windows()` 与「轻碰 Win」**：按**进程名前缀**筛选，剩一个窗口直接激活；只列 shell 真会
    显示的窗口。**「轻碰 Win」= 单个修饰键 + `trigger = "release"`**：按下时放行、期间有别的键就作废、单独
    松开才触发并注入 `VK_UNASSIGNED` 挡开始菜单；单修饰键 + `press` 保持老行为。
22. **窗口切换器数字选择模式**：筛选串非空 + 全属同一进程名 + ≥2 个窗口 → 前 10 行分到 `1`..`9`、`0`，
    按数字直接跳；此时数字键不进筛选框（其它情况照旧放行，`7zip` 要能用数字筛）。
23. **五个弹窗不进任务栏（`Qt.Tool`）且启动时预热**：`preload()` 在启动后第一个事件循环回合建窗口 + 假
    数据渲染一帧（**透明度 0 + 屏幕之外**），首帧到了就藏起来（冷启 224 ms → 预热后 ~30 ms）。弹出前先
    `cancelPreload()`。**显示器配置/缩放一变就丢掉窗口缓存重建**（模型留着），见 `pitfalls.md`。
24. **窗口切换器卡片**：无标题行（筛选框就是第一行，`listTop` = 50）、行内「进程名在上 / 窗口标题在下」、
    列表宽度 = 筛选框宽度、`windows()` 的 `title` 只当窗口标题。**卡片开着时再按同一个快捷键 = 关掉**
    （判断在 `PopupHost::showSwitch()`）。**打开时切英文输入法**（`platform/win/ime.*`，关卡片时写回）；
    启动器**不**切输入法（名字可能是中文）。
25. **在线更新：托盘菜单点一下，自己换 exe 并重启**。下载 `*-slim-windows-x64.zip`（约 700 KB，只有 exe，
    用 `QZipReader`）；**只换 exe、不动运行时**（新 exe 活不过 2.5 s 就回滚）；旧 exe → `<exe>.old` →
    新的就位 → 启动 → 等 2.5 s，`.old` 由新实例删；`--updated-from` 是重启时唯一的内部参数；下载的 exe
    mtime 对齐发布日。手动触发、启动时不检查。`Qt6::Network` 进了依赖 → 精简清单**必须留
    `tls/qschannelbackend.dll`**。
26. **远程桌面放行（`settings.remote_desktop`，默认开）**：前台窗口属主进程命中名单（默认只有
    `mstsc.exe`/`msrdc.exe`/`msrdcw.exe`/`RdClient.Windows.exe`）时**所有快捷键与重映射一律放行**，例外是
    条目自己的 `remote_desktop = true`。判据只有“前台窗口属主进程名”（子串、大小写无关），检测在钩子线程
    上。**名单里的空串是错误**（空 needle 表示“不限制”，会让所有窗口都算远程桌面）。进入时清长按重复与
    待定轻碰、松开已被放行的重映射目标键。
27. **程序启动器 `apps()`：`shell:AppsFolder` 的图标网格**。数据源 = 开始菜单「所有应用」（含商店/UWP 与
    `.msc`）；过滤两道（判据全在 `core::app_list`，**不按名字猜噪音**）；启动走
    `shell:AppsFolder\<AUMID>`；卡片 **6 列 × 6 行 / 格子 126×88 / 图标 40 / 卡片 800×622**（宽度必须由
    “内边距 + 列数 × 格宽”算出来）；筛选是名字**子串**、筛到一个**不**自动启动；图标**异步**取（常驻 STA
    线程 + `QQuickAsyncImageProvider`，键是启动名的 FNV-1a）。圆括号是动作、花括号 `app{}` 是注册构造器。
28. **启动器右键菜单 = Windows 自己的那一份**（`IContextMenu2/3`，命令由 shell 执行）：`CMF_EXTENDEDVERBS`
    要有「以管理员身份运行」；菜单出在**光标处**（物理像素）；**不套 DPI 上下文**（会放大一倍）；debug
    日志留 `shell menu: N item(s) for <启动名>`（**别删**）；右键在**松开**时弹；**选中条目先收卡片再执行**
    （`beforeInvoke`），取消则留着（`AppListModel::afterContextMenu()`）。
29. **启动器筛选认拼音**：`core::appSearchText()` 给出「名字 / 全拼 / 首字母」三段（`U+001F` 分隔）做子串
    匹配；多音字全读音都进（上限 `core::kMaxSearchVariants` = 8）；表是**离线生成后提交**的
    （`src/core/pinyin_data.*` ← `tools/pinyin_gen.ps1`，只覆盖 U+4E00–U+9FFF）。所以
    **`flowkeyd_models` 链 `flowkeyd_core`**。
30. **启动器三段：已固定 / 最近使用 / 全部程序**（概览为空时直接显示全部程序）。`Space` 固定/取消固定
    （筛选串非空时放行给 `TextInput`）；「全部程序」是同一卡片里的另一种视图（按名字/拼音首字母分组，
    `#` 组连续，`Esc` 返回）。持久状态在配置文件旁的 `launcher.json`（身份 = 图标键；模型层不碰磁盘）。
    模型是**行式**的；**卡片高度恒定**，不跟内容缩；滚动位置只在换视图时回顶部（别跟 `contentHeight`）。
    快速启动键：筛选后前 10 条 `0`–`9`、最近使用 `Alt+数字`、已固定 `Alt+F1..F12`（`modifiers` 必须
    **恰好** `Qt::AltModifier`），全在模型里算。
31. **启动器目录 = 开始菜单 + 控制面板「所有控制面板项」+ 内置 26 个 `ms-settings:` 页**。控制面板项用
    `SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder)`（**不是** `{26EE0668-…}` 本身），“网络连接”单独
    硬编码；每条启动名必须**验证能解析回去**。设置页没有 shell 名字也没有图标 → 内置中英对照 + 借“设置”
    应用图标（键仍是 URI）。合并只在 `Dispatcher::refreshAppCatalog()` 里做，**同名条目丢掉后面的**。
32. **没声明固定桌面的程序：`window("activate")` 把窗口搬到当前桌面**（判据 = 命中该窗口的第一条
    `window_rule` 有没有 `desktop`）。**必须等搬完再激活**（否则又把视图切走）；先问便宜的
    `isWindowOnCurrentDesktop()`，问不出来就不动窗口。`all_desktops = true` 不算声明桌面。
33. **没写 `monitor` 时摆到哪块屏 = `settings.window_monitor`，默认 `"cursor"`**（取值同 `monitor`，多
    `"cursor"`/`"last"`/`"none"`）。`"last"` 记在 `<配置目录>/windows.json`（键 = exe 名小写，值 = 设备名），
    只有真用到才读写。优先级：动作 `monitor` → 命中规则 `monitor` → 全局；`"none"` 逐层能显式关掉。
    目标是动态的且规则没写几何时**只放对屏、不重新居中**。
34. **图形化配置界面（托盘「配置...」）**：可缩放的普通窗口（左导航 + 分组卡片 + 状态行），**不加
    `Qt.Tool`**。**保存 = 整体重写 `config.lua`**（单一真源是 `core::configToMap()`，由 `configToLua()`
    生成；先留 `.bak`，覆盖非界面生成的文件前先问；首行标记 `-- flowkeyd-config-editor:generated`）。
    覆盖全量字段与 21 种动作。**保存前用同一个 Lua 求值器 + `core::compile()` 读回来校验，不通过一个字节
    都不写**。条目编辑走每次新建的弹层；和弦用**文本录入 + 即时校验**；**中文显示、英文存储**
    （`ConfigText` 单例，三态用 `TriRow`）；保存成功就 `reloadRequested()`。
    * `settings` 里嵌套的表（目前只有 `settings.workspace`）用 `ConfigEditor::setWorkspaceSetting()` 改，
      它只动那一层，不会把其它设置项写丢。
35. **启动的程序默认是「当前登录用户的普通权限」，`elevated = true` 才提权**（`run` 与 `launch`；`open` 与
    启动器总是普通权限）。做法：从登录 shell 拿主令牌 + `CreateProcessWithTokenW`；`ShellExecuteExW` 没有
    令牌参数，所以 `open` 与 `shell:AppsFolder` 走**普通权限的代理进程**（自己的隐藏副本 `--shell-*`，不
    提权/不读配置/不装钩子）。拿不到令牌就退回继承令牌 + warning。**启动器右键菜单里的命令仍然提权**
    （已知限制，见第 12 节）。
36. **工作空间：按显示器独立的一套空间（`settings.workspace` + `workspace` 动作）**。**M0（真机验证）到
    M11（文档、验收、收尾）全部完成**。要点：默认**开启**但没绑定就不会隐藏任何窗口；`count` 1–10（默认 9）；
    `switch_target` = `"cursor"`（默认）/ `"focus"`；`new_window` = `"active"`（默认）或 1..count
    （**只对 flowkeyd 运行期间新出现的窗口生效**，启动时已在的窗口照旧收编进各自活动空间）；
    `exclude` 空串报错；动作只有 `workspace("switch"|"move", N)`，`index` ≤ `count` 在 `--check` 校验；
    规则用 `window_rule.workspace` / `app.workspace`（后者是前者的简写），归属优先级 = 命中窗口的
    **第一条**规则的 `workspace` → `settings.workspace.new_window` → 那块屏的活动空间（纯函数
    `core::workspaceForNewWindow()`）；钩子线程**先发 `window_rule` 的摆放、再发工作空间的收编**
    （规则只能摆看得见的窗口，所以归属必须在摆完之后才决定）；隐藏方式钉死为
    `ShowWindow(SW_HIDE)`（`platform/win/workspace.*` 里唯一一处）；“窗口在不在”只看状态机记录、
    `IsWindowVisible` 不可信。事件管线：钩子线程（只有 `settings.workspace.enabled` 时才装
    `LOCATIONCHANGE` 与 `MINIMIZESTART..END` 两个窄范围监听）只把 `core::WorkspaceEvent` 入队，
    状态机的**唯一所有者是动作工作线程**，我们自己引起的显隐靠 `core::ExpectedWorkspaceEvents` 吞掉
    （`SW_HIDE` 会额外产生一条 `EVENT_OBJECT_SHOW`）。动作执行每次都先刷新「当前虚拟桌面」
    （只有**当前桌面**上的窗口才显隐，`hide` 列表用**一次批量 COM 查询**过滤），Z 序用 `restack()` 还原
    （不要用 `show()` 的 `insertAfter`，方向是反的）。显示器拔插 = `MonitorsChanged` 改归属（含
    `homeMonitor`）+ `relocateDisplacedWindows()` 把窗口物理上搬回**记录里那块屏**（判据
    `core::windowIsOffMonitor()`；搬用 `workspace::relocateToMonitor()`，**不能用**
    `monitor::moveToMonitor()`，它会把藏着的窗口弹出来）；拖屏靠 `LOCATIONCHANGE` + 防抖，但
    **记录里那块屏不在时要跳过**（否则丢掉 `homeMonitor`）。**安全兜底（M8）**：藏窗口前先把
    「窗口身份 + 归属」写进 `<配置目录>\workspace.json`（`core/workspace_log.*`，写不进去就**不藏**），
    显示成功后才删条目；启动最早期（读配置之前）按日志恢复，正常退出 / 注销 / 关机（`SessionEndWatcher`
    的 `WM_QUERYENDSESSION`）前也恢复，恢复失败最多等 2–3 s，日志留着下次再恢复。手动入口：托盘
    「恢复全部窗口」与 `--restore-windows`（有实例在跑就通过 `-restore` 事件请它做，没有就自己按日志
    恢复；不读配置、不提权）。**联动（M9）**：被藏起来的窗口 `IsWindowVisible` 是假，所以
    `window::find()` / `listOpenWindows()` 都要**多收一份状态机记录**（`tracked`）才能找到它；
    `window("activate")` / `windows()` 选中 / `launch` 之后一律**先 `revealWindowWorkspace()` 切空间、再激活**
    （顺序不能反：对 `SW_HIDE` 着的窗口 `raiseWindow()` 没用）；被藏着的窗口**永不 `toggle` 成最小化**
    （`planWindowAction(..., hiddenByUs)`），也不接受最小化 / 最大化 / 还原 / 挪屏（那些靠 `ShowWindow` /
    `SetWindowPlacement` 改显隐）；换屏后的归属：看得见的并入目标屏活动空间（`windowMovedToMonitor()`），
    藏着的**保留空间号**（`windowReboundToMonitor()`）；`window_rule.workspace` 在 `applyPlacementRules()`
    里生效，于是与 `window_rule` 的三个时机完全一致（首次出现 / 显示器重接入 / 启动），用户手动 `move`
    之后不会被拉回去。完整理由见 `decisions.md` 第 36 条。

---

## 3. 环境与工具链

| | |
| -- | -- |
| 开发环境 | 原生 Windows 10.0 build 26300（26H2，UBR 9711）(x64)，交互式桌面会话；工程路径 `D:\prj\flowkeyd` |
| Qt | `C:\Qt\6.11.2\mingw_64`（只有 mingw_64 这一套） |
| 编译器 | `C:\Qt\Tools\mingw1310_64\bin\g++.exe`（GCC 13.1.0） |
| CMake / Ninja | `C:\Qt\Tools\CMake_64\bin\cmake.exe` / `C:\Qt\Tools\Ninja\ninja.exe` |
| Qt Creator | kit `Desktop_Qt_6_11_2_MinGW_64_bit_Debug`，产物 `build/Desktop_Qt_…_Debug` |
| 未安装 | Visual Studio / MSVC、MSYS2、独立 mingw |
| Lua | `vendor/lua` = `lua/lua` **v5.5.1**（`7579fc9d`） |

Qt 侧齐备：`Qt6::Quick`、`QuickControls2`、`Qt6QuickControls2FluentWinUI3StyleImpl`、`Qt6::Test`、
`Qt6::QuickTest`、`Qt6::Widgets`、`Qt6::LabsPlatform`、`Qt6::Network`。
**本机是两块 1920x1080@200% 的屏**，`availableGeometry()` 可能比 `geometry()` 还宽；弹窗按**光标所在那块
屏**居中（`centreOnCursorScreen()`）。

### 依赖政策：什么可以静态链接，什么必须运行时解析

* **Qt 模块照常 `find_package` + `target_link_libraries`**，外加 **Qt 私有**模块 `Qt6::CorePrivate`
  （**只**为了 `qzipreader_p.h`）；要在 `find_package` **之前**设 `QT_FIND_PRIVATE_MODULES ON`。
* **允许静态链接**（MinGW 工具链自带）：`user32`、`kernel32`、`shell32`、`ole32`、`ntdll`、`advapi32`、
  `powrprof`、`uxtheme`、`comctl32`、`shlwapi`、**`gdi32`**（启动器读图标 DIB 像素；同样是每个 GUI 进程
  都加载的核心系统 DLL）。
* **必须 `LoadLibraryW` + `GetProcAddress` 运行时解析**：未公开入口（`win32u!NtUserSendInput` 等，没有
  `libwin32u.a`）以及不在上面集合里的 DLL（`dwmapi`、`imm32`、`d3dcompiler`…）。
* **COM 接口手写 vtable**（Core Audio、`IVirtualDesktopManagerInternal`/`IVirtualDesktopPinnedApps`）。
  **本仓库风险最高的两块代码**：布局写错不是返回错误码而是崩溃；接口定义以真机验证为准，虚拟桌面那张表
  按 `build.revision` 索引。
* 不引入会藏起未公开入口的封装层。

---

## 4. 代码地图

| 路径 | 职责 |
| ---- | ---- |
| `CMakeLists.txt` / `CMakePresets.json` | 顶层工程、Qt 查找、`qt_add_qml_module`、部署；两个 preset |
| `cmake/VendorLua.cmake` | `vendor/lua` → 静态库 `lua_static`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`，`LUA_USE_WINDOWS`，`AUTOMOC/AUTOUIC OFF`） |
| `cmake/PruneRuntime.cmake` | **发布运行时精简清单**（windeployqt 之后剪裁；release 再 strip）。**“发布包里有什么”唯一的家** |
| `cmake/version_revision.h.in` / `cmake/RunQTest.cmake` | git 修订常量；测试输出重定向 |
| `README.md` / `flowkeyd.lua.example` | **用户文档 + schema 权威定义** / 参考配置（零警告） |
| `install.ps1` | 对外一键安装器（**纯 ASCII、无 BOM**） |
| `logo.svg` / `assets/` / `tools/icon_gen` | 图标唯一真源；`.ico`（windres）+ 多尺寸 png（qrc）；`icons` 目标 |
| `tools/pinyin_gen.ps1` | 生成 `src/core/pinyin_data.cpp`（离线跑一次、结果提交） |
| `src/main.cpp` | `AttachConsole` + `freetype` 字体引擎（**第一行**）+ CLI 分发（含 `--shell-*` 代理分支）+ 日志 + 提权前单实例预检 + 自启确认 + 组装 Runtime + 排队 `preload()` |
| `src/cli.*` | 参数解析 + 中文帮助；`--quit` 早期分支；`--shell-*` 是**内部协议**（不进帮助） |
| `src/core/` | **纯逻辑层：不碰 Win32、不碰 Qt GUI**（只用 QtCore），能被 Qt Test 直接测 |
| `core/keys.*` / `core/engine.*` | 键名 ↔ `VK` 表、和弦、发送脚本解析、大写字母检查 / 快捷键状态机（匹配、吞键、重复、挂起、重映射 hold-tap、菜单遮断、轻碰） |
| `core/config.*` / `core/action.*` | 结构体 + 严格校验 + `Compiled`/`expandApps` / 动作表示 + 摘要（`--list` 与 `help()` 共用）+ `isDestructive()` |
| `core/template.*` / `core/window_match.*` | `{clipboard}`/`{selection}`/`{date}` / 窗口匹配纯函数 + “什么算一个程序窗口” |
| `core/remote_desktop.*` / `core/placement.*` | 远程桌面名单匹配 / `window_rule` 几何与匹配、`stepIndex()`、`windowActionMonitor()` |
| `core/app_list.*` / `core/launcher_state.*` | 启动器纯逻辑（判据、图标键、合并、匹配、分组、内置设置页）/ 固定+最近的持久状态 |
| `core/pinyin.*` / `core/pinyin_data.*` | 汉字读音查询 / 生成的读音表 |
| `core/monitor_ref.*` / `core/window_monitor_state.*` | “摆到哪块屏”的取值与判据 / `monitor = "last"` 的记忆 |
| `core/config_map.*` / `core/config_writer.*` | 结构体 ↔ QVariant 字典（界面用）/ 结构体 → Lua 源码（含生成标记、`.bak`） |
| `core/workspace.*` | 工作空间**纯逻辑状态机**（命令 → 显隐/焦点效果列表，含 M9 的 `revealWindow` / `windowReboundToMonitor`）+ `core/workspace_event.*`（钩子线程 → 工作线程的事件 + 「自己引起的事件」抑制表）+ `core/workspace_log.*`（恢复日志 `workspace.json` 的解析 / 序列化：64 位整数写成字符串）；M1–M11 已全部接线（`workspace(...)` 动作能真的显隐窗口、拔插会迁移与复位、崩溃与注销后能恢复、藏在别的空间里的窗口能被唤起、规则与 `new_window` 决定新窗口的归属） |
| `core/log_tail.*` / `core/update_check.*` / `core/version.*` / `core/desktop_badge.*` | 日志增量尾随 /
    更新纯逻辑 / 版本号 / 徽标文字与字号（含 `settings.tray_badge` 的三种写法）|
| `src/lua/lua_config.*` + `lua_prelude.lua` | **Lua 与 C++ 的唯一边界**（转成 `core::Config`，逐条目带上下文报错）；DSL 编进 qrc |
| `src/platform/win/` | 一个文件一件事：`ffi`（全部声明 + `currentExecutablePath()`）、`nt`、`process`（run 三条路 / open / `--shell-*` 代理）、`dwm`、`monitor`、`input`（`SendInput`、验收后门、`copySelection`）、`ime`、`hook`（回调 + 消息循环 + 定时器 + `window_rule` / 工作空间事件监听 + 显示器轮询 + 前台事件）、`apps`（扫描 + 启动 + 图标 + `StaThread`）、`shell_menu`、`audio`（MTA，高风险）、`clipboard`、`window`、`workspace`（工作空间的 hide/show/restack/身份/枚举/恢复日志读写，`SW_HIDE` 只在这里）、`session`（`WM_QUERYENDSESSION` 的隐藏窗口）、`desktop`（版本表）、`power`、`tray`、`logging`/`single_instance`（退出与「恢复全部窗口」两条事件通道）、`elevate`/`autostart`/`update` |
| `src/app/dispatcher.*` | **动作工作线程**：执行动作、`window` 的先启动再激活与默认 `toggle`、摆显示器、四张卡片的请求、`window_rule` 三遍、桌面轮询、启动器状态文件路径 |
| `src/app/runtime.*` | 引擎 + 钩子 + 分发 + 托盘 + 弹窗总装；`ControlCmd` 通道；`--quit` 事件句柄 |
| `src/app/{menu,help,window_list,app_list}_model.*` / `log_model.*` | 卡片与日志窗口的**纯逻辑**（`QAbstractListModel`，只用 QtCore）；行几何与鼠标命中不归它们管 |
| `src/app/popup_layout.*` / `popup_host.*` | 弹窗几何与 `centrePopup()`/`popupPixelSizeIsStale()`；挂模型、抢前台、建/复用窗口、`helpRun()`（**先藏窗口再执行**）、`preload()`、缩放变化丢窗口 |
| `src/app/config_editor.*` / `config_window.*` | 配置界面的模型层（在 `flowkeyd_models`，纯 QtCore，`save()` 先序列化再用真 Lua 校验）/ 宿主（在 exe，`loadFromModule`） |
| `src/app/app_icons.*` / `app_icon.*` | 启动器异步图标（STA 线程 + 缓存）/ qrc 多尺寸 `QIcon` + `trayBadgeIcon()` |
| `src/app/{update_model,update_archive,updater}.*` | 更新卡片状态机 / 取新 exe / 联网编排（sha256、mtime 对齐） |
| `src/qml/` | 六个窗口 + 配置界面共用控件（`SettingCard`/各 `*Row`/`ChordsEditor`/`ActionEditor`/`ActionListEditor`/`LaunchFieldsEditor`/`WindowRuleFields`/`HotkeyFields`/`OverlayCard`/`ConfigText` 单例与四个弹层）。全部 `pragma ComponentBehavior: Bound`；五个弹窗 `flags` 带 `Qt.Tool`；配色用 `palette`；中文 `Microsoft YaHei` |
| `tests/` | 每个模块一个 `tst_*`（清单见 `CMakeLists.txt`），含需 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 的 `tst_interactive`（联网用例还要 `FLOWKEYD_ALLOW_NETWORK_TESTS=1`） |
| `scripts/acceptance.ps1` | 桌面行为验收（192 项 / 实跑 189）。需交互式桌面，**不属于 `ctest`**；覆盖见 `reference.md` |
| `scripts/release.ps1` | 构建 + 打包（完整包 + 精简升级包 + `.sha256`）+ `gh` 上传；**发布说明脚本自己写**（按提交前缀分类）；工作区脏或 HEAD 没推会拒绝（`-AllowDirty`/`-Push`） |

> `scripts/install.ps1` / `uninstall.ps1` **已删除**：自启的注册、刷新与删除全在
> `src/platform/win/autostart.*`，由守护进程自己在启动时做。

### CMake 目标划分

| 目标 | 内容 | 谁链接 |
| ---- | ---- | ------ |
| `flowkeyd_core` | `src/core/*`（纯逻辑，只用 QtCore） | exe + 全部单测 |
| `flowkeyd_lua` | `src/lua/*` + qrc 的 `lua_prelude.lua` | exe + `tst_lua` |
| `flowkeyd_models` | 四个卡片模型 + `config_editor` + `popup_layout`（链 `flowkeyd_core`） | exe + 五个测试 |
| `flowkeyd_update` | `src/app/{update_*,updater}.*`（`Core` + `Network` + `CorePrivate`） | exe + 三个 update 测试 + `tst_interactive` |
| `flowkeyd_platform` | `src/platform/win/*` | exe + 平台层单测 |
| `flowkeyd` | `src/main.cpp`、`src/cli.*`、`src/app/*`、`tray.*`、QML、图标 qrc + `.rc` | —— |
| `flowkeyd_icon_gen` | `tools/icon_gen/main.cpp`（`EXCLUDE_FROM_ALL`） | —— |

* `flowkeyd_add_test(name [LIBS …])` 把 Qt/MinGW 的 DLL 目录写进 test 的 `PATH`。**测试目标只在这个函数
  被调用时创建，整段（连 `enable_testing()`）都包在 `FLOWKEYD_BUILD_TESTS` 里。**
* `popup_host.*` 与 `config_window.*` 用 QML/Quick，**不进** `flowkeyd_models`；只有 `config_editor.*` 进库。
* **配置界面的 QML 要在 `CMakeLists.txt` 里登两处**：加进 `FLOWKEYD_QML_FILES`，并给自己
  `QT_RESOURCE_ALIAS <文件名>`（否则不会被当类型名认识）；`ConfigText.qml` 还多一个
  `QT_QML_SINGLETON_TYPE TRUE`。
* **exe 的产物目录自包含且精简过**：`flowkeyd` 上挂 `POST_BUILD` 的 `windeployqt`（`--qmldir src/qml`
  必须给）+ `cmake/PruneRuntime.cmake`，只对 `flowkeyd` 做；release 还把干净发布目录写到
  `build/dist-release/`（**212 个文件 / 65.4 MB**，精简前 1378 / 149.8 MB）。`PruneRuntime.cmake` 挂在
  `LINK_DEPENDS` 上（改了清单自动重链接）。
* **分层铁律**：`src/core/`、四个卡片模型、`popup_layout.*`、`core/update_check.*` **不许出现
  `<windows.h>`、QML/QtWidgets、窗口句柄**。`platform/win/window.cpp` 里“候选窗口如何匹配”这种判断要
  拆成纯函数放进 `core`。

---

## 5. 构建、测试与运行

```powershell
$C = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'

& $C --preset windows-debug          # 首次
& $C --preset windows-release        # 首次
& $C --build --preset debug
& $C --build --preset release
& ctest --test-dir build/windows-debug --output-on-failure   # 单测只在 debug 上

# 联网用例（真的去 GitHub 查一次并下载 slim 包，只落到临时目录）
$env:FLOWKEYD_ALLOW_NETWORK_TESTS = '1'
& build\windows-debug\tst_interactive.exe checksAndDownloadsAnUpdateFromGitHub

dir build\dist-release               # 发布包（release 构建自动产出，拷走就能跑）
```

**零警告、零失败**（仓库范围内 `-Wall -Wextra -Werror`，`vendor/lua` 例外）。
preset：debug = `Debug` + `FLOWKEYD_BUILD_TESTS=ON`；release = **`RelWithDebInfo`**（另一条优化路径、能暴露
debug 看不到的警告）+ `FLOWKEYD_BUILD_TESTS=OFF`。

**运行（开发期一定带这三个开关）**：

```powershell
& build/windows-debug/flowkeyd.exe --no-elevate --allow-multi --console --config .\tmp\smoke.lua
```

`--no-elevate`：开发期一律关掉提权。`--allow-multi`：跳过单实例检查（每个实例都装钩子，**后装的先收到
事件**）。`--console`：保留控制台输出。

**离线命令（绝不允许提权）**：`--check`/`--list`/`--list-keys`，提权判断必须在它们 `return` 之后；`--quit`
也不提权，`--remove-autostart` 要管理员。开发实例与常驻的单实例锁按**配置文件路径**分开。

**桌面行为验证**：`scripts/acceptance.ps1`（192 项 / 实跑 189，约四分半，会持续注入按键/抢焦点 —— 先按
工作约定第 6 条提醒用户）。覆盖范围、跳过项、已知 flake 与**手工冒烟清单（20 项）**在
`docs/notes/reference.md`。

---

## 6. 架构与线程模型

```
        ┌─────────────────── Qt GUI 线程（主线程）───────────────────┐
        │ QApplication + QQmlApplicationEngine                        │
        │   * QSystemTrayIcon（右键菜单、气泡、悬停提示）              │
        │   * 日志窗口 + menu/help/switch/apps/update 五个 QML 卡片    │
        │     + 图形化配置窗口                                         │
        │ 收到动作结果 → 只做“建/前置窗口”这一件事，绝不执行动作       │
        └───────▲──────────────────────────────┬────────────────────┘
                │ 队列信号（QueuedConnection）  │ 用户选择 → 回投任务
                ▼                              ▼
        ┌──────────────────────┐  PostThreadMessage  ┌──────────────────────┐
        │ 钩子线程（自己的 Win32 │◄───────────────────│ 动作工作线程          │
        │ 消息循环 + SetTimer）  │  WM_APP 控制消息    │ (QThread)            │
        │  * WH_KEYBOARD_LL     │                     │ run/send/open/...    │
        │  * core::Engine       │──── 任务队列 ──────►│ window/volume/...    │
        │  回调必须极快返回      │                     │ clipboard/power/...  │
        └──────────────────────┘                      └──────────────────────┘
```

1. **钩子线程**：`LowLevelHooksTimeout` 默认 300 ms，慢回调的钩子会被 Windows **静默卸掉**（进程看起来还
   活着却什么都不做）→ 回调只做「解码 → 问引擎 → 注入重映射按键 → 把动作排给工作线程」然后立刻
   `CallNextHookEx`。定时器（长按重复、挂起超时、显示器轮询）也在这条线程上。
2. **动作工作线程**：`run`/`open`/`wait` 都会阻塞，绝不能在钩子线程上做；它也是四张卡片的“请求方”，但
   **窗口本身**由 GUI 线程创建。
3. **Qt GUI 线程**：托盘与全部 QML 窗口。跨线程一律走队列信号。

**钩子为什么不装在主线程**：QML 渲染的一次慢帧、`QSystemTrayIcon` 菜单的模态循环都可能让主线程几十毫秒
不回来；300 ms 的预算经不起这种抖动。

**通信与同步**：钩子 → 工作线程用 `QMetaObject::invokeMethod(dispatcher, …, Qt::QueuedConnection)`（只
入队不阻塞）；工作线程 ↔ GUI 用队列信号；托盘/CLI → 钩子线程用 `PostThreadMessage`（**目标线程还没消息
队列时会失败** → 钩子线程先 `PeekMessage`，再用 promise/原子量确认“队列已就绪”）；跨线程共享的配置是
`std::shared_ptr<const Compiled>`，**reload 时整体替换指针**，绝不就地改。

---

## 7. 不变量 —— 不要破坏这些

1. **钩子回调必须快速返回。** 不要在回调里 sleep、写慢速日志、做 IO 或执行动作。
2. **绝不对注入输入作出反应。** 用 `LLKHF_INJECTED` 判定；带自己标记（`dwExtraInfo` 里的 **`"FLOW"`**）的
   事件在任何状态被触碰之前就丢弃。**新增任何注入路径都必须打上标记**（包括 `ModifierGuard` 里“先松开
   Win/Alt 再按回去”的注入）。
3. **重映射的按键内联注入，动作不是。** 因此重映射里的 `{Sleep}` 会被忽略（`--check` 给警告）。
4. **绝不能让按键留在按下状态。** 任何丢弃重映射的路径（挂起、重载、退出）都必须注入释放。
5. **不要跨 `SendInput` 持有锁。** 钩子状态放在钩子线程自己拥有的对象里（没有锁），**不要给它再套锁**。
6. **配置错误必须可操作。** 每条校验信息都要指出出错的条目（`name` 或序号）与出错的值，格式固定为
   ``hotkey #3 (`terminal`): unknown field `nope`, expected one of …``。**未知字段一律报错**（C++ 侧自己
   实现严格白名单）。**动作表顶层是唯一的例外**（拼写错误会被忽略）—— 要在 README 的已知限制里写明。
7. **`core/` 与 `lua/` 保持无 Win32、无 GUI。** 这是核心逻辑无需桌面就能测试的前提。
8. **固定结构体布局是契约**：`INPUT` 在 x64 上必须 40 字节（`static_assert`）。
9. **被吞掉的 `Win+…`/`Alt+…` 和弦必须在修饰键 key-up 时注入标记按键（`VK 0xE8`）。** 跟和弦键一起注入
   **不够**：Windows 键的自动重复会在两者之间重新武装外壳。照抄 `Engine::mask_menu_key_up`：吞掉时设标志，
   **下一次** Win/Alt key-up 先注入标记。
10. **`send`/`type` 的 `release_modifiers` 也要做同样的菜单遮断**：`ModifierGuard` 注入的真实 key-up 引擎
    看不到，必须在注入层插一次 `VK_UNASSIGNED`。
11. **提权只发生在守护进程模式，且必须能降级。** `--check`/`--list`/`--list-keys` 绝不能弹 UAC；UAC 被拒
    时打 warning 继续以普通权限运行。提权重启要转发完整命令行（含相对路径的 `--config`）与工作目录。
    **本机实测：点“否”时 `ShellExecuteW("runas")` 返回 5 而不是 1223**，所以不能只判 `ERROR_CANCELLED`。
12. **`--console` 之外，守护进程模式总是打开默认日志文件**（目录要建）。日志窗口尾随的就是那个文件；
    离线命令仍然不碰它。
13. **虚拟桌面与音频的 COM 单元模型不能混。** 音频要 **MTA**、虚拟桌面要 **STA**，所以 `desktop` 每次
    调用都丢进一条**一次性的 STA 线程**。**Qt 在 Windows 上会为 OLE/拖放把主线程初始化成 STA**，别去改它。
14. **Lua 状态绝不越出 `lua_config` 的求值函数**（求值完就关掉，结果转成普通 C++ 结构）；**`Compiled`
    必须与 Lua 无关**。
15. **动作只能是声明式的表或简写字符串**；`action = function() end` 由 `lua_prelude.lua` 的
    `reject_functions` 明确拒绝。
16. **配置文本先剥掉 UTF-8 BOM 再交给 Lua**（词法分析器不认它）。
17. **弹窗窗口必须能拿到键盘焦点。** 钩子守护进程通常不持有前台锁 → `requestActivate()` →
    `SetForegroundWindow` → `AttachThreadInput` 到持有锁的线程再试 → `BringWindowToTop` +
    `SetWindowPos(HWND_TOP)`，**每条路径上 `AttachThreadInput` 都要配平**。
18. **模态/前台相关的一切都要在 Qt GUI 线程上做**（`QSystemTrayIcon`、`QQuickWindow`、`QMenu` 不能跨线程）。
19. **DPI 感知必须在创建任何窗口之前就绪**：**不要在 `QApplication` 构造之前创建窗口/触碰屏幕 API**；
    不要调 `SetProcessDPIAware()`（会让字发糊）。
20. **日志窗口是一个普通窗口，不是独立进程**：关掉它不能退出应用（`setQuitOnLastWindowClosed(false)`）。

---

## 8. Lua 5.5.1 的注意点（已实测）

1. **`luaL_openlibs` 现在是宏**（`luaL_openselectedlibs(L, ~0, 0)`）；调用仍然可用。
2. **`lua_newstate` 多了 `unsigned seed` 参数**（用 `luaL_newstate()` 就没问题）；**`lua_resume` 签名是
   `(L, from, narg, nresults, ...)`**，`lua_resetthread` 是宏（本项目不做协程）；新增 `lua_closethread`/
   `lua_closeslot`/`lua_toclose`。我们**用到**的 API 全部确认存在。
3. **两个行为差异**：`repeat` 仍是关键字 → 字段名用 `repeatable`（认 `["repeat"]` 别名）；字符串里的
   `\t`/`\n`/`\r` 是**合法但致命**的转义（`open("C:\tools")` 会静默变成 `C:` + TAB + `ools`）→ Windows
   路径一律用长字符串 `[[C:\path\app.exe]]`。
4. **`lua_newuserdatauv`**（不是旧的 `lua_newuserdata`）。
5. **把 Lua 版本号写进 `--version` 与 `--check`**，出问题时一眼看出是哪一份。

---

## 9. 实施阶段计划

每阶段都可运行、可提交、都要满足第 11 节。

* **0–4 已完成**：构建骨架（双 profile 绿、`ctest` 绿、托盘 + 日志窗口）→ 纯逻辑核心（`src/core/*` + 6 个
  测试目标）→ Lua 配置层（`--check`/`--list`、示例零警告）→ Win32 + 钩子 + 引擎接线（钩子/动作线程）→
  托盘 + 日志窗口（完整托盘菜单 + `log_tail`/`log_model`）。
* **5–10 已完成**：窗口动作 + 剪贴板 + 音量/媒体 → `menu`/`help` 弹窗 → 虚拟桌面 + 电源 → 示例配置 +
  README → 验收脚本（+ `FLOWKEYD_ACCEPT_INJECTED` 后门）→ **接管**（真实配置迁到
  `%USERPROFILE%\.config\flowkeyd\`，常驻由计划任务指向当前 exe）。
* **第 10 阶段之后新增**（细节都在第 2 节与 `docs/notes/`）：`window_rule`、`app{}`、四个挪窗口 op、字母
  键名小写、托盘数字徽标、窗口切换器（+数字选择 + IME）、弹窗不进任务栏 + 预热、发布包精简、精简升级包、
  `scripts/release.ps1`、在线更新、`install.ps1`、FreeType、程序启动器（+异步图标 / `shell:AppsFolder` /
  拼音 / 固定·最近·全部 / 控制面板与设置页）、弹窗缩放过期自愈、跨桌面唤起、`settings.window_monitor`、
  图形化配置界面、启动权限、**工作空间（M0–M11，含拔插 / 拖屏 / 恢复日志 / 规则与 `new_window`）**。

---

## 10. 已踩过的坑

**全部条目（分类：环境与工具 / 配置·Lua·核心逻辑 / 构建·测试·打包 / QML·界面 / 图形化配置界面 / Windows
领域坑 / 原生 shell 右键菜单 / 测试·验证手法）在 `docs/notes/pitfalls.md`。**

动下面这些区域之前**先读对应那一节**，否则会重复踩：

* **COM / vtable**（虚拟桌面、音频、`IVirtualDesktopPinnedApps`、`MoveViewToDesktop`）
* **DPI**（弹窗缩放过期、shell 菜单被放大一倍、`GetWindowRect` 被虚拟化）
* **QML 布局/模型**（`Q_PROPERTY` 自定义结构体静默失效、`Row`+锚点崩溃、`contentHeight` 抖动、
  `ItemDelegate` 内边距、角色名撞名）
* **注入与测量**（锁屏时注入是假的、像素指纹、进程内预览）
* **工作空间（显隐 / 显示器拔插 / 拖屏）**（`SW_HIDE` 的显隐语义、`SetWindowPlacement` 会改显隐、
  最大化的窗口要先还原再重新最大化、`LOCATIONCHANGE` 比 `MonitorsChanged` 先到）
* **提权 / 代理进程 / 启动权限**（`CreateProcessAsUser` 不可用、环境块丢失）

---

## 11. 完成定义（DoD）细则

1. `cmake --build --preset debug` 与 `--preset release` **都绿**（零新增警告；warning 当错误，直到项目
   所有者另有要求）。
2. `ctest --test-dir build/windows-debug --output-on-failure` **全绿**；新增/修改的逻辑都有对应测试。
3. 动了钩子/引擎/分发/窗口后端：跑 `scripts\acceptance.ps1`（只跑 release 那一份），结论写进
   `docs/notes/reference.md` 的「最近一次实跑」。动到脚本覆盖不到的界面（托盘菜单、日志窗口、`animate`）
   时仍要人眼过一遍。
4. **测试绝不执行真实的系统电源动作**（工作约定第 10 条）。
5. `flowkeyd --check --config flowkeyd.lua.example` 通过（零警告）。
6. 用户可见行为有变化时更新 `README.md`，有新经验时更新本文件或 `docs/notes/`；**新增/修改/删除可配置项
   时按工作约定第 13 条三处一起改**，并在提交信息里注明兼容性。
7. `git commit`：提交信息里说明**为什么**（尤其是引入新依赖时）。
8. 仓库里不留垃圾（`tmp/`、`build/`、临时配置都在 `.gitignore`）；**`build/dist-release/` 里只允许有
   `flowkeyd.exe` 与它需要的运行时**。
9. **保证 `build/dist-release/` 是最新发布包**（工作约定第 11 条）。

---

## 12. 本期不做的（有意留白）与后续工作

1. **`--simulate <SCRIPT>`**（把脚本化按键事件重放给真正的引擎）：最便宜的引擎验证手段，**建议尽早补**
   —— 不需要焦点、不装钩子，却能把匹配/吞键/重复/挂起/重映射全跑一遍。
2. **`--selftest` / `--probe`**（后端探测与自检：音频 vtable、虚拟桌面接口表、未公开 API 可用性）。
3. **完整的 `scripts/e2e.ps1`**：`acceptance.ps1` 是第一版；还缺动画采样、托盘菜单点击、自提权 UAC 流程、
   日志窗口的外部断言。
4. **鼠标钩子**（`WH_MOUSE_LL`）。
5. **延迟修饰键抑制**（让 `Ctrl+Alt+H` 也隐藏 Ctrl/Alt；相对 AutoHotkey 唯一真正的行为差距）。
6. **配置文件热重载**（去抖的 `ReadDirectoryChangesW`）。
7. **按应用限定的快捷键**（`#If WinActive(...)`）；唯一例外是远程桌面放行（全局策略 + 单条例外）。
8. **把 Lua 函数当动作**：刻意不做（声明式动作才能被 `--list` 显示、加载时校验完、跨线程安全）。
9. **配置里的 `require`/模块支持**（现在只有一份脚本）。
10. **`NumLock` 关闭时小键盘导航键与主键盘同名键的区分**（注意这是**行为变化**：`keys = "Up"` 将不再匹配
    小键盘的 `8`）。
11. **弹窗条目图标**与更细的动画；**帮助窗口的模糊搜索/按 `comment` 分组**。
12. **日志窗口增强**（`--follow`/`--grep`、`INFO`/`DEBUG` 分色）；**托盘图标跟随 explorer 重启**。
13. **把发布包再缩到更小**（静态链 Qt、单文件自解压）：要换 Qt 构建或新打包机制，不划算。
14. **程序启动器的打磨**：可配置排除表（按名字/目录过滤工具项）、最近使用优先排序、按开始菜单子目录
    分组、图标缓存到磁盘。
15. **启动器右键菜单里的命令仍然提权**（由 shell 在 flowkeyd 自己的进程里执行）：改成普通权限要把整个
    菜单宿主（消息转发 + `#32768` 菜单窗口 + `beforeInvoke`）搬到代理进程，代价与风险都不划算。

---

## 13. 在哪里扩展

**逐类改动的完整清单在 `docs/notes/extension.md`**（新动作类型 / 新选单字段 / 帮助窗口 / 窗口切换器 /
程序启动器 / 新 QML 弹窗 / 新电源操作 / 改配置模式 / 新的 `monitor` 取值 / 新 app 字段 / 新窗口条件与
跨桌面语义 / 启动器右键菜单 / 新按键与未公开 API / 虚拟桌面能力 / 在线更新 / 换图标 / 日志窗口 / 发布包
与发布说明）。三条最容易漏的：

* **新动作或新字段**要同时接 `core::actionToMap()`/`actionFromMap()`、`actionKindNames()`/
  `defaultActionMap()`、`ActionEditor.qml` 的 `visible: root.type === "…"` 分支与 `ConfigText.qml` 的中文
  名 —— 漏接界面**不会报错**，只是那个字段在界面里改不了。
* **新的 QML `import`** 先看 `cmake/PruneRuntime.cmake` 的保留名单，否则弹窗在启动预热那一步就报
  `module "…" is not installed`。
* **每个新行为都要落到能自动验证的地方**：纯逻辑进 `core` + 单测；桌面行为进 `scripts/acceptance.ps1`；
  需要真桌面的进 `tst_interactive`（直接跑单个用例：先 `$env:PATH = 'C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH`，再
  `tst_interactive.exe -o tmp\x.txt,txt <用例名>`）；只有几何/外观用 `tmp/preview/` 抓图。

---

## 14. 配置 schema 速查

**权威定义在 `README.md`「配置」那一章与 `flowkeyd.lua.example`；逐字段速查与“容易做漏的语义”在
`docs/notes/reference.md`。**

| 项 | 值 |
| -- | -- |
| 配置目录 / 日志文件 | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log` |
| 工作空间恢复日志 | `<配置目录>\workspace.json`（藏着窗口时才有；全部恢复后自动删掉） |
| DSL 全局表 | `flowkeyd.*`（挂在脚本全局） |
| 条目 | `settings{}` / `hotkey{}` / `remap{}` / `window_rule{}` / `app{}`（声明式写法 `apps`） |
| 动作 | `run`/`send`/`type`/`open`/`volume`/`media`/`clipboard`/`window`/`notify`/`menu`/`help`/`windows`/`apps`/`workspace`/`power`/`desktop`/`caps_lock`/`suspend`/`reload`/`quit`/`none` |
| 和弦 | `~` 放行原始按键、`*` 忽略额外修饰键；结构是 **修饰键 + 一个按键**；单个字母键名必须小写 |
| 启动权限 | `run`/`launch` 有 `elevated`（默认 `false` = 普通用户）；`open` 与启动器总是普通权限 |

CLI 开关：`-c/--config`、`--no-elevate`、`--console`、`--elevated`、`--check`、`--list`、`--list-keys`、
`--quit`、`--restore-windows`、`--no-autostart`、`--remove-autostart`、`--log-window`、`--log-level`、
`--log-file`、`--no-color`、`--allow-multi`、`--no-prompt`、`--updated-from`、`-h/--help`、`-V/--version`。

**本机真实配置不进仓库**：不要记录某台机器真实使用的配置内容（绑定清单、`--check` 计数、路径，以及“本机
现在跑的是哪一个 exe”）。**改真实配置不需要同步 README 或本文件。**

---

## 15. 待确认 / 需要用户拍板的事情

1. **示例配置要不要保留 `Win+X`/`Win+1..3` 这类会吞系统快捷键的例子？** 现在保留并加了醒目注释，但这是
   产品口味问题。
2. **要不要做一份“英文日志 + 中文帮助”的文案约定表？** 目前只在工作约定第 4 条里写了原则。
