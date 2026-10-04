# AGENTS.md

给 AI agent（以及人类）的 **flowkeyd** 开发笔记。

**先读这个文件。** 它记录环境、能用的命令、**已经拍板的设计决策**、不能破坏的不变量、
以及已经浪费过时间的坑。

> **本文件是 flowkeyd 唯一的工程笔记，`README.md` 是唯一的用户文档。**
> 配置 schema、动作字段与键名的权威定义在 `README.md` 的「配置」一章与
> `flowkeyd.lua.example`。任何不一致以 README 为准并修本文件。

## 工作约定（项目所有者设定）

1. 每个任务结束后更新本文件，把新经验与新要求写进去。
2. 每个任务结束后**构建 debug 与 release 两个 profile**（两条优化路径的警告都要挡住）；
   **单元测试只在 debug 上构建和运行**（`ctest --test-dir build/windows-debug`）。
   `scripts/acceptance.ps1` 只用 release 的产物。**release 构建 + debug 测试全绿才算完成**，
   纯文档任务同样适用。
   * 分工固定：**debug = 开发**（构建全部测试目标）；**release = 发布**
     （`FLOWKEYD_BUILD_TESTS=OFF`，不构建任何测试目标），并自动产出干净的发布包
     `build/dist-release/`。`build/windows-release` 或 `dist-release` 里出现测试产物、
     构建系统文件（`*.a`、`CMakeCache.txt`、`Temp`）就算 bug。
3. 目标平台是 **Windows**；可以使用未公开的 Win32 API。
4. **代码注释、本文件、README、示例配置一律用中文。日志与错误信息保持英文**
   （配置校验、`--check` 输出也一样）：日志/校验信息是机器可断言的字符串，
   而且中英混排的日志难读。用户可见的 CLI 帮助文本用中文。
5. 可以自行结束本项目（flowkeyd）正在运行的进程，不必事先征求许可。
   停法：`taskkill /PID <pid>`（**不带** `/F`）让它走干净退出；`/F` 只在杀不掉时兜底
   （代价是留下幽灵托盘图标）。这条只管 flowkeyd 自己。
6. 需要用户暂时别用这台机器时（会抢前台/焦点、会注入按键的冒烟验证），
   用提问形式提醒但**不等回答**，约 3 秒后自行继续；提醒里写清预计时长与影响范围。
7. **依赖按需引入，每次引入都要在提交信息里给出理由。** 默认只有 **Qt + `vendor/lua`**。
8. **agent 的 bash 是极简 WSL，`/bin` 下只有 `bash`/`mount`/`login`，
   `ls`/`grep`/`cat`/`head`/`tail` 全都没有。** 看文件用 read/edit 工具；
   跑命令一律走 `powershell.exe -NoProfile -Command "…"`，或绝对路径的外部程序
   （例如 `C:\Users\xingjian\scoop\apps\git\current\cmd\git.exe`）。
   **不要在命令行上直接拼中文**（会因代码页变乱码）：写成脚本文件，或只用 ASCII 模式串。
   用 bash 给 `powershell.exe -File` 做重定向时**不要写 `*>`**（bash 会拆成 `*` + `>`，
   还会在仓库根目录建出名字带反斜杠的垃圾文件）；用 `/mnt/d/…` 形式的路径 + 单个 `>`。
9. **接管已完成（2026-09）：现在常驻的就是 flowkeyd**，不要再去拉起别的实现。
   常驻实例由计划任务 `flowkeyd`（登录时 + 最高权限 + 15 秒延迟）拉起，任务指向
   **当前正在运行的 exe 路径**：守护进程每次启动都会检查并（重新）注册它。
   停止用 `flowkeyd.exe --quit`（不要 `taskkill /F`，会留幽灵托盘图标）；
   删除自启用 `flowkeyd.exe --remove-autostart`（先 `--quit`）。
   `scripts\install.ps1` / `uninstall.ps1` **已删除** —— 不要写部署脚本，
   也不要把常驻指向某个预设目录（如 `C:\Program Files\flowkeyd`）：任务跟着 exe 走。
   * **例外**：仓库根目录的 `install.ps1` 是给**外部用户**的一键安装器
     （从 GitHub Release 下载完整包、校验 sha256、解压到
     `%LOCALAPPDATA%\Programs\flowkeyd`、可选启动），不参与自启任务。
     README 最上面那条 `powershell -nop -c "irm <raw>/install.ps1 | iex"` 用的就是它。
     **这个文件必须保持纯 ASCII、无 UTF-8 BOM**：`irm` 会把 BOM 留成真实的 `U+FEFF`，
     `Invoke-Expression` 随即在 `param()` 上报 `ParserError: InvalidLeftHandSide`。
     带参数跑就用 `curl -o` 落盘 + `powershell -File`，或
     `&([scriptblock]::Create((irm <url>))) -Version …`。
     再改它时：全部字节 ≤ 0x7F、不写 BOM（行尾不用管，`core.autocrlf=true`）。
   * **本机 agent 的 `powershell.exe` 是不是提权的会变，每次都自己测**
     （`IsInRole(Administrator)`）再决定怎么拉起常驻：提权时 `Start-Process`；
     **不提权时千万不要**（守护进程自己会 `ShellExecuteW("runas")`，没人点 UAC 它会
     一直卡在 `ShellExecuteW` 里）。不提权就用已经注册好的任务：
     `& build\dist-release\flowkeyd.exe --quit` 然后 `schtasks /Run /TN flowkeyd`。
     先停再拉起很重要：不先停，新实例会弹一个「已在运行」原生框卡住。
   * 开发期起 flowkeyd 一律 `--no-elevate --allow-multi` + 一次性配置，
     并且**不要**占用用户真实配置里已有的和弦（`Win+S`、`Win+1..3`、`Win+W`、`Win+X`、
     `Win+/`、`CapsLock`、`Alt+H/J/K/L`、`Alt+Space`、`LWin+Q`、`LWin+F1..F4`、
     小键盘 `-`/`+`/`*`、`Ctrl+Alt+F4/F5/F12`）。`scripts/acceptance.ps1` 用一次性配置。
10. **任何自动化测试都不得触发真实的系统电源动作。**
    `shutdown`/`restart`/`logoff`/`sleep`/`hibernate`/`lock`/`screen_off` 一个都不许真执行；
    测试代码里不出现 `platform::win::power::execute()`（`tst_power_table` 只测纯逻辑表）。
    这些动作只有用户自己按键、或点选单条目时才允许发生。
11. **每个任务收尾时 release 产物必须是最新的**：`cmake --build --preset release` 会自动把
    干净的发布包写到 `build/dist-release/`（只有 `flowkeyd.exe` 与它需要的 Qt/MinGW 运行时）。
    常驻实例会锁住它自己那个 exe，所以收尾（两条 profile 都通过、debug 测试全绿之后）按
    这个顺序：

    ```powershell
    & build\dist-release\flowkeyd.exe --quit
    & 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
    Start-Process build\dist-release\flowkeyd.exe   # 不提权时改用 schtasks /Run /TN flowkeyd
    ```

    * 只有在常驻实例确实从 `build/dist-release` 跑的时候才需要先 `--quit`。
      它跑在别的路径（用户自己拷走的拷贝）时不要动用户的实例，只要保证
      `build/dist-release` 是最新的。
    * 纯文档任务通常 `ninja: no work to do`：什么都不用做。
    * 不想动二进制就别在提交之后再构建（提交会改分支 ref、触发重新配置 + 重新链接）。
12. **不要把某台机器上真实使用的配置文件写进仓库**：只放产品文档与参考配置
    （`README.md`、`flowkeyd.lua.example`）。真实配置的绑定清单、`--check` 计数、路径、
    以及“本机现在跑的是哪一个 exe”都不记录。**修改真实配置后不需要更新 README 或本文件。**

---

## 1. 这个项目是什么

`flowkeyd` 是一个由 **Lua 脚本**配置的 Windows 键盘钩子守护进程，用 **C++20 + Qt 6** 写成。
它安装一个 `WH_KEYBOARD_LL` 钩子，匹配按键和弦，按需把匹配到的按键从前台应用那里隐藏掉
（AutoHotkey 的行为），然后执行绑定的动作（启动进程、发送按键、控制音量/媒体、操作窗口、
切换虚拟桌面与剪贴板、弹出通知、弹出选单、弹出快捷键帮助、窗口切换器、睡眠/关机/重启/关屏）。
它也能做按键重映射。

一句话：**用 Lua 配置、用 Qt/C++ 写的 AutoHotkey。**

UI 只有托盘图标与四个 QML 卡片（日志窗口、`menu` 选单、`help` 帮助、`windows` 窗口切换器、
`update` 在线更新），全部是 **Qt Quick（QML）+ FluentWinUI3 样式**，跑在**同一个进程**里。

**当前的非目标**：图形化编辑器、鼠标钩子、把 Lua 函数当动作（配置是脚本，
但动作只能是声明式的表/字符串）。

