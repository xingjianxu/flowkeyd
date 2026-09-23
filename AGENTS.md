# AGENTS.md

给 AI agent（以及人类）的 **flowkeyd** 开发笔记。

先读这个文件。它记录了环境、真正能用的命令、**已经拍板的设计决策**、
不能破坏的不变量，以及已经让人浪费过时间一次的坑。

> **本文件是 flowkeyd 唯一的工程笔记，`README.md` 是唯一的用户文档。**
> 领域知识（键盘钩子的各种坑、窗口动作的前台锁绕行、虚拟桌面的接口表、
> 日志窗口的设计、提权的尾巴、`INPUT` 结构体布局……）都已经写进这两个文件，
> 不再有外部规格书。配置 schema、动作字段与键名的权威定义在 `README.md`
> 的「配置」那一章与 `flowkeyd.lua.example`。

> **本仓库的工作约定**（由项目所有者设定，2026-09）：
> 1. 每个任务结束后都要更新本文件，把新得到的经验和新出现的要求写进去，
>    这样下一个 agent 不必重新发现一遍。
> 2. 每个任务结束后**编译 debug 与 release 两个 profile**（两条优化路径的警告
>    都要挡住）。**单元测试只在 debug profile 上构建和运行**
>    （`ctest --test-dir build/windows-debug`，见第 5 节的命令行）；
>    `scripts/acceptance.ps1` 仍然只用 release 的产物（它跑的就是发布出去的那个
>    exe）。**release 构建 + debug 测试全绿才算完成**；纯文档任务同样适用。
>
>    两条 profile 的分工是固定的：**debug = 开发**（构建全部测试目标、部署好
>    运行时、可以随便跑），**release = 发布**（不构建任何测试目标）。
>    **发布版本是 `build/dist-release/`**（release 构建时自动产出）：里面只有
>    `flowkeyd.exe` 与它需要的 Qt/MinGW 运行时，**直接拷到别的机器上就能跑**。
>    `build/windows-debug` 里出现 `tst_*.exe` 是正常的，
>    `build/windows-release` 或 `build/dist-release` 里出现测试产物、构建系统文件
>    （`*.a`、`CMakeCache.txt`、临时脚本……）就算 bug。
> 3. 目标平台是 **Windows**；可以使用未公开的 Win32 API。
> 4. **代码注释、本文件、README 与示例配置一律用中文。**
>    **日志与错误信息保持英文**（配置校验信息、`--check` 输出也一样）：
>    理由是日志/校验信息是机器可断言的字符串，
>    而且中英混排的日志在终端里很难读。用户可见的 CLI 帮助文本用中文。
> 5. **可以自行结束本项目（flowkeyd）正在运行的进程**，不必事先征求许可；
>    重建被锁住的 exe、跑手工冒烟验证时先自己找 pid 再停。
>    停法：`taskkill /PID <pid>`（**不带** `/F`）让它走干净退出路径；
>    `/F` 只在进程提权而当前 shell 杀不掉时兜底（代价是留下幽灵托盘图标）。
>    **这条只管 flowkeyd 自己；见第 9 条。**
> 6. **需要用户暂时别用这台机器时**（会抢前台/焦点、会注入按键的手工冒烟验证），
>    用提问的形式提醒，但**不等回答**（例如“接下来约 N 分钟会注入按键/抢焦点，
>    请先别碰键盘和鼠标”），约 3 秒后自行继续。提醒里要写清预计时长与影响范围。
> 7. **依赖按需引入，但每次引入都要在提交信息里给出理由**。
>    默认只有 **Qt + `vendor/lua`**，见第 3 节的依赖政策。
> 8. **agent 的 bash 是一个极简 WSL，`/bin` 下只有 `bash`/`mount`/`login`，
>    `ls`/`grep`/`cat`/`head`/`tail` 全都没有。** 看文件用 read/edit 工具，
>    跑命令一律走 `powershell.exe -NoProfile -Command "…"`（或绝对路径的外部程序，
>    例如 `C:\Users\xingjian\scoop\apps\git\current\cmd\git.exe`）。
>    **不要把管道默写成 `| tail`。命令行上也不要直接拼中文**
>    （会因代码页变乱码）：要么写成脚本文件，要么只用 ASCII 的模式串。
> 9. **接管已经完成（2026-09，第 10 阶段）：现在该常驻的是 flowkeyd。**
>    用户日常绑定的提供者就是它，所以**不要**再去拉起别的实现。
>    2026-09 起常驻实例由**计划任务** `flowkeyd`（登录时 + 最高权限 + 15 秒延迟）
>    拉起，任务指向**当前正在运行的 `flowkeyd.exe` 路径**：守护进程每次启动都会
>    检查并（重新）注册它（见第 2 节第 10 条、第 4 节的 `autostart` 与第 10 节）。
>    停止用 `flowkeyd.exe --quit`（不要 `taskkill /F`，会留幽灵托盘图标）；
>    删除自启用 `flowkeyd.exe --remove-autostart`（先 `--quit`）。
>    **`scripts\install.ps1` / `scripts\uninstall.ps1` 已经删除** —— 自启的注册、
>    刷新与删除现在全在程序自己身上，**不要**再去写部署脚本、也不要再把常驻指向
>    某个预设目录（如 `C:\Program Files\flowkeyd`）：任务跟着 exe 走。
>    细节见第 10 节与 `README.md` 的「开机自启与更新」。
>
>    **注意（2026-09 实测）**：这台机器上 agent 的 `powershell.exe` **是提权的**
>    （`WindowsPrincipal.IsInRole(Administrator)` 为真），所以它能注册/启停那个
>    「最高权限」任务。但**不要**假定它永远如此：写脚本/验证时先用
>    `Assert-Admin` 之类的检查站稳，再动手。
>
>    开发期起 flowkeyd 一律用 `--no-elevate --allow-multi` + 一次性配置，
>    并且**不要**占用用户真实配置里已经有的和弦（`Win+S`、`Win+1..3`、`Win+W`、
>    `Win+X`、`Win+/`、`CapsLock`、`Alt+H/J/K/L`、`Alt+Space`、`LWin+Q`、
>    `LWin+F1..F4`、小键盘 `-`/`+`/`*`、`Ctrl+Alt+F4/F5/F12`）——
>    用户随时可能在用它们。`scripts/acceptance.ps1` 用的是一次性配置，符合这一条。
> 10. **任何自动化测试都不得触发真实的系统电源动作。**
>     `shutdown`/`restart`/`logoff`/`sleep`/`hibernate`/`lock`/`screen_off`
>     一个都不许真的执行 —— 测试代码里不出现 `platform::win::power::execute()`
>     （`tst_power_table` 只测纯逻辑表，不碰真实调用）。这些动作只有用户自己按
>     快捷键、或点选单条目时才允许发生。细节见第 5 节与第 11 节。
> 11. **每个任务收尾时，release 产物必须是最新的**（项目所有者 2026-09 要求）：
>    常驻实例必须跑在最新构建上，否则用户按快捷键用的还是旧行为。
>    **`cmake --build --preset release` 会自动把干净的发布包写到
>    `build/dist-release/`**（只有 `flowkeyd.exe` 与它需要的 Qt/MinGW 运行时，
>    不含任何构建系统文件或测试产物）。**不再需要往 `C:\Program Files\flowkeyd`
>    之类的安装目录部署任何东西** —— 那是旧方案，`install.ps1` 已经删掉；
>    新的自启任务指向的就是当前运行的 exe。
>
>    因为常驻实例会锁住 `build/dist-release/flowkeyd.exe`，收尾（在两条 profile
>    都构建通过、debug 测试全绿之后）按这个顺序做：
>
>    ```powershell
>    # 1) 停常驻（快捷键会失灵几秒）2) 构建 release 3) 从 dist-release 重新拉起
>    #    重新拉起时守护进程会自动把计划任务刷新成这个路径
>    & build\dist-release\flowkeyd.exe --quit
>    & 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
>    Start-Process build\dist-release\flowkeyd.exe
>    ```
>
>    * 只有在常驻实例确实是从 `build/dist-release` 跑的时候才需要先 `--quit`。
>      它跑在别的路径（用户自己拷走的拷贝）时，构建不会被锁，**不要**去动用户的
>      实例，只要保证 `build/dist-release` 是最新的就行。
>    * 纯文档任务通常 `ninja: no work to do`：什么都不用做。
>    * 需要管理员权限的只有**提权启动**（新实例会注册计划任务），本机 agent 的
>      shell 是提权的，实测能直接跑。

---

## 1. 这个项目是什么

`flowkeyd` 是一个由 **Lua 脚本**配置的 Windows 键盘钩子守护进程，
用 **C++20 + Qt 6** 写成。它安装一个 `WH_KEYBOARD_LL` 钩子，匹配按键和弦，
按需把匹配到的按键从前台应用那里隐藏掉（也就是 AutoHotkey 的行为），然后执行绑定
的动作（启动进程、发送按键、控制音量/媒体、操作窗口、切换虚拟桌面与剪贴板、
弹出通知、弹出选单、弹出快捷键帮助、睡眠/关机/重启/关屏）。它也能做按键重映射。

一句话：**用 Lua 配置、用 Qt/C++ 写的 AutoHotkey。**

UI 只有托盘图标、日志窗口、`menu` 选单、`help` 帮助这四样，全部是
**Qt Quick（QML）+ FluentWinUI3 样式**的窗口，跑在**同一个进程**里
（理由见第 2 节第 1 条与第 4 节）。

**当前的非目标**：图形化编辑器、鼠标钩子，
以及**把 Lua 函数当动作**（配置是脚本，但动作只能是声明式的表/字符串）。

### 技术选型一览

| 方面       | flowkeyd                                                                                                      |
| ---------- | ------------------------------------------------------------------------------------------------------------- |
| 语言/框架  | C++20 + Qt 6.11（Quick/QML 做 UI）+ 手写 Win32 声明                                                           |
| Lua        | **Lua 5.5.1**（`vendor/lua`，见第 3 节）                                                                      |
| DSL 全局表 | **`flowkeyd.*`** + 全局构造器                                                                                 |
| 配置目录   | `%USERPROFILE%\.config\flowkeyd\config.lua`                                                                   |
| 日志文件   | `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`                                                                 |
| 注入标记   | `dwExtraInfo` 里的 `"FLOW"`                                                                                   |
| 互斥体     | `Local\flowkeyd-<配置路径散列>`                                                                               |
| 开机自启   | 计划任务 `flowkeyd`（登录时 + 最高权限 + 15 秒延迟）指向**当前运行的 `flowkeyd.exe` 路径**：守护进程每次启动自动检查/注册（`--no-autostart` 跳过、`--remove-autostart` 删除），见第 2 节第 10 条与第 10 节 |
| 日志窗口   | **进程内的 QML 窗口**（FluentWinUI3），尾随同一个日志文件                                                     |
| 选单/帮助  | **QML 窗口**（FluentWinUI3），跑在 Qt GUI 线程上                                                              |
| 示例配置   | `flowkeyd.lua.example`                                                                                        |
| 自动化测试 | Qt Test 单元测试 + **`scripts/acceptance.ps1`**（116 项检查，注入按键 + 高亮/弹窗滚轮/拖动滚动条/“滚动不改键盘选中项”/鼠标点选与点筛选框/鼠标点选单条目/`Enter` 与双击真的执行动作/危险动作两次确认的外部验收；`--simulate`/`--selftest`/`--probe` 本期不做，见第 12 节） |
| 依赖管理   | CMake Presets + Ninja，`vendor/lua` 静态编进二进制                                                            |

---

## 2. 已经拍板的决策（不要再重新讨论）

这一节的每一条都是项目所有者 2026-09 明确选择的。**如果不确定要不要改，
先问用户**，不要自作主张地“改进”。

1. **应用形态：CLI 守护进程 + QML 弹窗，没有主窗口。**
   `main.cpp` 走 `QApplication` + `QQmlApplicationEngine` + `QSystemTrayIcon`；
   **现有的 `mainwindow.h/.cpp/.ui` 那套 Widgets 骨架要拆掉**
   （它在第 1 阶段删除）。命令行参数是唯一的控制面，UI 只有托盘图标、
   日志窗口、`menu` 选单、`help` 帮助这四样。
2. **Lua 用 `vendor/lua` 里现成的 Lua 5.5.1**（`lua/lua` @ `v5.5.1`），
   不换成 5.4。与 5.4 的差异要自己适配，**关键差异见第 7 节**。
3. **分阶段实施**（第 9 节），但**自动化测试以 Qt Test 单元测试为主**
   （第 5 节）。桌面行为（钩子真的吞了键、窗口真的被激活……）在阶段 9 之后
   由 `scripts/acceptance.ps1` 覆盖；脚本覆盖不到的仍然靠**手工冒烟验证**
   —— 每次动到钩子/引擎/分发/窗口后端，都要按第 5 节的清单过一遍。
4. **配置 schema 与 CLI 开关是 flowkeyd 自己的、稳定的公开接口**：
   字段名、取值、动作字段、DSL 构造器名（`settings{}`/`hotkey{}`/`remap{}`/
   `run()`/`send()`/`type_text()`/`open()`/`notify()`/`volume()`/`media()`/
   `window()`/`clipboard()`/`caps_lock()`/`suspend()`/`desktop()`/`power()`/
   `menu{}`/`help()`/`reload()`/`quit()`/`none()`）都要在 `README.md` 里写清楚，
   改动要当作破坏性变更对待（同步更新示例配置与 README）。
   挂在脚本全局的注册表表名是 **`flowkeyd`**。
5. **flowkeyd 最终接管这台机器**：第 10 阶段会把它做成常驻（提权）。
6. **不做 `--simulate` / `--selftest` / `--probe`。**
   `--check` / `--list` / `--list-keys` 保留（它们是产品功能，也是手工验证的
   主要工具）。
   → **阶段 9 补充（2026-09）**：这三个开关仍然不做，但“手工冒烟清单”已经
   自动化成了 **`scripts/acceptance.ps1`**（116 项检查），它靠一个
   **只给测试用的后门** `FLOWKEYD_ACCEPT_INJECTED=1` 抬升“丢弃注入输入”
   那道过滤（见第 5 节与阶段 9）。
   这是对一个“当时无法验证”的条款的修订，不是推翻：不变量 2 本身没动，
   日常跑的时候那道过滤照旧生效。
7. **依赖政策：默认只有 Qt + `vendor/lua`。** 需要新依赖时**按需引入**，
   但必须在提交信息里给出理由。
   `QUICK_START_DEPS`：JSON、CLI 解析、字符串工具都自己写或用 Qt 自带的；
   不要引入 `sol2`、`nlohmann::json`、`CLI11`、`spdlog` 之类“顺手”的库。
8. **构建：CMake Presets + Ninja，debug 与 release 双 profile 都必须编译通过。
   两条 profile 分工固定**：debug 里构建单测（`FLOWKEYD_BUILD_TESTS=ON`），
   release 里不构建单测、只产出干净的发布目录 `build/dist-release`。
   单元测试只在 debug 上跑，验收脚本只跑 release 的产品 exe
   （见工作约定第 2 条）。
9. **帮助窗口的界面用 Qt 自带的标准控件，不自绘。**
   （2026-09，项目所有者拍板：“不能用 qt 自带的列表控件实现么？不要自己绘制”，
   随后又要求“上部的搜索栏要用标准的 input 控件实现、鼠标能点选并执行列表项”。）
   之前那套自己算滑槽/滑块几何、自己命中测试、自己按格滚轮、用 `Rectangle` +
   `Label` + 一根 `Rectangle` 光标拼一个假输入框的做法，结果是**滑块拖不动**、
   **滚轮下高亮闪**、**筛选框点不动也进不了输入状态**、**列表项鼠标点不中**
   （详见第 10 节）。现在：
   * 列表是 `ListView` + `ItemDelegate`：滚动位置、滚轮、拖动滑块、惯性、
     悬停/按下/高亮全归 Qt 的标准样式；模型只管筛选、选中项与计数；
   * 筛选框是真正的 `TextField`：鼠标点一下就能打字，光标、选区、输入法、
     右键菜单、`Home`/`End`/左右箭头都是标准行为；模型只在 `setFilter()` 里
     接收最终文本（**它因此必须是 `Q_INVOKABLE`**，否则 QML 里那行调用会抛
     TypeError 而看上去像“处理器没跑”）；
   * **鼠标左键点一行 = 选中它 + 把它的按键复制走**；滚动（滚轮、拖滑块）仍然
     只滚视图、**不动**选中项；
   * **`Enter`（或双击一行）= 关掉窗口并执行那一行的动作**（2026-09 项目所有者
     拍板：“双击高亮选中的列表项或者直接回车，应该可以直接触发对应的 action”）。
     执行前先关窗是刻意的：`send`/`type`/`window` 这类动作作用在**前台窗口**上，
     不关窗就会打回帮助窗口自己的筛选框。触发的效果等价于按一下那个快捷键
     （先执行按下时的动作、再执行松开时的动作）；`remap` 行等价于按一下源键
     （注入它的目标按键）。
   * **`quit`/`suspend`/`power` 要两次**（项目所有者拍板）：第一次 `Enter`/双击
     只是把那一行“武装”起来（行变色 + 底部提示换成确认文案），再按一次才真的
     执行；`Esc`、上下换行、改筛选都取消。判定在 `core::isDestructive()`；
     **`menu` 不算危险**（它只是把选单弹出来，真正的危险条目在选单里还有一次
     选择）。
   * 列表只占行区域，所以旧实现里那两块“遮住滚进来的一行”的不透明底色
     与 `scrollTargetY()` 都删掉了；键盘把选中项带进视野用 Qt 的
     `positionViewAtIndex(..., Contain)`（行区域就是 `ListView` 的视口，
     不再有“表头盖住行”的问题）；
   * **滚轮方向跟着系统/Qt**（本机实测 `mouseData=-120` 往下、`+120` 往上，
     WinForms 的 `ListBox` 也一样）。自绘时期的 flowkeyd 曾把正数
     当成“往列表后面走”，方向与系统列表控件相反；**已经改掉**，
     `scripts/acceptance.ps1` 里帮助那一段因此是 `Wheel(-120)`。
   * 选单（`MenuPopup`）也走同一条路线（项目所有者 2026-09 要求），而且它
     **不滚动**（条目数决定卡片高度），所以没有滚动条：列表同样是真正的
     `ListView` + 标准 `ItemDelegate`，左键点一行 = 执行它，行几何与命中测试
     不再进模型（见第 10 节）。与帮助窗口的区别只有两处：**悬停仍然驱动高亮**
     （`Enter` 执行光标下那一条，这是菜单的语义），以及它一次只列一层、不筛选。
   * **中文字体用微软雅黑**（项目所有者 2026-09 拍板）：两个弹窗里的每个会画字
     的控件都写 `font.family: "Microsoft YaHei"`（`Window`/`Item` 没有 `font`
     属性，不会自动往下传；QML 的 `font` 值类型也只有 `family`，没有族列表）。
     默认族 `Segoe UI Variable` 没有中文字形，不管的话中文会回退到宋体
     —— 见第 10 节。
10. **开机自启：计划任务指向当前运行的 exe**（项目所有者 2026-09 拍板；先做过
    “安装目录”方案，后来改成自注册，见第 10 节）。任务 `flowkeyd` 在登录时以
    **最高权限**启动一个 exe（这样提权但不弹 UAC），而那个路径由守护进程**每次
    启动时自己检查/刷新**：任务缺失、或指向的 exe 与当前运行的这一个不同，就用
    当前路径重新注册（`platform/win/autostart`，内部走 `schtasks /Create /XML`）。
    因此**没有安装目录、也没有部署脚本**：把 exe 拷到哪儿就在哪儿生效，
    `scripts\install.ps1` / `scripts\uninstall.ps1` 已删除。
    * 开发/测试实例跳过：`--no-elevate`、`--allow-multi`、未提权、以及显式的
      `--no-autostart` 都不会去碰计划任务 —— 否则临时实例会把用户的开机自启
      劫持到一个会被清理的构建目录（计划任务指向失效路径是**完全静默**的失败）。
    * 删除自启用 `--remove-autostart`（需管理员；先 `--quit` 停常驻，否则它下次
      启动会把任务注册回来）。
    * 任务的每个参数（`PT15S` 延迟、`ExecutionTimeLimit=PT0S`、电池两项、
      `InteractiveToken`、`IgnoreNew`、`RestartOnFailure`）与当年 `install.ps1`
      的 XML 逐字段一致，为什么见第 10 节与 `README.md` 的「开机自启与更新」。
    * **注册 / 刷新之前先问用户**（2026-09 要求）：任务缺失、或指向的 exe 不是
      当前这一个时，先弹一个原生确认框；同意才动任务，拒绝就保持原样。
      `--no-prompt` 跳过这个询问、按默认「注册 / 更新」处理。
11. **启动时的两个交互确认（项目所有者 2026-09 要求）。**
    1. **已经在运行**：守护进程启动时先看一眼同一配置文件有没有实例在跑；有就弹一个
       原生提示框（`flowkeyd 已在运行`），用户点确定后以**退出码 1** 退出。
       **这一步刻意放在 UAC 提权之前**，所以重复双击不会白弹一次 UAC，也不会动到
       正在运行的那个实例（真正的互斥体获取仍在提权之后，用来兜住「两个进程同时
       启动」的竞态）。
    2. **开机自启**：任务缺失、或指向别的 exe 时先弹确认框（`flowkeyd 开机自启`）
       问要不要注册 / 更新，见第 2 节第 10 条的最后一条。

    两者都用**原生 `MessageBoxW`**（不需要 Qt 应用对象，因为它们跑在
    `QApplication` 构造之前）；`--no-prompt` 跳过这两个提示、按默认处理，
    供脚本与 `scripts/acceptance.ps1` 使用。细节与验过的路径见第 10 节。
12. **构建版本号 = `yy-MM-dd-<git 短修订>`（项目所有者 2026-09 拍板，`core/version.*`）。**
    例如 `26-09-22-42900ad`。**日期** 取运行中这个 exe 自己的最后写入时间
    （＝它被链接到磁盘的时刻，本地时间 `yy-MM-dd`）；**修订** 是构建时由 CMake
    用 `git rev-parse --short HEAD` 取到、经 `cmake/version_revision.h.in` 写进
    `build/<preset>/generated/flowkeyd_revision.h` 的编译期常量。显示在四处：
    托盘右键菜单里一个**不可点**的*版本*信息项、守护进程启动日志的第一行、
    `--version` 的第一行、以及 `--help` 的表头。日期不用编译期常量（那会让每次
    构建都重新链接，见第 10 节），修订用编译期常量（只在提交 / 切分支时才变）。
    **工作流是「先提交再构建」**：哈希就是构建时的 HEAD（工作区脏时显示最近一次
    提交）；把 `.git/HEAD` 与当前分支 ref 登记成 configure 依赖就是为了提交后
    重新构建能自动刷新它。取不到 exe 时日期段是 `unknown`，不在 git 仓库里时
    修订段是 `unknown`。
13. **窗口摆放规则 `window_rule{...}`（项目所有者 2026-09 要求）**：用 Lua 配置
    控制“某个程序启动时出现在哪个 workspace 和 monitor”，并且**断开的显示器
    重新接上时按配置重新归位**。构造器叫 `window_rule`（声明式字段是
    `window_rules`），字段：`process` / `title`（至少一个）、`desktop`、
    `all_desktops`、`topmost`、`monitor`、`maximize`、`x`/`y`、`width`/`height`、
    `name`、`enabled`。项目所有者拍板的几条：
    * `monitor` 只支持**序号**（1 起，左→右、上→下）、`"primary"`、**设备名**
      （`"DISPLAY2"` / `"\\.\DISPLAY2"`）；**不支持按分辨率匹配**。
    * **默认最大化**（写了 `monitor` 又没写位置/大小时铺满那块显示器的工作区），
      但位置与大小可配（`x`/`y` 相对目标工作区左上角，`width`/`height` 像素）。
      `maximize = true` 与位置/大小互斥。只写 `desktop` 时**不动窗口几何**。
    * 触发时机只有三个：**窗口第一次出现**、**显示器重新接入**、**flowkeyd
      启动时**。之后不再干预（用户自己移动/缩放窗口不会被纠正）。
    * **`all_desktops`（2026-09 新增）**：`true` 把窗口钉在**所有**虚拟桌面上
      （Task View 的「在所有桌面显示」），`false` 显式取消钉住，不写就不去碰它。
      **与 `desktop` 互斥**（`--check` 拒绝同时写）。走 shell 的
      `IVirtualDesktopPinnedApps`（不是 `IVirtualDesktopManagerInternal`，见第 10 节）。
    * **`topmost`（2026-09 新增）**：`true` 让窗口始终在最上层，`false` 显式取消
      置顶，不写就不去碰它。走已公开的 `SetWindowPos(HWND_TOPMOST)`。
    * 这两个开关都**不算“几何”**：只写它们（没写 `monitor`）时窗口的大小/位置保持
      不动，也不会因为“默认最大化”而突然变大；它们是“设置一次”的，用户之后
      手动取消钉住/置顶不会被纠正。
    * “主窗口”的判据：可见、无属主、非 `WS_EX_TOOLWINDOW`、有标题、尺寸非零。
    实现分散在 `core/placement`（纯几何/匹配）、`platform/win/monitor`（枚举与
    `SetWindowPlacement`）、`platform/win/desktop::moveWindowToDesktop`
    （未公开的 `MoveViewToDesktop`）、`platform/win/hook`（`SetWinEventHook` +
    显示器轮询）与 `app/dispatcher`（真正执行）。细节与坑见第 10 节。
14. **跨虚拟桌面的“唤起”与跟随（项目所有者 2026-09 拍板）。**
    1. `window` 动作的 `activate`：目标窗口不在当前虚拟桌面上时不算“已经在前台”，
       按下去就是**切到它所在的那张桌面并激活它**（视图跟着过去）；只有它已经在
       当前桌面并且真的在前台时才是 `toggle` 的收起。
    2. `window_rule` **真的**把窗口搬到了另一张桌面时，视图也跟着过去并重新激活
       那个窗口（“总是跟随”，不限于“窗口正在前台”）。
       **只在“窗口第一次出现”那一遍做**：启动 / 显示器重新接入那两遍只重新摆放
       已经在位的窗口，跟着走会把视图无谓地切来切去。
       “真的搬动”= 窗口的桌面 GUID 变了；窗口本来就在目标桌面上（同一个程序又开
       一个窗口）不算，不切。
    为什么这两条必须一起做：`MoveViewToDesktop` 把窗口搬走之后 shell 仍然把它当
    作**前台窗口**，所以“是不是已经激活”的判定不带上虚拟桌面时，同一个快捷键会
    去*收起*一个用户根本看不见的窗口 —— 见第 10 节。

---

## 3. 环境与工具链

|            |                                                                                                             |
| ---------- | ----------------------------------------------------------------------------------------------------------- |
| 开发环境   | 原生 Windows 10.0 build 26200 (x64)，交互式桌面会话                                                         |
| 工程路径   | `D:\prj\flowkeyd`                                                                                           |
| Qt         | `C:\Qt\6.11.2\mingw_64`（只有 mingw_64 这一套）                                                             |
| C++ 编译器 | `C:\Qt\Tools\mingw1310_64\bin\g++.exe`（GCC 13.1.0）                                                        |
| CMake      | `C:\Qt\Tools\CMake_64\bin\cmake.exe`                                                                        |
| Ninja      | `C:\Qt\Tools\Ninja\ninja.exe`                                                                               |
| Qt Creator | kit `Desktop_Qt_6_11_2_MinGW_64_bit_Debug`（生成器 `MinGW Makefiles`，产物目录 `build/Desktop_Qt_…_Debug`） |
| 未安装     | Visual Studio / MSVC、MSYS2、独立的 mingw（Qt 自带那个就够）                                                |
| Lua        | `vendor/lua` = `lua/lua` **v5.5.1**（`7579fc9d7ed90240487251dfb69168f8e64e9294`）                           |

**已验证的事实（2026-09）**：

* `vendor/lua` 的 32 个 `.c`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`）
  用 `g++/gcc -O2 -DLUA_USE_WINDOWS` **全部编译通过**，零错误。
  所以 Lua 5.5.1 这条路是通的，第 1 阶段直接就能接上。
* Qt 侧齐备：`Qt6::Quick`、`Qt6::QuickControls2`、
  `Qt6QuickControls2FluentWinUI3StyleImpl`、`Qt6::Test`、`Qt6::QuickTest`、
  `Qt6::Widgets`（`QSystemTrayIcon` 要用）、`Qt6::LabsPlatform` 都在。
* `qml/QtQuick/Controls/FluentWinUI3` 存在（该样式自 Qt 6.8 起提供）。

### `vendor/lua` 现在还不是一个**正式登记的** submodule

`vendor/lua/.git` 里写着 `gitdir: ../../.git/modules/vendor/lua`，
`.git/modules/vendor/lua` 也在，remote 是 `https://github.com/lua/lua`，
但是 **仓库根目录没有 `.gitmodules`**，所以 `git submodule status` 是空的、
`git status` 把 `vendor/` 整个当成未跟踪目录。

第 1 阶段要把它补成正式的 submodule（`.gitmodules` 指向
`https://github.com/lua/lua`、记录 `v5.5.1` 那个 commit），
否则克隆仓库的人拿不到 Lua 源码。

### 依赖政策：什么可以静态链接，什么必须运行时解析

**唯一的例外是 Qt 自己的库随便链**：

* **允许静态链接的集合**（MinGW 的工具链自带这些导入库，实测都在
  `C:\Qt\Tools\mingw1310_64\x86_64-w64-mingw32\lib` 下，
  `target_link_libraries` 里写名字即可）：
  `user32`、`kernel32`、`shell32`、`ole32`、`ntdll`、`advapi32`、
  `powrprof`、`uxtheme`、`comctl32`、`shlwapi`。
  （`comctl32`/`shlwapi`/`gdi32` 大概率用不到 —— UI 是 QML，不用手画。
  `powrprof` 直接静态链接 `SetSuspendState` 即可，不需要运行时解析。）
* **必须 `LoadLibraryW` + `GetProcAddress` 运行时解析**：
  未公开入口（`win32u!NtUserSendInput`、`NtUserGetAsyncKeyState`……，
  实测**没有** `libwin32u.a`）以及任何不在上面那个集合里的 DLL。
  理由：这些接口没有 ABI 承诺，而且少一条静态依赖就少一个“在某些机器上
  exe 根本起不来”的机会。
  **`dwmapi` 也走这条路**（虽然 `libdwmapi.a` 存在）：按窗口的
  `DWMWA_TRANSITIONS_FORCEDISABLED` 是 Win10 才有的属性，运行时解析 +
  `available()` 探测的成本几乎为零。
* **COM 接口手写 vtable**（Core Audio `IAudioEndpointVolume`、
  shell 的 `IVirtualDesktopManagerInternal`）。**这是本仓库风险最高的两块代码**：
  vtable 布局写错不是返回错误码，而是崩溃。接口定义以真机验证为准，
  虚拟桌面那张表按 `build.revision` 索引（见 `platform/win/desktop`）。

> 不引入会藏起未公开入口的封装层 —— 未公开 API 一律自己 `GetProcAddress`。

---

## 4. 代码地图（计划中的目录结构）

第 1 阶段就把这个骨架立起来；后面每个阶段往里填。

