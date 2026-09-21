# AGENTS.md

给 AI agent（以及人类）的 **flowkeyd** 开发笔记。

先读这个文件。它记录了环境、真正能用的命令、**已经拍板的设计决策**、
不能破坏的不变量，以及已经让人浪费过时间一次的坑。

> **参考实现是隔壁的 `../oskeyd`（Rust），但是不需要直接将rust翻译为C++。**
> 凡是 oskeyd 的 `AGENTS.md` / `README.md` 已经写清楚的**领域知识**
> （键盘钩子的各种坑、窗口动作的前台锁绕行、虚拟桌面的接口表、
> 日志查看器的设计、提权的尾巴、`INPUT` 结构体布局……），本文件**不重复**，
> 动手前直接去读那一份。本文件只写「用 Qt/C++ 复刻」特有的东西，
> 以及本项目已经拍板的决策与实施计划。
>
> oskeyd 的两份文档都在本机：
> `D:\prj\oskeyd\AGENTS.md`（1059 行，工程笔记）与
> `D:\prj\oskeyd\README.md`（886 行，用户文档 + 完整配置/动作/schema 说明）。
> **本项目的目标是把 oskeyd 的功能与配置语义 1:1 复刻出来**，
> 所以那两份文档同时也是 flowkeyd 的规格书。

> **本仓库的工作约定**（由项目所有者设定，2026-09）：
> 1. 每个任务结束后都要更新本文件，把新得到的经验和新出现的要求写进去，
>    这样下一个 agent 不必重新发现一遍。
> 2. 每个任务结束后**编译 debug 与 release 两个 profile**（两条优化路径的警告
>    都要挡住），但**单元测试只在 release 上跑**
>    （`ctest --test-dir build/windows-release`，见第 5 节的命令行），
>    `scripts/acceptance.ps1` 同理只用 release 的产物。
>    **release 构建 + release 测试全绿才算完成**；纯文档任务同样适用。
> 3. 目标平台是 **Windows**；可以使用未公开的 Win32 API。
> 4. **代码注释、本文件、README 与示例配置一律用中文。**
>    **日志与错误信息保持英文**（配置校验信息、`--check` 输出也一样）：
>    这是从 oskeyd 继承的约定，理由是日志/校验信息是机器可断言的字符串，
>    而且中英混排的日志在终端里很难读。用户可见的 CLI 帮助文本用中文。
> 5. **可以自行结束本项目（flowkeyd）正在运行的进程**，不必事先征求许可；
>    重建被锁住的 exe、跑手工冒烟验证时先自己找 pid 再停。
>    停法照抄 oskeyd：`taskkill /PID <pid>`（**不带** `/F`）让它走干净退出路径；
>    `/F` 只在进程提权而当前 shell 杀不掉时兜底（代价是留下幽灵托盘图标）。
>    **这条只管 flowkeyd 自己；见第 9 条。**
> 6. **需要用户暂时别用这台机器时**（会抢前台/焦点、会注入按键的手工冒烟验证），
>    用提问的形式提醒，但**不等回答**（例如“接下来约 N 分钟会注入按键/抢焦点，
>    请先别碰键盘和鼠标”），约 3 秒后自行继续。提醒里要写清预计时长与影响范围。
> 7. **依赖按需引入，但每次引入都要在提交信息里给出理由**（照抄 oskeyd 的规矩）。
>    默认只有 **Qt + `vendor/lua`**，见第 3 节的依赖政策。
> 8. **agent 的 bash 是一个极简 WSL，`/bin` 下只有 `bash`/`mount`/`login`，
>    `ls`/`grep`/`cat`/`head`/`tail` 全都没有。** 看文件用 read/edit 工具，
>    跑命令一律走 `powershell.exe -NoProfile -Command "…"`（或绝对路径的外部程序，
>    例如 `C:\Users\xingjian\scoop\apps\git\current\cmd\git.exe`）。
>    **不要把管道默写成 `| tail`。命令行上也不要直接拼中文**
>    （会因代码页变乱码）：要么写成脚本文件，要么只用 ASCII 的模式串。
> 9. **接管已经完成（2026-09，第 10 阶段）：现在该常驻的是 flowkeyd。**
>    用户日常绑定的提供者已经换成 flowkeyd，oskeyd 的常驻实例不再是它，
>    所以**不要**把 oskeyd 拉起来（回归出口除外，见阶段 10）。
>    提权常驻的启动由用户自己一条
>    `Start-Process -Verb RunAs ...\flowkeyd.exe` 完成 —— agent 的 shell 没有提权，
>    也不该去点 UAC；细节见第 10 阶段与 `README.md`。
>
>    开发期起 flowkeyd 一律用 `--no-elevate --allow-multi` + 一次性配置，
>    并且**不要**占用用户真实配置里已经有的和弦（`Win+S`、`Win+1..3`、`Win+W`、
>    `Win+X`、`Win+/`、`CapsLock`、`Alt+H/J/K/L`、`Alt+Space`、`LWin+Q`、
>    `LWin+F1..F4`、小键盘 `-`/`+`/`Enter`、`Ctrl+Alt+F4/F5/F12`）——
>    用户随时可能在用它们。`scripts/acceptance.ps1` 用的是一次性配置，符合这一条。
> 10. **任何自动化测试都不得触发真实的系统电源动作。**
>     `shutdown`/`restart`/`logoff`/`sleep`/`hibernate`/`lock`/`screen_off`
>     一个都不许真的执行 —— 测试代码里不出现 `platform::win::power::execute()`
>     （`tst_power_table` 只测纯逻辑表，不碰真实调用）。这些动作只有用户自己按
>     快捷键、或点选单条目时才允许发生。细节见第 5 节与第 11 节。

---

## 1. 这个项目是什么

`flowkeyd` 是 [`oskeyd`](../oskeyd) 的 **Qt 6 / C++ 复刻版**：一个由 **Lua 脚本**
配置的 Windows 键盘钩子守护进程。它安装一个 `WH_KEYBOARD_LL` 钩子，匹配按键和弦，
按需把匹配到的按键从前台应用那里隐藏掉（也就是 AutoHotkey 的行为），然后执行绑定
的动作（启动进程、发送按键、控制音量/媒体、操作窗口、切换虚拟桌面与剪贴板、
弹出通知、弹出选单、弹出快捷键帮助、睡眠/关机/重启/关屏）。它也能做按键重映射。

一句话：**用 Lua 配置、用 Qt/C++ 写的 AutoHotkey。**

> 与 oskeyd 唯一的功能性差异是 **UI 那一层**：oskeyd 的日志查看器是一个真正的
> 命令行窗口（独立进程），选单与帮助窗口是自己用 GDI 画的原生窗口；
> flowkeyd 把它们全部换成 **Qt Quick（QML）+ FluentWinUI3 样式**的窗口，
> 而且都跑在**同一个进程**里（理由见第 4 节第 4 条）。
> 除此之外，配置 schema、动作字段、按键名、CLI 开关、日志文案都保持 1:1。

**当前的非目标**（与 oskeyd 一致）：图形化编辑器、鼠标钩子，
以及**把 Lua 函数当动作**（配置是脚本，但动作只能是声明式的表/字符串）。

### 与 oskeyd 的差异清单（有意为之，不是遗漏）