| 方面       | flowkeyd                                                                       |
| ---------- | ------------------------------------------------------------------------------ |
| 语言/框架  | C++20 + Qt 6.11（Quick/QML 做 UI）+ 手写 Win32 声明                            |
| Lua        | **Lua 5.5.1**（`vendor/lua`，`lua/lua` @ `v5.5.1`）                            |
| DSL 全局表 | **`flowkeyd.*`** + 全局构造器                                                  |
| 配置/日志  | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log`                 |
| 注入标记   | `dwExtraInfo` 里的 `"FLOW"`                                                    |
| 互斥体     | `Local\flowkeyd-<配置路径散列>`                                                 |
| 开机自启   | 计划任务 `flowkeyd` 指向**当前运行 exe**（启动时自注册自检）                    |
| 在线更新   | 托盘菜单 *检查更新* → GitHub `releases/latest` → slim 包 → sha256 → 换 exe + 重启 |
| 自动化测试 | Qt Test 单元测试 + `scripts/acceptance.ps1`（134 项检查，需交互式桌面）         |
| 依赖管理   | CMake Presets + Ninja，`vendor/lua` 静态编进二进制                             |

---

## 2. 已经拍板的决策（不要再重新讨论）

每一条都是项目所有者明确选择的。**不确定要不要改就先问用户**，不要自作主张地“改进”。

1. **应用形态：CLI 守护进程 + QML 弹窗，没有主窗口。** `main.cpp` 走 `QApplication` +
   `QQmlApplicationEngine` + `QSystemTrayIcon`；命令行参数是唯一的控制面。
2. **Lua 用 `vendor/lua` 里现成的 5.5.1**，不换成 5.4。与 5.4 的差异见 §8。
3. **分阶段实施**（§9）；自动化测试以 Qt Test 为主，桌面行为由
   `scripts/acceptance.ps1` 覆盖，脚本覆盖不到的靠手工冒烟（§5）。
4. **配置 schema 与 CLI 开关是 flowkeyd 稳定的公开接口**：字段名、取值、动作字段、
   DSL 构造器名（`settings{}`/`hotkey{}`/`remap{}`/`window_rule{}`/`app{}`/`run()`/
   `send()`/`type_text()`/`open()`/`notify()`/`volume()`/`media()`/`window()`/
   `clipboard()`/`caps_lock()`/`suspend()`/`desktop()`/`power()`/`menu{}`/`help()`/
   `windows()`/`reload()`/`quit()`/`none()`）都要在 `README.md` 里写清楚，
   改动当**破坏性变更**对待（同步更新示例配置与 README）。脚本全局的注册表表名是
   **`flowkeyd`**。
5. **flowkeyd 最终接管这台机器**（第 10 阶段已完成）。
6. **不做 `--simulate` / `--selftest` / `--probe`**（§12）。`--check` / `--list` /
   `--list-keys` 保留（它们既是产品功能，也是手工验证的主要工具）。
   阶段 9 用**只给测试用的后门** `FLOWKEYD_ACCEPT_INJECTED=1` 抬升“丢弃注入输入”那道过滤，
   配合 `scripts/acceptance.ps1` 做验收。这是对“当时无法验证”的修订，
   **不变量 2 本身没动**：flowkeyd 自己注入的事件带着 `"FLOW"` 标记，仍在钩子回调第一步被丢掉。
7. **依赖政策：默认只有 Qt + `vendor/lua`。** 需要新依赖时按需引入，但必须在提交信息里
   给出理由。JSON、CLI 解析、字符串工具都自己写或用 Qt 自带的；不要引入 `sol2`、
   `nlohmann::json`、`CLI11`、`spdlog` 之类“顺手”的库。
8. **构建：CMake Presets + Ninja，debug 与 release 双 profile 都必须编译通过。**
   debug 里构建单测（`FLOWKEYD_BUILD_TESTS=ON`），release 里不构建单测、只产出
   `build/dist-release`。单元测试只在 debug 上跑，验收脚本只跑 release 的产品 exe。
9. **弹窗界面用 Qt 自带的标准控件，不自绘。**
   * 列表是 `ListView` + `ItemDelegate`；筛选框是真正的 `TextField`；
     **模型里要给 QML 调的方法一律加 `Q_INVOKABLE`**（否则 QML 抛 TypeError，
     而且**处理器里后面的语句会被静默跳过**，看上去像“处理器没跑”）。
   * **鼠标左键点一行 = 选中它 + 把它的按键复制走**；滚动（滚轮、拖滑块）只滚视图、
     **不动**选中项。
   * **`Enter`（或双击一行）= 关掉窗口并执行那一行的动作**。执行前先关窗是刻意的：
     `send`/`type`/`window` 作用在**前台窗口**上，不关窗就会打回弹窗自己的筛选框。
   * **`quit`/`suspend`/`power` 要按两次**（`core::isDestructive()`）：第一次只是“武装”
     （行变色 + 底部提示换确认文案），再按一次才真执行；`Esc`、上下换行、改筛选都取消。
     **`menu` 不算危险**。
   * 列表只占行区域（`topMargin`/`bottomMargin` + 不透明底色盖住滚到边缘的行），
     键盘把选中项带进视野用 `ListView.positionViewAtIndex(..., Contain)`。
   * **滚轮方向跟着系统/Qt**（本机实测 `mouseData=-120` 往下、`+120` 往上）。
   * 选单（`MenuPopup`）同一套：标准 `ListView` + `ItemDelegate`，**不滚动**
     （条目数决定卡片高度），左键点一行 = 执行它。区别只有悬停仍驱动高亮
     （`Enter` 执行光标下那一条，这是菜单的语义）。
   * **中文字体用 `font.family: "Microsoft YaHei"`**：默认族 `Segoe UI Variable` 没有中文
     字形，不管的话中文会回退到宋体。`Window`/`Item` 没有 `font` 属性、不会往下传，
     每个会画字的控件都要写（QML 的 `font` 值类型也只有 `family`，没有族列表）。
10. **开机自启：计划任务指向当前运行的 exe**（不是安装目录）。
    任务 `flowkeyd` 在登录时以**最高权限**启动一个 exe（提权但不弹 UAC），
    路径由守护进程每次启动时自检/刷新（`platform/win/autostart`，内部走
    `schtasks /Create /XML`，XML 以 UTF-16LE + BOM 写到 `%TEMP%`）。
    **不写任何安装目录、没有部署脚本。**
    * 开发/测试实例跳过：`--no-elevate`、`--allow-multi`、未提权、显式的
      `--no-autostart` 都不碰计划任务——否则临时实例会把用户的开机自启劫持到一个
      会被清理的构建目录（任务指向失效路径是**完全静默**的失败）。
    * 删除自启用 `--remove-autostart`（需管理员；先 `--quit`）。
    * 任务参数（都与当年 `install.ps1` 的 XML 逐字段一致）：`PT15S` 登录延迟、
      `ExecutionTimeLimit=PT0S`、`DisallowStartIfOnBatteries=false`、
      `StopIfGoingOnBatteries=false`、`InteractiveToken` + 指定 `UserId`、
      `MultipleInstancesPolicy=IgnoreNew`、`RestartOnFailure` PT1M×3、
      `WorkingDirectory` = exe 所在目录。**不用** BootTrigger（跑在 session 0，没桌面）、
      不用 Task Scheduler COM（要手写十来个 vtable）。
    * **注册/刷新之前先问用户**（原生确认框）；同意才动任务，拒绝就保持原样。
      `--no-prompt` 跳过询问、按默认「注册/更新」处理（脚本用）。
11. **启动时的两个交互确认**，都用**原生 `MessageBoxW`**（跑在 `QApplication` 构造之前）：
    1. **已经在运行**：同一配置文件有实例在跑就弹 `flowkeyd 已在运行`，
       点确定后以**退出码 1** 退出。**这一步刻意放在 UAC 提权之前**
       （真正的互斥体获取仍在提权之后，兜住“两个进程同时启动”的竞态）。
    2. **开机自启**：任务缺失或指向别的 exe 时弹 `flowkeyd 开机自启` 问要不要更新。

    `--no-prompt` 跳过这两个提示。守护进程出错（配置错误）弹的是 `QMessageBox`
    （标题 `flowkeyd 配置错误`，正文是英文错误）；离线命令那条路只打印、不弹窗、不提权。
12. **构建版本号 = `yy-MM-dd-<git 短修订>`**（`core/version.*`，例如 `26-09-22-42900ad`）。
    * **日期**取运行中这个 exe 自己的最后写入时间（本地时间 `yy-MM-dd`）——**不用编译期常量**
      （那会让每次构建都重新链接，`ninja: no work to do` 就不再成立）。
    * **修订**是构建时由 CMake 用 `git rev-parse --short HEAD` 取到、经
      `cmake/version_revision.h.in` 写进 `build/<preset>/generated/flowkeyd_revision.h`
      的编译期常量（只给 `core/version.cpp` 包含）。CMake 把 `.git/HEAD` 与**当前分支的
      ref 文件**登记成 `CMAKE_CONFIGURE_DEPENDS`（只在 configure 时那个 ref 确实以松散文件
      存在才会登记；在 `packed-refs` 里就会漏，提交后要显式 configure 一次）。
    * 显示在四处：托盘右键一个**不可点**的*版本*信息项、守护进程启动日志第一行、
      `--version` 第一行、`--help` 表头。取不到 exe 时日期段是 `unknown`；
      不在 git 仓库里时修订段是 `unknown`。
    * **工作流是「先提交再构建」**：哈希就是构建时的 HEAD（工作区脏时显示最近一次提交）。
13. **窗口摆放规则 `window_rule{...}`**：控制“某个程序启动时出现在哪个 workspace 和
    monitor”，且**显示器重新接入时按配置重新归位**。字段：`process`/`title`（至少一个）、
    `desktop`、`all_desktops`、`topmost`、`monitor`、`maximize`、`x`/`y`、`width`/`height`、
    `name`、`enabled`。
    * `monitor` 只支持**序号**（1 起，左→右、上→下）、`"primary"`、**设备名**
      （`"DISPLAY2"` / `"\\.\DISPLAY2"`）；不支持按分辨率匹配。
    * **默认最大化**（写了 `monitor` 又没写位置/大小时铺满工作区），位置与大小可配
      （`x`/`y` 相对目标工作区左上角）；`maximize = true` 与位置/大小互斥。
      只写 `desktop` 时**不动窗口几何**。
    * 触发时机只有三个：**窗口第一次出现**、**显示器重新接入**（判据是设备名从无到有，
      350 ms 轮询）、**flowkeyd 启动时**。之后不再干预。
    * `all_desktops`（`true` 钉在所有虚拟桌面 / `false` 显式取消，不写就不碰）走
      `IVirtualDesktopPinnedApps`（**不是** `IVirtualDesktopManagerInternal`），
      **与 `desktop` 互斥**。
    * `topmost` 走已公开的 `SetWindowPos(HWND_TOPMOST)`。这两个开关**都算“设置一次”**、
      不是几何：只写它们时窗口大小/位置不动，也不会因为“默认最大化”突然变大。
    * “主窗口”判据：可见、无属主、非 `WS_EX_TOOLWINDOW`、有标题、尺寸非零。
    * 实现分散在 `core/placement`（纯几何/匹配）、`platform/win/monitor`、
      `platform/win/desktop::moveWindowToDesktop`、`platform/win/hook`
      （`SetWinEventHook` + 显示器轮询）与 `app/dispatcher`。
14. **跨虚拟桌面的“唤起”与跟随。**
    1. `window` 动作的 `activate`：目标窗口不在当前虚拟桌面上时不算“已经在前台”，
       按下去就是**切到它所在的那张桌面并激活它**；只有它已经在当前桌面并且真的在前台时
       才是 `toggle` 的收起。
    2. `window_rule` **真的**把窗口搬到另一张桌面时，视图也跟过去并重新激活那个窗口。
       **只在“窗口第一次出现”那一遍做**（启动 / 显示器重新接入那两遍只重新摆放）。
       “真的搬动” = 窗口的桌面 GUID 变了。

    原因：`MoveViewToDesktop` 把窗口搬走之后 shell 仍然把它当**前台窗口**，
    所以“是不是已经激活”的判定不带虚拟桌面时，同一个快捷键会去*收起*用户看不见的窗口；
    而且这种情况 `SetForegroundWindow` / `AttachThreadInput` / `BringWindowToTop` 全部
    返回 TRUE 却什么都不做，必须先显式 `SwitchDesktop`。
15. **`app{...}`：把同一个程序的窗口规则与快捷键写在一起**（只是书写上的合并，
    `compile()` 把它展开成一条普通 `WindowRuleDef` + 若干 `HotkeyDef`，
    展开出来的条目**排在全局 `hotkey{}` / `window_rule{}` 之后**）。
    * 字段只有 `name`/`process`/`title`/`launch`/`window`/`hotkeys`/`enabled`；
      `process` 与 `title` 至少写一个。
    * `window` 就是一条 `window_rule`（字段完全一样），`process`/`title`/`name`
      自动继承，显式写的优先；不写 `window` 就只展开快捷键。
    * `launch` 就是这个程序怎么启动，字段与 `window` 动作的 `launch` 一样
      （`program`/`args`/`cwd`/`show`/`shell`/`env`/`wait_ms`）。它也是**默认值**：
      动作不写 `launch` 就整份继承，写了就**逐字段合并**；动作顶层的 `wait_ms`
      覆盖 `launch.wait_ms`（没有 `launch` 却写 `wait_ms` 要报错）。
    * “写了哪些字段”由 Lua 层在转换时记进 `core::LaunchFields`（`show`/`shell`/`args`
      的默认值与“没写”在值上分不开），见 §10。
    * `hotkeys` 里每一项就是一条 `hotkey`；其中的 `window` 动作自动补上 app 的
      `process`/`title`/`launch`（显式写的优先），**嵌套在 `menu` 条目里的也算**；
      只对表/构造器形式的 `window` 动作生效。
    * 名字默认：`name` → `process` → `title`；app 里**只有一个 hotkey** 时也给那条绑定
      当默认名（多个时保持“第一个和弦”的默认）。
    * `enabled = false` 把整条 app（规则 + 全部快捷键）都丢掉并给一条 warning。
    * **全局的 `hotkey{}` / `window_rule{}` 保留**。声明式写法在返回表里叫 `apps`；
      错误标签是 ``app #1 (`wps`)``（不写 `name` 就用 `process`）。
16. **`window` 动作的四个「挪窗口」op**（复用同一套目标查询、`swallow` 与 `--list` 摘要）：
    * `move_prev_desktop` / `move_next_desktop`：只动**虚拟桌面**，窗口几何不变，
      **视图不跟着走**（与 Windows 自己的 `Win+Ctrl+Shift+←/→` 同义）。两张桌面
      **首尾相接**。走 `desktop::moveWindowToAdjacentDesktop`（复用 `MoveViewToDesktop`）。
    * `move_left_monitor` / `move_right_monitor`：只动**显示器**，虚拟桌面不变。
      **保留最大化状态**；普通窗口保持原有大小并**居中**到目标工作区；最小化的只更新
      还原位置。没有更左/更右那块时失败并记日志，**不循环**。
    * 四个 op 都**不套用 `toggle`**、**不接受 `launch`**；
      `core::windowOpHasTransition()` 对跨显示器移动返回 true、对跨虚拟桌面移动返回
      false —— 写在不产生过渡的 op 上的 `animate` 会被 `--check` 报错。
    * 相邻下标由纯函数 `core::stepIndex(count, current, delta, wrap)` 算
      （虚拟桌面 `wrap = true`、显示器 `wrap = false`），有单测（`tst_placement`）。
17. **字母键名一律小写。** 配置里凡是表示按键的单个字母（`keys` 的按键、
    `remap` 的 `from`/`to`、发送脚本 `{...}` 里的键名、选单条目的 `key`）都写小写；
    `a` 就是 A 键，要按住 Shift 的“大写键”必须**显式**写 `Shift+a`，直接写大写的 `A`
    会被 `--check` 拒绝（`letter key names must be lowercase: write ...`）。
    * **例外：发送脚本里的裸字符保持 AutoHotkey 语义** —— `send("A")` 就是打出大写 A
      （等价于 `send("+a")`），`send("Hello")` 照旧能打出 `Hello`；要按字面输入任意文本用
      `type("...")` 或 `send("{Text}...")`。花括号里的是**键名**，所以 `send("{S}")` 报错、
      要写 `send("{s}")`（要 Shift 就写 `send("+s")`）。
    * 实现：`core/keys.cpp` 的 `uppercaseLetterKey()`（只识别“单个大写字母”这一种形状）
      + `parseChord` / `parseSendScript` 两处检查 + `core/config.cpp` 的 `validateMenu`
      与 `remap` 的 `to` 检查。`parseKeyOrScript()` 本身保持宽松。
      `nameFromKey()` 与 `allKeyNames()`（`--list-keys`）一律输出小写。
18. **`window` 的 `follow` 选项**（只对 `move_prev_desktop`/`move_next_desktop` 有效，
    默认 `false`）：`follow = true` 时搬完窗口**把视图也切到目标桌面**并重新激活它。
    这就是 `Win+i`/`Win+u` 与 `Win+Shift+i`/`Win+Shift+u` 的区别。
    * 配置层：`Action::follow`（`std::optional<bool>`）；`--check` 只允许它出现在这两个
      op 上（写在别的 op 上是静默空操作，所以报错）；`--list` 摘要里显示 `(follow)`。
    * 搬窗口与切视图在**同一个 STA 会话**里做完：先 `MoveViewToDesktop` 再
      `SwitchDesktop`，用**已知的目标下标**，不去读窗口的桌面 GUID（那个是异步的）。
      之后 `window::applyTo()` 再补一次 `raiseWindow()`。
    * “保持激活”是尽力而为：`raiseWindow` 失败只写 warning，动作仍算成功
      （detail 里带 `, could not activate`）。
    * **它不产生窗口过渡**（只切视图、不动几何），所以 `animate` 的规则不变。
19. **前台窗口是覆盖层时跳过它。** 不写 `target`/`process` 的 `window` 动作作用于
    `GetForegroundWindow()`；但按住 Win 约一秒会弹出 **PowerToys 的「快捷键指南」**
    （`PowerToys.ShortcutGuide.exe`，`WS_EX_TOOLWINDOW | WS_EX_TOPMOST`）并成为前台窗口，
    于是动作会去操控那个覆盖层（它不属于任何虚拟桌面，跨桌面移动直接报
    `could not read the desktop id of the window`）。现在 `window::find(foreground)`
    发现前台是别的工具窗口时，沿 Z 序往下找第一个“主窗口”（判据同上）。
    * 顺带把 `desktop` 层收紧了两处：`Session::windowDesktopId(hwnd, attempts)` 可重试
      （默认 1 次；`desktopIndexOfWindow` 用 3 次）——`GetWindowDesktopId` 在
      `MoveViewToDesktop`/`SwitchDesktop` 之后、或窗口处于过渡状态时会返回
      `TYPE_E_ELEMENTNOTFOUND`(0x8002802b)；`Session::moveWindowToAdjacent()` 搬完
      **等桌面 GUID 真的变了再返回**（否则按住 Win 连按两次会读到旧桌面、把目标算错）。
20. **托盘图标的主体是「当前是第几号虚拟桌面」。**
    * **画法**：蓝色渐变圆角方块（`logo.svg` 的 `#00d2ff` → `#3a7bd5`，圆角比例
      照抄 `rx = 56/256`）+ 白色粗体数字居中。数字按**字形墨迹**居中
      （按含行距的方框居中会明显偏高）。
    * **两位数一律显示 `9+`**（16 px 上两位数挤成一团）。画什么字、用什么字号是纯逻辑
      （`core::desktop_badge`，`tst_desktop_badge` 盯着），图片是 `app::desktopIcon()`
      （9 个尺寸各自渲染，不缩放位图）。
    * **查不到时退回应用图标**（锁屏、非交互会话、版本表对不上）：不画 `0`、不留空白图标；
      悬停提示里的 `（桌面 N/M）` 也跟着消失。
    * **来源是 500 ms 的轮询**，因为虚拟桌面没有任何“切换了”的通知。轮询放在**动作线程**上
      （`Dispatcher::startDesktopWatch()`，由 `Runtime::start()` 排队投进去——
      `QTimer` 必须在它自己那条线程上创建），查到的值经 `Runtime::reportDesktop()`
      投回 GUI 线程换图标，**绝不在 GUI 线程上做 COM**。查失败只在状态翻转时写一条
      debug 日志，并**保留上一次的数字**，免得锁屏时图标来回闪。
    * `desktop::currentDesktopIndex()` 是给这条轮询用的精简只读查询；
      `windowsVersion()` 的结果因此改成**进程内缓存**（否则每 500 ms 读一次注册表）。
    * 与 `desktop` 动作共用同一张未公开的版本表，所以**只有能读到桌面时**才有数字。
21. **窗口切换器 `windows()` 与「轻碰 Win」。** `windows()` 弹出一张卡片，列出当前打开的
    程序窗口；输入就按**进程名前缀**把窗口筛掉，筛选到**只剩一个窗口时直接激活它**。
    * 枚举判据是 `win::window::listOpenWindows()`：可见、无属主（或带 `WS_EX_APPWINDOW`）、
      非 `WS_EX_TOOLWINDOW`、有标题、尺寸非零、不是 `Progman`，**跳过 flowkeyd 自己的进程**，
      而且**只列 shell 真的会显示的窗口**（见下一条）。列表按 Z 序（最近使用在前），
      跨虚拟桌面的窗口也会列出来（激活时会把视图切过去）。
    * **被 shell 藏起来的“假窗口”不进列表**（例如 `TextInputHost.exe` 的
      「Windows 输入体验」：`IsWindowVisible` 为真、尺寸正常、却点不到）。
      `win::window::isSwitchableWindow()` = `isMainWindow()` + 「没被 cloaked，
      或者只是被搬到别的虚拟桌面上了」：`DWMWA_CLOAKED` 非零 = 没显示出来，
      而 `desktop::isWindowOnCurrentDesktop()` 又为真 = shell 藏在当前桌面上的宿主窗口。
      cloaked 但在**别的**虚拟桌面上的窗口要留着（`windows` 动作会切过去），
      所以判据里必须带上那个桌面查询；它也**只对 cloaked 的窗口问**（COM 不便宜）。
      纯判据在 `core`（`TopLevelWindowFacts` + `isMainWindow()` / `isSwitchableWindow()`），
      `platform/win/window.cpp` 只负责取 Win32 值。`window_rule` 不走这一条。
    * 筛选是**进程名的大小写无关前缀匹配**（标题只显示、不参与），**不是子串、也不是模糊
      搜索**：打 `chr` 命中 `chrome.exe`，打 `hrome` 不命中。自动激活的判据是
      「只剩一个窗口」，不是「只剩一个进程」。
    * **「轻碰 Win」= 单个修饰键 + `trigger = "release"`。** 引擎对这种和弦采用
      「tap」语义（`Engine::m_pendingTaps`）：按下修饰键本身**放行**（因此 `Win+E`/`Win+L`
      这些没被接管的系统组合不受影响），期间只要有别的按键按下就作废，单独松开时才触发；
      触发时把既有的菜单遮断标记立起来、注入 `VK_UNASSIGNED` 挡掉开始菜单。
      **单个修饰键配默认的 `trigger = "press"` 保持老行为**（按下即触发、可吞键）。
22. **窗口切换器的数字选择模式。** 筛选串命中的窗口**全属于同一个进程名**、而且不止一个时，
    卡片进入「窗口选择模式」：前 10 行依次分到 `1`..`9`、`0`，按一下数字直接跳过去；
    超过 10 个的不分配（`rowKey` 是空串，那一行不画徽标）。
    * 触发判据：**筛选串非空 + 可见窗口全属于同一个进程名 + 至少两个窗口**。
      空筛选绝不编号；命中多个进程名时也不编号。与 `setFilter()` 的「只剩一个窗口就直接
      激活」互不冲突（那边是 1 个窗口，这边至少 2 个）。
    * 这种模式下 `handleKey()` 先把 `Qt::Key_1`..`Key_9`/`Key_0` 接掉，所以它们**不会跑进
      筛选框**；没有对应行的数字被吃掉但什么都不做。不在这种模式时数字键照旧放行给
      `TextField`（`7zip` 这类进程名要能用数字筛）。
    * 纯逻辑在 `WindowListModel`（`m_numbered`/`m_rowKeys`，角色 `rowKey`；
      `footerText` 在这种模式下换成「数字键直接切换」，所以从 `CONSTANT` 改成
      `NOTIFY stateChanged`）。界面只是把 `rowKey` 画成行左侧的徽标（空串时不占位，
      标题与进程名跟着缩进 30 px）。