| 路径                                      | 职责                                                                                                                                                                                                                        |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CMakeLists.txt`                          | 顶层工程、Qt 查找、`qt_add_executable`、`qt_add_qml_module`、安装/部署                                                                                                                                                      |
| `CMakePresets.json`                       | `windows-debug` / `windows-release` 两个 preset（Ninja + `mingw1310_64` + Qt 6.11.2）                                                                                                                                       |
| `cmake/VendorLua.cmake`                   | 把 `vendor/lua` 编成静态库 `lua_static`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`，定义 `LUA_USE_WINDOWS`）                                                                                                              |
| `flowkeyd.lua.example`                    | 有文档、覆盖全部特性的参考配置（中文注释、无警告），`--check` 就是拿它跑的                                                                                                                                                  |
| `README.md`                               | 用户文档（中文，含完整配置/动作/schema 说明），是配置 schema 的权威定义                                                                                                                                                      |
| `logo.svg`                                | **应用图标的美术源**（仓库根目录，唯一的真源）。它不被任何构建步骤读取，只在改图标时被 `tools/icon_gen` 光栅化（见第 10 节）                                                                                                |
| `assets/flowkeyd.ico`                     | **exe 的 Windows 图标资源**（9 帧：16–64 为 DIB，128/256 为 PNG），由 windres 通过配置时生成的 `assets/flowkeyd.rc.in` 嵌进 exe。改了 `logo.svg` 要重新生成：`cmake --build --preset debug --target icons`。**要提交** |
| `assets/icons/flowkeyd-<n>.png`           | 运行时 `QIcon` 的 9 个尺寸（16/20/24/32/40/48/64/128/256），编在 exe 自己的 qrc 里（`:/icons/…`，见 `src/app/app_icon.*`）。**要提交**                                        |
| `assets/flowkeyd.rc.in`                   | 图标资源的 .rc 模板（`*.rc` 在 `.gitignore` 里，所以模板后缀是 `.in`）：CMake 在配置时把它展开成 `build/<preset>/generated/flowkeyd.rc`，`.ico` 写**绝对路径**（windres 不把 `ICON` 的相对路径当相对 .rc 文件）。纯 ASCII |
| `tools/icon_gen/main.cpp`                 | 一次性工具（`flowkeyd_icon_gen` + `icons` 目标，`EXCLUDE_FROM_ALL`，需要 Qt6::Svg）：把 `logo.svg` 光栅化成上面那两个产物。两条 profile 的正常构建都不碰它（见第 10 节）
| `src/main.cpp`                            | `AttachConsole` + CLI 分发 + 日志初始化 + **提权之前的单实例预检（“已在运行”原生提示框）** + 开机自启的确认框 + 组装 Runtime + Qt 事件循环                                                                                                                                               |
| `src/cli.h/.cpp`                          | 参数解析 + 中文帮助文本（手写，不用 CLI11）；`--quit` 走单独的早期分支：不装钩子、不提权，也不在 `isOfflineCommand()` 里（它确实要去碰另一个进程）；`helpText()`/`versionText()` 都接收 `core::buildVersion()` 给出的构建版本号                                                                                                                                                                                 |
| `src/core/`                               | **纯逻辑层：不碰 Win32、不碰 Qt GUI**（只用 QtCore 的类型），因此能被 Qt Test 直接测                                                                                                                                        |
| `src/core/keys.h/.cpp`                    | 键名 ↔ `VK` 表、`Modifiers`、`Chord`、AutoHotkey 发送脚本解析、小键盘 Enter 的内部伪码 `0x100`、`key_from_hook()`/`native_key()`                                                                                            |
| `src/core/config.h/.cpp`                  | 配置结构体、严格校验（未知字段要报错）、编译成 `Compiled`/`Binding`/`CompiledRemap`、配置文件搜寻与旧 TOML 的迁移提示                                                                                                       |
| `src/core/engine.h/.cpp`                  | 快捷键状态机：匹配、优先级、吞键、自动重复抑制、长按重复、挂起、重映射 hold/tap、Win/Alt 菜单遮断按键                                                                                                                       |
| `src/core/template.h/.cpp`                | `{clipboard}`、`{selection}`、`{date}` 等占位符展开                                                                                                                                                                         |
| `src/core/action.h/.cpp`                  | 声明式动作的表示 + 摘要文本（`--list` 与 `help()` 都用它）+ `isDestructive()`（帮助窗口要靠它决定“要不要再确认一次”）                                                                                                        |
| `src/core/log_tail.h/.cpp`                | 日志文件的增量尾随（纯逻辑，可单测）：按字节读、末尾不完整的 UTF-8 序列不消费、半行留到下一轮、一次最多 1000 行                                                                                                            |
| `src/core/window_match.h/.cpp`            | 窗口匹配与 `window` 动作决策的纯函数：标题/进程名子串、可执行文件名提取、`toggle` 边界、`animate` 是否有意义                                                                                                                |
| `src/core/placement.h/.cpp`               | `window_rule` 的**纯逻辑**（只用 QtCore、可单测）：显示器排序与选择（序号 / `primary` / 设备名）、「显示器重新接入」检测（设备名从无到有）、摆放几何（最大化 / 居中 / 指定位置与大小 / 夹进工作区）、规则匹配。`all_desktops` / `topmost` 不算几何，只影响 `WindowRule::summary()`。见第 2 节第 13 条 |
| `src/core/version.h/.cpp`                 | 构建版本号（纯逻辑、可单测）：`buildVersion(executablePath)`（拼成 `yy-MM-dd-<git 短修订>`）、`buildDateFromFile()`、`sourceRevision()`（编译进来的 `FLOWKEYD_GIT_REVISION`）、`unknownValue()`。修订来自 CMake 用 `cmake/version_revision.h.in` 生成的 `flowkeyd_revision.h`；机制与取舍见第 2 节第 12 条与第 10 节 |
| `src/lua/lua_config.h/.cpp`               | **Lua 与 C++ 的唯一边界**：建 `lua_State`、注入 DSL、把脚本里的表转成 `core::Config`（逐条目、带上下文的错误）、UTF-8 BOM 剔除、`.toml` 明确拒绝                                                                            |
| `src/lua/lua_prelude.lua`                 | 注入配置脚本的 DSL：`settings{}`/`hotkey{}`/`remap{}` + 动作构造器 + `flowkeyd` 表。**纯 Lua，改它不需要改 C++**（编进 qrc，见第 7 节）                                                                                     |
| `src/platform/win/`                       | Win32 后端（每个文件都只做一件事，方便单独替换）                                                                                                                                                                            |
| `src/platform/win/ffi.h/.cpp`             | 全部 Win32 声明、结构体与常量（`INPUT` 的 40 字节布局有 `static_assert` 盯着）                                                                                                                                              |
| `src/platform/win/nt.h/.cpp`              | 未公开的 `win32u.dll` 导出，运行时解析并校验                                                                                                                                                                                |
| `src/platform/win/dwm.h/.cpp`             | **运行时解析**的 `dwmapi!DwmSetWindowAttribute`：按窗口关掉过渡动画（`window` 的 `animate`）；拿不到 dwmapi 时只是保留动画，动作不失败                                                                                      |
| `src/platform/win/monitor.h/.cpp`         | 显示器枚举（`EnumDisplayMonitors` → `core::MonitorDescription`）、窗口当前在哪块屏、`applyPlacement`（`SetWindowPlacement` + `SetWindowPos`，带 `SWP_NOACTIVATE`，最大化时先还原再最大化）。**几何判断不在这一层** |
| `src/platform/win/input.h/.cpp`           | 按键注入（`SendInput`/`NtUserSendInput`）、按键状态、`ModifierGuard`（含菜单遮断标记）、`FLOWKEYD_ACCEPT_INJECTED` 测试后门（见第 5 节与阶段 9）                                                                                |
| `src/platform/win/hook.h/.cpp`            | 钩子回调、**钩子线程自己的 Win32 消息循环**、`SetTimer`、控制消息、重载；另外还负责 `window_rule` 的两个监听：`SetWinEventHook`（`EVENT_OBJECT_SHOW` / `DESTROY`，按 HWND 去重）与一个 350 ms 的显示器轮询定时器。**定时器 id 必须用 `SetTimer` 的返回值**，见第 10 节 |
| `src/platform/win/audio.h/.cpp`           | Core Audio `IAudioEndpointVolume`，手写 COM vtable（**高风险**）                                                                                                                                                            |
| `src/platform/win/clipboard.h/.cpp`       | 剪贴板读写（`CF_UNICODETEXT`）                                                                                                                                                                                              |
| `src/platform/win/window.h/.cpp`          | 窗口查找（标题子串/可执行文件名）、激活/最小化/最大化/还原/关闭/置顶、前台锁绕行、启动回退、`TransitionGuard`（RAII 恢复动画开关）、`setTopmost`（`window_rule` 的 `topmost` 与 `window` 的 `toggle_topmost` 共用）。**“是否已经激活”还要看虚拟桌面**：被 `window_rule` 搬到别的桌面的窗口仍被 shell 当前台窗口（见第 2 节第 14 条），`raiseWindow` 在这时先显式切到它那一张桌面。                  |
| `src/platform/win/desktop.h/.cpp`         | 虚拟桌面切换、**窗口移动**与**钉在所有桌面**：`CLSID_ImmersiveShell` → `IServiceProvider::QueryService` → 未公开的 `IVirtualDesktopManagerInternal`（按 `build.revision` 查表）+ 未公开的 `IVirtualDesktopPinnedApps`（IID 不随版本变，所以不进表）；`moveWindowToDesktop` 走 `MoveViewToDesktop`（vtable 下标 4，三种布局一致，`changed` 出参报告“真的换了桌面吗”）、`setWindowPinned`/`isWindowPinned` 走 `PinView`/`UnpinView`/`IsViewPinned`（下标 7/8/6）、`switchToWindowDesktop` 把视图切到**某个窗口所在**的桌面（未公开的 `IVirtualDesktop::GetID` 下标 4 与已公开的 `GetWindowDesktopId` 逐个比对，对不上就只报错），并用**已公开**的 `IVirtualDesktopManager::GetWindowDesktopId` / `IsWindowOnCurrentVirtualDesktop` 做验证与诊断 |
| `src/platform/win/power.h/.cpp`           | `powrprof!SetSuspendState`、`user32!ExitWindowsEx`、`LockWorkStation`、`WM_SYSCOMMAND`/`SC_MONITORPOWER` 广播，外加 `SeShutdownPrivilege`                                                                                   |
| `src/platform/win/tray.h/.cpp`            | 托盘图标 + 气泡提示 + 右键菜单（查看日志/挂起/重载/打开配置/版本/退出）+ 悬停提示（构建版本 + 挂起状态）。图标是构造时传进来的应用图标（`app::applicationIcon()`，见 `app/app_icon.*`）；拿不到时退回系统图标，免得托盘上什么都没有 |
| `src/platform/win/logging.h/.cpp`         | 控制台/文件日志器（英文、分级别、可选 ANSI 颜色），`--log-level`/`--log-file`/`--no-color`                                                                                                                                  |
| `src/platform/win/single_instance.h/.cpp` | 按配置路径散列命名的互斥体（含提权重启后的重试）；`instanceRunning`（`OpenMutexW`，不获取所有权，**提权之前**的「已在运行」检查）；**`--quit` 的命名事件通道**：`quitEventName`/`createQuitEvent`（带 Low 完整性标签的 SDDL，让不提权的调用方也能 `SetEvent`）/`requestQuit`/`quitEventExists` |
| `src/platform/win/elevate.h/.cpp`         | `ShellExecuteW("runas")` 自提权 + UAC 被拒时降级继续 + `--elevated` 标记 + 命令行/工作目录转发（`quote_arg`）                                                                                                               |
| `src/platform/win/autostart.h/.cpp`       | 开机自启的计划任务：`buildTaskXml`/`taskXmlCommand`/`decodeTaskOutput`/`sameExecutablePath` 是**纯函数**（可单测），`query/register/removeAutostartTask` 走隐藏的 `schtasks.exe /Create /XML`，`ensureAutostart(spec, confirm)` 是启动时的“缺失或指向别的 exe 就**先问用户、同意后**刷新成当前路径”策略（`confirm` 为空表示不问）。**不写任何安装目录**（见第 2 节第 10 条与第 10 节） |
| `src/app/`                                | 组装层：把 core / lua / platform 串起来，并拥有 Qt 对象                                                                                                                                                                     |
| `src/app/app_icon.h/.cpp`                 | 应用图标：把 qrc 里的 9 张 PNG 帧拼成一个多尺寸 `QIcon`（`applicationIcon()`），托盘、全部 QML 窗口与 Qt 消息框都用它。用 PNG 而不用 SVG 是为了不依赖 `Qt6Svg` 与 `imageformats/qsvg` 插件（见第 10 节） |
| `src/app/dispatcher.h/.cpp`               | **动作工作线程**（`QThread`）：执行动作列表，含 `window` 的“先启动再激活”与默认开的 `toggle` 收起、`menu` 的窗口请求、`help` 的窗口请求 + 每一行的“执行目标”（绑定是 press+release 两串动作，`remap` 是直接注入目标按键）；还执行 `window_rule`（窗口出现 / 显示器重新接入 / 启动时各跑一次，`isPlaceableWindow()` 判“主窗口”；**只有“窗口出现”那一遍**会在规则真的搬迁窗口时把视图跟过去并重新激活，见第 2 节第 14 条） |
| `src/app/runtime.h/.cpp`                  | 引擎 + 钩子 + 分发 + 托盘 + 弹窗的总装，`ControlCmd`（suspend/reload/quit）通道；还持有 `--quit` 的事件句柄并用 `QWinEventNotifier` 在 GUI 线程上监听（收到就走 `performShutdown`）                                                       |
| `src/app/log_model.h/.cpp`                | 日志窗口的模型：尾随日志文件（增量、半行、被截断的多字节 UTF-8）、最多 1000 行、按级别配色、子串过滤                                                                                                                        |
| `src/app/menu_model.h/.cpp`               | `menu` 选单的**纯逻辑**（`QAbstractListModel`，只用 QtCore）：卡片外框几何（宽高、标题、底部提示）、高亮移动（到边界回绕）、单字符选中、`Esc`/`Enter` 语义，以及给 QML 排版用的几个常量（`listTop`/`rowHeight`/`rowSpacing`/`rowInset`/`badgeSize`）。**行几何与鼠标命中不归它管**：列表是真正的 QML `ListView` + 标准 `ItemDelegate`（见第 2 节第 9 条与第 10 节），所以它没有 `rowRect`/`hitTest`，也**没有** `highlighted`/`hovered` 角色（那两个名字被标准委托占了）。悬停仍由模型持有（`hover`/`setHover`），因为「`Enter` 选光标下那一条」是选单的语义 |
| `src/app/help_model.h/.cpp`               | `help` 帮助的**纯逻辑**（同上）：筛选（和弦/`comment`/`name`/动作摘要）、`可见/总数` 计数、键盘选中项（**高亮就是它**，鼠标悬停不改高亮）、`Enter`/双击该执行还是先武装（危险动作两次确认）、三级 `Esc`，以及鼠标点选用的 `setSelected()`（**可单测**）。**列表的滚动、行几何与鼠标命中都不归它管**：那是一个真正的 QML `ListView` + `ItemDelegate` + Qt 自带的 `ScrollBar`（见第 2 节第 9 条）。`handleKey()` 只接导航键与 `Enter`/`Esc`，字符/退格/`Home`/`End` 放行给标准 `TextField` |
| `src/app/popup_layout.h/.cpp`             | 两个弹窗共用的几何类型（`PopupRect`/`PopupPoint`）与纯函数 `centrePopup()`（先在工作区居中、再夹进屏幕；**可单测**） |
| `src/app/popup_host.h/.cpp`               | 把上面的模型挂到 QML 窗口上；抢前台（`requestActivate` + `win::window::raiseWindow` 的前台锁绕行）；在 Qt GUI 线程上创建/复用窗口；用户选完（或按 `Enter`/双击帮助里的一行）把活儿回投工作线程；`helpRun()` 负责把**可见行下标**换算成条目下标，并且**先把窗口藏起来再执行**（**GUI 线程亲和**） |
| `src/qml/`                                | `LogWindow.qml`、`MenuPopup.qml`、`HelpPopup.qml`（三个文件都在开头写了 `pragma ComponentBehavior: Bound`）；配色一律用 `palette`，没有单独的 `Style.qml`；中文一律 `font.family: "Microsoft YaHei"`（默认族 `Segoe UI Variable` 没有中文字形，不管会回退到宋体，见第 10 节）。`HelpPopup.qml` 与 `MenuPopup.qml` 里除了卡片外框与按键徽标全是标准控件：帮助的筛选框是 `TextField`、列表是 `ListView` + Qt 自带 `ScrollBar` + `ItemDelegate`（列表只占行区域，不再需要表头/底部的遮罩）；选单的列表同样是 `ListView` + `ItemDelegate`（不滚动，所以没有滚动条；悬停与点击全部由委托提供） |
| `tests/`                                  | Qt Test：`tst_keys`、`tst_engine`、`tst_config`、`tst_lua`、`tst_template`、`tst_send_script`、`tst_window_match`、`tst_log_tail`、`tst_audio`、`tst_autostart`（自启的纯逻辑：XML 渲染/解析、输出解码、路径比较；**不碰真实计划任务**）、`tst_interactive`（需 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`，否则 skip；含剪贴板/音量/窗口后端/虚拟桌面/钉在所有桌面/置顶的真机验证）、`tst_menu_model`、`tst_help_model`、`tst_power_table`、`tst_desktop_table`、`tst_placement`（`window_rule` 的纯逻辑：显示器排序/选择、重连检测、摆放几何、匹配与摘要）、`tst_layout`、`tst_version`（构建时间戳与版本字符串的纯逻辑；只碰临时文件） |
| `scripts/acceptance.ps1`                  | 桌面行为的验收脚本（注入按键 + 焦点捕捉窗口的外部观察，116 项检查：含弹窗滚轮/滚动条拖动/鼠标点选与点筛选框/鼠标点选单条目/`Enter` 与双击真的执行动作/危险动作两次确认）；需交互式桌面，**不属于 `ctest`**，见第 5 节与阶段 9。它用 `--no-elevate` 起临时守护进程，所以**不会**碰真实的自启计划任务 |

> `scripts/install.ps1` / `scripts/uninstall.ps1` **已删除**（2026-09）：自启的注册、
> 刷新与删除现在全在 `src/platform/win/autostart.*` 里，由守护进程自己在启动时做。

### CMake 目标划分（阶段 6 之后）

| 目标               | 内容                                                              | 谁链接                    |
| ------------------ | ----------------------------------------------------------------- | ------------------------- |
| `flowkeyd_core`    | `src/core/*`（纯逻辑，只用 QtCore）                                | exe + 全部单测            |
| `flowkeyd_lua`     | `src/lua/*` + 编成 qrc 的 `lua_prelude.lua`                        | exe + `tst_lua`           |
| `flowkeyd_models`  | `src/app/{menu,help}_model.*` + `src/app/popup_layout.*`（纯逻辑，只用 QtCore） | exe + `tst_menu_model`/`tst_help_model` |
| `flowkeyd_platform`| `src/platform/win/*`（不碰 Qt GUI的 Win32 后端）                    | exe + 平台层单测           |
| `flowkeyd`         | `src/main.cpp`、`src/cli.*`、`src/app/*`、`src/platform/win/tray.*`、QML、**图标 qrc（`:/icons`）+ 图标 .rc** | ——                        |
| `flowkeyd_icon_gen`| `tools/icon_gen/main.cpp`（把 `logo.svg` 光栅化成 `assets/` 下的 .ico 与 PNG；**`EXCLUDE_FROM_ALL`**，只由 `icons` 目标手工构建） | ——（不进任何产物）        |

> `src/app/popup_host.*` 用 QML/QtQuick，所以**不进** `flowkeyd_models`，留在 exe 里；
> 模型层只有 QtCore，这样 `tst_menu_model`/`tst_help_model` 能在没有桌面的情况下跑。

`flowkeyd_add_test(name [LIBS …])` 负责把 Qt/MinGW 的 DLL 目录写进 test 的 `PATH`。
**测试目标只在这个函数被调用时创建，而整段（连 `enable_testing()`）都包在
`FLOWKEYD_BUILD_TESTS` 里**：debug preset 给 `ON`、release preset 给 `OFF`
（`CMakeLists.txt` 里的默认值跟着 `CMAKE_BUILD_TYPE` 走）。所以 release 的
构建树里根本不会出现 `tst_*.exe`。

**exe 的产物目录是自包含的**：`flowkeyd` 上挂了一条 `POST_BUILD` 的
`windeployqt`（`Qt6::windeployqt`），它把 Qt 与 MinGW 运行时的 DLL + exe 用到的
QML 模块（`--qmldir src/qml`）拷到 exe 同目录，所以双击
`build/windows-release/flowkeyd.exe`（或 debug 那一份）就能启动，不必手动改 `PATH`。
只对 `flowkeyd` 做，**不给测试可执行文件做**（那些 `tst_*` 靠 ctest 注入 `PATH`，
而且每个都部署一次会让构建慢得多）。大小参考：release 目录里 Qt 侧大约 120 MB
（含 19.7 MB 的 `opengl32sw.dll`，刻意保留 —— 不想为了省 20 MB 去赌软件回退）。

**release profile 还会额外产出一个干净的发布目录**：另一条 `POST_BUILD` 把
`flowkeyd.exe` 与它的运行时（同一条 `windeployqt`，只换个落点）放进
`build/dist-release/`。那个目录里**只有运行需要的东西**，可以直接整个拷到别的
机器上跑；发布版本以它为准（`build/windows-release` 是构建树，里面还有
`CMakeCache.txt`/`build.ninja`/`*.a` 之类的东西）。debug profile 不产出它。

**分层铁律**：`src/core/`、`src/lua/`（除 `lua_config.cpp` 里对 Lua C API 的
调用之外）与 `src/app/{menu,help}_model.*`、`src/app/popup_layout.*`
**不许出现 `<windows.h>`、不许出现 QML/QtWidgets、不许出现窗口句柄**。
这正是 `--check`/`--list` 能在没有桌面的情况下跑、以及单元测试能覆盖核心逻辑的原因。
`src/platform/win/window.cpp` 里“候选窗口如何匹配”这种判断要拆成纯函数放进
`core`（或单独的 `window_match.cpp`），让 `platform` 那层只剩枚举与 API 调用。

---

## 5. 构建、测试与运行（DoD）

### 一次性配置 preset

第 1 阶段要产出 `CMakePresets.json`，包含（名字可微调，但要写进本文件）：

```jsonc
// 要点：Ninja + GCC 13.1 + Qt 6.11.2；两条 profile（windows-debug /
// windows-release），release 会额外把干净的发布目录写到 build/dist-release
// （见第 4 节末与第 5 节）。
{
  "version": 6,
  "configurePresets": [
    { "name": "windows",        "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "C:/Qt/6.11.2/mingw_64",
        "CMAKE_C_COMPILER":   "C:/Qt/Tools/mingw1310_64/bin/gcc.exe",
        "CMAKE_CXX_COMPILER": "C:/Qt/Tools/mingw1310_64/bin/g++.exe",
        "CMAKE_MAKE_PROGRAM": "C:/Qt/Tools/Ninja/ninja.exe"
      } },
    { "name": "windows-debug",   "inherits": "windows", "cacheVariables": {
      "CMAKE_BUILD_TYPE": "Debug", "FLOWKEYD_BUILD_TESTS": "ON" } },
    { "name": "windows-release", "inherits": "windows", "cacheVariables": {
      "CMAKE_BUILD_TYPE": "RelWithDebInfo", "FLOWKEYD_BUILD_TESTS": "OFF" } }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "windows-debug" },
    { "name": "release", "configurePreset": "windows-release" }
  ],
  "testPresets": [
    { "name": "debug", "configurePreset": "windows-debug", "output": { "outputOnFailure": true } }
  ]
}
```

> **release 用 `RelWithDebInfo`（而不是 `Release`）。** 理由：release 走的是
> 另一条优化路径，能暴露 debug 看不到的警告；但同时保留符号，崩溃时能看栈。
> 如果项目所有者更想要 `Release`，改一行即可，但要在本文件里记一笔。

### 每个任务都要跑的（DoD）

```powershell
# 注意：命令行上不要直接拼中文；下面这些命令都是纯 ASCII
$C = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'

# 首次（两个 profile 各 configure 一次）
& $C --preset windows-debug
& $C --preset windows-release

# 每个任务都要跑这两条（两条 profile 都必须编译通过）
& $C --build --preset debug
& $C --build --preset release

# 单元测试：只跑 debug 那一份（release 里根本不构建测试目标，见工作约定第 2 条）
& ctest --test-dir build/windows-debug --output-on-failure

# release 构建已经顺手产出了发布目录（只有 exe + Qt/MinGW 运行时，拷走就能跑）
dir build\dist-release

# 任务收尾（上面全绿之后，见工作约定第 11 条）：
#   * build\dist-release 已经是最新发布包（release 构建的 POST_BUILD 自动产出），
#     不需要往任何安装目录部署；
#   * 常驻实例若正在从 build\dist-release 跑（会锁住那个 exe），先 --quit、
#     构建、再重新拉起，让它跑在新构建上，启动时会自动把计划任务刷新成这个路径。
& build\dist-release\flowkeyd.exe --quit
& $C --build --preset release
Start-Process build\dist-release\flowkeyd.exe
```

**任务收尾只需要保证 `build/dist-release/` 是最新发布包**（工作约定第 11 条）：
release 构建的 `POST_BUILD` 会自动把干净的发布产物写到那里（只有 `flowkeyd.exe`
与它需要的 Qt/MinGW 运行时，没有 `CMakeCache.txt`/`build.ninja`/`*.a`/`tst_*.exe`），
所以「部署编译后的文件、不带无关文件」不需要任何脚本。**旧的“镜像到
`C:\Program Files\flowkeyd`”方案已经删掉**（`install.ps1` 不复存在）。
常驻实例从 `build/dist-release` 跑的时候，重建前要先 `--quit`（正在跑的 exe 锁着），
构建完再拉起；`ninja: no work to do` 的纯文档任务什么都不用做。

**debug 与 release 两个 profile 都必须编译通过，这是每个任务（包括纯文档任务）
的硬性要求。** 理由：release 走的是完全不同的优化与链接路径
（`-O2` + LTO 若开启），只编译 debug 会漏掉只在一侧出现的警告；反过来，
debug 构建也是发现未初始化变量、迭代器失效这类问题的便宜手段。

**但单元测试只在 debug 上跑**（项目所有者 2026-09 二次拍板；此前是「只在
release 上跑」）：release 是**发布 profile**，`FLOWKEYD_BUILD_TESTS=OFF`，
里面**不构建任何测试目标**，所以 `build/windows-release` 里不会出现 `tst_*.exe`、
测试用的 `*_autogen`、`CTestTestfile.cmake`、测试日志这些东西。测试一律
`ctest --test-dir build/windows-debug`；`scripts/acceptance.ps1` 仍然只跑
release 的产物（`-Exe build\windows-release\flowkeyd.exe`，脚本的默认值），
因为它验证的是真正要发布的那个 exe —— 它与 `build/dist-release/flowkeyd.exe`
是同一个二进制（release 构建时拷过去的）。**零警告、零失败。**

**发布版本以 `build/dist-release/` 为准**，它在 release 构建时自动产出
（`POST_BUILD` 里的 `windeployqt`）：里面只有 `flowkeyd.exe`、Qt6/MinGW 的 DLL、
QML 模块与插件，没有 `CMakeCache.txt`/`build.ninja`/`*.a`。**这就是可以直接
拷贝到别的机器上运行的发布包**：目标机器不需要装 Qt，把整个目录拷过去、
双击 `flowkeyd.exe` 即可。调试期想用构建树里的那一份也行
（`build/windows-release/flowkeyd.exe`），那条部署命令没有去掉。

因为 Qt 项目是编译型 + 链接型，**改动 `vendor/lua` 的构建参数、Win32 声明、
QML 模块注册之后，两条 profile 都要重新全量构建一次**。

### 运行（开发期一定要带这两个开关）

```powershell
# 手工冒烟：用一次性配置，避免打扰常驻实例，见工作约定第 9 条
& build/windows-debug/flowkeyd.exe --no-elevate --allow-multi --console --config .\tmp\smoke.lua
```

* `--no-elevate`：`--check` 之类离线命令本来就不提权，但**守护进程模式**
  默认会提权（`ShellExecuteW("runas")`）。开发期一律关掉，免得每次弹 UAC。
* `--allow-multi`：跳过单实例检查，方便同时开好几个试验实例
  （注意：每个实例都会装一个 `WH_KEYBOARD_LL` 钩子，**后装的先收到事件**，
  所以调试时只开一个真正需要吞键的实例）。
* `--console`：保留控制台输出（见第 7 节的 `AttachConsole` 那一条）。

**离线命令（绝不允许提权）**：`--check` / `--list` / `--list-keys`。
提权判断必须在这些命令 `return` 之后。`--quit` 也不提权（它只去通知一个
已经在跑的实例），但它会碰另一个进程，所以不算离线命令（第 14 节）。
`--remove-autostart` 也不在离线命令里：它要管理员权限才能删计划任务。

**常驻实例与开发实例是分开的**：日常那个由计划任务拉起，任务指向**上一次
以“常驻方式”启动的那个 exe 路径**（守护进程启动时自注册 / 刷新，见第 2 节第 10 条）；
开发 / 冒烟一律用 `build/...` 里的一次性实例 + `--no-elevate --allow-multi`
+ 一次性配置 —— 这两个开关也保证它**不会**去碰那个真实的自启任务。
两者的单实例锁按**配置文件路径**分开，互不影响。
常驻实例会锁住它自己那个 exe；从 `build/dist-release` 跑的时候就按工作约定
第 11 条先 `--quit` 再重建。

> 从 2026-09 起，构建目录里就已经有 Qt 与 MinGW 的运行时 DLL（构建后自动跑
> `windeployqt`，见第 4 节末），所以上面这些命令**不再需要手动把 Qt 的 `bin`
> 加进 `PATH`**；直接 `build/windows-release/flowkeyd.exe --check` 就行。
> 双击 `flowkeyd.exe` 也能启动（不带参数 = 守护进程 + 默认配置 + 弹一次 UAC）。
> 要交付/换机器就用 `build/dist-release/`（同一个 exe，旁边只有运行所需的文件）。

### 桌面行为怎么验证（每次动到钩子/引擎/分发/窗口后端都要过一遍）

清单在下面（12 条），**从阶段 9 起有了自动化版本**：

```powershell
# 116 项检查，约三分钟，会持续注入按键/抢焦点；按工作约定第 6 条先提醒用户
# 只跑 release 那一份产物（见工作约定第 2 条，脚本默认 -Exe 就是它）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1
powershell.exe ... -Phase config                              # 只看配置，不注入按键
```

它自己生成一份**一次性配置**（在 `-WorkDir`，默认 `%TEMP%\flowkeyd-accept`），
起一个非提权的守护进程，从另一个上下文用 `SendInput` 注入按键，再用一个获得了
焦点的 WinForms 窗口从外面观察“按键到底有没有到达前台”（未绑定的键做正对照，
所以“焦点没拿到”不会被误会成“吞键成功”）。它需要交互式桌面会话，
**永远不进 `ctest`**。

要让注入的按键触发绑定，守护进程得用 `FLOWKEYD_ACCEPT_INJECTED=1` 启动：
那是**只给测试用的后门**（`core/engine.cpp` 一直会丢弃 `event.injected`，
改的是 `hook.cpp` 里给它赋值那一步），启用时日志里有一条警告。
flowkeyd 自己注入的按键带着 `"FLOW"` 标记，
在钩子回调的第一步就被丢掉，所以抬升过滤不会让重映射自己喂自己 ——
**不变量 2 没有被放宽**。

脚本**不做**的：动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程、
“托盘图标真的消失了”的直接观察，以及**一切电源动作**（工作约定第 10 条：
测试里不许真的关机/重启/注销/睡眠/休眠/锁定/关屏）。这几项仍然只能靠人的手。

**桌面被锁住时（`LogonUI` 进程在跑）脚本必然挂**：`GetForegroundWindow()` 返回 0，
`SendInput` 报 `5`（ACCESS_DENIED），表现为“捕捉窗口拿不到焦点”。这不是产品 bug，
先去把机器解锁再跑。

用一份只含被测绑定的**一次性配置**（快捷键一律避开用户真实配置里已有的和弦）：

1. `--check --config tmp/smoke.lua` 通过，`--list` 打印的形状与 README 里记录的一致。
2. 单实例：开两个不用 `--allow-multi` 的实例，第二个会**先弹「已经在运行」的
   原生提示框**（这一步在 UAC 提权之前），点确定后以退出码 1 退出；
   加 `--no-prompt` 时只记日志、直接退出（`scripts/acceptance.ps1` 用它避免
   卡在对话框上）。
3. 吞键：临时用 PowerShell 起一个能显示收到按键的窗口（记事本即可），
   按被测和弦，**前台窗口不应该收到那个键**；用一个未绑定的键做正对照
   （正对照收不到就说明焦点没拿到，本次验证无效 —— 别把“焦点压根不在”
   当成“吞键成功”）。
4. 被吞掉的 `Win+…` 和弦：按完之后 Windows **不能**弹出开始菜单/搜索；
   按住 Windows 键超过自动重复延迟再松开，**仍然不能**弹（这是遮断标记
   必须挂在修饰键 key-up 上的原因，见不变量 9）。
5. 自动重复：按住和弦键 2 秒，动作只执行一次（日志里只出现一条）。
6. 重映射：`remap{ from = "CapsLock", to = "Esc" }` 之后按 CapsLock，
   前台窗口收到 `Esc` 而不是 CapsLock。
7. `send` 动作：`Ctrl+Alt+T -> send("^{c}")` 时前台收到的是 `Ctrl+C`
   而不是 `Ctrl+Alt+C`（修饰键释放逻辑）。
8. `suspend`：挂起之后所有绑定都不触发，**suspend 自己那条仍然可用**；
   恢复后一切照旧。
9. `reload`：改配置里的某个 `send` 文本，`reload` 之后按下生效。
10. `quit`：托盘图标消失、钩子卸掉（之后按键全部正常工作）、进程干净退出。
11. `window` 动作：启动 → 激活 → 再按一次收起（`toggle` 默认开）→ 再按恢复。
12. 按顺序做完以上之后，**检查没有任何按键卡在按下状态**（乱按几下、
    看是否有键一直“粘住”）。
13. `window_rule`：用一次性配置写一条规则（`process` + `monitor` + 可选的
    `desktop`），启动一个匹配的窗口，确认它出现在指定的显示器上（默认最大化）
    与虚拟桌面上；再用 `DisplaySwitch.exe /internal` → `/extend` 制造一次
    “显示器重新接入”，确认日志里出现 `monitor connected: ...; re-applying
    window rules` 且手工挪走的窗口被摆回。别忘了最后 `/extend` 恢复桌面。
14. 跨桌面唤起（带了 `desktop` 的 `window_rule` 的程序）：按一下那个程序自己的
    快捷键（如 `Win+3`），**视图应该切到它那一张桌面、窗口拿到前台**，一次到位；
    再按一下才是收起（`toggle`）。日志里应有 `view -> desktop N/M`，
    而启动 / 显示器重新接入那两遍**不应该**出现它。

---

## 6. 架构与线程模型

```
        ┌─────────────────────────── Qt GUI 线程（主线程）──────────────────────────┐
        │ QApplication + QQmlApplicationEngine                                       │
        │   * QSystemTrayIcon（右键菜单、气泡提示、悬停提示）                          │
        │   * 日志窗口（QML，FluentWinUI3）：LogModel 尾随日志文件                     │
        │   * menu 选单 / help 帮助（QML 窗口）：MenuModel / HelpModel                  │
        │ 收到动作结果 → 只做“建/前置窗口”这一件事，绝不执行动作                       │
        └───────▲──────────────────────────────────────────────┬────────────────────┘
                │ 队列信号（Qt::QueuedConnection）              │ 用户选择 → 回投任务
                │                                              ▼
        ┌───────┴──────────────────────┐   PostThreadMessage  ┌──────────────────────┐
        │ 钩子线程（自己的 Win32 消息   │◄─────────────────────│ 动作工作线程          │
        │ 循环 + SetTimer）             │  WM_APP 控制消息      │ (QThread)            │
        │  * WH_KEYBOARD_LL 回调        │                      │  run/send/open/...   │
        │  * core::Engine（状态机）      │──── 任务队列 ───────►│  window/volume/...   │
        │  * 内联注入重映射按键          │                      │  clipboard/power/... │
        │  回调必须极快返回（300 ms）    │                      └──────────────────────┘
        └──────────────────────────────┘
```

三线程的分工（理由如下）：

1. **钩子线程**：`LowLevelHooksTimeout` 默认 300 ms，慢回调的钩子会被 Windows
   **静默卸掉**（守护进程看起来还活着，却什么都不做）。所以回调只做
   「解码事件 → 问引擎要决定 → 注入重映射按键（那几条 `SendInput` 是预先拆好的）
   → 把动作排给工作线程」，然后立刻 `CallNextHookEx`。
   定时器（长按重复、挂起超时）也在这个线程上，用 `SetTimer` + `WM_TIMER`。
2. **动作工作线程**：`run`/`open`/`wait` 都会阻塞几十毫秒到几秒，绝不能在
   钩子线程上做。它也是 `menu`/`help` 窗口的“请求方”——但**窗口本身**由
   Qt GUI 线程创建，用户选中之后把“选了第几项”回投给它执行。
3. **Qt GUI 线程**：托盘与全部 QML 窗口。Qt 要求 `QWindow`/`QQuickItem`
   只在 GUI 线程上碰；跨线程一律走队列信号。

**钩子为什么不装在 Qt 主线程上**：QML 渲染的一次慢帧、以及 `QSystemTrayIcon`
的菜单弹出（它会跑自己的模态循环）都可能让主线程几十毫秒不回来；
300 ms 的预算经不起这种抖动。独立线程还有第二个好处：钩子的生命周期
（装/卸）与 Qt 事件循环的生命周期解耦，`quit` 时能确定地先卸钩子再退循环。

**通信与同步**：

* 钩子线程 → 工作线程：**已定：`QMetaObject::invokeMethod(dispatcher, ...,
  Qt::QueuedConnection)`**（`app::Dispatcher` 是 `QThread` 上的 `QObject`）。
  选它是因为 `app/` 层本来就与 Qt 绑定，少一层自己写的同步；
  钩子回调里那次调用只做入队，不会阻塞。
* 工作线程 → GUI 线程：`Qt::QueuedConnection` 信号（`emit showMenu(...)` /
  `emit showHelp(...)` / `emit showLogWindow()`）。
* GUI 线程 → 工作线程：用户在弹窗里选了某一项，回调里 `emit` 一个
  “执行第 N 项动作”的信号。
* 托盘/CLI → 钩子线程：`PostThreadMessage(hookThreadId, WM_APP_*, ...)`。
  **注意 `PostThreadMessage` 在目标线程还没建消息队列时会失败**，
  钩子线程必须在开始工作前先 `PeekMessage` 一次建出队列，并把
  “队列已就绪”通过一个 `std::promise`/原子量告诉启动方。
* 跨线程共享的配置：`Compiled` 用 `std::shared_ptr<const Compiled>` 交给
  钩子线程与工作线程各持一份，**reload 时整体替换指针**，绝不做“就地改”。

---

## 7. 不变量 —— 不要破坏这些

这些都是踩过坑之后定下来的规矩：

1. **钩子回调必须快速返回。** 不要在回调里 sleep、写慢速日志、做 IO 或执行动作。
   工作排给工作线程。
2. **绝不对注入输入作出反应。** 用 `LLKHF_INJECTED` 判定；带自己标记
   （`dwExtraInfo` 里的 **`"FLOW"`**）的事件在任何状态被触碰之前就丢弃。
   **新增任何注入路径都必须打上标记**（包括 `ModifierGuard` 里那次
   “先松开 Win/Alt 再按回去”的注入）。
3. **重映射的按键内联注入，动作不是。** 与物理按键流的顺序对重映射很重要；
   因此重映射里的 `{Sleep}` 会被忽略（`--check` 要给警告）。
4. **绝不能让按键留在按下状态。** 任何丢弃重映射的路径（挂起、重载、退出）
   都必须注入释放操作。
5. **不要跨 `SendInput` 持有锁。** 钩子状态放在钩子线程自己拥有的对象里
   （只有那个线程碰它），用“没有锁”的方式保证安全。**不要给它再套一层锁。**
6. **配置错误必须可操作。** 每条校验信息都要指出出错的快捷键
   （用 `name` 或序号）和出错的值，格式固定为：
   `hotkey #3 (\`terminal\`): unknown field \`nope\`, expected one of \`name\`, \`keys\`, ...`。
   **未知字段一律报错**（C++ 侧要自己实现严格白名单 —— 这是本项目最容易做漏的
   一条，因为 C++ 没有 serde 的 `deny_unknown_fields`）。
   **动作表顶层是唯一的例外**：内部标签枚举 + `untagged` 的写法做不到，
   所以 `window("activate", { togle = false })` 里的拼写错误会被忽略
   —— 要在 README 的已知限制里写明。
7. **`core/` 与 `lua/` 保持无 Win32、无 GUI。** 这是核心逻辑无需桌面就能测试的前提。
8. **固定结构体布局是契约。** `INPUT` 在 x64 上必须 40 字节
   （`MOUSEINPUT` 是联合体中最大的成员）。用
   `static_assert(sizeof(INPUT) == 40, "INPUT layout")` 盯着它。
9. **被吞掉的 `Win+…`/`Alt+…` 和弦必须在修饰键 key-up 时注入标记按键
   （`VK 0xE8`，未分配键）。** 跟着和弦键按下一起注入是**不够**的：
   Windows 键的自动重复会在两者之间重新武装外壳，于是松开时照样弹出开始菜单。
   实现照抄 `Engine::mask_menu_key_up`：被吞掉的和弦设置一个标志，
   而**下一次** Win/Alt 的 key-up（它是被转发的）先注入标记。
   在钩子回调里做这件事正是让顺序安全的原因。
10. **`send`/`type` 动作的 `release_modifiers` 也要做同样的菜单遮断。**
    `ModifierGuard` 注入的真实 key-up 引擎看不到，必须在注入层自己插一次
    `VK_UNASSIGNED` 空按键。
11. **提权只发生在守护进程模式，且必须能降级。**
    `--check`/`--list`/`--list-keys` 绝不能弹 UAC；UAC 被拒绝时打 warning
    继续以普通权限运行，而不是静默退出。提权重启时要转发完整命令行
    （含相对路径的 `--config`）并把工作目录一并传过去。
    **本机实测：用户点“否”时 `ShellExecuteW("runas")` 返回的不是 1223 而是 5
    （拒绝访问）**，所以降级路径不能只判 `ERROR_CANCELLED`。
12. **`--console` 之外，守护进程模式总是打开默认日志文件**
    （`%USERPROFILE%\.config\flowkeyd\flowkeyd.log`，目录要 `create_dir_all`）。
    日志窗口尾随的就是那个文件；离线命令仍然不碰它。
13. **虚拟桌面与音频的 COM 单元模型不能混。** 音频要 **MTA**、
    虚拟桌面（shell 接口）要 **STA**，所以 `desktop` 的每次调用都丢进一条
    **一次性的 STA 线程**（`CoInitializeEx(COINIT_APARTMENTTHREADED)` +
    调用 + `CoUninitialize`）。不要让工作线程的单元模型取决于哪个后端先初始化。
    另外 **Qt 在 Windows 上会为 OLE/拖放把主线程初始化成 STA**，别去改它。
14. **Lua 状态绝不越出 `lua_config` 的求值函数。** 求值完就把
    `lua_State` 关掉，把结果转成普通的 C++ 结构。**`Compiled` 必须与 Lua 无关**
    （钩子回调与工作线程永远碰不到 Lua），所以不要往 `Config`/`Binding`/`Action`
    里塞 `lua_State*` 或 Lua 注册表引用。
15. **动作只能是声明式的表或简写字符串。** `action = function() end` 要由
    `lua_prelude.lua` 里的 `reject_functions` 明确拒绝。要让用户能写“自定义逻辑”，
    就加字段/构造器，而不是放行函数。
16. **配置文本先剥掉 UTF-8 BOM 再交给 Lua。** 记事本与
    `Set-Content -Encoding UTF8` 都会写 BOM，而 Lua 的词法分析器不认它
    （第 1 行报 `unexpected symbol near '<\239>'`）。要有单测盯着。
17. **弹窗窗口必须能拿到键盘焦点。** 钩子守护进程通常**不持有前台锁**，
    只调 Qt 的 `Window::requestActivate()` / `SetForegroundWindow` 会被拒绝，
    于是窗口弹出来了却收不到键盘 —— 一个只在“用户正聚焦在别的应用”时才出现的
    bug。做法是 `requestActivate()` → `SetForegroundWindow` → `AttachThreadInput`
    到持有锁的线程再重试 → `BringWindowToTop` + `SetWindowPos(HWND_TOP)`，
    **每条路径上 `AttachThreadInput` 都要配平**。
18. **模态/前台相关的一切都要在 Qt GUI 线程上做。**
    `QSystemTrayIcon`、`QQuickWindow`、`QMenu` 都不能跨线程。
19. **DPI 感知必须在创建任何窗口之前就绪。** 好消息是 **Qt 6 在 Windows 上
    默认就是 Per-Monitor DPI Aware V2**，不需要我们手动调
    `SetProcessDpiAwarenessContext`。所以这条变成了“**不要在
    `QApplication` 构造之前创建窗口/触碰屏幕 API**”，以及如果要改缩放策略，
    必须在构造 `QApplication` **之前**调
    `QGuiApplication::setHighDpiScaleFactorRoundingPolicy()`。
    不要为了省事去调 `SetProcessDPIAware()`（会把 Qt 的设置打乱、字发糊）。
20. **日志窗口是一个普通窗口，不是独立进程。** 关掉它不能退出应用：
    构造 `QApplication` 之后立刻 `setQuitOnLastWindowClosed(false)`，
    退出只走 `quit` 动作 / 托盘菜单 / `WM_CLOSE` 到隐藏的“主”窗口。
    （Qt 窗口没有真控制台那个「点叉就给守护进程发 `CTRL_CLOSE_EVENT`」的问题，
    所以日志窗口可以直接跑在同一个进程里。）

---

## 8. Lua 5.5.1 的注意点（已实测）

`vendor/lua` 是 **v5.5.1**（`lua/lua`，commit `7579fc9d`，`describe` 出来就是
`v5.5.1`）。配置 DSL 只用到最基础的表/字符串/数字/循环，所以与 Lua 5.4
相比差异不大，但下面这几条**已经确认**、集成时会直接撞上：

1. **`luaL_openlibs` 现在是宏**，展开成
   `luaL_openselectedlibs(L, ~0, 0)`；原来的函数签名换成了
   `luaL_openselectedlibs(lua_State *L, int load, int preload)`，配
   `LUA_GLIBK`/`LUA_LOADLIBK`/`LUA_COLIBK`… 这些位掩码常量。
   **调用 `luaL_openlibs(L)` 仍然可用**（宏还在 `lauxlib.h` 里），
   想只开一部分库才需要新 API。
2. **`lua_newstate` 多了一个 `unsigned seed` 参数**：
   `lua_State *lua_newstate(lua_Alloc f, void *ud, unsigned seed)`。
   用 `luaL_newstate()` 就没这个问题（我们就是用它）。
3. **`lua_resume` 的签名是 `(L, from, narg, nresults, ...)`**；
   `lua_resetthread` 是宏（`lua_closethread(L, NULL)`）。
   本项目不做协程，只记一笔。
4. 新增了 `lua_closethread`、`lua_closeslot`、`lua_toclose`（to-be-closed 变量）。
   配置脚本理论上能用 `__close`，但不影响我们。
5. 我们**用到**的 API 全部确认存在（实测 grep）：
   `luaL_newstate`、`luaL_openlibs`、`luaL_loadbufferx`、`luaL_loadstring`、
   `lua_pcallk`（宏 `lua_pcall`）、`luaL_checkversion`、`luaL_traceback`、
   `luaL_requiref`、`lua_getglobal`/`lua_setglobal`、`lua_rawget(i)`/`lua_rawset`、
   `lua_getfield`/`lua_setfield`、`lua_geti`/`lua_seti`、`lua_next`、
   `lua_gettop`/`lua_settop`、`lua_createtable`、`lua_tolstring`、
   `lua_pushlstring`、`lua_tointegerx`/`lua_tonumberx`、`lua_isinteger`、
   `lua_toboolean`、`lua_type`/`lua_typename`、`lua_pushcclosure`、
   `lua_error`、`lua_atpanic`、`lua_newuserdatauv`。
   编译：`vendor/lua` 的 32 个 `.c` 用 `-O2 -DLUA_USE_WINDOWS` 全部通过。
6. **Lua 侧的行为差异**要留意的两点（写配置示例时别踩）：
   * `repeat` 仍然是 Lua 关键字 → 字段名用 `repeatable`（配置里要认
     `["repeat"]` 这个别名，见第 14 节）。
   * 字符串里的 `\t`/`\n`/`\r` 是**合法但致命**的转义：
     `open("C:\tools")` 会**静默**变成 `C:` + TAB + `ools`、不报错。
     Windows 路径一律用长字符串 `[[C:\path\app.exe]]`。
7. **`lua_newuserdatauv`（而不是旧的 `lua_newuserdata`）** —— 5.4 起就是它，
   如果以后要给配置脚本暴露 C++ 对象，用这个。

> **把 Lua 5.5 的版本号写进 `--version` 与 `--check` 的输出**，
> 这样出问题时能一眼看出是哪一份 Lua。

---

## 9. 实施阶段计划

每个阶段都**可运行、可提交**，并且都要满足第 5 节的 DoD（双 profile + 测试全绿）。
阶段之间不要提前实现后面阶段的东西（尤其是 UI 与高风险 COM 后端）。

**进度表**（每次做完一个阶段就更新这里，详见各阶段的「已完成」小节）：

| 阶段 | 状态 | 备注 |
| ---- | ---- | ---- |
| 0 仓库与构建骨架 | **已完成** | 双 profile 绿、`ctest` 绿、托盘 + 日志窗口冒烟过 |
| 1 纯逻辑核心 | **已完成** | `src/core/*` + 6 个 Qt Test 目标全绿 |
| 2 Lua 配置层 | **已完成** | `flowkeyd_lua` 静态库 + `--check`/`--list` 可用、`tst_lua` 25 项全绿、`flowkeyd.lua.example` 能过 `--check` |
| 3 Win32 基础设施 + 钩子 + 引擎接线 | **已完成** | `flowkeyd_platform` 静态库 + 钩子线程/动作线程；`tst_layout|input|command_line|instance` 全绿；手工冒烟见阶段 3 小节 |
| 4 托盘 + 日志窗口 | **已完成** | 完整托盘菜单 + `core/log_tail`/`app/log_model` 尾随模型 + `LogWindow.qml` 真渲染；`tst_log_tail` 全绿；日志窗口渲染/尾随已截图验证 |
| 5 窗口动作 + 剪贴板 + 音量/媒体 | **已完成** | `core/window_match`、`platform/win/dwm|window|clipboard|audio`、dispatcher 接线与 `{selection}`；`tst_window_match`/`tst_audio` 全绿；真实桌面后端由 `tst_interactive` 验证 |
| 6 弹窗 `menu` / `help` | **已完成** | `flowkeyd_models` + 两张 QML 卡片；`tst_menu_model`/`tst_help_model` 全绿；渲染/筛选/键盘选择由 `tmp/preview` 验证；帮助窗口的列表后来（2026-09）改成了 Qt 自带的 `ListView` + `ScrollBar` |
| 7 虚拟桌面 + 电源 | **已完成** | `platform/win/desktop|power` + dispatcher 接线；`tst_desktop_table`/`tst_power_table` 全绿；真实 COM 探测/切换与关屏由 `tst_interactive` 验证 |
| 8 示例配置 + README | **已完成** | 覆盖全特性的 `flowkeyd.lua.example`（37 hotkey / 3 remap，`--check` 零警告）；`README.md` 已写全 |
| 9 验收（无 e2e 的替代） | **已完成** | `scripts/acceptance.ps1`（77 项检查：68 项原样 + 9 项弹窗滚轮回归，需交互式桌面）+ `FLOWKEYD_ACCEPT_INJECTED` 测试后门；debug 跑 3 遍、release 跑 2 遍全绿 |
| 10 接管 | **已完成** | 真实配置已迁到 `.config\flowkeyd\config.lua`（24 hotkey / 0 remap，零警告）；**2026-09 起常驻也有了自启**：计划任务 `flowkeyd` 指向**当前运行的 `flowkeyd.exe`**（由守护进程启动时自注册，不再有安装目录/部署脚本，见第 2 节第 10 条） |

### 阶段 0：仓库与构建骨架

**状态：已完成（2026-09）。**

**做什么**

1. 删除 `mainwindow.h` / `mainwindow.cpp` / `mainwindow.ui`；
   重写 `main.cpp` 为 `QApplication` + `QQmlApplicationEngine` +
   `QSystemTrayIcon` + **`AttachConsole(ATTACH_PARENT_PROCESS)`** 的最小可运行壳
   （托盘图标 + 一个空的日志窗口能弹出来）。
2. 写 `CMakeLists.txt`（`qt_standard_project_setup()`、
   `qt_add_executable`、`qt_add_qml_module`、`find_package(Qt6 … Quick QuickControls2 Widgets Test)`）
   与 `CMakePresets.json`。
3. 写 `cmake/VendorLua.cmake`，把 `vendor/lua` 编成静态库并链进 exe；
   在 `main` 里 `luaL_newstate()` + `luaL_openlibs()` + 关掉，证明能跑。
4. 补 `.gitmodules`，把 `vendor/lua` 正式登记为 submodule（指向 `v5.5.1`）。
5. 立起 `tests/` 目标与一个 smoke 测试（例如 `keys` 的表往返，或纯 C++ 的版本串）。
6. 更新 `.gitignore`（`build/` 已经在里面了；补 Ninja/CMake 产物）。

**验收**：debug/release 双构建绿；`ctest` 绿；`flowkeyd --help` 在终端里
打出中文帮助（这一步同时验证了 `AttachConsole`）；托盘图标可见，日志窗口能弹出、
关掉不退出进程。

**已完成的内容**（实际落地的东西，后续阶段直接接着用）：

* `CMakeLists.txt` + `CMakePresets.json`（`windows-debug` / `windows-release`，
  产物在 `build/windows-debug`、`build/windows-release`）+ `cmake/VendorLua.cmake`。
  仓库范围内打开 `-Wall -Wextra -Werror`（`flowkeyd_strict_warnings()`），
  `vendor/lua` 例外。
* 静态库 `flowkeyd_core`（`src/core/*`）与可执行文件 `flowkeyd` 分开，
  测试直接链 `flowkeyd_core`。
* **老仓库根目录那份 Qt Creator 模板 `main.cpp` 也删掉了**，入口在 `src/main.cpp`
  （与第 4 节的代码地图一致）。
* `src/platform/win/console.{h,cpp}`：`attachParentConsole()` + `writeStdout/writeStderr`
  （真控制台走 `WriteConsoleW`，重定向走 UTF-8 `WriteFile`）。
* `src/cli.{h,cpp}`：全部 CLI 开关（中文帮助 + 英文错误）、
  `commandLineArguments()` 走 `GetCommandLineW`（不受代码页影响）。
* `src/platform/win/tray.{h,cpp}`（左键 = 日志窗口、右键 = 查看日志/退出）、
  `src/app/log_window.{h,cpp}`（懒创建的 QML 窗口）+ `src/qml/LogWindow.qml`。
* `main.cpp`：离线命令在 `QApplication` 之前 `return`（不变量 11）；
  之后 `setQuitOnLastWindowClosed(false)` + `QQuickStyle::setStyle("FluentWinUI3")`。
* `.gitmodules` 指向 `https://github.com/lua/lua`（`vendor/lua`）。
* `.gitignore` 补了 `/tmp/`。

**手工验证结论**：

* `flowkeyd --version` / `--list-keys` / `--help` 都能打出内容（用
  `Start-Process -Wait -RedirectStandardOutput` 验证，见第 10 节的坑）；
  未知参数返回 2 并打印中文帮助。
* 常驻实例：`--no-elevate --allow-multi --console --log-window` 起得来，
  日志窗口标题是 `flowkeyd 日志`；**关掉窗口进程仍然活着**；
  `taskkill /PID <pid>`（不带 `/F`）能干净退出。
* 托盘图标的「左键打开日志窗口 / 右键退出」按第 5 节的清单由人工确认。

### 阶段 1：纯逻辑核心（无 FFI、无 GUI）

**做什么**：`src/core/keys`、`config`、`engine`、`template`、`action`。
包括那些非显然的细节：

* 通用修饰键 `VK_SHIFT` 与分侧 `VK_LSHIFT`/`VK_RSHIFT` 的 `same_key` 处理。
* 小键盘 Enter 的内部伪码（`0x100`）+ `key_from_hook(vk, extended)` /
  `native_key(vk)`；`NumpadSub`/`NumpadAdd` 是独立 `VK`，不需要伪码。
* `exact_modifiers = true` 时把和弦自身的修饰键位先减掉再比较
  （否则 `keys = "Ctrl+Shift"` 永远不触发）。
* `parse_key_or_script`：`to = "Esc"` 是**键名**而不是脚本（否则会变成 E、s、c）。
* AHK 发送脚本解析：`^{c}`、`{Enter}`、`{Esc 3}`、`{Enter down}`、`{{}`、
  `{Text}hello`；`!^+#` 是前缀不是字面量；`{!}` 映射到 Shift+1；
  `split_hold` **不能反转尾部**。
* 引擎：优先级、吞键、自动重复抑制（第二次及之后的 key-down 不重新匹配）、
  长按重复（`WM_TIMER` 驱动）、挂起、重映射 hold/tap、`mask_menu_key_up`。
* 模板：`{clipboard}`/`{selection}`/`{hotkey}`/`{name}`/`{date}`/`{time}`/
  `{datetime}`/`{timestamp}`/`{unix}`/`{config_dir}`/`{exe_dir}`/`{cwd}`/
  `{temp}`/`{env:NAME}`/`{{`/`}}`；**未知占位符原样保留**。
* 配置编译：命令式注册与声明式 `return {...}` 两种写法（后者排在后面）、
  `keys` 支持单个和弦或一组、`trigger` 三态、`repeatable`、
  `enabled`/`comment`/`swallow`、`menu` 条目校验（重名、空标签）、
  `--check` 的警告（例如重映射里的 `{Sleep}`）。

**验收**：Qt Test 覆盖以上每一条。双构建绿。

**状态：已完成（2026-09）。**

**已完成的内容**：

* `src/core/keys.{h,cpp}`、`action.{h,cpp}`、`config.{h,cpp}`、`engine.{h,cpp}`、
  `template.{h,cpp}`。全部只用 QtCore，**没有** `<windows.h>`、**没有** QML/Widgets。
* `Action` 用「一个结构体 + `Kind` 判别式」而不是 `std::variant`：
  `menu` 的递归（Menu → MenuItemDef → ActionSpec → Action）天然成立，
  也省掉了 20 个包装结构体与到处 `std::visit` 的噪音。
  各字段与 `README.md` 里那一张动作表一一对应。
* `ActionSpec` / `Settings` / `HotkeyDef` / `RemapDef` / `Compiled` / `ConfigError`
  都在 `core/config.h`；动作的**摘要文本**（`--list` 与 `help()` 的显示）在
  `core/action.h`。
* 校验：未知（`--check` 之外的）字段由**阶段 2 的 Lua 层**逐字段核对
  白名单（C++ 没有 `deny_unknown_fields`，见不变量 6），动作表顶层是例外。
* 这一阶段**不经过 Lua**：`loadConfig(Evaluator, path, out)` 把求值回调当参数，
  阶段 2 只要传一个真的 Lua 求值器进来即可；BOM 剥离、`.toml` 拒绝、
  候选路径与 `pickConfigPath()` 已经在本阶段实现并有单测（用一个假求值器）。
* 测试：`tests/tst_smoke|keys|send_script|config|engine|template.cpp`，
  共 6 个 Qt Test 可执行文件；配置直接在 C++ 里搭（`tests/helpers.h`）。
* **`tst_layout` 推迟到阶段 3**：它要盯的是 `platform/win/ffi.h` 里
  `static_assert(sizeof(INPUT) == 40)`，本阶段还没有那个文件。
* 路径一律用 `QDir::toNativeSeparators()` 输出，错误信息里看到的是
  `C:\Users\…\.config\flowkeyd\config.lua`。

### 阶段 2：Lua 配置层

**做什么**：`src/lua/lua_config` + `src/lua/lua_prelude.lua`（编进 qrc）。
把阶段 1 的 `core::Config` 接到真正的 Lua 求值上：

* 建状态 → 注入 prelude（把注册表 `state` 作为 `...` 传给 prelude）→
  求值配置文件 → 把 `settings`/`hotkeys`/`remaps` 三张表逐条目转成 C++ 结构
  → 收集错误（`hotkey #3 (\`terminal\`): ...`，一次性列全）→ 关状态。
* **逐条目转，不要一次整张表反序列化**，这样错误才能带上下文。
* 空表歧义：`args = {}` 要当空列表，`env = {}` 要当空 map 并给出人话提示
  （Lua 里 `{}` 既是空列表也是空表）。
* `ActionSpec::List([])`（空的动作列表）**必须显式报错**：
  `an empty action list does nothing`。
* 尾部调用的行号坑：DSL 报错的那次调用如果是脚本的最后一条语句，Lua 会丢掉
  调用者栈帧、traceback 里没有行号。**别为了行号改结构**。
* `--check` / `--list` / `--list-keys` 三个离线命令；
  `.toml` 后缀要**明确拒绝**并给迁移提示（说明现在只认 `.lua`）。
* 配置搜寻顺序：`%USERPROFILE%\.config\flowkeyd\config.lua` → exe 同目录 →
  `%APPDATA%\flowkeyd` → 当前目录；回退到非默认位置时打 warning。
  历史位置（`~\.config\flowkeyd\config.toml` 等）只给迁移提示，不加载。

**验收**：单测覆盖两种写法、每个构造器、内联函数被拒绝、空动作列表、
BOM、`\t` 陷阱、`.toml` 拒绝、错误信息格式；`--check --config flowkeyd.lua.example`
通过（示例配置同时要写出来，中文注释、覆盖全特性）。

**状态：已完成（2026-09）。**

**已完成的内容**：

* `src/lua/lua_prelude.lua`：DSL 预置环境（`settings{}` / `hotkey{}` / `remap{}` +
  全部动作构造器 + `flowkeyd` 表），由 `qt_add_resources` 编进 `:/lua/lua_prelude.lua`。
* `src/lua/lua_config.{h,cpp}`：Lua 与 C++ 的唯一边界。用 `luaL_loadbufferx`
  以 `@<path>` 为 chunk 名求值（所以报错是 `config.lua:12: ...`），语法错加
  `syntax error: ` 前缀；逐条目转换、未知字段用白名单报错；混合表递归检查。
* CMake 里新静态库 **`flowkeyd_lua`**（`lua_config` + qrc），同时被 exe 与
  `tst_lua` 链接。静态库里的 qrc 会被链接器丢掉，因此在 `lua_config.cpp` 里
  显式调了一次 `Q_INIT_RESOURCE(lua_prelude)`。
* `main.cpp`：`--check` 打印 `PATH: OK (N hotkey(s), M remap(s))`；
  `--list` 的排版固定（`tst_lua` 之外也可肉眼比对）。
* `flowkeyd.lua.example`：覆盖全部特性的示例配置，
  37 hotkey / 3 remap，`--check` 通过、零警告。
* 测试：`tests/tst_lua.cpp`，25 个用例（两种写法一致、循环生成、
  构造器等价于手写表、错误信息带条目名/行号、
  空动作列表、空表在 map 位置、重复 `settings` 只警告、BOM、`.toml` 拒绝）。

### 阶段 3：Win32 基础设施 + 钩子 + 引擎接线

**做什么**

* `platform/win/ffi.h`（Win32 声明 + `static_assert(sizeof(INPUT) == 40)`）、
  `logging`（英文、级别、ANSI 颜色、同时写文件）、`single_instance`、
  `elevate`、`nt`（`win32u!NtUserSendInput` 等，运行时解析 + 零输入校验）、
  `input`（`SendInput` 后端 + `ModifierGuard` 的菜单遮断）。
* `hook`：**独立的钩子线程**、自己的消息循环、`SetTimer`、`PostThreadMessage`
  控制消息（`WM_APP_*`：suspend / resume / reload / quit）、
  钩子安装/卸载、`INPUT`/`dwExtraInfo = "FLOW"` 标记与过滤。
* `app/dispatcher`（工作线程）+ `app/runtime`（总装）。
* 先实现这些动作：`none`、`suspend`、`reload`、`quit`、`caps_lock`、
  `notify`、`run`、`send`、`type`、`open`。`run` 的 `show` 要注意
  **不要用 `DETACHED_PROCESS`**（会静默杀死控制台子进程，用
  `CREATE_NO_WINDOW`）。

**验收**：单测（发送脚本→注入序列、`ModifierGuard` 的顺序断言、
`quote_arg`、`single_instance` 的名字散列、结构体布局）；**手工冒烟清单
第 1–11 条**。双构建绿。

**状态：已完成（2026-09）。**

**已完成的内容**：

* 新静态库 **`flowkeyd_platform`**（`src/platform/win/*`，不碰 Qt GUI），
  被 exe 与单测共同链接；系统库（`user32`/`kernel32`/`shell32`/`ole32`/
  `advapi32`/`powrprof`）写成它的 `PUBLIC` 依赖。
* `ffi.{h,cpp}`：统一的 `windows.h` 入口 + `static_assert(sizeof(INPUT)==40)` 等，
  `monotonicMs()`、`winErrorMessage()`。
* `logging.{h,cpp}`：`HH:MM:SS LEVEL message`，ANSI 颜色可选，
  同时追加写到日志文件；英文、可断言。
* `nt.{h,cpp}`：运行时解析 `win32u!NtUserSendInput` / `NtUserGetAsyncKeyState`，
  零输入调用校验后才启用。本机实测 `auto` 会选中 `win32u!NtUserSendInput`。
* `input.{h,cpp}`：`dwExtraInfo = 0x464C4F57`（`"FLOW"`）、两条注入后端、
  `sendOps`（批量 64，钩子回调里 `allowSleep=false`）、`ModifierGuard` +
  **纯函数** `modifierReleasePlan` / `modifierRestorePlan`（后者让“菜单遮断标记的
  顺序”可以单测，不必真的注入）。
* `single_instance` / `elevate` / `process`：互斥体名按配置路径 FNV-1a 散列；
  `quoteArg` 按 `CommandLineToArgvW` 规则；降级只判 `ShellExecuteW` 返回值；
  `run` 用 `CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW`（**不用**
  `DETACHED_PROCESS`）、`open` 用 `ShellExecuteW`。
* `hook.{h,cpp}`：独立钩子线程 + 自己的消息循环（`SetTimer(nullptr, …)`）、
  `PostThreadMessage` 控制消息、`PeekMessage` 建队列后用 `std::promise` 回报就绪、
  退出时先卸钩子再释放重映射按住的键。
* `app/dispatcher.{h,cpp}`（工作 `QThread`，`QMetaObject::invokeMethod` 队列）
  与 `app/runtime.{h,cpp}`（钩子 + 工作线程 + 配置生命周期）。
* 阶段 3 的动作：`none`/`suspend`/`reload`/`quit`/`caps_lock`/`notify`/`run`/
  `send`/`type`/`open`；其余动作会写一条 `not implemented yet (later stage)` 的
  警告，不静默。
* 测试：`tst_layout`（4）、`tst_input`（6）、`tst_command_line`（5）、
  `tst_instance`（3）；合计 11 个测试目标全绿。

**手工冒烟结论（2026-09，用 `tmp/smoke.lua`、`--no-elevate`）**：

* 第 1 条：`--check` / `--list` 通过，`--check` 打印 `…: OK (4 hotkey(s), 1 remap(s))`。
* 第 2 条：第二个同配置实例退出码 1，日志 `another flowkeyd instance already owns …`。
* 第 10 条的一部分：`taskkill /PID <pid>`（**不带** `/F`）能让进程干净退出，
  日志里能看到 `keyboard hook removed`。
* 其余条目（真按键的吞键/自动重复/重映射/`send` 修饰键释放/`suspend`/`reload`/
  `window`）需要**人的手**：钩子刻意忽略注入输入（不变量 2），所以脚本无法伪造
  “物理按键”。这些条目在阶段 9 完整验收时补做；`window` 本身要等阶段 5。
  → **阶段 9 已补做**：`scripts/acceptance.ps1` 把这些条目全跑通了（靠
  `FLOWKEYD_ACCEPT_INJECTED=1` 抬升过滤；不变量 2 本身没动）。

### 阶段 4：托盘 + 日志窗口

**做什么**

* `platform/win/tray`：`QSystemTrayIcon` + 右键菜单（查看日志 / 挂起·恢复 /
  重载配置 / 打开配置文件 / 退出）+ 悬停提示显示挂起状态 + `showMessage`
  气泡；左键单击打开日志窗口。
* `app/log_model` + `qml/LogWindow.qml`：尾随日志文件（**按字节读、只消费
  能完整解码的 UTF-8 前缀、只输出以 `\n` 结尾的完整行，半行留到下一轮**）、
  250 ms 轮询、最多 1000 行、按级别配色、子串过滤、自动滚到底。
* `setQuitOnLastWindowClosed(false)`；`--log-window` 保留为
  “启动时直接打开日志窗口”。

**验收**：单测（`complete_utf8_prefix_len`、增量尾随、半行、被截断的多字节
字符、1000 行上限、过滤）；手工：托盘左键弹出、再点前置不重复开、
关掉窗口进程还活着、挂起状态在提示里正确。

**状态：已完成（2026-09）。**

**已完成的内容**：

* `src/platform/win/tray.{h,cpp}`：完整右键菜单（查看日志 / 挂起·恢复 /
  重载配置 / 打开配置文件 / 退出），悬停提示跟着挂起状态走；新增信号
  `suspendToggleRequested`/`reloadRequested`/`openConfigRequested`。
* `src/core/log_tail.{h,cpp}`：`LogTailer`（增量尾随、半行、末尾不完整的
  UTF-8 序列不消费、1000 行上限）与纯函数 `completeUtf8PrefixLen`。
  **刻意放在 `core/`**（不是 `app/`）：它只用 QtCore，是纯逻辑，能直接被
  Qt Test 覆盖。
* `src/app/log_model.{h,cpp}`：`QAbstractListModel`，角色是 `line`/`level`
  （**不叫 `text`**：会与 QML `Text.text` 撞名），250 ms 轮询、1000 行上限、
  子串过滤（大小写无关）、`visibleCount`/`totalCount`、`appended` 信号。
* `src/qml/LogWindow.qml`：`ListView` + 筛选 `TextField` + 按级别配色 +
  新行自动滚到底（用户往上翻时不打扰）+ 标题里的行数。
* `src/app/log_window.{h,cpp}`：拥有 `LogModel`，把日志路径交给它；
  `ensureWindow()` 用 `setProperty("logModel", …)` 把模型挂给 QML。
* `main.cpp`：托盘信号接上日志窗口（左键只前置不 toggle）、挂起/恢复、
  重载、`ShellExecuteW` 打开配置文件、退出；并新增
  `Runtime::reportSuspended()`（把动作触发的挂起状态投回 GUI 线程，取代了
  原来 `Dispatcher::suspendedChanged` 那条连接）。
* 测试：`tests/tst_log_tail.cpp`（9 个用例：UTF-8 前缀、增量、半行、
  截断的多字节、1000 行上限、文件轮转、缺失文件不是错误）。

**手工验证结论（2026-09，空配置 `tmp/smoke4.lua`、`--no-elevate`）**：

* 守护进程启动/退出：`taskkill /PID`（不带 `/F`）能干净退出，日志里
  `keyboard hook removed`。注意：**打开着日志窗口时 `taskkill`（不带 `/F`）
  不会退出进程** —— WM_CLOSE 只被日志窗口吃掉（不变量 20），得走托盘
  “退出”/`quit` 动作，或测试时用 `/F`。
* 日志窗口：`--log-window` 启动后 QML **零警告**加载；用
  `PrintWindow` 截取窗口本身验证：标题从 `flowkeyd 日志 — 8 行` 变成
  `— 9 行`、筛选框可见、`DEBUG` 紫色/`INFO` 默认色、往日志文件追加一行后
  **约 250 ms 内自动出现并滚到底**（截图见 `tmp/logwin-before.png` /
  `tmp/logwin-after.png`，脚本 `tmp/logwin-shot.ps1`）。
* 托盘菜单的左键/右键点击、挂起提示、重载、打开配置、退出仍需**人的手**，
  留到阶段 9。

### 阶段 5：窗口动作 + 剪贴板 + 音量/媒体

**做什么**

* `window_match`（纯函数，可单测）：`target` 标题子串 + `process` 可执行文件
  名子串（`wezterm` 也要匹配 `wezterm-gui.exe`）、跳过不可见窗口与
  有属主的窗口（`GetWindow(hwnd, GW_OWNER) != NULL`）、
  最近使用的优先、已还原优于最小化。
* `platform/win/window`：`activate`/`minimize`/`maximize`/`restore`/`close`/
  `toggle_topmost`、前台锁绕行、`launch` 回退（**不套用 `toggle`**）、
  `is_active` 要求前台**且**未最小化、`toggle` 默认开（`Option<bool>` 语义：
  只有显式写 `false` 才关）、`TransitionGuard`（RAII：设 TRUE → `ShowWindow`
  → 设回 FALSE；属性读不回来）。
* `platform/win/clipboard`（`CF_UNICODETEXT`）、
  `platform/win/audio`（Core Audio，**手写 vtable，高风险**）、`media` 键
  （`VK_MEDIA_*` 注入即可）。
* 模板里的 `{selection}`（合成 Ctrl+C + 等待 + 读剪贴板）。

**验收**：单测（matcher 的各种组合、`toggle` 的三条边界、`TransitionGuard`
的恢复语义、音量步进/钳位的纯计算）；手工：`window` 的
启动→激活→收起→恢复、`volume` 对系统音量合成器可见、`clipboard` 的
get/set/append/clear。**`animate` 的效果要靠肉眼**（没有屏幕采样脚本了）。

**状态：已完成（2026-09）。**

**已完成的内容**：

* `src/core/window_match.{h,cpp}`（纯逻辑、可单测）：`executableBaseName`、
  `windowTitleMatches`、`windowProcessMatches`（拿不到属主进程名时**不算匹配**）、
  `windowMatchesQuery`，外加两个决策纯函数：`planWindowAction`（`toggle` 边界）
  与 `windowOpHasTransition`（`animate` 对哪些 op 有意义）。
* `src/platform/win/dwm.{h,cpp}`：运行时解析 `dwmapi!DwmSetWindowAttribute`，
  按窗口设 `DWMWA_TRANSITIONS_FORCEDISABLED`；拿不到 dwmapi 只记 debug。
* `src/platform/win/window.{h,cpp}`：`EnumWindows`（跳过不可见/有属主、
  已还原优先于最小化、按 Z 序取第一个）、进程名缓存（pid → 小写 exe 名）、
  `isActive`、前台锁三级绕行的 `raiseWindow`、`TransitionGuard`（RAII）、
  `applyTo`（activate/minimize/maximize/restore/close/toggle_topmost）。
* `src/platform/win/clipboard.{h,cpp}`：`CF_UNICODETEXT` 的 get/set/append/clear，
  `OpenClipboard` 重试 10 次（另一个进程占着剪贴板是常态）。
* `src/platform/win/audio.{h,cpp}`：Core Audio 手写 COM vtable（MTA），
  `apply`/`getPercent`/`isMuted`，以及纯计算 `nextVolumeScalar`（步进/钳位）。
* `input.copySelection`：合成 Ctrl+C（先 `ModifierGuard` 松修饰键，含菜单遮断），
  等 150 ms 后由调用方读剪贴板；`{selection}` 就是它。
* `app/dispatcher`：新增 `ExpandContext`（单次触发内缓存剪贴板/选中文本，
  只在模板真的需要时才读）、`launchThenActivate`（启动 → 轮询窗口 → 激活，
  默认等 3000 ms，**不套用 `toggle`**）、以及 `window`/`volume`/`media`/
  `clipboard` 四个动作的分支（`desktop`/`menu`/`help`/`power` 仍写明“later stage”）。
* 测试：`tests/tst_window_match.cpp`（7）、`tests/tst_audio.cpp`（5）、
  `tests/tst_interactive.cpp`（需 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）。

**手工验证结论（2026-09，`tst_interactive`，已显式开启交互测试）**：

* `clipboardRoundTrip`：get/set/append/clear 往返通过，并尽量恢复原文本。
* `volumeReadWriteAndRestore`：读→设 40/60%→读回一致；`set 0` 再 `down 10`
  仍是 `0%`（钳位）；最后恢复原音量与静音状态。
* `windowBackendLaunchesActivatesAndCloses`：`runCommand` 启动记事本（唯一标题）
  → `find` 命中 → `activate` 后 `isActive` 为真 → `minimize` 后为假 →
  `restore` → `close` 后窗口消失；并显式验证了 `dwm::available()` 与
  `forceDisableTransitions(hwnd, true/false)` 的往返（`TransitionGuard` 的底层）。
* `copySelectionCopiesTheFocusedSelection`：记事本里 `Ctrl+A` 全选后
  `copySelection`，剪贴板里拿到 `SELECTME-12345`（这就是 `{selection}` 的核心）。
* **仍需人的手**：`animate = true/false` 的肉眼区别、`volume`/`media` 在真实
  前台应用上的效果，以及托盘菜单点击。
  → **阶段 9 已补做大部分**：快捷键触发的完整链路（钩子吞键 → dispatcher →
  动作）、默认开的 `toggle`、`clipboard`/`send` 的外部证据都由
  `scripts/acceptance.ps1` 覆盖了；`animate`、`volume` 的实际听感与托盘点击
  仍然靠人的手。

### 阶段 6：弹窗 `menu` / `help`（FluentWinUI3）

**状态：已完成（2026-09）。**

**做什么**

* `app/menu_model`（纯逻辑：条目几何、高亮移动、单字符 `key` 选中
  （不区分大小写）、`↑`/`↓`/`Enter`/`Esc`、鼠标命中测试）。
* `app/help_model`（纯逻辑：筛选（和弦/`comment`/`name`/动作摘要子串）、
  `↑`/`↓`/`PgUp`/`PgDn`/`Home`/`End`/滚轮的钳位、
  `可见/总数` 计数、`Enter`/点击把哪一行的按键写进剪贴板、两级 `Esc`）。
* `qml/MenuPopup.qml`、`qml/HelpPopup.qml`：**`import QtQuick.Controls.FluentWinUI3`**；
  无边框圆角卡片（`flags: Qt.Window | Qt.FramelessWindowHint |
  Qt.WindowStaysOnTopHint`、`color: "transparent"` 才能有圆角）、
  按键徽标、两级字号、跟随结果变高（顶边不动）、超过窗口高时显示滚动条。
* `app/popup_host`：在 GUI 线程上创建/复用窗口、抢前台（不变量 17）、
  把按键事件转给模型（**字符输入走 `char` 事件，不要用
  `MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR)` 自己猜**——那是布局相关的，
  而且会把 `VK_UNASSIGNED`(0xE8) 之类的伪键翻成字符，
  而弹窗正拿着焦点、引擎正好会注入它）。
* 一次只允许一个选单/一个帮助窗口：再按快捷键只是把它前置
  （帮助窗口还要清空筛选）。
* 选中的动作**回投给工作线程执行**；`help` 只写剪贴板、不执行动作。
  → **2026-09 修订**：`help` 现在也执行动作（`Enter`/双击），但入口仍然是
  “回投给工作线程”：弹窗自己从不执行动作，见第 2 节第 9 条与第 10 节。

**验收**：单测（两个模型的全部分支）；手工：键盘/鼠标都能选、
`Esc` 只关窗不选、帮助窗口的筛选让窗口变矮、`Enter` 真的复制到剪贴板、
在别的应用聚焦时按快捷键也能拿到键盘焦点。

**已完成的内容**

* 新静态库 **`flowkeyd_models`**：`src/app/menu_model.{h,cpp}`、
  `src/app/help_model.{h,cpp}`、`src/app/popup_layout.{h,cpp}`。三者都只用 QtCore，
  所以 `tst_menu_model`/`tst_help_model` 不需要桌面就能跑。
* 两个模型都是 `QAbstractListModel`，角色直接给 QML 用：
  `menu` 是 `label`/`hint`/`keyText`/`highlighted`/`hovered` + 四个 `QRect`；
  `help` 是 `badges`/`label`/`detail`/`highlighted`（行下标就是 `ListView` 的
  下标，行几何由委托用锚点拼，不用模型给矩形）。
  → **2026-09 修订（选单）**：`menu` 的角色也收窄成 `label`/`hint`/`keyText`/
  `rowSelected`，四个 `QRect` 与 `hitTest` 全删了 —— 选单的列表换成了
  `ListView` + 标准 `ItemDelegate`（详见第 10 节）。
  → **2026-09 修订**：帮助窗口改成 Qt 自带的 `ListView` + `ScrollBar` 之后，
  滚动位置、命中、滚动条几何全归 Qt；`help` 那套 `rowRect`/`keysRect`/`textRect`/
  `scrollTrack`/`scrollThumb`/`hitTest`/`clickRow`/`wheel` 都删了，只留下
  `scrollTargetY()`（键盘选中项要摆到哪里）。
  → **2026-09 再修订**：悬停行（`setHover` + QML 的 `HoverHandler` +
  `ListView.indexAt()`）也删了：高亮只跟键盘选中项走，鼠标悬停不改高亮
  （详见第 10 节“取消帮助窗口的悬停高亮”）。
  选单不滚动（条目数决定卡片高度），所以没有滚动条；**它的悬停仍归模型管**
  （`hover`/`setHover`）：「`Enter` 执行光标下那一条」是选单的语义。
* 模型把「按键怎么解释」也包了：`handleKey(key, text)` 返回
  `{ decision: none|choose|copy|cancel, index, handled }`；QML 的
  `Keys.onPressed` 只负责“问模型要决定 → 执行决定”。字符来自 Qt 译好的
  `QKeyEvent::text()`，所以
  `VK_UNASSIGNED`(0xE8) 那条菜单遮断注入不会凭空变成筛选框里的一个字母。
  → **2026-09 再修订**：帮助窗口的 `handleKey()` 不再接字符/退格/`Home`/`End`
  （参数也从 `(key, text)` 收窄成 `(key)`）：筛选框换成了真正的 `TextField`，
  编辑键归它，模型只接导航键、`Enter`、`Esc`（详见第 2 节第 9 条与第 10 节）。
* `qml/MenuPopup.qml`、`qml/HelpPopup.qml`：无边框圆角卡片（
  `Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint`，
  `color: "transparent"`），配色一律走 `palette`（于是自动跟随系统浅色/深色）；
  帮助窗口的筛选框**不是 `TextField`**，而是自己画的一行字 + 一根光标——
  所有按键都走一个 `Keys.onPressed`，`↑`/`↓`/`Enter`/`Esc` 与输入不会互相抢键。
  → **2026-09 再修订**：那个自绘的假输入框已经删掉，换成真正的 `TextField`
  （点得进去、打得了字），列表项换成 `ItemDelegate`（点得中）。
  上面的取舍已经作废，以第 2 节第 9 条为准。
* `app/popup_host.{h,cpp}`（GUI 线程亲和）：
  `requestMenu`/`requestHelp` 可从任意线程调用（内部 `Qt::QueuedConnection`）；
  窗口懒创建、复用；抢前台用 `win::window::raiseWindow`（不变量 17）；
  用户选完把 `onChoose`/`onCopy` 回调交给调用方（`Dispatcher` 再投一次队列）。
* `Runtime::setPopupHost()` + `showMenuFromAnyThread()`/`showHelpFromAnyThread()`；
  `Runtime::shutdown()` **先 `closeAll()`** 再卸钩子/停工作线程，
  免得留下会碰到已销毁 `Dispatcher` 的回调。
* `Dispatcher::submitActions()`（线程安全）+ `openMenuAction`/`openHelpAction`：
  选单项的动作在这里展平；`help` 的列表从当前 `Compiled` 生成
  （`comment` 优先于 `name`，动作摘要用 `Action::summary()`，重映射渲染成
  `remap → Ctrl+C`）。
* 帮助窗口的标题带 `可见/总数`（`flowkeyd 快捷键 — 12/13 项`），
  方便从外面断言筛选生效了；`help` 没写 `title` 时表头默认
  是「快捷键」。
* 测试：`tests/tst_menu_model.cpp`（12 个用例）、`tests/tst_help_model.cpp`
  （17 个用例），合计 **17 个测试目标**全绿。

**手工验证结论（2026-09）**

用 `tmp/preview/`（一个**临时**的、不属于产品的 CMake 小程序：真实的
`PopupHost` + 真实的 QML + 真实的两个模型）验证了下面这些，截图在
`tmp/grab-0.png` / `tmp/grab-1.png`（`QScreen::grabWindow` 抓的，所以缩放正确）：

* 选单：标题、五个条目（含无 `key`/无 `hint` 的那条）、按键徽标、
  高亮行、底部提示全部正确；
* 帮助窗口：表头 + `13 项`、筛选框占位文本、和弦徽标（`Ctrl` `Alt` `F12`）、
  多和弦之间的圆点、两级字号、高亮行、滚动条、底部提示全部正确；
* 键盘链路：给窗口发 `↓`/`↓`/`Enter` → 回调收到 `menu chose 2`；
  给帮助窗口发 `f`/`1`/`2`/`Enter` → 回调收到 `help copy Ctrl+Alt+F12`，
  而且窗口标题变成 `1/13 项`、卡片高度从 708 降到 180（**筛选后变矮，顶边不动**）；
* 复用：再次 `requestMenu` 不新开窗口；`closeAll()` 之后两个窗口都不可见。

**仍需人的手**（与阶段 3/4/5 那批一起留到阶段 9）：真实快捷键触发的
「钩子吞键 → dispatcher → 弹窗」整条链路、弹窗抢到键盘焦点（在别的应用
聚焦时按快捷键）、鼠标悬停/点击选择、`Enter` 真的写进剪贴板。
自动化做不到的原因：钩子刻意忽略注入输入（不变量 2），脚本无法伪造物理按键。
→ **阶段 9 已补做**：`scripts/acceptance.ps1` 里的「选单弹窗」与「帮助弹窗」
两组检查把上面这些（除了鼠标悬停/点击）都跑通了 —— 包括弹窗在别的应用聚焦时
抢到键盘焦点、条目键真的跑了动作、`Enter` 真的写进了剪贴板。

### 阶段 7：虚拟桌面 + 电源

**做什么**

* `platform/win/desktop`：`CLSID_ImmersiveShell` → `IServiceProvider::QueryService`
  → `IVirtualDesktopManagerInternal`；**按 `build.revision` 查表**
  （表里是“起始版本”，取“生效版本不高于当前系统的最后一条”；修订号用
  `RegGetValueW` 从注册表读 `UBR`）；每次调用一条一次性 **STA** 线程；
  每个 HRESULT 都检查。
* `platform/win/power`：`SetSuspendState`（睡眠/休眠）、`ExitWindowsEx`
  （关机/重启/注销，只带 `EWX_FORCEIFHUNG`，**不用 `EWX_FORCE`**）、
  `LockWorkStation`、`WM_SYSCOMMAND` + `SC_MONITORPOWER` 广播（关屏，
  用 `SendMessageTimeoutW` + `SMTO_ABORTIFHUNG`，收件人是 `HWND_BROADCAST`）、
  `SeShutdownPrivilege`。**不需要权限的（`lock`/`sleep`/`screen_off`）
  要在 `enable_shutdown_privilege()` 之前 return**，否则没提权时会先报一句
  指错方向的权限错误。
* `dwm` 的 `animate` 与阶段 5 一起收尾。

**验收**：单测（版本表的选择是纯函数、电源 op 的字符串解析与简写、
`screen_off` 不走权限路径）；**破坏性的电源动作绝不自动化测试**
（睡眠/关机/重启/注销/锁定/关屏都不许在测试里真的执行）；真实调用手工验证
（关屏可以用，随后随便按一个键点亮）。

> **修订（2026-09）**：后来连“测试里可以关屏”这一点也收紧了 —— 电源动作
> 一律不进自动化测试，`powerScreenOffBlanksTheDisplay` 已删除。
> 见工作约定第 10 条。

**状态：已完成（2026-09）。**

**已完成的内容**

* `src/platform/win/desktop.{h,cpp}`：`CLSID_ImmersiveShell` →
  `IServiceProvider::QueryService` → 未公开的 `IVirtualDesktopManagerInternal`。
  `versionTable()` 是当前支持的七条版本表，
  `apiFor(build, revision)` 是纯函数（“最后一个生效版本不高于当前系统”）；
  三种 vtable 布局（`Plain` / `Monitor` / `MonitorShifted`）各写一套结构体，
  未用到的槽只留 `void *` 占位。`windowsVersion()` 用运行时解析的
  `ntdll!RtlGetVersion`（build）+ 注册表的 `UBR`（revision，`RegGetValueW`）；
  每次调用都在一条一次性的 **STA** 线程上（`inSta`），并逐条检查 `HRESULT`。
  还提供只读的 `probe()`（桌面数量/当前序号/系统版本/生效表项）。
* `src/platform/win/power.{h,cpp}`：`powrprof!SetSuspendState`（睡眠/休眠）、
  `user32!ExitWindowsEx`（关机/重启/注销，只带 `EWX_FORCEIFHUNG`）、
  `LockWorkStation`、`WM_SYSCOMMAND`+`SC_MONITORPOWER` 广播（`SendMessageTimeoutW`
  + `SMTO_ABORTIFHUNG` + `HWND_BROADCAST`）、`enableShutdownPrivilege()`
  （`AdjustTokenPrivileges` 返回 TRUE 但一个特权都没加上时看 `GetLastError()`
  是不是 `ERROR_NOT_ALL_ASSIGNED`）。**`powerOpTable()` 与
  `requiresShutdownPrivilege()` 是纯逻辑**：`lock`/`sleep`/`hibernate`/`screen_off`
  在 `enableShutdownPrivilege()` **之前** return，只有关机/重启/注销强制要特权。
  直接静态链接 `powrprof`（见第 3 节的依赖政策）。
* `src/platform/win/ffi.{h,cpp}`：新增 `hresultText()`（常见 HRESULT 的人话说明：
  `E_NOINTERFACE` 意味着版本表选错了 IID），`hresultMessage()` 改用它。
* `app/dispatcher.cpp`：`desktop` / `power` 两个分支接上真实后端；`power` 失败时
  除日志外补一条托盘气泡（没提权时用户看不到控制台）。原先那句
  `not implemented yet (later stage)` 的兜底警告保留在 `default:` 分支。
* 测试：`tests/tst_desktop_table.cpp`（5 个用例：选表规则的全部边界、
  24H2 的已知 IID、表按 build 升序、布局名稳定、系统版本可读）与
  `tests/tst_power_table.cpp`（7 个用例：表的规范名与特权标志、别名、
  `power:` 简写、非法取值报错）。两者都只碰纯逻辑，**不调用 `execute()`**。
* `tests/tst_interactive.cpp` 新增两个 opt-in 用例：
  `desktopBackendProbesAndSwitches`（只读探测 + 一次可逆的切换，切走再切回来）
  与 `powerScreenOffBlanksTheDisplay`（多一道 `FLOWKEYD_ALLOW_SCREEN_OFF=1`
  闸门；关屏后注入一个无害的 Shift 点亮屏幕）。
  → **2026-09 修订**：`powerScreenOffBlanksTheDisplay` 已**删除**（连同
  `FLOWKEYD_ALLOW_SCREEN_OFF` 这道闸门）：电源动作一律不进测试，见工作约定
  第 10 条。上面的实测输出只是当天的记录。

**手工验证结论（2026-09，`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` + `FLOWKEYD_ALLOW_SCREEN_OFF=1`；
关屏那条用例后来已删除，见下）**

```
QINFO  : desktopBackendProbesAndSwitches() desktop probe: count 4 current 2
         os 26200.9457 api 26100 layout plain
         manager {53f5ca0b-158f-4124-900c-057158060b27}
PASS   : desktopBackendProbesAndSwitches()
PASS   : powerScreenOffBlanksTheDisplay()
Totals: 8 passed, 0 failed
```

* 本机（build 26200.9457）命中的是版本表的 26100 条目、`plain` 布局，
  IID 与 MScholtes/VirtualDesktop 的 24H2 版本一致；`QueryService` 成功，
  4 个桌面、当前在第 2 个。切到第 1 个再切回来成功。
* `screen_off` 真的把全部显示器送进待机，随后注入的 Shift 把它点亮。
* 睡眠/关机/重启/注销/锁定**没有**被自动化测试触碰（它们只有用户按下去才会执行）。

### 阶段 8：`flowkeyd.lua.example` + README

**做什么**：写出覆盖全部特性的 `flowkeyd.lua.example`（中文注释、无警告），
并把 `README.md` 写成完整的中文用户文档（含配置/动作/schema 说明、
日志窗口、选单/帮助、验证那一节）。
`--check --config flowkeyd.lua.example` 必须通过。

**验收**：手工通读示例配置，确认每条注释与实现一致；双构建 + 测试绿。

**状态：已完成（2026-09）。**

**已完成的内容**

* `flowkeyd.lua.example` 在阶段 2 就已经写出；本阶段逐行通读过，
  确认每条注释与实现一致（Lua 5.5.1、CMake preset 构建、
  不存在 `--simulate`/`--selftest`/`--probe` 这类开关）。
* `README.md`（新增，约 700 行，中文）：Lua 5.5.1、CMake preset 构建、
  **日志窗口**（进程内 QML 窗口）、**选单/帮助**（QML + 跟随系统 palette）、
  **验证那一节**（Qt Test + 手工冒烟清单，并写明没有 e2e）、
  工作原理图（三线程 + Qt GUI 线程）、已知限制与路线图。
  文中所有 `--check`/`--list`/`--version` 的输出形状都用真实运行结果核对过。

**实测结果（2026-09）**

* `flowkeyd --version` → `flowkeyd 0.1.0` / `Lua 5.5.1`（版本号里带 Lua，出问题时
  能一眼看出是哪一份 Lua）。
* `flowkeyd --check --config flowkeyd.lua.example` →
  `D:\prj\flowkeyd\flowkeyd.lua.example: OK (37 hotkey(s), 3 remap(s))`，零警告。
* `flowkeyd --list` 的形状与 README 里记录的一致。

### 阶段 9：验收（无 e2e 的替代）

**做什么**：把第 5 节的手工冒烟清单**完整跑两遍**（debug 与 release 各一遍），
并逐条记录结果到本文件。
特别要覆盖：被吞掉的 `Win+S` 从四个角度（常规、0 ms 轻按、
1–2 次模拟 Windows 键自动重复）；小键盘的 `-`/`+`/`Enter` 与主键盘
`-`/`=`/`Enter` **互不触发**（注入小键盘 Enter 必须带
`KEYEVENTF_EXTENDEDKEY`，否则测的就是主键盘的 Enter）；重映射的
hold/tap；挂起/重载/退出。

> **修订（2026-09）**：DoD 现在只在 **debug** 上跑 `ctest`，
> `acceptance.ps1` 只跑 **release** 的产物（工作约定第 2 条），
> 下面的“debug 与 release 各一遍”只是当年的记录；
> 另外测试里不再执行任何真实电源动作（第 10 条）。

**状态：已完成（2026-09）。**

**已完成的内容**

* **`FLOWKEYD_ACCEPT_INJECTED` 测试后门**：`platform/win/input.{h,cpp}` 新增
  `acceptInjectedInput()`（读环境变量，进程内只缓存一次），`hook.cpp` 里改成
  `event.injected = (info->flags & LLKHF_INJECTED) != 0 && !acceptInjectedInput()`，
  并在装完钩子之后打一条警告。
  这是能自动验证“真的吞了键”的前提：钩子不认注入输入，脚本就伪造不了物理按键，
  而 `--simulate` 本期不做（第 12 节）。**不变量 2 没被放宽**：flowkeyd 自己
  注入的事件带着 `"FLOW"` 标记，仍然在钩子回调第一步就被丢掉。
* **`scripts/acceptance.ps1`**（新文件，当时 68 项检查：一次性配置 + 获得焦点的
  WinForms 捕捉窗口 + `SendInput` 注入 + 剪贴板/窗口/日志当外部证据。
  覆盖第 5 节清单的 1–12 条，另外还多做了：小键盘与主键盘互不触发（6 项）、
  重映射 hold/tap（不只是 CapsLock）、`menu`/`help` 弹窗的键盘选择与筛选、
  以及“重新打开的选单也要重新拿到焦点”。**后续又加了 9 项弹窗滚轮回归
  （见第 10 节的“弹窗在滚轮下闪烁”），2026-09 再把帮助窗口的列表换成 Qt 自带的
  `ListView` + `ScrollBar`时加了 5 项（拖动滑块 + 重新打开复位滚动位置），
  现在是 82 项**。
* 脚本的检查名用中文字面量，所以文件必须以
  **带 BOM 的 UTF-8** 保存 —— PowerShell 5.1 会把无 BOM 的 `.ps1` 按 ANSI
  代码页解码。三个窗口标题故意用 `[char]` 码点拼出来，这样即使 BOM 丢了
  逻辑也不会错（断言里的中文变乱码无所谓，检查结果仍然是对的）。

**实测结果（2026-09）**

```
# debug 跑了 3 遍、release 跑了 2 遍（当时还是 68 项）
checks: 68, failures: 0
```

> **2026-09 补充**：修完“弹窗在滚轮下闪烁”之后又加了 9 项检查（选单/帮助各几条，
> 包括“滚轮之后 Enter 复制的还是光标下那一行”这条能直接抓住旧代码的回归），
> 检查数到了 77 项（`checks: 77, failures: 0`）。
> **再后来（2026-09）帮助窗口的列表换成了 Qt 自带的 `ListView` + `ScrollBar`**：
> 又加了 5 项（拖动滑块能让列表滚、重新打开会复位滚动位置），现在是 **82 项**，
> 帮助那一段的滚轮注入也从 `Wheel(120)` 改成 `Wheel(-120)`（方向跟着系统）。
> 结果见第 11 节的 DoD 记录。

* `flowkeyd --check --config flowkeyd.lua.example` → 37/3、零警告（未变）。
* 四条 Win 和弦变体全绿：常规 / 0 ms 轻按 / 一次、两次 Windows 键自动重复 ——
  外壳都不会弹出搜索面板，而动作在**按下时**就派发了（剪贴板是外部证据）。
* 自动重复：按住 `F15` 约 1.5 秒（注入 12 次 key-down），`once.log` 只有一行。
* `send("^{c}")` 时前台看到的是 `Ctrl+C`（`Alt=False`），说明修饰键释放对了。
* 小键盘 `-`/`+`/`Enter` 与主键盘 `-`/`=`/`Enter` 互不触发。
* `quit` 之后：日志最后一行是 `keyboard hook removed`、没有 `ERROR`、进程表里
  没有它，而且再按被吞掉的键会重新到达前台窗口；最后没有按键卡在按下状态。
* 结果里那两次「失败」是**测试环境**造成的，不是产品 bug（细节见第 10 节）：
  一次是用户在跑测试时把 Notepad 点到前台（捕捉窗口丢了前台锁 —— 已把
  `FocusCatcher` 换成 `AttachThreadInput` 版，并在真的抢不到焦点时明确报出来）；
  一次是我最初选了 `Win+F16` 当被测和弦，而本机那个组合会拉出
  `SlideToShutDownHost`（已换成 `Win+S`）。
* 不过它**真的**查出了一个产品 bug：`menu` 第二次打开（复用同一个 QML 窗口）
  时拿不到键盘焦点 —— 见第 10 节的 `activateWindow` 那一条，已修并有检查盯着。

### 阶段 10：接管（品牌迁移 + 常驻）

**做什么**

1. 把本机真实绑定写进 `%USERPROFILE%\.config\flowkeyd\config.lua`，
   逐条核对：`CapsLock`→Ctrl+Space、`Alt+H/J/K/L`、
   `Alt+Space`→F14、`LWin+Q`→F24、`LWin+F1..F4`→虚拟桌面 1..4、
   `Win+S`→WezTerm、`Win+1/2/3`→Chrome/VS Code/WPS、`Win+W`→微信、
   `Win+X`→电源选单、`Win+/`→快捷键帮助、小键盘 `-`/`+`/`*`→音量、
   `Ctrl+Alt+F4/F5/F12`→quit/reload/suspend。
2. 确认没有别的键盘钩子守护进程在抢事件（有的话按工作约定第 5 条
   `taskkill /PID <pid>` 不带 `/F` 干净停掉）。
3. 用 release 版 flowkeyd（提权）接管常驻，逐条手工验证上面那批绑定。
4. 在本文件与 `README.md` 里写清“现在常驻的是 flowkeyd”。

**状态：已完成到“等用户点一次 UAC”为止（2026-09）。**

**已完成的内容**

1. 配置已迁到 `%USERPROFILE%\.config\flowkeyd\config.lua`：
   `flowkeyd --check`（**不带** `--config`，走默认搜寻）→
   `C:\Users\xingjian\.config\flowkeyd\config.lua: OK (24 hotkey(s), 0 remap(s))`，
   零警告；`--list` 的 24 条与第 14 节那张表逐条对应。
2. 机器重启后没有找到别的常驻实例，无需停。
3. 非提权预演（真的读用户那份配置，**不注入任何按键**，所以没触发任何真实动作）：
   日志里 `24 hotkey(s), 0 remap(s), tick 15 ms` + `keyboard hook installed`；
   `taskkill /PID <pid>`（**不带** `/F`）能干净退出（日志末尾
   `keyboard hook removed`）。
4. `README.md` 新增「本机现在常驻的是 flowkeyd」一节：启动命令、干净退出、
   为什么没加自启。
5. **当时没有加开机自启**（用户拍板），也**没有**给 exe 登记 `RUNASADMIN` 兼容性
   标记：那个标记会让 `flowkeyd --check` 之类离线命令也弹 UAC
   （离线命令本不该弹 UAC —— 不变量 11）。

> **2026-09 后续：自启补上了，后来又改成自注册。** 用户改主意，要求“开机自启 +
> 经常更新的场景下也别出问题”。第一版是**计划任务（登录时 + 最高权限）指向
> `C:\Program Files\flowkeyd\flowkeyd.exe`**，装/更新/卸载走 `install.ps1` /
> `uninstall.ps1`；**那一版已经删掉**（项目所有者 2026-09 再改）：现在没有
> 安装目录、也没有部署脚本，**任务由守护进程自己在每次启动时注册 / 刷新，指向
> 当前正在运行的 exe 路径**（实现见 `src/platform/win/autostart.*`，剥离自启用
> `--no-autostart`，删除用 `--remove-autostart`）。为什么用计划任务（而不是启动
> 文件夹 / 服务）、为什么开发实例要跳过、任务的每个参数为什么是那样，
> 见第 2 节第 10 条、第 10 节「开机自启」那几条与 `README.md` 的「开机自启与更新」。
> 下面那段历史记录保留，只为说明当时的处境。

**剩下要用户做的一件事**：agent 的 shell 没有提权，启动提权进程会弹 UAC 而
没人点（也不该由 agent 去点）。所以常驻由用户自己启动一次：

```powershell
Start-Process -Verb RunAs -FilePath 'D:\prj\flowkeyd\build\windows-release\flowkeyd.exe'
```

> **副作用：这个常驻实例会把 `build\windows-release\flowkeyd.exe` 锁住**（它跑在
> 用户的管理员令牌下，agent 的 shell 既 `Stop-Process` 不动也 `taskkill /F` 不掉）。
> 所以**每次要重新链接 release 的 exe 之前，都要请用户从托盘菜单点一下“退出”**，
> 构建完再照上面那条重新启动；debug 目录没被占用，可以先用它做验证。
> 细节见第 10 节。

**验收（用户启动后逐条确认）**：`CapsLock`、`Alt+H/J/K/L`、`Alt+Space`、
`LWin+Q`、`LWin+F1..F4`、`Win+S`、`Win+1/2/3`、`Win+W`、`Win+X`、`Win+/`、
小键盘 `-`/`+`/`*`、`Ctrl+Alt+F4/F5/F12`。
`Ctrl+Alt+F12`（挂起）与 `Ctrl+Alt+F4`（退出）是安全的自检项；
**`Win+X` 选单里千万别按到睡眠/关机/重启**。

> **2026-09 自注册改造的实测（已归档）**：把常驻从 `C:\Program Files\flowkeyd`
> 迁到 `build\dist-release`：旧的 `build\dist-release\flowkeyd.exe` 一启动就把
> 计划任务 `flowkeyd` 重写成指向自己（日志：`logon autostart task `flowkeyd`
> updated: D:\prj\flowkeyd\build\dist-release\flowkeyd.exe starts at every logon
> with the highest privileges`）；`--remove-autostart` 删得掉、再启动又自己回来；
> 用 `--no-elevate --allow-multi` 起的开发实例只记一条 `autostart task not managed`
> 并且**没有**改任务。旧的安装目录 `C:\Program Files\flowkeyd` 已删除。
> 验收脚本 116/0（它用 `--no-elevate` 起临时守护进程，不会被自启逻辑打扰）。

---

## 10. 已踩过的坑（省下你的时间）

### 本项目环境特有的

* **`qt_add_executable(... WIN32 ...)` 意味着 GUI 子系统 ⇒ 没有控制台。**
  于是一个从终端启动的 `flowkeyd --check` **什么都不会打印**（看起来像
  “命令什么都没做”）。修法是在 `main()` 里第一件事就
  `AttachConsole(ATTACH_PARENT_PROCESS)`（失败就忽略，例如双击启动），
  成功后把 `stdout`/`stderr` 重新打开到本进程的控制台
  （`CreateFileW(L"CONOUT$", …)` + `_open_osfhandle` + `freopen`，
  或干脆自己用 `WriteConsoleW` 写）。**并且行尾必须是 `\r\n`**
  （`\n` 只换行不回车，终端里会看到阶梯状输出）。
  同理，`qDebug()` 在没有控制台时走 `OutputDebugString`，
  **不要用 `qDebug` 当日志**，我们自己实现 `platform/win/logging`。
* **`QSystemTrayIcon` 在 QtWidgets 里，不是 QtGui 里。**
  要用它就必须 `QApplication`（而不是 `QGuiApplication`），
  即使 UI 全是 QML。`find_package` 要带上 `Widgets`。
  （替代方案是 `Qt6::LabsPlatform` 的 QML `SystemTrayIcon`，
  但那是 labs 模块，而且底层还是 QtWidgets；本项目选 C++ 这条路，
  因为托盘菜单要直接触发 `ControlCmd`。）
* **QML 窗口默认会让“关掉最后一个窗口”直接退出应用。** 日志窗口是普通窗口，
  用户点叉会退掉整个守护进程 —— 必须在构造 `QApplication` 之后立刻
  `QGuiApplication::setQuitOnLastWindowClosed(false)`。
  （日志窗口跑在同一个进程里，就必须自己保证它不会退掉守护进程。）
* **FluentWinUI3 是 Qt **Quick Controls** 的样式，不是 Widgets 样式。**
  Widgets 那边没有 FluentWinUI3，所以「用 FluentWinUI3」= 「UI 用 QML」。
  它自 Qt 6.8 起提供，用 `QQuickStyle::setStyle("FluentWinUI3")`
  （或 `-style FluentWinUI3`、`QT_QUICK_CONTROLS_STYLE`、
  `qtquickcontrols2.conf` 的 `[Controls] Style=`）**在加载任何 QML 之前**设置。
* **FluentWinUI3 还不支持一批控件**（`Dial`、`Drawer`、`HorizontalHeaderView`、
  `SplitView`、`StackView`、`SwipeDelegate`、`SwipeView`、`TreeViewDelegate`、
  `Tumbler`、`VerticalHeaderView`）——它们会**静默回退到 Fusion**，
  于是弹窗里会混进两种画风。**布局不要用它们**（多视图用 `Loader` +
  `visible` 切换即可）。
* **该样式靠图片资源实现，部分元素不可定制**；配色跟随系统的
  “Windows 主题色”与 `palette`。要改配色就改 `palette`，
  别去 hack 样式内部的图片。
* **无边框圆角弹窗**：`Window { flags: Qt.Window | Qt.FramelessWindowHint |
  Qt.WindowStaysOnTopHint; color: "transparent" }` + 内部一个
  `Rectangle { radius: … }`。只设 `FramelessWindowHint` 而 `color` 不是
  transparent 的话看不到圆角。**Windows 11 本身也会给无边框窗口加圆角**，
  两套圆角会打架，调的时候两个都试一下。
* **Qt 6 在 Windows 上默认就是 Per-Monitor DPI Aware V2**，
  不要再手动调 `SetProcessDpiAwarenessContext`。**不要在
  `QApplication` 之前创建窗口**；
  要改缩放取整策略就 `QGuiApplication::setHighDpiScaleFactorRoundingPolicy()`
  在它之前调。
* **Qt 会自动给我们一个有 COM 的线程**（主线程是 STA，为了 OLE/拖放）。
  虚拟桌面那条路要 STA、音频要 MTA，**别把音频初始化放到主线程**
  （否则会把主线程的单元模型定死成 MTA，Qt 的拖放/剪贴板可能出问题）。
  做法：桌面调用走一次性 STA 线程，音频在它自己的一次性 MTA 线程上。
* **`PostThreadMessage` 在目标线程还没消息队列时会静默失败。** 钩子线程要先
  `PeekMessage` 建出队列并把就绪状态告诉启动方，否则最早发出的
  “suspend/reload” 控制消息会丢。
* **PowerShell 5.1 的 `Set-Content -Encoding UTF8` 写出 BOM**，
  而 Lua 的词法分析器在 BOM 上报
  `unexpected symbol near '<\239>'`。加载器必须先剥 BOM（有单测）。
  我们自己写 `.ps1`（如果以后写）也要注意反向的那个坑：
  **没有 BOM 的 `.ps1` 会被 PowerShell 5.1 按 GBK 解码**，里面的中文会变乱码、
  甚至被当成引号，于是 here-string 提前结束、报一堆莫名其妙的解析错误。
  写文件的正确姿势：
  `[System.IO.File]::WriteAllText($p, $text, [System.Text.UTF8Encoding]::new($true))`。
* **`.cmd`/`.bat` 必须纯 ASCII**（cmd.exe 按 ANSI 代码页读文件）。
* **命令行上不要直接拼中文**（agent 的 bash 会因代码页变乱码）：
  写脚本文件，或只用 ASCII 的模式串。

#### 阶段 0/1 真的踩到的（2026-09）

* **Lua 的头文件里没有 `extern "C"` 保护。** 在 C++ 里直接 `#include <lua.h>`
  会把所有声明按 C++ 规则 mangling 成 `_Z13luaL_newstatev`，链接时对着
  `liblua_static.a` 报一屏 `undefined reference to luaL_newstate()`
  （而同一个 `.c` 文件用 C 编译就一切正常，所以看上去像“静态库没编进去”）。
  Lua 官方只给 `LUAMOD_API`（`luaopen_*`）加了 `extern "C"`。
  **统一用 `src/lua/lua_include.h`**（它把所有 Lua 头包在 `extern "C" {}` 里）。
* **`QTEST_MAIN` 的默认输出在本机拿不到。** 同一个可执行文件里 `fprintf(stdout, …)`
  能正常出现在重定向文件里，但 QtTest 自己的 `PASS/FAIL/Totals` 什么都不写：
  重定向、管道、`ctest --output-on-failure` 全都是空的，而 `-o <file>,txt`
  却能拿到完整结果（`Qt6Test.dll` 里也找不到 `WriteConsoleW`，但有
  `OutputDebugString`）。于是 `cmake/RunQTest.cmake` 让测试先写文件、
  再由脚本 `cmake -E cat` 出来，这样 ctest 才能显示到底哪条断言挂了。
  **新增测试不用做别的**，`flowkeyd_add_test()` 已经封好了这一层。
* **cmd.exe 不会等 GUI 子系统的进程**，所以 `cmd /c "flowkeyd.exe --help > out.txt"`
  会在 flowkeyd 还没来得及写之前就把控制抢回来，`out.txt` 看起来是空的。
  验证 CLI 输出请用：
  `Start-Process -FilePath .\flowkeyd.exe -ArgumentList '--help' -NoNewWindow -Wait -PassThru -RedirectStandardOutput out.txt`
  （`$p.ExitCode` 才是真的退出码）。控制台（非重定向）那条路走的是
  `WriteConsoleW`，需要人眼在真终端里确认一次。
* **`QCOMPARE` 的宏参数里不能出现“不受括号保护”的逗号。**
  `QCOMPARE(x, std::optional<std::pair<Vk, bool>>(...))` 会被当成三个参数
  （尖括号不算括号）。给这种类型起一个 `using VkShift = std::optional<std::pair<Vk, bool>>;`
  别名就解决了。`QVector<SendOp>{a, b}` 这种字面量要套一层 `(…)`。
* **`QDir::filePath()` 一律用 `/` 当分隔符**，不是 Windows 的 `\`。
  配置文件路径、候选列表、错误信息里都统一过一遍
  `QDir::toNativeSeparators()`，这样看到的是
  `C:\Users\xingjian\.config\flowkeyd\config.lua`。
  （反例：单测里拿 `C:\tools\config.lua` 比对时会失败。）
* **`qt_add_executable()` 会默认给测试套上 AUTOMOC**，连 `lua_static` 也不例外，
  结果是静态库里多一个空的 `mocs_compilation.cpp.obj`。
  `lua_static` 上显式写 `AUTOMOC OFF`/`AUTOUIC OFF`。
* **`set_tests_properties(... ENVIRONMENT_MODIFICATION)` 的两个修改项必须写在一个
  `"a;b"` 字符串里**，不能当两个参数传（会被当成 property/value 不匹配）。
  另外跑测试的 exe 需要 `PATH` 里有 Qt 的 `bin` 与 MinGW 的 `bin`，
  否则 `ctest` 一律报 `0xc0000135`（DLL not found）。
* **写测试用的假 `Evaluator` 不用真的 Lua**：`core::loadConfig()` 把求值回调当参数，
  所以 BOM 剥离、`.toml` 拦截、候选路径都能在阶段 1 单测（见 `tst_config`）。

#### 阶段 2/3 真的踩到的（2026-09）

* **带 `.qrc` 文件或 `qt_add_resources` 的静态库，资源初始化会被链接器丢掉。**
  `qt_add_resources(target "name" …)` 会生成 `qrc_name.cpp`，但静态库的归档只在
  “有人引用它导出的符号”时才会把那个 object 拉进来。保险做法是在被引用的源文件里
  调一次 `Q_INIT_RESOURCE(name)`（它必须在**全局命名空间**里，放进
  `namespace flowkeyd::lua` 会声明成 `flowkeyd::lua::qInitResources_…` 而链接失败）。
  另一个坑：`qt_add_resources(target path/to/foo.qrc)` 这个“直接传 .qrc”的写法
  在本机的 Qt 6.11 上**什么都没生成**（`build.ninja` 里没有 rcc 行）。
  用 `qt_add_resources(target "name" PREFIX "/x" BASE <dir> FILES …)` 才可靠。
* **`lua_next` 遍历中调 `lua_tolstring` 会把数字键就地转成字符串**，于是下一次
  `lua_next` 收到一个“表里不存在”的键，触发
  `PANIC: unprotected error in call to Lua API (invalid key to 'next')`
  （不是返回错误码，是 abort）。规则：只对 `lua_type==LUA_TSTRING` 的键取字符串；
  数字键用 `lua_tointeger`。
* **嵌套遍历时，内层压栈会打乱外层的 `lua_next` 游标。** 在外层 `while
  (lua_next(...))` 里调用一个“自己会压栈”（比如 `readTableList`）的函数之后，
  不能再假设“栈顶就是 value”，否则 `lua_pop(L,1)` 弹错东西、下一轮 `lua_next`
  又崩。写法：进循环前记 `loopBase = lua_gettop(L)`，每轮结束用
  `lua_settop(L, loopBase + 1)` 把栈恢复到“key 在栈顶”。
* **`lua_next` 的“复制表”惯用法里不能再多弹一次**：
  `lua_pushvalue(-2); lua_insert(-2); lua_settable(dst);` 之后栈上正好剩下 key，
  它就是下一轮要用的键。`settings{}` 多次给出的合并路径踩过这个。
* **`sendOps` 要同时接受 `QVector<SendOp>`（`core` 的解析输出）与
  `std::vector<SendOp>`（`core::Reaction::inject`）**，所以留了两个重载。
  `core` 里两种容器混着用，别再以为“只有 QVector”。
* **`-Werror` 下两个 Win32 小坑**：MinGW 的 `SendInput` 第二参是 `LPINPUT`
  （非 const），要 `const_cast`；`GetProcAddress` 的 `FARPROC` → 具体函数指针
  会被 `-Wcast-function-type` 报错，用 `std::memcpy` 绕开（比 `reinterpret_cast`
  到 `void*` 更干净）。
* **`HHOOK` 不是 `HANDLE`**：`UnhookWindowsHookEx` 只接受 `HHOOK`，成员写成
  `HANDLE` 会报 `invalid conversion`。
* **`QObject::moveToThread` 拒绝带 parent 的对象。** `app::Dispatcher` 因此
  用 `new Dispatcher(this)`（第一参是 `Runtime*`，parent 仍是 `nullptr`）而不是
  `new Dispatcher(this)` 传成 parent——仔细看签名，它没有第二个参数。
* **`core::Trigger` / `Phase` 在 `core/engine.h`，不在 `core/config.h`。**
  只 include `config.h` 时会报 `'Trigger' has not been declared`。

#### 阶段 4/5 真的踩到的（2026-09）

* **QML 的 model 角色名不能叫 `text`。** `Text` 本身就有 `text` 属性，
  delegate 里写 `required property string text` 会与它撞名。`LogModel` 的角色
  因此叫 `line`/`level`。同理，delegate 里不要用 `parent.text` 去拿模型值——
  `parent` 是 `ListView` 的 contentItem，不是模型。
* **无 BOM 的 `.ps1` 里的中文会把脚本弄坏（又踩一次）。** PowerShell 5.1 按 GBK
  解码无 BOM 的 `.ps1`，UTF-8 的中文注释变成乱码，甚至让 `Start-Process` 拿到
  错参数，表现为莫名其妙的 `exit=-1073741515`（`STATUS_DLL_NOT_FOUND`）——
  一度以为是缺 DLL。规则：**`tmp/` 下的一次性脚本一律纯 ASCII**，
  或者用 `[System.IO.File]::WriteAllText(..., UTF8Encoding($true))` 写带 BOM 的。
* **手动跑 exe 时要自己把 Qt 与 MinGW 的 `bin` 加进 `PATH`。** `ctest` 由
  `flowkeyd_add_test()` 的 `ENVIRONMENT_MODIFICATION` 加好了，但直接
  `Start-Process .\flowkeyd.exe` 会报 `0xc0000135`。
  → **2026-09 已修**：`flowkeyd` 现在有一条 `POST_BUILD` 的 `windeployqt`，
  构建目录里就已经有 `Qt6Gui.dll` 等 DLL 与 QML 模块，双击即可启动。
  下面的两条是当时（还没部署时）的现象记录。
* **截一个被遮住的窗口不能用 `CopyFromScreen`。** 它截的是屏幕在该坐标处的
  可见内容（窗口被终端遮住就截到终端）。要截窗口本身用
  `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)`；`GetWindowRect` 给出的
  尺寸是准的（`860x500` 的窗口带边框是 `873x536`）。
* **`emit other->someSignal()` 在类外是编译不过的**（信号是 `protected`）。
  动作触发的挂起状态变化改成 `Runtime::reportSuspended()`（公开方法，内部
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 把 emit 挪回 GUI 线程），
  同时删掉了 `Dispatcher::suspendedChanged`。
* **`WIN32_LEAN_AND_MEAN` 不包含 `ole2.h`**，所以 `CoInitializeEx` /
  `CoCreateInstance` / `CLSCTX_ALL` / `COINIT_MULTITHREADED` 在 `audio.cpp` 里
  必须显式 `#include <objbase.h>`。MinGW 的 `GUID` 可以直接用聚合初始化
  `{0xBCDE0395, 0xE52F, 0x467C, {…}}`（`Data1` 是 32 位 `unsigned long`）。
* **`Window` 在 Qt 6.2+ 有 `palette` 属性**（`QQuickWindow::palette`），
  所以 QML 里的默认前景色写 `root.palette.text` / `root.palette.placeholderText`
  就能跟随系统主题，不用硬编码颜色。
* **日志窗口打开着的时候，`taskkill /PID`（不带 `/F`）退不掉进程**：
  WM_CLOSE 被日志窗口吃掉（它只隐藏、不退出，不变量 20），而钩子进程
  没有别的可见窗口可关。测试脚本要么走托盘“退出”，要么直接 `/F`。
* **需要真实桌面的验证要做成“默认 skip 的交互式单测”。**
  `tests/tst_interactive.cpp` 靠 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 开启，
  `ctest` 里只是 skip。这比写一个只跑一次的临时程序好：
  它能反复验证剪贴板/音量/窗口后端，而且不会在 CI 里碰用户桌面。
* **平台层的“决策”要抽成 `core` 的纯函数才好测**：
  `planWindowAction()`（`toggle` 边界）与 `windowOpHasTransition()`
  （`animate` 对哪些 op 有意义）就是这么从 `window.cpp`/`dispatcher.cpp` 里
  抽出来的；否则这两条只能在真实桌面上碰运气。

#### 阶段 6 真的踩到的（2026-09）

* **`Q_PROPERTY(QRect …)` 要求头文件里能看到完整的 `QRect`。**
  moc 生成的 `qt_static_metacall` 会把 getter 的返回值赋给一个真的 `QRect`
  （而不是 `QVariant`），所以只前置声明不够，必须 `#include <QRect>`，
  否则报一屏 `invalid use of incomplete type 'class QRect'` 与
  `Meta Types must be fully defined`。
  另一个细节：`Q_PROPERTY(QRect x READ x)` 的 getter 必须**真的返回 `QRect`**，
  所以 `PopupRect` 上加了一个隐式 `operator QRect()`（模型内部仍然用自己的
  矩形类型，测试比较字段时不受影响）。
* **QML 里访问外层组件的 id（尤其是 delegate 里）会报 `Unqualified access`。**
  在文件开头加 `pragma ComponentBehavior: Bound` 就干净了（Qt 6.5+，本机 6.11）；
  JS 数组模型（`model: someJsArray`）的 delegate 里要写成
  `required property var modelData` 并用 `badgeItem.modelData` 这样的限定写法。
  用 `qmllint -I C:\Qt\6.11.2\mingw_64\qml <file>.qml` 能提前把这类问题找出来
  （**不要加 `--bare`**，那样连 QtQuick 都找不到）。
* **`import QtQuick.Controls.FluentWinUI3` 里确实能用 `Label`**
  （该样式模块的 qmldir 里没有 `Label.qml`，但基础模块的类型会一起导出；
  用 qmllint 实测过，不会报 `Label was not found`）。
* **本机 225% 缩放下 Qt 报出的 `availableGeometry()` 比 `geometry()` 还宽**
  （工作区从 x=108 开始、宽 485，而屏幕只有 533 宽）。只在工作区里居中
  “在工作区里居中”会把 500 逻辑像素宽的帮助卡片放到屏幕外面去，右边被切掉
  一大块。修法：居中之后**再按屏幕 `geometry()` 夹一次**
  （`app::centrePopup()`，纯函数、有单测）。
* **本机主显示器是 1200x2464 物理、225% 缩放（533x1095 逻辑）。**
  所有关于弹窗尺寸的判断都要按这个算：500 逻辑像素宽的帮助卡片其实
  （刚刚好）放得下，但没多少余量。
* **用 DPI 不感知的 PowerShell 进程去 `GetWindowRect` + `PrintWindow` 会拿到
  错的结果**：坐标被虚拟化成逻辑像素（窗口是 675x617 物理，`GetWindowRect`
  却报 300x274），于是 `PrintWindow` 把整张 675x617 的帧缓冲塞进 300x274 的
  位图里——看起来像“布局全错、被切了一半”。正确做法是让 Qt 自己抓：
  `QScreen::grabWindow(window->winId())`（返回的 `QPixmap` 带正确的 dpr），
  或者先 `SetProcessDPIAware()` 再量。
* **两个弹窗都是 `WindowStaysOnTopHint`，会互相遮住。** 想截某一个就要把它们
  分开放（或先隐藏另一个）；`grabWindow` 抓的是屏幕那块区域，
  被盖住的窗口抓出来是别的窗口的内容。
* **没有物理按键就无法验证弹窗链路。** 钩子刻意忽略注入输入（不变量 2），
  所以脚本没法伪造“用户按了 `Win+X`”。可行的做法是做一个**临时预览程序**
  （`tmp/preview/`：自己的 `CMakeLists.txt` + `main.cpp`，`file(GLOB)` 拉进
  `src/core`、`src/platform/win`、两个模型与 `popup_host.cpp`，再用
  `qt_add_qml_module` 注册同样的 `Flowkeyd` 模块）：它直接调
  `PopupHost::requestMenu/requestHelp`，用
  `QCoreApplication::sendEvent(window, &QKeyEvent(...))` 模拟键盘，
  再用 `QScreen::grabWindow` 截图。**这条路径能验证除“真实按键”之外的一切**，
  包括 `Keys.onPressed` → `model.handleKey` → `host.menuChoose` 的回调。
  （`tmp/` 在 `.gitignore` 里，重做一次大概十分钟。）
* **`Keys.onPressed` 只在窗口是活动窗口时才会把事件交给有焦点的 item。**
  预览程序里如果先弹帮助窗口再给选单窗口发按键，选单什么都不会做——
  这不是 bug，真实使用里同一时刻只有一个弹窗拿到键盘。
* **`QWindow::setProperty("visible", …)` 是隐藏/显示一个 QML `Window` 的
  最省事办法**（与 `LogWindow` 一致）；窗口不会因此被销毁，所以可以复用。

#### 阶段 7/8 真的踩到的（2026-09）

* **PowerShell 函数里不要把参数命名成 `$args`。** `$args` 是自动变量（未绑定参数
  的数组），`function Run-Cli($label, $args) { Start-Process -ArgumentList $args }`
  里那个 `$args` 永远是空的，`Start-Process` 会报“ArgumentList 不能为 Null”。
  换个名字（`$argList`）就好。同样，命令行上用 `|` 管道会被 agent 的 bash 抢走
  （`Select-Object: command not found`），要写进 `.ps1` 文件跑。
* **`Get-Content` 不带 `-Encoding UTF8` 会把无 BOM 的 UTF-8 文件数错行数。**
  实测 `flowkeyd.lua.example`（493 行、LF、无 BOM）被 `(Get-Content x).Count`
  数成 **386** 行，而 `Get-Content -Encoding UTF8` 与
  `[System.IO.File]::ReadAllLines` 都是 493。PowerShell 5.1 默认按 ANSI/GBK
  解码，多字节中文被误读（这是“无 BOM 的 `.ps1` 被按 GBK 解码”那个坑的另一个面）。
  数行数、比对配置文本一律用 `-Encoding UTF8` 或
  `[System.IO.File]::ReadAllLines`。
* **核对文档/示例改动的好办法是 `git diff --no-index`。** 把改动前的版本
  读到临时文件再与当前文件 diff，比肉眼比对可靠；注意 git 会自动处理
  CRLF/LF，不要自己去对齐行尾。
* **MinGW 的头文件里没有 `MONITOR_OFF`**（`SC_MONITORPOWER` 与
  `SMTO_ABORTIFHUNG` 有，`MONITOR_OFF` 没有），要自己写 `constexpr LPARAM kMonitorOff = 2;`。
  同理 `SetSuspendState` 虽然在 `powrprof.h` 里，但那个头会连带拉进
  `powerbase.h`/`powersetting.h`；只用一个入口时手写声明更干净
  （导入库照样静态链接）。
* **`hresultMessage()` 以前用 `FormatMessageW` 解析 HRESULT，结果几乎总是
  `error 0x…`。** 现在改成 `hresultText()`：`E_NOINTERFACE` 等几个常见值有
  人话名字，剩下的才退回十六进制。这一条对虚拟桌面特别重要：
  `E_NOINTERFACE` 意味着版本表选错了 IID，而不是随便一个内部错误。
* **`RtlGetVersion` 可以从已加载的 `ntdll.dll` 用 `GetModuleHandleW` +
  `GetProcAddress` 拿**（每个进程都有 ntdll），不用给 `flowkeyd_platform`
  加一条 ntdll 链接依赖；`GetVersionEx` 会被应用清单骗，不能用。
* **破坏性的后端（关屏）在 opt-in 的交互式单测里也要再加一道闸门。**
  （**已作废，2026-09**：现在干脆**不让它进测试** —— 电源动作只能由用户
  自己按，见工作约定第 10 条。下面是当年的做法，仅作记录。）
  `tst_interactive` 的 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1` 是“会碰真实桌面”
  的总闸；关屏会真的黑屏，所以单独用 `FLOWKEYD_ALLOW_SCREEN_OFF=1` 再问一次，
  并在测试里注入一个无害的 Shift 把屏幕点亮。睡眠/关机/重启/注销/锁定
  **永远不写进测试**。
* **`QCOMPARE` 可以比较 `std::optional<enum class>`**（`operator==` 由 optional
  提供），前提是模板参数里没有顶层逗号；`std::optional<std::pair<A,B>>` 这种
  才需要先起一个 `using` 别名。

#### 阶段 9/10 真的踩到的（2026-09）

* **“物理按键”可以自动化，但必须先加一个测试后门。** 钩子照规矩丢弃一切带
  `LLKHF_INJECTED` 的事件（不变量 2），于是 `SendInput` 伪造不了用户按键，
  而 `--simulate` 本期不做 —— 引擎与钩子的行为就只剩“人按键盘”一条路。
  解法是加一个只给测试用的后门 `FLOWKEYD_ACCEPT_INJECTED=1` 抬升那道过滤，
  而且**只改钩子给 `event.injected` 赋值的那一步**，引擎和其余不变量一概不动；
  启用时打一条警告。
  这样 `scripts/acceptance.ps1` 才能从外部观察到“键真的是被吞了”。
* **`Start-Process -PassThru` 拿不到退出码。** 无论加不加
  `-RedirectStandardOutput`，`$p.WaitForExit(6000)` 之后 `$p.ExitCode` 都是空
  （只有用 `-Wait` 启动的那种才会填）。验收脚本也不看守护进程的退出码，
  所以“干净退出”要靠日志（没有 `ERROR`、最后一行是 `keyboard hook removed`）
  加进程表里没有它来判定。
* **`$form.Activate()` 会静默失败。** Windows 的前台锁只允许“当前就在前台的
  那个进程”抢焦点；人家（或者用户）一点别的窗口，我们的捕捉窗口就再也拿不回来，
  后面每一条按键检查都会看起来像产品 bug。修法是照抄守护进程自己的
  `raiseWindow`：`AttachThreadInput` 到当前前台线程 → `SetForegroundWindow` →
  解挂；而且真抢不到焦点时要**明确报一条失败**，而不是让检查看起来像产品坏了。
  （就是这条让我把一次“用户中途点了 Notepad”误判成了两次产品失败。）
* **`Win+F16` 在本机是外壳快捷键。** 我最初拿它当“被吞掉的 Win 和弦”的样本，
  结果在某些外壳状态下它会拉出 `SlideToShutDownHost`（“滑动以关机”），
  看起来就像遮断失效。改用 `Win+S`（失败代价只是弹个搜索框）。
  **选测试和弦之前，先用一个不装钩子的小脚本探一下它会不会动外壳。**
* **弹窗复用之后 `window->isActive()` 可能是陈旧的 `true`。** `activateWindow`
  原来是
  ```
  window->raise();
  window->requestActivate();
  if (window->isActive()) return;      // ← 这里会早退
  ```
  于是 `menu` **第二次**打开（同一个 QML 窗口被重新 `visible = true`）时拿不到
  键盘焦点：Qt 说它 active，而它其实不是前台窗口。这类 bug 只在“用户正聚焦在
  别的应用”时才出现。修法是再问一句真正的状态：
  ```
  if (window->isActive() && GetForegroundWindow() == hwnd) return;
  ```
  然后照旧走 `raiseWindow`；验收脚本现在会断言“重新打开的选单也拿到了焦点”。
  （这个 bug 是阶段 9 的脚本查出来的 —— 阶段 6 的 `tmp/preview` 自己就是前台
  应用，所以看不到它。）
* **命令行里不要相信中文能被 agent 的 bash 看到。** 验收脚本的中文检查名
  经过 agent 的 bash 抓回来是乱码（控制台代码页），所以脚本把**窗口标题与前台
  窗口的类名/码点**写进 `-WorkDir\diag.txt`（UTF-8、不带 BOM）当诊断；
  要看中文结果就 `read` 那个文件，别盯控制台输出猜。
* **`--check` 不带 `--config` 读的是真实配置**（`%USERPROFILE%\.config\flowkeyd\`）。
  过渡期里这是个坑：你以为在检查示例配置，其实在检查用户的。验收/接管脚本里
  一律显式写 `--config`，只有“确认默认搜寻路径对不对”那一条才故意不带。

#### 2026-09 收尾：双击启动（windeployqt）

* **“双击报 找不到 Qt6Gui.dll”不是缺少依赖，而是构建目录里根本没有 Qt 的 DLL。**
  在装上部署之前，`build/windows-release/` 里只有一个 36 MB 的 `flowkeyd.exe`：
  Qt 的 DLL 全靠“`PATH` 里有 `C:\Qt\6.11.2\mingw_64\bin`”这件事在撑着，
  终端里跑得起来只是因为那个 shell 的 `PATH` 是手工加过的，资源管理器双击时
  自然什么都找不到。修法就是构建后自己部署（见第 4 节 CMake 目标划分末段）：
  ```cmake
  if(WIN32 AND TARGET Qt6::windeployqt)
      add_custom_command(TARGET flowkeyd POST_BUILD
          COMMAND Qt6::windeployqt --no-translations
                  --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/qml"
                  "$<TARGET_FILE:flowkeyd>"
          COMMENT "windeployqt: deploying the Qt runtime next to flowkeyd.exe"
          VERBATIM)
  endif()
  ```
  `--qmldir` 必须给：QML 文件是编在 qrc 里的，`windeployqt` 只能靠扫描源码目录
  发现 `import QtQuick.Controls.FluentWinUI3`，否则 `qml/` 半个模块都不会被拷过去
  （现象是 `--version` 正常、一开日志窗口就抱怨模块找不到）。
  `Qt6::windeployqt` 这个 target 要 Qt 6.3+ 才有（本机 6.11，有）。
* **Qt 官方给 MinGW 的那套只带 release 的 Qt DLL，没有 `Qt6Cored.dll`。**
  在 debug 的构建目录里看到 `Qt6Core.dll`（没有 `d` 后缀）是**对的**，
  `windows-debug` 的 exe 也真的能跑（`--version` / 守护进程都验证过），
  别照着 MSVC 的习惯去断言 `Qt6Guid.dll` 存在。MinGW 运行时的
  `libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll` 三个两个 profile
  都会部署。
* **不要用 `--style` 去只部署一种 QML 样式**：本机 6.11 的 `windeployqt` 没有这个选项，
  它把 Basic/Fusion/FluentWinUI3/Material/Universal/Windows **全拷了**（约 7.4 MB）。
  真正的原因是 `LogWindow.qml` 里写着 `import QtQuick.Controls`（基础模块），
  `qmlimportscanner` 无法知道运行时会用 `QQuickStyle::setStyle("FluentWinUI3")`。
  7 MB 不值得为它改 QML 或者加 `--qmlimport` 花招，先放着。
* **验证“双击能起来”要真的把 Qt 从 `PATH` 里拿掉。** 从 agent 的 shell 里
  `Start-Process` 启动的进程继承的是同一个 `PATH`（本来就不含 Qt），
  所以“在 agent shell 里能跑”就已经等价于双击；要断言就用
  `Start-Process ... -RedirectStandardOutput/-Wait` 看退出码与输出（`& exe` 是
  不等 GUI 子进程的，退出码永远是空的，见前面那条）。
* **`.gitignore` 里的 `*.dll`/`*.exe` 让部署出来的文件不会进版本库**，
  所以“构建目录里多出 100 MB DLL”不会污染 `git status` —— 不用为部署动 `.gitignore`。

#### 2026-09 修复：弹窗在滚轮下闪烁（`menu` / `help`）

> **已被下一节取代（2026-09 晚些时候）**：帮助窗口的列表整体换成了 Qt 自带的
> `ListView` + `ScrollBar`，下面这套“模型自己算滚动位置 + 自己清/留悬停”的做法
> 已经被删掉。留着它，是因为“为什么要换”的判断依据在这里。选单（`MenuPopup`）
> 仍然适用（它不滚动）。

* **现象**：鼠标滚轮滚弹窗时高亮“闪一下”——上下箭头完全正常，只有滚轮会。
* **根因**：滚轮走的是 `moveSelection()`，而它为了“键盘接管高亮”会把鼠标悬停
  清掉（`m_hover = -1`）。于是
  高亮先跳到**选中项**、下一帧又被紧随滚轮而至的鼠标微抖（1 px）拉回**光标那一行**。
  两帧之间隔了 ~20 ms，肉眼就是闪一下。本机实测（真实守护进程 + 高速截屏）：
  同一格滚轮前后两帧的差别是 167624 个像素，差别区域正好是整条高亮带。
* **修法（两层，缺一不可）**：
  1. `app::HelpModel::wheel()` 走新的 `moveSelection(delta, clearHover=false)`：
     滚轮**不清悬停**（滚轮只是把列表推上去，高亮应该留在光标那一行）。
     键盘的 `↑`/`↓`/`PgUp`/`Home` 仍然清悬停——那正是“键盘接管高亮”的语义。
  2. `HelpPopup.qml` 的 `onWheel` 在滚完之后按光标位置**重算悬停行**
     （`hoverAt(mouseX, mouseY)`）：列表滚了，同一个物理行现在对应另一条，
     不重算的话高亮会粘在旧的那一条上。两处都在同一个事件处理里做完，
     中间态不会被绘制。
     另外 `wheel.accepted = true`：选单/帮助都不该把滚轮事件漏给别的接收者。
* **`MenuPopup.qml` 的滚轮**：选单不滚动，
  但**必须接受**这个事件，否则它会继续往后冒、白白引起一次重绘。
* **怎么验证这种“一闪而过”的 bug**（`tmp/` 下的临时工具，不进版本库）：
  * 抓帧：`QQuickWindow::grabWindow()` 不够用（它在 GUI 线程上自己渲染一遍，
    看不到合成器的中间帧）。用 `Graphics.CopyFromScreen` 抓弹窗那块的屏幕区域，
    每帧算一个 FNV 指纹，~120 fps（1000x1416 区域约 8 ms/帧），
    打印“与上一帧不同”的帧号与时间戳。
  * 注入：`SendInput` 的 `MOUSEEVENTF_WHEEL` 之后**再注入 1 px 的
    `MOUSEEVENTF_MOVE`**——真鼠标滚轮几乎总会带一点位移，只注入滚轮是复现不出来的。
  * 抓到的帧存成 PNG，用 `imgdiff` 那种脚本打印“不同像素数 + 包围盒”，
    就能一眼看出差的到底是哪一块（高亮带 vs 窗口边框）。
  * 直接用 `tmp/wheel/realdaemon.ps1` 起一个**真实守护进程**（一次性配置 +
    `FLOWKEYD_ACCEPT_INJECTED=1`）比用预览程序更接近用户现场。* **`QCOMPARE(optional<int>, -1)` 是错的**（当年 `hover()` 还在时踩的；现在悬停
  已经删了，但这条适用于任何返回 `std::optional` 的接口）：`-1` 会被隐式构造成
  `std::optional<int>{-1}`（engaged），而“没有悬停”是 `std::nullopt`（disengaged），
  于是断言总是失败（当时要写 `QCOMPARE(model.hover(), std::nullopt)`）。
* **`GetWindowRect` + `SetCursorPos` 必须同一种像素。** 验收脚本现在开头就
  `SetProcessDPIAware()`（见 `FlowInject::DpiAware`），否则矩形是虚拟化过的
  逻辑像素、而 `SetCursorPos` 要物理像素，光标会落到别处去。
  弹窗几何靠**宽度反推缩放**（`menu` 卡片 300、`help` 卡片 500 逻辑像素），
  比用 DPI 猜稳。
* **提权常驻的实例会锁住 `build/windows-release/flowkeyd.exe`。** 它是以用户的
  管理员令牌跑的，agent 的 shell 既 `Stop-Process -Force` 不动、
  `taskkill /F` 也是“拒绝访问”。release 的全量构建因此会卡在最后一个链接步骤
  （`cannot open output file flowkeyd.exe: Permission denied`）。**这时只能请用户
  自己从托盘菜单点“退出”再重新 `Start-Process -Verb RunAs`**；
  debug 目录没被占用，验证可以先用 `build\windows-debug\flowkeyd.exe` 做，
  但 `acceptance.ps1` 的 DoD 仍然必须在 release 的产物上跑完
  （`ctest` 现在跑 debug，见工作约定第 2 条）。

#### 2026-09 重写：帮助窗口改用 Qt 自带的列表（`ListView` + `ScrollBar`）

* **为什么换**：自绘的滑槽/滑块**拖不动**（它只是个 `Rectangle`，没有拖动逻辑），
  而滚轮那条路要求模型自己算 `m_scroll`、自己维护输入/悬停的交互，两次都
  撞出“高亮闪一下”。项目所有者一句话拍板：“不能用 qt 自带的列表控件实现么？
  不要自己绘制”。换完之后：滑块能拖、滚轮是原生的、模型只剩筛选/选中/悬停。
* **最终结构**（`HelpPopup.qml`）：一个 `ListView` **铺满整张卡片**，
  `topMargin = listTop(88)`、`bottomMargin = listBottom(44)` 把表头与底部提示的
  位置让出来；表头/底部提示各有一块**不透明底色**（`z: 1`）盖住列表，
  否则行滚到边缘时会与标题/筛选框叠在一起（实测就是这样）。滚动条是
  `ScrollBar.vertical: ScrollBar { policy: AsNeeded; z: 3 }`，命中宽度 10 逻辑像素、
  从卡片顶到卡片底。
* **`ListView.positionViewAtIndex(..., Contain)` 不能用来“把选中行带进视野”。**
  它只保证行落在**列表自己的矩形**里，**不看** `topMargin`/`bottomMargin`：
  实测 13 条 / 12 行时，让第 12 条可见只把 `contentY` 挪了 4 像素，行基本躲在
  底部提示底下。`positionViewAtBeginning()` 更坑：它想去 `contentY = 0`，被 Qt 的
  `qBound(min=-topMargin, val, max=maxExtent)` 夹到了**最大位置**（实测 -40 而
  不是 -88，也就是“滚到底”）。→ 自己算了
  `HelpModel::scrollTargetY(line, contentY, topMargin, bottomMargin, viewportHeight)`
  （纯算术、有单测），QML 只把结果写回 `listView.contentY`；滚轮/拖动/惯性仍然
  是 Qt 的。
* **列表刚建好时 `contentY` 会被摆到一个“保持滚动比例”的位置**：实测 30 条时
  是 `contentY = 90`（而不是顶部的 `-88`），而且这一下发生在我们收到
  `selectedChanged`（→ 摆选中项）**之后**。修法：除了模型信号，再在 `ListView`
  的 `onContentHeightChanged` / `onHeightChanged` 里调一次同一个幂等的
  `followSelection()`——“行已经在行区域里”时它什么都不改，所以不会干扰用户
  自己滚出来的位置。
* **合成的 `QWheelEvent` 验证不了滚轮。**
  `QCoreApplication::sendEvent(window, &wheelEvent)` 递进去之后
  `event.isAccepted() == false`、列表一点都不动（`ScrollUpdate` 相位也一样）。
  要验证滚轮只能 `SetCursorPos` + `SendInput(MOUSEEVENTF_WHEEL)`，而且**弹窗
  必须是前台窗口**：`WM_MOUSEWHEEL` 送给**焦点**窗口，背景窗口收不到（现象：
  滚轮/拖动全无效而悬停却正常，看上去像两套 bug，其实是同一个环境问题）。
* **滚轮方向**：本机实测（一个 WinForms `ListBox` 当原生基线，加上 Qt 的
  `ListView`）**两者一致**：`mouseData = -120` 往下、`+120` 往上。
  自绘时期的 flowkeyd 是反的（`zDelta > 0` → 选中项往列表后面走）；
  改用 Qt 自带的列表之后就跟着系统走了，脚本里帮助那段是 `Wheel(-120)`。
* **滚动条那个 QML 类型的类名是 `ScrollBar_QMLTYPE_<n>`**，不是 `QQuickScrollBar`
  （`ScrollBar.qml` 是个 QML 文件）。在 `tmp/preview` 那种“遍历 item 树找控件”
  的工具里用 `contains("ScrollBar")` 匹配；它的 `x = 490 / w = 10 / h = 708`，
  `position`/`size` 就是滑块的位置/大小。
* **悬停与命中的那一套后来被整个删掉了（2026-09）**：当时是 `HoverHandler`
  （被动、不抢滚轮）在卡片上追光标，`indexAt(x + contentX, y + contentY)` 算行
  下标，再 `setHover()` 给模型，`onContentYChanged` 里再重算一次。这套东西的
  副作用就是“拖动滚动条会改高亮” —— 见下一节的记录。留下来的那条有用的知识：
  **滚轮在表头/底部提示上也能滚**，靠的是 Qt 的“未被接住的指针事件继续递给
  指针下面其他 item” —— 那两块只是普通的 `Rectangle`，不接滚轮，正好让下面的
  `ListView` 接住。
* **桌面被锁时（`LogonUI` 在跑）这些验证全都做不了**：`GetForegroundWindow()`
  返回 0、`SendInput` 报 `5`（ACCESS_DENIED）、弹窗抢不到前台。
  `scripts/acceptance.ps1` 会在“捕捉窗口拿到了键盘焦点（正对照）”那一条挂掉，
  看上去像产品 bug，其实是环境。先看 `LogonUI` 在不在。

#### 2026-09 修复：取消帮助窗口的悬停高亮（拖动滚动条不再改高亮）

* **现象**（项目所有者报）：在 `help` 弹窗里拖右侧滚动条，高亮会在行之间乱跳
  —— 看起来像“拖动滚动条改变了选中的列表项”。按照系统列表控件的语义，滚动
  不该动选中项/高亮。要求：取消这个非标准的自定义行为。
* **根因**：高亮取的是 `activeLine()`，当时写成
  `m_hover >= 0 ? m_hover : m_selected`，而 QML 侧有一个 `HoverHandler` 追光标，
  `syncHover()`（还有 `onContentYChanged` 里那一次重算）用 `ListView.indexAt()`
  把“指针下那一行”写成 `m_hover`。**滚动条就在 `ListView` 的矩形里面**，
  `indexAt()` 只看行矩形、不区分“指针是不是压在滚动条上”，于是一拖动，
  `contentY` 每变一点、`m_hover` 就换一行，高亮跟着指针跑。
* **修法**（直接删，不做特例）：
  * `HelpModel`：删掉 `m_hover`/`hover()`/`setHover()`，`activeLine()` 直接用
    `m_selected`（夹在 `[0, visibleCount-1]`）；`refilter()` 不再复位悬停；
    `moveSelection(delta, clearHover)` 的私有重载合并回单参数版本。
  * `HelpPopup.qml`：删掉 `HoverHandler`、`syncHover()`、`rowIndexAt()` 与
    `onContentYChanged` 那行。**高亮从此只有 `moveSelection()` 会改**
    （`↑`/`↓`/`PgUp`/`PgDn`/`Home`/`End`）；滚轮、拖滑块、点鼠标都不动它。
    委托上的 `TapHandler`（点一行 = 复制那一行）保留，本来就有。
  * README 的「快捷键帮助」那两条跟着改成“悬停不改高亮、`Enter` 复制的永远是
    键盘选中项”。
* **验收脚本怎么从外面看“列表真的滚了”**（这是本次最需要想的一点）：以前靠
  “把光标悬停在某一行上再 `Enter`，看剪贴板复制到哪一条”当外部证据 —— 悬停
  没了之后这条路断了。改用**左键点某一行 = 复制那一行**（委托的 `TapHandler`）：
  同一个屏幕位置在滚动前后复制到**不同的行**，就证明列表滚了；再按一次 `Enter`
  （复制键盘选中项，永远是第 1 行），就证明滚动没有把选中项带走。
  为此给 `FlowInject` 加了一个 `Click(x, y)`（`SetCursorPos` + 左键按下/抬起）。
  帮助那一段现在是 12 项检查，脚本总数 **84**（原 82）。
* **教训**：QML `ListView` 自带的 `ScrollBar` 在 `ListView` 的矩形**内部**，
  任何“用 `indexAt(指针位置)` 算悬停/命中”的写法都必须自己排除滚动条那一块
  （或者干脆别让悬停参与选中语义）。

#### 2026-09 重做：帮助窗口的筛选框与列表项换成标准控件（鼠标点得中）

* **现象**（项目所有者报）：帮助窗口里**鼠标点不中列表项**，上部的筛选框也
  **点不进去**、进不了输入状态。要求：用标准的 input 控件实现，整个界面尽量
  别出现自绘控件。
* **根因**：那两处本来就是自绘件 —— 筛选框是 `Rectangle` + `Label` + 一根
  `Rectangle` 光标拼的，点它当然没反应；列表项的点击只有一个 `TapHandler`，
  点了既不选中（高亮只跟键盘选中项走）也没有可见反馈，看起来就像“点不动”。
  列表还铺满整张卡片，靠两块不透明底色遮住滚进来的行。
* **改法**：筛选框换成真正的 `TextField`（`onTextChanged` 回写模型），每一行
  换成 `ItemDelegate`（`onClicked` = `setSelected(index)` + `helpCopy`），列表缩到
  行区域里、两块遮罩与 `scrollTargetY()` 一起删掉，键盘带选中项进视野改用
  `positionViewAtIndex(line, Contain)`。
* **`setFilter()` 必须是 `Q_INVOKABLE`。** 它原来是普通的 C++ public 方法
  （只有 `clearFilter()` 是 `Q_INVOKABLE`），QML 里 `model.setFilter(text)` 会抛
  `TypeError: Property 'setFilter' ... is not a function`。**这个错误在预览程序里
  既没打到 stderr，也没让 QML 处理器整体停下来**（处理器里它后面的语句被跳过），
  表现为“信号发了、处理器跑了、模型就是不变”，非常误导。
  诊断手法：在处理器里把中间状态写到一个能观察的属性上（当时是
  `filterField.placeholderText = "EDITED:" + ...`），再从 C++ 读回来。
  **教训：QML 要调的 C++ 方法一律加 `Q_INVOKABLE`，别指望异常会自己叫。**
* **`ItemDelegate` 的 padding 覆盖不掉。** FluentWinUI3 的 `ItemDelegate.qml`
  用 `topPadding: __config.topPadding || 0 + verticalOffset` 这类绑定给每个实例
  定内边距（实测左右 12 / 上下 8），**在实例上写 `padding: 0` 会被这些绑定压
  回去**，于是 `contentItem` 只有 `500x48` 里的 `476x32`，写死
  `height: rowHeight(46)` 的子项会溢出到下一行。**不要和样式的内边距较劲**：
  内容区里的东西全部锚在 `contentItem` 上（`anchors.top` + `anchors.bottom`），
  让它自适应。
* **模型角色名不能叫 `highlighted`。** 委托是标准 `ItemDelegate`，它自己就有
  `highlighted` 属性（标准样式用它画高亮），而委托里 `required property bool
  highlighted` 的名字必须等于模型角色名 —— 撞名就声明不了。角色改名
  `rowSelected`，委托里 `highlighted: rowItem.rowSelected`。
* **`QQuickTextInput` 故意忽略 `↑`/`↓`**（Qt 6 起，源码注释写着 “Don't allow
  MacOSX up/down support”），所以它们会冒到父项的 `Keys`；而 `Keys` 的默认
  优先级 `Keys.BeforeItem` 意味着**挂在 `TextField` 上的 `Keys.onPressed` 在
  `TextInput` 自己的键盘处理之前跑**，没接住的键原样放行就是标准的打字/退格/
  `Home`/`End`。这就是“一个输入框 + 一个列表”的键盘分工：模型只接导航键、
  `Enter`、`Esc`，编辑键全给输入框。
* **筛选回写用 `onTextChanged` + 等值判断，不要 `onTextEdited`。**
  `textEdited` 只在“逐字编辑”那条路径上发；输入法提交中文、粘贴、拖选文本
  不一定发。`textChanged` 一定会发，而 `if (field.text !== model.filter)` 这一句
  保证我们自己同步过去的文本不会被当成用户输入回吐一次（不会来回振荡）。
* **进程内预览里 `console.log` 不一定看得见。** 当时它一行都没出现在 `stderr`
  重定向里（而 `fprintf` 的输出都在），别拿它当调试的唯一手段；用“写一个能从
  C++ 读回来的属性”更可靠（见上面那条）。
* **QML 类型的类名带 id 后缀。** `TextField { id: filterField }` 在
  `metaObject()->className()` 里是 `TextField_QMLTYPE_1544` 这种，预览程序里找
  控件要用 `contains("TextField")` 而不是相等比较（`ListView` 没有 id 时才是干净
  的 `QQuickListView`）。
* **验收脚本跟着改了两处**：滚动条现在只铺在行区域上（`listTop 88` 到
  `卡片高 - listBottom 44`），所以拖动起点从“卡片顶下方 30”改成“行区域里靠上”
  （100）；另外“拖动/滚轮不改选中项”的判据改成**先不点击直接 `Enter`**
  （点选现在会改选中项，旧的判据会误报）。

#### 2026-09 新增：帮助窗口里 `Enter`/双击直接执行动作

* **“执行”的入口是 `HelpRequest::onRun`（条目下标），不是 `HelpEntry` 里带动作。**
  模型层（`flowkeyd_models`）只认 `HelpEntry::destructive` 这个布尔量，
  `core::Action` 一概不进模型；动作列表由 `dispatcher.cpp` 的 `openHelpAction()`
  建好并捕获在 `onRun` 里（绑定是 `press` + `release` 两串接起来，`remap` 是
  `remap.press` / `remap.release` 两串 `SendOp`，用 `win::sendOps` 注入）。
  这与 `menu` 的 `onChoose` 是同一套分工：弹窗从不执行动作。
* **`helpRun(index)` 拿到的是可见行下标，回调要的是条目下标。** 筛选之后
  “第几行”与“第几个条目”不是一回事：`HelpModel::itemIndexForVisible()` 换算。
  忘了换算的表现是“**筛选之后按 `Enter` 执行的是另一条的动作**”，
  而且看起来很像“动作没跑”（当 `targets[0]` 恰好是个 `none()` 时）。
  这个 bug 是验收脚本里「点选之后 `Enter` 执行的就是刚点中的那一行」与
  「危险动作第二次 `Enter` 真的执行了」两条检查查出来的，
  所以那两条要留着（它们各自钉住了筛选与危险动作下的映射）。
* **先关窗再执行。** `send`/`type`/`window` 作用在**前台窗口**上，而弹窗刚才是
  前台；不先 `visible = false` 就会把按键打进自己的筛选框（`menuChoose` 早就是
  这么做的）。验收脚本因此每个检查组都要重新开窗（`OpenHelp`）。
* **双击前 Qt 会先发两次 `clicked`，`setSelected(同一个下标)` 不能清武装状态。**
  否则“第二次双击”又变成“第一次确认”，危险动作永远执行不了。
  所以 `setSelected()` 只在选中项**真的变了**时才 `disarm()`（`moveSelection()`
  没有这个问题，它本来就只有变了才动）。单元测试
  `doubleClickActivatesTheRow` 盯着这一条。
* **单击仍然是“选中 + 复制”。** 项目所有者选的：双击/`Enter` 是执行，
  复制保留在鼠标上（也是验收脚本用来从外面读出“屏幕这一行现在是哪一条”的
  唯一手段）。不要把 `onClicked` 的 `helpCopy` 删掉，否则
  「拖动/滚轮真的滚了列表」那几条检查就没法从外部观察了。
* **危险动作的判定在 `core::isDestructive()`**（`quit`/`suspend`/`power`）。
  `menu` **不算**：它只是把选单弹出来，真正的危险条目在选单里还有一次选择。
  验收脚本用可逆的 `suspend` 验证“第二次 `Enter` 真的执行了”
  （`quit` 执行了脚本就没法接着跑了），之后立即用 `Ctrl+Alt+F11` 恢复。
* **“待确认”的视觉是模型的一个角色（`rowArmed`）+ 改掉的 `footerText`。**
  角色名不能叫 `highlighted`（标准 `ItemDelegate` 占了），
  `footerText` 因此从 `CONSTANT` 变成 `NOTIFY stateChanged`。
  那一行底色用了一个硬编码的琥珀色（`#E8A33D`，`opacity 0.22`）：
  系统 `palette` 里没有警告色，这是整个帮助窗口里唯一一处硬编码颜色。
* **`Esc` 现在是三级**：取消待确认 → 清筛选 → 关窗。
  验收脚本里对应“第一下只取消确认、再一下只清筛选、第三下才关窗”。
* **验收配置的第一行必须有一个能从外面看到的动作。**
  脚本新增了 `accept-help-first`（`Ctrl+Alt+F19` → `clipboard set HELP-FIRST`），
  因为 `Enter` 执行的是“第 1 行”，而原来的第一行 `accept-swallow` 是 `none()`
  —— “执行了”与“什么都没执行”从外面分不出来。
  快捷键总数因此从 14 变成 15、帮助条目从 17 变成 18。

#### 2026-09 重构：电源选单也用 Qt 自带的列表（`ListView` + `ItemDelegate`）

* **为什么改**：项目所有者要求「用类似帮助信息弹窗的方法重构电源管理选单，
  同样尽量不要用自绘控件，用 Qt 自带的控件」。旧实现是 `Repeater` + 自绘 `Item`
  （自己画高亮底、自己画徽标），外加一个铺满卡片的 `MouseArea` 拿
  `MenuModel::hitTest()` 做命中测试 —— 命中测试要自己算矩形，就必然要与
  行几何保持同步（也就是 `rowRect`/`badgeRect`/`labelRect`/`hintRect` 四个角色
  存在的唯一理由）。现在这些全删了：列表就是一个真正的 `ListView`，
  每一行是标准的 `ItemDelegate`。
* **行几何不再进模型**：`MenuModel` 只留卡片外框（`cardWidth`/`cardHeight`/
  `titleRect`/`footerRect`）与几个排版常量（`listTop`/`rowHeight`/`rowSpacing`/
  `rowInset`/`badgeSize`）。`listTop` **不是常量**：写了 `title` 时是 40，
  没写时是 10（`NOTIFY itemsChanged`）。
* **角色名不能再叫 `highlighted`/`hovered`**：委托是标准 `ItemDelegate`，
  它自己就有这两个属性（`highlighted` 用它画高亮、`hovered` 是它的只读状态），
  而 `required property` 的名字必须等于模型角色名 —— 撞名就声明不了。
  所以高亮角色改叫 `rowSelected`（与 `HelpModel` 同一套命名），而且模型里
  **不再有** `hovered` 角色：鼠标在哪一行由委托自己的 `hovered` 报告。
* **悬停仍然归模型管**（与帮助窗口不同）。帮助窗口的悬停高亮已经删了
  （拖动滚动条会改高亮，见上面那节），但**选单不滚动**，而「光标压在哪一条、
  `Enter` 就执行哪一条」是菜单的标准语义（`MouseArea` 时期就是这样），
  所以 `hover`/`setHover` 留着，只是驱动它的人换成了委托：
  `ItemDelegate { onHoveredChanged: if (hovered) model.setHover(index) }`。
  指针**离开整张卡片**时谁把悬停清掉？卡片上一个 `HoverHandler`
  （`onHoveredChanged: if (!hovered) setHover(-1)`）。
  * 别用铺满卡片的 `MouseArea` + `hoverEnabled` 干这件事：它会被后声明的
    `ListView` 盖住，委托收不到悬停；声明在前面又会在指针进入委托时
    收不到 `onExited`。`HoverHandler` 是被动的，不会挡委派自己的 `hovered`
    （进程内实测：悬停第 4 行 → `hover()==3`，移出卡片 → `hover()==-1`）。
* **`ItemDelegate` 的内边距实测是左右 12 / 上下 8**（`contentItem` 就是
  `276x24`，行宽 300），把内容锚在 `contentItem` 上就不会溢出（与帮助窗口
  那节同一个坑）。卡片里行宽就是卡片宽（旧的 `rowRect` 是内缩 10 的），
  高亮底是标准样式给的（左右各内缩 4 + 左边一条主题色竖条），看着更像菜单。
* **滚轮要接住但什么都不做**：卡片上挂一个 `WheelHandler` 把事件 `accepted`
  掉（`ListView` 的高度就是内容高度，本来也滚不动；`interactive: false`
  连 flick 也不给它）。
* **卡片几何没变，所以验收脚本的坐标没变**：行距仍然是 40
  （`rowHeight 38 + rowSpacing 2`），`listTop` 仍然是 40（有标题时），
  所以旧的滚轮检查里那个「第 1 行中线 y=99」仍然准，新加的点选检查用的是
  「第 0 行中线 y=60」。**改行距/`listTop` 必须同时改 `acceptance.ps1` 里
  那两处坐标**。

#### 2026-09 坑：正在跑的实例锁住它的 exe 时可以用改名绕过

* 常驻实例会锁住**它自己那个** `flowkeyd.exe`（自注册方案下通常是
  `build\dist-release\flowkeyd.exe`），release 全量构建会卡在最后一个链接步骤
  （`cannot open output file flowkeyd.exe: Permission denied`）。
* **常规解法是 `--quit`**（本机 agent 的 shell 是提权的，能停提权实例；
  见工作约定第 5/11 条）：停 → 构建 → 从 `build\dist-release` 重新拉起
  （启动时会自动把计划任务刷新成这个路径）。
* **改名只是“停不掉时”的降级手段**：运行中的 exe 允许改名（内核映像按区域映射，
  文件本身可以 `Move-Item`）。把它改成 `flowkeyd.exe.locked`（在 `.gitignore`
  的 `/build*/` 里，不污染仓库），链接就能照常跑，`windeployqt` 也不会因为已加载
  的 Qt DLL 而失败（实测）。
  * 副作用：常驻实例继续以旧二进制运行（它的映像文件换了名字），而
    `flowkeyd.exe.locked` 只有停掉实例之后才能删。
  * **还是要告知用户**：常驻实例现在跑的是旧构建，要重新启动一次。

#### 2026-09 修复：和弦不能是两个普通键 + 启动时的配置错误弹窗

* **`keys = "NumpadSub+NumpadAdd"` 不是「两键同按」，而是语法错。** 和弦的结构是
  `Modifiers + 一个按键`（`core::Chord`），最后一个 `+` 片段才是按键，前面的片段
  必须是修饰键。`NumpadSub` 不是修饰键，于是 `--check` 报
  `` `NumpadSub` in chord `NumpadSub+NumpadAdd` is not a modifier `` 与
  `numpad-mute: no usable keys`，守护进程直接拒绝启动。
  想做这种“组合”，只能用修饰键（`Ctrl+NumpadMult`）或换一个独立的键。
  已加 `tst_keys::rejectsBadChords` 与 `tst_config::twoPlainKeysAreNotAChord` 盯住，
  README 的和弦语法一节也写了这个限制。
  用户本机真实配置因此把小键盘静音从 `NumpadSub+NumpadAdd` 改成 `NumpadMult`
  （小键盘 `*`）。
* **守护进程模式下配置出错必须弹窗。** 双击启动的 flowkeyd 没有控制台，
  以前只把错误写进 `stderr`（拿重定向才看得到），用户看到的是“双击了没反应”。
  现在 `main.cpp` 的 `reportConfigFailure()` 在 `loadConfig` 失败（含旧 `.toml`
  拦截）时：先写 `stderr`，再建一个 `QApplication` + `QMessageBox`（标题
  `flowkeyd 配置错误`，正文是可选中复制的英文错误），确认后 `return 1`。
  **离线命令不算**：`--check` / `--list` 在那之前就 `return` 了，照旧只打印、
  不弹窗、不提权（不变量 11）。
* **`buildEnvironmentBlock` 的“不区分大小写排序”用错了折叠方向。** 它原来用
  `QString::compare(..., Qt::CaseInsensitive)`（**折成小写**），而 Windows
  （`RtlCompareUnicodeString(..., TRUE)`，以及系统自己给出的环境块）是**转成大写**
  再比码元。两者在 `_`(0x5F) 与字母上顺序相反：本机有 `NU_VERSION=0.115.1`，
  于是 `NU_VERSION` 与 `NUMBER_OF_PROCESSORS` 被排反了（系统自己的块里
  `NUMBER_OF_PROCESSORS` 在前，`GetEnvironmentStringsW` 实测）——
  `tst_command_line` 因此失败。改成 `entryName(...).toUpper()` 比较，
  并给测试加了一对固定的 `AAB` / `A_Z` 覆盖项，不再依赖机器上碰巧有什么环境变量。

#### 2026-09 新增：启动时的两个交互确认（已在运行 / 开机自启）

* **为什么用原生 `MessageBoxW` 而不是 `QMessageBox`**：这两个提示都跑在
  `QApplication` 构造**之前** —— 「已在运行」必须在提权之前（否则重复双击会白弹
  一次 UAC），而「开机自启」在守护进程的现有顺序里也在 `QApplication` 之前。
  `MessageBoxW` 不需要 Qt 应用对象，于是 `QApplication` 的构造时机不用动。
  代价是：这两个框是系统原生样式，而配置错误那个仍然是 `QMessageBox`。
* **「已在运行」怎么在提权之前看得见**：新增 `platform::win::instanceRunning()`，
  用 `OpenMutexW(SYNCHRONIZE)`（**不获取所有权**）查命名互斥体在不在。
  为了让**提权实例**创建的互斥体也能被非提权进程打开，互斥体改用与 `--quit`
  事件**同一套** SDDL（`D:(A;;GA;;;<用户 SID>)S:(ML;;NW;;;LW)`，压到 Low 完整性）
  —— 否则 `CreateMutexW` 要的写权限会吃 no-write-up。
  （`OpenMutexW` 只要读权限，本来大概率也能过；带上 SDDL 是为了两边都不靠运气。）
* **拒绝的路径要真的验一次**：确认框返回 false 时任务必须**原封不动**。
  实测（一次性配置 + 提权实例，`build\windows-debug`）：弹框后
  `CloseMainWindow()`（= WM_CLOSE = IDCANCEL）→ 日志
  `logon autostart task `flowkeyd` left unchanged: the user declined to update it`，
  并且 `schtasks /Query /TN flowkeyd /XML` 前后逐字节相同。
* **`--no-prompt` 是给自动化的**：`scripts/acceptance.ps1` 的「同配置的第二个实例
  被拒绝」那一条用 `-Wait` 等进程退出，弹框就会**永远卡住**。加了这个开关之后，
  第二个实例照旧「日志 + 退出码 1」，只是不弹框。没有它脚本没法跑。
* **对话框开着的时候 `--quit` 叫不动这个进程**：这两个框在 `QApplication::exec()`
  之前，`QWinEventNotifier` 还没开始转，所以框开着时 `--quit` 只能等到 10 秒超时、
  报 `still running`。正常路径（任务已经匹配、没有别的实例）不会弹框，
  用户点掉框就恢复了；只是脚本别在框开着的时候指望 `--quit`。
* **测试脚本里不要用 `$Pid` 当参数名**（PowerShell 自动变量，只读），
  与第 10 节的 `$args` 是同一类坑；同理 `Start-Process -PassThru` 的
  `ExitCode` 在 `-RedirectStandardOutput` 下仍然是空的（见本节另一条）。

#### 2026-09 修复：弹窗里的中文落到了宋体（改成微软雅黑）

* **现象**（项目所有者报）：`menu`（电源选单）与 `help` 两个弹窗里的中文看起来
  不对（宋体/衬线）。
* **根因**：FluentWinUI3 给控件的默认族是 **`Segoe UI Variable`**（本机实测，
  `QFontInfo` 报的就是它），它**没有中文字形**，于是中文字符走 Qt 的逐字回退，
  fallback 落到了宋体上 —— 跟同一行里的西文/数字摆在一起很违和。
  用 `tmp/fontcmp.qml`（两串一样的中文，一串用默认族、一串显式 YaHei）
  抓的对比图能一眼看出来：默认族是衬线，YaHei 是无衬线。
* **修法**：两个窗口各自加一个
  `readonly property string uiFontFamily: "Microsoft YaHei"`，
  然后把卡片里**每一个会画字的控件**都写上 `font.family: root.uiFontFamily`
  （选单 5 处：标题 / 徽标 / 副标题 / 条目名 / 底部提示；帮助 9 处：标题 /
  计数 / 筛选框 / 两个圆点分隔符与徽标文字 / 说明 / 动作摘要 / 空结果提示 /
  底部提示）。
* **QML 的 `font` 值类型只有 `family`，没有 `families` 列表**（Qt 6.11 的
  `qmltypes` 里只导出了 `family`），所以没法写“西文 Segoe UI + 中文雅黑”的
  回退表：指定 YaHei 等于整张卡片都用它。YaHei 自带西文字形，够用。
* **`Window`/`Item` 没有 `font` 属性**（`property("font")` 返回无效
  `QVariant`），字号/族不会自动往子项传，所以只能逐个控件写；
  字号仍然照旧由每个控件自己的 `font.pointSize` 定，只覆盖族。
  标准委托 `ItemDelegate` 自己也有 `font`，但它的字是我们放在
  `contentItem` 里的 `Label`，不用管它。
* **怎么从机器上验证“真的是雅黑”**：`tmp/preview` 里加了一组检查 —— 遍历窗口
  的 item 树，收集所有 `property("font")` 有效的控件，只看真的会画出字来的
  那几个（`text` 或 `placeholderText` 非空），断言 `font.family()` 是
  `Microsoft YaHei`，并把 `QFontInfo(font).family()` 一起打出来（它显示
  `Microsoft YaHei` 就说明这个族真的存在、没被回退掉）。
  本次结果：选单 17 个文字控件、帮助 120 个，全部命中，0 个例外。
* **顺手修了预览工具里一条假失败**：`menu: reopening resets the highlight`
  在改动前后都会挂 —— 因为上一步的 `QTest::mouseClick` 把光标留在了某一行上，
  窗口重新出现在光标底下时 Qt 会把那一行算成**悬停**（`hover()==1`），
  不是高亮没复位。现在先 `mouseMove` 把指针挪出卡片再断言
  （`highlight=0 hover=-1`），工具重新全绿（`failures: 0`）。
  **这类“工具自己造成的失败”要当场区分开**，否则下次会当成产品 bug 白查一遍。
* **行为变化**：`menu`/`help` 里的文字一律用微软雅黑（西文也跟着用 YaHei），
  不跟随系统字体设置；配色仍然跟随系统 `palette`。README 的「选单与电源」
  「已知限制」两节已同步。

#### 2026-09 新增：开机自启（计划任务 + 自注册 + `--quit` / `--remove-autostart`）

* **为什么只能是计划任务。** 自启要同时满足两件事：**提权**（否则电源动作、
  驱动提权窗口都没了）与**不弹 UAC**。`shell:startup` 快捷方式与
  `HKCU\...\Run` 都做不到（UAC 会在登录时拦一次，而 secure desktop 上的
  弹窗没人点）；Windows 服务更不行：session 0 里 `WH_KEYBOARD_LL` 看不到
  桌面按键，也没有托盘。只有计划任务的 `RunLevel=HighestAvailable` 能做到。
  也**没有**给 exe 登记 `RUNASADMIN`：那会让 `--check` 也弹 UAC。
* **计划任务的默认值几乎全是坑**（用 `schtasks /Create` 或手点向导都会中）：
  * `ExecutionTimeLimit` 默认 **`PT72H`** —— 三天后任务计划程序会亲手把
    守护进程停掉（现象：用了三天，快捷键突然全失效）。必须写 `PT0S`。
  * `DisallowStartIfOnBatteries` / `StopIfGoingOnBatteries` 默认**是开**的
    （“只在交流电时启动 / 掉电就停”）。本机是台式机、没电池，看起来无害，
    但换机器（或插上 UPS/笔记本）就变成“登录后没反应”。显式写 false。
  * `schtasks` 的默认触发器不能用：只能用 **BootTrigger** 或 LogonTrigger，
    而 BootTrigger 跑在 **session 0**（没桌面）—— 必须 `LogonTrigger`
    \+ `LogonType=InteractiveToken` + 指定 `UserId`。
  * 登录触发器要加 **15 秒延迟**：启动得太早 explorer 还没就绪，托盘图标就
    拿不到（本版本没处理 `TaskbarCreated`，explorer 重启后也不会自己补上）。
  * `MultipleInstancesPolicy=IgnoreNew` + flowkeyd 自己的单实例互斥体，双保险。
  * `WorkingDirectory` 写 exe 所在目录：`--config` 的相对路径与模板里的 `{cwd}`
    都看它。
  * `RestartOnFailure`（PT1M x3）值得留：崩了能自己回来，而**正常退出
    （退出码 0）不会触发重启**，所以托盘/`quit`/`--quit` 退出后不会被拉起来。
  * 完整 XML 在 `src/platform/win/autostart.cpp` 的 `buildTaskXml()`（纯函数、
    有单测），注册走隐藏的 `schtasks.exe /Create /TN flowkeyd /XML <临时文件> /F`
    （XML 以 UTF-16LE + BOM 写到 `%TEMP%`，用完就删）。**不用 Task Scheduler 的
    COM 接口**：那要手写十来个 vtable（本仓库风险最高的做法，见第 3 节），
    而 `schtasks` 是系统自带的稳定入口。查询用 `schtasks /Query /TN ... /XML`
    再解析 `<Command>`（纯函数 `taskXmlCommand`）。
* **任务指向“当前运行的 exe”，因此没有安装目录这回事。** 项目所有者 2026-09 拍板：
  把 exe 拷到哪儿就在哪儿生效，不要预设目录（旧方案是镜像到
  `C:\Program Files\flowkeyd`，已删除）。代价是任务可能指向 `build\dist-release`
  这类会被清理 / 重建的目录，而且正在跑的实例会锁住那个 exe；用两条规则兜住：
  * 每次启动都自检：任务缺失、或指向的 exe 与当前运行的这一个不同，就重新注册
    （所以“把 exe 换个地方再跑一次”就能自愈）；
  * **开发 / 测试实例（`--no-elevate`、`--allow-multi`、未提权、`--no-autostart`）
    绝不碰任务** —— 否则验收脚本（它用 `--no-elevate` 起临时实例）会把用户真实的
    开机自启劫持到 `build\windows-release`。
  计划任务指向失效路径是**完全静默**的失败（没托盘图标、快捷键不生效，不弹任何
  东西），这也是为什么上面那两条不能省。
* **`--quit`：给“从外面干净停掉守护进程”开一条通道。** 之前只有
  `taskkill /PID`（日志窗口开着时会被 `WM_CLOSE` 吃掉 / 不带 `/F` 无效）与
  `taskkill /F`（留幽灵托盘图标）两条烂路。做法：
  * 守护进程用 `CreateEventW` 建一个 `Local\flowkeyd-<散列>-quit`
    自动重置事件，用 `QWinEventNotifier`（QtCore，不用自己写线程）在 GUI
    线程上监听，收到就 `requestShutdownFromAnyThread()` —— 走的就是托盘“退出”
    那条 `performShutdown()`。
  * 客户端 `--quit` 先 `OpenEventW`，找不到就是“没有在跑的实例”（不是错误）；
    然后 `SetEvent`，再轮询 `OpenEventW`（**关掉自己的句柄之后**，因为句柄本身
    会吊住对象）直到对象消失 —— 那就是“真的退干净了”，脚本可以接着替换 exe。
  * **坑：完整性级别的 “no write up”。** 默认安全描述符建出来的对象带着创建者
    的完整性标签（提权的守护进程是 High），而 `SetEvent` 要的
    `EVENT_MODIFY_STATE` 算**写**权限 —— 非提权的 `--quit` 会直接吃
    `ERROR_ACCESS_DENIED`。所以事件用一个手工拼的 SDDL 建：
    `D:(A;;GA;;;<当前用户 SID>)S:(ML;;NW;;;LW)`，即把对象的强制标签压到
    **Low**，任何级别都能写它。拿不到 SID 时退回 `WD`（Everyone）。
    **怎么验证这种“只在跨权限时才会挂”的东西**：注册一个
    `RunLevel=LeastPrivilege` + `InteractiveToken` 的一次性任务，让它去跑
    `flowkeyd.exe --quit` 并把输出/退出码写进文件 —— 提权的 shell 里
    `runas /trustlevel` 之类都不如这个可靠（任务本身就能拿到 medium IL 的现场）。
* **守护进程一直把日志文件开着写**，所以 `[System.IO.File]::ReadAllLines` /
  `ReadAllText` 会报“文件正由另一进程使用”：它们要的是 `FileShare.Read`，
  与写句柄不兼容。读它要用 `FileShare.ReadWrite` 的 `FileStream`
  （`Get-Content` 默认就是 ReadWrite，所以没事）。
  （这也是早期那些“怎么读日志都拿到 0 行”的怪现象的来源。）
* **PowerShell 的数组 splatting 不能用来转发命名参数。**
  `& script.ps1 @arrayOfDashNames` 会报
  `找不到接受实际参数"-TaskName"的位置形式参数`（而且报的还是**里面**那一个
  调用，非常误导）。转发命名参数一律用**哈希表 splatting**：
  `$splat = @{ TaskName = $x }; & script.ps1 @splat`。
  （这条是 `install.ps1` 时代踩到的；脚本虽然删了，但写验证脚本时还是会碰到。）
* **正在跑的 exe 写不得、也删不得，但可以改名。** 提权常驻实例会锁住它自己那份
  `flowkeyd.exe`，于是 release 全量构建卡在链接（`cannot open output file`）。
  现在的常规解法是 `--quit` 停实例；实在停不下来时可以把被锁的 exe
  `Move-Item` 成 `*.locked`（内核映像按区域映射，运行中的文件可以改名），
  链接就能照常跑（见本节后面的“改名绕路”）。
* **`QCoreApplication::applicationDirPath()` 在没有 `QApplication` 时会警告并
  返回空串**（`QCoreApplication::applicationDirPath: Please instantiate the
  QApplication object first`）。它在 `core::configPathCandidates()` / 
  `legacyTomlCandidates()` 里被调用，而这两条都在 `QApplication` 之前跑
  （离线命令与守护进程都是），于是：**文档里写的“exe 同目录”候选实际是失效的**
  （`exeDir` 是空串，直接跳过），而 stderr 重定向时还会多一行 Qt 警告。
  这是个预先存在的小缺陷，不在本次自启任务的范围里，**没改**；
  要改就得给 core 一个不依赖 Qt 实例的 exe 目录来源（例如
  `core::setExeDirectory()`，由 main 从平台层传进去），两处调用点都得改。
* **本机 agent 的 `powershell.exe` 是提权的**（`IsInRole(Administrator)` 为真，
  实测能注册“最高权限”任务、能 `Stop-Process` 提权进程）。之前 AGENTS 里
  “agent 的 shell 没有提权”的结论在这台机器上不成立；但**别依赖它**，
  脚本里仍然要自己检查管理员。
* **提权实例被强杀会留下幽灵托盘图标**（explorer 不会马上发现进程没了）。
  所以停实例优先 `--quit`；`Stop-Process -Force` 只当兜底，而且要在脚本里说明。

#### 2026-09 修复：验收脚本在双屏下坐标错位（“拖动滚动条”那条检查时好时坏）

* **现象**：`scripts/acceptance.ps1` 跑到帮助弹窗那一组时，
  「拖动滚动条真的滚了列表（同一位置已经换了一行）」时会挂。挂的时候连看两次
  都是同一条，但换一次运行又变绿，很难归因。
* **抓因的手法**（值得收藏）：把脚本**复制到 `tmp/`**，用
  `Copy-Item` + `[System.IO.File]::ReadAllText/WriteAllText(..., UTF8Encoding($true))`
  做字符串替换（**必须保 BOM**，否则脚本里的中文断言会变成乱码），插入
  `Diag(...)` 把现场写进 `%TEMP%\flowkeyd-accept\diag.txt`；
  **然后一定要用 read 工具读 diag.txt** —— 把中文打到控制台会被代码页弄成乱码，
  看上去像“名称对不上”，很容易误判成另一条检查。
  （另一个发现：`Tee-Object` 写出来的日志是 **UTF-16**，不是 UTF-8。）
  最后加一条只含 ASCII 的探针，把失败的检查名写进 diag：
  `$script:failures += $name; Diag("FAILED-CHECK " + $name)` —— 这才是
  “到底是哪一条挂了”的可靠来源。
* **根因**（日志里的证据是 `afterDrag=[SENTINEL] fg=…Google Chrome`）：
  弹窗是按**光标所在那块屏**居中的（`popup_host.cpp` 的
  `centreOnCursorScreen()`），而脚本里的 `$midX`/`$rowY`/`$barX`/`$barY` 是
  **在某个检查组开头一次性算好的绝对坐标**。本机是**两快屏**
  （2x 1920x1080@200%，物理 3840x2160），拖动结束时 `SetCursorPos` 的目标
  y 会超出屏幕（`$barBottom + 400*$scale`），光标被夹到屏幕边缘后，
  下一个 `OpenHelp` 就可能把弹窗开在**另一块屏**上：坐标全部对不上，
  那一下点击会落到另一块屏的浏览器窗口上（既没有复制，也真的点了一下用户的
  Chrome）。
* **修法**：`FocusCatcher`（每个依赖焦点的检查组都先调它）里先把光标归位到
  主屏的固定点 `[FlowInject]::Cursor(200, 200)`。这样每个弹窗都在同一块屏、
  同一个位置出现，那些一次性算好的坐标就都成立了。修完连跑两次 116/0。
* **教训**：注入鼠标的脚本里，**凡是用绝对坐标就必须先把光标放到一个确定的
  起点**（多显示器下尤其如此）；弹窗“跟着光标走”是对的 UX，错的是“坐标算一次
  就管到底”的测试写法。

#### 2026-09 新增：`window_rule`（窗口摆放规则）

* **`SetTimer(nullptr, id, …)` 会忽略 `id` 并返回一个新的定时器 id**，`WM_TIMER`
  的 `wParam` 就是那个新 id（本机实测 30661/30662 这种），不是传进去的 1/2。
  **这是 flowkeyd 一个一直没被发现的老 bug**：钩子线程原来写
  `SetTimer(nullptr, 1, tick_ms, …)` 再比较 `message.wParam == 1`，于是
  `Engine::tick()` **从来没被调用过** —— 长按重复（`repeatable = true` /
  `trigger = "repeat"`）实际上一直是坏的（单测直接调 `Engine::tick()`，
  所以没暴露）。现在把返回值存进 `m_tickTimerId` / `m_placementTimerId`
  再比较，`KillTimer` 也用返回值。
* **`MoveViewToDesktop` 在 24H2 上就是 vtable 下标 4**，三种布局的签名一致
  （不带 `HMONITOR`），所以 `moveWindowToDesktop` 可以直接调。但**已公开的**
  `IVirtualDesktopManager::MoveWindowToDesktop` 拒绝移动**别的进程**的窗口 ——
  必须用内部接口 + `IApplicationViewCollection::GetViewForHwnd`
  （IID/SID 都是 `{1841C6D7-4F9D-42C0-AF41-8747538F10E5}`，`GetViewForHwnd`
  在 vtable 下标 6）。
* **`MoveViewToDesktop` 返回 S_OK 不等于窗口真的换了桌面。** 验证要用
  **已公开**的 `IVirtualDesktopManager::GetWindowDesktopId`
  （`CLSID_VirtualDesktopManager` `{AA509086-…}`，IID `{A5CD92FF-…}`，
  `GetWindowDesktopId` 下标 4）：`tst_interactive` 就是比较移动前后的 GUID。
  `IsWindowOnCurrentVirtualDesktop`（下标 3）也能用，但 shell 是**异步**移动的，
  要轮询等它生效（第一次写测试时只等了固定时间，看起来像“移动失败”）。
* **按 `process` 匹配会一次命中一堆应用的内部窗口。** 实测记事本启动时会有
  `Non Client Input Sink Window`（工具窗口）与 `NotepadTextBox`（无标题）这类
  顶层窗口。所以“主窗口”的判据是：可见 + 无属主 + **非 `WS_EX_TOOLWINDOW`** +
  **有标题** + **尺寸非零**。`window::topLevelWindows()` 与钩子的
  `noteWindowEvent` 也套同一套过滤（标题只能在真正摆放时判：窗口刚 show 时
  标题可能还没写）。
* **`SetWindowPlacement` 是跨显示器摆放的关键。** `SetWindowPos` 对最大化的
  窗口无效，所以顺序是：`IsZoomed` 就先 `SW_RESTORE`，然后写
  `WINDOWPLACEMENT.rcNormalPosition`（屏幕坐标）并设 `showCmd`（最大化 /
  正常 / 保持最小化），最后对非最大化的情况再补一次 `SetWindowPos`
  （`SWP_NOACTIVATE`）。最小化的窗口只更新“还原位置”，不会被弹出来。
  实测 `SetWindowPlacement` 不会抢焦点；代码里仍然在检测到焦点被抢走时
  还回去。
* **显示器“重新接入”的判据是设备名从无到有**，不是拓扑任何变化
  （分辨率/排列变化不算）。轮询（350 ms）比较 `EnumDisplayMonitors` 的设备名
  集合；这比 `WM_DISPLAYCHANGE`（需要一个顶层窗口来收广播）简单得多，
  而且不需要额外的窗口类。
* **挂起期间不做摆放**（`m_suspended`），与“挂起之后所有绑定都不触发”一致。
* **验证手法**：`DisplaySwitch.exe /internal` 再 `/extend` 可以真的制造一次
  “显示器断开→重新接入”，日志里会看到 `monitor connected: DISPLAY2; re-applying
  window rules` 与随后的 `window rule ... -> ...: maximized ...`。
  手工把窗口挪到另一块屏再 `/extend`，就能看到它被摆回去。

#### 2026-09 新增：`window_rule` 的 `all_desktops` 与 `topmost`

* **`all_desktops` 不是 `IVirtualDesktopManagerInternal` 的方法。** 「在所有桌面
  显示」（Task View 里的那一条）在另一个未公开接口 **`IVirtualDesktopPinnedApps`**
  上：`QueryService` 的 SID 是 `{B5A399E7-1C87-46B8-88E9-FC5747B171BD}`、IID 是
  `{4CE81583-1E4C-4632-A621-07A53543148F}`，vtable 是
  `3 IsAppIdPinned / 4 PinAppID / 5 UnpinAppID / 6 IsViewPinned / 7 PinView /
  8 UnpinView`（按 AppUserModelID 钉整个应用的那三个用不到，但必须留占位）。
  **这个 IID 自 Windows 10 起就没变过**（VD.ahk、MScholtes/VirtualDesktop、
  windhawk 的 virtual-desktop-helper 都用同一个），所以它**不进版本表** ——
  随版本变的只有 `IVirtualDesktopManagerInternal` / `IVirtualDesktop`。
  本机（build 26200.9457，24H2 / `Layout::Plain`）实测 `PinView`/`UnpinView`
  真的生效（`tst_interactive::pinsAWindowToAllDesktops` 用 `IsViewPinned` 从外面
  确认状态变了；**`PinView` 返回 S_OK 本身证明不了什么**）。
* **`WS_EX_TOPMOST` 只能靠 `SetWindowPos` 设，不能靠 `SetWindowLongPtr`。**
  原来的 `toggle_topmost` 是「先 `SetWindowLongPtr(GWL_EXSTYLE, style |
  WS_EX_TOPMOST)` 再 `SetWindowPos(HWND_TOPMOST)`」。实测这样在“刚创建、shell
  还没登记的窗口”上会出现**返回 TRUE 却什么都没发生**（`GetWindowLong` 读出来
  仍然是 0）的状态。现在统一走 `window::setTopmost()`，只调
  `SetWindowPos(hwnd, HWND_TOPMOST/HWND_NOTOPMOST, …, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)`。
  （`toggle_topmost` 的判定仍然读 `GWL_EXSTYLE`，所以它也跟着变正确了。）
* **新建窗口后立刻置顶可能无效，先对它做一次窗口操作就好了。** 实测：紧跟在
  “把一个窗口摆到另一块显示器并最大化再还原”之后，**新建**一个窗口再
  `SetWindowPos(HWND_TOPMOST)`，40 次重试（~1 秒）都不生效，而且
  `IsWindowOnCurrentVirtualDesktop` 报「不属于任何虚拟桌面」（shell 还没登记它）。
  同一个窗口先做一次 `applyPlacement`（`SetWindowPlacement`/`SetWindowPos`）再置顶
  就正常。真实路径（`window_rule` 由 `EVENT_OBJECT_SHOW` 触发，而且写了几何时本来
  就先摆放）不会碰到这个，但**写交互式测试时要注意**：`tst_interactive` 的
  `topmostIsAppliedAndCleared` 因此先对自己的窗口做一遍几何摆放。

#### 2026-09 修复：被 `window_rule` 搬到别的桌面的窗口会被“收起”而不是唤醒

* **现象**（项目所有者报）：带 `window_rule` 的程序（`Win+3` → WPS）被唤起时
  “会出现在指定的位置，但没有自动 active（也可能是没切到指定的虚拟桌面）”。
* **根因（两条，必须一起治）**：
  1. **前台窗口的假象。** `MoveViewToDesktop` 把窗口搬到别的桌面之后，**shell
     仍然把它当作前台窗口**（本机 24H2 实测：`GetForegroundWindow()` 指向它、
     而公开的 `IsWindowOnCurrentVirtualDesktop` 返回 FALSE）。旧的
     `window::isActive()` 只看 `GetForegroundWindow()`，于是 `toggle` 判定
     “已经激活” → 把用户根本看不见的窗口*最小化*。日志里的证据：
     `` `wps` -> Minimize "...doc - WPS Office" (already active) ``。
  2. **`SetForegroundWindow` 在这时候是空操作。** 窗口已经是 shell 的前台窗口时，
     `SetForegroundWindow` / `AttachThreadInput` / `BringWindowToTop` +
     `SetWindowPos` 四条路**全部返回 TRUE 却什么都不做**（本机实测，
     `onCurrentDesktop` 始终是 0）—— 所以光修 `isActive` 还不够，必须**显式
     `SwitchDesktop`**。
* **修法**：`window::isActive()`（以及 `raiseWindow()` 的提前返回）把“就在当前
  虚拟桌面上”一起算进去；`raiseWindow()` 发现窗口在别的桌面时先调
  `desktop::switchToWindowDesktop()` 把视图切过去，再 `SetForegroundWindow`。
  因此 `window::isActive()` 现在会做一次 COM 查询（`GetForegroundWindow() !=
  hwnd` 时短路，不会白跑）。
* **窗口→桌面对象的映射**：内部枚举只给 `IVirtualDesktop*`，而窗口那边只有**已
  公开**的 `GetWindowDesktopId`（GUID）。把两者对上号要用未公开的
  `IVirtualDesktop::GetID`（vtable 下标 4，照抄 VD.ahk 的 `VD_goToDesktopOfWindow`）。
  **这个下标自带着自检**：拿每个枚举到的桌面问 GUID，与公开 API 给出的逐个比对，
  **有且只有一个对上**才算成功；对不上（布局与版本表不符）时只报一条错误日志、
  不去切一张可能是错的桌面。实测（build 26200.9457）：4 个桌面的 GUID 互不相同，
  窗口所在那个能准确命中。
* **“搬迁”与“视图跟随”**：`MoveViewToDesktop` 是**异步生效**的，所以
  `moveWindowToDesktop` 的 `changed` 出参要等一下（最多 10×25 ms 读公开的
  `GetWindowDesktopId`，拿不到就报 `false`）。`window_rule` 只在这一份布尔量为真
  （**真的**换了桌面）且是“窗口第一次出现”那一遍时才 `switchTo` + `raiseWindow`。
* **别指望“搬走前台窗口”会把视图带着走。** 实测两种结果都出现过：窗口在前台时
  搬走*有时候*视图跟着走，不在前台时视图留在原处。所以不能把“跟随”建立在
  Windows 自己的行为上。
* **跨桌面唤醒的另一个坑（写测试时踩到的）**：如果目标窗口恰好是 shell 的
  “前台窗口”但在另一张桌面上，`SetForegroundWindow` 是空操作（见上）；
  反过来，如果它是*真*前台窗口，把它搬走有时会把视图也带走。所以交互式测试里
  要先 `moveWindowToDesktop` 再显式 `switchTo(原桌面)` 把视图按住，
  才能构造出用户报的那个现场。
* **实验手法（值得收藏）**：用**已公开**的 `IVirtualDesktopManager`
  （`IsWindowOnCurrentVirtualDesktop` / `GetWindowDesktopId`）从 PowerShell 的
  Add-Type 里直接观察，就能把“切了桌面”和“把窗口搬走了”区分开——
  靠**两个**已知在同一张桌面上的参照窗口（只搬走一个时另一个仍 `onCurrent=1`
  就说明是“搬窗口”，两个都 `onCurrent=1` 就是“切了视图”）。
  未公开的那半边（`QueryService` / `SwitchDesktop` / `GetID`）用
  `Marshal.GetDelegateForFunctionPointer` 手搝 vtable 调用即可，不必注册 COM。
  脚本在 `tmp/desk/`（不进版本库）。

#### 2026-09 实测：本机的记事本变成了带标签页的 Store 应用（交互式测试的载体得换）

* **现象**：`tst_interactive::windowBackendLaunchesActivatesAndCloses` 在
  “`Close` 之后窗口消失”这条上挂（`gone` 一直是 false），偶尔还会在“激活之后
  `isActive`”那条上挂。
* **根因**：本机的记事本是 **`Microsoft.WindowsNotepad` 11.2607（Store 应用）**，
  单实例 + 标签页 + **会话恢复**：
  * `notepad.exe`（System32 里那个 360 KB 的壳）启动的是它，给文件参数只是
    **给一个既有窗口加标签页**；会话恢复甚至会让窗口标题停在别的文件上——
    实测 `find` 拿到的窗口标题是上一次运行留下的 `flowkeyd-mon-<pid>.txt`；
  * `WM_CLOSE` 会因为别的标签页 / 恢复的会话弹确认框，窗口因此不会消失；
  * 前台在多个窗口/标签页之间切换时，`GetForegroundWindow()` 可能指向同进程的
    另一个窗口，于是 `isActive` 那条断言也会偶发失败。
* **不是本仓库的回归**：把修复前的提交（`ee38ac6`）用 `git worktree` 单独构建
  跑同一条用例，一样在 `gone` 上挂（同一个断言）。另一方面，`Stop-Process -Force`
  掉记事本会在它自己的 `LocalState\TabState\` 里留下垃圾标签页，越跑越脏。
* **做法**：`tst_interactive` 里凡是“窗口后端”的断言，改用**测试进程自己的顶层
  窗口**（`tests/tst_interactive.cpp` 里的 `TestWindow`：自己 `RegisterClassExW`
  + `CreateWindowExW`，标题可控、进程独占、`WM_CLOSE` 就是 `DestroyWindow`；
  等消息用只抽自己窗口消息的 `pump()`，不碰 Qt 的事件循环）。记事本只剩两个用途：
  1. 覆盖“启动一个真程序 + 按标题找到它的窗口”（`runCommand` + `find`）；
  2. `copySelectionCopiesTheFocusedSelection` 需要一个真能 Ctrl+C 的编辑器。
  两者都不再断言“窗口能被关掉”。
* **顺带发现的一条产品事实**：**刚创建、还没被 shell 登记的窗口**
  `GetWindowDesktopId` 会给全零 GUID，而 `IsWindowOnCurrentVirtualDesktop` 也报
  FALSE。所以 `desktop::isWindowOnCurrentDesktop` 现在把“不属于任何虚拟桌面”当成
  `std::nullopt`（不知道），而不是“在别的桌面上”——否则 `window::isActive` 会把
  一个就在眼前、刚创建的好窗口判成“没在眼前”（`toggle` 就永远收不起它）。

### 领域坑清单（动手前先看这一遍）

下面这些每一条都值得在动钩子/引擎/窗口/电源之前先读一遍：

* 通用修饰键 `VK_SHIFT` vs 分侧 `VK_LSHIFT`/`VK_RSHIFT`（`same_key`）。
* 小键盘 Enter 与主键盘 Enter 共用 `VK_RETURN`，只能靠扩展键标志区分；
  **`SendInput` 不带 `KEYEVENTF_EXTENDEDKEY` 就造不出小键盘的 Enter**。
* `NumLock` 不影响钩子看到的 `VK`（只影响字符翻译）。
* 被吞掉的 `Win+…` 会让外壳打开开始菜单/搜索；遮断标记必须挂在
  **修饰键 key-up** 上（按下时注入会被 Windows 键的自动重复重新武装）。
* `send`/`type` 的 `release_modifiers` 也要在注入层做菜单遮断。
* `!`/`^`/`+`/`#` 是 AutoHotkey 前缀，`{!}` 才是字面量。
* `keys::split_hold` 不能反转尾部（`^{c}` 展开后的释放顺序）。
* `exact_modifiers = true` 与“修饰键作为和弦按键”的冲突。
* **`SetForegroundWindow` 除非持有前台锁否则被拒**（递进式绕行 +
  `AttachThreadInput` 配平）。**而且它在“目标已经是 shell 的前台窗口，只是不在
  当前虚拟桌面上”时是空操作**（返回 TRUE 却什么都不发生）：被
  `MoveViewToDesktop` 搬走的窗口就是这个状态，只能先显式 `SwitchDesktop`
  （见第 2 节第 14 条与第 10 节）。