| 方面       | oskeyd                                                                 | flowkeyd                                                                                                      |
| ---------- | ---------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| 语言/框架  | Rust + 手写 FFI                                                        | C++20 + Qt 6.11（Quick/QML 做 UI）+ 手写 Win32 声明                                                           |
| Lua        | Lua 5.4（`mlua` vendored）                                             | **Lua 5.5.1**（`vendor/lua`，见第 3 节）                                                                      |
| DSL 全局表 | `oskeyd.*` + 全局构造器                                                | **`flowkeyd.*`** + 全局构造器（构造器名不变）                                                                 |
| 配置目录   | `%USERPROFILE%\.config\oskeyd\config.lua`                              | `%USERPROFILE%\.config\flowkeyd\config.lua`                                                                   |
| 日志文件   | `%USERPROFILE%\.config\oskeyd\oskeyd.log`                              | `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`                                                                 |
| 注入标记   | `dwExtraInfo` 里的 `"OSKE"`                                            | `dwExtraInfo` 里的 `"FLOW"`                                                                                   |
| 互斥体     | `Local\oskeyd-log-viewer-<pid>` 等                                     | `Local\flowkeyd-<配置路径散列>` 等（不再需要查看器互斥体）                                                    |
| 日志窗口   | `oskeyd --log-window` **独立进程**，跑在命令行窗口里                   | **进程内的 QML 窗口**（FluentWinUI3），尾随同一个日志文件                                                     |
| 选单/帮助  | 自绘 GDI 原生窗口，各自一条线程                                        | **QML 窗口**（FluentWinUI3），跑在 Qt GUI 线程上                                                              |
| 示例配置   | `oskeyd.lua.example`                                                   | `flowkeyd.lua.example`                                                                                        |
| 自动化测试 | `cargo test` + `--selftest`/`--probe`/`--simulate` + `scripts/e2e.ps1` | Qt Test 单元测试 + **`scripts/acceptance.ps1`**（77 项检查，注入按键 + 高亮/弹窗滚轮回归的外部验收；`--simulate`/`--selftest`/`--probe` 本期不做，见第 12 节） |
| 依赖管理   | `cargo`                                                                | CMake Presets + Ninja，`vendor/lua` 静态编进二进制                                                            |

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
3. **目标是全量对齐 oskeyd**，分阶段实施（第 10 节），但**自动化测试只写
   Qt Test 单元测试**（第 5 节）。这意味着桌面行为（钩子真的吞了键、窗口真的
   被激活……）只能靠**手工冒烟验证** —— 每次动到钩子/引擎/分发/窗口后端，
   都要按第 6 节的清单手工过一遍。
4. **配置 schema 与 CLI 开关 1:1 兼容 oskeyd，但换品牌名**：
   字段名、取值、动作字段、DSL 构造器名（`settings{}`/`hotkey{}`/`remap{}`/
   `run()`/`send()`/`type_text()`/`open()`/`notify()`/`volume()`/`media()`/
   `window()`/`clipboard()`/`caps_lock()`/`suspend()`/`desktop()`/`power()`/
   `menu{}`/`help()`/`reload()`/`quit()`/`none()`）**全部不变**；
   只有「注册表」那张挂在脚本全局的表从 `oskeyd` 改名成 **`flowkeyd`**，
   以及配置目录/日志文件/窗口标题/日志前缀/注入标记换成 flowkeyd。
   现有 oskeyd 的 `config.lua` 复制过来即可直接跑（本机那份根本没用到
   `oskeyd.*` 表，所以是纯复制）。
5. **flowkeyd 最终接管这台机器**：第 10 阶段会把它做成常驻（提权），
   并让 oskeyd 退役。
6. **不做 `--simulate` / `--selftest` / `--probe`，不写 oskeyd 那种 91 项检查的
   `scripts/e2e.ps1`。**
   `--check` / `--list` / `--list-keys` 保留（它们是产品功能，也是手工验证的
   主要工具）。
   → **阶段 9 补充（2026-09）**：这三个开关仍然不做，但“手工冒烟清单”已经
   自动化成了 **`scripts/acceptance.ps1`**（77 项检查），它靠一个
   **只给测试用的后门** `FLOWKEYD_ACCEPT_INJECTED=1` 抬升“丢弃注入输入”
   那道过滤（照抄 oskeyd 的 `OSKEYD_ACCEPT_INJECTED`，见第 10 节）。
   这是对一个“当时无法验证”的条款的修订，不是推翻：不变量 2 本身没动，
   日常跑的时候那道过滤照旧生效。
7. **依赖政策：默认只有 Qt + `vendor/lua`。** 需要新依赖时**按需引入**，
   但必须在提交信息里给出理由（照抄 oskeyd 的规矩）。
   `QUICK_START_DEPS`：JSON、CLI 解析、字符串工具都自己写或用 Qt 自带的；
   不要引入 `sol2`、`nlohmann::json`、`CLI11`、`spdlog` 之类“顺手”的库。
8. **构建：CMake Presets + Ninja，debug 与 release 双 profile 都必须编译通过；
   单元测试与验收脚本只跑 release**（见工作约定第 2 条）。

---

## 3. 环境与工具链

|            |                                                                                                             |
| ---------- | ----------------------------------------------------------------------------------------------------------- |
| 开发环境   | 原生 Windows 10.0 build 26200 (x64)，交互式桌面会话                                                         |
| 工程路径   | `D:\prj\flowkeyd`                                                                                           |
| 参考实现   | `D:\prj\oskeyd`（Rust，正在常驻运行）                                                                       |
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

照抄 oskeyd 的规则（见 `../oskeyd/AGENTS.md` 第 2 节末），**唯一的例外是
Qt 自己的库随便链**：

* **允许静态链接的集合**（MinGW 的工具链自带这些导入库，实测都在
  `C:\Qt\Tools\mingw1310_64\x86_64-w64-mingw32\lib` 下，
  `target_link_libraries` 里写名字即可）：
  `user32`、`kernel32`、`shell32`、`ole32`、`ntdll`、`advapi32`、
  `powrprof`、`uxtheme`、`comctl32`、`shlwapi`。
  （`comctl32`/`shlwapi`/`gdi32` 大概率用不到 —— UI 是 QML，不用手画。
  `powrprof` 比 oskeyd 那边好办：oskeyd 之所以运行时解析 `SetSuspendState`，
  只是 rust-mingw 的 self-contained 导入库里没有它，C++ 这边直接链即可。）
* **必须 `LoadLibraryW` + `GetProcAddress` 运行时解析**：
  未公开入口（`win32u!NtUserSendInput`、`NtUserGetAsyncKeyState`……，
  实测**没有** `libwin32u.a`）以及任何不在上面那个集合里的 DLL。
  理由：这些接口没有 ABI 承诺，而且少一条静态依赖就少一个“在某些机器上
  exe 根本起不来”的机会。
  **`dwmapi` 也走这条路**（虽然 `libdwmapi.a` 存在）：按窗口的
  `DWMWA_TRANSITIONS_FORCEDISABLED` 是 Win10 才有的属性，运行时解析 +
  `available()` 探测的成本几乎为零，而 oskeyd 也是这么做的。
* **COM 接口手写 vtable**（Core Audio `IAudioEndpointVolume`、
  shell 的 `IVirtualDesktopManagerInternal`）。**这是本仓库风险最高的两块代码**：
  vtable 布局写错不是返回错误码，而是崩溃。写法照抄
  `../oskeyd/src/win/audio.rs` 与 `../oskeyd/src/win/desktop.rs`
  （后者有一张按 `build.revision` 索引的接口版本表）。

> oskeyd 拒绝引入 `windows` crate 的理由在 C++ 侧不适用（我们本来就直接
> 写 Win32），但同样的精神要保留：**不引入会藏起未公开入口的封装层。**

---

## 4. 代码地图（计划中的目录结构）

第 1 阶段就把这个骨架立起来；后面每个阶段往里填。