23. **四个弹窗不进任务栏，而且启动时就预热好。**
    * **不进任务栏**：`MenuPopup.qml` / `HelpPopup.qml` / `SwitchPopup.qml` /
      `UpdatePopup.qml` 的 `flags` 都加 `Qt.Tool`（Windows 上就是 `WS_EX_TOOLWINDOW`）。
      `Qt.Tool` 窗口仍然能被激活、能拿键盘焦点（验证过），只是不加任务栏按钮、
      不进 `Alt+Tab`。日志窗口**没有**改（它留在任务栏里是对的）。
      选 `Qt.Tool` 而不是 `Qt.Popup`：后者要抓鼠标、点外面自动关。
    * **首次弹出不再现场付钱**：`PopupHost::preload()` 在启动后的第一个事件循环回合被
      `main` 排队调用，把四个窗口建出来、填一份假数据各渲染一帧
      （**透明度 0 + 屏幕之外**，用户看不到也点不到），首帧到了就藏起来。
      实测冷启动第一次弹出要 **224 ms** 才画出第一帧（~170 ms 是进程首次渲染的固定开销：
      QRhi/D3D11 设备、交换链、Quick 材质着色器首次编译；~50 ms 是该窗口的 QML 加载与
      样式装配），预热之后第一次弹出 **~30 ms**。
    * 预热用的假数据**不需要**清理：每次真实弹出都会先 `setItems` 覆盖。行数写得多于一屏
      （menu 8 行、help/switch 14 行）是为了让 `ListView` 把一屏的 `ItemDelegate` 与
      `ScrollBar` 也装配一遍。
    * **屏幕之外是必须的**：透明度 0 的置顶窗口留在屏幕里万一赶上鼠标点击就会吃掉它。
    * 真实弹出要先 `cancelPreload()`（用户可能在启动后的那 300 ms 里就按了快捷键）；
      用 `QSet<QQuickWindow*> m_warming` 判断，**不要用全局“代”号**
      （会把另外几个还在预热的窗口的首帧回调一起废掉）。
24. **窗口切换器卡片的几处调整。**
    * **没有标题行**：卡片里只剩筛选框、列表与底部提示；筛选框就是第一行，
      `listTop` = **50**（`kPad 12 + 筛选框 30 + 间隙 8`），卡片高度 238（3 行时）。
      计数挪到了**底部提示**里（省掉与筛选框占位文本重复的「输入筛选」）；`countText()` 保留。
    * **每一行是「进程名在上、窗口标题在下」**（项目所有者 2026-09 要求）：位置与样式
      一起对换 —— 主行是进程名（`pointSize 10.5` + `palette.text`），窗口标题退成副行的
      浅色小字（`8.5` + `palette.placeholderText`）。两个字符串还是只有都非空时才分两行。
      理由：筛选匹配的本来就是进程名，标题只用来分辨同一个程序的多个窗口。
      这只是 QML 里两个 `Label` 的上下关系，**模型层不知道也不管**。
    * **卡片开着时再按一次同一个快捷键 = 关掉它**（与 `Esc` 同义）。判断在
      `PopupHost::showSwitch()`：`window->isVisible() && !warming` 时直接 `switchDismiss()`
      返回，**不要在那里重新弹一次** —— 「轻碰 Win」的触发发生在 Win 键**松开**时，
      所以第二次轻碰进来时卡片正开着；重新弹就是「关掉又立刻重开」。`windows()` 动作
      因此是「开/关」切换，不是单纯的「开」。
    * `windows()` 的 `title` 参数现在只用作**窗口标题**（`caption()` =
      `<title> — N 个`），验收/诊断脚本靠它读“现在列了几个窗口”。
    * **列表宽度 = 筛选框宽度**：QML 里 `ListView` 的 `x`/`width` 直接用 `filterRect`。
    * **打开时切英文输入法**：筛选框匹配的是**进程名**（ASCII），而用户经常开着中文输入法。
      新模块 `platform/win/ime.*`（运行时解析 `imm32.dll`）：读
      `ImmGetConversionStatus`，`NATIVE`（`IME_CMODE_NATIVE`）置位时把它清掉
      （其余标志与句模式原样保留，等价于用户按一下 `Shift`）。
      Qt 在 Windows 上**不看** `Qt.ImhPreferLatin`（`QWindowsInputContext` 只用
      `ImEnabled` 决定要不要 `ImmAssociateContext`），所以 QML 里写提示没用。
      调用点两处：`PopupHost::showSwitch()` 弹出后一次，以及筛选框
      `onActiveFocusChanged` 时一次；**预热期间不调**。
      **关掉卡片时把打开前那份模式写回去**（`ime::readMode()` 快照、
      `ime::restoreMode()` 写回；调用点在 `switchChoose`/`switchDismiss`/`closeAll`）。
      快照只在真正弹出时记一次，所以 QML 那两次反复调用不会覆盖它。
      只影响 flowkeyd 自己这个进程的输入模式，**但本进程的几个窗口会互相看见**
      （在帮助窗口里按 `Shift` 切成中文之后，切换器窗口读到的状态也带 `NATIVE` 位）。
25. **在线更新：托盘菜单里点一下，自己换 exe 并重启。**
    * **下载的是发布脚本已经传上去的精简升级包**（`*-slim-windows-x64.zip`，约 700 KB，
      里面只有 `flowkeyd.exe`），没有第三种资产。解压用 Qt **私有**的 `QZipReader`
      （`Qt6::CorePrivate`）；某次发布万一没有 slim 包就退回到完整包。
    * **只换 `flowkeyd.exe`，不动 Qt/MinGW 运行时。** 新版本如果新增了运行时依赖，
      替换会**回滚**（新 exe 活不过 2.5 秒 → 把旧的那份改回来）并提醒用户手动下载完整包。
    * **触发方式是手动的**：只有点托盘菜单才联网，启动时不检查。匿名
      `GET /releases/latest`，请求带 `User-Agent` 与 `Accept: application/vnd.github+json`。
    * **替换顺序**：旧 exe 改名成 `<exe>.old` → 新的改名就位 → 启动新实例 →
      等 2.5 秒确认它还活着。运行中的映像可以改名（不能删/覆盖）。任何一步失败都回滚；
      `.old` 由**新实例**启动时删（旧进程还在退出时删不掉就隔 0.7 秒重试，最多 20 次）。
    * **`--updated-from <旧版本>` 是重启时唯一的额外参数**（内部标记）：新实例靠它知道
      “我刚被更新过”，于是弹一条托盘通知，并且**只认它**（用户自己重启不会重复提示）。
    * **下载下来的 exe 的 mtime 被对齐到发布日**（版本号里的日期段取 exe 的 mtime，
      见第 12 条）：不对齐的话更新完重启，版本号会变成“下载那一天”。
    * 替换需要能写安装目录（默认提权运行，正常路径没影响）；更新**不碰**配置、日志与
      开机自启任务。更新失败时的模态框在 `--no-prompt` 下不弹（自动化用）。
    * 新增静态库 **`flowkeyd_update`** 与 `platform/win/update.*`；第四个 QML 卡片
      `UpdatePopup.qml`（同样预热、同样 `Qt.Tool`）；`Qt6::Network` 进入依赖，
      所以 `cmake/PruneRuntime.cmake` **必须留下 `tls/qschannelbackend.dll`**。
26. **远程桌面放行（`settings.remote_desktop`，默认开）。**
   前台窗口的属主进程命中名单（默认只有微软的 RDP 客户端：`mstsc.exe`、`msrdc.exe`、
   `msrdcw.exe`、`RdClient.Windows.exe`）时，**所有快捷键与重映射一律放行** ——
   不拦截、不触发，键原样送给对面那台机器；单条例外是条目自己的
   `remote_desktop = true`（在远程桌面里也照常拦、照常执行）。
   * 判据只有「**前台窗口的属主进程名**」（大小写无关的子串，与 `window_rule.process`
     同一套），不看标题、不看是否全屏。`GetSystemMetrics(SM_REMOTESESSION)` 那条路
     （“flowkeyd 自己跑在远程会话里”）**没做**：被 RDP 进来操控是另一种场景，需要时
     再加一个信号。
   * 检测全在**钩子线程**上：`SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` 立刻更新
     （故意**不带** `WINEVENT_SKIPOWNPROCESS`：自家弹窗拿到前台就算“不在远程桌面里”）、
     启动时查一次、350 ms 的 placement tick 再兜一次（前台事件漏了也能纠回来）。
     按键路径上没有额外开销（引擎里只是一个 bool 与一次判断）。
   * 名单空表 = 谁都不算（等于关掉）；`processes` 里的**空串是错误** —— 空串在
     `windowProcessMatches()` 里表示“不限制”，会让**所有**窗口都算远程桌面。运行时也
     跳过空条目（双层保险），见 §10 的坑。
   * 进入这个状态时清掉长按重复与待定的「轻碰 Win」，并把**已经被放行**的重映射按住的
     目标键松开（`remote_desktop = true` 的例外不动）；已经吞掉、还按着的键在松开时仍然
     吞掉 —— 否则前台会看到一个孤立的 key-up。
   * 纯逻辑在 `core/remote_desktop.{h,cpp}`（`builtinRemoteDesktopProcesses()` /
     `isRemoteDesktopProcess()`），引擎侧是 `Engine::setRemoteDesktop()`，平台侧只有
     `platform/win/hook.cpp` 的 `noteForegroundWindow()`。

---

## 3. 环境与工具链

|            |                                                                |
| ---------- | -------------------------------------------------------------- |
| 开发环境   | 原生 Windows 10.0 build 26200 (x64)，交互式桌面会话             |
| 工程路径   | `D:\prj\flowkeyd`                                              |
| Qt         | `C:\Qt\6.11.2\mingw_64`（只有 mingw_64 这一套）                 |
| C++ 编译器 | `C:\Qt\Tools\mingw1310_64\bin\g++.exe`（GCC 13.1.0）            |
| CMake      | `C:\Qt\Tools\CMake_64\bin\cmake.exe`                            |
| Ninja      | `C:\Qt\Tools\Ninja\ninja.exe`                                   |
| Qt Creator | kit `Desktop_Qt_6_11_2_MinGW_64_bit_Debug`，产物 `build/Desktop_Qt_…_Debug` |
| 未安装     | Visual Studio / MSVC、MSYS2、独立的 mingw                       |
| Lua        | `vendor/lua` = `lua/lua` **v5.5.1**（`7579fc9d`）               |