* 有属主的窗口（对话框/工具提示/弹出菜单）永远不是用户想要的那个窗口。
* 终端窗口不能靠标题找（`process = "wezterm"`，窗口属于 `wezterm-gui.exe`）。
* `run`/`window.launch` 走 `CreateProcess`、**不查 `App Paths`**、
  也不能用 `DETACHED_PROCESS`（会静默杀死控制台子进程，用 `CREATE_NO_WINDOW`）。
* `DWMWA_TRANSITIONS_FORCEDISABLED` **读不回来**，只能“设 TRUE →
  `ShowWindow` → 设回 FALSE”；DWM 在过渡**开始**时读它。
* 虚拟桌面接口表里 `build.revision` 是“起始版本”不是“上限”。
* “另一个桌面上的窗口”是被 DWM cloak 掉的（判断桌面真切换了的外部依据）。
* 未公开 API 的用法：`GetProcAddress` + **使用前一次无害调用**校验 +
  永远保留已公开的回退。
* 用户点“否”时 `ShellExecuteW("runas")` 返回 **5**（拒绝访问）而不是 1223；
  `lpParameters` 是一个字符串，带空格的路径要自己按 `CommandLineToArgvW`
  的规则加引号。
* 提权后的进程会把它启动的子进程一起提权（写进 README 的已知限制）。
* 中文错误文案不可断言（`FormatMessageW` 是本地化的）——
  这也是我们**日志与校验信息保持英文**的实际原因。
