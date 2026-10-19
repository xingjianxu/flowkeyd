# AGENTS.md

给 AI agent（以及人类）的 **flowkeyd** 开发笔记。**先读这个文件。** 它记录环境、能用的命令、
**已经拍板的设计决策**、不能破坏的不变量、以及已经浪费过时间的坑。

> **本文件是 flowkeyd 唯一的工程笔记，`README.md` 是唯一的用户文档。**
> 配置 schema、动作字段与键名的权威定义在 `README.md` 的「配置」一章与
> `flowkeyd.lua.example`。任何不一致以 README 为准并修本文件。

## 工作约定（项目所有者设定）

1. 每个任务结束后更新本文件，把新经验与新要求写进去。
2. 每个任务结束后**构建 debug 与 release 两个 profile**（两条优化路径的警告都要挡住）；
   **单元测试只在 debug 上构建和运行**（`ctest --test-dir build/windows-debug`）；
   `scripts/acceptance.ps1` 只用 release 产物。**release 构建 + debug 测试全绿才算完成**，
   纯文档任务同样适用。分工固定：**debug = 开发**（构建全部测试目标）；**release = 发布**
   （`FLOWKEYD_BUILD_TESTS=OFF`，不构建任何测试目标），并自动产出干净的发布包
   `build/dist-release/`。`build/windows-release` 或 `dist-release` 里出现测试产物、构建系统
   文件（`*.a`、`CMakeCache.txt`、`Temp`）就算 bug。
3. 目标平台是 **Windows**；可以使用未公开的 Win32 API。
4. **代码注释、本文件、README、示例配置一律用中文。日志与错误信息保持英文**
   （配置校验、`--check` 输出也一样）：日志/校验信息是机器可断言的字符串，中英混排也难读。
   用户可见的 CLI 帮助文本用中文。
5. 可以自行结束本项目（flowkeyd）正在运行的进程，不必事先征求许可。停法：
   `taskkill /PID <pid>`（**不带** `/F`）让它走干净退出；`/F` 只在杀不掉时兜底（代价是留下
   幽灵托盘图标）。这条只管 flowkeyd 自己。
6. 需要用户暂时别用这台机器时（会抢前台/焦点、会注入按键的冒烟验证），用提问形式提醒但
   **不等回答**，约 3 秒后自行继续；提醒里写清预计时长与影响范围。
7. **依赖按需引入，每次引入都要在提交信息里给出理由。** 默认只有 **Qt + `vendor/lua`**。
8. **agent 的 bash 是极简 WSL，`/bin` 下只有 `bash`/`mount`/`login`。** 看文件用 read/edit
   工具；跑命令一律走 `powershell.exe -NoProfile -Command "…"`，或绝对路径的外部程序
   （如 `C:\Users\xingjian\scoop\apps\git\current\cmd\git.exe`）。
   **不要在命令行上直接拼中文**（代码页会变乱码）：写成脚本文件，或只用 ASCII 模式串。
   用 bash 给 `powershell.exe -File` 做重定向时**不要写 `*>`**（bash 会拆成 `*` + `>`，还会在
   仓库根目录建出名字带反斜杠的垃圾文件）；用 `/mnt/d/…` 形式的路径 + 单个 `>`。
9. **接管已完成（2026-09）：现在常驻的就是 flowkeyd**，不要再去拉起别的实现。常驻实例由
   计划任务 `flowkeyd`（登录时 + 最高权限 + 15 秒延迟）拉起，任务指向**当前正在运行的 exe
   路径**：守护进程每次启动都会检查并（重新）注册它。停止用 `flowkeyd.exe --quit`
   （不要 `taskkill /F`）；删除自启用 `flowkeyd.exe --remove-autostart`（先 `--quit`）。
   `scripts\install.ps1` / `uninstall.ps1` **已删除** —— 不要写部署脚本，也不要把常驻指向某个
   预设目录（任务跟着 exe 走）。
   * **例外**：仓库根目录的 `install.ps1` 是给**外部用户**的一键安装器（从 GitHub Release
     下载完整包、校验 sha256、解压到 `%LOCALAPPDATA%\Programs\flowkeyd`、可选启动），不参与
     自启任务。README 最上面那条 `powershell -nop -c "irm <raw>/install.ps1 | iex"` 用的就是它。
     **必须保持纯 ASCII、无 UTF-8 BOM**：`irm` 会把 BOM 留成真实的 `U+FEFF`，
     `Invoke-Expression` 随即在 `param()` 上报 `ParserError: InvalidLeftHandSide`。带参数跑就用
     `curl -o` 落盘 + `powershell -File`，或 `&([scriptblock]::Create((irm <url>))) -Version …`。
     再改它时：全部字节 ≤ 0x7F、不写 BOM（行尾不用管，`core.autocrlf=true`）。
   * **本机 agent 的 `powershell.exe` 是不是提权的会变，每次都自己测**
     （`IsInRole(Administrator)`）再决定怎么拉起常驻：提权时 `Start-Process`；**不提权时千万
     不要**（守护进程自己会 `ShellExecuteW("runas")`，没人点 UAC 它会一直卡在里面）。
     不提权就用已经注册好的任务：`& build\dist-release\flowkeyd.exe --quit` 然后
     `schtasks /Run /TN flowkeyd`。先停再拉起很重要：不先停，新实例会弹一个「已在运行」原生框
     卡住。
   * **不要用 `taskkill /IM flowkeyd.exe` 停常驻**：`taskkill`（不带 `/F`）给它的顶层窗口发的
     是 `WM_CLOSE`，而 `--quit` 事件那条路才算“干净退出”（第 5 节）。
   * 收尾脚本里 `--quit` / `schtasks /Run` 那两行**不要紧跟在中文注释后面**：`tmp/` 下的一次性
     `.ps1` 是无 BOM 的，PS 5.1 按 GBK 解码时中文注释会**静默吞掉下一行** —— 真踩过，现象是
     「`--quit` 什么都不打印、常驻实例还在跑」，很容易被当成 `--quit` 坏了。
     **`tmp/` 的脚本一律纯 ASCII。**
   * 开发期起 flowkeyd 一律 `--no-elevate --allow-multi` + 一次性配置，并且**不要**占用用户真实
     配置里已有的和弦（看那份配置文件本身或 `flowkeyd --list`；真实配置不进仓库，见第 12 条）。
     `scripts/acceptance.ps1` 用一次性配置。
10. **任何自动化测试都不得触发真实的系统电源动作。** `shutdown`/`restart`/`logoff`/`sleep`/
    `hibernate`/`lock`/`screen_off` 一个都不许真执行；测试代码里不出现
    `platform::win::power::execute()`（`tst_power_table` 只测纯逻辑表）。这些动作只有用户自己
    按键、或点选单条目时才允许发生。
11. **每个任务收尾时 release 产物必须是最新的**：`cmake --build --preset release` 会自动把干净的
    发布包写到 `build/dist-release/`（只有 `flowkeyd.exe` 与它需要的 Qt/MinGW 运行时）。
    常驻实例会锁住它自己那个 exe，所以收尾（两条 profile 都通过、debug 测试全绿之后）按这个顺序：

    ```powershell
    & build\dist-release\flowkeyd.exe --quit
    & 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
    Start-Process build\dist-release\flowkeyd.exe   # 不提权时改用 schtasks /Run /TN flowkeyd
    ```

    * 只有在常驻实例确实从 `build/dist-release` 跑的时候才需要先 `--quit`。它跑在别的路径
      （用户自己拷走的拷贝）时不要动用户的实例，只要保证 `build/dist-release` 是最新的。
    * 纯文档任务通常 `ninja: no work to do`：什么都不用做。
    * 不想动二进制就别在提交之后再构建（提交会改分支 ref、触发重新配置 + 重新链接）。
    * **刚发过版、之后只改了文档 / 脚本的提交，不要重新构建**：HEAD 已经离开已发布的那个修订，
      再构建出来的 `dist-release` 就不是发布页上那一份了（版本号里的修订也跟着变）。让它停在已
      发布的那一份上，等下一次真要发版时由 `scripts\release.ps1` 一起构建。
12. **不要把某台机器上真实使用的配置文件写进仓库**：只放产品文档与参考配置（`README.md`、
    `flowkeyd.lua.example`）。真实配置的绑定清单、`--check` 计数、路径，以及“本机现在跑的是哪一个
    exe”都不记录。**修改真实配置后不需要更新 README 或本文件。**
13. **可配置的东西必须三处同时到位：`README.md`（schema 的权威定义，必要时连
    `flowkeyd.lua.example`）、`src/core/config_map.cpp` 的 `*ToMap`/`*FromMap`、以及
    `src/qml/` 里对应的图形化界面（页 / 弹层 / `ActionEditor` 的 `visible: root.type === "…"`
    分支，外加 `ConfigText.qml` 里的中文名）。** 新增、修改、删除任何一个字段或取值（含默认值
    与取值范围）都要把这三处一起改；`settings`/`hotkey`/`remap`/`window_rule`/`app`/`launch`/
    选单条目与全部 20 种动作一视同仁。
    * **界面那一侧漏接不会报任何错**：字段被默默丢掉、或者用户在界面里根本改不了它（第 10 节
      有两个实例）。`tst_config_writer` 的整份配置往返与 `tst_config_editor` 的示例配置往返
      只盯住第 2 处；第 1 处与第 3 处没有自动检查，只能靠这份清单人肉过一遍。
    * 查缺口时**不能只 grep 字段名**：`name`/`enabled` 这种名字在 README 别处也会出现，
      要按「每一节的字段表」逐行对照代码里的白名单（`src/lua/lua_config.cpp` 的 `checkFields`
      与 `convertAction`）与界面（`config_map.cpp` + QML）。
    * 只改真实配置（不进仓库的那份）不在此列，见第 12 条。

---

## 1. 这个项目是什么

`flowkeyd` 是一个由 **Lua 脚本**配置的 Windows 键盘钩子守护进程，用 **C++20 + Qt 6** 写成。
它安装一个 `WH_KEYBOARD_LL` 钩子，匹配按键和弦，按需把匹配到的按键从前台应用那里隐藏掉
（AutoHotkey 的行为），然后执行绑定的动作（启动进程、发送按键、控制音量/媒体、操作窗口、
切换虚拟桌面与剪贴板、弹出通知、弹出选单、弹出快捷键帮助、窗口切换器、程序启动器、
睡眠/关机/重启/关屏）。它也能做按键重映射。一句话：**用 Lua 配置、用 Qt/C++ 写的 AutoHotkey。**

UI 只有托盘图标与五个 QML 卡片（日志窗口、`menu` 选单、`help` 帮助、`windows` 窗口切换器、
`apps` 程序启动器、`update` 在线更新），全部是 **Qt Quick（QML）+ FluentWinUI3 样式**，
跑在**同一个进程**里。

**当前的非目标**：鼠标钩子、把 Lua 函数当动作（配置是脚本，但动作只能是声明式的表/字符串）。
（**图形化配置界面已经做了**，见第 2 节第 34 条。）

| 方面       | flowkeyd                                                                       |
| ---------- | ------------------------------------------------------------------------------ |
| 语言/框架  | C++20 + Qt 6.11（Quick/QML 做 UI）+ 手写 Win32 声明                            |
| Lua        | **Lua 5.5.1**（`vendor/lua`，`lua/lua` @ `v5.5.1`）                             |
| DSL 全局表 | **`flowkeyd.*`** + 全局构造器                                                  |
| 配置/日志  | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log`                 |
| 注入标记   | `dwExtraInfo` 里的 `"FLOW"`                                                    |
| 互斥体     | `Local\flowkeyd-<配置路径散列>`                                                 |
| 开机自启   | 计划任务 `flowkeyd` 指向**当前运行 exe**（启动时自注册自检）                    |
| 在线更新   | 托盘菜单 *检查更新* → GitHub `releases/latest` → slim 包 → sha256 → 换 exe + 重启 |
| 自动化测试 | Qt Test 单元测试 + `scripts/acceptance.ps1`（166 项检查，实跑 163：3 条数字键检查常被跳过；需交互式桌面） |
| 依赖管理   | CMake Presets + Ninja，`vendor/lua` 静态编进二进制                             |

---

## 2. 已经拍板的决策（不要再重新讨论）

每一条都是项目所有者明确选择的。**不确定要不要改就先问用户**，不要自作主张地“改进”。

1. **应用形态：CLI 守护进程 + QML 弹窗，没有主窗口。** `main.cpp` 走 `QApplication` +
   `QQmlApplicationEngine` + `QSystemTrayIcon`；命令行参数是唯一的控制面。
2. **Lua 用 `vendor/lua` 里现成的 5.5.1**，不换成 5.4。与 5.4 的差异见第 8 节。
3. **分阶段实施**（第 9 节）；自动化测试以 Qt Test 为主，桌面行为由 `scripts/acceptance.ps1`
   覆盖，脚本覆盖不到的靠手工冒烟（第 5 节）。
4. **配置 schema 与 CLI 开关是 flowkeyd 稳定的公开接口**：字段名、取值、动作字段、DSL 构造器名
   （`settings{}`/`hotkey{}`/`remap{}`/`window_rule{}`/`app{}`/`run()`/`send()`/`type_text()`/
   `open()`/`notify()`/`volume()`/`media()`/`window()`/`clipboard()`/`caps_lock()`/`suspend()`/
   `desktop()`/`power()`/`menu{}`/`help()`/`windows()`/`reload()`/`quit()`/`none()`）都要在
   `README.md` 里写清楚，改动当**破坏性变更**对待（同步更新示例配置与 README）。脚本全局的注册表
   表名是 **`flowkeyd`**。
5. **flowkeyd 最终接管这台机器**（第 10 阶段已完成）。
6. **不做 `--simulate` / `--selftest` / `--probe`**（第 12 节）。`--check` / `--list` /
   `--list-keys` 保留（既是产品功能，也是手工验证的主要工具）。阶段 9 用**只给测试用的后门**
   `FLOWKEYD_ACCEPT_INJECTED=1` 抬升“丢弃注入输入”那道过滤，配合 `scripts/acceptance.ps1` 做验收。
   这是对“当时无法验证”的修订，**不变量 2 本身没动**：flowkeyd 自己注入的事件带着 `"FLOW"` 标记，
   仍在钩子回调第一步被丢掉。
7. **依赖政策：默认只有 Qt + `vendor/lua`。** 需要新依赖时按需引入，但必须在提交信息里给出理由。
   JSON、CLI 解析、字符串工具都自己写或用 Qt 自带的；不要引入 `sol2`、`nlohmann::json`、
   `CLI11`、`spdlog` 之类“顺手”的库。
8. **构建：CMake Presets + Ninja，debug 与 release 双 profile 都必须编译通过。** debug 里构建单测
   （`FLOWKEYD_BUILD_TESTS=ON`），release 里不构建单测、只产出 `build/dist-release`。单元测试只在
   debug 上跑，验收脚本只跑 release 的产品 exe。
9. **弹窗界面用 Qt 自带的标准控件，不自绘。**
   * 列表是 `ListView` + `ItemDelegate`；筛选框是真正的 `TextField`；**模型里要给 QML 调的方法一律加
     `Q_INVOKABLE`**（否则 QML 抛 TypeError，而且**处理器里后面的语句会被静默跳过**，看上去像
     “处理器没跑”）。
   * **鼠标左键点一行 = 选中它 + 把它的按键复制走**；滚动（滚轮、拖滑块）只滚视图、**不动**选中项。
   * **`Enter`（或双击一行）= 关掉窗口并执行那一行的动作**。执行前先关窗是刻意的：`send`/`type`/
     `window` 作用在**前台窗口**上，不关窗就会打回弹窗自己的筛选框。
   * **`quit`/`suspend`/`power` 要按两次**（`core::isDestructive()`）：第一次只是“武装”（行变色 +
     底部提示换确认文案），再按一次才真执行；`Esc`、上下换行、改筛选都取消。**`menu` 不算危险**。
   * 列表只占行区域（`topMargin`/`bottomMargin` + 不透明底色盖住滚到边缘的行），键盘把选中项带进
     视野用 `ListView.positionViewAtIndex(..., Contain)`。
   * **滚轮方向跟着系统/Qt**（本机实测 `mouseData=-120` 往下、`+120` 往上）。
   * 选单（`MenuPopup`）同一套：标准 `ListView` + `ItemDelegate`，**不滚动**（条目数决定卡片高度），
     左键点一行 = 执行它。区别只有悬停仍驱动高亮（`Enter` 执行光标下那一条，这是菜单的语义）。
   * **中文字体用 `font.family: "Microsoft YaHei"`**：默认族 `Segoe UI Variable` 没有中文字形，
     不管的话中文会回退到宋体。`Window`/`Item` 没有 `font` 属性、不会往下传，每个会画字的控件都要写。
10. **开机自启：计划任务指向当前运行的 exe**（不是安装目录）。任务 `flowkeyd` 在登录时以**最高
    权限**启动一个 exe（提权但不弹 UAC），路径由守护进程每次启动时自检/刷新
    （`platform/win/autostart`，内部走 `schtasks /Create /XML`，XML 以 UTF-16LE + BOM 写到 `%TEMP%`）。
    **不写任何安装目录、没有部署脚本。**
    * 开发/测试实例跳过：`--no-elevate`、`--allow-multi`、未提权、显式的 `--no-autostart` 都不碰
      计划任务——否则临时实例会把用户的开机自启劫持到一个会被清理的构建目录（任务指向失效路径是
      **完全静默**的失败）。
    * 删除自启用 `--remove-autostart`（需管理员；先 `--quit`）。
    * 任务参数（都与当年 `install.ps1` 的 XML 逐字段一致）：`PT15S` 登录延迟、
      `ExecutionTimeLimit=PT0S`、`DisallowStartIfOnBatteries=false`、`StopIfGoingOnBatteries=false`、
      `InteractiveToken` + 指定 `UserId`、`MultipleInstancesPolicy=IgnoreNew`、`RestartOnFailure`
      PT1M×3、`WorkingDirectory` = exe 所在目录。**不用** BootTrigger（跑在 session 0，没桌面），
      不用 Task Scheduler COM（要手写十来个 vtable）。
    * **注册/刷新之前先问用户**（原生确认框）；同意才动任务，拒绝就保持原样。`--no-prompt` 跳过询问、
      按默认「注册/更新」处理（脚本用）。
11. **启动时的两个交互确认**，都用**原生 `MessageBoxW`**（跑在 `QApplication` 构造之前）：
    1. **已经在运行**：同一配置文件有实例在跑就弹 `flowkeyd 已在运行`，点确定后以**退出码 1** 退出。
       **这一步刻意放在 UAC 提权之前**（真正的互斥体获取仍在提权之后，兜住“两个进程同时启动”的竞态）。
    2. **开机自启**：任务缺失或指向别的 exe 时弹 `flowkeyd 开机自启` 问要不要更新。

    `--no-prompt` 跳过这两个提示。守护进程出错（配置错误）弹的是 `QMessageBox`（标题
    `flowkeyd 配置错误`，正文是英文错误）；离线命令那条路只打印、不弹窗、不提权。
12. **构建版本号 = `yy-MM-dd-<git 短修订>`**（`core/version.*`，例如 `26-09-22-42900ad`）。
    * **日期**取运行中这个 exe 自己的最后写入时间（本地时间 `yy-MM-dd`）——**不用编译期常量**
      （那会让每次构建都重新链接，`ninja: no work to do` 就不再成立）。
    * **修订**是构建时由 CMake 用 `git rev-parse --short HEAD` 取到、经 `cmake/version_revision.h.in`
      写进 `build/<preset>/generated/flowkeyd_revision.h` 的编译期常量（只给 `core/version.cpp` 包含）。
      CMake 把 `.git/HEAD` 与**当前分支的 ref 文件**登记成 `CMAKE_CONFIGURE_DEPENDS`（只在 configure
      时那个 ref 确实以松散文件存在才会登记；在 `packed-refs` 里就会漏，提交后要显式 configure 一次）。
    * 显示在四处：托盘右键一个**不可点**的*版本*信息项、守护进程启动日志第一行、`--version` 第一行、
      `--help` 表头。取不到 exe 时日期段是 `unknown`；不在 git 仓库里时修订段是 `unknown`。
    * **工作流是「先提交再构建」**：哈希就是构建时的 HEAD（工作区脏时显示最近一次提交）。提交之后
      **懒一下也没关系**，但踩过一次「刚提交完直接 `cmake --build --preset release` 是 no work to do、
      生成的 `flowkeyd_revision.h` 还是上一个提交」——为了不把旧哈希的包当成新的发布，**收尾时显式
      `cmake --preset windows-release` 一次（或者看一眼 `--version` 的哈希）再构建**是最稳的。
13. **窗口摆放规则 `window_rule{...}`**：控制“某个程序启动时出现在哪个 workspace 和 monitor”，
    且**显示器重新接入时按配置重新归位**。字段：`process`/`title`（至少一个）、`desktop`、
    `all_desktops`、`topmost`、`monitor`、`maximize`、`x`/`y`、`width`/`height`、`name`、`enabled`。
    * `monitor` 只支持**序号**（1 起，左→右、上→下）、`"primary"`、**设备名**（`"DISPLAY2"` /
      `"\\.\DISPLAY2"`）；不支持按分辨率匹配。
    * **默认最大化**（写了 `monitor` 又没写位置/大小时铺满工作区），位置与大小可配（`x`/`y` 相对
      目标工作区左上角）；`maximize = true` 与位置/大小互斥。只写 `desktop` 时**不动窗口几何**。
    * 触发时机只有三个：**窗口第一次出现**、**显示器重新接入**（判据是设备名从无到有，350 ms 轮询）、
      **flowkeyd 启动时**。之后不再干预。
    * `all_desktops`（`true` 钉在所有虚拟桌面 / `false` 显式取消，不写就不碰）走
      `IVirtualDesktopPinnedApps`（**不是** `IVirtualDesktopManagerInternal`），**与 `desktop` 互斥**。
    * `topmost` 走已公开的 `SetWindowPos(HWND_TOPMOST)`。这两个开关**都算“设置一次”**、不是几何：
      只写它们时窗口大小/位置不动，也不会因为“默认最大化”突然变大。
    * “主窗口”判据：可见、无属主、非 `WS_EX_TOOLWINDOW`、有标题、尺寸非零。
    * 实现分散在 `core/placement`（纯几何/匹配）、`platform/win/monitor`、
      `platform/win/desktop::moveWindowToDesktop`、`platform/win/hook`（`SetWinEventHook` + 显示器
      轮询）与 `app/dispatcher`。
14. **跨虚拟桌面的“唤起”与跟随。**
    1. `window` 动作的 `activate`：目标窗口不在当前虚拟桌面上时不算“已经在前台”，按下去就是**切到
       它所在的那张桌面并激活它**；只有它已经在当前桌面并且真的在前台时才是 `toggle` 的收起。
    2. `window_rule` **真的**把窗口搬到另一张桌面时，视图也跟过去并重新激活那个窗口。
       **只在“窗口第一次出现”那一遍做**（启动 / 显示器重新接入那两遍只重新摆放）。
       “真的搬动” = 窗口的桌面 GUID 变了。

    原因：`MoveViewToDesktop` 把窗口搬走之后 shell 仍然把它当**前台窗口**，所以“是不是已经激活”的
    判定不带虚拟桌面时，同一个快捷键会去*收起*用户看不见的窗口；而且这种情况 `SetForegroundWindow` /
    `AttachThreadInput` / `BringWindowToTop` 全部返回 TRUE 却什么都不做，必须先显式 `SwitchDesktop`。
15. **`app{...}`：把同一个程序的窗口规则与快捷键写在一起**（只是书写上的合并，`compile()` 把它展开成
    一条普通 `WindowRuleDef` + 若干 `HotkeyDef`，展开出来的条目**排在全局 `hotkey{}` / `window_rule{}`
    之后**）。
    * 字段只有 `name`/`process`/`title`/`launch`/`window`/`hotkeys`/`enabled`；`process` 与 `title`
      至少写一个。
    * `window` 就是一条 `window_rule`（字段完全一样），`process`/`title`/`name` 自动继承，显式写的
      优先；不写 `window` 就只展开快捷键。
    * `launch` 就是这个程序怎么启动，字段与 `window` 动作的 `launch` 一样（`program`/`args`/`cwd`/
      `show`/`shell`/`env`/`wait_ms`/`elevated`）。它也是**默认值**：动作不写 `launch` 就整份继承，写了就
      **逐字段合并**；动作顶层的 `wait_ms` 覆盖 `launch.wait_ms`（没有 `launch` 却写 `wait_ms` 要报错）。
    * “写了哪些字段”由 Lua 层在转换时记进 `core::LaunchFields`（`show`/`shell`/`args` 的默认值与
      “没写”在值上分不开），见第 10 节。
    * `hotkeys` 里每一项就是一条 `hotkey`；其中的 `window` 动作自动补上 app 的
      `process`/`title`/`launch`（显式写的优先），**嵌套在 `menu` 条目里的也算**；只对表/构造器形式的
      `window` 动作生效。
    * 名字默认：`name` → `process` → `title`；app 里**只有一个 hotkey** 时也给那条绑定当默认名
      （多个时保持“第一个和弦”的默认）。
    * `enabled = false` 把整条 app（规则 + 全部快捷键）都丢掉并给一条 warning。
    * **全局的 `hotkey{}` / `window_rule{}` 保留**。声明式写法在返回表里叫 `apps`；错误标签是
      ``app #1 (`wps`)``（不写 `name` 就用 `process`）。
16. **`window` 动作的四个「挪窗口」op**（复用同一套目标查询、`swallow` 与 `--list` 摘要）：
    * `move_prev_desktop` / `move_next_desktop`：只动**虚拟桌面**，窗口几何不变，**视图不跟着走**
      （与 Windows 自己的 `Win+Ctrl+Shift+←/→` 同义）。两张桌面**首尾相接**。走
      `desktop::moveWindowToAdjacentDesktop`（复用 `MoveViewToDesktop`）。
    * `move_left_monitor` / `move_right_monitor`：只动**显示器**，虚拟桌面不变。**保留最大化状态**；
      普通窗口保持原有大小并**居中**到目标工作区；最小化的只更新还原位置。没有更左/更右那块时
      失败并记日志，**不循环**。
    * 四个 op 都**不套用 `toggle`**、**不接受 `launch`**；`core::windowOpHasTransition()` 对跨显示器
      移动返回 true、对跨虚拟桌面移动返回 false —— 写在不产生过渡的 op 上的 `animate` 会被 `--check`
      报错。
    * 相邻下标由纯函数 `core::stepIndex(count, current, delta, wrap)` 算（虚拟桌面 `wrap = true`、
      显示器 `wrap = false`），有单测（`tst_placement`）。

17. **字母键名一律小写。** 配置里凡是表示按键的单个字母（`keys` 的按键、`remap` 的 `from`/`to`、
    发送脚本 `{...}` 里的键名、选单条目的 `key`）都写小写；`a` 就是 A 键，要按住 Shift 的“大写键”
    必须**显式**写 `Shift+a`，直接写大写的 `A` 会被 `--check` 拒绝
    （`letter key names must be lowercase: write ...`）。
    * **例外：发送脚本里的裸字符保持 AutoHotkey 语义** —— `send("A")` 就是打出大写 A
      （等价于 `send("+a")`），`send("Hello")` 照旧能打出 `Hello`；要按字面输入任意文本用
      `type("...")` 或 `send("{Text}...")`。花括号里的是**键名**，所以 `send("{S}")` 报错、要写
      `send("{s}")`（要 Shift 就写 `send("+s")`）。
    * 实现：`core/keys.cpp` 的 `uppercaseLetterKey()`（只识别“单个大写字母”这一种形状）+
      `parseChord` / `parseSendScript` 两处检查 + `core/config.cpp` 的 `validateMenu` 与 `remap`
      的 `to` 检查。`parseKeyOrScript()` 本身保持宽松。`nameFromKey()` 与 `allKeyNames()`
      （`--list-keys`）一律输出小写。
18. **`window` 的 `follow` 选项**（只对 `move_prev_desktop`/`move_next_desktop` 有效，默认 `false`）：
    `follow = true` 时搬完窗口**把视图也切到目标桌面**并重新激活它。这就是 `Win+i`/`Win+u` 与
    `Win+Shift+i`/`Win+Shift+u` 的区别。
    * 配置层：`Action::follow`（`std::optional<bool>`）；`--check` 只允许它出现在这两个 op 上（写在
      别的 op 上是静默空操作，所以报错）；`--list` 摘要里显示 `(follow)`。
    * 搬窗口与切视图在**同一个 STA 会话**里做完：先 `MoveViewToDesktop` 再 `SwitchDesktop`，用
      **已知的目标下标**，不去读窗口的桌面 GUID（那个是异步的）。之后 `window::applyTo()` 再补一次
      `raiseWindow()`。
    * “保持激活”是尽力而为：`raiseWindow` 失败只写 warning，动作仍算成功（detail 里带
      `, could not activate`）。
    * **它不产生窗口过渡**（只切视图、不动几何），所以 `animate` 的规则不变。
19. **前台窗口是覆盖层时跳过它。** 不写 `target`/`process` 的 `window` 动作作用于
    `GetForegroundWindow()`；但按住 Win 约一秒会弹出 **PowerToys 的「快捷键指南」**
    （`PowerToys.ShortcutGuide.exe`，`WS_EX_TOOLWINDOW | WS_EX_TOPMOST`）并成为前台窗口，于是动作会去
    操控那个覆盖层（跨桌面移动直接报 `could not read the desktop id of the window`）。现在
    `window::find(foreground)` 发现前台是别的工具窗口时，沿 Z 序往下找第一个“主窗口”（判据同上）。
    * 顺带把 `desktop` 层收紧了两处：`Session::windowDesktopId(hwnd, attempts)` 可重试（默认 1 次；
      `desktopIndexOfWindow` 用 3 次）——`GetWindowDesktopId` 在 `MoveViewToDesktop`/`SwitchDesktop`
      之后、或窗口处于过渡状态时会返回 `TYPE_E_ELEMENTNOTFOUND`(0x8002802b)；
      `Session::moveWindowToAdjacent()` 搬完**等桌面 GUID 真的变了再返回**（否则按住 Win 连按两次会
      读到旧桌面、把目标算错）。
20. **托盘图标的主体是「当前是第几号虚拟桌面」。**
    * **画法**：蓝色渐变圆角方块（`logo.svg` 的 `#00d2ff` → `#3a7bd5`，圆角比例照抄 `rx = 56/256`）
      + 白色粗体数字居中。数字按**字形墨迹**居中（按含行距的方框居中会明显偏高）。
    * **两位数一律显示 `9+`**（16 px 上两位数挤成一团）。画什么字、用什么字号是纯逻辑
      （`core::desktop_badge`，`tst_desktop_badge` 盯着），图片是 `app::desktopIcon()`（9 个尺寸各自
      渲染，不缩放位图）。
    * **查不到时退回应用图标**（锁屏、非交互会话、版本表对不上）：不画 `0`、不留空白图标；悬停提示里
      的 `（桌面 N/M）` 也跟着消失。
    * **来源是 500 ms 的轮询**，因为虚拟桌面没有任何“切换了”的通知。轮询放在**动作线程**上
      （`Dispatcher::startDesktopWatch()`，由 `Runtime::start()` 排队投进去——`QTimer` 必须在它自己
      那条线程上创建），查到的值经 `Runtime::reportDesktop()` 投回 GUI 线程换图标，**绝不在 GUI 线程上
      做 COM**。查失败只在状态翻转时写一条 debug 日志，并**保留上一次的数字**，免得锁屏时图标来回闪。
    * `desktop::currentDesktopIndex()` 是给这条轮询用的精简只读查询；`windowsVersion()` 的结果因此改成
      **进程内缓存**（否则每 500 ms 读一次注册表）。
    * 与 `desktop` 动作共用同一张未公开的版本表，所以**只有能读到桌面时**才有数字。