| 路径                                      | 职责                                                                                                                                                                                                                        |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CMakeLists.txt`                          | 顶层工程、Qt 查找、`qt_add_executable`、`qt_add_qml_module`、安装/部署                                                                                                                                                      |
| `CMakePresets.json`                       | `windows-debug` / `windows-release` 两个 preset（Ninja + `mingw1310_64` + Qt 6.11.2）                                                                                                                                       |
| `cmake/VendorLua.cmake`                   | 把 `vendor/lua` 编成静态库 `lua_static`（排除 `lua.c`/`luac.c`/`onelua.c`/`ltests.c`，定义 `LUA_USE_WINDOWS`）                                                                                                              |
| `flowkeyd.lua.example`                    | 有文档、覆盖全部特性的参考配置（中文注释、无警告），`--check` 就是拿它跑的                                                                                                                                                  |
| `README.md`                               | 用户文档（中文），结构照抄 oskeyd 的 `README.md`，改掉品牌名与 UI 那两节                                                                                                                                                    |
| `src/main.cpp`                            | `AttachConsole` + CLI 分发 + 日志初始化 + 单实例 + 组装 Runtime + Qt 事件循环                                                                                                                                               |
| `src/cli.h/.cpp`                          | 参数解析 + 中文帮助文本（手写，不用 CLI11）                                                                                                                                                                                 |
| `src/core/`                               | **纯逻辑层：不碰 Win32、不碰 Qt GUI**（只用 QtCore 的类型），因此能被 Qt Test 直接测                                                                                                                                        |
| `src/core/keys.h/.cpp`                    | 键名 ↔ `VK` 表、`Modifiers`、`Chord`、AutoHotkey 发送脚本解析、小键盘 Enter 的内部伪码 `0x100`、`key_from_hook()`/`native_key()`                                                                                            |
| `src/core/config.h/.cpp`                  | 配置结构体、严格校验（未知字段要报错）、编译成 `Compiled`/`Binding`/`CompiledRemap`、配置文件搜寻与旧 TOML 的迁移提示                                                                                                       |
| `src/core/engine.h/.cpp`                  | 快捷键状态机：匹配、优先级、吞键、自动重复抑制、长按重复、挂起、重映射 hold/tap、Win/Alt 菜单遮断按键                                                                                                                       |
| `src/core/template.h/.cpp`                | `{clipboard}`、`{selection}`、`{date}` 等占位符展开                                                                                                                                                                         |
| `src/core/action.h/.cpp`                  | 声明式动作的表示 + 摘要文本（`--list` 与 `help()` 都用它）                                                                                                                                                                  |
| `src/core/log_tail.h/.cpp`                | 日志文件的增量尾随（纯逻辑，可单测）：按字节读、末尾不完整的 UTF-8 序列不消费、半行留到下一轮、一次最多 1000 行                                                                                                            |
| `src/core/window_match.h/.cpp`            | 窗口匹配与 `window` 动作决策的纯函数：标题/进程名子串、可执行文件名提取、`toggle` 边界、`animate` 是否有意义                                                                                                                |
| `src/lua/lua_config.h/.cpp`               | **Lua 与 C++ 的唯一边界**：建 `lua_State`、注入 DSL、把脚本里的表转成 `core::Config`（逐条目、带上下文的错误）、UTF-8 BOM 剔除、`.toml` 明确拒绝                                                                            |
| `src/lua/lua_prelude.lua`                 | 注入配置脚本的 DSL：`settings{}`/`hotkey{}`/`remap{}` + 动作构造器 + `flowkeyd` 表。**纯 Lua，改它不需要改 C++**（编进 qrc，见第 7 节）                                                                                     |
| `src/platform/win/`                       | Win32 后端（每个文件都只做一件事，方便单独替换）                                                                                                                                                                            |
| `src/platform/win/ffi.h/.cpp`             | 全部 Win32 声明、结构体与常量（`INPUT` 的 40 字节布局有 `static_assert` 盯着）                                                                                                                                              |
| `src/platform/win/nt.h/.cpp`              | 未公开的 `win32u.dll` 导出，运行时解析并校验                                                                                                                                                                                |
| `src/platform/win/dwm.h/.cpp`             | **运行时解析**的 `dwmapi!DwmSetWindowAttribute`：按窗口关掉过渡动画（`window` 的 `animate`）；拿不到 dwmapi 时只是保留动画，动作不失败                                                                                      |
| `src/platform/win/input.h/.cpp`           | 按键注入（`SendInput`/`NtUserSendInput`）、按键状态、`ModifierGuard`（含菜单遮断标记）、`FLOWKEYD_ACCEPT_INJECTED` 测试后门（见第 5 节与阶段 9）                                                                                |
| `src/platform/win/hook.h/.cpp`            | 钩子回调、**钩子线程自己的 Win32 消息循环**、`SetTimer`、控制消息、重载                                                                                                                                                     |
| `src/platform/win/audio.h/.cpp`           | Core Audio `IAudioEndpointVolume`，手写 COM vtable（**高风险**）                                                                                                                                                            |
| `src/platform/win/clipboard.h/.cpp`       | 剪贴板读写（`CF_UNICODETEXT`）                                                                                                                                                                                              |
| `src/platform/win/window.h/.cpp`          | 窗口查找（标题子串/可执行文件名）、激活/最小化/最大化/还原/关闭/置顶、前台锁绕行、启动回退、`TransitionGuard`（RAII 恢复动画开关）                                                                                          |
| `src/platform/win/desktop.h/.cpp`         | 虚拟桌面切换：`CLSID_ImmersiveShell` → `IServiceProvider::QueryService` → 未公开的 `IVirtualDesktopManagerInternal`，按 `build.revision` 查表                                                                               |
| `src/platform/win/power.h/.cpp`           | `powrprof!SetSuspendState`、`user32!ExitWindowsEx`、`LockWorkStation`、`WM_SYSCOMMAND`/`SC_MONITORPOWER` 广播，外加 `SeShutdownPrivilege`                                                                                   |
| `src/platform/win/tray.h/.cpp`            | 托盘图标 + 气泡提示 + 右键菜单（查看日志/挂起/重载/打开配置/退出）+ 悬停提示                                                                                                                                                |
| `src/platform/win/logging.h/.cpp`         | 控制台/文件日志器（英文、分级别、可选 ANSI 颜色），`--log-level`/`--log-file`/`--no-color`                                                                                                                                  |
| `src/platform/win/single_instance.h/.cpp` | 按配置路径散列命名的互斥体，含提权重启后的重试                                                                                                                                                                              |
| `src/platform/win/elevate.h/.cpp`         | `ShellExecuteW("runas")` 自提权 + UAC 被拒时降级继续 + `--elevated` 标记 + 命令行/工作目录转发（`quote_arg`）                                                                                                               |
| `src/app/`                                | 组装层：把 core / lua / platform 串起来，并拥有 Qt 对象                                                                                                                                                                     |
| `src/app/dispatcher.h/.cpp`               | **动作工作线程**（`QThread`）：执行动作列表，含 `window` 的“先启动再激活”与默认开的 `toggle` 收起、`menu`/`help` 的窗口请求                                                                                                 |
| `src/app/runtime.h/.cpp`                  | 引擎 + 钩子 + 分发 + 托盘 + 弹窗的总装，`ControlCmd`（suspend/reload/quit）通道                                                                                                                                             |
| `src/app/log_model.h/.cpp`                | 日志窗口的模型：尾随日志文件（增量、半行、被截断的多字节 UTF-8）、最多 1000 行、按级别配色、子串过滤                                                                                                                        |
| `src/app/menu_model.h/.cpp`               | `menu` 选单的**纯逻辑**（`QAbstractListModel`，只用 QtCore）：条目几何、高亮移动（到边界回绕）、单字符选中、`Esc`/`Enter` 语义、命中测试（**可单测**） |
| `src/app/help_model.h/.cpp`               | `help` 帮助的**纯逻辑**（同上）：筛选（和弦/`comment`/`name`/动作摘要）、滚动钳位、`可见/总数` 计数、滚动条几何、`Enter` 复制哪一行、两级 `Esc`、滚轮（**可单测**） |
| `src/app/popup_layout.h/.cpp`             | 两个弹窗共用的几何类型（`PopupRect`/`PopupPoint`）与纯函数 `centrePopup()`（先在工作区居中、再夹进屏幕；**可单测**） |
| `src/app/popup_host.h/.cpp`               | 把上面的模型挂到 QML 窗口上；抢前台（`requestActivate` + `win::window::raiseWindow` 的前台锁绕行）；在 Qt GUI 线程上创建/复用窗口；用户选完把活儿回投工作线程（**GUI 线程亲和**） |
| `src/qml/`                                | `LogWindow.qml`、`MenuPopup.qml`、`HelpPopup.qml`（三个文件都在开头写了 `pragma ComponentBehavior: Bound`）；配色一律用 `palette`，没有单独的 `Style.qml` |
| `tests/`                                  | Qt Test：`tst_keys`、`tst_engine`、`tst_config`、`tst_lua`、`tst_template`、`tst_send_script`、`tst_window_match`、`tst_log_tail`、`tst_audio`、`tst_interactive`（需 `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`，否则 skip）、`tst_menu_model`、`tst_help_model`、`tst_power_table`、`tst_desktop_table`、`tst_layout` |
| `scripts/acceptance.ps1`                  | 桌面行为的验收脚本（注入按键 + 焦点捕捉窗口的外部观察，77 项检查）；需交互式桌面，**不属于 `ctest`**，见第 5 节与阶段 9 |

### CMake 目标划分（阶段 6 之后）

| 目标               | 内容                                                              | 谁链接                    |
| ------------------ | ----------------------------------------------------------------- | ------------------------- |
| `flowkeyd_core`    | `src/core/*`（纯逻辑，只用 QtCore）                                | exe + 全部单测            |
| `flowkeyd_lua`     | `src/lua/*` + 编成 qrc 的 `lua_prelude.lua`                        | exe + `tst_lua`           |
| `flowkeyd_models`  | `src/app/{menu,help}_model.*` + `src/app/popup_layout.*`（纯逻辑，只用 QtCore） | exe + `tst_menu_model`/`tst_help_model` |
| `flowkeyd_platform`| `src/platform/win/*`（不碰 Qt GUI的 Win32 后端）                    | exe + 平台层单测           |
| `flowkeyd`         | `src/main.cpp`、`src/cli.*`、`src/app/*`、`src/platform/win/tray.*`、QML | ——                        |

> `src/app/popup_host.*` 用 QML/QtQuick，所以**不进** `flowkeyd_models`，留在 exe 里；
> 模型层只有 QtCore，这样 `tst_menu_model`/`tst_help_model` 能在没有桌面的情况下跑。

`flowkeyd_add_test(name [LIBS …])` 负责把 Qt/MinGW 的 DLL 目录写进 test 的 `PATH`。

**exe 自己的产物目录是自包含的**：`flowkeyd` 上挂了一条 `POST_BUILD` 的
`windeployqt`（`Qt6::windeployqt`），它把 Qt 与 MinGW 运行时的 DLL + exe 用到的
QML 模块（`--qmldir src/qml`）拷到 exe 同目录，所以双击
`build/windows-release/flowkeyd.exe` 就能启动，不必手动改 `PATH`。
只对 `flowkeyd` 做，**不给测试可执行文件做**（那 19 个 `tst_*` 靠 ctest 注入 `PATH`，
而且每个都部署一次会让构建慢得多）。大小参考：release 目录里 Qt 侧大约 120 MB
（含 19.7 MB 的 `opengl32sw.dll`，刻意保留 —— 不想为了省 20 MB 去赌软件回退）。

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
// 要点：Ninja + GCC 13.1 + Qt 6.11.2，两条 profile，产物分别在 build/debug、build/release
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
    { "name": "windows-debug",   "inherits": "windows", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "windows-release", "inherits": "windows", "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" } }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "windows-debug" },
    { "name": "release", "configurePreset": "windows-release" }
  ],
  "testPresets": [
    { "name": "debug",   "configurePreset": "windows-debug",   "output": { "outputOnFailure": true } },
    { "name": "release", "configurePreset": "windows-release", "output": { "outputOnFailure": true } }
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

# 单元测试：只跑 release 那一份（见工作约定第 2 条）
& ctest --test-dir build/windows-release --output-on-failure
```

**debug 与 release 两个 profile 都必须编译通过，这是每个任务（包括纯文档任务）
的硬性要求。** 理由：release 走的是完全不同的优化与链接路径
（`-O2` + LTO 若开启），只编译 debug 会漏掉只在一侧出现的警告；反过来，
debug 构建也是发现未初始化变量、迭代器失效这类问题的便宜手段。

**但测试只在 release 上跑**（项目所有者 2026-09 拍板）：`ctest --test-dir
build/windows-release` 全绿就够了，不需要再跑 `build/windows-debug` 那一遍；
`scripts/acceptance.ps1` 同理只跑 `-Exe build\windows-release\flowkeyd.exe`
（脚本的 `-Exe` 默认值就是它）。**零警告、零失败。**

因为 Qt 项目是编译型 + 链接型，**改动 `vendor/lua` 的构建参数、Win32 声明、
QML 模块注册之后，两条 profile 都要重新全量构建一次**。

### 运行（开发期一定要带这两个开关）

```powershell
# 手工冒烟：不抢用户的 oskeyd，用一次性配置，见工作约定第 9 条
& build/windows-debug/flowkeyd.exe --no-elevate --allow-multi --console --config .\tmp\smoke.lua
```

* `--no-elevate`：`--check` 之类离线命令本来就不提权，但**守护进程模式**
  默认会提权（`ShellExecuteW("runas")`）。开发期一律关掉，免得每次弹 UAC。
* `--allow-multi`：跳过单实例检查，方便同时开好几个试验实例
  （注意：每个实例都会装一个 `WH_KEYBOARD_LL` 钩子，**后装的先收到事件**，
  所以调试时只开一个真正需要吞键的实例）。
* `--console`：保留控制台输出（见第 7 节的 `AttachConsole` 那一条）。

**离线命令（绝不允许提权）**：`--check` / `--list` / `--list-keys`。
提权判断必须在这些命令 `return` 之后。

> 从 2026-09 起，构建目录里就已经有 Qt 与 MinGW 的运行时 DLL（构建后自动跑
> `windeployqt`，见第 4 节末），所以上面这些命令**不再需要手动把 Qt 的 `bin`
> 加进 `PATH`**；直接 `build/windows-release/flowkeyd.exe --check` 就行。
> 双击 `flowkeyd.exe` 也能启动（不带参数 = 守护进程 + 默认配置 + 弹一次 UAC）。

### 桌面行为怎么验证（每次动到钩子/引擎/分发/窗口后端都要过一遍）

清单在下面（12 条），**从阶段 9 起有了自动化版本**：

```powershell
# 77 项检查，约两分钟，会持续注入按键/抢焦点；按工作约定第 6 条先提醒用户
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
那是**只给测试用的后门**（与 oskeyd 的 `OSKEYD_ACCEPT_INJECTED` 同款；
`core/engine.cpp` 一直会丢弃 `event.injected`，改的是 `hook.cpp` 里给它赋值
那一步），启用时日志里有一条警告。flowkeyd 自己注入的按键带着 `"FLOW"` 标记，
在钩子回调的第一步就被丢掉，所以抬升过滤不会让重映射自己喂自己 ——
**不变量 2 没有被放宽**。

脚本**不做**的：动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程、
“托盘图标真的消失了”的直接观察，以及**一切电源动作**（工作约定第 10 条：
测试里不许真的关机/重启/注销/睡眠/休眠/锁定/关屏）。这几项仍然只能靠人的手。

用一份只含被测绑定的**一次性配置**（快捷键一律避开用户真实配置里已有的和弦）：

1. `--check --config tmp/smoke.lua` 通过，`--list` 打印的形状与 oskeyd 一致。
2. 单实例：开两个不用 `--allow-multi` 的实例，第二个必须拒绝启动。
3. 吞键：临时用 PowerShell 起一个能显示收到按键的窗口（记事本即可），
   按被测和弦，**前台窗口不应该收到那个键**；用一个未绑定的键做正对照
   （正对照收不到就说明焦点没拿到，本次验证无效 —— 别把“焦点压根不在”
   当成“吞键成功”）。
4. 被吞掉的 `Win+…` 和弦：按完之后 Windows **不能**弹出开始菜单/搜索；
   按住 Windows 键超过自动重复延迟再松开，**仍然不能**弹（这是遮断标记
   必须挂在修饰键 key-up 上的原因，见 `../oskeyd/AGENTS.md` 第 6 节）。
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

三线程的分工与 oskeyd 完全一致，理由也一样：

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
  “队列已就绪”通过一个 `std::promise`/原子量告诉启动方——照
  `../oskeyd/src/win/hook.rs` 的做法。
* 跨线程共享的配置：`Compiled` 用 `std::shared_ptr<const Compiled>` 交给
  钩子线程与工作线程各持一份，**reload 时整体替换指针**，绝不做“就地改”。

---

## 7. 不变量 —— 不要破坏这些

前 6 条是从 oskeyd 直接继承的（那边有血泪史，见 `../oskeyd/AGENTS.md` 第 4/6 节）：

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
   （用 `name` 或序号）和出错的值，格式与 oskeyd 一致：
   `hotkey #3 (\`terminal\`): unknown field \`nope\`, expected one of \`name\`, \`keys\`, ...`。
   **未知字段一律报错**（C++ 侧要自己实现严格白名单 —— 这是本项目最容易做漏的
   一条，因为 C++ 没有 serde 的 `deny_unknown_fields`）。
   **动作表顶层是唯一的例外**：oskeyd 那边（serde 的内部标签枚举 + `untagged`）
   也做不到，所以 `window("activate", { togle = false })` 里的拼写错误两边都
   会被忽略——保持一致，但要在 README 的已知限制里写明。
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
    见 `../oskeyd/AGENTS.md` 第 6 节那条长坑：`ModifierGuard` 注入的
    真实 key-up 引擎看不到，必须在注入层自己插一次 `VK_UNASSIGNED` 空按键。
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
    **每条路径上 `AttachThreadInput` 都要配平**（照抄
    `../oskeyd/src/win/window.rs::raise_window`）。
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
    （oskeyd 做成独立进程是因为**它用的是真控制台**，用户点叉会给守护进程发
    `CTRL_CLOSE_EVENT`；Qt 窗口没有这个问题，所以这条有意偏离 oskeyd。）

---

## 8. Lua 5.5.1 与 oskeyd 的 Lua 5.4 的差异（已实测）

`vendor/lua` 是 **v5.5.1**（`lua/lua`，commit `7579fc9d`，`describe` 出来就是
`v5.5.1`）。配置 DSL 只用到最基础的表/字符串/数字/循环，所以差异不大，
但下面这几条**已经确认**、集成时会直接撞上：

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
   * `repeat` 仍然是 Lua 关键字 → 字段名用 `repeatable`（与 oskeyd 的
     `alias = "repeat"` 规矩一致）。
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
| 6 弹窗 `menu` / `help` | **已完成** | `flowkeyd_models` + 两张 QML 卡片；`tst_menu_model`/`tst_help_model` 全绿；渲染/筛选/键盘选择由 `tmp/preview` 验证 |
| 7 虚拟桌面 + 电源 | **已完成** | `platform/win/desktop|power` + dispatcher 接线；`tst_desktop_table`/`tst_power_table` 全绿；真实 COM 探测/切换与关屏由 `tst_interactive` 验证 |
| 8 示例配置 + README | **已完成** | `flowkeyd.lua.example` 与 oskeyd 逐行对齐（除 UI/probe/simulate 那几处）；`README.md` 已写出；`--check` 37 hotkey / 3 remap |
| 9 验收（无 e2e 的替代） | **已完成** | `scripts/acceptance.ps1`（77 项检查：68 项原样 + 9 项弹窗滚轮回归，需交互式桌面）+ `FLOWKEYD_ACCEPT_INJECTED` 测试后门；debug 跑 3 遍、release 跑 2 遍全绿 |
| 10 接管 | **已完成（待用户点一次 UAC）** | 真实配置已迁到 `.config\flowkeyd\config.lua`（24 hotkey / 0 remap，零警告）；oskeyd 本来就没在跑；常驻启动由用户手动 `Start-Process -Verb RunAs` |

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
逐项对齐 oskeyd 的同名模块，包括那些非显然的细节：

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

**验收**：Qt Test 覆盖以上每一条（对齐
`../oskeyd/src/keys.rs`、`engine.rs`、`template.rs`、`config.rs` 里那些测试的
**测试点**，不必逐字照抄断言）。双构建绿。

**状态：已完成（2026-09）。**

**已完成的内容**：

* `src/core/keys.{h,cpp}`、`action.{h,cpp}`、`config.{h,cpp}`、`engine.{h,cpp}`、
  `template.{h,cpp}`。全部只用 QtCore，**没有** `<windows.h>`、**没有** QML/Widgets。
* `Action` 用「一个结构体 + `Kind` 判别式」而不是 `std::variant`：
  `menu` 的递归（Menu → MenuItemDef → ActionSpec → Action）天然成立，
  也省掉了 20 个包装结构体与到处 `std::visit` 的噪音。
  各字段与 oskeyd 的同名变体一一对应。
* `ActionSpec` / `Settings` / `HotkeyDef` / `RemapDef` / `Compiled` / `ConfigError`
  都在 `core/config.h`；动作的**摘要文本**（`--list` 与 `help()` 的显示）在
  `core/action.h`。
* 校验与 oskeyd 同源：未知 `--check` 之外的字段由**阶段 2 的 Lua 层**逐字段核对
  白名单（C++ 没有 `deny_unknown_fields`，见不变量 6），动作表顶层与 oskeyd 一样
  是例外。
* 这一阶段**不经过 Lua**：`loadConfig(Evaluator, path, out)` 把求值回调当参数，
  阶段 2 只要传一个真的 Lua 求值器进来即可；BOM 剥离、`.toml` 拒绝、
  候选路径与 `pickConfigPath()` 已经在本阶段实现并有单测（用一个假求值器）。
* 测试：`tests/tst_smoke|keys|send_script|config|engine|template.cpp`，
  共 6 个 Qt Test 可执行文件；配置直接在 C++ 里搭（`tests/helpers.h`）。
* **`tst_layout` 推迟到阶段 3**：它要盯的是 `platform/win/ffi.h` 里
  `static_assert(sizeof(INPUT) == 40)`，本阶段还没有那个文件。
* 路径一律用 `QDir::toNativeSeparators()` 输出，错误信息里看到的是
  `C:\Users\…\.config\flowkeyd\config.lua`（与 oskeyd 的 `PathBuf` 显示一致）。

### 阶段 2：Lua 配置层

**做什么**：`src/lua/lua_config` + `src/lua/lua_prelude.lua`（编进 qrc）。
把阶段 1 的 `core::Config` 接到真正的 Lua 求值上：

* 建状态 → 注入 prelude（把注册表 `state` 作为 `...` 传给 prelude）→
  求值配置文件 → 把 `settings`/`hotkeys`/`remaps` 三张表逐条目转成 C++ 结构
  → 收集错误（`hotkey #3 (\`terminal\`): ...`，一次性列全）→ 关状态。
* **逐条目转，不要一次 `deserialize_any` 整张表**（oskeyd 也是逐条目的，
  这样错误才能带上下文）。
* 空表歧义：`args = {}` 要当空列表，`env = {}` 要当空 map 并给出人话提示
  （Lua 里 `{}` 既是空列表也是空表）。
* `ActionSpec::List([])`（空的动作列表）**必须显式报错**：
  `an empty action list does nothing`。
* 尾部调用的行号坑：DSL 报错的那次调用如果是脚本的最后一条语句，Lua 会丢掉
  调用者栈帧、traceback 里没有行号。**别为了行号改结构**（oskeyd 也接受了）。
* `--check` / `--list` / `--list-keys` 三个离线命令；
  `.toml` 后缀要**明确拒绝**并给迁移提示（照抄 oskeyd 的 `ConfigError::LegacyToml`
  文案，把 oskeyd 换成 flowkeyd）。
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
  `--list` 的排版与 oskeyd 的 `print_bindings` 逐字形似（`tst_lua` 之外也可肉眼比对）。
* `flowkeyd.lua.example`：由 oskeyd 的示例改写（品牌名 + UI 相关注释），
  37 hotkey / 3 remap，`--check` 通过、零警告。
* 测试：`tests/tst_lua.cpp`，25 个用例，覆盖 oskeyd `src/lua.rs` 的全部测试点
  （两种写法一致、循环生成、构造器等价于手写表、错误信息带条目名/行号、
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
  `CREATE_NO_WINDOW`；见 `../oskeyd/AGENTS.md` 第 6 节）。

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
* `logging.{h,cpp}`：`HH:MM:SS LEVEL message`（与 oskeyd 同形），ANSI 颜色可选，
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

**验收**：单测（两个模型的全部分支）；手工：键盘/鼠标都能选、
`Esc` 只关窗不选、帮助窗口的筛选让窗口变矮、`Enter` 真的复制到剪贴板、
在别的应用聚焦时按快捷键也能拿到键盘焦点。

**已完成的内容**

* 新静态库 **`flowkeyd_models`**：`src/app/menu_model.{h,cpp}`、
  `src/app/help_model.{h,cpp}`、`src/app/popup_layout.{h,cpp}`。三者都只用 QtCore，
  所以 `tst_menu_model`/`tst_help_model` 不需要桌面就能跑。
* 两个模型都是 `QAbstractListModel`，角色直接给 QML 用：
  `menu` 是 `label`/`hint`/`keyText`/`highlighted`/`hovered` + 四个 `QRect`；
  `help` 是 `badges`/`label`/`detail`/`highlighted`/`hovered`/`line` + 三个 `QRect`。
  **几何算术全部在模型里**，QML 只把模型算出来的矩形画出来。
* 模型把「按键怎么解释」也包了：`handleKey(key, text)` 返回
  `{ decision: none|choose|copy|cancel, index, handled }`；QML 的
  `Keys.onPressed` 只负责“问模型要决定 → 执行决定”。字符来自 Qt 译好的
  `QKeyEvent::text()`（等价于 oskeyd 的 `WM_CHAR`），所以
  `VK_UNASSIGNED`(0xE8) 那条菜单遮断注入不会凭空变成筛选框里的一个字母。
* `qml/MenuPopup.qml`、`qml/HelpPopup.qml`：无边框圆角卡片（
  `Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint`，
  `color: "transparent"`），配色一律走 `palette`（于是自动跟随系统浅色/深色）；
  帮助窗口的筛选框**不是 `TextField`**，而是自己画的一行字 + 一根光标——
  所有按键都走一个 `Keys.onPressed`，`↑`/`↓`/`Enter`/`Esc` 与输入不会互相抢键。
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
  与 oskeyd 一样方便从外面断言筛选生效了；`help` 没写 `title` 时表头默认
  是「快捷键」（oskeyd 的 `unwrap_or("快捷键")`）。
* 测试：`tests/tst_menu_model.cpp`（13 个用例）、`tests/tst_help_model.cpp`
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
  每个 HRESULT 都检查。**表的内容照抄 `../oskeyd/src/win/desktop.rs`。**
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
  `versionTable()` 是照抄 `../oskeyd/src/win/desktop.rs` 的七条版本表，
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
  直接静态链接 `powrprof`（AGENTS.md 第 2 节允许），不像 oskeyd 那样运行时解析。
* `src/platform/win/ffi.{h,cpp}`：新增 `hresultText()`（与 oskeyd 的
  `hresult_text` 同源的人话说明：`E_NOINTERFACE` 意味着版本表选错了 IID），
  `hresultMessage()` 改用它。
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

**做什么**：把 oskeyd 的 `oskeyd.lua.example` 译成 `flowkeyd.lua.example`
（品牌名替换 + UI 相关注释改写），把 `README.md` 从 oskeyd 的版本翻译过来
（改掉品牌名、日志窗口那一节、选单/帮助那一节、验证那一节）。
`--check --config flowkeyd.lua.example` 必须通过。

**验收**：手工通读示例配置，确认每条注释与实现一致；双构建 + 测试绿。

**状态：已完成（2026-09）。**

**已完成的内容**

* `flowkeyd.lua.example` 在阶段 2 就已经写出；本阶段用
  `git diff --no-index`（把 oskeyd 那份做品牌名替换、再删掉 `--simulate` 行）
  逐行核对过：除了有意为之的四处（头部多一行 `--list-keys`、虚拟桌面那节
  去掉 `--probe`、选单/帮助那节把“自绘 GDI 原生窗口”改成 QML/FluentWinUI3、
  help 那节去掉 `--simulate` 提示），其余完全一致。
* `README.md`（新增，约 700 行）从 oskeyd 的 README 翻译并改写：
  品牌名、Lua 5.5.1、CMake preset 构建、去掉 `--simulate`/`--selftest`/`--probe`、
  **日志窗口**改成“进程内 QML 窗口”、**选单/帮助**改成“QML + 跟随系统 palette”、
  **验证那一节**改成“Qt Test + 手工冒烟清单”（并写明没有 e2e）、
  工作原理图改成三线程 + Qt GUI 线程、已知限制与路线图按本项目重写。
  文中所有 `--check`/`--list`/`--version` 的输出形状都用真实运行结果核对过。

**实测结果（2026-09）**

* `flowkeyd --version` → `flowkeyd 0.1.0` / `Lua 5.5.1`（版本号里带 Lua，出问题时
  能一眼看出是哪一份 Lua）。
* `flowkeyd --check --config flowkeyd.lua.example` →
  `D:\prj\flowkeyd\flowkeyd.lua.example: OK (37 hotkey(s), 3 remap(s))`，零警告。
* `flowkeyd --list` 的形状与 oskeyd 的 `print_bindings` 逐字形似。

### 阶段 9：验收（无 e2e 的替代）

**做什么**：把第 5 节的手工冒烟清单**完整跑两遍**（debug 与 release 各一遍），
并逐条记录结果到本文件。
特别要覆盖：被吞掉的 `Win+S` 从四个角度（常规、0 ms 轻按、
1–2 次模拟 Windows 键自动重复）；小键盘的 `-`/`+`/`Enter` 与主键盘
`-`/`=`/`Enter` **互不触发**（注入小键盘 Enter 必须带
`KEYEVENTF_EXTENDEDKEY`，否则测的就是主键盘的 Enter）；重映射的
hold/tap；挂起/重载/退出。

> **修订（2026-09）**：DoD 现在只在 **release** 上跑 `ctest` 与
> `acceptance.ps1`（工作约定第 2 条），下面的“debug 与 release 各一遍”
> 只是当年的记录；另外测试里不再执行任何真实电源动作（第 10 条）。

**状态：已完成（2026-09）。**

**已完成的内容**

* **`FLOWKEYD_ACCEPT_INJECTED` 测试后门**：`platform/win/input.{h,cpp}` 新增
  `acceptInjectedInput()`（读环境变量，进程内只缓存一次），`hook.cpp` 里改成
  `event.injected = (info->flags & LLKHF_INJECTED) != 0 && !acceptInjectedInput()`，
  并在装完钩子之后打一条警告。oskeyd 的同款后门是 `OSKEYD_ACCEPT_INJECTED`。
  这是能自动验证“真的吞了键”的前提：钩子不认注入输入，脚本就伪造不了物理按键，
  而 `--simulate` 本期不做（第 12 节）。**不变量 2 没被放宽**：flowkeyd 自己
  注入的事件带着 `"FLOW"` 标记，仍然在钩子回调第一步就被丢掉。
* **`scripts/acceptance.ps1`**（新文件，当时 68 项检查：一次性配置 + 获得焦点的
  WinForms 捕捉窗口 + `SendInput` 注入 + 剪贴板/窗口/日志当外部证据。
  覆盖第 5 节清单的 1–12 条，另外还多做了：小键盘与主键盘互不触发（6 项）、
  重映射 hold/tap（不只是 CapsLock）、`menu`/`help` 弹窗的键盘选择与筛选、
  以及“重新打开的选单也要重新拿到焦点”。**后续又加了 9 项弹窗滚轮回归，
  现在是 77 项**（见第 10 节的“弹窗在滚轮下闪烁”）。
* 脚本的检查名用中文字面量（oskeyd 的 e2e 也是这个风格），所以文件必须以
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
> 现在是 **77 项**（`checks: 77, failures: 0`，已在 release 与 debug 上跑过）。

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
  `SlideToShutDownHost`（已换成 oskeyd 用的 `Win+S`）。
* 不过它**真的**查出了一个产品 bug：`menu` 第二次打开（复用同一个 QML 窗口）
  时拿不到键盘焦点 —— 见第 10 节的 `activateWindow` 那一条，已修并有检查盯着。

### 阶段 10：接管（品牌迁移 + 常驻）

**做什么**

1. 把 `C:\Users\xingjian\.config\oskeyd\config.lua` 译到
   `%USERPROFILE%\.config\flowkeyd\config.lua`（本机那份**没有**用到
   `oskeyd.*` 表，所以基本是整份复制 + 改注释里的命令名），
   逐条核对本机真实绑定：`CapsLock`→Ctrl+Space、`Alt+H/J/K/L`、
   `Alt+Space`→F14、`LWin+Q`→F24、`LWin+F1..F4`→虚拟桌面 1..4、
   `Win+S`→WezTerm、`Win+1/2/3`→Chrome/VS Code/WPS、`Win+W`→微信、
   `Win+X`→电源选单、`Win+/`→快捷键帮助、小键盘 `-`/`+`/`Enter`→音量、
   `Ctrl+Alt+F4/F5/F12`→quit/reload/suspend。
2. 按工作约定第 6 条提醒用户后，停掉 oskeyd 的常驻实例
   （`taskkill /PID <pid>`，不带 `/F`；提权实例见
   `../oskeyd/AGENTS.md` 第 5 节的三条出路），
   把 `target\release\oskeyd.exe` 的 RUNASADMIN 兼容性标记与登录自启动项
   一并记录清楚（不要静默删掉用户的东西 —— 要么备份、要么在总结里说明）。
3. 用 release 版 flowkeyd（提权）接管常驻，逐条手工验证上面那批绑定。
4. 在本文件与 `README.md` 里写清“现在常驻的是 flowkeyd”。
5. **回归出口**：如果 flowkeyd 有问题，把 oskeyd 拉回来
   （`Start-Process -Verb RunAs target\release\oskeyd.exe`）。

**状态：已完成到“等用户点一次 UAC”为止（2026-09）。**

**已完成的内容**

1. 配置已迁到 `%USERPROFILE%\.config\flowkeyd\config.lua`：绑定与 oskeyd 那份
   **逐条一致**（两边读的是同一套 schema），改的只有注释里的程序名与 UI 说法
   （`oskeyd --probe` 那行删掉了，flowkeyd 没有 `--probe`）。
   `flowkeyd --check`（**不带** `--config`，走默认搜寻）→
   `C:\Users\xingjian\.config\flowkeyd\config.lua: OK (24 hotkey(s), 0 remap(s))`，
   零警告；`--list` 的 24 条与第 14 节那张表逐条对应。
2. oskeyd **没有常驻实例可停**：机器 6:24 重启后它没被拉起来（它没有登录自启项，
   只有 `D:\prj\oskeyd\target\release\oskeyd.exe` 上的 `RUNASADMIN` 兼容性标记）。
   那个标记**没有动过**，所以回滚照样是一条 `Start-Process -Verb RunAs`。
3. 非提权预演（真的读用户那份配置，**不注入任何按键**，所以没触发任何真实动作）：
   日志里 `24 hotkey(s), 0 remap(s), tick 15 ms` + `keyboard hook installed`；
   `taskkill /PID <pid>`（**不带** `/F`）能干净退出（日志末尾
   `keyboard hook removed`）。
4. `README.md` 新增「本机现在常驻的是 flowkeyd」一节：启动命令、干净退出、
   回滚到 oskeyd、为什么没加自启。
5. **没有加开机自启**（用户拍板），也**没有**给 exe 登记 `RUNASADMIN` 兼容性
   标记：那个标记会让 `flowkeyd --check` 之类离线命令也弹 UAC（oskeyd 现在
   就有这个毛病，离线命令本不该弹 UAC —— 不变量 11）。

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
小键盘 `-`/`+`/`Enter`、`Ctrl+Alt+F4/F5/F12`。
`Ctrl+Alt+F12`（挂起）与 `Ctrl+Alt+F4`（退出）是安全的自检项；
**`Win+X` 选单里千万别按到睡眠/关机/重启**。
回滚：`Start-Process -Verb RunAs -FilePath 'D:\prj\oskeyd\target\release\oskeyd.exe'`。

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
  （这正是 oskeyd 把日志查看器做成独立进程的那类问题的 Qt 版本。）
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
  不要再手动调 `SetProcessDpiAwarenessContext`（oskeyd 需要，
  因为它自己建原生窗口）。**不要在 `QApplication` 之前创建窗口**；
  要改缩放取整策略就 `QGuiApplication::setHighDpiScaleFactorRoundingPolicy()`
  在它之前调。
* **Qt 会自动给我们一个有 COM 的线程**（主线程是 STA，为了 OLE/拖放）。
  虚拟桌面那条路要 STA、音频要 MTA，**别把音频初始化放到主线程**
  （否则会把主线程的单元模型定死成 MTA，Qt 的拖放/剪贴板可能出问题）。
  照 oskeyd 的做法：桌面调用走一次性 STA 线程，音频在它自己的一次性 MTA 线程上。
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
* **`QDir::filePath()` 一律用 `/` 当分隔符**，与 oskeyd 的 `PathBuf` 显示不同。
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
  （工作区从 x=108 开始、宽 485，而屏幕只有 533 宽）。照 oskeyd 那样
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
* **核对“示例配置有没有漏东西”的好办法：把参考文件做品牌名替换后
  `git diff --no-index`。** 把 `oskeyd.lua.example` 读成字符串、`-replace`
  掉品牌名、删掉 `--simulate` 行、写成临时文件，再与 `flowkeyd.lua.example`
  diff；本机实测两者只差有意为之的四处（见阶段 8）。注意 git 会自动处理
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
  解法是照抄 oskeyd：`FLOWKEYD_ACCEPT_INJECTED=1`（oskeyd 叫
  `OSKEYD_ACCEPT_INJECTED`）抬升那道过滤，而且**只改钩子给 `event.injected`
  赋值的那一步**，引擎和其余不变量一概不动；启用时打一条警告。
  这样 `scripts/acceptance.ps1` 才能从外部观察到“键真的是被吞了”。
* **`Start-Process -PassThru` 拿不到退出码。** 无论加不加
  `-RedirectStandardOutput`，`$p.WaitForExit(6000)` 之后 `$p.ExitCode` 都是空
  （只有用 `-Wait` 启动的那种才会填）。oskeyd 的 e2e 也不看守护进程的退出码，
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
  看起来就像遮断失效。改用 oskeyd 的 `Win+S`（失败代价只是弹个搜索框）。
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

* **现象**：鼠标滚轮滚弹窗时高亮“闪一下”——上下箭头完全正常，只有滚轮会。
* **根因**：滚轮走的是 `moveSelection()`，而它为了“键盘接管高亮”会把鼠标悬停
  清掉（`m_hover = -1`，与 oskeyd 的 `State::move_selection` 一致）。于是
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
* **`MenuPopup.qml` 的滚轮**：选单不滚动（oskeyd 也没处理 `WM_MOUSEWHEEL`），
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
    `FLOWKEYD_ACCEPT_INJECTED=1`）比用预览程序更接近用户现场。* **`QCOMPARE(optional<int>, -1)` 是错的**：`-1` 会被隐式构造成
  `std::optional<int>{-1}`（engaged），而“没有悬停”是 `std::nullopt`（disengaged），
  于是断言总是失败。写 `QCOMPARE(model.hover(), std::nullopt)`。
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
  但 `ctest`/`acceptance.ps1` 的 DoD 仍然必须在 release 上跑完。

### 从 oskeyd 继承的领域坑（照抄那份的解法，不要重新发明）

下面这些在 `../oskeyd/AGENTS.md` 第 6 节都有**完整的现象描述 + 修法**，
这里只列标题，动手前**务必去读那一条**：

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
* `SetForegroundWindow` 除非持有前台锁否则被拒（递进式绕行 +
  `AttachThreadInput` 配平）。
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

---

## 11. 完成定义（DoD）细则

一个任务算完成，必须同时满足：

1. `cmake --build --preset debug` 与 `cmake --build --preset release` **都绿**
   （零新增警告；warning 当错误处理，直到项目所有者另有要求）。
   **两条 profile 都要构建，但只在 release 上跑测试**（见下一条）。
2. `ctest --test-dir build/windows-release --output-on-failure` **全绿**
   （不再跑 `build/windows-debug` 那一遍，见工作约定第 2 条）；
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

> **阶段 0/1 的实测结果（2026-09-20）**：`windows-debug` 与 `windows-release`
> 两个 profile 都是 `build exit 0`、零警告（`-Wall -Wextra -Werror`），
> 6 个测试目标在两边都是 `100% tests passed`。
> 跑 `ctest` 时请用第 5 节的命令行（`ctest --test-dir build/windows-release`；
> 当年两个 profile 都跑，现在只跑 release，见工作约定第 2 条）；
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
> `--check`/`--list` 形状与 oskeyd 一致（`menu "冒烟选单" (3 item(s))`、`help`），
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
> `--version` 打印 `flowkeyd 0.1.0` + `Lua 5.5.1`；`--list` 的形状与 oskeyd 一致。
> 交互式验证（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`，另加
> `FLOWKEYD_ALLOW_SCREEN_OFF=1`）8 个用例全绿（其中的
> `powerScreenOffBlanksTheDisplay` 后来已删除：电源动作不再进测试）：虚拟桌面探测到
> `count 4 / current 2 / os 26200.9457 / api 26100 / layout plain /
> manager {53f5ca0b-158f-4124-900c-057158060b27}`，切走再切回成功；
> `screen_off` 真的黑屏并被随后注入的 Shift 点亮。
> `README.md` 与 `flowkeyd.lua.example` 已按 oskeyd 逐节核对。
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
> 也能被 `taskkill /PID`（不带 `/F`）干净停掉；oskeyd 本来就没在跑，
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

> 提醒：Qt 的编译单元很多，`--preset` 的构建目录是分开的
> （`build/windows-debug` / `build/windows-release`），所以
> **debug 实例在运行不会锁住 release 产物**，反之亦然。
> 这比 oskeyd 那边的体验好，但仍然要记住：正在运行的
> `flowkeyd.exe` 会锁住它自己那个 profile 的产物。

---

## 12. 本期不做的（有意留白）与后续工作

按项目所有者的决定，**本期不做**下面这些；它们是明确的待办，不是遗忘：

1. **`--simulate <SCRIPT>`**（把脚本化按键事件重放给真正的引擎，干跑）。
   这是 oskeyd 里最便宜的引擎验证手段，**强烈建议尽早补**：
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
   如果以后要补，照 oskeyd 的脚本改造：它的断言字符串
   （英文日志、窗口标题格式）在本项目里保持兼容，正是为了这个。
4. **鼠标钩子**（`WH_MOUSE_LL`）：oskeyd 也没做。
5. **延迟修饰键抑制**：让 `Ctrl+Alt+H` 也隐藏 Ctrl 和 Alt
   （相对 AutoHotkey 唯一真正的行为差距）。设计草案见
   `../oskeyd/AGENTS.md` 第 8 节第 1 条。
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
12. **帮助窗口的模糊搜索、IME/中文输入、按 `comment` 分组**。
13. **日志窗口的增强**：`--follow`/`--grep` 之类的参数、把 `INFO` 与 `DEBUG`
    分色渲染（现在只按级别上色）。
14. **托盘图标跟随 explorer 重启**（处理 `TaskbarCreated`）并使用真正的
    应用图标（现在只能用系统图标）。

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
  **几何算术全部留在 `MenuModel`**（纯函数、有单测），别散到 QML 里。
* **帮助窗口的新内容或新交互**：条目在 `app/dispatcher` 的 `open_help` 里从
  `Compiled` 的 `bindings`/`remaps` 生成（帮助列表与 `--list` 看的是同一批数据，
  所以 `help` 没有配置参数），交互与绘制在 `HelpModel` + `HelpPopup.qml`。
* **新的 QML 弹窗（第三种）**：不要另起一套配色与字号：
  `import QtQuick.Controls.FluentWinUI3`，颜色一律从 `palette`（`base`/`text`/
  `placeholderText`/`highlight`/`highlightedText`/`alternateBase`/`mid`）取，
  字号用 oskeyd 那套 `pointSize`（12.5 标题 / 11 正文 / 10.5 帮助正文 /
  9 副标题与徽标 / 8.5 细节），几何交给一个 `flowkeyd_models` 里的纯逻辑模型
  （有单测）；**避开 FluentWinUI3 不支持的那些控件**（见第 10 节）。
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
* **新的窗口条件**：`core/window_match`（纯逻辑）+ `platform/win/window` 的
  枚举适配 + `launch_then_activate` 回退 + 手工冒烟清单里加一条用例。
* **新按键或别名**：扩展 `core/keys` 的键表并加一个往返用例
  （要有一个测试遍历表里的每个名字）。如果那个键要靠扩展标志才能与别的键区分
  （像小键盘的 Enter），还要在 `key_from_hook`/`native_key` 里加一条翻译。
* **新的 Windows 版本的虚拟桌面接口**：往 `platform/win/desktop` 的版本表里加
  一条（生效的 `build.revision`、两个 IID、vtable 布局），然后在真机上确认
  选中的条目、桌面数量与序号。
* **新的未公开 API**：在 `platform/win/nt` 里用 `GetProcAddress` 解析，
  使用前先用一次无害调用校验，并永远保留一个已公开的回退。
  已公开但不在静态链接集合里的库走同一条路（`dwmapi` 是范例）。
* **新的动作后端**：在 `src/platform/win/` 下新建模块，从 `dispatcher` 调用，
  并（如果以后补了 `--selftest`）加一项检查。
* **新的日志窗口行为**：尾随逻辑在 `app/log_model`（纯逻辑、可单测），
  渲染在 `LogWindow.qml`。加命令行参数就改 `cli.cpp` 并更新 README 的
  命令行表格。

---

## 14. 配置 schema 速查（与 oskeyd 1:1，只换品牌名）

**这一节是索引，权威定义在 `../oskeyd/README.md`「配置」那一章与
其 `oskeyd.lua.example`。** 两边任何一处不一致，**以 oskeyd 为准并修 flowkeyd**。

### 命令行的两处改名

| oskeyd                                    | flowkeyd                                      |
| ----------------------------------------- | --------------------------------------------- |
| `%USERPROFILE%\.config\oskeyd\config.lua` | `%USERPROFILE%\.config\flowkeyd\config.lua`   |
| `%USERPROFILE%\.config\oskeyd\oskeyd.log` | `%USERPROFILE%\.config\flowkeyd\flowkeyd.log` |
| `oskeyd.*`（DSL 全局表）                  | `flowkeyd.*`                                  |
| `oskeyd.lua.example`                      | `flowkeyd.lua.example`                        |

CLI 开关名字**完全不变**（`-c/--config`、`--no-elevate`、`--console`、
`--elevated`、`--check`、`--list`、`--list-keys`、`--log-window`、
`--log-level`、`--log-file`、`--no-color`、`--allow-multi`、`-h/--help`、
`-V/--version`），只有帮助文本里的程序名换成 `flowkeyd`。
`--parent-pid` 与 `--simulate`/`--selftest`/`--probe` 见第 12 节。

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
* 和弦语法：`~` 放行原始按键、`*` 忽略额外修饰键；`Numpad*` 与主键盘同名键不同。
* 动作：`run`/`send`/`type`/`open`/`volume`/`media`/`clipboard`/`window`/
  `notify`/`menu`/`help`/`power`/`desktop`/`caps_lock`/`suspend`/`reload`/
  `quit`/`none`，字段逐条见 oskeyd README 的动作表。
  简写字符串：`"run:…"`、`"send:…"`、`"type:…"`、`"open:…"`、`"notify:t|b"`、
  `"volume:up"`、`"media:next"`、`"clipboard:get"`、`"window:minimize"`、
  `"desktop:1"`、`"power:sleep"`、裸关键字 `reload`/`quit`/`help`/`none`。
* **完全没有动作**的快捷键就是一个按键屏蔽器（会吞掉它匹配到的按键）。
* `window` 的 `toggle`（默认**开**）只对 `op = "activate"` 有意义；
  `launch` 回退不套用它；显式 `toggle = false` 才关闭。
* `window` 的 `animate`（默认**关**）只对会改变窗口状态的 `op` 有意义，
  写在不产生过渡的 `op`（`close`/`toggle_topmost`）上要被 `--check` 拒绝。

### 本机真实配置（迁移的输入，也是验收的清单）

**2026-09 阶段 10 之后，这份配置实际住在
`%USERPROFILE%\.config\flowkeyd\config.lua`**（oskeyd 那份还在原地，内容除了
注释之外与 flowkeyd 那份一致）。`flowkeyd --check`（走默认搜寻）→
`OK (24 hotkey(s), 0 remap(s))`，零警告。

`C:\Users\xingjian\.config\oskeyd\config.lua`（2026-09，12225 字节）里的绑定：

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
| 小键盘 `-`/`+`/`Enter` | `volume("down"/"up"/"toggle")`，前两个 `repeatable`                               |
| `Ctrl+Alt+F12`         | `suspend("toggle")`                                                               |
| `Ctrl+Alt+F5`          | `reload()`                                                                        |
| `Ctrl+Alt+F4`          | `quit()`                                                                          |

**没有** `remap{}`。`settings` 里只有 `log_level = "info"`。

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
   oskeyd 的示例保留并加了醒目注释。倾向保留（注释里说明“这会吞掉系统快捷键”），
   但这是产品口味问题。
5. **要不要给 flowkeyd 也做一份 `AGENTS.md` 里那种“英文日志 + 中文帮助”的
   文案约定表？** 目前只在第 4 条工作约定里写了原则。
6. **帮助窗口的滚轮方向要不要翻过来？** 现在（与 oskeyd 1:1）是
   `angleDelta.y > 0`（向前滚 / 系统里的“向上滚”）→ 选中项**向列表后面**走，
   比普通列表控件的直觉**是反的**（oskeyd 的 `WM_MOUSEWHEEL` 分支也是这个方向）。
   要改只是 `HelpModel::wheel()` 里一个符号，但那就不是 1:1 了；
   问用户之前不要自作主张。
7. **提权常驻实例会锁住 release 的 exe，而 agent 杀不掉它**（它跑在用户的管理员
   令牌下，`Stop-Process -Force` / `taskkill /F` 都是“拒绝访问”）。
   每次 release 全量构建前要请用户从托盘菜单点一下“退出”。
   可选的长期解法：把常驻实例改从一份**拷贝**（比如 `%LOCALAPPDATA%\flowkeyd\`）
   启动，构建目录就不再被占用 —— 但那需要用户改一下启动习惯。