* **`SetTimer(nullptr, id, …)` 的 `id` 会被忽略**，`WM_TIMER.wParam` 是返回值，
  不是传进去的 id（见第 10 节：这里踩出了一个老 bug）。
* **`IVirtualDesktopManagerInternal::MoveViewToDesktop` 在 vtable 下标 4**
  （三种布局一致），需要先用 `IApplicationViewCollection::GetViewForHwnd`
  把 `HWND` 换成 `IApplicationView*`；公开的 `MoveWindowToDesktop` 动不了
  别的进程的窗口。
* **`GetForegroundWindow()` 不等于“用户看得见的窗口”**：窗口被搬到别的虚拟桌面
  之后 shell 还把它当前台窗口，所以“是不是已经激活”必须再问一句公开的
  `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop`。
* **`IVirtualDesktop::GetID` 在 vtable 下标 4**（先 `IsViewVisible`（3），
  照抄 VD.ahk 的 `VD_goToDesktopOfWindow`），用来把窗口的
  `GetWindowDesktopId` 对到内部枚举的桌面上；拿**有且只有一个匹配**当自检。

#### 2026-09 新增：托盘右键与启动日志里的构建版本（build 时间戳）

* **需求**：托盘右键能看到构建版本号，启动日志里也要有一行。项目所有者 2026-09
  先定成「构建的时间戳」，随后改成 **`yy-MM-dd-<git 短修订>`**（例如
  `26-09-22-42900ad`）—— 日期告诉你这是哪天的构建，哈希告诉你源码是哪个提交。