21. **窗口切换器 `windows()` 与「轻碰 Win」。** `windows()` 弹出一张卡片，列出当前打开的程序窗口；
    输入就按**进程名前缀**把窗口筛掉，筛选到**只剩一个窗口时直接激活它**。
    * 枚举判据是 `win::window::listOpenWindows()`：可见、无属主（或带 `WS_EX_APPWINDOW`）、非
      `WS_EX_TOOLWINDOW`、有标题、尺寸非零、不是 `Progman`，**跳过 flowkeyd 自己的进程**，而且
      **只列 shell 真的会显示的窗口**（见下一条）。列表按 Z 序（最近使用在前），跨虚拟桌面的窗口也会
      列出来（激活时会把视图切过去）。
    * **被 shell 藏起来的“假窗口”不进列表**（例如 `TextInputHost.exe` 的「Windows 输入体验」：
      `IsWindowVisible` 为真、尺寸正常、却点不到）。`win::window::isSwitchableWindow()` =
      `isMainWindow()` + 「没被 cloaked，或者只是被搬到别的虚拟桌面上了」：`DWMWA_CLOAKED` 非零 =
      没显示出来，而 `desktop::isWindowOnCurrentDesktop()` 又为真 = shell 藏在当前桌面上的宿主窗口。
      cloaked 但在**别的**虚拟桌面上的窗口要留着（`windows` 动作会切过去），所以判据里必须带上那个
      桌面查询；它也**只对 cloaked 的窗口问**（COM 不便宜）。纯判据在 `core`（`TopLevelWindowFacts` +
      `isMainWindow()` / `isSwitchableWindow()`），`platform/win/window.cpp` 只负责取 Win32 值。
      `window_rule` 不走这一条。
    * 筛选是**进程名的大小写无关前缀匹配**（标题只显示、不参与），**不是子串、也不是模糊搜索**：
      打 `chr` 命中 `chrome.exe`，打 `hrome` 不命中。自动激活的判据是「只剩一个窗口」，不是「只剩一个
      进程」。
    * **「轻碰 Win」= 单个修饰键 + `trigger = "release"`。** 引擎对这种和弦采用「tap」语义
      （`Engine::m_pendingTaps`）：按下修饰键本身**放行**（因此 `Win+E`/`Win+L` 这些没被接管的系统组合
      不受影响），期间只要有别的按键按下就作废，单独松开时才触发；触发时把既有的菜单遮断标记立起来、
      注入 `VK_UNASSIGNED` 挡掉开始菜单。**单个修饰键配默认的 `trigger = "press"` 保持老行为**
      （按下即触发、可吞键）。
22. **窗口切换器的数字选择模式。** 筛选串命中的窗口**全属于同一个进程名**、而且不止一个时，卡片进入
    「窗口选择模式」：前 10 行依次分到 `1`..`9`、`0`，按一下数字直接跳过去；超过 10 个的不分配
    （`rowKey` 是空串，那一行不画徽标）。
    * 触发判据：**筛选串非空 + 可见窗口全属于同一个进程名 + 至少两个窗口**。空筛选绝不编号；命中多个
      进程名时也不编号。与 `setFilter()` 的「只剩一个窗口就直接激活」互不冲突（那边是 1 个窗口）。
    * 这种模式下 `handleKey()` 先把 `Qt::Key_1`..`Key_9`/`Key_0` 接掉，所以它们**不会跑进筛选框**；
      没有对应行的数字被吃掉但什么都不做。不在这种模式时数字键照旧放行给 `TextField`（`7zip` 这类
      进程名要能用数字筛）。
    * 纯逻辑在 `WindowListModel`（`m_numbered`/`m_rowKeys`，角色 `rowKey`；`footerText` 在这种模式下
      换成「数字键直接切换」，所以从 `CONSTANT` 改成 `NOTIFY stateChanged`）。界面只是把 `rowKey` 画成
      行左侧的徽标（空串时不占位，标题与进程名跟着缩进 30 px）。
23. **五个弹窗不进任务栏，而且启动时就预热好。**
    * **不进任务栏**：`MenuPopup.qml` / `HelpPopup.qml` / `SwitchPopup.qml` / `UpdatePopup.qml` 的
      `flags` 都加 `Qt.Tool`（Windows 上就是 `WS_EX_TOOLWINDOW`）。`Qt.Tool` 窗口仍然能被激活、
      能拿键盘焦点（验证过），只是不加任务栏按钮、不进 `Alt+Tab`。日志窗口**没有**改（它留在任务栏里
      是对的）。选 `Qt.Tool` 而不是 `Qt.Popup`：后者要抓鼠标、点外面自动关。
    * **首次弹出不再现场付钱**：`PopupHost::preload()` 在启动后的第一个事件循环回合被 `main` 排队
      调用，把五个窗口建出来、填一份假数据各渲染一帧（**透明度 0 + 屏幕之外**，用户看不到也点不到），
      首帧到了就藏起来。实测冷启动第一次弹出要 **224 ms** 才画出第一帧（~170 ms 是进程首次渲染的固定
      开销：QRhi/D3D11 设备、交换链、Quick 材质着色器首次编译；~50 ms 是该窗口的 QML 加载与样式装配），
      预热之后第一次弹出 **~30 ms**。
    * 预热用的假数据**不需要**清理：每次真实弹出都会先 `setItems` 覆盖。行数写得多于一屏
      （menu 8 行、help/switch 14 行）是为了让 `ListView` 把一屏的 `ItemDelegate` 与 `ScrollBar` 也
      装配一遍。
    * **屏幕之外是必须的**：透明度 0 的置顶窗口留在屏幕里万一赶上鼠标点击就会吃掉它。
    * 真实弹出要先 `cancelPreload()`（用户可能在启动后的那 300 ms 里就按了快捷键）；用
      `QSet<QQuickWindow*> m_warming` 判断，**不要用全局“代”号**（会把另外几个还在预热的窗口的首帧
      回调一起废掉）。
    * **显示器配置 / 缩放一变就把窗口缓存丢掉重建**（`discardPopupWindows()` + 重新 `preload()`，
      模型留着）：隐藏 + 摆在屏幕外的窗口收不到 `WM_DPICHANGED`，缩放变过之后它们会停在旧缩放上，
      弹出时只有正确大小的一半 —— 项目所有者 2026-10 报的「启动器尺寸又变小了」就是它（细节、实测
      数据与两层修法见第 10 节「弹窗缩放过期」）。
24. **窗口切换器卡片的几处调整。**
    * **没有标题行**：卡片里只剩筛选框、列表与底部提示；筛选框就是第一行，`listTop` = **50**
      （`kPad 12 + 筛选框 30 + 间隙 8`），卡片高度 238（3 行时）。计数挪到了**底部提示**里
      （省掉与筛选框占位文本重复的「输入筛选」）；`countText()` 保留。
    * **每一行是「进程名在上、窗口标题在下」**（项目所有者 2026-09 要求）：主行是进程名
      （`pointSize 10.5` + `palette.text`），窗口标题退成副行的浅色小字（`8.5` +
      `palette.placeholderText`）。两个字符串还是只有都非空时才分两行。理由：筛选匹配的本来就是进程名，
      标题只用来分辨同一个程序的多个窗口。这只是 QML 里两个 `Label` 的上下关系，**模型层不知道也不管**。
    * **卡片开着时再按一次同一个快捷键 = 关掉它**（与 `Esc` 同义）。判断在
      `PopupHost::showSwitch()`：`window->isVisible() && !warming` 时直接 `switchDismiss()` 返回，
      **不要在那里重新弹一次** —— 「轻碰 Win」的触发发生在 Win 键**松开**时，所以第二次轻碰进来时卡片
      正开着；重新弹就是「关掉又立刻重开」。`windows()` 动作因此是「开/关」切换。
    * `windows()` 的 `title` 参数现在只用作**窗口标题**（`caption()` = `<title> — N 个`），验收/诊断
      脚本靠它读“现在列了几个窗口”。
    * **列表宽度 = 筛选框宽度**：QML 里 `ListView` 的 `x`/`width` 直接用 `filterRect`。
    * **打开时切英文输入法**：筛选框匹配的是**进程名**（ASCII），而用户经常开着中文输入法。新模块
      `platform/win/ime.*`（运行时解析 `imm32.dll`）：读 `ImmGetConversionStatus`，
      `NATIVE`（`IME_CMODE_NATIVE`）置位时把它清掉（其余标志与句模式原样保留，等价于用户按一下
      `Shift`）。Qt 在 Windows 上**不看** `Qt.ImhPreferLatin`（`QWindowsInputContext` 只用
      `ImEnabled` 决定要不要 `ImmAssociateContext`），所以 QML 里写提示没用。调用点两处：
      `PopupHost::showSwitch()` 弹出后一次，以及筛选框 `onActiveFocusChanged` 时一次；**预热期间
      不调**。**关掉卡片时把打开前那份模式写回去**（`ime::readMode()` 快照、`ime::restoreMode()` 写回；
      调用点在 `switchChoose`/`switchDismiss`/`closeAll`）。快照只在真正弹出时记一次。只影响 flowkeyd
      自己这个进程的输入模式，**但本进程的几个窗口会互相看见**。
25. **在线更新：托盘菜单里点一下，自己换 exe 并重启。**
    * **下载的是发布脚本已经传上去的精简升级包**（`*-slim-windows-x64.zip`，约 700 KB，里面只有
      `flowkeyd.exe`），没有第三种资产。解压用 Qt **私有**的 `QZipReader`（`Qt6::CorePrivate`）；
      某次发布万一没有 slim 包就退回到完整包。
    * **只换 `flowkeyd.exe`，不动 Qt/MinGW 运行时。** 新版本如果新增了运行时依赖，替换会**回滚**
      （新 exe 活不过 2.5 秒 → 把旧的那份改回来）并提醒用户手动下载完整包。
    * **触发方式是手动的**：只有点托盘菜单才联网，启动时不检查。匿名 `GET /releases/latest`，请求带
      `User-Agent` 与 `Accept: application/vnd.github+json`。
    * **替换顺序**：旧 exe 改名成 `<exe>.old` → 新的改名就位 → 启动新实例 → 等 2.5 秒确认它还活着。
      运行中的映像可以改名（不能删/覆盖）。任何一步失败都回滚；`.old` 由**新实例**启动时删（旧进程还在
      退出时删不掉就隔 0.7 秒重试，最多 20 次）。
    * **`--updated-from <旧版本>` 是重启时唯一的额外参数**（内部标记）：新实例靠它知道“我刚被更新过”，
      于是弹一条托盘通知，并且**只认它**（用户自己重启不会重复提示）。
    * **下载下来的 exe 的 mtime 被对齐到发布日**（版本号里的日期段取 exe 的 mtime，见第 12 条）：
      不对齐的话更新完重启，版本号会变成“下载那一天”。
    * 替换需要能写安装目录（默认提权运行，正常路径没影响）；更新**不碰**配置、日志与开机自启任务。
      更新失败时的模态框在 `--no-prompt` 下不弹（自动化用）。
    * 新增静态库 **`flowkeyd_update`** 与 `platform/win/update.*`；一张 QML 卡片 `UpdatePopup.qml`
      （同样预热、同样 `Qt.Tool`）；`Qt6::Network` 进入依赖，所以 `cmake/PruneRuntime.cmake`
      **必须留下 `tls/qschannelbackend.dll`**。
26. **远程桌面放行（`settings.remote_desktop`，默认开）。** 前台窗口的属主进程命中名单（默认只有微软的
    RDP 客户端：`mstsc.exe`、`msrdc.exe`、`msrdcw.exe`、`RdClient.Windows.exe`）时，**所有快捷键与
    重映射一律放行** —— 不拦截、不触发，键原样送给对面那台机器；单条例外是条目自己的
    `remote_desktop = true`（在远程桌面里也照常拦、照常执行）。
    * 判据只有「**前台窗口的属主进程名**」（大小写无关的子串，与 `window_rule.process` 同一套），不看
      标题、不看是否全屏。`GetSystemMetrics(SM_REMOTESESSION)` 那条路（“flowkeyd 自己跑在远程会话里”）
      **没做**。
    * 检测全在**钩子线程**上：`SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` 立刻更新（故意**不带**
      `WINEVENT_SKIPOWNPROCESS`：自家弹窗拿到前台就算“不在远程桌面里”）、启动时查一次、350 ms 的
      placement tick 再兜一次。按键路径上没有额外开销。
    * 名单空表 = 谁都不算（等于关掉）；`processes` 里的**空串是错误** —— 空串在
      `windowProcessMatches()` 里表示“不限制”，会让**所有**窗口都算远程桌面。运行时也跳过空条目
      （双层保险），见第 10 节。
    * 进入这个状态时清掉长按重复与待定的「轻碰 Win」，并把**已经被放行**的重映射按住的**目标键松开**
      （`remote_desktop = true` 的例外不动）；已经吞掉、还按着的键在松开时仍然吞掉。
    * 纯逻辑在 `core/remote_desktop.{h,cpp}`；引擎侧是 `Engine::setRemoteDesktop()`，平台侧只有
      `platform/win/hook.cpp` 的 `noteForegroundWindow()`。
27. **程序启动器 `apps()`：开始菜单里的程序，摆成一张图标网格。** 绑定写法就是普通快捷键
    （本机绑在 **`Win+Space`** 上，会吞掉 Windows 自己的输入法切换；真实配置不进仓库，见第 14 节）。
    * **数据源 = `shell:AppsFolder`**（项目所有者 2026-10 拍板「扫开始菜单」，后来发现两个
      `Start Menu\Programs` 目录里只有 `.lnk` —— 商店应用与 `.msc` 系统工具根本不在那里）。
      它就是开始菜单「所有应用」列的**同一份**（`Get-StartApps` 也是它）：经典程序快捷方式、
      **商店/UWP 应用**、系统工具（`.msc`）全在里面。枚举 = `SHParseDisplayName("shell:AppsFolder")`
      + `SHBindToObject(IShellFolder)` + `EnumObjects`，每条读 `PKEY_AppUserModel_ID` /
      `PKEY_Link_TargetParsingPath` / `PKEY_Link_Arguments`（PKEY 的 GUID/PID **手写在 `apps.cpp`
      里**，不用 `<propkey.h>`：那些符号在 MinGW 的 `libuuid` 里）。真机：159 条 → 138 个程序
      （21 条被过滤），枚举 + 解析约 80 ms。
    * **过滤两道**（判据都在 `core::app_list`，纯逻辑、`tst_app_list` 盯着）：`appTargetIsProgram()`
      留 `.exe`/`.bat`/`.cmd`/`.msc`/`.cpl`、shell 虚拟项（`::{…}`）与目标为空但有
      `<包家族名>!<AppId>` 的商店应用，丢掉文档 / 帮助 / 网址（`.chm`/`.txt`/`.url`/`http(s)://`/
      `steam://`）；`appLooksLikeUninstaller()` 再丢掉**卸载程序**（名字含「卸载」或独立的
      `uninstall`，或者目标文件名是 `unins000.exe` 这类）。**不按名字猜“这算不算噪音”**：
      `Visual Studio Installer`、`Qt Maintenance Tool`、「【小狼毫】输入法设定」都留着。
    * **名字就是 shell 给的显示名**（`SIGDN_NORMALDISPLAY`）—— 与开始菜单逐字一致，跟系统语言走
      （中文系统上是「文件资源管理器」「命令提示符」）。
    * **启动走 `shell:AppsFolder\<AUMID>`**（`apps::launchApp()`）：先把解析名当 `lpFile` 交给
      `ShellExecuteExW`，失败再走 `SHParseDisplayName` + `SEE_MASK_IDLIST`（两条真机都验过）。
      参数、工作目录、`runas` 标记、商店应用的激活全部交给 shell。**代价是提权实例启动的程序也是提权
      的**（与 `run` 同一个已知限制）—— 2026-10 之后这一条由 `apps::launchAppAsUser()`（必要时经
      普通权限的代理进程）修正，见第 2 节第 35 条。
    * **列表按需重扫**：缓存在动作线程上，最多 30 秒；`Runtime::start()` 还会排队扫一次，所以装完程序
      不用重启 flowkeyd。
    * **网格是 6 列 × 6 行**（`AppListModel` 的 `columns()` / `cellWidth()` / `cellHeight()`），
      格子 126 × 88、图标 40，**卡片 800 × 622**（宽度 = 两边内边距与缩进 + 6 × 126）；屏幕不够高时
      按高度自动收行（`setMaxRows` / `rowsForAvailableHeight`）；`↑`/`↓` 走一整行、`←`/`→` 走一格、
      `PgUp`/`PgDn` 翻页、`Home`/`End` 到头尾，到边界夹住不回绕。筛选是**名字的子串**（与窗口切换器的
      前缀匹配不同）。卡片尺寸是项目所有者 2026-10 定的（原来 564 宽 / 5 列）：**宽度必须从
      “内边距 + 列数 × 格宽” 算出来**，不要只改宽度不改格子。改这几组数字时用 `tmp/preview/` 那套
      （真 `PopupHost` + 真 QML + 真开始菜单扫描）抓一张图看过：这次得到 1600 × 1244 设备像素
      = 800 × 622 逻辑像素 @200%，图标与两行名字都在格子里、没有被裁切。
    * **筛选到只剩一个也不自动启动**（那是“切窗口”的语义；这里会真的拉起一个进程）。卡片开着时再按
      一次同一个快捷键 = 关掉它。**不切输入法**（与窗口切换器相反）：名字可能是中文。
    * **图标必须异步取**：真机实测每个图标约 **3 ms**（首次冷启动那一个可达 148 ms，159 个条目连取
      一遍约 400 ms）——同步做会让卡片等小半秒，而且 `QQuickImageProvider` 的同步回调跑在渲染线程上
      （那里没有 COM 单元）。所以 `app::AppIconProvider` 是一条常驻 STA 线程 +
      `QQuickAsyncImageProvider`：模型给每一行一个 `image://flowkeyd-app/<core::appIconKey(启动名)>`，
      图标取好就 `finished()`，用户看到图标一格格“长出来”。键是启动名的 64 位 FNV-1a（小写十六进制），
      所以 URL 里没有反斜杠/空格/中文，而且同一个程序永远是同一个 URL。
    * **尺寸按屏幕缩放**：QML 的 `Image.sourceSize` 给设备像素（逻辑 40 × `Window.devicePixelRatio`；
      用 `Window.devicePixelRatio` 而不是 `Screen.devicePixelRatio`，后者要额外
      `import QtQuick.Window`，发布包会多一个 QML 模块）。
    * 与 `app{...}` 区分：那个是**花括号**的注册构造器（窗口规则 + 快捷键），这个是**圆括号**的动作；
      简写 `apps`（别名 `launcher`/`programs`）。
28. **程序启动器的右键菜单就是 Windows 自己的那一份。** 右键点一格 = 弹那个条目的**原生 shell 菜单**
    （已公开的 `IContextMenu`/`IContextMenu2`/`IContextMenu3`，实现在
    `src/platform/win/shell_menu.*`）：条目与资源管理器、开始菜单逐条一致（打开 / 以管理员身份运行 /
    打开文件位置 / 固定到“开始”屏幕 / 属性 / 卸载……），而且**动作也由 shell 自己执行**。
    * `QueryContextMenu` 用 `CMF_NORMAL | CMF_EXTENDEDVERBS`：带上“扩展动词”，所以「以管理员身份
      运行」直接就有（资源管理器里那是 Shift+右键才多出来的那批）。
    * 菜单出在**光标处**（`GetCursorPos`），不从 QML 传坐标：Qt 的全局坐标是设备无关的**逻辑**像素，
      而 `TrackPopupMenuEx` 要的是**物理**像素。
    * **菜单就在本进程自己的 Per-Monitor V2 上下文里弹，不要去动 DPI 上下文**：win32k 对 PMv2 线程
      弹的菜单按显示器逐个缩放，这正是资源管理器那份的尺寸。曾经照微软「宿主 shell 菜单」的建议把线程
      临时降级成 `UNAWARE_GDISCALED`，结果菜单**高一倍** —— 根因、实测数据与测量手法见第 10 节
      「原生 shell 右键菜单」。
    * 每次弹出都在 debug 日志里留一条 `shell menu: N item(s) for <启动名>`：验收脚本靠它把「菜单窗口
      有多高」换算成「每条多少逻辑像素」。**这条日志不要删。**
    * 每一格的委托里挂一个 `TapHandler { acceptedButtons: Qt.RightButton }`（只吃右键，左键仍然是
      `ItemDelegate` 自己的 `clicked`），并且是在**松开**时（`onTapped`）弹菜单——按下就弹的话，那次
      右键的 key-up 会把它自己关掉。
    * 项目所有者 2026-10 拍板：**选中条目就把卡片收掉，取消（`Esc` / 点菜单外面）则留着**。收卡片
      发生在执行命令**之前**（平台层 `showItemMenu()` 的 `beforeInvoke` 回调）——除了产品语义，还有
      一个实打实的理由：「属性」开出来的对话框以卡片为属主，卡片后收就会连对话框一起被藏掉。
    * 这条规则本身在 `AppListModel::afterContextMenu(invoked)` 里（纯逻辑、`tst_app_list_model`
      盯着），QML 只负责执行。预热用的假数据没有快捷方式，右键直接什么都不做。
29. **程序启动器的筛选认汉语拼音**（第 27 条的那张网格）：输入串按「名字 / 全拼 / 首字母」三段做
    **子串**匹配（三段之间用 `U+001F` 隔开，段与段拼不出假匹配）。
    * 三段由 `core::appSearchText(name)` 一次算好：`记事本` → `jishiben` + `jsb`；
      `Visual Studio Code` → 全拼那段就是它自己 + `vsc`；中文与拉丁混排的 `QQ音乐` → `qqyinyue` +
      `qyy`；空格与标点留进全拼、不进首字母。
    * **多音字的每一种读音都进搜索串**（`乐` → `le`/`yue`），读音组合数是每个字读音数的乘积，超过
      `core::kMaxSearchVariants`（8）就退回「只用主读音」。
    * 表是**离线生成后提交进仓库**的（`src/core/pinyin_data.*`，生成器 `tools/pinyin_gen.ps1`）：
      数据来自 mozillazg/pinyin-data（MIT，读音源自 Unihan 的 `kMandarin`），**只覆盖
      U+4E00–U+9FFF**（20992 个码点里 20924 个有读音，多音字 5009 个、额外读音 6499 条）。
      Ext A/B 等生僻字没有拼音。**`ü` 记作 `v`，同时额外认写成 `u` 的那一条**（女 → `nv` 与 `nu`）。
    * 因为模型要算这个串，**`flowkeyd_models` 现在链 `flowkeyd_core`**（两个都只依赖 QtCore，
      没有新增运行时依赖）。
    * 验证：`tst_app_list`（含整张表的自检：读音形状 / 无重复 / 覆盖率）与 `tst_app_list_model`；
      `tmp/preview/` 那套在真开始菜单（138 个程序）上跑过一遍，挑「Directory Opus (启动)」打首字母
      `doqd` 能筛到它。
    * **备选方案（没选）**：Windows 的中文排序确实把汉字按拼音排（`LCMapStringW` 的 sort key 主权重，
      实测 乐 落在 `le`、长 落在 `zhang`、行 落在 `xing`），但它只给出「相对顺序」而不是音节边界 ——
      要用它就得自己维护约 410 个锚字再做二分，而且权重顺序没有任何文档承诺。