* `vendor/lua` 的 32 个 `.c`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`）用
  `-O2 -DLUA_USE_WINDOWS` 全部编译通过。Qt 侧齐备：`Qt6::Quick`、`QuickControls2`、
  `Qt6QuickControls2FluentWinUI3StyleImpl`、`Qt6::Test`、`Qt6::QuickTest`、`Qt6::Widgets`
  （`QSystemTrayIcon` 要用）、`Qt6::LabsPlatform`、`Qt6::Network`。
* **本机是两块 1920x1080@200% 的屏（物理 3840x2160）**，`availableGeometry()` 可能比
  `geometry()` 还宽；主显示器逻辑尺寸约 533x1095（225% 缩放下）。弹窗是按**光标所在那块屏**
  居中的（`centreOnCursorScreen()`）。

### 依赖政策：什么可以静态链接，什么必须运行时解析

* **Qt 的模块照常 `find_package` + `target_link_libraries`**：`Core`/`Gui`/`Qml`/`Quick`/
  `QuickControls2`/`Widgets`/`Test`/`Network`，外加一个 **Qt 私有**模块
  `Qt6::CorePrivate`（**只**为了 `<QtCore/private/qzipreader_p.h>`）。
  官网装出来的 Qt 不会因为 `COMPONENTS Core` 就自动加载私有模块，需要在
  `find_package(Qt6 …)` **之前**设 `QT_FIND_PRIVATE_MODULES ON`。
* **允许静态链接的集合**（MinGW 工具链自带这些导入库）：
  `user32`、`kernel32`、`shell32`、`ole32`、`ntdll`、`advapi32`、`powrprof`、`uxtheme`、
  `comctl32`、`shlwapi`。
* **必须 `LoadLibraryW` + `GetProcAddress` 运行时解析**：未公开入口
  （`win32u!NtUserSendInput`、`NtUserGetAsyncKeyState`……，没有 `libwin32u.a`）
  以及任何不在上面那个集合里的 DLL（`dwmapi`、`imm32`、`d3dcompiler`…）。
  理由：这些接口没有 ABI 承诺，少一条静态依赖就少一个“在某些机器上 exe 根本起不来”的机会。
* **COM 接口手写 vtable**（Core Audio `IAudioEndpointVolume`、shell 的
  `IVirtualDesktopManagerInternal`/`IVirtualDesktopPinnedApps`）。**这是本仓库风险最高的
  两块代码**：vtable 布局写错不是返回错误码，而是崩溃。接口定义以真机验证为准，
  虚拟桌面那张表按 `build.revision` 索引。
* 不引入会藏起未公开入口的封装层 —— 未公开 API 一律自己 `GetProcAddress`。

---

## 4. 代码地图

| 路径 | 职责 |
| ---- | ---- |
| `CMakeLists.txt` / `CMakePresets.json` | 顶层工程、Qt 查找、`qt_add_executable`、`qt_add_qml_module`、部署；两个 preset（Ninja + mingw1310_64 + Qt 6.11.2） |
| `cmake/VendorLua.cmake` | 把 `vendor/lua` 编成静态库 `lua_static`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`，定义 `LUA_USE_WINDOWS`，`AUTOMOC/AUTOUIC OFF`） |
| `cmake/PruneRuntime.cmake` | **发布运行时精简清单**（`cmake -P` 脚本）：windeployqt 之后删掉用不到的 Quick Controls 样式、qmltooling、`opengl32sw.dll`、`D3Dcompiler_47.dll`、`plugins.qmltypes`、FluentWinUI3 磁盘上与插件重复的 .qml/.png，release 再 `strip`。**它是“发布包里有什么”唯一的家**（§10「发布包精简」） |
| `cmake/version_revision.h.in` / `cmake/RunQTest.cmake` | git 修订的编译期常量；测试输出重定向（QtTest 输出拿不到，见 §10） |
| `flowkeyd.lua.example` | 有文档、覆盖全部特性的参考配置（中文注释、`--check` 零警告） |
| `README.md` | **用户文档（只写使用方法）**：安装、快速上手、命令行、配置/动作/schema 全部字段、托盘与弹窗、开机自启、在线更新、已知限制。**配置 schema 的权威定义** |
| `install.ps1` | 对外一键安装器（**纯 ASCII、无 BOM**，见工作约定第 9 条） |
| `logo.svg` / `assets/` / `tools/icon_gen` | 图标美术源（唯一真源）；`assets/flowkeyd.ico`（exe 资源，windres 嵌进 PE）+ `assets/icons/flowkeyd-<n>.png`（qrc 里的多尺寸 `QIcon`）；`tools/icon_gen`（`EXCLUDE_FROM_ALL` 的 `icons` 目标，把 svg 光栅化） |
| `src/main.cpp` | `AttachConsole` + `QT_QPA_PLATFORM=windows:fontengine=freetype`（**第一行**）+ CLI 分发 + 日志初始化 + 提权前的单实例预检 + 自启确认框 + 组装 Runtime + 接 `desktopChanged` 到 Tray + 排队 `PopupHost::preload()` |
| `src/cli.h/.cpp` | 参数解析 + 中文帮助文本（手写）；`--quit` 走单独早期分支 |
| `src/core/` | **纯逻辑层：不碰 Win32、不碰 Qt GUI**（只用 QtCore），能被 Qt Test 直接测 |
| `src/core/keys.*` | 键名 ↔ `VK` 表、`Modifiers`、`Chord`、AutoHotkey 发送脚本解析、小键盘 Enter 的内部伪码 `0x100`、`key_from_hook()`/`native_key()`、大写字母键名检查 |
| `src/core/config.*` | 配置结构体、严格校验（未知字段要报错）、编译成 `Compiled`/`Binding`/`CompiledRemap`、配置文件搜寻与旧 TOML 的迁移提示、`AppDef` + `expandApps`/`applyWindowDefaults` |
| `src/core/engine.*` | 快捷键状态机：匹配、优先级、吞键、自动重复抑制、长按重复、挂起、重映射 hold/tap、Win/Alt 菜单遮断、单个修饰键的「轻碰」语义 |
| `src/core/action.*` | 声明式动作的表示 + 摘要文本（`--list` 与 `help()` 都用它）+ `isDestructive()` |
| `src/core/template.*` | `{clipboard}`、`{selection}`、`{date}` 等占位符展开 |
| `src/core/window_match.*` | 窗口匹配与 `window` 动作决策的纯函数 + 「什么算一个程序窗口」的纯判据（`TopLevelWindowFacts`、`isMainWindow()`、`isSwitchableWindow()`） |
| `src/core/remote_desktop.*` | 「这个前台进程算不算远程桌面客户端」的纯逻辑：内置名单 + 子串匹配（§2 第 26 条） |
| `src/core/placement.*` | `window_rule` 的纯逻辑：显示器排序与选择、重连检测、摆放几何、规则匹配、`stepIndex()` |
| `src/core/log_tail.*` | 日志文件的增量尾随（纯逻辑）：按字节读、末尾不完整的 UTF-8 序列不消费、半行留到下一轮、一次最多 1000 行 |
| `src/core/update_check.*` | 在线更新纯逻辑：仓库地址、`releases/latest` JSON 解析、资产挑选、版本比较、`buildVersionDate()` |
| `src/core/version.*` / `src/core/desktop_badge.*` | 构建版本号；托盘数字徽标的文字与字号 |
| `src/lua/lua_config.*` + `src/lua/lua_prelude.lua` | **Lua 与 C++ 的唯一边界**（转成 `core::Config`，逐条目、带上下文的错误；BOM 剔除、`.toml` 拒绝）；注入配置脚本的 DSL（纯 Lua，改它不需要改 C++，编进 qrc） |
| `src/platform/win/` | Win32 后端，每个文件只做一件事 |
| `ffi.h/.cpp` | 全部 Win32 声明、结构体与常量（`INPUT` 的 40 字节布局有 `static_assert`） |
| `nt.h/.cpp` | 未公开的 `win32u.dll` 导出，运行时解析并校验 |
| `dwm.h/.cpp` | 运行时解析的 `dwmapi`：`DwmSetWindowAttribute`（关过渡动画）、`DwmGetWindowAttribute`（读 `DWMWA_CLOAKED`）。拿不到时只是保留动画 / 按“它在显示”处理，动作不失败 |
| `monitor.h/.cpp` | 显示器枚举、窗口在哪块屏、`applyPlacement`（`SetWindowPlacement` + `SetWindowPos`，带 `SWP_NOACTIVATE`，最大化时先还原再最大化）。**几何判断不在这一层** |
| `input.h/.cpp` | 按键注入（`SendInput`/`NtUserSendInput`）、按键状态、`ModifierGuard`（含菜单遮断标记）、`FLOWKEYD_ACCEPT_INJECTED` 测试后门、`copySelection` |
| `ime.h/.cpp` | 运行时解析的 `imm32.dll`：`readMode`/`useAlphanumericMode`/`restoreMode`。拿不到 `imm32` 或没有输入上下文时**不当错误** |
| `hook.h/.cpp` | 钩子回调、**钩子线程自己的 Win32 消息循环**、`SetTimer`、控制消息、重载；还有 `window_rule` 的两个监听：`SetWinEventHook`（`EVENT_OBJECT_SHOW`/`DESTROY`，按 HWND 去重）与 350 ms 显示器轮询，外加 `EVENT_SYSTEM_FOREGROUND`（远程桌面放行，`noteForegroundWindow()`）。**定时器 id 必须用 `SetTimer` 的返回值**（§10） |
| `audio.h/.cpp` | Core Audio `IAudioEndpointVolume`，手写 COM vtable（**高风险**，MTA） |
| `clipboard.h/.cpp` | 剪贴板读写（`CF_UNICODETEXT`，`OpenClipboard` 重试 10 次） |
| `window.h/.cpp` | 窗口查找/激活/最小化/最大化/还原/关闭/置顶、前台锁绕行、启动回退、`TransitionGuard`（RAII）、`setTopmost`、`isMainWindow`/`isSwitchableWindow`/`listOpenWindows`。**“是否已经激活”还要看虚拟桌面**；**前台查询会跳过 `WS_EX_TOOLWINDOW` 覆盖层** |
| `desktop.h/.cpp` | 虚拟桌面切换、窗口移动与钉在所有桌面：`IVirtualDesktopManagerInternal`（按 `build.revision` 查表）+ `IVirtualDesktopPinnedApps`（IID 不随版本变，不进表）。`moveWindowToDesktop`、`setWindowPinned`、`switchToWindowDesktop`、`currentDesktopIndex()`、`windowsVersion()`（进程内缓存） |
| `power.h/.cpp` | `SetSuspendState`、`ExitWindowsEx`、`LockWorkStation`、`SC_MONITORPOWER` 广播，外加 `SeShutdownPrivilege` |
| `tray.h/.cpp` | 托盘图标 + 气泡 + 右键菜单（查看日志/挂起/重载/打开配置/检查更新/版本/退出）+ 悬停提示；`setDesktop()` 换成数字徽标 |
| `logging.h/.cpp` / `single_instance.h/.cpp` | 英文、分级别、可选 ANSI 颜色的日志器；按配置路径散列命名的互斥体 + `--quit` 的命名事件通道 |
| `elevate.h/.cpp` / `autostart.h/.cpp` / `update.h/.cpp` | `ShellExecuteW("runas")` 自提权 + 降级；计划任务（纯函数 `buildTaskXml`/`taskXmlCommand`/`sameExecutablePath` + `ensureAutostart(spec, confirm)`）；换 exe 的最后一步（改名 → 就位 → 启动 → 2.5 秒确认 → 失败回滚） |
| `src/app/` | 组装层：把 core/lua/platform 串起来，并拥有 Qt 对象 |
| `dispatcher.h/.cpp` | **动作工作线程**（`QThread`）：执行动作列表、`window` 的“先启动再激活”与默认开的 `toggle`、`menu`/`help`/`windows` 的窗口请求、`window_rule`（三遍）、`startDesktopWatch()`/`pollDesktop()` |
| `runtime.h/.cpp` | 引擎 + 钩子 + 分发 + 托盘 + 弹窗的总装；`ControlCmd`（suspend/reload/quit）通道；`--quit` 的事件句柄（`QWinEventNotifier` 在 GUI 线程上监听）；`reportDesktop()`/`desktopChanged`；`showSwitchFromAnyThread()` 等 |
| `log_model.h/.cpp` | 日志窗口的模型：尾随日志文件、最多 1000 行、按级别配色、子串过滤 |
| `menu_model.h/.cpp` / `help_model.h/.cpp` / `window_list_model.h/.cpp` | 三个卡片的**纯逻辑**（`QAbstractListModel`，只用 QtCore）。行几何与鼠标命中**不归它们管**（`ListView` + `ItemDelegate`）；`help` 只管筛选/选中项/`Enter`/`Esc`/`setSelected`；`menu` 还持有悬停（`Enter` 执行光标下那一条）；`window_list` 管进程名前缀筛选、自动激活与数字选择模式 |
| `popup_layout.h/.cpp` / `popup_host.h/.cpp` | 弹窗共用的几何类型与 `centrePopup()`（先在工作区居中、再夹进屏幕）；把模型挂到 QML 窗口上、抢前台、在 GUI 线程上创建/复用窗口、`helpRun()`（可见行下标 → 条目下标，**先藏窗口再执行**）、`preload()`、`switchUseEnglishInput()`/`restoreSwitchInputMode()` |
| `update_model.h/.cpp` / `update_archive.h/.cpp` / `updater.h/.cpp` | 更新卡片的状态机（八个阶段、版本号/发布说明/进度/按钮可见性，**不联网不解压不换文件**）；从 zip 里取出新 exe（`QZipReader` + PE 魔数检查）；联网编排（异步 `QNetworkAccessManager` + sha256 + 解压到 `<exe>.new` + mtime 对齐发布日） |
| `app_icon.h/.cpp` | 把 qrc 里的 9 张 PNG 帧拼成多尺寸 `QIcon`（`applicationIcon()`）；`desktopIcon(number)` 现画桌面号徽标 |
| `src/qml/` | `LogWindow.qml`、`MenuPopup.qml`、`HelpPopup.qml`、`SwitchPopup.qml`、`UpdatePopup.qml`。都写 `pragma ComponentBehavior: Bound`；**四个弹窗的 `flags` 都带 `Qt.Tool`**；配色一律用 `palette`（没有单独的 `Style.qml`）；中文一律 `font.family: "Microsoft YaHei"`；列表全部是标准 `ListView` + `ItemDelegate`（+ `ScrollBar`） |
| `tests/` | Qt Test：`tst_keys`、`tst_engine`、`tst_config`、`tst_lua`、`tst_template`、`tst_send_script`、`tst_window_match`、`tst_remote_desktop`、`tst_log_tail`、`tst_audio`、`tst_autostart`、`tst_menu_model`、`tst_help_model`、`tst_window_list_model`、`tst_power_table`、`tst_desktop_table`、`tst_placement`、`tst_layout`、`tst_version`、`tst_desktop_badge`、`tst_update`、`tst_update_model`、`tst_update_install`、`tst_command_line`、`tst_instance`、`tst_input`，以及需 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 的 `tst_interactive`（真机：剪贴板/音量/窗口/虚拟桌面/钉住/置顶/输入法/覆盖层/更新下载；联网那条还要 `FLOWKEYD_ALLOW_NETWORK_TESTS=1`） |
| `scripts/acceptance.ps1` | 桌面行为验收（注入按键 + 焦点捕捉窗口的外部观察，134 项检查），需交互式桌面，**不属于 `ctest`** |
| `scripts/release.ps1` | 构建 release + 打包（完整包 + 精简升级包，各附 `.sha256`）+ 用 `gh` 上传 GitHub Release。tag 取刚构建的 exe 的 `--version`。工作区脏或 HEAD 没推到 origin 会直接拒绝（要 `-AllowDirty`/`-Push`）。**唯一的新前置依赖是 `gh`**。开关：`-SkipBuild`/`-SkipResident`/`-SkipUpload` |

> `scripts/install.ps1` / `uninstall.ps1` **已删除**：自启的注册、刷新与删除现在全在
> `src/platform/win/autostart.*` 里，由守护进程自己在启动时做。

### CMake 目标划分

| 目标 | 内容 | 谁链接 |
| ---- | ---- | ------ |
| `flowkeyd_core` | `src/core/*`（纯逻辑，只用 QtCore） | exe + 全部单测 |
| `flowkeyd_lua` | `src/lua/*` + 编成 qrc 的 `lua_prelude.lua` | exe + `tst_lua` |
| `flowkeyd_models` | `src/app/{menu,help}_model.*` + `window_list_model.*` + `popup_layout.*` | exe + 三个 model 测试 |
| `flowkeyd_update` | `src/app/{update_model,update_archive,updater}.*`（`Qt6::Core` + `Network` + **`CorePrivate`**） | exe + 三个 update 测试 + `tst_interactive` |
| `flowkeyd_platform` | `src/platform/win/*`（不碰 Qt GUI 的 Win32 后端） | exe + 平台层单测 |
| `flowkeyd` | `src/main.cpp`、`src/cli.*`、`src/app/*`、`tray.*`、QML、图标 qrc + 图标 .rc | —— |
| `flowkeyd_icon_gen` | `tools/icon_gen/main.cpp`（`EXCLUDE_FROM_ALL`，只由 `icons` 目标手工构建） | —— |

* `flowkeyd_add_test(name [LIBS …])` 把 Qt/MinGW 的 DLL 目录写进 test 的 `PATH`。
  **测试目标只在这个函数被调用时创建，整段（连 `enable_testing()`）都包在
  `FLOWKEYD_BUILD_TESTS` 里**：debug preset 给 `ON`、release preset 给 `OFF`。
* `src/app/popup_host.*` 用 QML/QtQuick，所以**不进** `flowkeyd_models`，留在 exe 里。
  在线更新**单独一个库 `flowkeyd_update`**（它是唯一需要 `Qt6::Network` 与 Qt 私有头的东西）。
* **exe 的产物目录是自包含且精简过的**：`flowkeyd` 上挂一条 `POST_BUILD` 的
  `windeployqt`（`--qmldir src/qml` 必须给，否则 QML 模块不会被拷过去），紧接着跑
  `cmake/PruneRuntime.cmake`。只对 `flowkeyd` 做，**不给测试可执行文件做**。
  release profile 还额外把干净的发布目录写到 `build/dist-release/`：**212 个文件 / 63.0 MB**
  （精简前 1378 个 / 149.8 MB），可以直接拷到别的机器上跑。
  `flowkeyd` 上还挂了 `LINK_DEPENDS`（`cmake/PruneRuntime.cmake`）：改了精简清单就会重新链接。
* **分层铁律**：`src/core/`、`src/app/{menu,help,window_list}_model.*`、
  `src/app/popup_layout.*`、`src/core/update_check.*` **不许出现 `<windows.h>`、
  不许出现 QML/QtWidgets、不许出现窗口句柄**。这正是 `--check`/`--list` 能在没有桌面的
  情况下跑、以及单元测试能覆盖核心逻辑的原因。`platform/win/window.cpp` 里“候选窗口如何
  匹配”这种判断要拆成纯函数放进 `core`。

---

## 5. 构建、测试与运行（DoD）

### 每个任务都要跑的

```powershell
$C = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'

& $C --preset windows-debug          # 首次
& $C --preset windows-release        # 首次

& $C --build --preset debug
& $C --build --preset release

& ctest --test-dir build/windows-debug --output-on-failure   # 单测只在 debug 上

# 联网用例（会真的去 GitHub 查一次并下载 slim 包，只落到临时目录）
$env:FLOWKEYD_ALLOW_NETWORK_TESTS = '1'
& build\windows-debug\tst_interactive.exe checksAndDownloadsAnUpdateFromGitHub

dir build\dist-release               # 发布包（release 构建自动产出，拷走就能跑）
```

**任务收尾只需要保证 `build/dist-release/` 是最新的发布包**（工作约定第 11 条）。
**零警告、零失败**（仓库范围内 `-Wall -Wextra -Werror`，`vendor/lua` 例外）。

### preset 要点

```jsonc
// windows-debug:  CMAKE_BUILD_TYPE=Debug,          FLOWKEYD_BUILD_TESTS=ON
// windows-release: CMAKE_BUILD_TYPE=RelWithDebInfo, FLOWKEYD_BUILD_TESTS=OFF
// 产物 build/windows-debug、build/windows-release；Ninja + GCC 13.1 + Qt 6.11.2
```

> release 用 `RelWithDebInfo`（不是 `Release`）：它走另一条优化路径、能暴露 debug 看不到的
> 警告，同时保留符号便于看栈（发布时会 `strip`）。

### 运行（开发期一定要带这两个开关）

```powershell
& build/windows-debug/flowkeyd.exe --no-elevate --allow-multi --console --config .\tmp\smoke.lua
```

* `--no-elevate`：守护进程模式默认提权，开发期一律关掉。
* `--allow-multi`：跳过单实例检查。注意每个实例都装一个 `WH_KEYBOARD_LL` 钩子，
  **后装的先收到事件**，所以调试时只开一个真正需要吞键的实例。
* `--console`：保留控制台输出。

**离线命令（绝不允许提权）**：`--check` / `--list` / `--list-keys`。提权判断必须在这些命令
`return` 之后。`--quit` 也不提权（它只去通知一个已经在跑的实例），但它会碰另一个进程；
`--remove-autostart` 要管理员权限才能删任务。

开发实例与常驻实例的单实例锁按**配置文件路径**分开，互不影响。

### 桌面行为怎么验证

```powershell
# 134 项检查，约三分钟，会持续注入按键/抢焦点；按工作约定第 6 条先提醒用户
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1
powershell.exe ... -Phase config      # 只看配置，不注入按键
```

它自己生成一份**一次性配置**，起一个非提权的守护进程，从另一个上下文用 `SendInput`
注入按键，再用一个获得焦点的 WinForms 窗口从外面观察“按键到底有没有到达前台”
（未绑定的键做正对照）。要让注入的按键触发绑定，守护进程得用
`FLOWKEYD_ACCEPT_INJECTED=1` 启动（只给测试用的后门，启用时日志里有一条警告）。

脚本**不做**的：动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程、“托盘图标真的消失了”
的直接观察，以及**一切电源动作**。这几项仍然只能靠人的手。

「轻碰 Win」（`keys = "LWin"` + `trigger = "release"`）这条路径由脚本里
「窗口切换器」那一段兜住：一次轻碰弹卡片、**再轻碰一次关掉**（与 `Esc` 同义）、
卡片不在任务栏里，而且 Win 松开后前台没有被外壳抢走（遮断标记生效的判据）。
2026-10 加上这 6 条后跑过一次全绿（`checks: 125, failures: 0`）。2026-10-04 又加上
「远程桌面放行」那 9 条（名单里写的是捕捉窗口自己那个进程，因此不碰真 RDP 客户端），
全绿：`checks: 134, failures: 0`。

**桌面被锁住时（`LogonUI` 在跑）脚本必然挂**：`GetForegroundWindow()` 返回 0，
`SendInput` 报 `5`（ACCESS_DENIED）。这不是产品 bug，先去解锁再跑。
还有另一种“判据全对、注入就是不落地”的状态（RDP 会话没真正接收输入）：
先看 `GetForegroundWindow()` 是不是 0，是的话就别在产品代码里找原因。

### 手工冒烟清单（脚本覆盖不到的部分）

1. `--check --config tmp/smoke.lua` 通过，`--list` 形状与 README 一致。
2. 被吞掉的 `Win+…` 和弦：按完之后 Windows **不能**弹出开始菜单/搜索；
   按住 Windows 键超过自动重复延迟再松开，**仍然不能**弹。
3. 自动重复：按住和弦键 2 秒，动作只执行一次。
4. `suspend`：挂起之后所有绑定都不触发，**suspend 自己那条仍然可用**。
5. `quit`：托盘图标消失、钩子卸掉、进程干净退出。
6. `animate = true/false` 的肉眼区别；`volume` 的实际听感；托盘菜单点击。
7. `window_rule`：`DisplaySwitch.exe /internal` → `/extend` 制造一次“显示器重新接入”，
   确认日志里出现 `monitor connected: ...; re-applying window rules` 且手工挪走的窗口被摆回。
8. **远程桌面放行**（验收脚本用的是假名单，真机要手过一遍）：用 `mstsc.exe` /
   Windows App 连上一台机器，聚焦那个窗口，按一个被绑定的和弦（例如 `Ctrl+Alt+t`）——
   对面那台机器应当收到它、本机不该有任何动作；日志里出现
   `remote desktop detected (mstsc.exe)`。把焦点切回本机窗口，日志出现
   `left the remote desktop (…)`，和弦又回到本机行为；示例配置里 `remote_desktop = true`
   的 `Ctrl+Alt+m`（静音）在远程桌面里仍然生效。