* **落地**：新文件 `src/core/version.{h,cpp}`（纯逻辑、只用 QtCore，编在
  `flowkeyd_core` 里）提供 `buildVersion(executablePath)`（拼成 `yy-MM-dd-<rev>`）、
  `buildDateFromFile(path)`、`sourceRevision()` 与 `unknownValue()`；
  `app::helpText()/versionText()`、`Tray::setBuildVersion()` 与启动日志都只消费
  `buildVersion()` 那一个字符串。`tst_version` 用 `QFile::setFileTime` 钉一个已知
  时间再读回来（期望值 `21-03-04`），并用正则盯住「日期段 `yy-MM-dd`、修订段是小写
  十六进制」——不用碰真实 exe。
  * **格式改过两次**：先是 `0.1.0 (build 2026-09-22 17:18:05)`，再是
    `yyyyMMddHHmm`（纯时间戳），最后按项目所有者要求定成
    **`yy-MM-dd-<git 短修订>`**。前两版为此引入的 `FLOWKEYD_VERSION` /
    `projectVersion()` / `formatBuildVersion()` 都已经删掉，要再改格式只动
    `core/version.*`。
* **日期取“正在运行的这个 exe 的最后写入时间”**（本地时间 `yy-MM-dd`），也就是它
  被链接到磁盘的那一刻；因此每次重新构建它都会跟着变，而且**不需要任何构建脚本**。