30. **程序启动器分三段：已固定 / 最近使用 / 全部程序**（项目所有者 2026-10 拍板）。
    * **概览**：从上到下是「已固定」网格、「最近使用」网格（最多两行 = 12 个，`core::kRecentLimit`）、
      最后一个「全部程序（N）」按钮。**还什么都没固定、也没启动过任何程序时**概览直接就是全部程序的
      网格 —— 比让用户对着空卡片去点按钮友好。
    * **`Space` 固定 / 取消固定**当前高亮的条目（固定住的图标右上角一个小圆点）。**筛选框非空时
      `Space` 放行**给标准 `TextInput`：否则用户打不出 `visual studio` 这种带空格的名字。
    * **「全部程序」是同一张卡片里的另一种视图**（换的是 `ListView` 的行，不是第二个窗口）：一行一个
      程序、按**名字 / 拼音首字母分组**（`A`–`Z`；数字、符号、拼音表外的生僻字开头的一律 `#`）、带
      滚动条；`Esc`（或最上面那个「← 返回」按钮）回概览。分组键由 `core::appSortInfo()` 给出
      （`sortText` 带 `0`/`1` 前缀，保证「#」那一组是**连续**的）。
    * **持久状态在配置文件旁边的 `launcher.json`**（`core/launcher_state`，手写 JSON、容错解析、
      坏文件当空的）。条目的身份是**图标键**（`core::appIconKey`，与 `image://flowkeyd-app/` 同一个
      键）：目录重扫、改名字、路径大小写差别都认得出来。读写（`QFile`）在 `app::PopupHost` 里 ——
      **模型层不碰磁盘**，所以 `tst_app_list_model` 是纯逻辑。启动一个程序时记一笔「最近使用」：
      `PopupHost::appChoose()` 在把 `onChoose` 交出去**之前**记（那时 `m_appRequest` 还没被清空）。
    * **模型是「行」式的**（`rowKind` ∈ `header`/`grid`/`button`/`list`），不是「格」式的：分段、
      分组、按钮在一张 `ListView` 里天然共存；键盘选中项只是（行, 列），`↑`/`↓` 自动**跳过表头**，
      `positionViewAtIndex(selectedRow)` 就是「把选中项带进视野」。卡片高度**恒定** =
      `maxRows × cellHeight` + 上下占位（本机 800 × 622）：概览 / 筛选 / 「全部程序」列表三个视图一样高，
      **不跟内容缩** —— 卡片“过小”那次修复（2026-10-05，项目所有者判定按各行高度之和算出来的 248 高
      卡片太小）。
    * 对外的接口没变：还是 `apps([title])`，固定与最近使用是**运行时状态**，配置 schema 里没有对应的
      字段。
    * **滚动位置只在「换视图」时回到顶部**，不跟 `ListView.contentHeight` 走：那个属性在滚动中会因为
      「还没创建出来的委托按估算高度算」而抖几像素，跟着它把选中行摆进视野，就会把滚轮 / 拖滑块刚滚
      出来的位置立刻拽回第 0 行（现象是「有滚动条，但滚不动」，2026-10-05 修）。
    * **筛选之后的数字快速启动键**（easymotion 风格，项目所有者 2026-10 要求）：筛选串非空时，命中的
      前 10 条各分到一个数字，号码就是**显示序号**（第 1 个 `0`、第 2 个 `1`……第 10 个 `9`；与窗口
      切换器的 `1`..`9`、`0` **不同**），画在那一格**图标右上角**（`rowItems` 每一项的 `key` 字段，
      模型侧是 `m_itemKeys` + `numberedMode`），按一下直接启动它。只在筛选那一种扁平网格里生效。
      **没有对应条目的号码被吃掉但什么都不做**（漏给筛选框会把列表筛空）；筛选框为空时不编号，数字
      照常打进筛选框（`7-Zip` 这类名字要用数字筛）。号码出现时那格原来的「已固定」小圆点让到图标左上角。
    * **最近使用程序的跳转键 = `Alt` + 数字**（按「最近使用」顺序，第 1 个 `Alt+1`……第 9 个 `Alt+9`、
      第 10 个 `Alt+0`，最多 10 个）。**已固定程序的固定快捷键 = `Alt` + 功能键**（第 1 个 `Alt+F1`、
      第 2 个 `Alt+F2`……最多 12 个，第 13 个起没有）。
      * **为什么是 `Alt`**：裸字母 / 数字是筛选框的主要输入；功能键本身单独按还有别的含义（`F1` 是
        帮助、`Alt+F4` 是关窗），所以都带上 `Alt` 并只在自己的低层钩子之外、**卡片开着时**才有效。
      * 模型侧：`m_recentKeys`（`m_recentShown` 顺序）与 `m_pinKeys`（`m_pinnedShown` 顺序），都在
        `rebuildRows()` 里重算；徽标文字由 `recentKeyLabelForLine()` / `pinKeyLabelForLine()` 给出
        （`Alt+数字` / `Alt+F数字`）。
      * 与筛选号码**不同**：它们不随筛选变化，卡片开着的任何视图里都有效。所以筛选之后它们能启动一个
        **根本没显示出来**的固定 / 最近项 —— `activateItem()` 因此不再要求条目在当前视图里。
      * `modifiers` 必须**恰好**等于 `Qt::AltModifier`：`Ctrl`/`Shift` 的组合、`AltGr`（`Ctrl+Alt`）
        都放行给 `TextInput`。靠 `handleKey(int key, int modifiers)` 的第二个参数（QML 传
        `event.modifiers`；`qmlEntryPointsAreInvokable` 里的签名也要跟着写成 `handleKey(int,int)`）。
        已固定列表非空时，`Alt` + 没分配到的功能键也被吃掉；最近列表非空时同理吃掉没分配到的数字。
      * **筛选号码优先显示**：同一格既是固定 / 最近项、又在前 10 条结果里时画号码，但 `Alt` + 它的
        数字 / 功能键照样有效。徽标上写的是完整按键（`Alt+3` / `Alt+F1`）。「全部程序」列表里固定在
        右端画的是同一个徽标（没有键时才是小圆点）。
31. **启动器的目录是三份来源拼的：开始菜单 + 控制面板 + 内置的系统设置页**（项目所有者 2026-10 需求）。
    三份都喂给同一个模型，**筛选 / 固定 / 最近使用 / 数字快速启动键 / `Alt` 键 / 右键菜单一视同仁**。
    * **第一份还是 `shell:AppsFolder`**（第 27 条），一个字没改。
    * **第二份：控制面板的「所有控制面板项」**（`platform/win/apps.cpp` 的 `listControlPanelItems()`）。
      真机 37 个候选 → 35 条：名字（`SIGDN_NORMALDISPLAY`）、图标、启动全交给 shell，所以跟系统语言走；
      **开始菜单里只有「设置」与「控制面板」两个总入口**，「网络连接」「电源选项」「程序和功能」
      「设备管理器」「声音」「鼠标」这些单项只在控制面板里。
      * 用 `SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder)` 拿文件夹 —— 真机验过它就是
        `::{26EE0668-A00A-44D7-9371-BEB064C98683}\0`（`Shell.Application.Namespace(3)` 看到的那个，
        36 条），**不是 `{26EE0668-…}` 本身**（那是 8 个分类的分类视图）。
      * 启动名 = `SIGDN_DESKTOPABSOLUTEPARSING`。**必须验证它能解析回去**（`SHParseDisplayName`
        成功）才采用，否则退回 `SIGDN_FILESYSPATH`；两个都不行就丢掉这条。真机上有两条被丢掉：
        没显示名的那个（shell 把解析名当名字给回来）与**「字体」**（解析名里带的是本地化显示名）。
        这两个规则分别在 `entryFromAbsolutePidl()` 与 `launchNameForPidl()` 里，**宁可少一条也不给一个
        按下去没反应的格子**。
      * 另外硬编码了一个解析名：**「网络连接」**（`::{7007ACC7-3202-11D1-AAD2-00805FC1270E}`）。
        它是 delegate folder，不在「所有控制面板项」的枚举结果里，但项目所有者点名要它。
    * **第三份：内置的 26 个 `ms-settings:` 页面**（`core/app_list.*` 的 `windowsSettingsPages()` /
      `builtinSettingsEntries()`）。这些页在 shell 里**既没有本地化名字也取不到图标**
      （`SIGDN_NORMALDISPLAY` 返回的就是 URI 本身），所以名字是内置的中英对照（`QLocale` 第一项是
      `zh*` 就用中文，否则英文）、图标借「设置」应用自己那一张（`core::appIconLaunchName()`：
      `ms-settings:` 前缀 → 设置应用的 AUMID，**键仍然是 URI** —— 每个页面各一个身份）。
      名字带 `设置：` / `Settings: ` 前缀，所以在「全部程序」里聚在 `S` 那一组，输入「设置」/`shezhi`/`sz`
      能一次全筛出来。
    * 合并只在 `Dispatcher::refreshAppCatalog()` 里做（不在平台层）：三份接起来时**同名条目丢掉后面的**
      （`core::appendLauncherEntries()`，开始菜单那份在前）。真机上只撞一条（「Windows 工具」），结果
      138 + 35 + 26 → **198 条**。日志行也扩成了「三份各多少 → 合计多少」。
    * 验收：`scripts/acceptance.ps1` 的启动器那一段加了 3 条（用拼音首字母 `dyxx` / `sz` 筛，
      纯 ASCII 注入得了），见第 5 节。
32. **没声明固定桌面的程序：`window("activate")` 把窗口搬到当前桌面，而不是把视图切走。** 项目所有者
    2026-10 提的需求：不这样做，Windows 会把视图切到那个窗口上一次所在的那张桌面上。
    * 判据是「**命中这个窗口的第一条 `window_rule` 有没有 `desktop`**」（规则按书写顺序匹配，先写的赢）：
      `core::windowRulePinsDesktop()` / `core::firstWindowRule()` 是纯逻辑，`tst_placement` 盯着。
      * 写了 `desktop` —— 用户按快捷键就是想过去，保持老行为：`raiseWindow` 切到它所在的那张桌面并
        激活它（视图跟着走）。
      * 没写（或者压根没有规则）—— 窗口跟着用户走：`win::desktop::moveWindowToCurrentDesktop()`
        先把它搬到**当前**桌面，再激活；视图不动。
    * 只对 `op = "activate"` 生效；`launch` 回退那条路径也算（新窗口找到之后同样处理）。其余 op 行为
      一个字没改。
    * **搬必须在激活之前、而且必须等它搬完**：虚拟桌面的移动是异步的，立刻激活的话
      `window::raiseWindow` 会读到旧的桌面 GUID、把视图切过去 —— 那正是要避免的事。所以
      `moveWindowToCurrentDesktop()` 内部等桌面 GUID 真的变了再返回（`waitForDesktopChange()`）。
    * “要不要搬”先问一句**便宜**的 `isWindowOnCurrentDesktop()`（公开的 `IVirtualDesktopManager`，
      不碰版本表那套未公开接口）：已经在当前桌面上就什么都不做。问不出来（锁屏 / 非交互会话 / 窗口不
      属于任何桌面）时**不动窗口**，交给 `raiseWindow` 的老行为处理。
    * 日志里会多一段 `(brought to desktop 2/4)`（`window` 动作那条 INFO 行）。
    * `all_desktops = true` **不算**“声明了固定桌面”：窗口本来就到处都是。
33. **没写 `monitor` 的窗口摆到哪块显示器上：`settings.window_monitor`，默认 = 光标所在的那一块。**
    项目所有者 2026-10 提的需求：唤起一个**没配 monitor** 的程序时，让它出现在鼠标所在的那块屏上；
    这个行为可配，能改成“出现在之前 active 后所在的 monitor 上”。
    * `settings.window_monitor` 取值与 `monitor` 完全一样（`2` / `"primary"` / 设备名 / `"cursor"` /
      `"last"` / `"none"`），默认 `"cursor"`。`MonitorRef` 因此多了 `Cursor` / `Last` / `None` 三个
      `Kind`（`core/monitor_ref.{h,cpp}`，从 `config.h` 里拆出来是因为 `action.h` 也要用它）。
      `monitorRefIsConcrete()`（序号 / 主显示器 / 设备名）与 `monitorRefIsDynamic()`（光标 / 上次那块）
      是纯逻辑，`selectMonitor()` 对这三种返回 `nullopt`。
    * `"last"` 是**flowkeyd 自己记住的**：`<配置目录>/windows.json`，键是**可执行文件名的小写**
      （与 `process` 同一套），值是**设备名**（序号会随排列变）。只有真的用到 `"last"` 时才读/写这个
      文件。第一次还没有记忆时**不动窗口、只把落点记下来**。纯逻辑在 `core/window_monitor_state.*`
      （`tst_window_monitor_state` 盯着），落盘在 `app::Dispatcher` 的 `MonitorMemory` 上。
    * **优先级**（`core::windowActionMonitor()`，纯逻辑、`tst_placement` 盯着）：`window` 动作自己的
      `monitor` → 命中这个窗口的第一条 `window_rule` 的 `monitor` → `settings.window_monitor`。
      `monitor = "none"` 逐层都能“显式关掉”。
    * **生效时机**（项目所有者的选择：“所有 window_rule 匹配到的窗口”）：`window_rule` 的三个时机
      与 `window` 动作的 `op = "activate"`（含 `launch` 回退那条路）。于是**双击图标开出来的窗口**
      也会被搬到光标那块屏 —— 写 `monitor = "none"`（或全局 `window_monitor = "none"`）关掉。
    * **几何怎么处理**（项目所有者的选择：“保持大小并居中”，与 `move_left_monitor` /
      `move_right_monitor` 一致）：目标是动态的而且规则里没写 `maximize` / `x` / `y` / `width` /
      `height` 时，只“把它放对屏”；已经在目标那一块上就**不去碰它**。写死的三种仍然照旧（写 `monitor`
      又没写位置大小时默认最大化）。为此 `WindowRule::maximize` 的默认值只对
      `monitorRefIsConcrete()` 为真时才为 true。
    * 平台层只多两个函数：`win::monitor::indexForCursor()`（`GetCursorPos` + `MonitorFromPoint`）与
      `win::monitor::moveToMonitor()`（`moveToAdjacentMonitor` 重构后调的其实是它）。
    * `--list` 多打一行 `window monitor: … (for rules and window actions that do not name one)`。
    * 一个副带效果：以前 `window_rule{ process = "x" }`（什么都没写）会被 `--check` 以
      「only matches windows and does nothing」丢掉；现在它有意义了（摆到缺省显示器），只有在
      `monitor` 写了 `"none"` 或全局也是 `"none"` 时才会再报那条 warning。

34. **图形化配置界面：托盘菜单的「配置...」打开一个普通窗口，不用自己写 Lua 也能改配置。**
    项目所有者 2026-10 提的需求；四个方向都是当场拍板的（依次：保存怎么落地、覆盖到哪一层、
    什么窗口形态、按键怎么录入）。
    * **形态**：Windows 11「设置」那种**可缩放普通窗口**（不是那五个无边框小卡片）：左边
      导航（常规 / 快捷键 / 按键重映射 / 窗口规则 / 程序 / 高级），右边分组卡片，底部是
      状态行 + 还原 / 保存 / 关闭。**不加 `Qt.Tool`**：它就该进任务栏、能最大化。
    * **保存 = 整体重写 `config.lua`**（项目所有者选了这条，而不是双文件）：界面的单一真源是
      `core::configToMap()` 那份字典，保存时由 `core::configToLua()` 写成一份字段与文档逐字
      对应的 Lua。代价是界面之外的内容（注释、循环、变量）会没——所以**写之前总留一份
      `config.lua.bak`**，而覆盖一份**不是**界面生成的文件之前先问一句。生成的 Lua 第一行是
      `-- flowkeyd-config-editor:generated`（`isEditorGenerated()` 看的就是它）；把这行删掉，
      界面下一次就又会问。字段含义与取值一律不变，所以配置 schema 这个公开接口**没动**。
    * **覆盖全量**：`settings` 的全部字段 + 四个条目（`hotkey` / `remap` / `window_rule` /
      `app`）的全部字段 + 全部 20 种动作。界面里不存在的写法只有一种：**动作简写字符串**
      （`send("^{c}")`）与动作列表在界面上都归一到「一串动作」，加载时简写就被展开成等价的动作表
      （行为一模一样，只是写回文件时不再是简写）。
    * **保存前一定先自己读一遍**：`ConfigEditor::save()` 把拼好的文本交给**守护进程用的同一个**
      Lua 求值器 + `core::compile()`，不过就根本不落盘（所以界面不可能写出非法配置）；报错情况下
      文件一个字节都不动。
    * **条目编辑走弹层**（`HotkeyOverlay` 等，盖在窗口上的自建遮罩）：不是 `Dialog`（那要跟
      FluentWinUI3 的样式对齐，而且里面要放一整套表单）。弹层**每次新建**，于是每个输入框都从当前
      值初始化一次；「新增」的流程是「点新增 → 弹层里填 → 确定才追加」（列表里永远不会出现半成品）。
    * **和弦用文本录入 + 即时校验**（项目所有者的选择），不做「按一下录入」：录制要在录制期间把
      引擎挂起，那几秒里全局快捷键集体失效。校验向 `ConfigEditor::chordError()` 要结果，用的是与
      `--check` 同一个 `core::parseChord()`；编辑已有条目时把「它自己那几个和弦」当作例外传进去，
      否则会一直报「已经被用了」。
    * **中文显示、英文存储**：下拉框里的取值一律经 `ConfigText`（QML 单例）翻成中文，写进配置的
      还是 `"release"` / `"move_next_desktop"` 这种公开取值。三态（`跟随默认` / `是` / `否`）用
      `TriRow`：配置里有一批**可选**布尔量，一个开关表达不出「没写过」。
    * **保存成功就重载**：`ConfigEditor::saved` → `ConfigWindow::reloadRequested()` →
      `Runtime::reloadFromAnyThread()`。界面**自己不碰钩子线程**，也不弹一个「要重载吗」。

35. **启动的程序默认是「当前登录用户的普通权限」，`elevated = true` 才提权。** 项目所有者
    2026-10 拍板（四个选项都是当场问的）：选项放在**每个动作上**（不是全局设置）、`run` 与
    `open` 与程序启动器**一视同仁**、程序启动器的**右键菜单保持现状**（写进已知限制）。
    * 理由：守护进程默认提权运行，继承令牌会让**每一条**快捷键启动的东西都变成管理员进程 ——
      拖放失效、配置写到奇怪的地方、有些程序直接拒绝启动。
    * 配置面：`run` 的 `elevated`、`launch` 的 `elevated`（`window` 的 `launch` 与 `app{}` 的
      `launch` 共用同一份字段块，所以也进 `core::LaunchFields` 做逐字段合并）、`--list` 摘要里
      只给显式的提权项的尾巴加 `(elevated)`。只有 `run` / `launch` 有这一项：`open` 没有参数
      （它总是普通权限），程序启动器的例外走右键菜单自带的「以管理员身份运行」。
    * **怎么做到**（`platform/win/process.cpp`）：从登录 shell（`GetShellWindow()` 的属主进程，
      兜底 = 本会话第一个 `explorer.exe`）拿一份主令牌，`CreateProcessWithTokenW` 启动 →
      子进程落在 Medium；`elevated = true` 而自己没提权时用 `ShellExecuteW("runas")`（会弹 UAC，
      而且**没法转交 `env`**，写了 `env` 只给一条 warning）。拿不到令牌 / Secondary Logon 被禁用时
      **退回继承当前令牌**并写 warning（“按键按下去什么都不发生”比“这次提权了”更糟）。
    * **`ShellExecuteExW` 没有令牌参数**，所以 `open` 与程序启动器（`apps::launchAppAsUser()`）
      把请求交给一个**普通权限的代理进程**：flowkeyd 自己的隐藏副本，内部命令
      `--shell-open` / `--shell-app`（+ `--shell-args`/`--shell-cwd`/`--shell-show`/`--shell-result`，
      新的 `platform/win/ffi::currentExecutablePath()`）。代理进程在 `main` 的离线命令那一类里：
      **不提权、不读配置、不装钩子、不碰单实例锁**，结果写到 `%TEMP%` 的一个小文件里（失败时里面是
      错误文本）。代价是每次多起一个隐藏进程（实测总体 ~120 ms）。没提权时直接在本进程里调 shell。
    * **右键菜单不在这一层**：`shell_menu` 的命令由 shell 在 flowkeyd 自己的进程里执行，所以从
      那里选「打开」开出来的程序会提权。改成普通权限要把菜单宿主（含 `#32768` 消息转发）整个搬到
      代理进程，风险与代价都不划算。
    * 验证：`tst_interactive::launchesProgramsWithTheRequestedPrivilege`（读**目标进程令牌**的
      完整性 SID：默认必须是 `S-1-16-8192`；本进程已提权时再验 `elevated = true` 是
      `S-1-16-12288`）、`opensTargetsAsTheLoggedOnUser`（`open` 与 `app` 两种模式各跑一遍）、
      `reportsALauncherFailureFromTheShellHelper`；断言用**中文 SID** 而不是 `whoami` 的本地化
      文本，而且**不写 `whoami` 那种一次性文件**以外的任何东西。

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
* **本机是两块 1920x1080@200% 的屏（物理 3840x2160）**，`availableGeometry()` 可能比 `geometry()`
  还宽；主显示器逻辑尺寸约 533x1095（225% 缩放下）。弹窗是按**光标所在那块屏**居中的
  （`centreOnCursorScreen()`）。

### 依赖政策：什么可以静态链接，什么必须运行时解析

* **Qt 的模块照常 `find_package` + `target_link_libraries`**：`Core`/`Gui`/`Qml`/`Quick`/
  `QuickControls2`/`Widgets`/`Test`/`Network`，外加一个 **Qt 私有**模块 `Qt6::CorePrivate`
  （**只**为了 `<QtCore/private/qzipreader_p.h>`）。官网装出来的 Qt 不会因为 `COMPONENTS Core`
  就自动加载私有模块，需要在 `find_package(Qt6 …)` **之前**设 `QT_FIND_PRIVATE_MODULES ON`。
* **允许静态链接的集合**（MinGW 工具链自带这些导入库）：`user32`、`kernel32`、`shell32`、`ole32`、
  `ntdll`、`advapi32`、`powrprof`、`uxtheme`、`comctl32`、`shlwapi`、**`gdi32`**。
  * `gdi32` 是 2026-10 为了程序启动器加进来的（见第 2 节第 27 条）：`GetImage()` 给的是 `HBITMAP`，
    读出像素只能用 `GetObjectW` / `GetDIBits` / `CreateCompatibleDC`。它与名单里其它 DLL 一样是每个
    GUI 进程都会加载的核心系统 DLL（Qt6Gui 自己就导入它）。
* **必须 `LoadLibraryW` + `GetProcAddress` 运行时解析**：未公开入口（`win32u!NtUserSendInput`、
  `NtUserGetAsyncKeyState`……没有 `libwin32u.a`）以及任何不在上面那个集合里的 DLL（`dwmapi`、
  `imm32`、`d3dcompiler`…）。理由：这些接口没有 ABI 承诺，少一条静态依赖就少一个“在某些机器上 exe
  根本起不来”的机会。
* **COM 接口手写 vtable**（Core Audio `IAudioEndpointVolume`、shell 的
  `IVirtualDesktopManagerInternal`/`IVirtualDesktopPinnedApps`）。**这是本仓库风险最高的两块代码**：
  vtable 布局写错不是返回错误码，而是崩溃。接口定义以真机验证为准，虚拟桌面那张表按 `build.revision`
  索引。
* 不引入会藏起未公开入口的封装层 —— 未公开 API 一律自己 `GetProcAddress`。

---

## 4. 代码地图