9. 按顺序做完以上之后，**检查没有任何按键卡在按下状态**。

---

## 6. 架构与线程模型

```
        ┌─────────────────── Qt GUI 线程（主线程）───────────────────┐
        │ QApplication + QQmlApplicationEngine                        │
        │   * QSystemTrayIcon（右键菜单、气泡、悬停提示）              │
        │   * 日志窗口 + menu/help/switch/update 四个 QML 卡片         │
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

1. **钩子线程**：`LowLevelHooksTimeout` 默认 300 ms，慢回调的钩子会被 Windows **静默卸掉**
   （守护进程看起来还活着，却什么都不做）。所以回调只做「解码事件 → 问引擎要决定 →
   注入重映射按键 → 把动作排给工作线程」，然后立刻 `CallNextHookEx`。
   定时器（长按重复、挂起超时、显示器轮询）也在这个线程上。
2. **动作工作线程**：`run`/`open`/`wait` 都会阻塞，绝不能在钩子线程上做。
   它也是 `menu`/`help` 窗口的“请求方”——但**窗口本身**由 Qt GUI 线程创建。
3. **Qt GUI 线程**：托盘与全部 QML 窗口。跨线程一律走队列信号。

**钩子为什么不装在 Qt 主线程上**：QML 渲染的一次慢帧、以及 `QSystemTrayIcon` 的菜单弹出
（会跑自己的模态循环）都可能让主线程几十毫秒不回来；300 ms 的预算经不起这种抖动。

**通信与同步**：

* 钩子线程 → 工作线程：**`QMetaObject::invokeMethod(dispatcher, …, Qt::QueuedConnection)`**
  （`app::Dispatcher` 是 `QThread` 上的 `QObject`）。钩子回调里那次调用只做入队，不阻塞。
* 工作线程 → GUI 线程 / GUI 线程 → 工作线程：`Qt::QueuedConnection` 信号。
* 托盘/CLI → 钩子线程：`PostThreadMessage(hookThreadId, WM_APP_*, ...)`。
  **`PostThreadMessage` 在目标线程还没消息队列时会失败**，钩子线程必须先 `PeekMessage`
  一次建出队列，并把“队列已就绪”通过 `std::promise`/原子量告诉启动方。
* 跨线程共享的配置：`Compiled` 用 `std::shared_ptr<const Compiled>` 各持一份，
  **reload 时整体替换指针**，绝不做“就地改”。

---

## 7. 不变量 —— 不要破坏这些

1. **钩子回调必须快速返回。** 不要在回调里 sleep、写慢速日志、做 IO 或执行动作。
2. **绝不对注入输入作出反应。** 用 `LLKHF_INJECTED` 判定；带自己标记（`dwExtraInfo` 里的
   **`"FLOW"`**）的事件在任何状态被触碰之前就丢弃。**新增任何注入路径都必须打上标记**
   （包括 `ModifierGuard` 里“先松开 Win/Alt 再按回去”的注入）。
3. **重映射的按键内联注入，动作不是。** 因此重映射里的 `{Sleep}` 会被忽略
   （`--check` 给警告）。
4. **绝不能让按键留在按下状态。** 任何丢弃重映射的路径（挂起、重载、退出）都必须注入释放。
5. **不要跨 `SendInput` 持有锁。** 钩子状态放在钩子线程自己拥有的对象里，
   用“没有锁”的方式保证安全。**不要给它再套一层锁。**
6. **配置错误必须可操作。** 每条校验信息都要指出出错的快捷键（用 `name` 或序号）和出错的
   值，格式固定为：``hotkey #3 (`terminal`): unknown field `nope`, expected one of …``。
   **未知字段一律报错**（C++ 侧要自己实现严格白名单）。
   **动作表顶层是唯一的例外**（`window("activate", { togle = false })` 里的拼写错误会被
   忽略）—— 要在 README 的已知限制里写明。
7. **`core/` 与 `lua/` 保持无 Win32、无 GUI。** 这是核心逻辑无需桌面就能测试的前提。
8. **固定结构体布局是契约。** `INPUT` 在 x64 上必须 40 字节
   （`static_assert(sizeof(INPUT) == 40, "INPUT layout")`）。
9. **被吞掉的 `Win+…`/`Alt+…` 和弦必须在修饰键 key-up 时注入标记按键（`VK 0xE8`，
   未分配键）。** 跟着和弦键按下一起注入是**不够**的：Windows 键的自动重复会在两者之间
   重新武装外壳。实现照抄 `Engine::mask_menu_key_up`：被吞掉的和弦设置一个标志，
   而**下一次** Win/Alt 的 key-up 先注入标记。
10. **`send`/`type` 动作的 `release_modifiers` 也要做同样的菜单遮断。**
    `ModifierGuard` 注入的真实 key-up 引擎看不到，必须在注入层自己插一次 `VK_UNASSIGNED`。
11. **提权只发生在守护进程模式，且必须能降级。** `--check`/`--list`/`--list-keys` 绝不能弹
    UAC；UAC 被拒绝时打 warning 继续以普通权限运行。提权重启时要转发完整命令行
    （含相对路径的 `--config`）并把工作目录一并传过去。
    **本机实测：用户点“否”时 `ShellExecuteW("runas")` 返回的是 5（拒绝访问）而不是 1223**，
    所以降级路径不能只判 `ERROR_CANCELLED`。
12. **`--console` 之外，守护进程模式总是打开默认日志文件**（目录要 `create_dir_all`）。
    日志窗口尾随的就是那个文件；离线命令仍然不碰它。
13. **虚拟桌面与音频的 COM 单元模型不能混。** 音频要 **MTA**、虚拟桌面要 **STA**，
    所以 `desktop` 的每次调用都丢进一条**一次性的 STA 线程**
    （`CoInitializeEx(COINIT_APARTMENTTHREADED)` + 调用 + `CoUninitialize`）。
    **Qt 在 Windows 上会为 OLE/拖放把主线程初始化成 STA**，别去改它。
14. **Lua 状态绝不越出 `lua_config` 的求值函数。** 求值完就关掉 `lua_State`，
    把结果转成普通 C++ 结构。**`Compiled` 必须与 Lua 无关**。
15. **动作只能是声明式的表或简写字符串。** `action = function() end` 要由
    `lua_prelude.lua` 里的 `reject_functions` 明确拒绝。
16. **配置文本先剥掉 UTF-8 BOM 再交给 Lua。** 记事本与 `Set-Content -Encoding UTF8`
    都会写 BOM，而 Lua 的词法分析器不认它（第 1 行报 `unexpected symbol near '<\239>'`）。
17. **弹窗窗口必须能拿到键盘焦点。** 钩子守护进程通常**不持有前台锁**，只调 Qt 的
    `Window::requestActivate()` / `SetForegroundWindow` 会被拒绝。做法是
    `requestActivate()` → `SetForegroundWindow` → `AttachThreadInput` 到持有锁的线程再重试
    → `BringWindowToTop` + `SetWindowPos(HWND_TOP)`，**每条路径上 `AttachThreadInput`
    都要配平**。
18. **模态/前台相关的一切都要在 Qt GUI 线程上做。** `QSystemTrayIcon`、`QQuickWindow`、
    `QMenu` 都不能跨线程。
19. **DPI 感知必须在创建任何窗口之前就绪。** Qt 6 在 Windows 上默认就是 Per-Monitor
    DPI Aware V2，不需要手动调 `SetProcessDpiAwarenessContext`；这条变成了
    “**不要在 `QApplication` 构造之前创建窗口/触碰屏幕 API**”。
    不要调 `SetProcessDPIAware()`（会把 Qt 的设置打乱、字发糊）。
20. **日志窗口是一个普通窗口，不是独立进程。** 关掉它不能退出应用：构造 `QApplication`
    之后立刻 `setQuitOnLastWindowClosed(false)`。

---

## 8. Lua 5.5.1 的注意点（已实测）

1. **`luaL_openlibs` 现在是宏**（展开成 `luaL_openselectedlibs(L, ~0, 0)`）。
   调用 `luaL_openlibs(L)` 仍然可用。
2. **`lua_newstate` 多了一个 `unsigned seed` 参数**；用 `luaL_newstate()` 就没这个问题。
3. **`lua_resume` 的签名是 `(L, from, narg, nresults, ...)`**；`lua_resetthread` 是宏。
   本项目不做协程。
4. 新增 `lua_closethread`、`lua_closeslot`、`lua_toclose`（to-be-closed 变量）。
5. 我们**用到**的 API 全部确认存在；编译：`vendor/lua` 的 32 个 `.c` 用
   `-O2 -DLUA_USE_WINDOWS` 全部通过。
6. **Lua 侧两个行为差异**（写配置示例时别踩）：
   * `repeat` 仍然是关键字 → 字段名用 `repeatable`（配置里要认 `["repeat"]` 这个别名）。
   * 字符串里的 `\t`/`\n`/`\r` 是**合法但致命**的转义：`open("C:\tools")` 会**静默**变成
     `C:` + TAB + `ools`。Windows 路径一律用长字符串 `[[C:\path\app.exe]]`。
7. **`lua_newuserdatauv`**（而不是旧的 `lua_newuserdata`）。

> **把 Lua 的版本号写进 `--version` 与 `--check` 的输出**，这样出问题时能一眼看出是哪一份。

---

## 9. 实施阶段计划

每个阶段都可运行、可提交，并且都要满足 §5 的 DoD。

| 阶段 | 状态 | 交付 |
| ---- | ---- | ---- |
| 0 仓库与构建骨架 | **已完成** | 双 profile 绿、`ctest` 绿、托盘 + 日志窗口冒烟过 |
| 1 纯逻辑核心 | **已完成** | `src/core/*` + 6 个 Qt Test 目标全绿 |
| 2 Lua 配置层 | **已完成** | `flowkeyd_lua` 静态库 + `--check`/`--list` 可用、`tst_lua` 全绿、示例配置能过 `--check` |
| 3 Win32 基础设施 + 钩子 + 引擎接线 | **已完成** | `flowkeyd_platform` 静态库 + 钩子线程/动作线程；`tst_layout/input/command_line/instance` 全绿 |
| 4 托盘 + 日志窗口 | **已完成** | 完整托盘菜单 + `log_tail`/`log_model` 尾随模型 + `LogWindow.qml`；`tst_log_tail` 全绿 |
| 5 窗口动作 + 剪贴板 + 音量/媒体 | **已完成** | `core/window_match`、`dwm/window/clipboard/audio`、dispatcher 接线与 `{selection}`；`tst_window_match`/`tst_audio` 全绿 |
| 6 弹窗 `menu` / `help` | **已完成** | `flowkeyd_models` + 两张 QML 卡片；`tst_menu_model`/`tst_help_model` 全绿 |
| 7 虚拟桌面 + 电源 | **已完成** | `desktop`/`power` + dispatcher 接线；`tst_desktop_table`/`tst_power_table` 全绿 |
| 8 示例配置 + README | **已完成** | 覆盖全特性的 `flowkeyd.lua.example`（`--check` 零警告）；README 已写全 |
| 9 验收（无 e2e 的替代） | **已完成** | `scripts/acceptance.ps1`（当时 119 项，现在 134 项）+ `FLOWKEYD_ACCEPT_INJECTED` 测试后门 |
| 10 接管 | **已完成** | 真实配置迁到 `.config\flowkeyd\config.lua`；常驻由计划任务 `flowkeyd` 指向当前运行的 exe |

第 10 阶段之后新增的能力（都在本文件对应章节有记录）：
`window_rule`（+`all_desktops`/`topmost`/`follow`）、`app{...}`、四个「挪窗口」op、
字母键名小写、托盘数字徽标、窗口切换器（+数字选择模式+IME 切换）、弹窗不进任务栏 + 预热、
发布包精简、精简升级包、`scripts/release.ps1`、在线更新、`install.ps1` 一键安装、
FreeType 字体引擎。

---

## 10. 已踩过的坑

### 环境与工具

* **`qt_add_executable(... WIN32 ...)` 意味着 GUI 子系统 ⇒ 没有控制台。** 从终端启动的
  `flowkeyd --check` 什么都打印不出来。修法：`main()` 第一件事
  `AttachConsole(ATTACH_PARENT_PROCESS)`（失败就忽略），然后自己用 `WriteConsoleW` 写
  （重定向时走 UTF-8 `WriteFile`）。**行尾必须是 `\r\n`**。
  不要用 `qDebug` 当日志（没有控制台时它走 `OutputDebugString`）。
* **`QSystemTrayIcon` 在 QtWidgets 里**，不是 QtGui 里。要用它就必须 `QApplication`。
* **QML 窗口默认会让“关掉最后一个窗口”直接退出应用** → `setQuitOnLastWindowClosed(false)`。
* **FluentWinUI3 是 Qt Quick Controls 的样式，不是 Widgets 样式。** 用
  `QQuickStyle::setStyle("FluentWinUI3")` 在加载任何 QML 之前设置。
* **FluentWinUI3 不支持一批控件**（`Dial`、`Drawer`、`HorizontalHeaderView`、`SplitView`、
  `StackView`、`SwipeDelegate`、`SwipeView`、`TreeViewDelegate`、`Tumbler`、
  `VerticalHeaderView`）——它们会**静默回退到 Fusion**。布局不要用它们。
* **无边框圆角弹窗**：`flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint` +
  `color: "transparent"` + 内部一个 `Rectangle { radius: … }`。只设 `FramelessWindowHint`
  而 `color` 不是 transparent 就看不到圆角。
* **Qt 6 在 Windows 上默认就是 Per-Monitor DPI Aware V2**；不要在 `QApplication` 之前
  创建窗口。要改缩放取整策略就在它之前调。
* **Qt 会自动给主线程一个 STA**（为了 OLE/拖放）。别把音频初始化放到主线程。
* **字体引擎是平台插件的构造参数，只能在 `QApplication` 之前选。** Windows 上默认走
  DirectWrite，本机高缩放下 Qt Quick 界面发虚；`main()` 第一行
  `qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype")`。**不要在 `main()` 之后、
  更不要在 QML 里设它。**
* **`PostThreadMessage` 在目标线程还没消息队列时会静默失败** → 钩子线程先 `PeekMessage`。
* **PowerShell 5.1 的 `Set-Content -Encoding UTF8` 写出 BOM**，而 Lua 的词法分析器在 BOM
  上报错 → 加载器必须先剥 BOM。反向：**没有 BOM 的 `.ps1` 会被 PowerShell 5.1 按 GBK
  解码**，里面的中文会变乱码、甚至被当成引号让 here-string 提前结束，或者**静默吞掉下一行**
  （表现为“脚本不报错、只是少做一步”）。写文件的正确姿势：
  `[System.IO.File]::WriteAllText($p, $text, [System.Text.UTF8Encoding]::new($true))`。
  **`tmp/` 下的一次性脚本一律纯 ASCII。**
* **`.cmd`/`.bat` 必须纯 ASCII。** **命令行上不要直接拼中文。**
* **`$args` 与 `$Pid` 是 PowerShell 自动变量**，不要当参数名/变量名。
* **`Start-Process -PassThru` 拿不到退出码**（`$p.ExitCode` 是空的）；
  `cmd.exe` 也不会等 GUI 子系统的进程。验证 CLI 输出用
  `Start-Process -FilePath .\flowkeyd.exe -ArgumentList '--help' -NoNewWindow -Wait -PassThru -RedirectStandardOutput out.txt`。
* **PowerShell 的数组 splatting 不能转发命名参数** → 用哈希表 splatting
  （`$splat = @{ TaskName = $x }; & script.ps1 @splat`）。
* **`$ErrorActionPreference = 'Stop'` 下原生命令往 stderr 写一个字就会被包成终止性异常。**
  脚本调用 cmake/git/gh 时要用 helper 临时把 EAP 切回 `Continue`、只看退出码。
* **PowerShell 接原生命令的 UTF-8 stdout 会按控制台代码页解码成乱码** → 按字节重定向到文件
  再看。**`Tee-Object` 写出来的日志是 UTF-16。**
* **`Get-Content` 不带 `-Encoding UTF8` 会把无 BOM 的 UTF-8 数错行数** → 用
  `[System.IO.File]::ReadAllLines`。
* **守护进程一直把日志文件开着写**，`[System.IO.File]::ReadAllLines` 会报“文件正由另一进程
  使用” → 用 `FileShare.ReadWrite` 的 `FileStream`（`Get-Content` 默认就是 ReadWrite）。
* **核对文档/示例改动用 `git diff --no-index`**（比肉眼可靠）。
* **`tmp/` 里有上次跑留下的旧日志**：先看时间戳或先删掉旧的，别把三天前的输出当成刚才那次。
* **`irm` 会把响应体开头的 BOM 留成真实的 `U+FEFF`** → `Invoke-Expression` 在 `param()` 上
  报 `InvalidLeftHandSide`。也**不能**用 `curl | powershell -Command -`（GBK 解码中文）。
  所以 `install.ps1` 只能二选一，本项目选「纯 ASCII + 无 BOM」。

### 配置 / Lua / 核心逻辑

* **Lua 的头文件里没有 `extern "C"` 保护** → 在 C++ 里直接 `#include <lua.h>` 会 mangling，
  链接时报一屏 `undefined reference`。**统一用 `src/lua/lua_include.h`。**
* **`lua_next` 遍历中调 `lua_tolstring` 会把数字键就地转成字符串**，于是下一次 `lua_next`
  收到一个不存在的键，触发 `PANIC: invalid key to 'next'`（abort）。规则：只对
  `lua_type == LUA_TSTRING` 的键取字符串，数字键用 `lua_tointeger`。
* **嵌套遍历时内层压栈会打乱外层的 `lua_next` 游标**。写法：进循环前记
  `loopBase = lua_gettop(L)`，每轮结束 `lua_settop(L, loopBase + 1)`。
* **`lua_next` 的“复制表”惯用法里不能再多弹一次**：`pushvalue(-2); insert(-2); settable(dst);`
  之后栈上正好剩下 key。
* **Lua C API 的栈不自动扩容。** `lua_push*`/`lua_rawgeti` 不会自己扩容；
  `readTableList` 会把读到的每个条目都留在栈上，条目一多就直接写到数组之外 ——
  现象是 `--check` 以 `0xC0000374`（heap corruption）崩掉，而且到 `lua_close()` 才报错。
  **修法**：压条目之前先 `lua_checkstack(L, 条目数 + LUA_MINSTACK)`；
  `lua_settop(L, n)` 之前也要先 `lua_checkstack`。**诊断手法**：临时给 `lua_static` 加
  `LUA_USE_APICHECK`，它会把所有非法索引变成确定的 abort。
* **`-Werror` 下两个 Win32 小坑**：MinGW 的 `SendInput` 第二参是 `LPINPUT`（非 const），
  要 `const_cast`；`GetProcAddress` 的 `FARPROC` → 具体函数指针会被
  `-Wcast-function-type` 报错，用 `std::memcpy` 绕开。
* **`HHOOK` 不是 `HANDLE`**。**`core::Trigger`/`Phase` 在 `core/engine.h`**，不在 `config.h`。
* **`QObject::moveToThread` 拒绝带 parent 的对象。**
* **`emit other->someSignal()` 在类外编译不过**（信号是 `protected`）→ 用公开方法 +
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 把 emit 挪回 GUI 线程。
* **`QCOMPARE` 的宏参数里不能出现“不受括号保护”的逗号**（尖括号不算括号）→ 起 `using` 别名。
* **`QCOMPARE(optional<int>, -1)` 是错的**：`-1` 会被隐式构造成 engaged 的 optional，
  而“没有值”是 `std::nullopt`。
* **`QDir::filePath()` 一律用 `/` 当分隔符**，不是 Windows 的 `\` → 输出前统一过一遍
  `QDir::toNativeSeparators()`。
* **`QWheelEvent` 合成不出来**：`QCoreApplication::sendEvent(window, &wheelEvent)` 递进去之后
  `isAccepted()` 为 false、列表一点都不动。要验证滚轮只能 `SetCursorPos` + `SendInput`
  （而且弹窗必须是前台窗口，`WM_MOUSEWHEEL` 送给焦点窗口）。
* **`WIN32_LEAN_AND_MEAN` 不包含 `ole2.h`** → `CoInitializeEx`/`CoCreateInstance` 要显式
  `#include <objbase.h>`。MinGW 的 `GUID` 可以直接聚合初始化。
* **MinGW 的头文件里没有 `MONITOR_OFF`**（`SC_MONITORPOWER` 与 `SMTO_ABORTIFHUNG` 有）。
* **`hresultMessage()` 以前用 `FormatMessageW` 解析 HRESULT，结果几乎总是 `error 0x…`。**
  现在用 `hresultText()`：`E_NOINTERFACE` 等几个常见值有人话名字。这对虚拟桌面特别重要：
  `E_NOINTERFACE` 意味着版本表选错了 IID。
* **`RtlGetVersion` 可以从已加载的 `ntdll.dll` 用 `GetModuleHandleW` + `GetProcAddress` 拿**
  （不用加链接依赖）；`GetVersionEx` 会被应用清单骗，不能用。

### 构建 / 测试 / 打包

* **`QTEST_MAIN` 的默认输出在本机拿不到**：重定向、管道、`ctest --output-on-failure` 全是空的，
  而 `-o <file>,txt` 能拿到完整结果。→ `cmake/RunQTest.cmake` 让测试先写文件、再由脚本
  `cmake -E cat` 出来。**新增测试不用做别的**，`flowkeyd_add_test()` 已经封好了。
* **`qt_add_executable()` 会默认给测试套上 AUTOMOC**，连 `lua_static` 也不例外 →
  `lua_static` 上显式写 `AUTOMOC OFF`/`AUTOUIC OFF`。
* **`set_tests_properties(... ENVIRONMENT_MODIFICATION)` 的两个修改项必须写在一个 `"a;b"`
  字符串里。** 跑测试的 exe 需要 `PATH` 里有 Qt 与 MinGW 的 `bin`，否则 `ctest` 报
  `0xc0000135`（DLL not found）。
* **带 qrc 的静态库，资源初始化会被链接器丢掉** → 在被引用的源文件里调一次
  `Q_INIT_RESOURCE(name)`（必须在**全局命名空间**里）。
  另一个坑：`qt_add_resources(target path/to/foo.qrc)` 这个“直接传 .qrc”的写法在本机
  Qt 6.11 上**什么都没生成**；用 `qt_add_resources(target "name" PREFIX "/x" BASE <dir> FILES …)`。
* **写测试用的假 `Evaluator` 不用真的 Lua**：`core::loadConfig()` 把求值回调当参数。
* **`windeployqt` 只会多拷、不会删。** 它按 qmldir 里的 `optional import … auto` 把六个
  Quick Controls 样式全拷了，没有开关能只留一个 → 想变小只能“拷完再删”
  （`cmake/PruneRuntime.cmake`）。**`--style` 这个选项本机 6.11 的 windeployqt 没有。**
* **发布包精简（1378 个文件 / 149.8 MB → 212 个 / 63.0 MB）**，四类东西（都实测过）：
  1. 没用到的那几个 Quick Controls 样式（Imagine/Material/Universal/Windows/NativeStyle，
     连同它们的 QML 目录与 DLL）→ ~12 MB / ~500 个文件。
  2. `qmltooling/`（13 个插件）、`imageformats/{qgif,qjpeg,qsvg}` + `iconengines/qsvgicon` +
     `Qt6Svg.dll`、`networkinformation/`、`generic/qtuiotouch`、`Qt6Quick3DUtils.dll`、
     `plugins.qmltypes` → ~5 MB / ~40 个文件。
  3. **`opengl32sw.dll`（19.7 MB）与 `D3Dcompiler_47.dll`（4.0 MB）**：Qt Quick 在 Windows 上
     走 D3D11 RHI（没显卡驱动时 WARP 也能画），而 Win10+ 的 `System32` 本来就有
     `d3dcompiler_47.dll`。**这两个必须同进同退**（少了 D3D 编译器 Qt 会退回 OpenGL，
     那时才真需要软件回退）。失败时的现象是“弹窗与日志窗口出不来（托盘与快捷键还在）”。
  4. **exe 里的 43 MB 调试符号**（`RelWithDebInfo` 带 `-g`）→ release 用
     `objcopy --strip-all` → 1.6 MB。
  * **FluentWinUI3 的磁盘 .qml/.png 可以整份删掉（省 848 个文件）**：样式插件把整套资源
    **内嵌在自己的 qrc 里**，模块 qmldir 里的
    `prefer :/qt-project.org/imports/QtQuick/Controls/FluentWinUI3/` 就指向那里。
    验证“插件里真有这些资源”的手法：把 DLL 按 **UTF-16** 解码再查 `Config.qml` 这类名字
    （按 ASCII 查一个都找不到）。**`qmldir` 必须留着**，`plugins.qmltypes` 可以删。
  * **Basic 与 Fusion 删不得**（实测报错 `module "QtQuick.Controls.Basic" is not installed`）：
    `QtQuick.Controls` 的 qmldir 有 `default import …Basic auto`，FluentWinUI3 有
    `import …Fusion auto`。两个合起来约 4 MB，作为保险留着。
  * **`tls/` 不能整个删**：加了 `Qt6::Network` 之后 https 靠 `tls/qschannelbackend.dll`
    这个插件。只删 `qopensslbackend.dll` 与 `qcertonlybackend.dll`。
  * **`objcopy --strip-all` 不是幂等的**（每次都重写 PE 头 COFF 的 `TimeDateStamp`）→
    dist-release 那一步**不再 strip**（源 exe 已经被 strip 过），否则两棵 release 树的 exe
    就不再逐字节相同。
  * **改了精简清单必须重新链接**（prune 挂在 `POST_BUILD` 上，只在链接时跑）→
    `set_property(TARGET flowkeyd APPEND PROPERTY LINK_DEPENDS …/cmake/PruneRuntime.cmake)`
    已加。否则光改脚本是 `ninja: no work to do`，部署目录里还是旧内容。
  * **精简过头的及早发现**：四个弹窗启动时就预热，缺模块会立刻报
    `could not load ….qml: module "…" is not installed`；`scripts/acceptance.ps1` 有一条
    哨兵检查盯着这个。**日志窗口不在预热里**，它只 import `QtQuick`/`Controls`/`Layouts`。
* **`Compress-Archive` / `ZipFile::CreateFromDirectory` 在 Windows PowerShell 5.1 里把 zip
  条目名的目录分隔符写成 `\`**（Windows 能解，但 unzip/tar/WSL 会解出一堆名字里带反斜杠的
  文件）→ 发布脚本的 `New-ZipPackage` 自己用 `ZipArchive` 逐个写条目。
  （`ZipArchive`/`ZipArchiveMode` 在 `System.IO.Compression`，`ZipFile` 在
  `System.IO.Compression.FileSystem`，**两个都要 `Add-Type`**。）
* **发布时“哪个文件进哪个包”用白名单 + 兜底报错**（`scripts/release.ps1` 的 `$SlimFiles` /
  `$DependencyPatterns`）：`dist` 里出现两边都不认识的文件就直接失败，逼人当场分类。
* **`gh release create --notes` 的内容会加在自动生成说明前面**；那段文字故意写成一行
  （带换行的参数在 Windows 上要多绕一道）。
* **tar/zip 里的 `README.txt` 用带 BOM 的 UTF-8 写，`.sha256` 不带 BOM**
  （`sha256sum -c` 认的是逐字节内容）。

### QML / 界面

* **QML 的 model 角色名不能叫 `text`/`highlighted`/`hovered`**：标准控件（`Text`、
  `ItemDelegate`）自己就有这些属性，`required property` 的名字必须等于角色名，撞名就声明不了。
  本项目用 `line`/`level`、`rowSelected`、`rowArmed`、`rowKey`、`windowTitle`/`windowProcess`。
  委托里高亮写 `highlighted: rowItem.rowSelected`。
* **delegate 里访问外层组件的 id 会报 `Unqualified access`** → 文件开头
  `pragma ComponentBehavior: Bound`。JS 数组模型的 delegate 要写
  `required property var modelData`。
* **用 `qmllint -I C:\Qt\6.11.2\mingw_64\qml <file>.qml` 提前查问题**（**不要加 `--bare`**）。
  QML 类型的类名带 id 后缀（`TextField_QMLTYPE_1544`），遍历 item 树找控件要用
  `contains("TextField")` 而不是相等比较。
* **`import QtQuick.Controls.FluentWinUI3` 里确实能用 `Label`。**
* **本机 225% 缩放下 Qt 报出的 `availableGeometry()` 比 `geometry()` 还宽**（工作区从
  x=108 开始、宽 485，而屏幕只有 533 宽）→ 居中之后再按屏幕 `geometry()` 夹一次
  （`app::centrePopup()`，纯函数、有单测）。
* **用 DPI 不感知的 PowerShell 进程 `GetWindowRect` + `PrintWindow` 会拿到错的结果**
  （坐标被虚拟化成逻辑像素）→ 让 Qt 自己抓（`QScreen::grabWindow(window->winId())`），
  或先 `SetProcessDPIAware()`。
* **`ItemDelegate` 的内边距覆盖不掉**（FluentWinUI3 的 `ItemDelegate.qml` 用绑定给每个实例
  定内边距，实测左右 12 / 上下 8）→ **不要和样式的内边距较劲**：内容区里的东西全部锚在
  `contentItem` 上，让它自适应。
* **`QQuickTextInput` 故意忽略 `↑`/`↓`**，所以它们会冒到父项的 `Keys`；而 `Keys` 的默认
  优先级 `Keys.BeforeItem` 意味着挂在 `TextField` 上的 `Keys.onPressed` 在 `TextInput` 自己
  的键盘处理之前跑 —— 这就是“一个输入框 + 一个列表”的键盘分工。
* **筛选回写用 `onTextChanged` + 等值判断，不要 `onTextEdited`**（后者在输入法提交/粘贴/
  拖选时不一定发）。`if (field.text !== model.filter)` 保证不会来回振荡。
* **`ListView.positionViewAtIndex(..., Contain)` 不能用来“把选中行带进视野”**：它只保证行
  落在**列表自己的矩形**里，**不看** `topMargin`/`bottomMargin`。`positionViewAtBeginning()`
  更坑：它想去 `contentY = 0`，被 Qt 的 `qBound` 夹到了**最大位置**。
  → 自己算 `HelpModel::scrollTargetY(...)`（纯算术、有单测），QML 只把结果写回
  `listView.contentY`。
* **列表刚建好时 `contentY` 会被摆到一个“保持滚动比例”的位置**（实测 30 条时是
  `contentY = 90` 而不是顶部的 `-88`），而且发生在收到 `selectedChanged` **之后** →
  除了模型信号，还要在 `onContentHeightChanged`/`onHeightChanged` 里调一次同一个幂等的
  `followSelection()`。
* **两个弹窗都是 `WindowStaysOnTopHint`，会互相遮住**；`grabWindow` 抓的是屏幕那块区域。
* **`QWindow::setProperty("visible", …)` 是隐藏/显示一个 QML `Window` 的最省事办法**
  （窗口不会被销毁，可以复用）。
* **`Keys.onPressed` 只在窗口是活动窗口时才会把事件交给有焦点的 item。**
* **进程内预览里 `console.log` 不一定看得见** → 用“写一个能从 C++ 读回来的属性”调试。
* **`GetWindowRect` + `SetCursorPos` 必须同一种像素**（脚本开头先 `SetProcessDPIAware()`）。
  弹窗几何靠**宽度反推缩放**（menu 卡片 300、help 卡片 500 逻辑像素）比猜 DPI 稳。

### Windows 领域坑

* **`SetTimer(nullptr, id, …)` 会忽略 `id` 并返回一个新的定时器 id**，`WM_TIMER` 的
  `wParam` 就是那个新 id。这曾让 `Engine::tick()` 从未被调用（长按重复一直是坏的）→
  把返回值存进成员再比较，`KillTimer` 也用返回值。
* **`SetForegroundWindow` 除非持有前台锁否则被拒**（递进式绕行 + `AttachThreadInput` 配平）。
  **而且它在“目标已经是 shell 的前台窗口，只是不在当前虚拟桌面上”时是空操作**
  （返回 TRUE 却什么都不发生）—— 被 `MoveViewToDesktop` 搬走的窗口就是这个状态，
  只能先显式 `SwitchDesktop`。
* **`GetForegroundWindow()` 不等于“用户看得见的窗口”**：窗口被搬到别的虚拟桌面之后 shell
  还把它当前台窗口（而公开的 `IsWindowOnCurrentVirtualDesktop` 返回 FALSE）。
* **`IVirtualDesktopManagerInternal::MoveViewToDesktop` 在 vtable 下标 4**（三种布局一致），
  需要先用 `IApplicationViewCollection::GetViewForHwnd`（IID/SID 都是
  `{1841C6D7-4F9D-42C0-AF41-8747538F10E5}`，下标 6）把 `HWND` 换成 `IApplicationView*`。
  **已公开的 `IVirtualDesktopManager::MoveWindowToDesktop` 拒绝移动别的进程的窗口。**
* **`MoveViewToDesktop` 返回 S_OK 不等于窗口真的换了桌面。** 验证要用**已公开**的
  `IVirtualDesktopManager::GetWindowDesktopId`（`CLSID_VirtualDesktopManager`
  `{AA509086-…}`，IID `{A5CD92FF-…}`，下标 4）；`IsWindowOnCurrentVirtualDesktop`（下标 3）
  也能用，但 shell 是**异步**移动的，要轮询等它生效。
* **`IVirtualDesktop::GetID` 在 vtable 下标 4**（先 `IsViewVisible`（3），照抄 VD.ahk 的
  `VD_goToDesktopOfWindow`），用来把窗口的 `GetWindowDesktopId` 对到内部枚举的桌面上；
  **拿“有且只有一个匹配”当自检**。
* **`IVirtualDesktopPinnedApps` 的 IID 自 Win10 起就没变过**（SID
  `{B5A399E7-1C87-46B8-88E9-FC5747B171BD}`、IID `{4CE81583-1E4C-4632-A621-07A53543148F}`，
  vtable `3 IsAppIdPinned / 4 PinAppID / 5 UnpinAppID / 6 IsViewPinned / 7 PinView /
  8 UnpinView`），所以**不进版本表**。`PinView` 返回 S_OK 本身证明不了什么，
  要用 `IsViewPinned` 从外面确认状态变了。
* **虚拟桌面接口表的 `build.revision` 是“起始版本”不是上限**（取“生效版本不高于当前系统的
  最后一条”）；修订号用 `RegGetValueW` 从注册表读 `UBR`。
* **“另一个桌面上的窗口”是被 DWM cloak 掉的。** `DWMWA_CLOAKED` 非零 = 没显示出来，但
  **不能单独用**：`cloaked != 0` + `IsWindowOnCurrentVirtualDesktop == FALSE` → 在别的虚拟
  桌面上（**要留**）；`cloaked != 0` + `== TRUE` → shell 藏在当前桌面上的假窗口（**排掉**）。
  两个 `DWM_CLOAKED_SHELL(2)` 在数值上分不开，只能问一句虚拟桌面。
* **`WS_EX_TOPMOST` 只能靠 `SetWindowPos` 设，不能靠 `SetWindowLongPtr`**（后者会出现
  返回 TRUE 却什么都没发生）。
* **新建窗口后立刻置顶可能无效**（shell 还没登记它：`GetWindowDesktopId` 给全零 GUID、
  `IsWindowOnCurrentVirtualDesktop` 报 FALSE）→ 先对它做一次窗口操作（`applyPlacement`）
  再置顶。产品路径不会碰到这个。因此 `desktop::isWindowOnCurrentDesktop` 把
  “不属于任何虚拟桌面”当成 `std::nullopt`（不知道），而不是“在别的桌面上”。
* **有属主的窗口（对话框/工具提示/弹出菜单）永远不是用户想要的那个窗口。**
  “主窗口”判据还必须带上 **非 `WS_EX_TOOLWINDOW`** 与 **有标题**：按 `process` 匹配会一次
  命中一堆内部窗口（`Non Client Input Sink Window`、无标题的 `NotepadTextBox`）。
  `WS_EX_APPWINDOW` 那一条照任务栏/Alt+Tab 的规则来（“无属主 **或** 带 `WS_EX_APPWINDOW`”）。
* **`core::windowProcessMatches()` 里的空 needle 是「不限制」的意思、返回 true**（它本来
  是给 `window` 动作的可选 `process` 用的）。所以任何拿它做名单匹配的地方都**不能**把
  空串当普通条目：远程桌面名单里混进一个 `""` 会让**所有**前台窗口都算远程桌面、
  快捷键整片失效。修法是两层：加载时拒绝空条目（`settings.remote_desktop.processes[%1]`），
  运行时的 `isRemoteDesktopProcess()` 也 `continue` 跳过它。
* **`SetWindowPlacement` 是跨显示器摆放的关键。** 顺序是：`IsZoomed` 就先 `SW_RESTORE`，
  然后写 `WINDOWPLACEMENT.rcNormalPosition`（屏幕坐标）并设 `showCmd`，最后对非最大化的
  情况再补一次 `SetWindowPos`（`SWP_NOACTIVATE`）。最小化的窗口只更新“还原位置”。
  `SetWindowPlacement` 不会抢焦点。
* **`DWMWA_TRANSITIONS_FORCEDISABLED` 读不回来**，只能“设 TRUE → `ShowWindow` → 设回
  FALSE”（RAII `TransitionGuard`）；DWM 在过渡**开始**时读它。
* **终端窗口不能靠标题找**（`process = "wezterm"`，窗口属于 `wezterm-gui.exe`）。
* **`run`/`window.launch` 走 `CreateProcess`、不查 `App Paths`、也不能用
  `DETACHED_PROCESS`**（会静默杀死控制台子进程，用 `CREATE_NO_WINDOW`）。
* **提权后的进程会把它启动的子进程一起提权**（写进 README 的已知限制）。
* **中文错误文案不可断言**（`FormatMessageW` 是本地化的）—— 这也是我们日志与校验信息
  保持英文的实际原因。
* **输入法（IMM32/TSF）的转换模式：别的应用不受影响，但本进程的几个窗口互相看得见**
  （在一个窗口里按 `Shift` 切中文，另一个窗口读到的也带 `NATIVE` 位）。
  同一台机器上不同窗口报出的标志位还不一样（见过 `0x800`/`0xc00`/`0xfb0`）——
  **只有 `NATIVE`（0x1）那一位有意义，还原时整份照抄**，不要自己拼一个
  `IME_CMODE_ALPHANUMERIC(0)` 写回去。从另一个进程去 `ImmGetContext` 拿不到上下文
  （连 `AttachThreadInput` 也救不回来），只能在进程内验证。
* **`windows.h` 的 `DELETE` 宏与 `core/keys.h` 的 `Vk DELETE` 撞名**：把任何会拉进
  `windows.h` 的头文件加进被 `main.cpp` 包含的头里就会炸（报
  `expected unqualified-id before numeric constant`）。平台头只放在 `.cpp` 里。
* **`.arg(标题, 句柄)` 会把句柄当成字段宽度**（Qt 6 有
  `arg(const QString &, int fieldWidth, QChar)` 重载）：一条日志会变成 132 KB。
  **两个参数的 `.arg()` 里只要有一个不是 `QString`，就自己先转成字符串**（本项目用
  `handleText(hwnd)`）。
* **`QDate::fromString(text, "yy-MM-dd")` 的两位年份是启发式的**（实测 `26` 解成 **1926**），
  而 `toString` 反着写就是 `26-09-27`；解析自己的输出必须自己拼年份。
* **Windows 上“正在运行的 exe”可以改名、不可以删/覆盖**：在线更新就是靠这个把旧 exe 改成
  `<exe>.old` 再把新的改名就位；`.old` 只能由**新进程**删。`MoveFileExW` 不能跨卷
  （新文件必须先落在同目录）。运行中的 exe 被锁时，release 全量构建会卡在链接
  （`cannot open output file flowkeyd.exe: Permission denied`）→ 常规解法是 `--quit`；
  实在停不掉时可以把被锁的 exe `Move-Item` 成 `*.locked`（内核映像按区域映射），
  但常驻实例跑的就是旧构建了，要告知用户。
* **提权实例被强杀会留下幽灵托盘图标**（explorer 不会马上发现进程没了）→ 优先 `--quit`。
* **`--quit` 的事件名按“传进去的配置路径字符串”命名** → 停一个开发实例必须用与启动时
  **同一个拼写**（脚本里一律传绝对路径）。
* **完整性级别的 “no write up”**：提权守护进程建的事件默认带 High 标签，非提权的 `--quit`
  会吃 `ERROR_ACCESS_DENIED` → 用 SDDL `D:(A;;GA;;;<用户 SID>)S:(ML;;NW;;;LW)` 把强制标签
  压到 **Low**（互斥体同理，为了让非提权进程能 `OpenMutexW`）。
* **`--check` 不带 `--config` 读的是真实配置**（`%USERPROFILE%\.config\flowkeyd\`）→
  验收/接管脚本里一律显式写 `--config`。
* **`QCoreApplication::applicationDirPath()` 在没有 `QApplication` 时会警告并返回空串** ——
  它在 `core::configPathCandidates()` / `legacyTomlCandidates()` 里被调用，而这两条都在
  `QApplication` 之前跑（离线命令与守护进程都是），于是**文档里写的“exe 同目录”候选实际
  是失效的**（`exeDir` 是空串，直接跳过）。这是个已知小缺陷，**没改**；要改就得给 core 一个
  不依赖 Qt 实例的 exe 目录来源（例如 `core::setExeDirectory()`，由 main 从平台层传进去）。

### 测试 / 验证手法

* **“物理按键”可以自动化，但必须先加一个测试后门**：钩子照规矩丢弃一切带 `LLKHF_INJECTED`
  的事件，于是 `SendInput` 伪造不了用户按键 → `FLOWKEYD_ACCEPT_INJECTED=1` 抬升那道过滤，
  而且**只改钩子给 `event.injected` 赋值的那一步**。启用时打一条警告。
* **`$form.Activate()` 会静默失败**：Windows 的前台锁只允许“当前就在前台的那个进程”抢焦点。
  验收脚本要照抄守护进程自己的 `raiseWindow`（`AttachThreadInput` 到当前前台线程 →
  `SetForegroundWindow` → 解挂），而且真抢不到焦点时要**明确报一条失败**。
  弹窗复用之后 `window->isActive()` 可能是陈旧的 `true`（`activateWindow` 会早退）→
  再问一句 `GetForegroundWindow() == hwnd`。
* **`Win+F16` 在本机是外壳快捷键**（会拉出“滑动以关机”）。选测试和弦之前，先用一个不装钩子
  的小脚本探一下它会不会动外壳。本项目用 `Win+S`（失败代价只是弹个搜索框）。
* **注入鼠标的脚本里，凡是用绝对坐标就必须先把光标放到一个确定的起点**（多显示器下尤其
  如此）：弹窗“跟着光标走”是对的 UX，错的是“坐标算一次就管到底”的测试写法
  （`FocusCatcher` 里 `[FlowInject]::Cursor(200, 200)`）。
* **要验证弹窗链路又不想真的按键盘**：做一个**临时预览程序**（`tmp/preview/`：自己的
  `CMakeLists.txt` + `main.cpp`，`file(GLOB)` 拉进 `src/core`、`src/platform/win`、模型与
  `popup_host.cpp`，再用 `qt_add_qml_module` 注册同样的模块）：直接调
  `PopupHost::requestMenu/requestHelp`，用 `QCoreApplication::sendEvent(window, &QKeyEvent(...))`
  模拟键盘，再用 `QScreen::grabWindow` 截图。**这条路径能验证除“真实按键”之外的一切**，
  而且锁屏时也能跑。（`tmp/` 在 `.gitignore` 里，重做一次大概十分钟。）
  注意预览工具自己会报一堆 `ItemDelegate.qml` 的 `TypeError`（用官方 Qt 跑也一样），
  那是**预览工具的环境问题**，产品日志里是 0 条。
* **“一闪而过”的 bug 怎么查**：用 `Graphics.CopyFromScreen` 抓弹窗那块的屏幕区域
  （`QQuickWindow::grabWindow()` 看不到合成器的中间帧），每帧算一个 FNV 指纹，~120 fps，
  打印“与上一帧不同”的帧号；把抓到的帧存成 PNG 用 `imgdiff` 打印“不同像素数 + 包围盒”。
  注入 `MOUSEEVENTF_WHEEL` 之后**再注入 1 px 的 `MOUSEEVENTF_MOVE`**（真鼠标滚轮几乎总会带
  一点位移）；而且**弹窗必须是前台窗口**（背景窗口收不到 `WM_MOUSEWHEEL`）。
* **从外面看见托盘图标变了**：`SetCursorPos` + 一次**相对**的 `mouse_event(MOUSEEVENTF_MOVE)`
  把光标推到屏幕边缘，自动隐藏的任务栏才会滑出来；然后 `CopyFromScreen` 整条任务栏，
  像素差求包围盒。新起的实例图标会落进「隐藏的图标」溢出弹窗里，要看真实效果就重启常驻实例。
* **本机任务栏是自动隐藏的竖条**（`Shell_TrayWnd` 是 `0,0,96,2160`），抓图坐标要与这个对齐。
* **需要真实桌面的验证要做成“默认 skip 的交互式单测”**（`tst_interactive` +
  `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）：这比写一个只跑一次的临时程序好，
  能反复验证剪贴板/音量/窗口后端，而且不会在 CI 里碰用户桌面。
* **本机的记事本变成了带标签页、单实例、带会话恢复的 Store 应用**：`WM_CLOSE` 会弹确认框、
  给文件参数只是加标签页、标题可能停在别的文件上 → “窗口后端”的断言改用**测试进程自己的
  顶层窗口**（`tests/tst_interactive.cpp` 里的 `TestWindow`：自己
  `RegisterClassExW` + `CreateWindowExW`，标题可控、进程独占、`WM_CLOSE` 就是 `DestroyWindow`；
  等消息用只抽自己窗口消息的 `pump()`）。记事本只剩两个用途：覆盖“启动一个真程序 + 按标题
  找到它的窗口”，以及需要一个真能 Ctrl+C 的编辑器。
* **本地假 GitHub 是验证在线更新最好用的手法**（`tmp/e2e-local.ps1` +
  `tmp/e2e-http-server.ps1`，临时改 `latestReleaseApiUrl()` 指向它，测完就删）：
  用裸 `TcpListener`（`HttpListener` 要 URL ACL）发一份 `releases/latest` 的 JSON 和一个装着
  自家新 exe 的 slim zip，让一个从 `build/dist-release` 拷出来的沙箱实例自己走完
  「检查 → 下载 → sha256 → 解压 → 换名 → 重启」。沙箱 exe 要用**带临时钩子**的那份
  （否则没人点菜单，它不会自己检查），zip 里的“新版本”要用**不带钩子**的那份
  （否则重启后会再检查一次、无限循环）。同样的手法也能验 `install.ps1`
  （把 `$ApiLatest`/`$DownloadBase` 改成本地地址，返回一个不存在的版本，
  于是脚本完整走完「取版本 → 下载 → 失败」而不真装）。
* **`--quit` 成功与否别只看退出码，也别只看那一瞬间的进程表**（常驻已经开始关闭时
  `Get-Process` 仍可能看得到它）：可靠的判据是 stdio 里那句 `flowkeyd: <path> exited`
  加稍后为空。收尾脚本要**轮询**等它真的消失，还有残留就 abort（不构建也不重启）。
* **`Start-Process` 起的后台进程会把继承的 stdout/stderr 一直握着**，让调用方一直等 →
  起后台服务就把两个流都重定向到文件。
* **`--check` 之类的输出中文经 agent 的 bash 抓回来是乱码**（控制台代码页）→
  验收脚本把窗口标题与前台窗口的类名/码点写进 `-WorkDir\diag.txt`（UTF-8、无 BOM）当诊断，
  要读结果就用 read 工具读那个文件，别盯控制台猜。
* **调试脚本插入诊断的正确姿势**：把脚本复制到 `tmp/`，用
  `[System.IO.File]::ReadAllText/WriteAllText(..., UTF8Encoding($true))` 做字符串替换
  （**必须保 BOM**），再加一条只含 ASCII 的探针把失败的检查名写进 diag。
  **把临时脚本的中文检查名打到控制台会被代码页弄成乱码，很容易误判成另一条检查挂了。**
* **窗口切换器的行为只能靠验收脚本验证**（`PopupHost` 要真 QML 窗口，单测碰不到）：
  用**注入的「轻碰 Win」+ 窗口标题前缀**这套组合从外面看。两个容易踩的点：
  （1）触发发生在 **Win 键松开时**，所以“第二次轻碰”进来时卡片已经开着；
  （2）遮断标记要是没生效，外壳会弹开始菜单、卡片会在 300 ms 内自己关掉 ——
  所以“卡片还在前台”要**再等半秒**再断言，只看“它出现过”会放过这种失败。

---

## 11. 完成定义（DoD）细则

一个任务算完成，必须同时满足：

1. `cmake --build --preset debug` 与 `--preset release` **都绿**（零新增警告；
   warning 当错误处理，直到项目所有者另有要求）。
2. `ctest --test-dir build/windows-debug --output-on-failure` **全绿**（测试目标只在 debug
   profile 里构建）；新增/修改的逻辑都有对应测试（`core/`、`lua/`、模型层这些可测的部分）。
3. 如果动了钩子/引擎/分发/窗口后端：跑 `scripts\acceptance.ps1`（只跑 release 那一份），
   把结论写进本文件。动到脚本覆盖不到的界面（托盘菜单、日志窗口、`animate`）时，
   仍然要人眼过一遍。
4. **测试绝不执行真实的系统电源动作**（§工作约定 10）。
5. `flowkeyd --check --config flowkeyd.lua.example` 通过（零警告）。
6. 用户可见行为有变化时更新 `README.md`，有新经验时更新本文件。
7. `git commit`：提交信息里说明**为什么**（尤其是引入新依赖时）。
8. 仓库里不留垃圾：`tmp/`、`build/`、临时配置文件都在 `.gitignore` 里。
   **发布目录 `build/dist-release/` 里只允许有 `flowkeyd.exe` 与它需要的运行时**。
9. **保证 `build/dist-release/` 是最新发布包**（§工作约定 11）——这是「用户日常按的快捷键
   真的跑在新构建上」的唯一保证。

---

## 12. 本期不做的（有意留白）与后续工作

1. **`--simulate <SCRIPT>`**（把脚本化按键事件重放给真正的引擎）。这是最便宜的引擎验证手段，
   **强烈建议尽早补**：不需要焦点、不装钩子，却能把匹配/吞键/重复/挂起/重映射全跑一遍。
2. **`--selftest` / `--probe`**（各平台后端探测与自检）。补它们的收益：Core Audio 的 COM
   vtable、虚拟桌面接口表、未公开 API 的可用性这些只能靠真实调用验证的东西，会有一条确定的、
   幂等的检查路径。
3. **完整的 `scripts/e2e.ps1`**。`scripts/acceptance.ps1` 已经是它的第一版；还缺动画的屏幕
   采样、托盘菜单点击、自提权的 UAC 流程，以及日志窗口那一套的外部断言。
   扩展时沿着现在这套脚本走：断言字符串（英文日志、窗口标题格式）是稳定的，正是为了这个。
4. **鼠标钩子**（`WH_MOUSE_LL`）。
5. **延迟修饰键抑制**：让 `Ctrl+Alt+H` 也隐藏 Ctrl 和 Alt（相对 AutoHotkey 唯一真正的行为
   差距）。
6. **配置文件热重载**（去抖的 `ReadDirectoryChangesW`）。
7. **按应用限定的快捷键**（等价于 AutoHotkey 的 `#If WinActive(...)`）。
   * 唯一的例外是 §2 第 26 条的「远程桌面放行」：那只是一个明确的前台条件（属主进程命中
     名单），做成全局策略 + 单条例外，而不是通用的按应用绑定。
8. **把 Lua 函数当动作**：刻意不做（声明式动作才能被 `--list` 显示、在加载时校验完、
   并在钩子/工作线程边界上保持安全）。
9. **配置里的 `require`/模块支持**（现在只有一份脚本）。
10. **`NumLock` 关闭时小键盘的导航键与主键盘同名键的区分**（做法可以照抄小键盘 Enter 的
    伪码表）。注意这是**行为变化**：`keys = "Up"` 将不再匹配小键盘的 `8`。
11. **弹窗的条目图标**与更细的动画。
12. **帮助窗口的模糊搜索、按 `comment` 分组**。
13. **日志窗口的增强**：`--follow`/`--grep` 之类的参数、把 `INFO` 与 `DEBUG` 分色渲染。
14. **托盘图标跟随 explorer 重启**（处理 `TaskbarCreated`）。
15. **把发布包再缩到更小**：再往下（静态链 Qt、把 Qt 自己的 QML 模块也编进 exe、单文件
    自解压）要换一套 Qt 构建或引入新的打包机制，为了几十 MB 不划算。

---

## 13. 在哪里扩展

* **新动作类型**：`core/action.h` 加一个变体 → `Action::summary` 加摘要 →
  `app/dispatcher` 里处理它 → 如果它带参数，加进 `core/config` 的 `validate_action`
  （顶层动作与 `menu` 条目共用它）→ 在 `lua_prelude.lua` 里加构造器并挂进 `flowkeyd` 表
  （构造器如果是“原样返回用户表”的那种，**记得自己补 `type`**）→ 写进 `README.md` 的表格与
  `flowkeyd.lua.example` → **在 `scripts/acceptance.ps1` 里加一条能自动验证的检查**。
* **新的选单条目字段**（例如图标）：`config` 加字段 → `MenuModel` 与 `MenuPopup.qml` 里画出来
  → 校验（重名、空标签）→ README 表格。条目是在委托的 `contentItem` 里用锚点摆的，
  **不要再把行几何（矩形）往模型里塞**。
* **帮助窗口的新内容或新交互**：条目在 `app/dispatcher` 的 `openHelpAction()` 里从 `Compiled`
  的 `bindings`/`remaps` 生成（与 `--list` 看同一批数据，所以 `help` 没有配置参数），
  纯逻辑在 `HelpModel`，界面在 `HelpPopup.qml`。
  **另一条线**：模型层与 `core` 之间现在只靠一个 `destructive` 布尔量连着，
  要让一行能执行动作就在 `openHelpAction()` 的 `targets` 里给它一个条目
  （绑定用 `press`+`release`，`remap` 用两串 `SendOp`），**不要往 `HelpEntry` 里塞
  `core::Action`**。新加模型角色名时注意别和标准控件自己的属性撞名。
* **窗口切换器的新行为**：纯逻辑在 `WindowListModel`（筛选/选中/按键决定，
  `tst_window_list_model` 直接测），行里画什么在 `SwitchPopup.qml`，
  **窗口本身的开/关在 `PopupHost::showSwitch()`**（它只能看到窗口、看不到按键，
  所以像「再按一次同一个快捷键 = 关掉」这类判断要放在那里，而且要想清楚「触发发生在
  按键的哪一半」——「轻碰 Win」是**松开**时触发的）。每加一条可从外面观察到的行为，
  就在 `scripts/acceptance.ps1` 的「窗口切换器」那一段加一条检查：那是这条路径唯一的
  自动化覆盖。
* **新的 QML 弹窗**：`import QtQuick.Controls.FluentWinUI3`；颜色一律从 `palette`
  （`base`/`text`/`placeholderText`/`highlight`/`highlightedText`/`alternateBase`/`mid`）取；
  字号用现在这套 `pointSize`（12.5 标题 / 11 正文 / 10.5 帮助正文 / 9 副标题与徽标 /
  8.5 细节）；**中文一律 `font.family: "Microsoft YaHei"`**；需要滚动的列表用真正的
  `ListView` + `ScrollBar`；固定表头/底部提示看 `HelpPopup.qml` 的
  “`topMargin`/`bottomMargin` + 不透明底色”三件套；**避开 FluentWinUI3 不支持的那些控件**；
  文件开头写 `pragma ComponentBehavior: Bound`，并用 `qmllint -I …` 确认零警告；
  别忘了加进 `PopupHost::preload()` 与 `flags: … | Qt.Tool`。
* **给弹窗/日志窗口加新的 QML `import` 时，先看 `cmake/PruneRuntime.cmake` 的保留名单**：
  用了别的东西（比如 `QtQuick.Dialogs`）就要从清单里拿掉对应的删除项，否则弹窗在启动预热
  那一步就会报 `could not load ….qml: module "…" is not installed`
  （`scripts/acceptance.ps1` 有一条哨兵检查盯着；开发期跑 `build/windows-debug` 也会报）。
  改了清单就会自动重新链接（`LINK_DEPENDS`），不用手动清构建目录。
* **新的电源操作**：`PowerOp` 加变体 → `platform/win/power` 里处理（需要特权的先调
  `enable_shutdown_privilege()`；不需要的要放在它**之前** return）→ `as_str` 与简写 →
  README 表格。**不给它加自动化测试。**
* **改配置模式（新字段/新取值）**：`core/config` 加字段 → 需要的话在 `lua_prelude.lua` 里加
  构造器 → `flowkeyd.lua.example` 里加一条（`--check` 会立刻告诉你它能不能过校验）→
  README 表格。**字段名不要用 Lua 关键字**（`repeat`/`end`/`for`/`local`/`function`/`then`/
  `until`……）；需要的话给它一个 Lua 友好的别名。
* **新的窗口摆放字段**：`core/config` 加字段 → `lua_config.cpp` 的 `convertWindowRule` 加白名单
  与读取 → `lua_prelude.lua` 的 `window_rule` 注释 → `core/placement` 里影响几何/匹配
  （或只影响 `summary()`）→ `app/dispatcher` 的 `placeWindowOnce` → README 的 `window_rule`
  一节与示例 → `tst_placement`/`tst_config`/`tst_lua`。如果新字段要调新的 Win32/COM 后端，
  放在 `platform/win/*` 里，并在 `tst_interactive` 里加一条真机验证。
* **新的 app 字段**：`core/config.h` 的 `AppDef` 加字段 → `lua_config.cpp` 的 `convertApp` 加
  白名单与读取 → 在 `core/config.cpp` 的 `expandApps` 里决定怎么继承/忽略
  （纯逻辑，`tst_config` 直接测）→ `lua_prelude.lua` 的 `app` 注释 → README 与示例 →
  `tst_config`（展开）/`tst_lua`（转换）。如果新字段是“可被动作局部覆盖的一整份值”
  （像 `launch`），照 `LaunchFields` 的做法把“写了哪些键”记下来。
* **新的窗口条件**：`core/window_match`（纯逻辑）+ `platform/win/window` 的枚举适配 +
  `launchThenActivate` 回退 + 手工冒烟清单里加一条用例。
* **新按键或别名**：扩展 `core/keys` 的键表并加一个往返用例（要有一个测试遍历表里的每个名字）。
  如果那个键要靠扩展标志才能与别的键区分（像小键盘的 Enter），还要在
  `key_from_hook`/`native_key` 里加一条翻译。
* **新的 Windows 版本的虚拟桌面接口**：往 `platform/win/desktop` 的版本表里加一条
  （生效的 `build.revision`、两个 IID、vtable 布局），然后在真机上确认选中的条目、桌面数量
  与序号。
* **新的虚拟桌面能力**：先在 `platform/win/desktop` 里手写那个接口的 vtable 结构体，
  字段下标以 VD.ahk / MScholtes 的实现为参考，并尽量用**已公开**的 API 做一次可验证的
  交叉检查（`GetID` 只认“有且只有一个匹配”；`PinView`/`UnpinView` 用 `IsViewPinned` 验证
  状态真的变了）。失败时只报错、不要去做可能是错的事。
  **先判断那个 IID 是否随版本变化**：变的（如 `IVirtualDesktopManagerInternal`）进版本表；
  不变的（如 `IVirtualDesktopPinnedApps`）只用一个常量。
* **新的未公开 API**：在 `platform/win/nt` 里用 `GetProcAddress` 解析，使用前先用一次无害调用
  校验，并永远保留一个已公开的回退。已公开但不在静态链接集合里的库走同一条路
  （`dwmapi`、`imm32` 是范例）。
* **远程桌面检测的新信号**（例如「本进程跑在远程会话里」的
  `GetSystemMetrics(SM_REMOTESESSION)`）：纯逻辑放 `core/remote_desktop.*`，判定点只有
  `platform/win/hook.cpp` 的 `updateRemoteDesktop()`（钩子线程），引擎只接一个 bool
  （`Engine::setRemoteDesktop()`）。加信号时记住三件事：名单要可配
  （`settings.remote_desktop`）、默认名单要窄、状态翻转时释放被放行的重映射按键由引擎负责。
  只加一个内置进程名时改 `builtinRemoteDesktopProcesses()` + README + 示例配置 +
  `tst_remote_desktop` 四处就够。
* **新的动作后端**：在 `src/platform/win/` 下新建模块，从 `dispatcher` 调用。
* **在线更新的新行为**（启动时自动检查、签名校验、把运行时也一并升级）：纯逻辑在
  `src/core/update_check.*`，状态与文案在 `src/app/update_model.*`，联网/下载/校验/落盘在
  `src/app/updater.*`，换文件与重启在 `src/platform/win/update.*`，卡片在
  `UpdatePopup.qml`（按钮通过 `PopupHost::updateXxx()` 转给 `Updater`）。
  改“去哪儿查”只改 `core::updateRepository()` / `latestReleaseApiUrl()`；新增一个状态就在
  `UpdateModel::Phase`/`phaseName()`/`statusForPhase()`/几个 `canXxx()` 里各加一条
  （`tst_update_model` 盯着）。**不要在 `main` 里提前做替换**：换 exe 必须在运行时停干净之后。
  验证走 `tst_update`/`tst_update_model`/`tst_update_install`、`tst_interactive` 的联网用例，
  以及 §10 那个“本地假 GitHub”的沙箱 E2E。
* **换图标**：改仓库根目录的 `logo.svg`（唯一真源），然后
  `cmake --build --preset debug --target icons` 重新生成 `assets/flowkeyd.ico` 与
  `assets/icons/flowkeyd-<n>.png`（**两者都要提交**）；尺寸列表写在
  `tools/icon_gen/main.cpp`（`kSizes`）、`CMakeLists.txt` 的 `qt_add_resources` 与
  `src/app/app_icon.cpp` 里，**三处要一起改**。exe 图标走 windres、托盘图标走 qrc，
  两份产物用的是同一张源图。
* **新的日志窗口行为**：尾随逻辑在 `app/log_model`（纯逻辑、可单测），渲染在 `LogWindow.qml`。
  加命令行参数就改 `cli.cpp` 并更新 README 的命令行表格。
* **构建版本号的来源**：全部在 `src/core/version.*`；显示侧（托盘、启动日志、`--version`、
  `--help`）不用动。
* **发布包里新增/删除文件**：`scripts/release.ps1` 的 `$SlimFiles` / `$DependencyPatterns`
  就是“哪个文件进哪个包”的**唯一清单**；`dist` 里出现两边都不认识的文件时发布脚本会直接失败。
  新的部署产物按“每次构建都会变吗”分类：会变的写进 `$SlimFiles`（进完整包与精简包），
  不变的（新加的运行时 dll / 插件 / QML 模块）写进 `$DependencyPatterns`（只进完整包）。

---

## 14. 配置 schema 速查

**这一节只是索引，权威定义在 `README.md`「配置」那一章与 `flowkeyd.lua.example`。**

| 项 | 值 |
| -- | -- |
| 配置目录 / 日志文件 | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log` |
| DSL 全局表 | `flowkeyd.*`（挂在脚本全局） |
| 示例配置 | `flowkeyd.lua.example` |

CLI 开关：`-c/--config`、`--no-elevate`、`--console`、`--elevated`、`--check`、`--list`、
`--list-keys`、`--quit`、`--no-autostart`、`--remove-autostart`、`--log-window`、
`--log-level`、`--log-file`、`--no-color`、`--allow-multi`、`--no-prompt`、`--updated-from`、
`-h/--help`、`-V/--version`。

### 容易做漏的语义

* `settings{}`：`log_level`、`swallow`、`exact_modifiers`、`release_modifiers`、
  `repeat_interval_ms`、`repeat_delay_ms`、`tick_ms`、`input_backend`、`single_instance`、
  `elevate`、`remote_desktop`。**未知键报错。**
* `hotkey{}`：`keys`（单个和弦或一组）、`name`、`trigger`（`press`/`release`/`repeat`）、
  `action`（别名 `press`、`on_press`）、`on_release`、`swallow`、`repeatable`
  （`true` 或 `{ interval_ms, delay_ms }`；也认 `["repeat"]`）、`enabled`、`comment`、
  `remote_desktop`。
  `trigger = "repeat"` 与 `repeatable = true` 是同一件事；**互相矛盾的组合要被拒绝**。
* `remap{}`：`from`（键或和弦）、`to`（键名或发送脚本）、`mode`（`hold` 默认 / `tap`）、
  `swallow`、`name`、`remote_desktop`。
* `remote_desktop`（`settings` 一项 + 每个 `hotkey`/`remap` 一项）：见 §2 第 26 条。
  `settings.remote_desktop` 是 `true` / `false`，或者 `{ enabled = …, processes = { … } }`；
  条目上的 `remote_desktop = true` = 在远程桌面里也照常拦（默认 `false` = 放行）。
* `window_rule{}`：见 §2 第 13 条。
* `app{}`：见 §2 第 15 条；声明式写法叫 `apps`。
* 和弦语法：`~` 放行原始按键、`*` 忽略额外修饰键；`Numpad*` 与主键盘同名键不同。
  **和弦的结构是 `Modifiers + 一个按键`**：`keys = "NumpadSub+NumpadAdd"` 是语法错
  （`NumpadSub` 不是修饰键），守护进程会直接拒绝启动。
* **单个字母的键名必须小写**（§2 第 17 条）。
* 动作：`run`/`send`/`type`/`open`/`volume`/`media`/`clipboard`/`window`/`notify`/`menu`/
  `help`/`windows`/`power`/`desktop`/`caps_lock`/`suspend`/`reload`/`quit`/`none`，字段逐条见
  README 的动作表。简写字符串：`"run:…"`、`"send:…"`、`"type:…"`、`"open:…"`、
  `"notify:t|b"`、`"volume:up"`、`"media:next"`、`"clipboard:get"`、`"window:minimize"`、
  `"desktop:1"`、`"power:sleep"`、裸关键字 `reload`/`quit`/`help`/`windows`/`none`。
* **完全没有动作**的快捷键就是一个按键屏蔽器（会吞掉它匹配到的按键）。
* `window` 的 `toggle`（默认**开**）只对 `op = "activate"` 有意义；`launch` 回退不套用它；
  显式 `toggle = false` 才关闭。“已经激活”要同时满足：前台、未最小化、
  **就在当前虚拟桌面上**。
* `window` 动作顶层的 `wait_ms` 是 `launch.wait_ms` 的简写；没有 `launch` 可覆盖时要报错。
* `window` 的 `animate`（默认**关**）只对会改变窗口状态的 `op` 有意义。
* `windows([title])` 是窗口切换器（§2 第 21/22/24 条）。常见绑法是 `keys = "LWin"` +
  `trigger = "release"`（「轻碰 Win」）。

### 本机真实配置不进仓库

**不要在本仓库里记录某台机器上真实使用的配置文件内容**（绑定的清单、`--check` 的计数、
配置文件的路径、以及“本机现在跑的是哪一个 exe”）。仓库里只放产品文档与参考配置；
真实配置是用户自己的东西，**改了它不需要同步 README 或本文件**。
要看当前绑定时直接看那份配置文件本身与 `flowkeyd --list`。

---

## 15. 待确认 / 需要用户拍板的事情

1. **示例配置要不要保留 `Win+X`/`Win+1..3` 这类会吞系统快捷键的例子？** 现在保留并加了
   醒目注释（说明“这会吞掉系统快捷键”），但这是产品口味问题。
2. **要不要做一份“英文日志 + 中文帮助”的文案约定表？** 目前只在工作约定第 4 条里写了原则。