* **git 修订是编译期常量，但只在提交 / 切分支时才变**：CMake 在配置时跑
  `git rev-parse --short HEAD`，经 `cmake/version_revision.h.in` 写进
  `build/<preset>/generated/flowkeyd_revision.h`（**只给 `core/version.cpp` 包含**，
  所以它变了只重编那一个文件）。为了让「提交之后再构建」能自动刷新它，CMake 把
  `.git/HEAD` 与**当前分支的 ref 文件**登记成了 `CMAKE_CONFIGURE_DEPENDS` ——
  只登记 HEAD 不够（同一分支上提交时 HEAD 文件本身不变），登记 `.git/index` 又太
  敏感（`git status` 刷新索引也会触发重新配置）。`.git` 是文件（worktree /
  submodule）时整段跳过，哈希留在 `unknown`。
  * **因此工作流是「先提交，再构建」**：构建时 HEAD 是哪个提交，版本号里就是哪个
    （工作区脏时显示的是最近一次提交）。收尾给常驻重建 release 时也是这个顺序。
  * **提交之后的那次构建一定会重新配置 + 重新链接**（哈希变了，`version.cpp`
    要重编、exe 要重链）—— 而 release 的 exe 正被常驻实例锁着，所以顺序必须是
    「改代码 → 提交 → `--quit` 常驻 → 构建 release → 重新拉起常驻」。纯文档的提交
    也会改分支 ref、触发重新配置；不想动二进制就别在提交之后再构建。