| 路径 | 职责 |
| ---- | ---- |
| `CMakeLists.txt` / `CMakePresets.json` | 顶层工程、Qt 查找、`qt_add_executable`、`qt_add_qml_module`、部署；两个 preset（Ninja + mingw1310_64 + Qt 6.11.2） |
| `cmake/VendorLua.cmake` | 把 `vendor/lua` 编成静态库 `lua_static`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`，定义 `LUA_USE_WINDOWS`，`AUTOMOC/AUTOUIC OFF`） |
| `cmake/PruneRuntime.cmake` | **发布运行时精简清单**（`cmake -P` 脚本）：windeployqt 之后删掉用不到的 Quick Controls 样式、qmltooling、`opengl32sw.dll`、`D3Dcompiler_47.dll`、`plugins.qmltypes`、FluentWinUI3 磁盘上与插件重复的 .qml/.png，release 再 `strip`。**它是“发布包里有什么”唯一的家**（见第 10 节「发布包精简」） |
| `cmake/version_revision.h.in` / `cmake/RunQTest.cmake` | git 修订的编译期常量；测试输出重定向（QtTest 输出拿不到，见第 10 节） |
| `flowkeyd.lua.example` | 有文档、覆盖全部特性的参考配置（中文注释、`--check` 零警告） |
| `README.md` | **用户文档（只写使用方法）**：安装、快速上手、命令行、配置/动作/schema 全部字段、托盘与弹窗、开机自启、在线更新、已知限制。**配置 schema 的权威定义** |
| `install.ps1` | 对外一键安装器（**纯 ASCII、无 BOM**，见工作约定第 9 条） |
| `logo.svg` / `assets/` / `tools/icon_gen` | 图标美术源（唯一真源）；`assets/flowkeyd.ico`（exe 资源，windres 嵌进 PE）+ `assets/icons/flowkeyd-<n>.png`（qrc 里的多尺寸 `QIcon`）；`tools/icon_gen`（`EXCLUDE_FROM_ALL` 的 `icons` 目标，把 svg 光栅化） |
| `tools/pinyin_gen.ps1` | 生成 `src/core/pinyin_data.cpp`（程序启动器的拼音表）：离线跑一次、结果提交进仓库（第 2 节第 29 条） |
| `src/main.cpp` | `AttachConsole` + `QT_QPA_PLATFORM=windows:fontengine=freetype`（**第一行**）+ CLI 分发（含代理进程的 `--shell-*` 那条离线分支，见第 2 节第 35 条）+ 日志初始化 + 提权前的单实例预检 + 自启确认框 + 组装 Runtime + 接 `desktopChanged` 到 Tray + 排队 `PopupHost::preload()` |
| `src/cli.h/.cpp` | 参数解析 + 中文帮助文本（手写）；`--quit` 走单独早期分支；`--shell-open`/`--shell-app`/`--shell-args`/`--shell-cwd`/`--shell-show`/`--shell-result` 是**内部协议**（不进帮助文本，见第 2 节第 35 条） |
| `src/core/` | **纯逻辑层：不碰 Win32、不碰 Qt GUI**（只用 QtCore），能被 Qt Test 直接测 |
| `src/core/keys.*` | 键名 ↔ `VK` 表、`Modifiers`、`Chord`、AutoHotkey 发送脚本解析、小键盘 Enter 的内部伪码 `0x100`、`key_from_hook()`/`native_key()`、大写字母键名检查 |
| `src/core/config.*` | 配置结构体、严格校验（未知字段要报错）、编译成 `Compiled`/`Binding`/`CompiledRemap`、配置文件搜寻与旧 TOML 的迁移提示、`AppDef` + `expandApps`/`applyWindowDefaults` |
| `src/core/engine.*` | 快捷键状态机：匹配、优先级、吞键、自动重复抑制、长按重复、挂起、重映射 hold/tap、Win/Alt 菜单遮断、单个修饰键的「轻碰」语义 |
| `src/core/action.*` | 声明式动作的表示 + 摘要文本（`--list` 与 `help()` 都用它）+ `isDestructive()` |
| `src/core/template.*` | `{clipboard}`、`{selection}`、`{date}` 等占位符展开 |
| `src/core/window_match.*` | 窗口匹配与 `window` 动作决策的纯函数 + 「什么算一个程序窗口」的纯判据（`TopLevelWindowFacts`、`isMainWindow()`、`isSwitchableWindow()`） |
| `src/core/remote_desktop.*` | 「这个前台进程算不算远程桌面客户端」的纯逻辑：内置名单 + 子串匹配（第 2 节第 26 条） |
| `src/core/app_list.*` | **程序启动器**的纯逻辑：条目类型（`AppEntry`）、「算不算程序」与「算不算卸载程序」两条判据、图标键（启动名的 64 位 FNV-1a）与**取图标的名字**（`appIconLaunchName()`）、扫描结果的去重/排序、**跨来源合并**（`appendLauncherEntries()`）、名字匹配（字面 + 拼音 + 首字母）、「全部程序」列表的排序键与分组表头（`appSortInfo`/`appSortText`/`appGroupLetter`）、**内置的 26 个常用 `ms-settings:` 设置页表**（`windowsSettingsPages()` / `settingsNamesAreChinese()` / `builtinSettingsEntries()`） |
| `src/core/launcher_state.*` | 程序启动器的**持久状态**：`LauncherState`（固定 + 最近使用的**图标键**列表）、`launcherStatePath()`（配置文件旁边的 `launcher.json`）、容错的 JSON 解析 / 序列化、`touchRecent()` / `togglePinned()` |
| `src/core/pinyin.*` / `src/core/pinyin_data.*` | 汉字 → 读音（去声调、多音字、`ü` → `v`/`u`）的查询层 + 生成的读音表（`tools/pinyin_gen.ps1` 从 mozillazg/pinyin-data 生成） |
| `src/core/monitor_ref.*` | 「窗口该摆到哪块显示器上」的取值：`MonitorRef`（序号 / `primary` / 设备名 / `cursor` / `last` / `none`）与 `monitorRefIsConcrete()` / `monitorRefIsDynamic()`；单独一个头是因为 `core/action.h` 与 `core/config.h` 都要用它 |
| `src/core/config_map.*` | **配置结构体 <-> QVariant 字典**（图形化配置界面用）：键名与 Lua schema 逐字对应、`std::optional` 没写就不在字典里、枚举写成配置里的字符串、动作统一成「一串动作」（`short` 简写在这里展开、嵌套列表拍平）。只依赖 QtCore，`tst_config_writer` / `tst_config_editor` 直接测 |
| `src/core/config_writer.*` | **配置结构体 -> Lua 源码**（界面保存时整份重写 `config.lua`）：`kEditorGeneratedMarker` 与 `isEditorGenerated()`、Lua 值的排版与转义（控制字符用十进制转义）、`configToLua()` 里把界面内部的形状换回配置的形状（`actions` -> `action`、空 `window`/`launch` 不写、`remote_desktop` 布尔/表两种形状、`repeatable` 三种形状），以及 `writeConfigLuaFile()`（先留 `.bak`、再 `QSaveFile`、目录不存在就建） |
| `src/core/window_monitor_state.*` | `monitor = "last"` 的**持久记忆**：程序（可执行文件名小写）→ 显示器设备名，`windowMonitorStatePath()`（旁边的 `windows.json`）、容错 JSON、`rememberWindowMonitor()` / `rememberedWindowMonitor()` |
| `src/core/placement.*` | `window_rule` 的纯逻辑：显示器排序与选择、重连检测、摆放几何、规则匹配、`stepIndex()`、`windowActionMonitor()`（动作 → 规则 → 全局缺省的优先级） |
| `src/core/log_tail.*` | 日志文件的增量尾随（纯逻辑）：按字节读、末尾不完整的 UTF-8 序列不消费、半行留到下一轮、一次最多 1000 行 |
| `src/core/update_check.*` | 在线更新纯逻辑：仓库地址、`releases/latest` JSON 解析、资产挑选、版本比较、`buildVersionDate()` |
| `src/core/version.*` / `src/core/desktop_badge.*` | 构建版本号；托盘数字徽标的文字与字号 |
| `src/lua/lua_config.*` + `src/lua/lua_prelude.lua` | **Lua 与 C++ 的唯一边界**（转成 `core::Config`，逐条目、带上下文的错误；BOM 剔除、`.toml` 拒绝）；注入配置脚本的 DSL（纯 Lua，改它不需要改 C++，编进 qrc） |
| `src/platform/win/` | Win32 后端，每个文件只做一件事：`ffi.*`（全部 Win32 声明、结构体与常量，`INPUT` 的 40 字节布局有 `static_assert`，外加 `currentExecutablePath()`）、`nt.*`（运行时解析的 `win32u.dll` 导出）、`process.*`（`run`：直接 `CreateProcessW` / 用登录 shell 的令牌 / `runas` 三条路；`open` 与程序启动器的 `--shell-*` 代理进程，见第 2 节第 35 条）、`dwm.*`（`DwmSetWindowAttribute` 关过渡动画 / `DwmGetWindowAttribute` 读 `DWMWA_CLOAKED`；拿不到时保留动画、动作不失败）、`monitor.*`（枚举、`indexForWindow`、`indexForCursor`、`applyPlacement`、`moveToMonitor`、`moveToAdjacentMonitor`；**几何判断不在这一层**）、`input.*`（`SendInput`/`NtUserSendInput`、按键状态、`ModifierGuard`、`FLOWKEYD_ACCEPT_INJECTED` 后门、`copySelection`）、`ime.*`（运行时解析的 `imm32.dll`）、`hook.*`（钩子回调 + 钩子线程自己的消息循环 + `SetTimer` + 控制消息 + 重载；`window_rule` 的两个监听 `SetWinEventHook` 与 350 ms 显示器轮询；`EVENT_SYSTEM_FOREGROUND`；**定时器 id 必须用 `SetTimer` 的返回值**）、`apps.*`（两份扫描 + `launchApp()`（在本进程里调 shell）+ `launchAppAsUser()`（必要时经代理进程）+ `shellIcon()` + `StaThread`）、`shell_menu.*`、`audio.*`（手写 vtable，**高风险**，MTA）、`clipboard.*`、`window.*`、`desktop.*`（`IVirtualDesktopManagerInternal` 按版本查表 + 不随版本变的 `IVirtualDesktopPinnedApps`）、`power.*`、`tray.*`、`logging.*` / `single_instance.*`、`elevate.*` / `autostart.*` / `update.*` |
| `src/app/` | 组装层：把 core/lua/platform 串起来，并拥有 Qt 对象 |
| `dispatcher.h/.cpp` | **动作工作线程**（`QThread`）：执行动作列表、`window` 的“先启动再激活”与默认开的 `toggle`、**把窗口摆到该去的那块显示器上**（第 2 节第 33 条，含 `MonitorMemory`）、`menu`/`help`/`windows`/`apps` 的窗口请求、`window_rule`（三遍）、`startDesktopWatch()`/`pollDesktop()`；启动器的持久状态文件路径也是在这里算好塞进 `AppRequest` 的 |
| `runtime.h/.cpp` | 引擎 + 钩子 + 分发 + 托盘 + 弹窗的总装；`ControlCmd`（suspend/reload/quit）通道；`--quit` 的事件句柄（`QWinEventNotifier` 在 GUI 线程上监听）；`reportDesktop()`/`desktopChanged` |
| `log_model.h/.cpp` | 日志窗口的模型：尾随日志文件、最多 1000 行、按级别配色、子串过滤 |
| `{menu,help,window_list,app_list}_model.*` | 四个卡片的**纯逻辑**（`QAbstractListModel`，只用 QtCore）。行几何与鼠标命中**不归它们管**；`help` 只管筛选/选中项/`Enter`/`Esc`/`setSelected`；`menu` 还持有悬停；`window_list` 管进程名前缀筛选、自动激活与数字选择模式；`app_list` 管**行**式的三个视图、名字/拼音/首字母子串筛选、三种快速启动标识（筛选之后的 `0`–`9`、`Alt` + 数字、`Alt` + 功能键）、`Space` 固定与「最近使用」记账、卡片高度 |
| `popup_layout.*` / `popup_host.*` | 弹窗共用的几何类型与 `centrePopup()`、`popupPixelSizeIsStale()`；把模型挂到 QML 窗口上、抢前台、在 GUI 线程上创建/复用窗口、`helpRun()`（**先藏窗口再执行**）、`preload()`、**屏幕缩放 / 几何变时丢掉窗口缓存重建**、`switchUseEnglishInput()`/`restoreSwitchInputMode()` |
| `config_editor.*` | **图形化配置界面的模型层**（在 `flowkeyd_models` 里，只依赖 QtCore）：以 `core::configToMap()` 的字典为唯一状态，提供设置项/四个条目列表的增删改与上下移动、列表行的标题副标题（每次改动重算）、和弦冲突检查（`chordError()`，与 `--check` 同一套 `parseChord()`）、`pickFreeChord()`/`pickFreeRemapSource()`。**`save()` 先序列化、再用真 Lua 层读回来校验，不通过就根本不落盘**。`tst_config_editor` 盯着 |
| `config_window.*` | 图形化配置界面的宿主（在 exe 里，需要 QtQuick）：`loadFromModule("Flowkeyd", "ConfigWindow")` 创建/复用一个普通窗口，把模型挂上去；**重开窗口时不重新读盘**（除非还没载入过、或换了一个配置文件），免得丢掉没保存的改动；保存成功时发 `reloadRequested()` |
| `app_icons.*` | **程序启动器的图标**：`QQuickAsyncImageProvider` + 一条常驻 STA 线程（队列 + 按「启动名@边长」缓存），把 `shellIcon()` 的 BGRA 变成 `QImage`（`Format_ARGB32`，**直通 alpha**）；表是“图标键 → 启动名”，只增不改 |
| `update_model.*` / `update_archive.*` / `updater.*` | 更新卡片的状态机（八个阶段、版本号/发布说明/进度/按钮可见性，**不联网不解压不换文件**）；从 zip 里取出新 exe（`QZipReader` + PE 魔数检查）；联网编排（异步 `QNetworkAccessManager` + sha256 + 解压到 `<exe>.new` + mtime 对齐发布日） |
| `app_icon.h/.cpp` | 把 qrc 里的 9 张 PNG 帧拼成多尺寸 `QIcon`（`applicationIcon()`）；`desktopIcon(number)` 现画桌面号徽标 |
| `src/qml/` | 六个窗口：`ConfigWindow.qml`（左导航 + 分组卡片，见第 2 节第 34 条）、`LogWindow.qml`、`MenuPopup.qml`、`HelpPopup.qml`、`SwitchPopup.qml`、`AppPopup.qml`、`UpdatePopup.qml`；配置界面另有一组共用控件（`SettingCard`/`SettingRow`/`ChoiceRow`/`BoolRow`/`TriRow`/`SpinRow`/`TextRow`/`EntryRow`/`ChordsEditor`/`ActionEditor`/`ActionListEditor`/`LaunchFieldsEditor`/`WindowRuleFields`/`HotkeyFields`/`OverlayCard`/`ConfigText` 单例 与四个编辑弹层）。都写 `pragma ComponentBehavior: Bound`；**五个弹窗的 `flags` 都带 `Qt.Tool`**（配置界面是普通窗口，不加）；配色一律用 `palette`（没有单独的 `Style.qml`）；中文一律 `font.family: "Microsoft YaHei"`；列表/网格全部是标准 `ListView`/`GridView` + `ItemDelegate`（+ `ScrollBar`） |
| `tests/` | Qt Test：`tst_keys`、`tst_engine`、`tst_config`、`tst_config_writer`、`tst_config_editor`、`tst_lua`、`tst_template`、`tst_send_script`、`tst_window_match`、`tst_remote_desktop`、`tst_log_tail`、`tst_audio`、`tst_autostart`、`tst_menu_model`、`tst_help_model`、`tst_window_list_model`、`tst_app_list`、`tst_app_list_model`、`tst_launcher_state`、`tst_window_monitor_state`、`tst_power_table`、`tst_desktop_table`、`tst_placement`、`tst_layout`、`tst_version`、`tst_desktop_badge`、`tst_update`、`tst_update_model`、`tst_update_install`、`tst_command_line`、`tst_instance`、`tst_input`，以及需 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 的 `tst_interactive`（真机：剪贴板/音量/窗口/虚拟桌面/钉住/置顶/输入法/覆盖层/**启动权限**/更新下载；联网那条还要 `FLOWKEYD_ALLOW_NETWORK_TESTS=1`） |
| `scripts/acceptance.ps1` | 桌面行为验收（注入按键 + 焦点捕捉窗口的外部观察，166 项检查，实跑 163）。需交互式桌面，**不属于 `ctest`** |
| `scripts/release.ps1` | 构建 release + 打包（完整包 + 精简升级包，各附 `.sha256`）+ 用 `gh` 上传 GitHub Release。tag 取刚构建的 exe 的 `--version`。**发布说明由脚本自己写**（上一个 Release 的 tag → HEAD 的提交主题，按前缀分类；`-Notes`/`-NotesFile` 可整份替换），不用 `gh --generate-notes`。工作区脏或 HEAD 没推到 origin 会直接拒绝（要 `-AllowDirty`/`-Push`）。**唯一的新前置依赖是 `gh`**。开关：`-SkipBuild`/`-SkipResident`/`-SkipUpload`/`-Clobber` |

> `scripts/install.ps1` / `uninstall.ps1` **已删除**：自启的注册、刷新与删除现在全在
> `src/platform/win/autostart.*` 里，由守护进程自己在启动时做。

### CMake 目标划分

| 目标 | 内容 | 谁链接 |
| ---- | ---- | ------ |
| `flowkeyd_core` | `src/core/*`（纯逻辑，只用 QtCore） | exe + 全部单测 |
| `flowkeyd_lua` | `src/lua/*` + 编成 qrc 的 `lua_prelude.lua` | exe + `tst_lua` |
| `flowkeyd_models` | `src/app/{menu,help,window_list,app_list}` 四个模型 `.*` + `config_editor.*` + `popup_layout.*`（`app_list_model` 要算拼音搜索串、`config_editor` 要用 `core::config_map`，所以这个库链 `flowkeyd_core`） | exe + 四个 model 测试 + `tst_config_editor` |
| `flowkeyd_update` | `src/app/{update_model,update_archive,updater}.*`（`Qt6::Core` + `Network` + **`CorePrivate`**） | exe + 三个 update 测试 + `tst_interactive` |
| `flowkeyd_platform` | `src/platform/win/*`（不碰 Qt GUI 的 Win32 后端） | exe + 平台层单测 |
| `flowkeyd` | `src/main.cpp`、`src/cli.*`、`src/app/*`、`tray.*`、QML、图标 qrc + 图标 .rc | —— |
| `flowkeyd_icon_gen` | `tools/icon_gen/main.cpp`（`EXCLUDE_FROM_ALL`，只由 `icons` 目标手工构建） | —— |

* `flowkeyd_add_test(name [LIBS …])` 把 Qt/MinGW 的 DLL 目录写进 test 的 `PATH`。
  **测试目标只在这个函数被调用时创建，整段（连 `enable_testing()`）都包在 `FLOWKEYD_BUILD_TESTS` 里**：
  debug preset 给 `ON`、release preset 给 `OFF`。
* `src/app/popup_host.*` 用 QML/QtQuick，所以**不进** `flowkeyd_models`，留在 exe 里。
  在线更新**单独一个库 `flowkeyd_update`**（它是唯一需要 `Qt6::Network` 与 Qt 私有头的东西）。
  图形化配置界面拆成两半：**模型 `app/config_editor.*` 进 `flowkeyd_models`**（只依赖 QtCore，能单测），
  **宿主 `app/config_window.*` 留在 exe**（要 `QQmlComponent`/`QQuickWindow`）。
* **配置界面的 QML 文件要在 `CMakeLists.txt` 里登两处**：加进 `FLOWKEYD_QML_FILES`，并且给自己一个
  `QT_RESOURCE_ALIAS <文件名>`（否则它不会以类型名被认识；同模块的兄弟组件引用全靠这个）。
  `ConfigText.qml` 还多一个 `QT_QML_SINGLETON_TYPE TRUE`（它就是那个中文对照表单例，少了它
  `ConfigText.label(…)` 会直接报错）。
* **exe 的产物目录是自包含且精简过的**：`flowkeyd` 上挂一条 `POST_BUILD` 的 `windeployqt`
  （`--qmldir src/qml` 必须给，否则 QML 模块不会被拷过去），紧接着跑 `cmake/PruneRuntime.cmake`。
  只对 `flowkeyd` 做，**不给测试可执行文件做**。release profile 还额外把干净的发布目录写到
  `build/dist-release/`：**212 个文件 / 65.4 MB**（精简前 1378 个 / 149.8 MB）。
  `flowkeyd` 上还挂了 `LINK_DEPENDS`（`cmake/PruneRuntime.cmake`）：改了精简清单就会重新链接。
* **分层铁律**：`src/core/`、`src/app/{menu,help,window_list,app_list}_model.*`、
  `src/app/popup_layout.*`、`src/core/update_check.*` **不许出现 `<windows.h>`、不许出现 QML/
  QtWidgets、不许出现窗口句柄**。这正是 `--check`/`--list` 能在没有桌面的情况下跑、以及单元测试能
  覆盖核心逻辑的原因。`platform/win/window.cpp` 里“候选窗口如何匹配”这种判断要拆成纯函数放进 `core`。

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

> release 用 `RelWithDebInfo`（不是 `Release`）：它走另一条优化路径、能暴露 debug 看不到的警告，
> 同时保留符号便于看栈（发布时会 `strip`）。

### 运行（开发期一定要带这两个开关）

```powershell
& build/windows-debug/flowkeyd.exe --no-elevate --allow-multi --console --config .\tmp\smoke.lua
```

* `--no-elevate`：守护进程模式默认提权，开发期一律关掉。
* `--allow-multi`：跳过单实例检查。注意每个实例都装一个 `WH_KEYBOARD_LL` 钩子，**后装的先收到事件**，
  所以调试时只开一个真正需要吞键的实例。
* `--console`：保留控制台输出。

**离线命令（绝不允许提权）**：`--check` / `--list` / `--list-keys`。提权判断必须在这些命令 `return`
之后。`--quit` 也不提权（它只去通知一个已经在跑的实例），但它会碰另一个进程；`--remove-autostart`
要管理员权限才能删任务。开发实例与常驻实例的单实例锁按**配置文件路径**分开，互不影响。

### 桌面行为怎么验证

```powershell
# 166 项检查，约三分钟，会持续注入按键/抢焦点；按工作约定第 6 条先提醒用户
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1
powershell.exe ... -Phase config      # 只看配置，不注入按键
```

它自己生成一份**一次性配置**，起一个非提权的守护进程，从另一个上下文用 `SendInput` 注入按键，再用一个
获得焦点的 WinForms 窗口从外面观察“按键到底有没有到达前台”（未绑定的键做正对照）。要让注入的按键触发
绑定，守护进程得用 `FLOWKEYD_ACCEPT_INJECTED=1` 启动（只给测试用的后门，启用时日志里有一条警告）。

脚本**不做**的：动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程、“托盘图标真的消失了”的直接观察，
以及**一切电源动作**。这几项仍然只能靠人的手。

覆盖范围与现役结论：

* 启动器那一段有一条**弹窗缩放过期**的回归守卫：先把预热好的启动器窗口 `SetWindowPos` 成
  800×622 **物理**像素，再按快捷键弹出 —— 断言卡片真的被重建了，而且宽度等于
  `800 × GetDpiForWindow()/96`（现取窗口所在屏的 DPI 当**独立**参照）。细节见第 10 节。
* 「轻碰 Win」：一次轻碰弹卡片、**再轻碰一次关掉**、卡片不在任务栏里、Win 松开后前台没有被外壳抢走
  （遮断标记生效的判据）。
* 远程桌面放行 9 条：名单里写的是捕捉窗口自己那个进程，因此不碰真 RDP 客户端。真机确认过：聚焦
  Windows App 时出现 `remote desktop detected (msrdc.exe)`，切回普通窗口后 `left the remote desktop (…)`。
* 程序启动器：弹出 / 不在任务栏 / 拿到键盘焦点 / 标题里读得出条数 / **在卡片里打字真的筛掉条目** /
  再按一次关掉 / 重开时筛选已复位 / `Esc` 关掉 / 关掉后前台回到捕捉窗口；**右键菜单 5 条**（能拿到卡片
  矩形、右键一格后本进程多出一个 `#32768` 系统菜单窗口、位置在光标附近、尺寸是「每条约 22 **逻辑**
  像素」、`Esc` 关掉菜单、取消之后卡片还在）；**已固定/最近使用/全部程序 6 条**（`Space` 固定后标题里的
  条数变化、**卡片高度不变**、关掉再打开固定还在、`End`+`Enter` 进「全部程序」列表、列表里 `Esc` 是
  返回概览、再按 `Space` 取消固定）；「全部程序」列表的**滚轮**检查（滚 3 格 → 抓列表左侧一条竖带的
  像素指纹前后比对）；**数字快速启动键 3 条**（找一个只匹配 1..8 个程序的单字母筛选串，再按 `9` ——
  编号从 `0` 起，`9` 必定没有对应条目；卡片还在、条数不变就说明它被吃掉了）。
  **卡片里绝不能按 `Enter`** —— 那会真的启动列表里第一个程序（`End` + `Enter` 落在「全部程序」按钮上
  是安全的）。
* `-Phase config` 覆盖 `--check`（17 个快捷键）/ `--list`。脚本开头会先把 `$WorkDir\launcher.json`
  删掉，所以每次跑都从「什么都没固定」开始。
* **脚本从头到尾不碰虚拟桌面**（要自己造第二张桌面、还要把用户的窗口搬来搬去，代价与风险都不划算）：
  跨桌面唤起那条路（第 2 节第 32 条）的自动化覆盖放在 `tst_interactive` ——
  `tst_placement`（纯逻辑）盯“命中这个窗口的第一条 `window_rule` 有没有 `desktop`”；
  `tst_interactive::bringsAWindowOnAnotherDesktopToTheCurrentOne()` 盯“窗口被搬到当前桌面、而**视图
  没动**”，外加“已经在当前桌面时是空操作”（跑之前先建一次窗口、等 shell 登记它 —— 同文件的
  `moveWindowToAnotherDesktopAndBack()` 没等，**它是 flaky 的**，需要重跑一次再下结论）；
  `tst_interactive::activatesAWindowThatIsOnAnotherDesktop()` 盯另一个方向。
* 最近一次实跑（2026-10-13，release 产物）：`checks: 163, failures: 0`。比 166 少的 3 条就是上面那组
  数字键检查 —— 真机目录 198 条时，26 个单字母里**没有一个**命中 1..8 个程序，脚本打一条诊断
  （写进 `$WorkDir\diag.txt`，UTF-8 无 BOM）**跳过**它们：是跳过，不是失败。
* **锁屏时（`LogonUI` 在跑）脚本必然挂**：`GetForegroundWindow()` 返回 0，`SendInput` 报 5
  （ACCESS_DENIED）。还有另一种“判据全对、注入就是不落地”的状态（RDP 会话没真正接收输入）：先看
  `GetForegroundWindow()` 是不是 0，是的话**别在产品代码里找原因**，先去解锁 / 重连。
* **RDP 会话里注入的「相对鼠标移动」不会动光标**（实测：同会话 `SetCursorPos(300,300)` 成功、
  `GetCursorPos` 也读得到，可是一条 `MOUSEEVENTF_MOVE` 的 `SendInput` 返回 1 而光标纹丝不动）→
  脚本里要用绝对定位（`SetCursorPos`，即 `[FlowInject]::Cursor()`）走路。
* 一条已修的 flake：右键弹出的原生 shell 菜单只能拿「类名是 `#32768` 的可见窗口存在」作判据，而解析
  shell 条目 + `QueryContextMenu` 要 100~200 ms，这段时间里别的 `#32768` 窗口会让“存在”提前成立
  （`ClassRect` 读回全 0、两条尺寸检查一起挂）→ 现在判据是三条一起看：**可见窗口存在 + 矩形非空 +
  守护进程日志里已经有 `shell menu: N item(s)`**。
* 往脚本里插诊断时：把脚本复制到 `tmp/`，用
  `[System.IO.File]::ReadAllText/WriteAllText(..., UTF8Encoding($true))` 做替换（**必须保 BOM**），
  再加一条只含 ASCII 的探针把失败的检查名写进 diag。**把临时脚本的中文检查名打到控制台会被代码页
  弄成乱码，很容易误判成另一条检查挂了。**

### 手工冒烟清单（脚本覆盖不到的部分）

1. `--check --config tmp/smoke.lua` 通过，`--list` 形状与 README 一致。
2. 被吞掉的 `Win+…` 和弦：按完之后 Windows **不能**弹出开始菜单/搜索；按住 Windows 键超过自动重复
   延迟再松开，**仍然不能**弹。
3. 自动重复：按住和弦键 2 秒，动作只执行一次。
4. `suspend`：挂起之后所有绑定都不触发，**suspend 自己那条仍然可用**。
5. `quit`：托盘图标消失、钩子卸掉、进程干净退出。
6. `animate = true/false` 的肉眼区别；`volume` 的实际听感；托盘菜单点击。
7. `window_rule`：`DisplaySwitch.exe /internal` → `/extend` 制造一次“显示器重新接入”，确认日志里出现
   `monitor connected: ...; re-applying window rules` 且手工挪走的窗口被摆回。
8. **远程桌面放行**（脚本用的是假名单）：用 `mstsc.exe` / Windows App 连上一台机器，聚焦那个窗口，按
   一个被绑定的和弦（如 `Ctrl+Alt+t`）—— 对面那台机器应当收到它、本机不该有任何动作；日志里出现
   `remote desktop detected (mstsc.exe)`。把焦点切回本机窗口，日志出现 `left the remote desktop (…)`，
   和弦又回到本机行为；示例配置里 `remote_desktop = true` 的 `Ctrl+Alt+m`（静音）在远程桌面里仍然生效。
9. **程序启动器的右键菜单**（脚本只能断言“系统菜单窗口出现了”与「每条约 22 逻辑像素」）：弹出启动器，
   右键一格 —— 菜单应该长在光标处、字号与系统其它菜单一致（200% 缩放下每条约 44 物理 / 22 逻辑像素高；
   **不能**是两倍大的那种），不同的格子内容会变；挑一条无害的（「属性」或「打开文件位置」）真按一次，
   确认卡片在命令跑之前就收了、shell 开出来的窗口在前台；再右键一次按 `Esc`，卡片应当还留着。
10. 按顺序做完以上之后，**检查没有任何按键卡在按下状态**。
11. **程序启动器的 `Alt` + 数字 / 功能键**（脚本只能按没分配到的键）：固定两三条、再启动两三条之后
    打开卡片 —— 角上应当是 `Alt+F1` / `Alt+F2` 与 `Alt+1` / `Alt+2` 这样的徽标（宽度跟着文字走），
    固定项右边那个「已固定」小圆点让到图标左上角；按 `Alt+F1` 启动第 1 个固定项、`Alt+1` 启动第 1 个
    最近项；**筛选一个筛不到那条固定项的串之后再按 `Alt+F1`，照样应该启动它**；`Ctrl+数字`（如全选）
    与裸字母 / 数字照旧进筛选框；卡片关着时这些组合在本机**不再有任何效果**。注意 `Alt+F4` /
    `Alt+F5`…`Alt+F10` 在拥有系统菜单的窗口上是 Windows 自己的命令（关窗 / 还原 / 移动 / 大小 /
    最小化 / 最大化），可能先被 win32k 处理。
12. **启动器里的控制面板项与设置页真的能启动**（脚本只验到「筛选能筛到它们」）：开一个控制面板项
    （筛 `dyxx` → 「电源选项」→ `Enter`，应当弹出电源选项）与一个设置页（筛 `sz` → 「设置：显示」→
    `Enter`，应当打开设置 app 的显示那一页）；再右键一个控制面板格，菜单里应当有「打开」这类正常条目。
13. **跨桌面唤起**（第 2 节第 32 条）的真机手感：切到第 2 张虚拟桌面，按下一个**没写 `window_rule`
    `desktop`** 的程序（例如记事本）的 `window("activate")` 快捷键 —— 窗口应该被**拉到第 2 张桌面**、
    视图**不切**，日志里有 `(brought to desktop 2/2)`；再给同一个程序写一条
    `window_rule{ desktop = 1 }` 重载配置，再按一次 —— 这次应该是**切到第 1 张桌面**、窗口留在那儿；
    最后确认窗口不是最大化/最小化状态、也没有“卡在按下状态”的修饰键。
14. **窗口摆到光标那块屏**（第 2 节第 33 条）的真机手感：把鼠标放到**左边**那块屏，按下某个**没写
    `monitor`** 的程序的 activate 快捷键（它的窗口本来在右边那块屏上）—— 窗口应该被**搬到鼠标那一块**
    （保持大小并居中，不跟着最大化），日志里多一段 `placed 1280x800 at … on monitor 1/2`；再保持鼠标
    不动按一次（先 `toggle` 收起、再按一次恢复）—— 这次日志里是 `already on monitor 1/2`，而**窗口的
    位置没有被重新居中**。然后把鼠标放到右边那块屏、再按一次 —— 窗口跟过去。再把配置改成
    `settings{ window_monitor = "last" }`（或给那一条写 `monitor = "last"`）重载：第一次按**不会动窗口**
    （日志 `no monitor remembered yet; noted monitor 1/2`，旁边多出一个 `windows.json`），把它拖到另一
    块屏再按一次 —— 从此它每次都回到那块屏；把那块屏的显示器断开（或把 `windows.json` 里的设备名改错）
    后按一次：只记一条 warning（`the remembered monitor … is not connected`）、窗口留在原处。
    靠 `settings{ window_monitor = "none" }` 把整项关掉后重载，再按一次应当**完全不动窗口**。
15. **启动权限**（第 2 节第 35 条）：常驻实例是提权的，所以拿它验反而最直接 ——
    1. 按一个 `action = run("notepad.exe")` 的快捷键，然后在**任务管理器**里看那个 `notepad.exe`：
       「详细信息」页的 `notepad.exe` 行右键只能看到「结束任务」而**没有** UAC 盾牌图标；以管理员启动
       的会话里跑 `whoami /groups` 会看到 `High Mandatory Level`，普通权限是 `Medium`。命令行最快：
       `& build\dist-release\flowkeyd.exe --list` 里那条摘要应当**没有** `(elevated)`。
    2. 同一个键写 `run("notepad.exe", nil, { elevated = true })`（或给 `launch` 加 `elevated = true`）
       重载 —— 从**非提权**的实例按下去应当弹一次 UAC（这个 agent 验证不了，必须人按）；
       从提权实例按下去**不弹**（已经是管理员了）。
    3. `open([[...\x.docx]])` 与从启动器点一个程序：开出来的程序同样应当是普通权限；
       但启动器**右键菜单**里的「打开」开出来的是提权的（已知限制）。

---

## 6. 架构与线程模型

```
        ┌─────────────────── Qt GUI 线程（主线程）───────────────────┐
        │ QApplication + QQmlApplicationEngine                        │
        │   * QSystemTrayIcon（右键菜单、气泡、悬停提示）              │
        │   * 日志窗口 + menu/help/switch/apps/update 五个 QML 卡片    │
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
   （守护进程看起来还活着，却什么都不做）。所以回调只做「解码事件 → 问引擎要决定 → 注入重映射按键 →
   把动作排给工作线程」，然后立刻 `CallNextHookEx`。定时器（长按重复、挂起超时、显示器轮询）也在这个
   线程上。
2. **动作工作线程**：`run`/`open`/`wait` 都会阻塞，绝不能在钩子线程上做。它也是 `menu`/`help` 窗口的
   “请求方”——但**窗口本身**由 Qt GUI 线程创建。
3. **Qt GUI 线程**：托盘与全部 QML 窗口。跨线程一律走队列信号。

**钩子为什么不装在 Qt 主线程上**：QML 渲染的一次慢帧、以及 `QSystemTrayIcon` 的菜单弹出（会跑自己的
模态循环）都可能让主线程几十毫秒不回来；300 ms 的预算经不起这种抖动。

**通信与同步**：

* 钩子线程 → 工作线程：**`QMetaObject::invokeMethod(dispatcher, …, Qt::QueuedConnection)`**
  （`app::Dispatcher` 是 `QThread` 上的 `QObject`）。钩子回调里那次调用只做入队，不阻塞。
* 工作线程 → GUI 线程 / GUI 线程 → 工作线程：`Qt::QueuedConnection` 信号。
* 托盘/CLI → 钩子线程：`PostThreadMessage(hookThreadId, WM_APP_*, ...)`。**`PostThreadMessage` 在
  目标线程还没消息队列时会失败**，钩子线程必须先 `PeekMessage` 一次建出队列，并把“队列已就绪”通过
  `std::promise`/原子量告诉启动方。
* 跨线程共享的配置：`Compiled` 用 `std::shared_ptr<const Compiled>` 各持一份，**reload 时整体替换
  指针**，绝不做“就地改”。

---

## 7. 不变量 —— 不要破坏这些

1. **钩子回调必须快速返回。** 不要在回调里 sleep、写慢速日志、做 IO 或执行动作。
2. **绝不对注入输入作出反应。** 用 `LLKHF_INJECTED` 判定；带自己标记（`dwExtraInfo` 里的 **`"FLOW"`**）
   的事件在任何状态被触碰之前就丢弃。**新增任何注入路径都必须打上标记**（包括 `ModifierGuard` 里
   “先松开 Win/Alt 再按回去”的注入）。
3. **重映射的按键内联注入，动作不是。** 因此重映射里的 `{Sleep}` 会被忽略（`--check` 给警告）。
4. **绝不能让按键留在按下状态。** 任何丢弃重映射的路径（挂起、重载、退出）都必须注入释放。
5. **不要跨 `SendInput` 持有锁。** 钩子状态放在钩子线程自己拥有的对象里，用“没有锁”的方式保证安全。
   **不要给它再套一层锁。**
6. **配置错误必须可操作。** 每条校验信息都要指出出错的快捷键（用 `name` 或序号）和出错的值，格式固定
   为：``hotkey #3 (`terminal`): unknown field `nope`, expected one of …``。**未知字段一律报错**
   （C++ 侧要自己实现严格白名单）。**动作表顶层是唯一的例外**（`window("activate", { togle = false })`
   里的拼写错误会被忽略）—— 要在 README 的已知限制里写明。
7. **`core/` 与 `lua/` 保持无 Win32、无 GUI。** 这是核心逻辑无需桌面就能测试的前提。
8. **固定结构体布局是契约。** `INPUT` 在 x64 上必须 40 字节
   （`static_assert(sizeof(INPUT) == 40, "INPUT layout")`）。
9. **被吞掉的 `Win+…`/`Alt+…` 和弦必须在修饰键 key-up 时注入标记按键（`VK 0xE8`，未分配键）。**
   跟着和弦键按下一起注入是**不够**的：Windows 键的自动重复会在两者之间重新武装外壳。实现照抄
   `Engine::mask_menu_key_up`：被吞掉的和弦设置一个标志，而**下一次** Win/Alt 的 key-up 先注入标记。
10. **`send`/`type` 动作的 `release_modifiers` 也要做同样的菜单遮断。** `ModifierGuard` 注入的真实
    key-up 引擎看不到，必须在注入层自己插一次 `VK_UNASSIGNED`。
11. **提权只发生在守护进程模式，且必须能降级。** `--check`/`--list`/`--list-keys` 绝不能弹 UAC；UAC 被
    拒绝时打 warning 继续以普通权限运行。提权重启时要转发完整命令行（含相对路径的 `--config`）并把工作
    目录一并传过去。**本机实测：用户点“否”时 `ShellExecuteW("runas")` 返回的是 5（拒绝访问）而不是
    1223**，所以降级路径不能只判 `ERROR_CANCELLED`。
12. **`--console` 之外，守护进程模式总是打开默认日志文件**（目录要 `create_dir_all`）。日志窗口尾随的
    就是那个文件；离线命令仍然不碰它。
13. **虚拟桌面与音频的 COM 单元模型不能混。** 音频要 **MTA**、虚拟桌面要 **STA**，所以 `desktop` 的
    每次调用都丢进一条**一次性的 STA 线程**（`CoInitializeEx(COINIT_APARTMENTTHREADED)` + 调用 +
    `CoUninitialize`）。**Qt 在 Windows 上会为 OLE/拖放把主线程初始化成 STA**，别去改它。
14. **Lua 状态绝不越出 `lua_config` 的求值函数。** 求值完就关掉 `lua_State`，把结果转成普通 C++ 结构。
    **`Compiled` 必须与 Lua 无关**。
15. **动作只能是声明式的表或简写字符串。** `action = function() end` 要由 `lua_prelude.lua` 里的
    `reject_functions` 明确拒绝。
16. **配置文本先剥掉 UTF-8 BOM 再交给 Lua。** 记事本与 `Set-Content -Encoding UTF8` 都会写 BOM，而 Lua
    的词法分析器不认它（第 1 行报 `unexpected symbol near '<\239>'`）。
17. **弹窗窗口必须能拿到键盘焦点。** 钩子守护进程通常**不持有前台锁**，只调 Qt 的
    `Window::requestActivate()` / `SetForegroundWindow` 会被拒绝。做法是 `requestActivate()` →
    `SetForegroundWindow` → `AttachThreadInput` 到持有锁的线程再重试 → `BringWindowToTop` +
    `SetWindowPos(HWND_TOP)`，**每条路径上 `AttachThreadInput` 都要配平**。
18. **模态/前台相关的一切都要在 Qt GUI 线程上做。** `QSystemTrayIcon`、`QQuickWindow`、`QMenu` 都不能
    跨线程。
19. **DPI 感知必须在创建任何窗口之前就绪。** Qt 6 在 Windows 上默认就是 Per-Monitor DPI Aware V2，
    不需要手动调 `SetProcessDpiAwarenessContext`；这条变成了“**不要在 `QApplication` 构造之前创建窗口/
    触碰屏幕 API**”。不要调 `SetProcessDPIAware()`（会把 Qt 的设置打乱、字发糊）。
20. **日志窗口是一个普通窗口，不是独立进程。** 关掉它不能退出应用：构造 `QApplication` 之后立刻
    `setQuitOnLastWindowClosed(false)`。

---

## 8. Lua 5.5.1 的注意点（已实测）

1. **`luaL_openlibs` 现在是宏**（展开成 `luaL_openselectedlibs(L, ~0, 0)`）；调用仍然可用。
2. **`lua_newstate` 多了一个 `unsigned seed` 参数**；用 `luaL_newstate()` 就没这个问题。
3. **`lua_resume` 的签名是 `(L, from, narg, nresults, ...)`**；`lua_resetthread` 是宏。本项目不做协程。
4. 新增 `lua_closethread`、`lua_closeslot`、`lua_toclose`（to-be-closed 变量）。
5. 我们**用到**的 API 全部确认存在；`vendor/lua` 的 32 个 `.c` 用 `-O2 -DLUA_USE_WINDOWS` 全部通过。
6. **Lua 侧两个行为差异**（写配置示例时别踩）：
   * `repeat` 仍然是关键字 → 字段名用 `repeatable`（配置里要认 `["repeat"]` 这个别名）。
   * 字符串里的 `\t`/`\n`/`\r` 是**合法但致命**的转义：`open("C:\tools")` 会**静默**变成 `C:` + TAB +
     `ools`。Windows 路径一律用长字符串 `[[C:\path\app.exe]]`。
7. **`lua_newuserdatauv`**（而不是旧的 `lua_newuserdata`）。

> **把 Lua 的版本号写进 `--version` 与 `--check` 的输出**，这样出问题时能一眼看出是哪一份。

---

## 9. 实施阶段计划

每个阶段都可运行、可提交，并且都要满足第 5 节的 DoD。

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
| 9 验收（无 e2e 的替代） | **已完成** | `scripts/acceptance.ps1` + `FLOWKEYD_ACCEPT_INJECTED` 测试后门 |
| 10 接管 | **已完成** | 真实配置迁到 `.config\flowkeyd\config.lua`；常驻由计划任务指向当前运行的 exe |

第 10 阶段之后新增的能力（都在本文件对应章节有记录）：`window_rule`（+ `all_desktops`/`topmost`/
`follow`）、`app{...}`、四个「挪窗口」op、字母键名小写、托盘数字徽标、窗口切换器（+ 数字选择模式 +
IME 切换）、弹窗不进任务栏 + 预热、发布包精简、精简升级包、`scripts/release.ps1`、在线更新、
`install.ps1` 一键安装、FreeType 字体引擎、程序启动器（+ 异步图标、数据源换成 `shell:AppsFolder`、
拼音 / 首字母筛选、固定 / 最近使用 / 「全部程序」分组列表、控制面板项与内置设置页）、弹窗缩放过期
自愈、跳桌面唤起（第 2 节第 32 条）、没写 `monitor` 的窗口按 `settings.window_monitor` 摆位
（第 2 节第 33 条）、图形化配置界面（第 34 条）、启动的程序默认以普通用户权限启动（第 35 条）。

---

## 10. 已踩过的坑

（每条都是「现象 → 原因 → 做法」，数字都是真机实测。）

### 环境与工具

* **`qt_add_executable(... WIN32 ...)` 意味着 GUI 子系统 ⇒ 没有控制台。** 从终端启动的
  `flowkeyd --check` 什么都打印不出来。修法：`main()` 第一件事 `AttachConsole(ATTACH_PARENT_PROCESS)`
  （失败就忽略），然后自己用 `WriteConsoleW` 写（重定向时走 UTF-8 `WriteFile`）。**行尾必须是
  `\r\n`**。不要用 `qDebug` 当日志（没有控制台时它走 `OutputDebugString`）。
* **`QSystemTrayIcon` 在 QtWidgets 里**，不是 QtGui → 要用它就必须 `QApplication`。
* **QML 窗口默认会让“关掉最后一个窗口”直接退出应用** → `setQuitOnLastWindowClosed(false)`。
* **FluentWinUI3 是 Qt Quick Controls 的样式，不是 Widgets 样式**（用 `QQuickStyle::setStyle`
  在加载任何 QML 之前设置）。
* **FluentWinUI3 不支持一批控件**（`Dial`、`Drawer`、`HorizontalHeaderView`、`SplitView`、
  `StackView`、`SwipeDelegate`、`SwipeView`、`TreeViewDelegate`、`Tumbler`、
  `VerticalHeaderView`）——它们会**静默回退到 Fusion**。布局不要用它们。
* **无边框圆角弹窗**：`flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint` +
  `color: "transparent"` + 内部一个 `Rectangle { radius: … }`。只设 `FramelessWindowHint` 而
  `color` 不是 transparent 就看不到圆角。
* **Qt 6 在 Windows 上默认就是 Per-Monitor DPI Aware V2**；不要在 `QApplication` 之前创建窗口。
* **Qt 会自动给主线程一个 STA**（为了 OLE/拖放）。别把音频初始化放到主线程。
* **字体引擎是平台插件的构造参数，只能在 `QApplication` 之前选。** Windows 上默认走 DirectWrite，
  本机高缩放下 Qt Quick 界面发虚 → `main()` 第一行
  `qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype")`。**不要在 `main()` 之后、更不要在 QML
  里设它。**
* **`PostThreadMessage` 在目标线程还没消息队列时会静默失败** → 钩子线程先 `PeekMessage`。
* **PowerShell 5.1 的 `Set-Content -Encoding UTF8` 写出 BOM**，而 Lua 的词法分析器在 BOM 上报错 →
  加载器必须先剥 BOM。反向：**没有 BOM 的 `.ps1` 会被 PS 5.1 按 GBK 解码**，里面的中文会变乱码、
  甚至被当成引号让 here-string 提前结束，或者**静默吞掉下一行**（表现为“脚本不报错、只是少做一
  步”）。写文件的正确姿势：`[System.IO.File]::WriteAllText($p, $text,
  [System.Text.UTF8Encoding]::new($true))`。**`tmp/` 下的一次性脚本一律纯 ASCII。**
* **`.cmd`/`.bat` 必须纯 ASCII。** **命令行上不要直接拼中文。**
* **`$args` 与 `$Pid` 是 PowerShell 自动变量**，不要当参数名/变量名。
* **`Start-Process -PassThru` 拿不到退出码**（`$p.ExitCode` 是空的）；`cmd.exe` 也不会等 GUI
  子系统的进程。验证 CLI 输出用 `Start-Process -FilePath .\flowkeyd.exe -ArgumentList '--help'
  -NoNewWindow -Wait -PassThru -RedirectStandardOutput out.txt`。
* **PowerShell 的数组 splatting 不能转发命名参数** → 用哈希表 splatting。
* **`$ErrorActionPreference = 'Stop'` 下原生命令往 stderr 写一个字就会被包成终止性异常。**
  脚本调用 cmake/git/gh 时要用 helper 临时把 EAP 切回 `Continue`、只看退出码。
* **PowerShell 接原生命令的 UTF-8 stdout 会按控制台代码页解码成乱码** → 按字节重定向到文件再看。
  **`Tee-Object` 写出来的日志是 UTF-16。**
* **`Get-Content` 不带 `-Encoding UTF8` 会把无 BOM 的 UTF-8 数错行数** →
  用 `[System.IO.File]::ReadAllLines`。
* **守护进程一直把日志文件开着写**，`ReadAllLines` 会报“文件正由另一进程使用” → 用
  `FileShare.ReadWrite` 的 `FileStream`（`Get-Content` 默认就是 ReadWrite）。
* **核对文档/示例改动用 `git diff --no-index`**（比肉眼可靠）。
* **`tmp/` 里有上次跑留下的旧日志**：先看时间戳或先删掉旧的，别把三天前的输出当成刚才那次。
* **`irm` 会把响应体开头的 BOM 留成真实的 `U+FEFF`** → `Invoke-Expression` 在 `param()` 上报
  `InvalidLeftHandSide`。也**不能**用 `curl | powershell -Command -`（GBK 解码中文）。所以
  `install.ps1` 只能二选一，本项目选「纯 ASCII + 无 BOM」。
* **脚本里的 `$PSScriptRoot` 在 `param()` 的默认值里是空的**（PS 5.1）→ 默认值写空串，进函数体再算。
* **`[ordered]@{}` 的整数键不能用 `$d[1234]` 取**：OrderedDictionary 的索引器把整数参数当成**位置**，
  返回 `$null`（不报错）→ 用普通 `@{}`，或者把键写成字符串。
* **`-f` 格式串里的 `{{`/`}}` 是转义大括号**：拿它拼 C++ 的 `{0x1234, 0x0023}` 会被解析坏
  （生成出 `0xx4`，GCC 报 `unable to find numeric literal operator 'operator""xx4'`）→ 花括号用字符串
  相加拼。
* **提交进仓库的 `.ps1` 带中文注释时必须写 UTF-8 BOM**：没有 BOM 时 PS 5.1 按 GBK 解码，中文注释里的
  字节会被当成引号/括号，脚本直接语法错（`UnexpectedToken`，报错行号还可能落在别处）。文件工具的
  默认写入是无 BOM 的，写完要转一次。

### 配置 / Lua / 核心逻辑

* **Lua 的头文件里没有 `extern "C"` 保护** → 在 C++ 里直接 `#include <lua.h>` 会 mangling，链接时报
  一屏 `undefined reference`。**统一用 `src/lua/lua_include.h`。**
* **`lua_next` 遍历中调 `lua_tolstring` 会把数字键就地转成字符串**，于是下一次 `lua_next` 收到一个
  不存在的键，触发 `PANIC: invalid key to 'next'`（abort）。规则：只对 `lua_type == LUA_TSTRING` 的键
  取字符串，数字键用 `lua_tointeger`。
* **嵌套遍历时内层压栈会打乱外层的 `lua_next` 游标** → 进循环前记 `loopBase = lua_gettop(L)`，
  每轮结束 `lua_settop(L, loopBase + 1)`。
* **`lua_next` 的“复制表”惯用法里不能再多弹一次**：`pushvalue(-2); insert(-2); settable(dst);`
  之后栈上正好剩下 key。
* **Lua C API 的栈不自动扩容。** `readTableList` 会把读到的每个条目都留在栈上，条目一多就直接写到
  数组之外 —— 现象是 `--check` 以 `0xC0000374`（heap corruption）崩掉，而且到 `lua_close()` 才报错。
  修法：压条目之前先 `lua_checkstack(L, 条目数 + LUA_MINSTACK)`。**诊断手法**：临时给 `lua_static` 加
  `LUA_USE_APICHECK`，它会把所有非法索引变成确定的 abort。
* **`-Werror` 下两个 Win32 小坑**：MinGW 的 `SendInput` 第二参是 `LPINPUT`（非 const），要
  `const_cast`；`GetProcAddress` 的 `FARPROC` → 具体函数指针会被 `-Wcast-function-type` 报错，用
  `std::memcpy` 绕开。
* **`HHOOK` 不是 `HANDLE`**。**`core::Trigger`/`Phase` 在 `core/engine.h`**，不在 `config.h`。
* **`QObject::moveToThread` 拒绝带 parent 的对象。**
* **`emit other->someSignal()` 在类外编译不过**（信号是 `protected`）→ 用公开方法 +
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 把 emit 挪回 GUI 线程。
* **`QCOMPARE` 的宏参数里不能出现“不受括号保护”的逗号**（尖括号不算括号）→ 起 `using` 别名。
* **`QCOMPARE(optional<int>, -1)` 是错的**：`-1` 会被隐式构造成 engaged 的 optional，而“没有值”是
  `std::nullopt`。
* **`QDir::filePath()` 一律用 `/` 当分隔符** → 输出前统一过一遍 `QDir::toNativeSeparators()`。
* **`QWheelEvent` 合成不出来**：`QCoreApplication::sendEvent(window, &wheelEvent)` 递进去之后
  `isAccepted()` 为 false、列表一点都不动。要验证滚轮只能 `SetCursorPos` + `SendInput`（而且弹窗必须
  是前台窗口）。
* **`WIN32_LEAN_AND_MEAN` 不包含 `ole2.h`** → `CoInitializeEx`/`CoCreateInstance` 要显式
  `#include <objbase.h>`。MinGW 的 `GUID` 可以直接聚合初始化。
* **MinGW 的头文件里没有 `MONITOR_OFF`**（`SC_MONITORPOWER` 与 `SMTO_ABORTIFHUNG` 有）。
* **`hresultMessage()` 以前用 `FormatMessageW` 解析 HRESULT，结果几乎总是 `error 0x…`。** 现在用
  `hresultText()`：`E_NOINTERFACE` 等几个常见值有人话名字。这对虚拟桌面特别重要：`E_NOINTERFACE`
  意味着版本表选错了 IID。
* **`RtlGetVersion` 可以从已加载的 `ntdll.dll` 用 `GetModuleHandleW` + `GetProcAddress` 拿**
  （不用加链接依赖）；`GetVersionEx` 会被应用清单骗，不能用。

### 构建 / 测试 / 打包

* **`QTEST_MAIN` 的默认输出在本机拿不到**：重定向、管道、`ctest --output-on-failure` 全是空的，而
  `-o <file>,txt` 能拿到完整结果。→ `cmake/RunQTest.cmake` 让测试先写文件、再由脚本 `cmake -E cat`
  出来。**新增测试不用做别的**，`flowkeyd_add_test()` 已经封好了。
* **把 `scripts\release.ps1` 的输出从 agent 的 bash 用 `>` 重定向到文件时，ninja 的中间一段输出会丢**
  （父 PowerShell 的 host 子进程与孙子 ninja 共享同一个继承的句柄、各自独立推进文件偏移）：
  **不是脚本吞输出**（`Invoke-Live` 就是裸 `& $Exe @ArgList` 直通）—— 判据看退出码、
  `build/dist-release` 的文件数、以及 `--version` 的哈希，别在 ninja 日志的断口上找 bug。
* **`qt_add_executable()` 会默认给测试套上 AUTOMOC**，连 `lua_static` 也不例外 →
  `lua_static` 上显式写 `AUTOMOC OFF`/`AUTOUIC OFF`。
* **`set_tests_properties(... ENVIRONMENT_MODIFICATION)` 的两个修改项必须写在一个 `"a;b"` 字符串里。**
  跑测试的 exe 需要 `PATH` 里有 Qt 与 MinGW 的 `bin`，否则 `ctest` 报 `0xc0000135`（DLL not found）。
* **带 qrc 的静态库，资源初始化会被链接器丢掉** → 在被引用的源文件里调一次 `Q_INIT_RESOURCE(name)`
  （必须在**全局命名空间**里）。另一个坑：`qt_add_resources(target path/to/foo.qrc)` 这个“直接传 .qrc”
  的写法在本机 Qt 6.11 上**什么都没生成**；用 `qt_add_resources(target "name" PREFIX "/x" BASE <dir>
  FILES …)`。
* **写测试用的假 `Evaluator` 不用真的 Lua**：`core::loadConfig()` 把求值回调当参数。
* **`windeployqt` 只会多拷、不会删。** 它按 qmldir 里的 `optional import … auto` 把六个 Quick Controls
  样式全拷了，没有开关能只留一个 → 想变小只能“拷完再删”（`cmake/PruneRuntime.cmake`）。
  **`--style` 这个选项本机 6.11 的 windeployqt 没有。**
* **发布包精简（1378 个文件 / 149.8 MB → 212 个 / 64.0 MB）**，四类东西（都实测过）：
  1. 没用到的那几个 Quick Controls 样式（Imagine/Material/Universal/Windows/NativeStyle，连同它们的
     QML 目录与 DLL）→ ~12 MB / ~500 个文件。
  2. `qmltooling/`（13 个插件）、`imageformats/{qgif,qjpeg,qsvg}` + `iconengines/qsvgicon` +
     `Qt6Svg.dll`、`networkinformation/`、`generic/qtuiotouch`、`Qt6Quick3DUtils.dll`、
     `plugins.qmltypes` → ~5 MB / ~40 个文件。
  3. **`opengl32sw.dll`（19.7 MB）与 `D3Dcompiler_47.dll`（4.0 MB）**：Qt Quick 在 Windows 上走
     D3D11 RHI（没显卡驱动时 WARP 也能画），而 Win10+ 的 `System32` 本来就有 `d3dcompiler_47.dll`。
     **这两个必须同进同退**（少了 D3D 编译器 Qt 会退回 OpenGL，那时才真需要软件回退）。失败时的现象
     是“弹窗与日志窗口出不来（托盘与快捷键还在）”。
  4. **exe 里的 43 MB 调试符号**（`RelWithDebInfo` 带 `-g`）→ release 用 `objcopy --strip-all` →
     1.6 MB。
  * **FluentWinUI3 的磁盘 .qml/.png 可以整份删掉（省 848 个文件）**：样式插件把整套资源**内嵌在自己的
    qrc 里**，模块 qmldir 里的 `prefer :/qt-project.org/imports/QtQuick/Controls/FluentWinUI3/` 就指向
    那里。验证“插件里真有这些资源”的手法：把 DLL 按 **UTF-16** 解码再查 `Config.qml` 这类名字（按
    ASCII 查一个都找不到）。**`qmldir` 必须留着**，`plugins.qmltypes` 可以删。
  * **Basic 与 Fusion 删不得**（实测报错 `module "QtQuick.Controls.Basic" is not installed`）：
    `QtQuick.Controls` 的 qmldir 有 `default import …Basic auto`，FluentWinUI3 有
    `import …Fusion auto`。两个合起来约 4 MB，作为保险留着。
  * **`tls/` 不能整个删**：加了 `Qt6::Network` 之后 https 靠 `tls/qschannelbackend.dll`。只删
    `qopensslbackend.dll` 与 `qcertonlybackend.dll`。
  * **`objcopy --strip-all` 不是幂等的**（每次都重写 PE 头 COFF 的 `TimeDateStamp`）→ dist-release
    那一步**不再 strip**（源 exe 已经被 strip 过），否则两棵 release 树的 exe 就不再逐字节相同。
  * **改了精简清单必须重新链接**（prune 挂在 `POST_BUILD` 上，只在链接时跑）→
    `set_property(TARGET flowkeyd APPEND PROPERTY LINK_DEPENDS …/cmake/PruneRuntime.cmake)` 已加。
  * **精简过头的及早发现**：五个弹窗启动时就预热，缺模块会立刻报
    `could not load ….qml: module "…" is not installed`；`scripts/acceptance.ps1` 有一条哨兵检查盯着
    这个。**日志窗口不在预热里**，它只 import `QtQuick`/`Controls`/`Layouts`。
* **`Compress-Archive` / `ZipFile::CreateFromDirectory` 在 Windows PowerShell 5.1 里把 zip 条目名的
  目录分隔符写成 `\`**（Windows 能解，但 unzip/tar/WSL 会解出一堆名字里带反斜杠的文件）→ 发布脚本的
  `New-ZipPackage` 自己用 `ZipArchive` 逐个写条目。（`ZipArchive`/`ZipArchiveMode` 在
  `System.IO.Compression`，`ZipFile` 在 `System.IO.Compression.FileSystem`，**两个都要 `Add-Type`**。）
* **发布时“哪个文件进哪个包”用白名单 + 兜底报错**（`scripts/release.ps1` 的 `$SlimFiles` /
  `$DependencyPatterns`）：`dist` 里出现两边都不认识的文件就直接失败，逼人当场分类。
* **发布说明要自己写，不要指望 `gh --generate-notes`**：这个仓库是直接往 master 上提交的（不开 Pull
  Request），`--generate-notes` 除了一行 `**Full Changelog**` 什么都给不出来。**正文只有 changelog**
  （按提交信息前缀分类 + 末尾自己拼的 Full Changelog 比较链接）；「下载哪个包」那一段**不写进发布页**。
  用 `--notes-file` 交出去；`-Clobber` 重发同一个 tag 时也顺手 `gh release edit` 刷新正文 ——
  **`gh release edit` 没有 `--generate-notes`**，所以整份正文必须我们自己写全。
* **PowerShell 5.1 按控制台代码页（本机 GBK/936）解码原生命令的 stdout**：`git log` 里中文提交信息的
  UTF-8 字节会被解成乱码，而且 **GBK 的双字节序列会把 `"\n"` 当尾字节吃掉** —— 表现为 `git log` 少一条
  提交、两条提交粘成一行（发布说明直接少列一个改动）。修法：脚本开头
  `[Console]::OutputEncoding = UTF8`（没有控制台时这个赋值会抛异常，包在 try/catch 里）。
* **tar/zip 里的 `README.txt` 用带 BOM 的 UTF-8 写，`.sha256` 不带 BOM**（`sha256sum -c` 认的是逐字节
  内容）。

### QML / 界面

* **QML 的 model 角色名不能叫 `text`/`highlighted`/`hovered`**：标准控件（`Text`、`ItemDelegate`）自己
  就有这些属性，`required property` 的名字必须等于角色名，撞名就声明不了。本项目用 `line`/`level`、
  `rowSelected`、`rowArmed`、`rowKey`、`windowTitle`/`windowProcess`。委托里高亮写
  `highlighted: rowItem.rowSelected`。
* **delegate 里访问外层组件的 id 会报 `Unqualified access`** → 文件开头
  `pragma ComponentBehavior: Bound`。JS 数组模型的 delegate 要写 `required property var modelData`。
* **用 `qmllint -I C:\Qt\6.11.2\mingw_64\qml <file>.qml` 提前查问题**（**不要加 `--bare`**）。QML 类型的
  类名带 id 后缀（`TextField_QMLTYPE_1544`），遍历 item 树找控件要用 `contains("TextField")` 而不是
  相等比较。
* **`import QtQuick.Controls.FluentWinUI3` 里确实能用 `Label`。**
* **本机 225% 缩放下 Qt 报出的 `availableGeometry()` 比 `geometry()` 还宽**（工作区从 x=108 开始、
  宽 485，而屏幕只有 533 宽）→ 居中之后再按屏幕 `geometry()` 夹一次（`app::centrePopup()`，纯函数、
  有单测）。
* **用 DPI 不感知的 PowerShell 进程 `GetWindowRect` + `PrintWindow` 会拿到错的结果**（坐标被虚拟化成
  逻辑像素）→ 让 Qt 自己抓（`QScreen::grabWindow(window->winId())`），或先 `SetProcessDPIAware()`。
* **弹窗缩放过期：预热好的卡片在显示器缩放变过之后只剩一半大。** 项目所有者 2026-10 报的
  「启动器弹出界面尺寸又变小了」（之前那次「过小」是卡片高度按内容收缩，是另一回事）。根因：五个弹窗
  窗口在启动时预热一次就一直留着复用，而它们平时是**隐藏 + 摆在屏幕之外的**；用户后来把系统缩放从
  100% 改成 200% 时，这种窗口收不到 `WM_DPICHANGED`（它们不属于任何显示器），Qt 也就不刷新它们的
  `devicePixelRatio` —— 于是窗口的**物理**尺寸停在「逻辑数字」上。真机实测（从外面用 `GetWindowRect`
  量，先 `SetProcessDpiAwarenessContext(PMv2)`，不然拿到的是虚拟化坐标）：

  | 状态 | menu | help | switch | apps | update |
  | ---- | ---- | ---- | ------ | ---- | ------ |
  | 缩放变过（坏） | 300×394 | 500×708 | 560×670 | 800×1244 | 520×360 |
  | 重启进程（好） | 600×788 | 1000×1416 | 1120×1340 | 1600×1244 | 1040×720 |

  `apps` 那行最能看出问题：宽度 800 物理 = 400 逻辑，6 列网格（756 逻辑像素）被切掉一半。**注意
  `GetDpiForWindow` 照样报 192**，所以「窗口 DPI 对不对」是看不出来的，只有真实物理尺寸能看出来。
  修法两层，都在 `PopupHost`：
  1. **屏幕配置一变就丢掉窗口缓存重建**（`discardPopupWindows()` + 重新 `preload()`）：接
     `QGuiApplication::screenAdded`/`screenRemoved` 与每个 `QScreen` 的 `geometryChanged`/
     `availableGeometryChanged`/`logicalDotsPerInchChanged`/`physicalDotsPerInchChanged`，500 ms 去抖
     （一次显示器切换会连着发好几个信号，而建一整套五个卡片要几百毫秒）。
  2. **每次弹出前量一遍真实尺寸**：`GetWindowRect` 与「逻辑尺寸 × 这块屏的缩放」不符就丢掉重建。判断是
     纯函数 `app::popupPixelSizeIsStale()`（`tst_menu_model` 盯着），容差几像素是为了无边框窗口的边框 /
     DWM 阴影；拿不到尺寸（窗口还没建出来）时不重建。

  重建时**模型必须留着**：启动器的固定 / 最近使用就存在 `AppListModel` 里，`ensureXxxWindow()` 里那几个
  「模型只建一次」的判断就是为它加的；`discardPopupWindows()` 里也要先 `restoreSwitchInputMode()`
  （否则输入法那份快照白丢了）。回归守卫两处：`tmp/preview/` 的 `checkStalePopupScale()`（把（隐藏的）
  窗口强行缩成 800×622 物理像素再弹出 —— 用旧窗口上的一个 `setProperty` 标记断言「真的换了一个窗口」，
  锁屏也能跑）与 `scripts/acceptance.ps1` 里那一条。
* **`ItemDelegate` 的内边距覆盖不掉**（FluentWinUI3 的 `ItemDelegate.qml` 用绑定给每个实例定内边距，
  实测左右 12 / 上下 8）→ **不要和样式的内边距较劲**：内容区里的东西全部锚在 `contentItem` 上，让它
  自适应。
* **`QQuickTextInput` 故意忽略 `↑`/`↓`**，所以它们会冒到父项的 `Keys`；而 `Keys` 的默认优先级
  `Keys.BeforeItem` 意味着挂在 `TextField` 上的 `Keys.onPressed` 在 `TextInput` 自己的键盘处理之前跑
  —— 这就是“一个输入框 + 一个列表”的键盘分工。
* **`Keys.onPressed` 的 `event.key` 与 `event.modifiers` 都是 `int`**（`QQuickKeyEvent` 里两个
  `Q_PROPERTY(int …)`）→ C++ 侧接 `handleKey(int key, int modifiers)`、QML 里写
  `model.handleKey(event.key, event.modifiers)` 不会有类型转换问题 —— 改 `Q_INVOKABLE` 的签名时要同步
  改 `tst_app_list_model` 里 `indexOfMethod("handleKey(int,int)")` 那一串（旧签名会直接变成 -1）。
* **筛选回写用 `onTextChanged` + 等值判断，不要 `onTextEdited`**（后者在输入法提交/粘贴/拖选时不一定
  发）。`if (field.text !== model.filter)` 保证不会来回振荡。
* **`ListView.positionViewAtIndex(..., Contain)` 不能用来“把选中行带进视野”**：它只保证行落在**列表
  自己的矩形**里，**不看** `topMargin`/`bottomMargin`。`positionViewAtBeginning()` 更坑：它想去
  `contentY = 0`，被 Qt 的 `qBound` 夹到了**最大位置**。→ 自己算
  `HelpModel::scrollTargetY(...)`（纯算术、有单测），QML 只把结果写回 `listView.contentY`。
* **列表刚建好时 `contentY` 会被摆到一个“保持滚动比例”的位置**（实测 30 条时是 `contentY = 90` 而不是
  顶部的 `-88`），而且发生在收到 `selectedChanged` **之后**。**但这一次不要挂在 `onContentHeightChanged`
  上**：换视图时靠 `onSelectedChanged` / `onHeightChanged` / 模型 reset 就够了。
* **`ListView.contentHeight` 在滚动中是“估算值”，会抖。** 还没创建出来的委托用「已见过的高度的平均值」
  估高，而滚动时创建的委托一直在换，于是 `contentHeight` 每滚一点就变几像素（实测 123 行、混合 26/44
  高的行：8597 → 8602 → 8406 → …）。所以 **`onContentHeightChanged` 里不要摆选中行**：
  `positionViewAtIndex(选中行, Contain)` 会把滚轮 / 拖滑块刚滚出来的位置拽回选中行（第 0 行 = 顶部），
  现象就是「有滚动条但滚不动」。行高全都一样时（帮助窗口就是）它不抖，所以这个坑只在“一行一种高度”的
  列表里出现。
* **两个弹窗都是 `WindowStaysOnTopHint`，会互相遮住**；`grabWindow` 抓的是屏幕那块区域。
* **`QWindow::setProperty("visible", …)` 是隐藏/显示一个 QML `Window` 的最省事办法**（窗口不会被销毁，
  可以复用）。
* **`Keys.onPressed` 只在窗口是活动窗口时才会把事件交给有焦点的 item。**
* **进程内预览里 `console.log` 不一定看得见** → 用“写一个能从 C++ 读回来的属性”调试。
* **`GetWindowRect` + `SetCursorPos` 必须同一种像素**（脚本开头先 `SetProcessDPIAware()`）。弹窗几何靠
  **宽度反推缩放**（menu 卡片 300、help 卡片 500 逻辑像素）比猜 DPI 稳。
* **「同一行有多种形状」的列表**（启动器的分段表头 / 网格行 / 整行按钮 / 分组列表行）：在**一个**
  `ListView` 的委托里按 `rowKind` 把几个**内联组件**（`component HeaderRow: Label { … }`、
  `component GridRow: Row { … }`、`component ButtonRow: ItemDelegate { … }`、
  `component ListRow: ItemDelegate { … }`）都实例化出来、用 `visible:` 开关。**不用 `Loader`**：
  `pragma ComponentBehavior: Bound` 下，`Loader` 装出来的 item 拿不到委托自己的属性（它只能看文件
  作用域的 id）。四种形状都建出来也不贵 —— `ListView` 只给看得见的十来行建委托。**内联组件可以直接用
  文件根对象的 id**（`root.appModel` / `root.cellWidth` 这类），`qmllint` 与运行时都验过。
* **内联组件的属性名不能撞基类自己的**：`component HeaderRow: Label` 里想接文字就得叫 `label`
  （`text` 是 `Label` 自己的）、`component ButtonRow: ItemDelegate` 同理；模型的角色名也不能叫
  `highlighted`/`text`。
* **`Space` 会和标准 `TextField` 抢**：`Keys.onPressed` 是 `Keys.BeforeItem`，所以“按 `Space` 固定”
  会把筛选框里的空格一起吃掉。**只在筛选串为空时截走它**（`handleKey()` 返回 `handled: false` 放行），
  否则用户打不出 `visual studio` 这种名字。
* **`ListView` 的行高与卡片高度是两回事**：卡片高度**恒定** = `maxRows × cellHeight` + 上下占位
  （`AppListModel::relayout()`），而行高是每种行的形状自己的事（表头 26 / 格子 88 / 整行按钮 40 /
  分组列表一行 44）—— 不要再把「各行高度之和」当成卡片高度（那样只固定一两条时卡片会缩成 248 高）。
  对了人和机器各验一次：模型侧断言 `cardHeight()`，外面用 `WindowRect` 看真实高度。
* **`Q_PROPERTY` 写自定义结构体是静默失效的**：`AppListModel` 一开始写的是
  `Q_PROPERTY(PopupRect filterRect …)`（成员确实是 `PopupRect`），QML 那边 `filterRect.x` 全是
  `undefined` → 筛选框、底部提示、整个网格全堆在左上角，而且**没有一条 QML 警告**。必须像
  `window_list_model` 那样把属性类型写成 `QRect`（`PopupRect` 有隐式转换）。光看 `qmllint` 与单测都
  发现不了，是“把窗口 grab 成图看一眼”看出来的。

#### 图形化配置界面（2026-10，下面这些都是在真机上撞出来的）

* **`Row`（定位器）+ 子项锚点当 `ItemDelegate.contentItem` 会直接崩。** 现象：一个由
  `Repeater` 建在 `ColumnLayout` 里的 `ItemDelegate`，它的 `contentItem` 是一个 `Row`，行里有
  几个带 `anchors.verticalCenter` 的按钮 —— **切换到这个页面就 SIGSEGV，一条警告都不打**
  （`Row` 的 `implicitWidth` 又要拿回委托自己的宽度去算，绕回了布局引擎）。实测：同一个 `Row`
  里放 1 个按钮没事、放 5 个必崩；内容区换成 `RowLayout` + `Layout.alignment` 就稳。所以**凡是给
  标准控件做内容区的地方，一律用布局，不在定位器里给子项加锚点**（`EntryRow`、`ActionEditor`、
  `ActionListEditor`、`AppOverlay`、`ChordsEditor` 里那几行按钮共用一套写法）。定位器里确实需要
  垂直居中的，用 `RowLayout` 的 `Layout.alignment: Qt.AlignVCenter`。诊断方法：把可疑的那一小段
  拆成一个单独的小 QML（`tmp/` 里）+ `qml.exe` 跑一遍，看退出码（`139` = 崩）。
* **给弹层塞数据必须在切 `overlayKind` 之前。** 弹层是用 `Loader` + `sourceComponent` 装的，而
  **`overlayKind` 一变，`Loader` 就同步把弹层建出来并跑 `onLoaded` 里的赋值**；先改
  `overlayKind`、后写 `overlayEntry` 的话，弹层拿到的是上一次的（首次是空的）条目 —— 界面看起来
  完全正常，但「确定」写回去的是一个空条目，而且是静默的。现在只留一个入口 `showOverlay()`，
  顺序写在里面。
* **`Component.onCompleted` 里读不到还没挂上来的东西。** 下拉框（`ChoiceRow` / `TriRow` / 动作类型
  框）一开始都用 `Component.onCompleted: currentIndex = options.indexOf(value)`：如果这个控件是在
  `editor` / 数据挂上来**之前**就建好的（弹层里、`Loader` 装出来的动作编辑器里），那时 `options`
  还是空数组，`currentIndex` 就被定死在 0 —— 于是**界面显示的取值与真实值不一样**
  （一个 `trigger = "release"` 的绑定显示成了「按下时」），照样一声不响。改成 `currentIndex:` 绑定
  （它会在依赖变化时自己重算）就好了。判断这类问题的唯一手法：拿真数据建一张图看一眼。
* **不可见的那一段的绑定也会被求值。** `run.args` 是字符串列表，而 `open.args` 是**一个字符串**，
  两者同名；编辑一个 `open` 动作时 `run` 那一段虽然 `visible: false`，它的 `text: linesOf(action.args)`
  照样跑，于是 `"…".join is not a function`。凡是「同名但类型不同」的字段，取值的辅助函数都要先
  `Array.isArray()` 再 `join`。
* **信号处理器的隐式参数已弃用**：`onPicked: root.action.op = value` 里的 `value` 是 Qt 注入的
  （它能工作，但会打 `Parameter "value" is not declared` 警告，而且与同名属性（`BoolRow.value`）
  撞在一起）。写成 `onPicked: function (value) { … }` 就干净了 —— 单表达式处理器（`onEdited: f(value)`）
  必须改。
* **验证这套界面不需要编译 C++。** `tmp/` 里放一份「假模型」（`QtObject`，属性/方法名与
  `app::ConfigEditor` 一样）+ 一个把 `ConfigWindow` 包一层的 `Preview.qml`，然后
  `qml.exe Preview.qml` 就能把整套界面跑起来（**记得 `QT_ASSUME_STDERR_HAS_CONSOLE=1`**：
  GUI 子系统的 `qml.exe` 不打控制台，日志会全丢；不设它时看到的是“什么都没输出”）。再配一个
  定时器逐页切、逐弹层开、用 `contentItem.grabToImage()` 抓图存 PNG（**图要人眼看过**：上面那两条
  “静默地显示错值”就是这么发现的），锁屏也能跑。`src/qml` 里改了文件就拷一份过去；目录里再放一个
  `qmldir`（就一行 `singleton ConfigText 1.0 ConfigText.qml`）才认得单例。想验证**模块装载**那条路
  （守护进程走的是 `loadFromModule`）就把 `build/<preset>/Flowkeyd` 整个拷到一个临时目录、删掉
  qmldir 里的 `prefer :/qt/qml/…` 那一行（那个 qrc 在 exe 里）再用 `-I` 指过去。
  只想验**某一个表单组件**（例如 `ActionEditor` 的某个动作分支或 `LaunchFieldsEditor`）时
  不必造假模型：`editor: null` 就够了（代码里到处都写了 `root.editor ? … : []`）—— 把每种
  动作各建一个 `ActionEditor`、让它跑两秒，再看 stderr 里有没有 `TypeError` /
  `is not a function` / 绑定警告（**`console.log` 也走 stderr**，所以要
  `QT_ASSUME_STDERR_HAS_CONSOLE=1`；GUI 子系统的 `qml.exe` 不设它时什么都看不到）。
* **动作的 `launch` 有两个写法，别只给一个写界面。** `window` 动作的 `launch` 与 `app{}` 的
  `launch` 字段完全一样（`program`/`args`/`cwd`/`show`/`shell`/`env`/`wait_ms`），但
  `ActionEditor.qml` 一开始是手写的几个格子（只有 `program`/`args`/`cwd`/`wait_ms`），于是
  `show`/`shell`/`env` 在界面里**改不了**（配置里写了的那三项能原样保留，只是看不见、改不动，
  一声不响）。现在两者共用 `LaunchFieldsEditor`（`showWaitMs: false`，`wait_ms` 由动作顶层那条
  自己画）。**同一份字段块只在一个组件里实现**，这就是 `WindowRuleFields` / `HotkeyFields` 已经
  用的办法。
* **`remap{}` 的 `name` / `enabled` 一度没写进 README。** 配置认、界面认、`--list` 也打，只有
  README 的 `remap{}` 那一节漏了（第 14 节的速查表当时也漏了 `enabled`）。**对照三处清单
  （工作约定第 13 条）时别只查“代码里有没有”，还要把文档那一列逐字段过一遍**：这两个名字在
  `hotkey{}` 的表格与示例配置里都出现过，所以 grep 字段名是对不出来的 —— 得按“每一节自己的
  字段表”核对。

### Windows 领域坑

* **开始菜单「所有应用」= `shell:AppsFolder`**（`Get-StartApps` 也是枚举它）。两个
  `Start Menu\Programs` 目录里**只有 `.lnk`**（真机 124 个）：商店/UWP 应用根本没有 `.lnk`（它们注册在
  包清单里），`.msc` 系统工具其实有 `.lnk`、但目标是 `.msc` 而不是 `.exe`。所以「目标必须是 `.exe`」
  那道旧过滤会**静默丢掉**计算器、记事本、终端、服务、事件查看器、计算机管理…… —— 项目所有者
  2026-10-05 就是这么发现启动器「少了程序」的。
* **控制面板的「所有控制面板项」= `SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder)`**
  （`{82A74AEB-AEB4-465C-A014-D097EE346D63}`）：真机解析出来就是
  `::{26EE0668-A00A-44D7-9371-BEB064C98683}\0`（也是 `Shell.Application.Namespace(3)` 看到的那个，
  36 条）。**别拿 `{26EE0668-…}` 本身去枚举**：那是 8 个分类的分类视图。另外「网络连接」是 delegate
  folder，不在这个枚举结果里，要单独按解析名补。
* **控制面板项的启动名必须验证回程**：`SIGDN_DESKTOPABSOLUTEPARSING` 有时给的是带**本地化显示名**的
  路径（真机：「字体」→ `::{26EE0668-…}\0\字体`），`SHParseDisplayName` 解不回去 → 采用前先自己解析
  一次（失败退回 `SIGDN_FILESYSPATH`，两个都不行就丢掉）。还有条目把解析名当显示名给回来
  （`SIGDN_NORMALDISPLAY` 返回 `::{…}` 开头的东西），那种在列表里就是一行乱码，也丢掉。
* **`ms-settings:` 页面在 shell 里没有名字也没有图标**：`SHCreateItemFromParsingName` 能建出条目、
  `SHParseDisplayName` 也解析得了（所以启动照常交给 shell），但 `SIGDN_NORMALDISPLAY` 返回的就是 URI
  本身，`GetImage` 也拿不到像样的图标 → 名字只能自己带一份中英对照、图标借「设置」应用那一张
  （`core::appIconLaunchName()`）。
* **往启动器的目录里加来源时别忘了「同名去重」**：三份来源之间真的会撞名（真机：「Windows 工具」）→
  `core::appendLauncherEntries()`（开始菜单那份在前，所以保留它）。
* **AppsFolder 条目的属性要自己读**：`SIGDN_NORMALDISPLAY` + `PKEY_AppUserModel_ID` /
  `PKEY_Link_TargetParsingPath` / `PKEY_Link_Arguments`。AUMID 有三种形状：`<包家族名>!<AppId>`（UWP）、
  显式 AUMID（`Chrome`、`MSEdge`）、**目标路径当 AUMID**（scoop / Qt 那些没设过 AppUserModelID 的）；
  `.msc` 那些是 `Microsoft.AutoGenerated.{GUID}`。目标可能是 `{已知文件夹 GUID}\相对\路径.exe` 这种
  shell 写法，所以**只能看末尾扩展名**，不能拿去 `QFileInfo`。**用 `propkey.h` 里的 `PKEY_*` 要引
  MinGW 的 `libuuid`**，所以 GUID/PID 是手写在 `apps.cpp` 里的。
* **`shell:AppsFolder\<AUMID>` 一个字符串能干三件事**（真机 159/159 都验过）：启动
  （`ShellExecuteExW` 把解析名当 `lpFile`，或 `SHParseDisplayName` + `SEE_MASK_IDLIST`）、取图标
  （`SHCreateItemFromParsingName` + `GetImage`：159 个都有图标，而且没有两个是同一张）、当右键菜单的
  输入（`QueryContextMenu` 对 UWP / `.msc` / `AutoGenerated` / `::{…}` 都给出「打开 …」这类正常菜单）。
* **名字变成显示名之后就是中文了，目录顺序不再等于显示顺序。** `core::prepareAppEntries()` 按名字
  （码点）排，中文全落在拉丁名字后面；真正给用户看的顺序是 `core::appSortText()`（拼音）。别依赖扫描
  结果的顺序。
* **图标键（= 固定 / 最近使用的身份）从「`.lnk` 路径」换成「启动名」之后，`launcher.json` 会整份失配**
  （只是丢掉固定 / 最近，不报错）。而且只靠目标路径当 AUMID 的那些程序（scoop 工具、Qt 多版本工具）
  在换版本 / 换路径后会变成一条新条目 —— 已写进 README 的已知限制。
* **验收脚本里那个「单字母筛选串只命中 1..8 个程序」的前提很紧**：条目涨到 198 之后真机上 26 个字母里
  一个都不满足（`a` 命中 89）→ 凑不到时脚本打诊断并**跳过**那 3 条数字键检查。
* **`SetTimer(nullptr, id, …)` 会忽略 `id` 并返回一个新的定时器 id**，`WM_TIMER` 的 `wParam` 就是那个
  新 id。这曾让 `Engine::tick()` 从未被调用（长按重复一直是坏的）→ 把返回值存进成员再比较，
  `KillTimer` 也用返回值。
* **`SetForegroundWindow` 除非持有前台锁否则被拒**（递进式绕行 + `AttachThreadInput` 配平）。
  **而且它在“目标已经是 shell 的前台窗口，只是不在当前虚拟桌面上”时是空操作**（返回 TRUE 却什么都不
  发生）—— 被 `MoveViewToDesktop` 搬走的窗口就是这个状态，只能先显式 `SwitchDesktop`。
* **`GetForegroundWindow()` 不等于“用户看得见的窗口”**：窗口被搬到别的虚拟桌面之后 shell 还把它当前台
  窗口（而公开的 `IsWindowOnCurrentVirtualDesktop` 返回 FALSE）。
* **`IVirtualDesktopManagerInternal::MoveViewToDesktop` 在 vtable 下标 4**（三种布局一致），需要先用
  `IApplicationViewCollection::GetViewForHwnd`（IID/SID 都是
  `{1841C6D7-4F9D-42C0-AF41-8747538F10E5}`，下标 6）把 `HWND` 换成 `IApplicationView*`。
  **已公开的 `IVirtualDesktopManager::MoveWindowToDesktop` 拒绝移动别的进程的窗口。**
* **`MoveViewToDesktop` 返回 S_OK 不等于窗口真的换了桌面。** 验证要用**已公开**的
  `IVirtualDesktopManager::GetWindowDesktopId`（`CLSID_VirtualDesktopManager` `{AA509086-…}`，
  IID `{A5CD92FF-…}`，下标 4）；`IsWindowOnCurrentVirtualDesktop`（下标 3）也能用，但 shell 是**异步**
  移动的，要轮询等它生效。
* **`IVirtualDesktop::GetID` 在 vtable 下标 4**（先 `IsViewVisible`（3），照抄 VD.ahk 的
  `VD_goToDesktopOfWindow`），用来把窗口的 `GetWindowDesktopId` 对到内部枚举的桌面上；**拿“有且只有
  一个匹配”当自检**。
* **`IVirtualDesktopPinnedApps` 的 IID 自 Win10 起就没变过**（SID
  `{B5A399E7-1C87-46B8-88E9-FC5747B171BD}`、IID `{4CE81583-1E4C-4632-A621-07A53543148F}`，vtable
  `3 IsAppIdPinned / 4 PinAppID / 5 UnpinAppID / 6 IsViewPinned / 7 PinView / 8 UnpinView`），所以
  **不进版本表**。`PinView` 返回 S_OK 本身证明不了什么，要用 `IsViewPinned` 从外面确认状态变了。
* **虚拟桌面接口表的 `build.revision` 是“起始版本”不是上限**（取“生效版本不高于当前系统的最后一条”）；
  修订号用 `RegGetValueW` 从注册表读 `UBR`。
* **“另一个桌面上的窗口”是被 DWM cloak 掉的。** `DWMWA_CLOAKED` 非零 = 没显示出来，但**不能单独用**：
  `cloaked != 0` + `IsWindowOnCurrentVirtualDesktop == FALSE` → 在别的虚拟桌面上（**要留**）；
  `cloaked != 0` + `== TRUE` → shell 藏在当前桌面上的假窗口（**排掉**）。两个 `DWM_CLOAKED_SHELL(2)`
  在数值上分不开，只能问一句虚拟桌面。
* **`WS_EX_TOPMOST` 只能靠 `SetWindowPos` 设，不能靠 `SetWindowLongPtr`**（后者会出现返回 TRUE 却
  什么都没发生）。
* **新建窗口后立刻置顶可能无效**（shell 还没登记它：`GetWindowDesktopId` 给全零 GUID、
  `IsWindowOnCurrentVirtualDesktop` 报 FALSE）→ 先对它做一次窗口操作（`applyPlacement`）再置顶。产品
  路径不会碰到这个。因此 `desktop::isWindowOnCurrentDesktop` 把“不属于任何虚拟桌面”当成
  `std::nullopt`（不知道），而不是“在别的桌面上”。
* **有属主的窗口（对话框/工具提示/弹出菜单）永远不是用户想要的那个窗口。** “主窗口”判据还必须带上
  **非 `WS_EX_TOOLWINDOW`** 与**有标题**：按 `process` 匹配会一次命中一堆内部窗口（`Non Client Input
  Sink Window`、无标题的 `NotepadTextBox`）。`WS_EX_APPWINDOW` 那一条照任务栏/Alt+Tab 的规则来
  （“无属主 **或** 带 `WS_EX_APPWINDOW`”）。
* **`core::windowProcessMatches()` 里的空 needle 是「不限制」的意思、返回 true**（它本来是给 `window`
  动作的可选 `process` 用的）。所以任何拿它做名单匹配的地方都**不能**把空串当普通条目：远程桌面名单里
  混进一个 `""` 会让**所有**前台窗口都算远程桌面、快捷键整片失效。修法两层：加载时拒绝空条目
  （`settings.remote_desktop.processes[%1]`），运行时的 `isRemoteDesktopProcess()` 也 `continue`
  跳过它。
* **`SetWindowPlacement` 是跨显示器摆放的关键。** 顺序是：`IsZoomed` 就先 `SW_RESTORE`，然后写
  `WINDOWPLACEMENT.rcNormalPosition`（屏幕坐标）并设 `showCmd`，最后对非最大化的情况再补一次
  `SetWindowPos`（`SWP_NOACTIVATE`）。最小化的窗口只更新“还原位置”。`SetWindowPlacement` 不会抢
  焦点。
* **`DWMWA_TRANSITIONS_FORCEDISABLED` 读不回来**，只能“设 TRUE → `ShowWindow` → 设回 FALSE”
  （RAII `TransitionGuard`）；DWM 在过渡**开始**时读它。
* **终端窗口不能靠标题找**（`process = "wezterm"`，窗口属于 `wezterm-gui.exe`）。
* **`run`/`window.launch` 走 `CreateProcess`、不查 `App Paths`、也不能用 `DETACHED_PROCESS`**
  （会静默杀死控制台子进程，用 `CREATE_NO_WINDOW`）。
* **启动的子进程默认不再继承管理员令牌**（第 2 节第 35 条）：提权实例会从登录 shell 复制一份
  令牌，用 `CreateProcessWithTokenW` 启动。三个真机踩过的点：
  * **`CreateProcessAsUser` 用不了**：它要 `SeAssignPrimaryTokenPrivilege`，而提权管理员令牌里
    根本没有这条（`AdjustTokenPrivileges` 报 1300 ERROR_NOT_ALL_ASSIGNED、
    `CreateProcessAsUser` 报 1314 ERROR_PRIVILEGE_NOT_HELD）。`CreateProcessWithTokenW` 只要
    `SeImpersonatePrivilege`（提权管理员默认就有），代价是经过 Secondary Logon 服务。
  * **不能同时传 `lpApplicationName` 与 `lpFile`**：那样子进程的 `argv[0]` 变成 exe 路径、
    **原来第一个参数被吞掉**（`--shell-open <路径>` 会变成 `argv[1] = <路径>`……看起来“什么都没发生”）。
    要么只传 `lpApplicationName = nullptr` + 完整命令行（现在的做法），要么把 `lpFile` 置 null。
  * **`CreateProcessWithTokenW` 会丢掉我们的环境块**（真机：代理进程里 `PATH` 没了，于是子进程
    找不到 `flowkeyd.exe` 旁边的 Qt DLL，报「找不到 Qt6Core.dll」）→ 必须显式给 `lpEnvironment`
    （`buildEnvironmentBlock({})` + `CREATE_UNICODE_ENVIRONMENT`）。`runCommand()` 因此**总是**
    显式传环境块（两条路都一样，免得“用 token 启动的子进程没有 `PATH`”这种隐形的半残）。
* **代理进程 = 我们自己的 exe**，所以从**别的可执行文件**（例如 `tst_interactive.exe`）里调用
  时要显式告诉它用哪个 exe（`runShellHelperAsUser(request, helperExecutable)`）；默认是
  `currentExecutablePath()`。不提供这个口的后果曾经是：测试进程把测试自己又启动了一遍，
  在那条路径上它是 MTA、而且没有控制台，于是弹一个「找不到 Qt6Test.dll」。
* **从提权的进程里 `ShellExecuteW` 开出来的东西也是提权的**（实测：子进程 `whoami /groups`
  仍报 `S-1-16-12288`）。这就是 `open` / 程序启动器必须绕一次代理进程的原因。
* **中文错误文案不可断言**（`FormatMessageW` 是本地化的）—— 这也是我们日志与校验信息保持英文的实际
  原因。
* **`IShellItemImageFactory::GetImage()` 在 `.lnk` 上很慢**：真机实测每个图标约 **3 ms**（首次冷启动
  那一个可 148 ms；159 个连取一遍约 400 ms）。所以它只能在“按需 + 缓存 + 异步”的路径上用。它给回来的
  `HBITMAP` 是 32 位 **直通 alpha（不是预乘）**、**自顶向下**的 BGRA：真机验过（83 个图标里 32973 个
  半透明像素中有 15155 个 `RGB > alpha`，预乘的数据不可能出现），对应 `QImage::Format_ARGB32`，
  `GetDIBits` 时必须给**负的** `biHeight`。
* **`CreateIconIndirect(hbmMask = nullptr)` 会失败**（即使 hbmColor 是 32 位带 alpha 的）：文档说
  mask “被忽略”，但它仍然要一个有效的位图句柄。想走 `QImage::fromHICON()` 那条路得自己造一张 1bpp 的
  掩码；本项目改成直接读 DIB 像素（代价是 `flowkeyd_platform` 多静态链一个 `gdi32`，见第 3 节）。
* **快捷方式的名字不能用 shell 那两套**：`SHGFI_DISPLAYNAME` 对一半的 `.lnk` 返回空串或被截断的值
  （真机：`iSCSI Initiator` → `iSCSI`、两个 ODBC 都变成 `ODBC`），`IShellLink::GetDescription` 是
  “Open Visual Studio 2026 Tools Command Prompt for targeting x64” 这种冗长的提示语。
  **用文件名去掉 `.lnk`**。
* **`IShellLink::GetPath` 可能返回 `%windir%\system32\...` 这种没展开的路径** → 去重前先
  `ExpandEnvironmentStringsW`，否则同一个程序的两条快捷方式会被当成两个。
* **输入法（IMM32/TSF）的转换模式：别的应用不受影响，但本进程的几个窗口互相看得见**（在一个窗口里按
  `Shift` 切中文，另一个窗口读到的也带 `NATIVE` 位）。同一台机器上不同窗口报出的标志位还不一样（见过
  `0x800`/`0xc00`/`0xfb0`）—— **只有 `NATIVE`（0x1）那一位有意义，还原时整份照抄**，不要自己拼一个
  `IME_CMODE_ALPHANUMERIC(0)` 写回去。从另一个进程去 `ImmGetContext` 拿不到上下文（连
  `AttachThreadInput` 也救不回来），只能在进程内验证。
* **`windows.h` 的 `DELETE` 宏与 `core/keys.h` 的 `Vk DELETE` 撞名**：把任何会拉进 `windows.h` 的
  头文件加进被 `main.cpp` 包含的头里就会炸（报 `expected unqualified-id before numeric constant`）。
  平台头只放在 `.cpp` 里。
* **`.arg(标题, 句柄)` 会把句柄当成字段宽度**（Qt 6 有 `arg(const QString &, int fieldWidth, QChar)`
  重载）：一条日志会变成 132 KB。**两个参数的 `.arg()` 里只要有一个不是 `QString`，就自己先转成字符串**
  （本项目用 `handleText(hwnd)`）。
* **`QDate::fromString(text, "yy-MM-dd")` 的两位年份是启发式的**（实测 `26` 解成 **1926**），而
  `toString` 反着写就是 `26-09-27`；解析自己的输出必须自己拼年份。
* **Windows 上“正在运行的 exe”可以改名、不可以删/覆盖**：在线更新就是靠这个把旧 exe 改成
  `<exe>.old` 再把新的改名就位；`.old` 只能由**新进程**删。`MoveFileExW` 不能跨卷。运行中的 exe 被锁时
  release 全量构建会卡在链接（`cannot open output file flowkeyd.exe: Permission denied`）→ 常规解法是
  `--quit`；实在停不掉时可以把被锁的 exe `Move-Item` 成 `*.locked`（内核映像按区域映射），但常驻实例
  跑的就是旧构建了，要告知用户。
* **提权实例被强杀会留下幽灵托盘图标**（explorer 不会马上发现进程没了）→ 优先 `--quit`。
* **`--quit` 的事件名按“传进去的配置路径字符串”命名** → 停一个开发实例必须用与启动时**同一个拼写**
  （脚本里一律传绝对路径）。
* **完整性级别的 “no write up”**：提权守护进程建的事件默认带 High 标签，非提权的 `--quit` 会吃
  `ERROR_ACCESS_DENIED` → 用 SDDL `D:(A;;GA;;;<用户 SID>)S:(ML;;NW;;;LW)` 把强制标签压到 **Low**
  （互斥体同理，为了让非提权进程能 `OpenMutexW`）。
* **`--check` 不带 `--config` 读的是真实配置**（`%USERPROFILE%\.config\flowkeyd\`）→ 验收/接管脚本里
  一律显式写 `--config`。
* **`QCoreApplication::applicationDirPath()` 在没有 `QApplication` 时会警告并返回空串** —— 它在
  `core::configPathCandidates()` / `legacyTomlCandidates()` 里被调用，而这两条都在 `QApplication`
  之前跑（离线命令与守护进程都是），于是**文档里写的“exe 同目录”候选实际是失效的**（`exeDir` 是空串，
  直接跳过）。这是个已知小缺陷，**没改**；要改就得给 core 一个不依赖 Qt 实例的 exe 目录来源
  （例如 `core::setExeDirectory()`，由 main 从平台层传进去）。

### 原生 shell 右键菜单（`platform/win/shell_menu`）

* **`TrackPopupMenuEx` 弹的是 shell 生成的菜单，所以菜单消息必须转给 `IContextMenu2/3`。**
  `QueryContextMenu` 做出来的 `HMENU` 里可能有子菜单与自绘条目，系统会把 `WM_INITMENUPOPUP` /
  `WM_DRAWITEM` / `WM_MEASUREITEM` / `WM_MENUCHAR` 发给**菜单拥有窗口**（我们的启动器卡片，一个
  **Qt 的**窗口）。不转发的话：子菜单展开是空的、图标不画、字母加速键不生效。做法是菜单开着的那一小段
  时间用 `SetWindowLongPtrW(GWLP_WNDPROC)` 临时换掉它的窗口过程，其余消息原样 `CallWindowProc` 回去。
  **别拿 `GWLP_USERDATA` 存自己那份 router**：那是 Qt 的窗口，不该动它留给自己的字段；弹菜单这个调用
  本身是阻塞的，一条线程同时只有一份，一份 `thread_local` 就够。
* **命令号是「偏移」，不是绝对 id。** `QueryContextMenu(hmenu, 0, idCmdFirst=1, idCmdLast=0x7FFF, …)`
  之后，`TrackPopupMenuEx(… TPM_RETURNCMD | TPM_NONOTIFY)` 返回的号减掉 `idCmdFirst` 才是
  `CMINVOKECOMMANDINFOEX::lpVerb`（用 `MAKEINTRESOURCEA(offset)`、配 `CMIC_MASK_UNICODE` 再填一份
  `lpVerbW`）。返回 0 = 用户取消。
* **想要「以管理员身份运行」就得加 `CMF_EXTENDEDVERBS`。** 那是资源管理器里按住 Shift 右键才多出来的
  那批「扩展动词」。
* **坐标必须是物理像素。** QML 那边（`Item.mapToGlobal`）给的是设备无关的**逻辑**像素，直接丢给
  `TrackPopupMenuEx` 会偏 → 平台层用 `GetCursorPos` 自己取（也顺便解决多屏 / 混合缩放的换算）。
* **菜单的拥有窗口必须是前台窗口**（否则“点菜单外面就关掉”失效）：调用前
  `SetForegroundWindow(owner)`，回来之后补一条 `PostMessageW(owner, WM_NULL, 0, 0)`（MSDN 那套收尾，
  不然菜单可能还挂在屏幕上）。
* **不要在 shell 菜单外面套 DPI 上下文。** 曾经照「宿主 shell 菜单」的建议把当前线程临时降级成
  `DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED`（`SetThreadDpiAwarenessContext`），结果菜单**高一倍**：
  shell 给 owner-draw 条目量尺寸用的是**拥有窗口的 DPI**（启动器卡片 PMv2、192），而那个上下文又把整张
  菜单按 96→192 缩放一遍 —— 2 倍乘 2 倍，条目从 22 逻辑像素变成 44。本进程（Qt 6）本来就是 Per-Monitor
  V2，**win32k 对 PMv2 线程弹的菜单按显示器逐个缩放**（正是资源管理器自己那份的尺寸），什么都不用做。
  * 2026-10 真机实测（Windows 11 26200、200% 缩放、`shell:AppsFolder\Chrome` 那份 8 条菜单）：
    PMv2 线程 + PMv2 拥有窗口 = 386x327 物理、每条 44 物理（**正确**）；GDISCALED 线程 + PMv2 拥有窗口
    = 520x654、每条 88 物理（**两倍**）；GDISCALED / UNAWARE 线程 + 一个 unaware 拥有窗口 = 条目 44
    物理，但被 GDI 缩放后的字会顶到 76 物理，**高度还不一致**。
  * 测量手法：`tmp/dpimenu/`（一次性探针：自建拥有窗口 + `TrackPopupMenuEx` + 一条 watcher 线程
    `EnumThreadWindows` 找 `#32768` 量矩形，量完就 `ExitProcess`）。**锁屏时也能量**：向菜单窗口
    `PostMessage(WM_KEYDOWN, VK_ESCAPE)` 就能收掉它（`TrackPopupMenuEx` 返回 0），不需要注入键盘。
  * 回归守卫：`tmp/preview/` 的 `checkAppsMenu()`（真卡片 + 真 shell 菜单，用 `MN_GETHMENU`(0x01E1)
    从菜单窗口拿回自己的 `HMENU`、逐条 `GetMenuItemRect`）断言「每条约 22 逻辑像素」；
    `scripts/acceptance.ps1` 那边只能看菜单窗口的矩形，所以靠守护进程日志里的
    `shell menu: N item(s)` 换算每条多少逻辑像素。
* **“属性”这类命令开出来的对话框以 `hwndOwner`（我们的卡片）为属主** → 收卡片必须发生在
  `InvokeCommand` **之前**（本项目就是用 `showItemMenu()` 的 `beforeInvoke` 回调做的），否则
  `setProperty("visible", false)` 会把对话框一起藏掉。
* `IContextMenu::InvokeCommand` 里的“属性”/“打开方式”是**在调用方线程上跑模态循环**的
  （`PropertySheet`），所以这个调用可能阻塞 GUI 线程到用户关掉那个对话框为止；弹菜单本身也是阻塞的
  （`TrackPopupMenuEx` 的模态循环）。这期间 Qt 的事件队列不跑（Win32 消息照样派发），所以别指望那时
  还有定时器 / 队列回调生效。
* **锁屏 / 会话没真正接收输入时，注入测试全是假的**：`SendInput` 会**返回成功**（`GetLastError` 也是
  陈旧的），但鼠标不会动、窗口拿不到前台、`CopyFromScreen` 报“句柄无效”。判别方法（不需要人看
  屏幕）：`GetForegroundWindow() == 0`，或者注入一次 1 px 的 `MOUSEEVENTF_MOVE` 再 `GetCursorPos`
  看它有没有真的动。遇到过“会话是 `WTSActive`、`SM_REMOTESESSION=1`、`SendInput` 也说成功，但输入不
  落地”的 RDP 会话 —— 那时不要在生产代码里找原因。

### 测试 / 验证手法

* **“物理按键”可以自动化，但必须先加一个测试后门**：钩子照规矩丢弃一切带 `LLKHF_INJECTED` 的事件，
  于是 `SendInput` 伪造不了用户按键 → `FLOWKEYD_ACCEPT_INJECTED=1` 抬升那道过滤，而且**只改钩子给
  `event.injected` 赋值的那一步**。启用时打一条警告。
* **“只有显示器配置变了才会出现”的弹窗问题可以在进程内复现**：把那个（隐藏的）弹窗窗口 `SetWindowPos`
  成「逻辑尺寸当作物理像素」的大小（就它停在旧缩放上的样子），再正常弹出一次 —— 产品会发现尺寸不对、
  丢掉重建（`tmp/preview/` 的 `checkStalePopupScale()`，锁屏也能跑）。断言时**给旧窗口打个
  `setProperty` 标记**：标记不在了才能证明窗口真的换了一个，否则分不清是重建了还是 Qt 自己把尺寸改了
  回来。
* **`$form.Activate()` 会静默失败**：Windows 的前台锁只允许“当前就在前台的那个进程”抢焦点。验收脚本
  要照抄守护进程自己的 `raiseWindow`（`AttachThreadInput` 到当前前台线程 → `SetForegroundWindow` →
  解挂），而且真抢不到焦点时要**明确报一条失败**。弹窗复用之后 `window->isActive()` 可能是陈旧的
  `true`（`activateWindow` 会早退）→ 再问一句 `GetForegroundWindow() == hwnd`。
* **`Win+F16` 在本机是外壳快捷键**（会拉出“滑动以关机”）。选测试和弦之前，先用一个不装钩子的小脚本探
  一下它会不会动外壳。本项目用 `Win+S`（失败代价只是弹个搜索框）。
* **注入鼠标的脚本里，凡是用绝对坐标就必须先把光标放到一个确定的起点**（多显示器下尤其如此）：
  弹窗“跟着光标走”是对的 UX，错的是“坐标算一次就管到底”的测试写法（`FocusCatcher` 里
  `[FlowInject]::Cursor(200, 200)`）。
* **要验证弹窗链路又不想真的按键盘**：做一个**临时预览程序**（`tmp/preview/`：自己的
  `CMakeLists.txt` + `main.cpp`，`file(GLOB)` 拉进 `src/core`、`src/platform/win`、模型与
  `popup_host.cpp`，再用 `qt_add_qml_module` 注册同样的模块）：直接调
  `PopupHost::requestMenu/requestHelp`，用 `QCoreApplication::sendEvent(window, &QKeyEvent(...))`
  模拟键盘，再用 `QScreen::grabWindow` 截图。**这条路径能验证除“真实按键”之外的一切**，而且锁屏时
  也能跑。（`tmp/` 在 `.gitignore` 里，重做一次大概十分钟。）注意预览工具自己会报一堆
  `ItemDelegate.qml` 的 `TypeError`（用官方 Qt 跑也一样），那是**预览工具的环境问题**，产品日志里是
  0 条。
* **这套预览工具是验“分段 + 多种行形状”界面的主力**：它里面的 `checkApps()` 会真的扫开始菜单
  （真机 100 个程序）、真的让异步图标回来、用 `QTest::keyClick(window, …)` 走 `Space` / `End` /
  `Enter` / `Esc`，而且**断言的是模型状态与窗口几何**（`pinnedCount()` / `allMode()` /
  `cardHeight()` / 标题里的个数），截图只是给人眼看的那一眼。它需要的方法变了就直接改它（它是 `tmp/`
  里的一次性工具）；改不动就照它的形状重做一份，比“跑不了验收脚本就交”稳。**它跑在进程内**，
  锁屏 / RDP 不接收输入时照样能跑（产品弹窗的抓图靠 `QQuickWindow::grabWindow()`，不碰屏幕）。
* **“一闪而过”的 bug 怎么查**：用 `Graphics.CopyFromScreen` 抓弹窗那块的屏幕区域
  （`QQuickWindow::grabWindow()` 看不到合成器的中间帧），每帧算一个 FNV 指纹，~120 fps，打印“与上一帧
  不同”的帧号；把抓到的帧存成 PNG 用 `imgdiff` 打印“不同像素数 + 包围盒”。注入 `MOUSEEVENTF_WHEEL`
  之后**再注入 1 px 的 `MOUSEEVENTF_MOVE`**（真鼠标滚轮几乎总会带一点位移）；而且**弹窗必须是前台
  窗口**（背景窗口收不到 `WM_MOUSEWHEEL`）。
* **「滚轮真的滚了没有」从外面只有一个可观察量：像素。** 验收脚本里
  `[FlowInject]::RegionHash(x, y, w, h)`（FNV-1a 扫一块屏幕区域）只抓列表左侧一条竖带、前后比对：
  避开右边的滚动条（它的淡入淡出自己会变）与光标那一列（`ItemDelegate` 的悬停高亮跟着光标走）。锁屏时
  `CopyFromScreen` 会抛，`RegionHash` 吃掉异常返回 0（前后必然相等 → 检查报失败，那种会话本来就不该
  跑验收）。
* **锁屏时“看渲染”只剩一条路**：`QScreen::grabWindow()` / `CopyFromScreen` 会报“句柄无效”（拿到的是
  黑图），但 **`QQuickWindow::grabWindow()`（进程内渲染）照常能用**，`QTest::keyClick(window, …)` 的
  进程内注入也照常能用 —— `tmp/preview/` 那套就靠这两条在锁屏时验证弹窗（它抓到过上面那条
  `Q_PROPERTY` 的坑）。注意锁屏时委托是**逐帧惰性创建**的，而且推不推帧说不准，所以“到底建了多少个
  格”这种断言不要写死（只断言“至少有几个”）。
* **从外面看见托盘图标变了**：`SetCursorPos` + 一次**相对**的 `mouse_event(MOUSEEVENTF_MOVE)` 把光标
  推到屏幕边缘，自动隐藏的任务栏才会滑出来；然后 `CopyFromScreen` 整条任务栏，像素差求包围盒。新起的
  实例图标会落进「隐藏的图标」溢出弹窗里，要看真实效果就重启常驻实例。
* **本机任务栏是自动隐藏的竖条**（`Shell_TrayWnd` 是 `0,0,96,2160`），抓图坐标要与这个对齐。
* **需要真实桌面的验证要做成“默认 skip 的交互式单测”**（`tst_interactive` +
  `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）：这比写一个只跑一次的临时程序好，能反复验证剪贴板/音量/
  窗口后端，而且不会在 CI 里碰用户桌面。
* **本机的记事本变成了带标签页、单实例、带会话恢复的 Store 应用**：`WM_CLOSE` 会弹确认框、给文件参数
  只是加标签页、标题可能停在别的文件上 → “窗口后端”的断言改用**测试进程自己的**顶层窗口
  （`tests/tst_interactive.cpp` 里的 `TestWindow`：自己 `RegisterClassExW` + `CreateWindowExW`，标题
  可控、进程独占、`WM_CLOSE` 就是 `DestroyWindow`；等消息用只抽自己窗口消息的 `pump()`）。记事本只剩
  两个用途：覆盖“启动一个真程序 + 按标题找到它的窗口”，以及需要一个真能 Ctrl+C 的编辑器。
* **本地假 GitHub 是验证在线更新最好用的手法**（`tmp/e2e-local.ps1` + `tmp/e2e-http-server.ps1`，
  临时改 `latestReleaseApiUrl()` 指向它，测完就删）：用裸 `TcpListener`（`HttpListener` 要 URL ACL）
  发一份 `releases/latest` 的 JSON 和一个装着自家新 exe 的 slim zip，让一个从 `build/dist-release` 拷
  出来的沙箱实例自己走完「检查 → 下载 → sha256 → 解压 → 换名 → 重启」。沙箱 exe 要用**带临时钩子**的
  那份（否则没人点菜单，它不会自己检查），zip 里的“新版本”要用**不带钩子**的那份（否则重启后会再检查
  一次、无限循环）。同样的手法也能验 `install.ps1`（把 `$ApiLatest`/`$DownloadBase` 改成本地地址，
  返回一个不存在的版本，于是脚本完整走完「取版本 → 下载 → 失败」而不真装）。
* **`--quit` 成功与否别只看退出码，也别只看那一瞬间的进程表**（常驻已经开始关闭时 `Get-Process` 仍
  可能看得到它）：可靠的判据是 stdio 里那句 `flowkeyd: <path> exited` 加稍后为空。收尾脚本要**轮询**
  等它真的消失，还有残留就 abort（不构建也不重启）。
* **`Start-Process` 起的后台进程会把继承的 stdout/stderr 一直握着**，让调用方一直等 → 起后台服务就把
  两个流都重定向到文件。
* **发布后怎么核对**（2026-10-19 那次：`scripts\release.ps1 -Push` 一次过，`100% tests passed, 0 tests
  failed out of 34`，tag `v26-10-19-9f9fbca`）：`gh release view <tag> --json
  tagName,isDraft,isPrerelease,assets --jq …` 看四个资产都在 `uploaded` 状态；正文要另想办法看，**别用
  PowerShell 的 `>` 抓**：`gh`/`gh api` 的 UTF-8 stdout 会先按控制台代码页（GBK）解码、再被 `>` 写成
  **UTF-16**，中文一律变成 `�?`，看起来像「发布页上的正文是乱码」——其实远端是好的（用 HTTP 取
  `api.github.com/repos/<owner>/<repo>/releases/tags/<tag>` 的 `body` 才是逐字节正确的，2026-10-19 验证
  过）。同理，收尾脚本里判断「发布成功」靠的是脚本自己的退出码 + 上面那 4 个资产。
* **`--check` 之类的输出中文经 agent 的 bash 抓回来是乱码**（控制台代码页）→ 验收脚本把窗口标题与前台
  窗口的类名/码点写进 `-WorkDir\diag.txt`（UTF-8、无 BOM）当诊断，要读结果就用 read 工具读那个文件。
* **窗口切换器的行为只能靠验收脚本验证**（`PopupHost` 要真 QML 窗口，单测碰不到）：用**注入的「轻碰
  Win」+ 窗口标题前缀**这套组合从外面看。两个容易踩的点：（1）触发发生在 **Win 键松开时**，所以“第二
  次轻碰”进来时卡片已经开着；（2）遮断标记要是没生效，外壳会弹开始菜单、卡片会在 300 ms 内自己关掉
  —— 所以“卡片还在前台”要**再等半秒**再断言，只看“它出现过”会放过这种失败。

---

## 11. 完成定义（DoD）细则

一个任务算完成，必须同时满足：

1. `cmake --build --preset debug` 与 `--preset release` **都绿**（零新增警告；warning 当错误处理，
   直到项目所有者另有要求）。
2. `ctest --test-dir build/windows-debug --output-on-failure` **全绿**（测试目标只在 debug profile 里
   构建）；新增/修改的逻辑都有对应测试（`core/`、`lua/`、模型层这些可测的部分）。
3. 如果动了钩子/引擎/分发/窗口后端：跑 `scripts\acceptance.ps1`（只跑 release 那一份），把结论写进
   本文件。动到脚本覆盖不到的界面（托盘菜单、日志窗口、`animate`）时，仍然要人眼过一遍。
4. **测试绝不执行真实的系统电源动作**（工作约定第 10 条）。
5. `flowkeyd --check --config flowkeyd.lua.example` 通过（零警告）。
6. 用户可见行为有变化时更新 `README.md`，有新经验时更新本文件；**新增 / 修改 / 删除可配置项
   （字段、取值、默认值）时按工作约定第 13 条把 README、`core/config_map.*` 与 QML 界面三处
   一起改**，并在提交信息里注明对已有配置的兼容性。
7. `git commit`：提交信息里说明**为什么**（尤其是引入新依赖时）。
8. 仓库里不留垃圾：`tmp/`、`build/`、临时配置文件都在 `.gitignore` 里。
   **发布目录 `build/dist-release/` 里只允许有 `flowkeyd.exe` 与它需要的运行时**。
9. **保证 `build/dist-release/` 是最新发布包**（工作约定第 11 条）——这是「用户日常按的快捷键真的跑在
   新构建上」的唯一保证。

---

## 12. 本期不做的（有意留白）与后续工作

1. **`--simulate <SCRIPT>`**（把脚本化按键事件重放给真正的引擎）。这是最便宜的引擎验证手段，
   **强烈建议尽早补**：不需要焦点、不装钩子，却能把匹配/吞键/重复/挂起/重映射全跑一遍。
2. **`--selftest` / `--probe`**（各平台后端探测与自检）：Core Audio 的 COM vtable、虚拟桌面接口表、
   未公开 API 的可用性这些只能靠真实调用验证的东西，会有一条确定的、幂等的检查路径。
3. **完整的 `scripts/e2e.ps1`**。`scripts/acceptance.ps1` 已经是它的第一版；还缺动画的屏幕采样、
   托盘菜单点击、自提权的 UAC 流程，以及日志窗口那一套的外部断言。扩展时沿着现在这套脚本走：
   断言字符串（英文日志、窗口标题格式）是稳定的，正是为了这个。
4. **鼠标钩子**（`WH_MOUSE_LL`）。
5. **延迟修饰键抑制**：让 `Ctrl+Alt+H` 也隐藏 Ctrl 和 Alt（相对 AutoHotkey 唯一真正的行为差距）。
6. **配置文件热重载**（去抖的 `ReadDirectoryChangesW`）。
7. **按应用限定的快捷键**（等价于 AutoHotkey 的 `#If WinActive(...)`）。唯一的例外是第 2 节第 26 条的
   「远程桌面放行」：那只是一个明确的前台条件，做成全局策略 + 单条例外，而不是通用的按应用绑定。
8. **把 Lua 函数当动作**：刻意不做（声明式动作才能被 `--list` 显示、在加载时校验完，并在钩子/工作
   线程边界上保持安全）。
9. **配置里的 `require`/模块支持**（现在只有一份脚本）。
10. **`NumLock` 关闭时小键盘的导航键与主键盘同名键的区分**（做法可以照抄小键盘 Enter 的伪码表）。
    注意这是**行为变化**：`keys = "Up"` 将不再匹配小键盘的 `8`。
11. **弹窗的条目图标**与更细的动画。
12. **帮助窗口的模糊搜索、按 `comment` 分组**。
13. **日志窗口的增强**：`--follow`/`--grep` 之类的参数、把 `INFO` 与 `DEBUG` 分色渲染。
14. **托盘图标跟随 explorer 重启**（处理 `TaskbarCreated`）。
15. **把发布包再缩到更小**：再往下（静态链 Qt、把 Qt 自己的 QML 模块也编进 exe、单文件自解压）要换
    一套 Qt 构建或引入新的打包机制，为了几十 MB 不划算。
16. **程序启动器的打磨**：数据源已经三份了，卸载程序也已经滤掉，剩下的就是各类工具项
    （「【小狼毫】输入法设定」这种）。可做的：按名字/目录过滤掉工具项（需要给 `apps()` 加参数与校验，
    或者加一张可配置的排除表）、最近使用优先排序、按开始菜单子目录分组、把图标缓存到磁盘。
17. **程序启动器右键菜单里的命令仍然提权**：它们由 shell 在 flowkeyd 自己的进程里执行（第 2 节
    第 35 条）。要改成普通权限就得把整个菜单宿主（`IContextMenu` 的消息转发 + `#32768` 菜单窗口
    的尺寸/坐标、`beforeInvoke` 收卡片）搬到一个代理进程里 —— 代价与风险都不划算。

---

## 13. 在哪里扩展

* **新动作类型**：`core/action.h` 加一个变体 → `Action::summary` 加摘要 → `app/dispatcher` 里处理它 →
  如果它带参数，加进 `core/config` 的 `validate_action`（顶层动作与 `menu` 条目共用它）→ 在
  `lua_prelude.lua` 里加构造器并挂进 `flowkeyd` 表（构造器如果是“原样返回用户表”的那种，**记得自己补
  `type`**）→ 写进 `README.md` 的动作表与 `flowkeyd.lua.example` → **在 `scripts/acceptance.ps1` 里加
  一条能自动验证的检查**。
  加了新动作（或新字段）还要记得同时接三处：`core::actionToMap()` / `core::actionFromMap()`（界面用的
  字典）与 `core::actionKindNames()` / `core::defaultActionMap()`，再在 `ActionEditor.qml` 里加一段
  `visible: root.type === "…"` 的表单（`menu` 的 `items` 与 `window` 的 `launch` 是现成的两个例子），
  并在 `ConfigText.qml` 的 `action` 表里给它一个中文名。忘了接界面那一侧不会报错，只是“这一种动作在
  界面里改不了”（字段会被默默丢掉）。
* **新的选单条目字段**（例如图标）：`config` 加字段 → `MenuModel` 与 `MenuPopup.qml` 里画出来 → 校验
  （重名、空标签）→ README 表格。条目是在委托的 `contentItem` 里用锚点摆的，**不要再把行几何（矩形）
  往模型里塞**。
* **帮助窗口的新内容或新交互**：条目在 `app/dispatcher` 的 `openHelpAction()` 里从 `Compiled` 的
  `bindings`/`remaps` 生成（与 `--list` 看同一批数据，所以 `help` 没有配置参数），纯逻辑在
  `HelpModel`，界面在 `HelpPopup.qml`。模型层与 `core` 之间现在只靠一个 `destructive` 布尔量连着，
  要让一行能执行动作就在 `openHelpAction()` 的 `targets` 里给它一个条目（绑定用 `press`+`release`，
  `remap` 用两串 `SendOp`），**不要往 `HelpEntry` 里塞 `core::Action`**。新加模型角色名时注意别和标准
  控件自己的属性撞名。
* **窗口切换器的新行为**：纯逻辑在 `WindowListModel`（筛选/选中/按键决定，`tst_window_list_model`
  直接测），行里画什么在 `SwitchPopup.qml`，**窗口本身的开/关在 `PopupHost::showSwitch()`**（它只能
  看到窗口、看不到按键，所以像「再按一次同一个快捷键 = 关掉」这类判断要放在那里，而且要想清楚「触发
  发生在按键的哪一半」——「轻碰 Win」是**松开**时触发的）。每加一条可从外面观察到的行为，就在
  `scripts/acceptance.ps1` 的「窗口切换器」那一段加一条检查：那是这条路径唯一的自动化覆盖。
* **程序启动器的新行为**：分层看 —— `core/app_list.*`（“算不算程序”与“算不算卸载程序”、去重/排序、
  名字匹配、图标键、取图标的名字、跨来源合并、排序键与分组表头、内置的 `ms-settings:` 页面表）、
  `platform/win/apps.*`（两份扫描；**别把名字匹配往平台层堆**）、`core/launcher_state.*`（固定 /
  最近使用的顺序、截断与 JSON 解析序列化）、`app/app_list_model.*`（行式的三个视图与选中项/卡片高度、
  三种快速启动标识）、`app/app_icons.*`（异步图标的线程/缓存/尺寸）、卡片 `AppPopup.qml`。把条目喂给
  卡片、**启动那一步**（`apps::launchApp()`）以及状态文件路径都在 `Dispatcher::openAppsAction()`；
  真正读写 `launcher.json` 的是 `PopupHost`（`loadAppState()` / `saveAppState()`，由模型的
  `stateEdited` 信号驱动）。改完要跑 `tst_app_list` / `tst_app_list_model` / `tst_launcher_state`；
  能从外面观察到的行为就在 `scripts/acceptance.ps1` 的「程序启动器」那一段加一条检查（**注意别在网格
  里按 `Enter`**，那会真的启动一个程序；`End` + `Enter` 落在「全部程序」按钮上是安全的）；纯粹的行
  几何 / 卡片外观用 `tmp/preview/` 那套抓图看。滚动这类“列表内容动没动”的检查有两层：
  `tmp/preview/` 里直接断言 `ListView.contentY`（真 QML + 真模型，锁屏也能跑）；从外面看只有像素，
  所以 `scripts/acceptance.ps1` 用 `RegionHash` 抓一条竖带前后比对。
  **快速启动键**（筛选号码、`Alt` + 数字、`Alt` + 功能键）只在模型里算：加一种就动 `rebuildRows()`
  里的 `m_itemKeys` / `m_recentKeys` / `m_pinKeys`、标签函数（`digitLabelForLine()` /
  `recentKeyLabelForLine()` / `pinKeyLabelForLine()`）、`itemModels()` 的优先级、`handleKey()` 的
  一条分支，以及 `footerText()` 的一句提示 —— 别把它做成 QML 里的按键表。**新的一行形状**：模型加一个
  `Row::Kind`、`data()` 的 `RowKindRole` 多一个字符串、`AppPopup.qml` 多一个内联组件（并加进
  `preload()` 的假数据好让它被预热）。
* **程序启动器的过滤与身份**：凡是「某个东西算不算程序」「某个程序算不算卸载程序」的判据都写在
  `core/app_list.*`（纯逻辑，`tst_app_list` 直接测）；**不要往平台层里堆名字匹配**。「哪些条目进列表」
  目前只有一个开关：`platform/win/apps.cpp` 里那两次调用。要做成可配置（例如给 `apps()` 加一张排除表）
  就得同步改 `core/config` 的校验、README 的 schema 表与示例配置 —— 那是对外稳定接口，当破坏性变更
  对待。
* **拼音表 / 匹配语义**：表是生成的（`tools/pinyin_gen.ps1` → `src/core/pinyin_data.*`），**别手改
  生成物**；要改覆盖范围（例如把 Ext A 也纳入）就改脚本再跑一遍，`pinyin_data.h` 里的区间常量要跟着
  改。匹配语义（三段、组合数上限、哪些字符进首字母、拉丁词的首字母怎么算）全在 `core::appSearchText()`，
  单测是 `tst_app_list`（表完整性也在那里）；模型侧只是把那个串算一次。
* **新的 QML 弹窗**：`import QtQuick.Controls.FluentWinUI3`；颜色一律从 `palette`
  （`base`/`text`/`placeholderText`/`highlight`/`highlightedText`/`alternateBase`/`mid`）取；字号用现在
  这套 `pointSize`（12.5 标题 / 11 正文 / 10.5 帮助正文 / 9 副标题与徽标 / 8.5 细节）；**中文一律
  `font.family: "Microsoft YaHei"`**；需要滚动的列表用真正的 `ListView` + `ScrollBar`；固定表头/底部
  提示看 `HelpPopup.qml` 的“`topMargin`/`bottomMargin` + 不透明底色”三件套；**避开 FluentWinUI3 不
  支持的那些控件**；文件开头写 `pragma ComponentBehavior: Bound`，并用 `qmllint -I …` 确认零警告；
  别忘了加进 `PopupHost::preload()` 与 `flags: … | Qt.Tool`。
* **给弹窗/日志窗口加新的 QML `import` 时，先看 `cmake/PruneRuntime.cmake` 的保留名单**：用了别的东西
  （比如 `QtQuick.Dialogs`）就要从清单里拿掉对应的删除项，否则弹窗在启动预热那一步就会报
  `could not load ….qml: module "…" is not installed`（`scripts/acceptance.ps1` 有一条哨兵检查盯着；
  开发期跑 `build/windows-debug` 也会报）。改了清单就会自动重新链接（`LINK_DEPENDS`），不用手动清
  构建目录。
* **新的电源操作**：`PowerOp` 加变体 → `platform/win/power` 里处理（需要特权的先调
  `enable_shutdown_privilege()`；不需要的要放在它**之前** return）→ `as_str` 与简写 → README 表格。
  **不给它加自动化测试。**
* **改配置模式（新字段/新取值）**：`core/config` 加字段 → 需要的话在 `lua_prelude.lua` 里加构造器 →
  `flowkeyd.lua.example` 里加一条（`--check` 会立刻告诉你它能不能过校验）→ README 表格。**字段名不要
  用 Lua 关键字**（`repeat`/`end`/`for`/`local`/`function`/`then`/`until`……）；需要的话给它一个 Lua
  友好的别名。
  同时记得接上图形界面那边：`core::settingsToMap()`/`settingsFromMap()`（或对应的条目转换函数）、
  `core::configToLua()` 如果新字段有“两种书写形状”（像 `remote_desktop`），再在 `ConfigGeneralPage` /
  `ConfigAdvancedPage` / 对应的弹层里加一行。`tst_config_writer` 的**整份配置往返**会把接错的地方直接
  暴露出来（它拿真 Lua 层读回自己写的东西，逐字段对比）。三处清单（含「界面漏接不报错」这条）见
  工作约定第 13 条；**README 那一处要在同一个任务里改完**，不要留到以后。
* **新的窗口摆放字段**：`core/config` 加字段 → `lua_config.cpp` 的 `convertWindowRule` 加白名单与读取
  → `lua_prelude.lua` 的 `window_rule` 注释 → `core/placement` 里影响几何/匹配（或只影响
  `summary()`）→ `app/dispatcher` 的 `placeWindowOnce` → README 的 `window_rule` 一节与示例 →
  `tst_placement`/`tst_config`/`tst_lua`。如果新字段要调新的 Win32/COM 后端，放在 `platform/win/*`
  里，并在 `tst_interactive` 里加一条真机验证。
* **新的 `monitor` 取值 / 新的显示器算法**：取值本身加在 `core/monitor_ref.h` 的 `Kind` 上
  （`lua_config.cpp` 的 `convertMonitorRef` 与 `selectMonitor()` / `describe()` /
  `monitorRefIsConcrete()` / `monitorRefIsDynamic()` 四处都要跟）→ 解析成“哪一块”的那一步只有两个
  地方：`dispatcher.cpp` 的 `resolveWindowMonitor()`（问系统 / 查记忆）与 `placeWindowOnce`（摆几何）；
  真要调新的 Win32/COM 后端就放给 `platform/win/monitor.*`（像 `indexForCursor` 那样）→ README 的
  「窗口摆到哪块显示器上」一节与示例配置 → `tst_placement`（纯逻辑）+ 需要真桌面的放 `tst_interactive`。
  **状况是 `Kind::None` 一定要单独处理**：它不是“摆到某一块”，而是“这次不碰位置”。
* **新的 app 字段**：`core/config.h` 的 `AppDef` 加字段 → `lua_config.cpp` 的 `convertApp` 加白名单与
  读取 → 在 `core/config.cpp` 的 `expandApps` 里决定怎么继承/忽略（纯逻辑，`tst_config` 直接测）→
  `lua_prelude.lua` 的 `app` 注释 → README 与示例 → `tst_config`（展开）/`tst_lua`（转换）。如果新字段
  是“可被动作局部覆盖的一整份值”（像 `launch`），照 `LaunchFields` 的做法把“写了哪些键”记下来。
* **新的窗口条件**：`core/window_match`（纯逻辑）+ `platform/win/window` 的枚举适配 +
  `launchThenActivate` 回退 + 手工冒烟清单里加一条用例。
* **「窗口在别的虚拟桌面上时怎么办」的其它规则**：决定这一步的是 `core::windowRulePinsDesktop()`
  （纯逻辑）+ `dispatcher.cpp` 的 `bringWindowToCurrentDesktop()`（两个调用点：`executeWindowAction`
  与 `launchThenActivate`）+ `platform/win/desktop::moveWindowToCurrentDesktop()`。要加第三种语义就照
  这三层拆，别把配置查询塞进平台层；纯逻辑那一层改完在 `tst_placement` 里加断言，真机行为在
  `tst_interactive` 里加一条（参照 `bringsAWindowOnAnotherDesktopToTheCurrentOne` 的窗口登记等待）。
* **启动器右键菜单的新行为**：平台层在 `platform/win/shell_menu.*`（要换条目只改 `QueryContextMenu`
  的 flags；某一条该不该出现 / 变灰是 `IContextMenu` 自己的事），「执行之前先干点什么」走
  `showItemMenu()` 的 `beforeInvoke` 回调，而「选完关不关卡片」这条规则在
  `AppListModel::afterContextMenu()`（`tst_app_list_model` 盯着）。能从外面观察到的行为就在
  `scripts/acceptance.ps1` 的「程序启动器」那一段加一条检查（**注意别在菜单里按 `Enter`**：那一格可能
  是「卸载」或「以管理员身份运行」）。**动到菜单尺寸 / DPI 时先看第 10 节那段**：不要给菜单套 DPI
  上下文（会把菜单放大一倍），改完在 `tmp/dpimenu/` 与 `tmp/preview/` 的 `checkAppsMenu()` 上量一遍
  （每条约 22 逻辑像素）。
* **新按键或别名**：扩展 `core/keys` 的键表并加一个往返用例（要有一个测试遍历表里的每个名字）。如果
  那个键要靠扩展标志才能与别的键区分（像小键盘的 Enter），还要在 `key_from_hook`/`native_key` 里加一条
  翻译。
* **新的 Windows 版本的虚拟桌面接口**：往 `platform/win/desktop` 的版本表里加一条（生效的
  `build.revision`、两个 IID、vtable 布局），然后在真机上确认选中的条目、桌面数量与序号。
* **新的虚拟桌面能力**：先在 `platform/win/desktop` 里手写那个接口的 vtable 结构体，字段下标以 VD.ahk /
  MScholtes 的实现为参考，并尽量用**已公开**的 API 做一次可验证的交叉检查（`GetID` 只认“有且只有一个
  匹配”；`PinView`/`UnpinView` 用 `IsViewPinned` 验证状态真的变了）。失败时只报错、不要去做可能是错的
  事。**先判断那个 IID 是否随版本变化**：变的（如 `IVirtualDesktopManagerInternal`）进版本表；不变的
  （如 `IVirtualDesktopPinnedApps`）只用一个常量。
* **新的未公开 API**：在 `platform/win/nt` 里用 `GetProcAddress` 解析，使用前先用一次无害调用校验，
  并永远保留一个已公开的回退。已公开但不在静态链接集合里的库走同一条路（`dwmapi`、`imm32` 是范例）。
* **远程桌面检测的新信号**（例如「本进程跑在远程会话里」的 `GetSystemMetrics(SM_REMOTESESSION)`）：
  纯逻辑放 `core/remote_desktop.*`，判定点只有 `platform/win/hook.cpp` 的 `updateRemoteDesktop()`
  （钩子线程），引擎只接一个 bool（`Engine::setRemoteDesktop()`）。加信号时记住三件事：名单要可配
  （`settings.remote_desktop`）、默认名单要窄、状态翻转时释放被放行的重映射按键由引擎负责。只加一个
  内置进程名时改 `builtinRemoteDesktopProcesses()` + README + 示例配置 + `tst_remote_desktop` 四处就够。
* **新的动作后端**：在 `src/platform/win/` 下新建模块，从 `dispatcher` 调用。
* **在线更新的新行为**（启动时自动检查、签名校验、把运行时也一并升级）：纯逻辑在
  `src/core/update_check.*`，状态与文案在 `src/app/update_model.*`，联网/下载/校验/落盘在
  `src/app/updater.*`，换文件与重启在 `src/platform/win/update.*`，卡片在 `UpdatePopup.qml`（按钮通过
  `PopupHost::updateXxx()` 转给 `Updater`）。改“去哪儿查”只改 `core::updateRepository()` /
  `latestReleaseApiUrl()`；新增一个状态就在 `UpdateModel::Phase`/`phaseName()`/`statusForPhase()`/
  几个 `canXxx()` 里各加一条（`tst_update_model` 盯着）。**不要在 `main` 里提前做替换**：换 exe 必须在
  运行时停干净之后。验证走 `tst_update`/`tst_update_model`/`tst_update_install`、`tst_interactive`
  的联网用例，以及第 10 节那个“本地假 GitHub”的沙箱 E2E。
* **换图标**：改仓库根目录的 `logo.svg`（唯一真源），然后
  `cmake --build --preset debug --target icons` 重新生成 `assets/flowkeyd.ico` 与
  `assets/icons/flowkeyd-<n>.png`（**两者都要提交**）；尺寸列表写在 `tools/icon_gen/main.cpp`
  （`kSizes`）、`CMakeLists.txt` 的 `qt_add_resources` 与 `src/app/app_icon.cpp` 里，**三处要一起改**。
  exe 图标走 windres、托盘图标走 qrc，两份产物用的是同一张源图。
* **新的日志窗口行为**：尾随逻辑在 `app/log_model`（纯逻辑、可单测），渲染在 `LogWindow.qml`。加命令行
  参数就改 `cli.cpp` 并更新 README 的命令行表格。
* **构建版本号的来源**：全部在 `src/core/version.*`；显示侧（托盘、启动日志、`--version`、`--help`）
  不用动。
* **发布包里新增/删除文件**：`scripts/release.ps1` 的 `$SlimFiles` / `$DependencyPatterns` 就是
  “哪个文件进哪个包”的**唯一清单**；`dist` 里出现两边都不认识的文件时发布脚本会直接失败。新的部署产物
  按“每次构建都会变吗”分类：会变的写进 `$SlimFiles`（进完整包与精简包），不变的（新加的运行时 dll /
  插件 / QML 模块）写进 `$DependencyPatterns`（只进完整包）。
* **发布说明的正文**：`scripts/release.ps1` 的 `Get-ChangeSummaryLines`（分类）/
  `Get-PreviousReleaseTag` / `Get-RepositorySlug` / `Write-GeneratedNotes`（拼起来 + 写文件）。分类靠
  **提交信息的前缀**，表就是函数上面的 `$script:NoteCategories`（要认新的前缀就加一行；不想逐条列出的
  前缀加进 `$script:NoteSkippedPrefixes`）。**它只读提交信息，不看 diff** —— 所以写提交信息时的前缀
  （`新增：` / `修复：` / `变更：` / `配置：`）就是发布页的分类依据。**正文只有 changelog**（用户明确
  要求，不要加回「下载哪个包」那一段）。

---

## 14. 配置 schema 速查

**这一节只是索引，权威定义在 `README.md`「配置」那一章与 `flowkeyd.lua.example`。**

| 项 | 值 |
| -- | -- |
| 配置目录 / 日志文件 | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log` |
| DSL 全局表 | `flowkeyd.*`（挂在脚本全局） |
| 示例配置 | `flowkeyd.lua.example` |

CLI 开关：`-c/--config`、`--no-elevate`、`--console`、`--elevated`、`--check`、`--list`、
`--list-keys`、`--quit`、`--no-autostart`、`--remove-autostart`、`--log-window`、`--log-level`、
`--log-file`、`--no-color`、`--allow-multi`、`--no-prompt`、`--updated-from`、`-h/--help`、
`-V/--version`。

### 容易做漏的语义

* `settings{}`：`log_level`、`swallow`、`exact_modifiers`、`release_modifiers`、
  `repeat_interval_ms`、`repeat_delay_ms`、`tick_ms`、`input_backend`、`single_instance`、`elevate`、
  `remote_desktop`、`window_monitor`。**未知键报错。** `window_monitor`（默认 `"cursor"`）= 没写明
  `monitor` 时摆到哪块屏（第 2 节第 33 条）。
* `hotkey{}`：`keys`（单个和弦或一组）、`name`、`trigger`（`press`/`release`/`repeat`）、`action`
  （别名 `press`、`on_press`）、`on_release`、`swallow`、`repeatable`（`true` 或
  `{ interval_ms, delay_ms }`；也认 `["repeat"]`）、`enabled`、`comment`、`remote_desktop`。
  `trigger = "repeat"` 与 `repeatable = true` 是同一件事；**互相矛盾的组合要被拒绝**。
* `remap{}`：`from`（键或和弦）、`to`（键名或发送脚本）、`mode`（`hold` 默认 / `tap`）、`swallow`、
  `name`、`enabled`、`remote_desktop`。
* `remote_desktop`（`settings` 一项 + 每个 `hotkey`/`remap` 一项）：见第 2 节第 26 条。
  `settings.remote_desktop` 是 `true` / `false`，或者 `{ enabled = …, processes = { … } }`；条目上的
  `remote_desktop = true` = 在远程桌面里也照常拦（默认 `false` = 放行）。
* `window_rule{}`：见第 2 节第 13 条；`monitor` 的六种取值（含 `"cursor"` / `"last"` / `"none"`）与
  缺省行为见第 2 节第 33 条。
* `app{}`：见第 2 节第 15 条；声明式写法叫 `apps`。
* 和弦语法：`~` 放行原始按键、`*` 忽略额外修饰键；`Numpad*` 与主键盘同名键不同。
  **和弦的结构是 `Modifiers + 一个按键`**：`keys = "NumpadSub+NumpadAdd"` 是语法错（`NumpadSub` 不是
  修饰键），守护进程会直接拒绝启动。
* **单个字母的键名必须小写**（第 2 节第 17 条）。
* 动作：`run`/`send`/`type`/`open`/`volume`/`media`/`clipboard`/`window`/`notify`/`menu`/`help`/
  `windows`/`apps`/`power`/`desktop`/`caps_lock`/`suspend`/`reload`/`quit`/`none`，字段逐条见 README
  的动作表。简写字符串：`"run:…"`、`"send:…"`、`"type:…"`、`"open:…"`、`"notify:t|b"`、
  `"volume:up"`、`"media:next"`、`"clipboard:get"`、`"window:minimize"`、`"desktop:1"`、
  `"power:sleep"`、裸关键字 `reload`/`quit`/`help`/`windows`/`apps`/`none`。
* **启动权限**（第 2 节第 35 条）：`run` 与 `launch`（`window` 的 / `app{}` 的，共用一份字段块）都有
  `elevated`，**默认 `false` = 当前登录用户的普通权限**；`open` 与程序启动器没有这个字段（它们总是
  普通权限）。
* **完全没有动作**的快捷键就是一个按键屏蔽器（会吞掉它匹配到的按键）。
* `window` 的 `toggle`（默认**开**）只对 `op = "activate"` 有意义；`launch` 回退不套用它；显式
  `toggle = false` 才关闭。“已经激活”要同时满足：前台、未最小化、**就在当前虚拟桌面上**。
* `window` 动作顶层的 `wait_ms` 是 `launch.wait_ms` 的简写；没有 `launch` 可覆盖时要报错。
* `window` 的 `animate`（默认**关**）只对会改变窗口状态的 `op` 有意义。
* `window` 的 `monitor` 只对 `op = "activate"` 有意义：显式指定这次把窗口摆到哪块屏（不写就走
  “命中的 `window_rule` 的 `monitor` → `settings.window_monitor`”）。写在其它 op 上会被 `--check`
  拒绝（第 2 节第 33 条）。
* `windows([title])` 是窗口切换器（第 2 节第 21/22/24 条）。常见绑法是 `keys = "LWin"` +
  `trigger = "release"`（「轻碰 Win」）。
* `apps([title])` 是程序启动器（第 2 节第 27/31 条）：列表来自**三份来源** —— **`shell:AppsFolder`**
  （= 开始菜单「所有应用」：含商店 / UWP 应用与 `.msc` 系统工具；文档 / 帮助 / 网址与**卸载程序**被滤
  掉）、**控制面板的「所有控制面板项」**、以及**内置的 26 个常用 `ms-settings:` 设置页**（名字带
  「设置：」前缀，`Settings: ` 是英文系统的写法）；启动、图标与右键菜单都交给 shell；筛选是**名字 /
  全拼 / 首字母的子串**（不是前缀，也不是模糊搜索）、筛到一个也**不**自动启动；**不切输入法**（与
  `windows` 相反）。拼音的细节见第 2 节第 29 条；**筛选之后前 10 条各带一个 `0`–`9` 的快速启动键**
  （没有对应条目的号码被吃掉、不漏进筛选框），**最近使用程序另有 `Alt` + 数字（`Alt+1`–`Alt+9`、
  `Alt+0`）、已固定程序另有 `Alt` + 功能键（`Alt+F1`–`Alt+F12`）的跳转键**（只在卡片里生效、不随筛选
  变化），见第 2 节第 30 条。

### 本机真实配置不进仓库

**不要在本仓库里记录某台机器上真实使用的配置文件内容**（绑定的清单、`--check` 的计数、配置文件的
路径、以及“本机现在跑的是哪一个 exe”）。仓库里只放产品文档与参考配置；真实配置是用户自己的东西，
**改了它不需要同步 README 或本文件**。要看当前绑定时直接看那份配置文件本身与 `flowkeyd --list`。

---

## 15. 待确认 / 需要用户拍板的事情

1. **示例配置要不要保留 `Win+X`/`Win+1..3` 这类会吞系统快捷键的例子？** 现在保留并加了醒目注释
   （说明“这会吞掉系统快捷键”），但这是产品口味问题。
2. **要不要做一份“英文日志 + 中文帮助”的文案约定表？** 目前只在工作约定第 4 条里写了原则。