* **为什么不用编译期常量**（CMake 构建时生成一个 `FLOWKEYD_BUILD_TIMESTAMP`）：
  那样每次 `cmake --build` 都要重新生成头文件 → 重新编译 → 重新链接 exe +
  跑两遍 `windeployqt`，于是「什么都没改」的构建不再是 `ninja: no work to do`
  （第 5 / 11 节都拿它当纯文档任务的判据）；更糟的是常驻实例正跑着
  `build\dist-release\flowkeyd.exe` 时那个文件是**锁住**的，连一次「看看是不是
  最新」的构建都会失败。读文件时间则只在 exe 真的被重新链接时才变。
* **试过但不可行：PE 头的 `TimeDateStamp`。** 那是“编译进镜像里”的链接时间，
  看起来最优雅，但实测本机 MinGW 的 `ld` 写出来的是一个固定的小数值
  （`windows-release` 与 `windows-debug` 两份都是 `1410660`，换算成时间是
  1970-01-17），不是链接时间 —— 所以只能读文件时间。（诊断脚本
  `tmp/peinfo.ps1`，`tmp/` 不进版本库。）
* **显示位置**：`src/platform/win/tray.cpp` 在「退出」上面加了一个
  **`setEnabled(false)` 的信息项**（`版本 26-09-22-42900ad`），悬停提示也带上构建
  版本（后面再跟挂起状态）；`main.cpp` 一拿到命令行就把 `core::buildVersion(
  win::currentExecutablePath())` 算好，一路传给 `--help` / `--version` 的文案、
  日志第一行与 `Tray::setBuildVersion()`。`--version` 的第一行就是它
  （`flowkeyd 26-09-22-42900ad` + `Lua 5.5.1`），`--help` 表头也是
  （`flowkeyd 26-09-22-42900ad — 由 Lua 配置驱动的……`）。
* **怎么验证托盘那一项**：托盘菜单点击进不了自动化（见第 5 节），但菜单文本可以
  在进程内断言 —— 一个临时小程序构造真实的 `Tray`，用
  `tray.findChild<QSystemTrayIcon*>()->contextMenu()->actions()` 把每一项的
  `text()`/`isEnabled()` 与 `icon->toolTip()` dump 到 UTF-8 文件里（**不要打到
  控制台**，中文会被代码页弄乱）。当时看到的：

  ```
  buildVersion=26-09-22-42900ad
  tooltip=flowkeyd 26-09-22-42900ad
  enabled=1 text=查看日志(&V)
  enabled=1 text=挂起快捷键(&S)
  enabled=1 text=重载配置(&L)
  enabled=1 text=打开配置文件(&O)
  enabled=0 text=版本 26-09-22-42900ad
  enabled=1 text=退出(&Q)
  tooltip=flowkeyd 26-09-22-42900ad（已挂起）
  ```
* **改“停常驻 → 重建 → 重新拉起”的脚本一定要 fail fast，而且检查要轮询。**
  本次写第一个版本时脚本中段因为一个行接续错误只报了一条 parse error 就
  **继续往下跑**（没设 `$ErrorActionPreference = 'Stop'`），于是：`--quit` 没发
  出去、release 构建撞在被锁住的 exe 上失败、而后面那条 `Start-Process` 又在常驻
  还活着时起了第二个实例 —— 它弹出一个原生的「flowkeyd 已在运行」模态框坐在屏幕上
  等人点，需要 `taskkill /PID <pid>`（**不带** `/F`）把它收掉。现在的脚本在 quit
  之后**轮询** `Get-Process flowkeyd`（最多 15 秒）直到它真的消失，还有残留才
  abort，不构建也不重启 —— **只查一次会误判**：`Start-Process -Wait` 返回时那个
  进程对象可能还没从进程表里消失。
* **`--quit` 成功与否别只看退出码**（`Start-Process -PassThru` 的 `ExitCode` 本来
  就是空的，见本节的另一条），也别只看那一瞬间的进程表：常驻已经开始关闭时
  `Get-Process` 仍可能看得到它。可靠的判据是 stdio 里那句
  `flowkeyd: <path> exited`（退出码 0）加稍后为空。

#### 2026-09 新增：应用图标（`logo.svg` → exe 资源 + 托盘）

* **需求**（项目所有者）：把仓库根目录的 `logo.svg` 用到两处：**exe 的图标**
  （资源管理器 / 任务栏 / 快捷方式看到的）与**系统托盘里的图标**。
  此前托盘用的是 `QStyle::SP_ComputerIcon` 那个系统图标。
* **两处用的是两份不同的东西**，别混：
  1. **exe 图标**只认 Windows 资源——`.ico` 经 `windres` 嵌进 PE（`IMAGERESOURCE`）。
     Qt 那套 `QIcon`/qrc 对资源管理器**毫无影响**（双击 exe 看到的还是默认图标）。
     实现在 `assets/flowkeyd.ico` + `CMakeLists.txt` 里配置时生成的 `flowkeyd.rc`
     （模板 `assets/flowkeyd.rc.in`）：`enable_language(RC)` 后把生成的 .rc 当作
     `qt_add_executable` 的一个源文件，windres 就会把它编进去并链接。
  2. **托盘图标**是 `QSystemTrayIcon` 拿的 `QIcon`，来自 exe **自己的 qrc**
     （`qt_add_resources(flowkeyd "app_icons" …)` + `src/app/app_icon.cpp` 把 9 张
     PNG 拼成一个多尺寸 `QIcon`）。同一个 `QIcon` 也给了 `QApplication::setWindowIcon()`，
     于是日志/选单/帮助窗口与配置错误的消息框都跟着它。
* **`.rc` 里写的是绝对路径**：windres 解析 `ICON "..."` 的相对路径既不是相对 .rc
  文件、也不保证相对源码目录（取决于生成器给的工作目录），相对路径会在某些构建
  目录下突然找不到图标。所以 CMake 用 `configure_file` 把 `.ico` 的绝对路径填进
  模板，生成到 `build/<preset>/generated/flowkeyd.rc`。
  * `.gitignore` 里有 `*.rc`（Qt Creator 的模板就会生成同名文件），所以**模板叫
    `assets/flowkeyd.rc.in`**，而且只有生成的 .rc 才是被忽略的那个。
  * `.rc` 刻意保持**纯 ASCII**（连注释也英文）：windres 的预处理器对非 ASCII 注释
    没有明确保证（与“.cmd/.bat 必须纯 ASCII”同源），中文说明放在 CMakeLists.txt。
* **运行时用 PNG 而不是 SVG**（本项目没有链 `Qt6::Svg`）：PNG 是 QtGui 内建的格式，
  而 SVG 要走 `imageformats/qsvg` 插件（它又需要 `Qt6Svg.dll`）。那个插件确实是
  windeployqt 自己拷的（本机 `build/windows-release/imageformats/` 里就有），但那是
  靠扫依赖“猜”出来的：少一个插件，图标就**静默**变成空白，而且在构建目录里看不出来。
  用 PNG 就没有这条路。
* **多尺寸帧是必要的**：只放一张 256 的图，托盘在 200% 缩放下要靠 Qt 缩图，边缘会糊。
  所以 `tools/icon_gen` 一次生成 16/20/24/32/40/48/64/128/256 九张，
  `QIcon::addFile` 逐张加进去（Qt 自己按需要的尺寸挑）。
* **`logo.svg` 不在构建时被读**：构建时光栅化会把 `Qt6::Svg` 与 windeployqt 的
  插件依赖变成必需项，而且会让每次构建都依赖那个工具。改成“一次性生成 + 产物提交”：
  `tools/icon_gen` 是 `EXCLUDE_FROM_ALL` 的 `flowkeyd_icon_gen`，配一个
  `icons` 目标：

  ```powershell
  cmake --build --preset debug --target icons
  ```

  * 目标里用 `cmake -E env "PATH=<Qt bin>;<MinGW bin>;..."` 跑那个工具：它自己
    **不是**部署过的目标（只有 `flowkeyd` 挂 windeployqt），所以需要 Qt 的 DLL
    在 PATH 上。
  * `find_package(Qt6 QUIET COMPONENTS Svg)` 是单独一次查找：没装 QtSvg 时只是少
    了这个手工工具（并且不生成 `icons` 目标），两个 profile 的正常构建不受影响。
  * 它自己的 `AUTOMOC/AUTOUIC` 关掉了（纯 C++，没有 `Q_OBJECT`），免得白编一个空
    的 `mocs_compilation.cpp`（与 `lua_static` 那条同理）。
* **ICO 格式的两个坑**：
  * 帧可以是 DIB（BITMAPINFOHEADER + XOR 位图 + AND 掩码）或 PNG。本工具小尺寸
    （≤64）用 DIB、大尺寸（128/256）用 PNG：纯 DIB 的话 256 光栅要 256 KB 以上。
  * DIB 帧里 `biHeight` 要写 **2×高度**（XOR + AND 两块），AND 掩码即使 32 位色
    也必须写（按 4 字节对齐、全 0），而且两块都是**自下而上**。写错了不会报错，
    只是图标在资源管理器里花掉。
* **怎么确认 exe 里真的嵌进去了**（不用启动程序）：
  `[System.Drawing.Icon]::ExtractAssociatedIcon(exe)` 能拿到图标就说明 PE 里有；
  再把它 `ToBitmap().Save(png)` 出来看一眼，就能确认不是空壳。
  验证 qrc 那一半靠 `flowkeyd --list` 不行（托盘图标只有真跑起来才画），
  本次是构建 + 单元测试全绿，托盘效果由项目所有者自己看一眼。

---

## 11. 完成定义（DoD）细则

一个任务算完成，必须同时满足：

1. `cmake --build --preset debug` 与 `cmake --build --preset release` **都绿**
   （零新增警告；warning 当错误处理，直到项目所有者另有要求）。
   **两条 profile 都要构建，但单元测试只在 debug 上跑**（见下一条）。
2. `ctest --test-dir build/windows-debug --output-on-failure` **全绿**
   （测试目标只在 debug profile 里构建；release 是发布 profile，见工作约定第 2 条）；
   新增/修改的逻辑都有对应测试（`core/`、`lua/`、模型层这些可测的部分）。
3. 如果动了钩子/引擎/分发/窗口后端：跑 **`scripts\acceptance.ps1`**
   （只跑 `-Exe build\windows-release\flowkeyd.exe` 那一份，见第 5 节），
   把结论写进本文件。
   动到脚本覆盖不到的界面（托盘菜单、日志窗口、`animate`）时，仍然要人眼过一遍。
4. **测试绝不执行真实的系统电源动作**：`shutdown`/`restart`/`logoff`/
   `sleep`/`hibernate`/`lock`/`screen_off` 在任何测试里都不许真的跑；
   `platform::win::power::execute()` 不出现测试代码里（`tst_power_table`
   只测纯逻辑）。要确认这些动作能不能用，只能由用户自己按键试一次。
5. `flowkeyd --check --config flowkeyd.lua.example` 通过
   （阶段 2 之后，只要示例配置存在就要能过）。
6. 用户可见行为有变化时更新 `README.md`，有新经验时更新本文件。
7. `git commit`：提交信息里说明**为什么**（尤其是引入新依赖时）。
8. 仓库里不留垃圾：`tmp/`、`build/`、临时配置文件都在 `.gitignore` 里。
   **发布目录 `build/dist-release/` 里只允许有 `flowkeyd.exe` 与它需要的
   运行时**：测试可执行文件、临时配置、诊断文件一律不许留在
   `build/windows-release` 或 `build/dist-release` 里（见工作约定第 2 条）。
9. **保证 `build/dist-release/` 是最新发布包**（工作约定第 11 条）：release 构建的
   `POST_BUILD` 已经自动把干净的发布产物写到那里（只有 `flowkeyd.exe` 与它需要的
   Qt/MinGW 运行时），**不需要再往任何安装目录部署、也没有部署脚本**。
   常驻实例若正在从 `build/dist-release` 跑（会锁住那个 exe），按工作约定第 11 条
   先 `--quit` → 构建 → 重新拉起；`ninja: no work to do` 的纯文档任务什么都不用做。
   这是「用户日常按的快捷键真的跑在新构建上」的唯一保证
   （前 8 条只保证构建与测试是绿的）。

> **阶段 0/1 的实测结果（2026-09-20）**：`windows-debug` 与 `windows-release`
> 两个 profile 都是 `build exit 0`、零警告（`-Wall -Wextra -Werror`），
> 6 个测试目标在两边都是 `100% tests passed`。
> 跑 `ctest` 时请用第 5 节的命令行（现在是 `ctest --test-dir build/windows-debug`；
> 当年两个 profile 都跑，后来改成只跑 release，2026-09 又改成只跑 debug，
> 见工作约定第 2 条）；
> 每个测试实际是 `cmake/RunQTest.cmake` 包的一层，它会把 QtTest 的输出
> `cat` 出来（原因见第 10 节）。
> `--check` / `--list` 在阶段 2 之后就绪（现在打的是真实结果）。

> **阶段 2/3 的实测结果（2026-09）**：`windows-debug` 与 `windows-release`
> 两边都是 `build exit 0`、零警告，`ctest` **11 个测试目标**全绿
> （`tst_lua` 25 个用例、`tst_layout|input|command_line|instance` 共 18 个用例）；
> `flowkeyd --check --config flowkeyd.lua.example` 通过（37 hotkey / 3 remap、零警告）。
> 常驻冒烟：一次性配置 + `--no-elevate` 启停正常，第二个同配置实例被拒，
> `taskkill /PID`（不带 `/F`）后日志里有 `keyboard hook removed`。
> 注入后端在本机实测选中 `win32u!NtUserSendInput`（已通过零输入调用校验）。

> **阶段 4/5 的实测结果（2026-09）**：`windows-debug` 与 `windows-release`
> 两边都是 `build exit 0`、零警告，`ctest` **15 个测试目标**全绿（含
> `tst_log_tail` 9、`tst_window_match` 7、`tst_audio` 5；`tst_interactive` 默认
> skip）。`flowkeyd --check --config flowkeyd.lua.example` 仍通过（37/3、零警告）。
> 交互式验证（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）额外 5 个用例全绿：
> 剪贴板往返、音量读写/钳位/恢复、记事本窗口的 启动→激活→最小化→恢复→关闭、
> 以及 `copySelection`。日志窗口用 `PrintWindow` 截图验证了渲染与尾随。
> 需要真实按键的那部分（吞键/自动重复/重映射/`toggle`/`animate`/托盘点击）
> 留到阶段 9，原因见阶段 4/5 的「手工验证结论」。

> **阶段 6 的实测结果（2026-09）**：`windows-debug` 与 `windows-release`
> 两边都是 `build exit 0`、零警告，`ctest` **17 个测试目标**全绿
> （新增 `tst_menu_model` 13 个用例、`tst_help_model` 17 个用例）。
> `flowkeyd --check --config flowkeyd.lua.example` 仍通过（37/3、零警告）；
> 一次性配置 `tmp/smoke6.lua`（一个 `menu` + 一个 `help` + 一个 `none`）的
> `--check`/`--list` 形状符合预期（`menu "冒烟选单" (3 item(s))`、`help`），
> 守护进程用 `--no-elevate --allow-multi` 启停正常（日志里有
> `keyboard hook removed`）。两个弹窗的渲染、筛选变矮、键盘选择、
> 回调回投与窗口复用由 `tmp/preview/` 的临时预览程序截图/日志验证，
> 见阶段 6 的「手工验证结论」；**真实快捷键触发那一步留到阶段 9**。
> 另外顺手给 `LogWindow.qml` 补了 `pragma ComponentBehavior: Bound`，
> 现在 `qmllint` 对三个 QML 文件都是零警告。

> **阶段 7/8 的实测结果（2026-09）**：`windows-debug` 与 `windows-release`
> 两边都是 `build exit 0`、零警告，`ctest` **19 个测试目标**全绿
> （新增 `tst_desktop_table` 5 个用例、`tst_power_table` 7 个用例）。
> `flowkeyd --check --config flowkeyd.lua.example` 仍通过（37/3、零警告）；
> `--version` 打印 `flowkeyd 0.1.0` + `Lua 5.5.1`。
> 交互式验证（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`，另加
> `FLOWKEYD_ALLOW_SCREEN_OFF=1`）8 个用例全绿（其中的
> `powerScreenOffBlanksTheDisplay` 后来已删除：电源动作不再进测试）：虚拟桌面探测到
> `count 4 / current 2 / os 26200.9457 / api 26100 / layout plain /
> manager {53f5ca0b-158f-4124-900c-057158060b27}`，切走再切回成功；
> `screen_off` 真的黑屏并被随后注入的 Shift 点亮。
> `README.md` 与 `flowkeyd.lua.example` 已逐节核对一致。
> **仍需人的手**：`animate = true/false` 的肉眼区别、托盘菜单里的
> 电源条目与点击，以及“托盘图标真的消失了”的直接观察（阶段 3/4/5/6 那批的
> 其余部分已由 `scripts/acceptance.ps1` 覆盖，见阶段 9）。

> **阶段 9/10 的实测结果（2026-09）**：`windows-debug` 与 `windows-release`
> 两边仍然是 `build exit 0`、零警告，`ctest` **19 个测试目标**全绿。
> `flowkeyd --check --config flowkeyd.lua.example` 仍通过（37/3、零警告）。
> 新增 `scripts/acceptance.ps1`（68 项检查）：debug 跑了三遍、release 跑了两遍，
> 结果都是 `checks: 68, failures: 0`（中间的两次失败是测试环境造成的，已记在
> 第 10 节）。接管部分：用户真实配置已迁到 `.config\flowkeyd\config.lua`
> （`--check` → `OK (24 hotkey(s), 0 remap(s))`、零警告），非提权预演能装钩子
> 也能被 `taskkill /PID`（不带 `/F`）干净停掉；
> 提权常驻由用户自己一条 `Start-Process -Verb RunAs` 启动（README 里有）。

> **2026-09 修订（DoD 收窄）**：从这里往后，`ctest` 与 `acceptance.ps1` 都只跑
> `build/windows-release`（debug 仍然必须构建，只是不再跑测试）；任何测试都不
> 再执行真实的电源动作。上面各阶段的“两边都……”只是当年的记录，不必照抄。

> **2026-09 修实（弹窗在滚轮下闪烁）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零警告；`ctest --test-dir
> build/windows-release` **19 个测试目标全绿**（含新的
> `tst_help_model::wheelKeepsTheHoverButKeyboardDropsIt`）；
> `acceptance.ps1`（只跑 release）**77 项、0 失败**（`checks: 77, failures: 0`）。
> 其中一次跑的“焦点正对照”两条挂了——那是第 10 节里写过的测试环境问题
> （用户刚点过托盘的常驻实例，前台锁不在我们手上），重跑即绿；
> 闪烁本身是用 `tmp/` 下的高速截屏工具对比出来的（修前每一格滚轮两帧、
> 修后一帧），手法记在第 10 节。
> **注意**：为了链接 release 的 exe，请用户关掉了提权常驻实例；
> 构建/验证完之后要用 `Start-Process -Verb RunAs` 重新拉起它（见阶段 10）。

> **2026-09 重写（帮助窗口改用 Qt 自带的列表）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零警告；`ctest --test-dir
> build/windows-release` **19 个测试目标全绿**（`tst_help_model` 里新增的
> `scrollTargetKeepsTheRowInsideTheRowArea`）；
> `flowkeyd --check --config flowkeyd.lua.example` → `OK (37 hotkey(s), 3 remap(s))`、
> 零警告，用户真实配置（不带 `--config`）→ `OK (24 hotkey(s), 0 remap(s))`、零警告。
> 弹窗那套（列表位置、拖动滑块、滚轮、滚轮在表头上、悬停跟随滚动）由
> `tmp/preview` **在进程内**验证（`QWindowSystemInterface` + `QTest` 注入，
> 不走操作系统）：
> 静止 `contentY = -88`；拖滑块 `position 0 → 0.341`（`contentY 0 → 447.9`）；
> 列表里滚轮往下滚 `contentY` 变大且悬停行从 11 跟到 13；光标在表头上滚轮照样
> 能滚；光标离开行区域时悬停回到 `-1`。
> **`acceptance.ps1` 这一次没跑成**：写这份记录时桌面是锁的（`LogonUI` 在跑，
> `GetForegroundWindow()` 返回 0、`SendInput` 报 5），脚本会在“捕捉窗口拿到了
> 键盘焦点（正对照）”那一条挂掉。解锁后请自己跑一遍：
> `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1`
> （预期 **82 项**：原 77 + 拖动滑块 2 项 + 重新打开复位 2 项 + 滚轮前置 1 项；
> 帮助那一段的滚轮注入已改成 `Wheel(-120)`）。
> → **后来又有一次改动（2026-09，取消悬停高亮）**：脚本现在是 **84 项**，帮助那
> 一段改用「左键点一行 = 复制它」来观察滚动，见第 10 节与本节最后一条记录。

> **2026-09 文档整理（本文件自包含）**：本文件与 `README.md` 是 flowkeyd 唯一的
> 工程笔记与用户文档，不再引用任何外部项目的文档；配置 schema、动作字段与键名的
> 权威定义在 `README.md`。本次只改了本文件，所以 debug/release 两个 profile 都是
> `ninja: no work to do`（`build exit 0`），`ctest --test-dir build/windows-release`
> 19 个测试目标全绿，`--check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s))`、零警告。

> **2026-09 修复（取消帮助窗口的悬停高亮）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零警告；
> `ctest --test-dir build/windows-release` **19 个测试目标全绿**（`tst_help_model`
> 删掉了两个悬停用例，`rolesExposeWhatTheDelegateNeeds` 改成盯“高亮只跟键盘
> 选中项”）；`qmllint -I C:\Qt\6.11.2\mingw_64\qml src\qml\HelpPopup.qml`
> 零警告；`flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s))`、零警告。
> `scripts/acceptance.ps1`（只跑 release）**84 项、0 失败**
> （`checks: 84, failures: 0`），新增「左键点某一行会复制它的按键」、
> 「拖动滚动条不会改键盘选中项（Enter 复制的还是第 1 行）」与「滚轮不会改
> 键盘选中项」三条；拖动/滚轮之后用“同一屏幕位置已经换了一行”证明列表真的滚了。
> 行为变化：鼠标悬停不再改变高亮，`Enter` 只复制键盘选中项那一行，
> 拖动滚动条只滚视图（README 的「快捷键帮助」已同步）。
> 这一类任务只碰模型/QML/文档，但按工作约定第 2 条仍然两个 profile 都构建、
> 只在 release 上跑测试与验收脚本。

> **2026-09 重做（帮助窗口换成标准控件）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零警告；
> `ctest --test-dir build/windows-release` **19 个测试目标全绿**
> （`tst_help_model` 19 项：删掉退格/`Home`/`End`/`scrollTargetY` 的用例，
> 新增 `mouseClickSelectsTheRow`、`editingKeysAreLeftToTheTextField`）；
> `qmllint -I C:\Qt\6.11.2\mingw_64\qml src\qml\HelpPopup.qml` 零警告；
> `flowkeyd --check --config flowkeyd.lua.example` → `OK (37 hotkey(s), 3 remap(s))`、
> 零警告，用户真实配置（不带 `--config`）→ `OK (24 hotkey(s), 0 remap(s))`。
> `scripts/acceptance.ps1`（只跑 release）**86 项、0 失败**
> （`checks: 86, failures: 0`）：新增「点选之后 `Enter` 复制的就是刚点中的那一
> 行」（拖动滚动条之后、滚轮之后各一条）与「用鼠标点一下筛选框再输入就会筛选」。
> 弹窗本身用 `tmp/preview`（真实的 `PopupHost` + 真实 QML，进程内注入鼠标与
> 键盘）验证：筛选框一打开就有焦点、**点一下筛选框就拿到焦点**、**点一行会选中
> 并复制**、点完焦点仍在输入框里、打字就筛选、模型改筛选输入框也跟着变、
> `↓`/`Enter`/两级 `Esc` 正常、选到最后一行会滚进视野（`contentY=864`）；
> 截图 `tmp/help-new.png`（标准 `TextField` 的焦点下划线、标准 `ItemDelegate`
> 的悬停/高亮都在）。
> 行为变化：`Home`/`End` 不再是列表导航键（归输入框做光标移动），
> 鼠标左键点一行会**改变键盘选中项**（点选 + 复制），滚轮/拖滑块仍然不改。

> **2026-09 新增（帮助窗口 `Enter`/双击直接执行动作）的 DoD**：
> `windows-debug` 与 `windows-release` 两边都是 `build exit 0`、零警告；
> `ctest --test-dir build/windows-release` **19 个测试目标全绿**
> （`tst_help_model` 22 项：`enterCopiesTheActiveRow` 换成 `enterRunsTheActiveRow`，
> 新增 `destructiveRowsNeedASecondConfirmation`、`escapeAndNavigationDisarm`、
> `doubleClickActivatesTheRow`；`tst_config` 新增 `destructiveActionsAreFlagged`）；
> `qmllint -I C:\Qt\6.11.2\mingw_64\qml src\qml\HelpPopup.qml` 零警告；
> `flowkeyd --check --config flowkeyd.lua.example` → `OK (37 hotkey(s), 3 remap(s))`、
> 零警告，用户真实配置（不带 `--config`）→ `OK (24 hotkey(s), 0 remap(s))`。
> `scripts/acceptance.ps1`（只跑 release）**112 项、0 失败**
> （`checks: 112, failures: 0`；上一次是 86）：新增「`Enter` 执行的是刚点中的
> 那一行（动作真的跑了）」、「执行动作之前帮助窗口先关掉了」、「双击一行直接
> 执行它的动作」、「点选之后 `Enter` 执行的就是刚点中的那一行」、
> 「危险动作第一次 `Enter` 只是等确认（窗口没关 / 守护进程还活着 / 日志里没有
> `quit`）」、「`Esc` 取消确认」、「第二下 `Esc` 只清筛选」、「第三下才关窗」、
> 「危险动作第二次 `Enter` 真的执行了」（用可逆的 `suspend` + 立刻恢复）。
> 脚本的一次性配置新增了 `accept-help-first`（`Ctrl+Alt+F19` →
> `clipboard set HELP-FIRST`），快捷键 14 → 15、帮助条目 17 → 18。
> 行为变化：`Enter` 不再复制而是执行（复制保留在左键单击上），
> 执行前先关窗，`quit`/`suspend`/`power` 要按两次，`Esc` 从两级变三级
> （README 的 `help` 动作表与「快捷键帮助」一节、`flowkeyd.lua.example` 已同步）。
> 这次真正查出的 bug 是“可见行下标当成条目下标”，见第 10 节。

> **2026-09 重构（电源选单改用 Qt 自带的列表）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零警告；
> `ctest --test-dir build/windows-release` **19 个测试目标全绿**
> （`tst_menu_model` 12 项：删掉 `hitTestHitsRowsAndIgnoresChrome`，
> `rowsAreStackedInsideTheCard`/`rolesExposeTheGeometryForQml` 换成
> `listMetricsMatchTheCardGeometry`/`rolesExposeWhatTheDelegateNeeds`，
> 后者连带盯着 `highlighted`/`hovered`/`rowRect` 这三个角色名
> **不许再加回来**）；
> `qmllint -I C:\Qt\6.11.2\mingw_64\qml src\qml\MenuPopup.qml src\qml\HelpPopup.qml src\qml\LogWindow.qml`
> 零警告；`flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s))`、零警告，用户真实配置（不带 `--config`）→
> `OK (24 hotkey(s), 0 remap(s))`；`--version` 仍是 `flowkeyd 0.1.0` + `Lua 5.5.1`。
> `scripts/acceptance.ps1`（只跑 release）**116 项、0 失败**
> （`checks: 116, failures: 0`；上一次是 112）：新增「为了点选检查能再打开
> 一次选单」、「能拿到选单窗口的矩形（点选用）」、「鼠标左键点一行直接执行
> 它的动作」、「点完选单关掉了」。旧的「选单不响应滚轮：`Enter` 选的还是
> 光标下的 `cancel` 那一行」原样保留并**真的跑了**（它现在由标准委托的
> `hovered` 驱动），所以“悬停→高亮→`Enter`”整条链路是真实鼠标移动验证过的。
> 弹窗本身的渲染与交互另外用 `tmp/preview`（真实的 `PopupHost` + 真实 QML，
> 进程内注入鼠标与键盘，锁屏时也能跑）验证：委托高 `40`（= `rowHeight 38` +
> `rowSpacing 2`）、行距 `40`、`ListView.y == listTop == 40`、
> 列表高 `count * 40`、`ItemDelegate` 实测内边距左右 `12` 上下 `8`
> （`contentItem` `276x24`）、悬停第 4 行→`hover()==3` 且 `accept()==3`、
> 移出卡片→`hover()==-1` 且高亮回到键盘项、单击第 2 行→`onChoose(1)` 且窗口
> 关掉、`↓`/`Enter`/`Esc` 照旧、重新打开复位高亮；截图 `tmp/menu-new.png`。
> 行为变化：选单的行高亮底与左侧色条改由 FluentWinUI3 的标准 `ItemDelegate`
> 画（行宽从「内缩 10」变成整张卡片宽），悬停/点击/滚轮语义不变
> （README 的「选单与电源」一节已同步）。
> **本次构建用了第 10 节的改名绕路**：用户的提权常驻实例锁住了旧的
> `build\windows-release\flowkeyd.exe`（agent 杀不掉），把它改成
> `flowkeyd.exe.locked` 之后 release 链接与 `windeployqt` 都正常；
> 常驻实例需要用户从托盘退出后重新 `Start-Process -Verb RunAs` 一次
> （它现在跑的是旧构建）。

> **2026-09 修复（和弦限制 + 配置错误弹窗）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零编译警告（`windeployqt` 那句
> `dxcompiler.dll` 警告是它自己的，不是编译器警告）；
> `ctest --test-dir build/windows-release` **19 个测试目标全绿**
> （`tst_keys` 新增 `NumpadMult` 与“两键不是和弦”的断言，
> `tst_config` 新增 `twoPlainKeysAreNotAChord`，`tst_command_line` 加了一对
> 固定的 `AAB` / `A_Z` 覆盖项并修好了环境块排序的大小写折叠方向）；
> `flowkeyd --check --config flowkeyd.lua.example` → `OK (37 hotkey(s), 3 remap(s))`、
> 零警告；用户真实配置（不带 `--config`）→ `OK (24 hotkey(s), 0 remap(s))`、
> 零警告（静音已改为 `NumpadMult`）。配置错误弹窗用一次性坏配置实测：
> 进程弹出一个标题为 `flowkeyd 配置错误` 的可见窗口（唯一窗口），
> 关掉后退出码为 1。
> **行为变化**：小键盘静音的绑定从（无效的）`NumpadSub+NumpadAdd` 改为
> `NumpadMult`；守护进程启动时配置出错会弹 Qt 标准消息框（README 已同步，
> 离线命令仍然是只打印）。本机没有常驻实例在跑，release 直接链接成功。

> **2026-09 修复（弹窗的中文改成微软雅黑）的 DoD**：`windows-debug` 与
> `windows-release` 都是 `build exit 0`（唯一那句 `dxcompiler.dll` 是
> `windeployqt` 自己的提示，不是编译器警告）；
> `ctest --test-dir build/windows-release` **19 个测试目标全绿**
> （本次只改 QML 与文档，没有新增/修改可单测的逻辑）；
> `qmllint -I C:\Qt\6.11.2\mingw_64\qml` 对三个 QML 文件零警告。
> 弹窗本身用 `tmp/preview`（真实的 `PopupHost` + 真实 QML，进程内注入，
> 锁屏下也能跑）验证：新增的字体检查在**选单 17 个 / 帮助 120 个**会画字的
> 控件上全部命中 `Microsoft YaHei`（`font.family()` 与 `QFontInfo(font).family()`
> 都是它），`failures: 0`；截图 `tmp/font-yhei-menu.png` /
> `tmp/font-yhei-help.png`（雅黑）与 `tmp/font-compare.png`（默认族 vs YaHei
> 的对照图，默认族下的中文是衬线）。
> `scripts/acceptance.ps1` **没跑**：验证时桌面是锁的（`LogonUI` 在跑，
> 脚本会在“捕捉窗口拿到键盘焦点（正对照）”那一条挂掉），而本次只动 QML 的
> `font.family`（行高、卡片宽度、行距、滚动条位置都没变，脚本用的坐标不受影响），
> 按第 5 节不属于“必须重跑验收”的改动。解锁后想确认的话直接跑一遍即可
> （预期仍然 **116 项 / 0 失败**）。

> **2026-09 约定变更（测试改用 debug profile + 新增发布目录 `build/dist-release`）**：
> 项目所有者拍板：**release 目录必须是「可以直接拷贝到其他机器运行的发布版本」，
> 测试用的 exe 一律走 debug 产出目录**。落地方式：
> `CMakeLists.txt` 新增 `FLOWKEYD_BUILD_TESTS`（debug preset `ON` / release preset
> `OFF`，默认跟着 `CMAKE_BUILD_TYPE`），整段单测（含 `enable_testing()`）都包在
> 它里面 —— release 构建树里因此不再有 `tst_*.exe`、测试用的 `*_autogen`、
> `CTestTestfile.cmake`；release profile 另加一条 `POST_BUILD`，把 exe 与它的
> `windeployqt` 运行时放进**干净的 `build/dist-release/`**（只有 exe + Qt/MinGW
> 运行时 + QML 模块，45 个条目，没有任何构建系统文件）。
> `CMakePresets.json` 去掉了 `testPresets.release`（release 里没有测试，留着只会
> 让人把「0 个测试」误当成「全绿」）。
> 本次 DoD：两个 profile 都是 `build exit 0`、零编译警告（release configure 会打印
> `flowkeyd: unit tests are disabled in this profile (FLOWKEYD_BUILD_TESTS=OFF)`）；
> `ctest --test-dir build/windows-debug` **19 个测试目标全绿**；
> `build/windows-release` 里 `tst_*` 计数为 0。
> `build/dist-release/flowkeyd.exe` 与 `build/windows-release/flowkeyd.exe`
> **SHA-256 完全相同**；把 `dist-release` 整份拷到别处、并把 `PATH` 清成
> `C:\Windows\System32;C:\Windows` 之后，`--version`、`--check --config
> flowkeyd.lua.example`（`OK (37 hotkey(s), 3 remap(s))`、零警告）与 `--list-keys`
> 都正常，守护进程也能装钩子、被 `taskkill`（**不带** `/F`）干净停掉
> （日志末尾 `keyboard hook removed`）。
> 存量清理：把旧 `build/windows-release` 里遗留的 19 个 `tst_*.exe` 与它们的
> `.manifest`/`*_autogen`、`CTestTestfile.cmake`、`Testing/`、改名留下的
> `flowkeyd.exe.locked` 全都删掉了（目录条目从 129 降到 69）。
> `scripts/acceptance.ps1` 的默认 `-Exe` 仍然指向 `build/windows-release`，无需改动
> （它与 dist 里的是同一个二进制）；本次改动不涉及钩子/引擎/分发/窗口后端，
> 所以按第 11 节第 3 条没有重跑验收脚本。
> **本节里所有「ctest 只跑 windows-release」的历史记录都早于这次变更**，
> 保留它们只是为了记录当时的做法。

> 提醒：Qt 的编译单元很多，`--preset` 的构建目录是分开的
> （`build/windows-debug` / `build/windows-release`），所以
> **debug 实例在运行不会锁住 release 产物**，反之亦然。
> 但仍然要记住：正在运行的 `flowkeyd.exe` 会锁住它自己那个 profile 的产物。

> **2026-09 新增（开机自启 + `--quit` + 安装脚本）的 DoD**：
> `windows-debug` 与 `windows-release` 都是 `build exit 0`、零编译警告
> （release 的 `dist-release` 里也是新构建，Size/时间戳与 `windows-release` 一致）；
> `ctest --test-dir build/windows-debug` **19 个测试目标全绿**
> （`tst_instance` 从 4 项到 7 项：新增 `quitEventNameFollowsTheKey`、
> `quitEventRoundTrip`）；`flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s))`、零警告，用户真实配置 →
> `OK (24 hotkey(s), 0 remap(s))`；`--version` 仍是 `flowkeyd 0.1.0` + `Lua 5.5.1`。
> 实测（本机，`session 1`，均为真实提权环境）：
> * `scripts/install.ps1` 把 `build\dist-release`（1378 个文件）装到
>   `C:\Program Files\flowkeyd`，注册任务 `flowkeyd`，并拉起实例；
>   任务导出的参数：`RunLevel=Highest`、`MSFT_TaskLogonTrigger` + `PT15S` +
>   `UserId=WKS-HW\xingjian`、`ExecutionTimeLimit=PT0S`、
>   `MultipleInstances=IgnoreNew`、`DisallowStartIfOnBatteries=False`、
>   `StopIfGoingOnBatteries=False`、`RestartInterval=PT1M` / `RestartCount=3`、
>   `WorkingDirectory=C:\Program Files\flowkeyd`；日志里是
>   `running elevated: actions can drive windows of elevated processes`（没有提权重启）。
> * 再跑一次 `install.ps1`（更新路径）：旧实例收到 `--quit`，日志依次是
>   `quit requested by another process` / `keyboard hook removed`，随后新实例起来
>   （全程没有 `taskkill`）。`-ExeOnly` 也验证过（换完 exe 的 SHA-256 与
>   `dist-release` 一致），`uninstall.ps1 -RemoveFiles` 用一份 **临时安装目录 +
>   临时任务名** 验证（任务与目录都被清掉，真实任务不受影响）。
> * **非提权的 `--quit`** 专门验了：用一个 `LeastPrivilege` + `InteractiveToken`
>   的一次性任务去跑 `--quit`（这才是真实场景：守护进程提权、调用方不提权），
>   退出码 0、实例真的停了、日志里 `quit requested by another process`。
> * `--quit` 没找到实例时退出码 1、stderr 是
>   `flowkeyd: no running instance for <path>`。
> * `scripts/acceptance.ps1`（只跑 release 产物，先把常驻实例 `--quit` 掉，
>   免得两边的钩子互相干扰；跑完用 `Start-ScheduledTask` 拉回来）：
>   **`checks: 116, failures: 0`**，连跑两次都是。
>   过程里发现并修了**验收脚本自己的一个双屏陷阱**（详情见第 10 节）：
>   前两次跑都在「拖动滚动条真的滚了列表（同一位置已经换了一行）」这一条上挂，
>   根因是弹窗按**光标所在那块屏**居中，而脚本用的是一次性算好的绝对坐标——
>   两次跑之间光标滑到了另一块屏上，坐标就全对不上了（日志证据：
>   `afterDrag=[SENTINEL] fg=…Google Chrome`，即那一下点在了另一个屏的浏览器上）。
>   修法是 `FocusCatcher` 里先把光标归位到主屏的固定点 `(200, 200)`；
>   修完连跑两次 116/0。
> * 顺带查实了一条与产品无关的环境事实：这台机器是 **两块 1920x1080@200%**
>   （物理 3840x2160），所以“弹窗在哪个屏上”取决于光标在哪块屏上。
> 行为变化：新增 `--quit`；常驻实例改为由计划任务启动（当时是安装目录；
> 2026-09 后续又改成“任务指向当前运行的 exe”，见本节后面的记录）。

> **2026-09 约定补充（任务收尾只要保证 `build/dist-release` 最新）**：项目所有者
> 要求「每个任务收尾把编译后的文件（不带无关文件）部署到 `build/dist-release` 即可，
> 不要再部署到 `C:\Program Files\flowkeyd`」。查实后这个机制**本来就有**
> （release 构建的 `POST_BUILD` 自动 `windeployqt` 到 `build/dist-release`），
> 所以**不需要任何部署脚本**：工作约定第 11 条与第 11 节第 9 条改写成
> 「先 `--quit` 常驻 → 构建 release → 从 dist-release 重新拉起」，
> `scripts/install.ps1` / `scripts/uninstall.ps1` **已删除**。
> （历史：更早的版本是 `install.ps1` 把 `build\dist-release` 镜像到
> `C:\Program Files\flowkeyd` 并注册任务；那个安装目录与脚本都作废了。）
>
> **2026-09 自注册改造的 DoD**：`windows-debug` 与 `windows-release` 都是
> `build exit 0`、零编译警告；`ctest --test-dir build/windows-debug`
> **20 个测试目标全绿**（新增 `tst_autostart` 11 项：XML 渲染/解析、UTF-16 输出
> 解码、路径归一、延迟渲染）；`flowkeyd --check --config flowkeyd.lua.example`
> → `OK (37 hotkey(s), 3 remap(s))`、零警告。实测：daemon 启动时自己把计划任务
> 重写成当前 exe（日志 `logon autostart task `flowkeyd` updated: ...`），
> `--remove-autostart` 删得掉且幂等，再启动又自己回来；`--no-elevate --allow-multi`
> 的开发实例只记 `autostart task not managed` 且不动任务；旧安装目录
> `C:\Program Files\flowkeyd` 已删除；`scripts/acceptance.ps1` **116/0**
> （它用 `--no-elevate` 起临时守护进程，所以不会被自启逻辑打扰）。
> 常驻实例现在从 `build\dist-release\flowkeyd.exe` 跑，任务指向它。
> 本次只有新的 `tst_autostart` 不碰真实计划任务（纯函数断言），其他测试未变。

> **2026-09 新增（启动时的两个交互确认）的 DoD**：`windows-debug` 与
> `windows-release` 两边都是 `build exit 0`、零编译警告；
> `ctest --test-dir build/windows-debug` **20 个测试目标全绿**
> （`tst_instance` 新增 `instanceRunningSeesTheMutex`：互斥体在时看得见、
> 释放后看不见）；`flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s))`、零警告。
> 实机验证（`build/windows-debug`）：
> * 已在运行：第一个实例起来后，第二个加 `--no-prompt --console` 退出码 1、
>   stderr 是 `another flowkeyd instance already owns … (use --allow-multi to override)`；
>   不加 `--no-prompt` 时弹出一个标题为 `flowkeyd 已在运行` 的原生框，
>   关掉它之后进程退出（提示确实出现在**提权之前**：该次启动全程没弹 UAC）。
> * 开机自启：提权实例 + 一次性配置 → 弹出 `flowkeyd 开机自启` 框（任务当时指向
>   `build\dist-release\flowkeyd.exe`，与调试 exe 不同），拒绝后日志是
>   `logon autostart task `flowkeyd` left unchanged: the user declined to update it`，
>   前后 `schtasks /Query /XML` 逐字节相同。
> * `scripts/acceptance.ps1` 的「第二个实例被拒绝」改用 `--no-prompt` 启动。
> 行为变化：重复启动不再静默退出，而是（提权之前）弹一个「已在运行」提示框；
> 注册 / 刷新开机自启之前先问用户；新增 CLI 开关 `--no-prompt`。
> `README.md` 的「开机自启与更新」「命令行」两节与 `AGENTS.md` 第 2 节第 11 条
> 已同步。

> **2026-09 新增（构建版本号：托盘右键 + 启动日志 + `--help`）的 DoD**：
> 版本号最终是 **`yy-MM-dd-<git 短修订>`**（格式改过两次：`0.1.0 (build …)` →
> `yyyyMMddHHmm` → 现在这个，项目所有者逐次拍板）。
> `windows-debug` 与 `windows-release` 两边都是 `build exit 0`、零编译警告
> （release 里那两句 `dxcompiler.dll` 是 `windeployqt` 自己的提示）；
> `ctest --test-dir build/windows-debug` **21 个测试目标全绿**
> （`tst_version` 4 项：日期推导、缺文件时退化、修订段形状、`buildVersion()` 的
> 拼装，全部只碰临时文件）。`flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s))`、零警告。
> 实测：提交前那次构建（HEAD `42900ad`）里，配置输出是
> `-- flowkeyd: source revision 42900ad`，`build/windows-debug/generated/flowkeyd_revision.h`
> 就是它，`--version` → `flowkeyd 26-09-22-42900ad`。**提交 `a43340c` 之后两个
> profile 都自动重新配置到了新哈希**（这正是 `CMAKE_CONFIGURE_DEPENDS` 那条要验的
> 东西）：两份生成头都变成 `a43340c`，`build/dist-release/flowkeyd.exe` 的
> `--version` 变成 `flowkeyd 26-09-22-a43340c`，重新拉起的常驻实例日志第一行是
> `flowkeyd 26-09-22-a43340c starting` —— 这是快照：版本号里的 rev 永远是**构建
> 当时的 HEAD**，所以这份记录自身那个（只改文档的）提交不会再出现在二进制里，
> 收尾时是「提交 → 构建 → 重新拉起常驻」，让常驻跑的那份就对应 HEAD。
> 托盘菜单用进程内 dump 验证（真实 `Tray` + `findChild`）：`版本 <日期>-<rev>`
> 是 `enabled=0` 的信息项，悬停提示带构建版本与挂起状态。
> `build/dist-release` 已是最新（release 的 `POST_BUILD` 自动产出，SHA-256 与
> `build/windows-release/flowkeyd.exe` 相同，`tst_*` 计数为 0），常驻实例已按
> 工作约定第 11 条 `--quit` → 重建 → 从 `dist-release` 重新拉起，自启任务仍指向
> `build\dist-release\flowkeyd.exe`。
> **注意版本号里的哈希就是构建时的 HEAD**：本次的文档改动本身会成为一个提交，
> 所以最终发布的那份 exe 内嵌的是**那个提交**的哈希，不要去追这份记录里写的例子
> （它是构建当时的 HEAD）。要让它显示最新提交就先提交再构建。
> 本次没有改钩子/引擎/分发/窗口后端，所以按第 11 节第 3 条没有重跑
> `scripts/acceptance.ps1`（托盘那一项本来也进不了那个脚本）。

> **2026-09 新增（应用图标：exe 资源 + 托盘）的 DoD**：`windows-debug` 与
> `windows-release` 都是 `build exit 0`（release 里那两句 `dxcompiler.dll` 仍是
> `windeployqt` 自己的提示）；`ctest --test-dir build/windows-debug`
> **21 个测试目标全绿**（本次没有新增可单测的纯逻辑，所以数量不变）；
> `flowkeyd --check --config flowkeyd.lua.example` → `OK (37 hotkey(s), 3 remap(s))`、
> 零警告。
> 图标本身：`logo.svg` → `assets/flowkeyd.ico`（9 帧：16/20/24/32/40/48 为 DIB，
> 128/256 为 PNG，共 56 KB）与 `assets/icons/flowkeyd-<n>.png`（9 张），
> `cmake --build --preset debug --target icons` 可重现（跑两次 `assets/flowkeyd.ico`
> 的 SHA-256 完全相同：`77AED5F5…`）。release 构建里 windres 编出了
> `CMakeFiles/flowkeyd.dir/generated/flowkeyd.rc.obj` 并正常链接到 exe；
> `[System.Drawing.Icon]::ExtractAssociatedIcon(build\dist-release\flowkeyd.exe)`
> 返回 32x32 并 `ToBitmap()` 出来就是 logo（`tmp/exe-icon.png`，非空壳），
> `assets/flowkeyd.ico` 的 9 个目录项也被独立解析过一遍（尺寸/偏移都对）。
> **托盘图标没有做功能验证**（项目所有者要求：编译完他自己看）——所以第 11 节
> 第 3 条那套验收脚本本次没跑；若托盘图标没显示，先查
> `build/windows-release/imageformats/` 与 qrc 的 `:/icons/…` 是否在
> （`src/app/app_icon.cpp` 对缺失文件会退到系统图标，不会报错）。
> 按工作约定第 11 条：常驻实例已 `--quit` → 构建 release → 从
> `build\dist-release` 重新拉起（它启动时会自己把计划任务刷新成这个路径）。

> **2026-09 新增（`window_rule`：按程序摆放窗口到指定 workspace / monitor）的 DoD**：
> `windows-debug` 与 `windows-release` 两边都是 `build exit 0`、零编译警告
> （release 里那两句 `dxcompiler.dll` 是 `windeployqt` 自己的提示）；
> `ctest --test-dir build/windows-debug` **22 个测试目标全绿**
> （新增 `tst_placement` 14 项：显示器排序/选择、重连检测、摆放几何、匹配与摘要；
> `tst_config` 新增 `windowRulesCompile` / `windowRuleGeometryDefaults` /
> `windowRuleProblemsAreReported`；`tst_lua` 新增 `windowRulesAreConverted`）；
> `tst_interactive`（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）**9 项全绿**，其中两个是
> 新增的：`moveWindowToAnotherDesktopAndBack`（用**已公开**的
> `GetWindowDesktopId` 确认窗口的桌面 GUID 真的变了、再移回来）与
> `placementMovesAWindowToAnotherMonitor`（跨屏 + 最大化 + 还原成普通窗口）。
> `flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s), 3 window rule(s))`、零警告；
> 用户真实配置（不带 `--config`）→ `OK (24 hotkey(s), 0 remap(s))`、零警告。
> `scripts/acceptance.ps1`（只跑 release）**116 项、0 失败**。
> 手工端到端（`--no-elevate --allow-multi` 的一次性实例 + 一次性配置）：
> * 记事本启动后日志里出现
>   `window rule `notepad-right` -> "..." [Notepad]: desktop 2/4, maximized 3840x2160 at 3840,0`，
>   `GetWindowPlacement` 确认它在第 2 块屏、`WS_MAXIMIZE` 置位；
> * 应用内部窗口（`Non Client Input Sink Window` / 无标题的 `NotepadTextBox`）
>   被 `WS_EX_TOOLWINDOW` / “有标题”这两条过滤挡掉；
> * `DisplaySwitch.exe /internal` 再 `/extend` 真的制造了一次重连，日志里出现
>   `monitor connected: DISPLAY2; re-applying window rules`，随后把手工挪到
>   另一块屏的记事本重新摆回第 2 块屏并最大化。
> **顺带修了一个老 bug（行为变化）**：`SetTimer(nullptr, id, …)` 会忽略 `id`，
> 所以钩子线程原来 `wParam == 1` 的比较从来不成立，`Engine::tick()` 从未被调用，
> 长按重复（`repeatable = true` / `trigger = "repeat"`）实际上一直是坏的。
> 现在用 `SetTimer` 的返回值比较，长按重复真的会重复了（见第 10 节）。
> 本次没有给 `acceptance.ps1` 加摆放检查（它的 116 项保持原样）：端到端由
> 交互式单测 + 上面那次手工 `DisplaySwitch` 验证覆盖。
> 按工作约定第 11 条：常驻实例已 `--quit` → 构建 release → 从
> `build\dist-release` 重新拉起（它启动时会自己把计划任务刷新成这个路径）。

> **2026-09 修复（`window_rule` 的窗口不会被跨桌面唤醒）的 DoD**：
> `windows-debug` 与 `windows-release` 两边都是 `build exit 0`、零编译警告；
> `ctest --test-dir build/windows-debug` **22 个测试目标全绿**（`tst_interactive`
> 仍是 opt-in，ctest 里 skip）。
> `tst_interactive` 本身（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`，`-o <file>,txt`
> 才看得到 QtTest 输出）**10 项全绿**（连跑三遍）。
> 新增的 opt-in 用例 `activatesAWindowThatIsOnAnotherDesktop` 通过，诊断行确认了
> 现场是 `window on another desktop: shellForegroundIsIt true`（也就是用户报的
> 那个状态）；断言过了四条 —— 跨桌面时 `window::isActive` 必须为假、
> `window::raiseWindow` 必须把视图切回去并让它拿到前台、已回当前桌面时
> `desktop::switchToWindowDesktop` 报 `already on desktop N/M`、
> 搬到它已经在的那张桌面时 `changed` 为假。这也是 `IVirtualDesktop::GetID`
> （下标 4）在本机能用的直接证据。
> **顺手把交互式用例的“窗口载体”换成了测试自己的窗口**（`TestWindow`）：本机的
> 记事本已经变成单实例、带标签页与会话恢复的 Store 应用，拿它当载体的那些断言
> （尤其是“`Close` 之后窗口消失”）在本机已经不可靠，而且**在修复前的提交上一样
> 会挂**（用 `git worktree` 建 `ee38ac6` 实测，不是回归）—— 细节见第 10 节。
> 端到端（`--no-elevate --allow-multi` 的一次性实例 + 一次性配置 + `SendInput`）：
> 记事本一出现，日志除了 `desktop 3/4` 还多了一句 `view -> desktop 3/4`，
> 从 PowerShell 用**已公开**的 `IsWindowOnCurrentVirtualDesktop` 看，视图真的跟着
> 到了那张桌面（修前是 `onCurrentDesktop=0`）；再按一次同一个快捷键则是
> `Minimize ... (already active)`（那时它已经真的在前台了，`toggle` 语义正确）。
> debug 与 release 两份 exe 都跑过这条端到端。
> `flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s), 3 window rule(s))`、零警告；用户真实配置（不带
> `--config`）→ `OK (24 hotkey(s), 0 remap(s), 3 window rule(s))`、零警告。
> `scripts/acceptance.ps1`（只跑 release，先把常驻 `--quit` 掉、跑完再拉起）
> **116 项、0 失败**（`checks: 116, failures: 0`，与上次持平：脚本本身没动）。
> 行为变化（项目所有者拍板，见第 2 节第 14 条）：跨桌面唤起会**切桌面**；
> `window_rule` 真的搬迁窗口时视图会跟着走（只在窗口第一次出现那一遍）。
> README（`window` 动作、`window_rule`、已知限制、验证）、
> `flowkeyd.lua.example` 与第 10/11 节已同步。
> 按工作约定第 11 条：常驻实例已 `--quit` → 构建 release → 从
> `build\dist-release` 重新拉起（自启任务仍指向那个路径），版本号里的 git 修订
> 就是最终提交（「提交 → 构建 → 重新拉起」的顺序）。
> 另外顺手清理了 `build/windows-release` 里那批从“release 还构建测试”的年代
> 留下的 `CMakeFiles/tst_*.dir` 目录（构建树里不再有 `tst_*`）。

> **2026-09 新增（`window_rule` 的 `all_desktops` 与 `topmost`）的 DoD**：
> `windows-debug` 与 `windows-release` 两边都是 `build exit 0`、零编译警告
> （release 里那句 `dxcompiler.dll` 仍是 `windeployqt` 自己的提示）；
> `ctest --test-dir build/windows-debug` **22 个测试目标全绿**
> （`tst_config` 新增 `windowRulePinsAndTopmost` + `all_desktops`/`desktop` 互斥；
> `tst_lua` 的 `windowRulesAreConverted` 加了 `all_desktops`/`topmost`（含显式
> `false`）；`tst_placement` 的 `windowRuleSummaryIsReadable` 加了两种摘要）。
> `tst_interactive`（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）**12 项全绿**，新增
> `pinsAWindowToAllDesktops`（`IsViewPinned` 确认 `PinView`/`UnpinView` 真的生效、
> 幂等时 `changed` 为假）与 `topmostIsAppliedAndCleared`（先按 `placeWindowOnce`
> 的顺序摆一遍几何再置顶）。
> `flowkeyd --check --config flowkeyd.lua.example` →
> `OK (37 hotkey(s), 3 remap(s), 4 window rule(s))`、零警告；
> 用户真实配置（不带 `--config`）→ `OK (24 hotkey(s), 0 remap(s), 4 window rule(s))`、
> 零警告，`--list` 里有 ``window rule `wezterm-all-desktops`: process "wezterm", all desktops``。
> `scripts/acceptance.ps1`（只跑 release，先把常驻 `--quit` 掉）**116 项、0 失败**
> （与上次持平：脚本本身没动，但 `window::setTopmost` 是从 `toggle_topmost`
> 里抽出来的，所以重跑了一遍）。
> **真机端到端**：常驻实例（`build\dist-release`，提权）启动时把正在跑的 WezTerm
> 窗口钉住了，日志里是
> ``window rule `wezterm-all-desktops` -> "π - flowkeyd" [org.wezfurlong.wezterm]: all desktops``；
> 再用一个只读的 PowerShell 探针（`IVirtualDesktopPinnedApps::IsViewPinned`，
> 手写 `[ComImport]` 接口，`tmp/pincheck.ps1`）确认那个 HWND 的 `IsViewPinned=1`。
> 行为变化：`window_rule` 多了两个字段（`all_desktops` 与 `desktop` 互斥，
> `topmost` 用已公开的 `SetWindowPos`）；`window` 动作的 `toggle_topmost`
> 顺手修好了（以前先写 `WS_EX_TOPMOST` 再 `SetWindowPos`，在“刚创建的窗口”上会
> 返回 TRUE 却不生效，见第 10 节）。README（`window_rule` 一节、已知限制）、
> `flowkeyd.lua.example` 与第 10/14 节已同步。
> 按工作约定第 11 条：常驻实例已 `--quit` → 构建 release → 从
> `build\dist-release` 重新拉起（自启任务仍指向那个路径）；本条记录与代码
> 一起提交之后又构建并拉起了一次，所以常驻跑的那份版本号里就是这次提交的哈希
> （「提交 → 构建 → 重新拉起」）。

---

## 12. 本期不做的（有意留白）与后续工作

按项目所有者的决定，**本期不做**下面这些；它们是明确的待办，不是遗忘：

1. **`--simulate <SCRIPT>`**（把脚本化按键事件重放给真正的引擎，干跑）。
   这是最便宜的引擎验证手段，**强烈建议尽早补**：
   它不需要焦点、不装钩子，却能把匹配/吞键/重复/挂起/重映射全跑一遍。
   没有它，引擎的行为只能靠 `scripts/acceptance.ps1`（要真的注入按键、
   要交互式桌面）或者手工按键盘。
2. **`--selftest` / `--probe`**（各平台后端探测与自检）。
   补它们的收益：Core Audio 的 COM vtable、虚拟桌面接口表、
   未公开 API 的可用性这些**只能靠真实调用才能验证**的东西，
   会有一条确定的、幂等的检查路径。
3. **完整的 `scripts/e2e.ps1`**。**`scripts/acceptance.ps1` 已经是它的第一版**：
   它从另一个上下文注入按键、抢焦点、用剪贴板/窗口/日志做外部证据，
   把“钩子真的吞了键”“重映射真的注入了目标键”“`quit` 真的卸了钩子”都变成
   了可重复的断言。还缺的是动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程、
   以及日志窗口那一套的外部断言。
   如果以后要补，就沿着现在这套脚本扩展：断言字符串（英文日志、
   窗口标题格式）是稳定的，正是为了这个。
4. **鼠标钩子**（`WH_MOUSE_LL`）。
5. **延迟修饰键抑制**：让 `Ctrl+Alt+H` 也隐藏 Ctrl 和 Alt
   （相对 AutoHotkey 唯一真正的行为差距）。
6. **配置文件热重载**（去抖的 `ReadDirectoryChangesW`），
   取代手动 `reload` 快捷键。
7. **按应用限定的快捷键**（等价于 AutoHotkey 的 `#If WinActive(...)`）。
8. **把 Lua 函数当动作**：需要在工作线程上长期持有 Lua 状态，
   还要管超时、panic 与挂起/重载时的生命周期。刻意不做：声明式动作才能被
   `--list` 显示、在加载时校验完、并在钩子/工作线程边界上保持安全。
9. **配置里的 `require`/模块支持**（现在只有一份脚本）。
10. **`NumLock` 关闭时小键盘的导航键**（`8`/`2`/`4`/`6`/`0`/`.`/`Home`/`End`/
    `PgUp`/`PgDn`）与主键盘同名键的区分：做法可以照抄小键盘 Enter 的伪码表。
    注意这是**行为变化**：`keys = "Up"` 将不再匹配小键盘的 `8`。
11. **弹窗的条目图标**（配色已经跟随系统了：卡片全部走 `palette`；
    剩下的是条目左侧的图标位）与更细的动画。
12. **帮助窗口的模糊搜索、按 `comment` 分组**（筛选框自 2026-09 起是标准的
    `TextField`，所以中文/输入法已经能用了；缺的只是模糊匹配与分组）。
13. **日志窗口的增强**：`--follow`/`--grep` 之类的参数、把 `INFO` 与 `DEBUG`
    分色渲染（现在只按级别上色）。
14. **托盘图标跟随 explorer 重启**（处理 `TaskbarCreated`）。
    （真正的应用图标本期已经做了：`logo.svg` → `assets/` 下的 .ico 与 PNG，
    见第 10 节“应用图标”那一段。）

---

## 13. 在哪里扩展

* **新动作类型**：`core/action.h` 加一个变体 → `Action::summary` 里加摘要 →
  `app/dispatcher` 里处理它 → 如果它带参数，加进 `core/config` 的
  `validate_action`（顶层动作与 `menu` 条目共用它）→ 在 `lua_prelude.lua` 里加
  一个构造器并挂进 `flowkeyd` 表（否则用户只能手写 `{ type = "..." }`；
  构造器如果是“原样返回用户表”的那种，**记得自己补 `type`**）→
  写进 `README.md` 的表格与 `flowkeyd.lua.example` →
  **在 `scripts/acceptance.ps1` 里加一条能自动验证的检查**（面板/弹窗/剪贴板
  之类能从外部观察的，别留给人的手）。
* **新的选单条目字段**（例如图标）：`config` 加字段 → `MenuModel` 与
  `MenuPopup.qml` 里画出来 → 校验（重名、空标签）→ README 表格。
  条目是在委托的 `contentItem` 里用锚点摆的（见 `HelpPopup.qml`），
  **不要再把行几何（矩形）往模型里塞**：行下标就是 `ListView` 的下标，
  鼠标命中、悬停、点击全由标准 `ItemDelegate` 提供。
* **帮助窗口的新内容或新交互**：条目在 `app/dispatcher` 的 `open_help` 里从
  `Compiled` 的 `bindings`/`remaps` 生成（帮助列表与 `--list` 看的是同一批数据，
  所以 `help` 没有配置参数），纯逻辑（筛选、选中项、`Enter`/双击该执行还是
  先武装、三级 `Esc`、鼠标点选用的 `setSelected`）在 `HelpModel`，界面在
  `HelpPopup.qml`（真正的 `TextField` 筛选框 + `ListView`/`ItemDelegate`/`ScrollBar`）。
  **新加一行字段时不要再把行几何往模型里塞**：委托用锚点自己摆，模型只出内容。
  新加模型角色名时注意别和标准控件自己的属性撞名 —— `highlighted` 被
  `ItemDelegate` 占了，所以模型里叫 `rowSelected`（待确认的叫 `rowArmed`）
  （见第 2 节第 9 条与第 10 节的重写笔记）。
  **要让一行能执行动作，就在 `openHelpAction()` 的 `targets` 里给它一个条目
  （绑定用 `press`+`release`，`remap` 用两串 `SendOp`），不要往 `HelpEntry`
  里塞 `core::Action`**：模型层与 `core` 之间现在只靠一个 `destructive` 布尔量
  连着，那样才能保持只用 QtCore、没有桌面也能单测。
* **新的 QML 弹窗（第三种）**：不要另起一套配色与字号：
  `import QtQuick.Controls.FluentWinUI3`，颜色一律从 `palette`（`base`/`text`/
  `placeholderText`/`highlight`/`highlightedText`/`alternateBase`/`mid`）取，
  字号用现在这套 `pointSize`（12.5 标题 / 11 正文 / 10.5 帮助正文 /
  9 副标题与徽标 / 8.5 细节）；**中文一律 `font.family: "Microsoft YaHei"`**
  （见第 10 节“弹窗的中文落到宋体”，`Window`/`Item` 没有 `font` 属性，
  不会自动往子项传，每个 `Label`/`TextField` 都得写）；
  需要滚动的列表用真正的 `ListView` +
  `ScrollBar`（不要自绘滑槽），固定表头/底部提示看 `HelpPopup.qml` 的
  “`topMargin`/`bottomMargin` + 不透明底色”三件套；
  **避开 FluentWinUI3 不支持的那些控件**（见第 10 节）。
  文件开头写 `pragma ComponentBehavior: Bound`，并用 `qmllint -I …` 确认零警告。
* **新的电源操作**：`PowerOp` 加变体 → `platform/win/power` 里处理
  （需要特权的先调 `enable_shutdown_privilege()`；不需要的要放在它**之前** return）
  → `as_str` 与简写 → README 表格。**不给它加自动化测试**（破坏性；见工作
  约定第 10 条：测试里一律不许真的执行电源动作）。
* **改配置模式（新字段 / 新取值）**：`core/config` 加字段 →
  需要的话在 `lua_prelude.lua` 里加构造器 → `flowkeyd.lua.example` 里加一条
  （`--check` 会立刻告诉你它能不能过校验）→ README 表格。
  **字段名不要用 Lua 关键字**（`repeat`、`end`、`for`、`local`、`function`、
  `then`、`until`……）；需要的话给它一个 Lua 友好的别名。
* **新的窗口摆放字段**：`core/config` 加字段 → `lua_config.cpp` 的
  `convertWindowRule` 加白名单与读取 → `lua_prelude.lua` 的 `window_rule` 注释 →
  `core/placement` 里影响几何/匹配（或只影响 `summary()`）→ `app/dispatcher` 的
  `placeWindowOnce` → README 的 `window_rule` 一节与 `flowkeyd.lua.example` →
  `tst_placement` / `tst_config` / `tst_lua`。
  如果新字段要调新的 Win32/COM 后端，放在 `platform/win/*` 里，并在
  `tst_interactive` 里加一条真机验证（`all_desktops`/`topmost` 就是这么做的）。
* **新的窗口条件**：`core/window_match`（纯逻辑）+ `platform/win/window` 的
  枚举适配 + `launch_then_activate` 回退 + 手工冒烟清单里加一条用例。
* **新按键或别名**：扩展 `core/keys` 的键表并加一个往返用例
  （要有一个测试遍历表里的每个名字）。如果那个键要靠扩展标志才能与别的键区分
  （像小键盘的 Enter），还要在 `key_from_hook`/`native_key` 里加一条翻译。
* **新的 Windows 版本的虚拟桌面接口**：往 `platform/win/desktop` 的版本表里加
  一条（生效的 `build.revision`、两个 IID、vtable 布局），然后在真机上确认
  选中的条目、桌面数量与序号。
* **新的虚拟桌面能力**（“把窗口钉到所有桌面”已经这么做了）：先在
  `platform/win/desktop` 里手写那个接口的 vtable 结构体，字段下标以 VD.ahk /
  MScholtes 的实现为参考，并尽量用**已公开**的 API 做一次可验证的交叉检查
  （`GetID` 只认“有且只有一个匹配”；`PinView`/`UnpinView` 用同一个接口的
  `IsViewPinned` 验证状态真的变了）。失败时只报错、不要去做可能是错的事。
  **先判断那个 IID 是否随版本变化**：变的（如 `IVirtualDesktopManagerInternal`）
  进版本表；不变的（如 `IVirtualDesktopPinnedApps`）只用一个常量，别硬塞进表。
* **新的未公开 API**：在 `platform/win/nt` 里用 `GetProcAddress` 解析，
  使用前先用一次无害调用校验，并永远保留一个已公开的回退。
  已公开但不在静态链接集合里的库走同一条路（`dwmapi` 是范例）。
* **新的动作后端**：在 `src/platform/win/` 下新建模块，从 `dispatcher` 调用，
  并（如果以后补了 `--selftest`）加一项检查。
* **换图标**：改仓库根目录的 `logo.svg`（唯一的真源），然后
  `cmake --build --preset debug --target icons` 重新生成 `assets/flowkeyd.ico`
  与 `assets/icons/flowkeyd-<n>.png`（**两者都要提交**）；尺寸列表写在
  `tools/icon_gen/main.cpp`（`kSizes`）、`CMakeLists.txt` 的 `qt_add_resources`
  与 `src/app/app_icon.cpp` 里，**三处要一起改**。只想改 exe 图标或只想改托盘图标
  是不可能的：两份产物用的是同一张源图（exe 那边走 windres、托盘那边走 qrc，
  见第 10 节“应用图标”）。新图标如果不再是正方形，还要看一眼 `renderFrame()`
  的铺满策略（现在是 KeepAspectRatio）。
* **新的日志窗口行为**：尾随逻辑在 `app/log_model`（纯逻辑、可单测），
  渲染在 `LogWindow.qml`。加命令行参数就改 `cli.cpp` 并更新 README 的
  命令行表格。
* **构建版本号的来源**：全部在 `src/core/version.*`（日期来自运行中 exe 的最后
  写入时间，修订来自 CMake 生成的 `FLOWKEYD_GIT_REVISION`，见
  `cmake/version_revision.h.in`）。要换成别的来源就改 `buildVersion()` /
  `buildDateFromFile()` / `sourceRevision()`，显示侧（托盘、启动日志、
  `--version`、`--help`）不用动。

---

## 14. 配置 schema 速查

**这一节只是索引，权威定义在 `README.md`「配置」那一章与
`flowkeyd.lua.example`。** 任何一处不一致，**以 README 为准并修本文件**。

### 路径与全局表

| 项         | 值                                            |
| ---------- | --------------------------------------------- |
| 配置目录   | `%USERPROFILE%\.config\flowkeyd\config.lua`   |
| 日志文件   | `%USERPROFILE%\.config\flowkeyd\flowkeyd.log` |
| DSL 全局表 | `flowkeyd.*`（挂在脚本全局的注册表）          |
| 示例配置   | `flowkeyd.lua.example`                        |

CLI 开关：`-c/--config`、`--no-elevate`、`--console`、`--elevated`、
`--check`、`--list`、`--list-keys`、`--quit`、`--no-autostart`、
`--remove-autostart`、`--log-window`、`--log-level`、`--log-file`、`--no-color`、
`--allow-multi`、`--no-prompt`、`-h/--help`、`-V/--version`。
`--parent-pid` 与 `--simulate`/`--selftest`/`--probe` 见第 12 节；
`--quit`（请正在跑的实例干净退出）见第 2 节第 10 条与第 5 节；
`--no-autostart` / `--remove-autostart`（自启任务的跳过与删除）见第 2 节第 10 条。

### 配置语义要点（容易做漏的）

* `settings{}`：`log_level`、`swallow`、`exact_modifiers`、`release_modifiers`、
  `repeat_interval_ms`、`repeat_delay_ms`、`tick_ms`、`input_backend`、
  `single_instance`、`elevate`。**未知键报错。**
* `hotkey{}`：`keys`（单个和弦或一组）、`name`、`trigger`
  （`press`/`release`/`repeat`）、`action`（别名 `press`、`on_press`）、
  `on_release`、`swallow`、`repeatable`（`true` 或
  `{ interval_ms, delay_ms }`；也认 `["repeat"]`）、`enabled`、`comment`。
  `trigger = "repeat"` 与 `repeatable = true` 是同一件事；
  **互相矛盾的组合要被 `--check` 拒绝。**
* `remap{}`：`from`（键或和弦）、`to`（键名或发送脚本）、
  `mode`（`hold` 默认 / `tap`）、`swallow`、`name`。
* `window_rule{}`：`process` / `title`（至少一个）、`desktop`（1 起）、
  `all_desktops`、`topmost`、`monitor`（序号 / `"primary"` / 设备名 `"DISPLAY2"`）、
  `maximize`、`x`/`y`（相对目标显示器工作区左上角）、`width`/`height`、`name`、
  `enabled`。写了 `monitor` 且没写位置/大小时 `maximize` 默认 true；
  `maximize = true` 与位置/大小互斥；`all_desktops = true` 与 `desktop` 互斥。
  触发时机：窗口首次出现、显示器重新接入、flowkeyd 启动。`all_desktops` 走未公开的
  `IVirtualDesktopPinnedApps`（IID `{4CE81583-…}` 自 Win10 起未变，不进版本表），
  `topmost` 走已公开的 `SetWindowPos`。**规则真的把窗口搬到另一张桌面时，只有
  “窗口首次出现”那一遍会让视图跟着切过去并重新激活它**（见第 2 节第 14 条）。
* 和弦语法：`~` 放行原始按键、`*` 忽略额外修饰键；`Numpad*` 与主键盘同名键不同。
* 动作：`run`/`send`/`type`/`open`/`volume`/`media`/`clipboard`/`window`/
  `notify`/`menu`/`help`/`power`/`desktop`/`caps_lock`/`suspend`/`reload`/
  `quit`/`none`，字段逐条见 `README.md` 的动作表。
  简写字符串：`"run:…"`、`"send:…"`、`"type:…"`、`"open:…"`、`"notify:t|b"`、
  `"volume:up"`、`"media:next"`、`"clipboard:get"`、`"window:minimize"`、
  `"desktop:1"`、`"power:sleep"`、裸关键字 `reload`/`quit`/`help`/`none`。
* **完全没有动作**的快捷键就是一个按键屏蔽器（会吞掉它匹配到的按键）。
* `window` 的 `toggle`（默认**开**）只对 `op = "activate"` 有意义；
  `launch` 回退不套用它；显式 `toggle = false` 才关闭。
  “已经激活”要同时满足：前台、未最小化、**就在当前虚拟桌面上**（见第 2 节第 14 条）。
* `window` 的 `animate`（默认**关**）只对会改变窗口状态的 `op` 有意义，
  写在不产生过渡的 `op`（`close`/`toggle_topmost`）上要被 `--check` 拒绝。

### 本机真实配置（也是验收的清单）

**这份配置住在 `%USERPROFILE%\.config\flowkeyd\config.lua`**。
`flowkeyd --check`（走默认搜寻）→ `OK (24 hotkey(s), 0 remap(s))`，零警告。

`%USERPROFILE%\.config\flowkeyd\config.lua` 里的绑定：

| 快捷键                 | 动作                                                                              |
| ---------------------- | --------------------------------------------------------------------------------- |
| `CapsLock`             | `caps_lock("off")` + `send("^{Space}")`                                           |
| `Alt+H/J/K/L`          | `send("{Left}")`/`{Down}`/`{Up}`/`{Right}`                                        |
| `Alt+Space`            | `send("{F14}")`                                                                   |
| `LWin+Q`               | `send("{F24}")`                                                                   |
| `LWin+F1..F4`          | `desktop(1..4)`（用 Lua `for` 循环生成）                                          |
| `Win+S`                | `window("activate", { process = "wezterm", launch = … })`                         |
| `Win+1/2/3`            | `window("activate", { process = "chrome"/"code"/"wps", launch = … })`             |
| `Win+W`                | `window("activate", { process = "weixin", launch = … })`                          |
| `Win+X`                | `menu{ title = "电源", items = { sleep/shutdown/restart/lock/screen_off/取消 } }` |
| `Win+/`                | `help()`                                                                          |
| 小键盘 `-`/`+`/`*` | `volume("down"/"up"/"toggle")`，前两个 `repeatable`（静音用 `NumpadMult`） |
| `Ctrl+Alt+F12`         | `suspend("toggle")`                                                               |
| `Ctrl+Alt+F5`          | `reload()`                                                                        |
| `Ctrl+Alt+F4`          | `quit()`                                                                          |

**没有** `remap{}`。`settings` 里只有 `log_level = "info"`。

窗口摆放规则（`window_rule`，2026-09 新增；`wezterm-all-desktops` 也是 2026-09 新增）：

| 程序      | desktop | monitor | all_desktops |
| --------- | ------- | ------- | ------------ |
| `chrome`  | 1       | 2       |              |
| `code`    | 2       | 2       |              |
| `wps`     | 3       | 2       |              |
| `wezterm` |         |         | true         |

（`monitor = 2` 在本机是 `\\.\DISPLAY2`，也就是右边那块 1920x1080；
没写位置/大小时默认最大化。`wezterm` 只钉在所有虚拟桌面上、不摆几何。
`--check` → `OK (24 hotkey(s), 0 remap(s), 4 window rule(s))`、
零警告。仅供用户自己手工验证行为，没有进 `acceptance.ps1`。）

---

## 15. 待确认 / 需要用户拍板的事情

写好本文件时（2026-09）留下的开放问题。**动手实现到相应位置前，先问用户。**

1. **release profile 用 `RelWithDebInfo` 还是 `Release`？** 本文件按
   `RelWithDebInfo` 写（保留符号便于定位崩溃，且同样是优化路径）。
   要改就改 `CMakePresets.json` 并更新第 5 节。
   → **已按 `RelWithDebInfo` 实现（阶段 0）**。
2. **任务队列用 `std::mutex` + `condition_variable` 还是
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`？**
   前者与 Qt 解耦（`app/` 层更干净），后者少一层自己写的同步。
   倾向后者（Qt 项目里更自然），但要在第 6 节写定。
   → **已按后者实现（阶段 3）**：`app::Dispatcher` 跑在 `QThread` 上，
   钩子线程用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递。
3. **日志窗口要不要保留 `--log-window` 这个开关？** 本文件按“保留，
   意思是启动时直接打开日志窗口”写；`--parent-pid` 则打算**接受但忽略**
   （为了 CLI 兼容）。如果用户觉得没必要，删掉更干净。
   → **已按这个方案实现（阶段 0）**；不想要的再说一声。
4. **示例配置要不要保留 `Win+X`/`Win+1..3` 这类会吞系统快捷键的例子？**
   现在的示例保留并加了醒目注释（注释里说明“这会吞掉系统快捷键”），
   但这是产品口味问题。
5. **要不要给 flowkeyd 做一份“英文日志 + 中文帮助”的文案约定表？**
   目前只在第 4 条工作约定里写了原则。
6. **提权常驻实例会锁住 release 的 exe，而 agent 杀不掉它**（它跑在用户的管理员
   令牌下，`Stop-Process -Force` / `taskkill /F` 都是“拒绝访问”）。
   每次 release 全量构建前要请用户从托盘菜单点一下“退出”。
   可选的长期解法：把常驻实例改从一份**拷贝**（比如 `%LOCALAPPDATA%\flowkeyd\`）
   启动，构建目录就不再被占用 —— 但那需要用户改一下启动习惯。
   → **已解决（2026-09）**：常驻不再从构建目录跑（旧方案是安装目录的拷贝），
   停实例用 `--quit`（不再需要 `taskkill`），自启任务由守护进程自己注册 / 刷新。
   当年的“改名绕路”（把被锁的 exe `Move-Item` 成 `*.locked`）仍然有效，
   但只在“停不掉时”当降级手段（见第 10 节）。
   → **附带更正**：本机 agent 的 shell **是提权的**（与这段原始的假设不同），
   所以提权实例也能 `Stop-Process`。仍然优先用 `--quit`。
7. **自启任务指向“当前运行的 exe”，所以它跟着用户跑到哪里。** 项目所有者
   2026-09 拍板了这个方向（去掉安装目录 + 部署脚本），并选了「每次启动自检 +
   开发实例跳过」。需要知道的代价：
   * 常驻从 `build\dist-release` 跑时，重建前必须先 `--quit`（exe 被锁）；
   * 若用户把 exe 放在一个会被清理 / 移动的目录，任务会**静默**失效，
     直到下次手动启动一次（自检会把它刷新回来）。
   这两条都已经写进工作约定第 11 条与第 10 节的“开机自启”一节。
