# flowkeyd

一个用 Lua 脚本配置的 Windows 键盘钩子守护进程。

flowkeyd 是 **oskeyd**（Rust 参考实现）的 **Qt 6 / C++ 复刻版**：它安装一个
`WH_KEYBOARD_LL` 钩子，把你的配置与按键和弦进行匹配，在你需要时把匹配到的按键
对前台应用隐藏（也就是 AutoHotkey 的行为），然后执行你绑定的动作。动作默认在
**按下**的那一刻就跑（不等按键或修饰键抬起），而且只跑一次；想改成抬起时触发或
按住重复，用 `trigger` 设置即可。它还能把一个按键重映射为另一个按键、在按住源键
期间一直按住目标键、按住时重复，并通过已公开的 `user32!SendInput` 或未公开的
`win32u!NtUserSendInput` 注入按键。动作可以是启动程序、发送按键、控制音量/媒体、
操作窗口、切换虚拟桌面与剪贴板、弹出通知、执行电源动作，或者**弹出一个选单**
——例如一个电源选单（睡眠 / 关机 / 重启，按 `S`/`P`/`R` 选择、`Esc` 关闭）。
同一套卡片还用来弹一个**快捷键帮助**：`Win+/` 列出当前全部绑定，可以直接输入筛选，
`Enter`（或双击一行）就直接把那一行的动作跑起来。
同一套卡片还能当**窗口切换器**（`windows()`）：列出当前所有打开的程序窗口，
输入就按**进程名前缀**筛选，只剩一个窗口时直接切过去；筛到一个程序而它开了好几个
窗口时，每一行会带上数字快捷键（`1`..`9`、`0`），按数字直接跳过去 —— 常见绑法是
「轻碰一下 Win 键」（`keys = "LWin"` + `trigger = "release"`）。
它还能按程序摆放窗口：`window_rule{...}` 让某个程序的窗口第一次出现时落到指定的
虚拟桌面 / 显示器上（默认铺满那块显示器的工作区），并在之前断开的显示器重新接上时
重新归位。规则还能把窗口**钉在所有虚拟桌面上**（`all_desktops = true`）或让它
**始终在最上层**（`topmost = true`）。
如果要给同一个程序同时配「窗口规则」与「唤起它的快捷键」，用 `app{...}` 写到
一起即可，`process` / `title` 只写一遍。

托盘通知区域里的图标平时是应用图标，但**主体内容是当前是第几号虚拟桌面**：
守护进程每 500 ms 问一次 shell 现在在第几张桌面，把图标换成对应的数字
（蓝底白字，`10` 以上显示 `9+`），悬停提示里也写着 `桌面 2/4` ——
一眼就能看出现在在哪张桌面上。查不到当前桌面时（锁屏、非交互会话）退回应用图标。

```lua
hotkey{
  name = "terminal",
  keys = "Ctrl+Alt+t",
  action = run("wt.exe"),
}

remap{ from = "CapsLock", to = "Esc" }

-- 按程序摆放窗口：第一次出现时放到第 2 个虚拟桌面的右屏并最大化
window_rule{ process = "wezterm", desktop = 2, monitor = 2 }

-- 也可以只钉住 / 置顶：切到哪张桌面都看得见，而且一直在最上层
window_rule{ process = "wezterm", all_desktops = true, topmost = true }

-- 同一个程序的窗口规则 + 快捷键 + 启动参数写在一起：process 与 launch 只写一遍，
-- hotkey 里的 window 动作自动拿到它们
app{
  process = "wps",
  launch = { program = [[C:\tools\wps.exe]], wait_ms = 10000 },
  window = { desktop = 3, monitor = 2 },
  hotkeys = {
    { keys = "Win+3", action = window("activate") },
  },
}
```

## 与 oskeyd 的关系

flowkeyd 的目标是把 oskeyd 的**功能与配置语义 1:1 复刻**出来：配置 schema、动作
字段、按键名、CLI 开关、日志文案都保持一致。把
`%USERPROFILE%\.config\oskeyd\config.lua` 复制到
`%USERPROFILE%\.config\flowkeyd\config.lua` 即可直接跑。

唯一有意的差异是 **UI 那一层**：oskeyd 的日志查看器是一个真正的命令行窗口
（独立进程），选单与帮助窗口是自己用 GDI 画的原生窗口；flowkeyd 把它们全部换成
**Qt Quick（QML）+ FluentWinUI3 样式**的窗口，而且都跑在**同一个进程**里。
选单与帮助的配色跟随系统主题（浅色/深色都会跟着变），不像 oskeyd 那样固定深色。
帮助窗口的列表用的是 **Qt 自带的 `ListView` + `ScrollBar`**（不是自绘的滑槽），
所以：滚动条能拖、滚轮是原生的平滑滚动，而且**滚轮方向跟着系统**——
oskeyd（以及自绘时期）把 `+120` 当成「往列表后面走」，与系统列表控件相反，
这条有意不再复刻。筛选框与列表项也全部是标准控件（`TextField` /
`ItemDelegate`）：鼠标点得进去、点得中，不再是自绘的假输入框。
**选单弹窗走的是同一条路线**：列表一样是 Qt 自带的 `ListView` + 标准
`ItemDelegate`，悬停 / 按下 / 高亮都交给 FluentWinUI3 的标准样式，
鼠标点一行就是执行它 —— 行几何与命中测试不再由 flowkeyd 自己算。

| 方面     | oskeyd                             | flowkeyd                                   |
| -------- | ---------------------------------- | ------------------------------------------ |
| 语言     | Rust + 手写 FFI                    | C++20 + Qt 6.11 + 手写 Win32 声明          |
| 配置脚本 | Lua 5.4（`mlua` vendored）         | Lua 5.5.1（`vendor/lua` 静态编进二进制）   |
| DSL 表名 | `oskeyd.*`                         | `flowkeyd.*`（构造器名不变）               |
| 注入标记 | `dwExtraInfo` 里的 `"OSKE"`        | `dwExtraInfo` 里的 `"FLOW"`                |
| 日志窗口 | 独立进程里的命令行窗口             | 进程内的 QML 窗口（FluentWinUI3）          |
| 选单/帮助 | 自绘 GDI 原生窗口，固定深色        | QML 窗口（FluentWinUI3），跟随系统主题     |
| 自动化   | `cargo test` + `--selftest`/e2e    | Qt Test 单测 + `scripts/acceptance.ps1`（注入按键的验收） |

## 状态

版本号形如 `26-09-22-42900ad`：前半（`yy-MM-dd`）是**构建这个 exe 的日期**
（取 exe 自己的最后写入时间，也就是它被链接出来的时刻），后半是**构建时源码所在的
git 提交**（`git rev-parse --short HEAD` 的缩写）。托盘右键菜单里的*版本*、
启动日志的第一行、`--version` 与 `--help` 都能看到它 —— 这样一眼就能确认正在跑的
是哪一次构建、来自哪个提交。下面描述的一切都已实现并有测试覆盖；每一层是如何在
真实 Windows 上验证的，见[验证它能工作](#验证它能工作)。[已知限制](#已知限制)
列出了刻意还没做的部分。

## 环境要求

* Windows 10 或 11（在 build 26200 上验证）。
* **Qt 6.8+（本机用的是 6.11.2 的 `mingw_64`）**、GCC 13（Qt 自带的 MinGW 即可）、
  CMake 3.24+ 与 Ninja。不需要 Visual Studio / MSVC。
* 挂钩子和注入输入都不需要管理员权限。但 flowkeyd 默认会以管理员身份运行：
  未提权启动时会弹一次 UAC 并重启自己，因为只有提权后动作才能驱动提权进程的窗口
  （否则会被 Windows 的 UIPI 拦下）。不想看到 UAC 就加 `--no-elevate`，
  或在配置里写 `elevate = false`（例如已用任务计划程序的“最高权限”启动时）。

运行期没有任何第三方依赖：Lua 5.5.1 静态链在二进制里，Qt 与 MinGW 的运行时 DLL
由构建时的 `windeployqt` 拷到 exe 同目录（见下节），所以构建产物是自包含的。

## 构建与运行

```powershell
$C = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'

# 首次各配置一次
& $C --preset windows-debug
& $C --preset windows-release

& $C --build --preset debug
& $C --build --preset release

# 单元测试：只在 debug 里构建和运行（release 是发布 profile，不含测试目标）
& ctest --test-dir build/windows-debug --output-on-failure
```

两条 profile 的分工是固定的：

| 目录                    | 用途       | 里面有什么                                                          |
| ----------------------- | ---------- | ------------------------------------------------------------------- |
| `build/windows-debug`   | 开发       | 全部测试目标（`tst_*.exe`）+ 已部署的 Qt 运行时，随手就能跑         |
| `build/windows-release` | 构建树     | 只是编译产物；**这里出现 `tst_*.exe` 就是 bug**                     |
| `build/dist-release`    | **发布**   | 只有 `flowkeyd.exe` 与它需要的 Qt/MinGW 运行时 —— 拷走就能跑        |

`build/dist-release/` 是 release 构建时**自动**产出的（不用另跑命令）：它里面的
`flowkeyd.exe` 与 `build/windows-release/flowkeyd.exe` 是同一个二进制，只是旁边
没有 `CMakeCache.txt`/`build.ninja`/`*.a` 这些构建系统文件。**要交付或换机器，
就整个拷 `build\dist-release\`**：目标机器不需要装 Qt。
`CMakePresets.json` 里写死了本机的 Qt / MinGW / Ninja 路径，换机器时改那里。

**构建产物是自包含的**：每次链接完 `flowkeyd` 之后会自动跑一次 `windeployqt`，
把 Qt 与 MinGW 的运行时 DLL、以及 exe 用到的 QML 模块（`QtQuick`、
`QtQuick.Controls.FluentWinUI3`……）拷到产物目录。所以
**直接双击 `build\dist-release\flowkeyd.exe`（或用构建树里那一份）就能启动**，
不需要把 Qt 的 `bin` 加进 `PATH`，也不需要额外跑部署脚本。
（想走标准的安装规则时仍然可以用 `cmake --install`：那套规则走的是
`qt_generate_deploy_app_script`；见 `CMakeLists.txt` 末段。）

> 双击启动等价于**不带任何参数**启动：它没有控制台，日志只进
> `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`（用托盘「查看日志」看），
> 并且默认会弹一次 UAC 自提权（不想提权就在配置里写 `elevate = false`）。

配置文件默认放在 `%USERPROFILE%\.config\flowkeyd\config.lua`（见下文
[配置文件在哪里](#配置文件在哪里)）。它是一段 Lua 脚本，参考配置见
[`flowkeyd.lua.example`](flowkeyd.lua.example)。

启动后 flowkeyd 是一个常驻托盘的程序：从终端启动时那个控制台属于你的 shell，
flowkeyd 不会去动它（`--console` 可以强制保留输出）。右键菜单是
*查看日志*、*挂起/恢复快捷键*、*重载配置*、*打开配置文件*、*版本 …*、*退出*
（*版本* 是个不可点的信息项），左键单击直接打开日志窗口。悬停提示会显示
构建版本与挂起状态。

托盘图标、日志窗口与三个弹窗（选单 / 帮助 / 窗口切换器）用的是**应用自己的
图标**（仓库根目录 `logo.svg`
那张：一个深色圆角方块加两个叠加的按键），不再是系统图标。exe 文件本身在资源
管理器/任务栏里显示的那个图标是另一份东西——嵌在 PE 资源里的 `assets/flowkeyd.ico`
——两者都由 `logo.svg` 生成（开发时要换图标：改 `logo.svg` 然后
`cmake --build --preset debug --target icons`；见 [AGENTS.md](AGENTS.md) 第 10 节）。

**构建版本号**长这样：`26-09-22-42900ad` —— 前半是**构建这个 exe 的日期**
（`yy-MM-dd`，取运行中的可执行文件自己的最后写入时间），后半是**构建时源码的
git 提交缩写**（`git rev-parse --short HEAD`）。日期每次重新链接都会跟着变，
git 那一段在提交变了重新构建时自动更新（都不需要任何构建脚本）。启动日志的
第一行、`--version` 与 `--help` 打印的也是它：

```console
$ flowkeyd --version
flowkeyd 26-09-22-42900ad
Lua 5.5.1
```

所以「跑的是不是最新那份」看这一行就够了（常驻实例从 `build\dist-release` 跑的
时候，重建前要先 `--quit`，见[开机自启与更新](#开机自启与更新任务计划程序)）。

> 想让版本号里是你最新提交的哈希，就**先提交再构建**：哈希是 CMake 配置时取的，
> 而 `.git` 的 HEAD 与当前分支的 ref 被登记成了 configure 依赖，提交之后重新构建
> 会自动重新配置一次。工作区里还有未提交的改动时，显示的哈希是最近一次提交的。

日志写控制台，同时（守护进程模式下总是）追加写到
`%USERPROFILE%\.config\flowkeyd\flowkeyd.log`
（`--log-file` 可以指定别的地方，`--log-level` 调整级别）。

**日志窗口**（左键单击托盘图标）是一个**进程内的 QML 窗口**（FluentWinUI3
样式）：它尾随上面的日志文件，每 250 ms 追一次新行，最多显示最后 1000 行，
按级别配色，带一个子串筛选框，新行会自动滚到底（你往上翻时不会打扰你）。
窗口标题带着已显示的行数（`flowkeyd 日志 — 128 行`）。

它是**同一个进程里的普通窗口**，所以：关掉它不会退出守护进程（
`setQuitOnLastWindowClosed(false)`）；再点一次托盘图标只会把它抬到前面，
不会开出第二个；退出只能走托盘菜单的*退出*、`quit` 动作，或 `taskkill`
（**不带** `/F` 时如果日志窗口开着，`WM_CLOSE` 会被它吃掉，所以要么走托盘退出，
要么用 `/F`）。日志窗口里看到的只是内存里的一小段——完整日志在日志文件里。

### 开机自启与更新（任务计划程序）

flowkeyd **自己**会注册一个**登录时触发**的计划任务（*使用最高权限运行*）：
每次启动时它检查这个任务，**不存在、或指向的 exe 与当前正在运行的这个不是同一个，
就先弹一个确认框问你要不要注册 / 更新**，同意之后才用当前路径重新注册一次
（不同意就保持原样，下次启动会再问）。之后每次登录、以及每次机器重启，
flowkeyd 都会以管理员权限起来，**不弹 UAC**。

**没有安装目录**：自启跟着你运行的那个 `flowkeyd.exe` 走。把 exe 放到
`D:\Tools\flowkeyd\flowkeyd.exe` 并运行一次，任务就指向那里；换到别处再运行一次，
在确认框里同意之后任务就更新；直接运行 `build\dist-release\flowkeyd.exe`
也一样在哪儿生效。

为什么必须是计划任务，而不是 `shell:startup` 快捷方式或 `HKCU\...\Run`：

* flowkeyd 需要管理员权限才能驱动提权进程的窗口、才能执行电源动作；
  只有计划任务能做到「提权启动且不弹 UAC」（后两者要么以普通权限跑，
  要么每次登录弹一次 UAC）。
  也**不要**给 exe 登记 `RUNASADMIN` 兼容性标记：那会让**任何**调用都提权，
  连 `flowkeyd --check` 都会弹 UAC —— 离线命令本就不该弹 UAC。
* 服务（Windows Service）不行：它跑在 session 0，`WH_KEYBOARD_LL` 看不到桌面的
  按键，也没有托盘图标。

```powershell
# 删掉自启（需要管理员）。先停实例，否则它下次启动会把任务注册回来。
& D:\Tools\flowkeyd\flowkeyd.exe --quit
& D:\Tools\flowkeyd\flowkeyd.exe --remove-autostart
```

**更新循环**（不用任何脚本）：`cmake --build --preset release` 会把干净的发布包
写到 `build\dist-release\`。常驻实例如果正从那里跑（它会锁住那个 exe），先
`--quit`、构建、再重新启动一次，任务会自动指向新路径。构建产物也可以整个拷到
别的机器上直接运行。

`--no-autostart` 可以临时关掉自启管理；开发 / 测试实例（`--no-elevate`、
`--allow-multi`、或没有提权的进程）本来就**不会**动这个任务。
`--no-prompt` 则不弹任何交互提示：自启按默认的「注册 / 更新」处理，
「已在运行」也只记日志、不弹框（脚本与自动化用）。

脚本里的任务参数（这些默认值全是坑，改的时候别删）：

| 设置 | 值 | 为什么 |
| ---- | -- | ------ |
| `RunLevel` | *最高权限*（`HighestAvailable`） | 提权且不弹 UAC |
| 触发器 | *登录时* + 延迟 15 秒 | 托盘要等 explorer；本版本还没有处理 `TaskbarCreated`（explorer 重启后重新挂托盘图标），延迟是最便宜的兜底 |
| `ExecutionTimeLimit` | `PT0S`（不限） | **默认是 72 小时** —— 三天后任务计划程序会亲手把守护进程停掉 |
| 「只在交流电时启动 / 掉电就停」 | 关 | 默认是开 |
| 多个实例 | 忽略新实例 | 加上 flowkeyd 自己的单实例互斥体，双保险 |
| 工作目录 | exe 所在目录 | `--config` 的相对路径与 `{cwd}` 模板看它 |
| 允许按需启动 | 开 | 更新后不用重启系统，`Start-ScheduledTask flowkeyd` 就能起 |
| 失败后重启 | 1 分钟一次，最多 3 次 | COM 那几块（Core Audio / 虚拟桌面 vtable）崩了能自己回来；**正常退出（退出码 0）不会触发重启**，所以托盘/`quit` 动作退出后不会被拉起来 |

**任务失败是静默的**：路径写错、exe 被删、单实例冲突…结果都只是「没有托盘图标、
快捷键不生效」，不会弹任何东西。排查顺序：任务计划程序里看 `flowkeyd` 这个任务
（*上次运行结果*）、看 `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`、
再手动跑一次 `Start-ScheduledTask -TaskName flowkeyd`。

自启任务的路径**跟着当前运行的 exe 走**：一旦那个路径失效（你把 exe 删了或移走了），
表现就是上面那种静默失败；下次手动启动一次 flowkeyd，自检会把它刷新回来。
另外，开发 / 测试实例（`--no-elevate`、`--allow-multi`、或没有提权的进程）**不会**
碰这个任务，免得临时实例把真实的自启劫持到构建目录。

想立刻关掉正在运行的实例：

```powershell
& 'D:\Tools\flowkeyd\flowkeyd.exe' --quit   # 换成你自己的 exe 路径
```

`--quit` 按**配置文件路径**匹配实例（`--config` 可选），最多等 10 秒；
它走的是一条命名的事件通道，让守护进程走**干净的退出路径**（卸钩子、退循环），
而不是 `taskkill /F` —— 后者会留下一个幽灵托盘图标。
没有在跑的实例时它返回 1，不算错误。
事件对象带 Low 完整性标签，所以**不提权**的调用方也能请提权的守护进程退出。

## 命令行

```
flowkeyd [选项]

-c, --config <PATH>     配置文件（默认：%USERPROFILE%\.config\flowkeyd\config.lua，
                        找不到时依次回退到 exe 同目录、%APPDATA%\flowkeyd、
                        当前目录下的 config.lua）
    --no-elevate        不自动提权，直接以当前权限运行
    --console           保留控制台输出
    --elevated          内部标记：已经提权，不要再重启自己
    --check             校验配置并退出
    --list              打印已解析的快捷键与重映射
    --list-keys         打印所有可接受的按键名
    --quit              请正在运行的实例干净退出（按配置文件路径匹配，最多等 10 秒；
                        没找到在跑的实例时返回 1；不会装钩子，也不需要管理员）
    --no-autostart      不要注册 / 刷新「登录时自启」的计划任务（开发测试用）
    --remove-autostart  删除那个计划任务后退出（需要管理员权限）
    --log-window        启动时直接打开日志窗口
    --parent-pid <PID>  兼容参数，本项目忽略（日志窗口在进程内）
    --log-level <LVL>   trace|debug|info|warn|error|off
    --log-file <PATH>   同时把日志追加写入文件
    --no-color          关闭 ANSI 颜色
    --allow-multi       跳过单实例检查
    --no-prompt         不弹交互提示（已在运行 / 是否注册开机自启都按默认处理）
-h, --help              帮助
-V, --version           版本号（`yy-MM-dd-git 短修订`）与 Lua 版本号
```

`--check` 的输出是一行机器可读的结论：

```console
$ flowkeyd --check --config flowkeyd.lua.example
D:\prj\flowkeyd\flowkeyd.lua.example: OK (43 hotkey(s), 3 remap(s), 7 window rule(s))
```

（`window rule(s)` 只在配置里真的写了 `window_rule` 时才出现，没有摆放规则的
配置仍然输出老样子。）

`--list` 的形状与 oskeyd 的 `print_bindings` 逐字形似（因此两边的输出可以对比）。

**离线命令**（`--check` / `--list` / `--list-keys` / `--help` / `--version`）
永远不会弹 UAC，也绝不安装钩子——为一个只读的校验弹窗很没道理。
`--quit` 同样不装钩子、不提权（它只去通知一个已经在跑的实例，
见[开机自启与更新](#开机自启与更新任务计划程序)），但它会去碰另一个进程，
所以不算离线命令。`--remove-autostart` 也不在离线命令里：它要管理员权限才能删任务。

守护进程模式（不带任何离线命令）下配置读不出来或校验不过时，flowkeyd 除了把
错误写进 `stderr`，还会弹一个 Qt 标准消息框（标题 `flowkeyd 配置错误`，错误文本
可选中复制），点确定后以退出码 1 退出。双击启动时进程没有控制台，只有弹窗能
让你知道到底出了什么事。（例如一个和弦里写了两个普通键，
`keys = "NumpadSub+NumpadAdd"`，就会在这里直接报出来的那条。）

守护进程模式下同一份配置只允许一个实例。再启动一次时会**先弹一个原生提示框**
（`flowkeyd 已在运行`），点确定后以退出码 1 退出；这一步刻意放在 **UAC 提权之前**，
所以重复双击不会白白弹一次 UAC，也不会动到正在运行的那个实例。
（`--allow-multi` 跳过单实例检查；`--no-prompt` 只跳过提示、检查照做。）

## 配置

一个逐项注释、覆盖全部特性的文件见 [`flowkeyd.lua.example`](flowkeyd.lua.example)
——`--check` 就是拿它当样本跑的。

### 配置脚本的写法

配置文件是**一段真正的 Lua 5.5.1 脚本**，由 flowkeyd 以你的权限执行：可以算表达式、
写循环、用 `os.getenv` / `string.format` 之类的标准库。它也能做任何你的账号
能做的事，所以别去运行来路不明的配置。

两种写法完全等价，可以混用：

```lua
-- 1) 命令式：在脚本体里逐个注册。条目顺序就是匹配顺序（先注册者优先）。
settings{ swallow = true, tick_ms = 15 }

hotkey{ name = "terminal", keys = "Ctrl+Alt+t", action = run("wt.exe") }

-- 循环生成是 Lua 配置最直接的收益
for i = 1, 4 do
  hotkey{ name = "desktop-" .. i, keys = "Win+F" .. i, action = desktop(i) }
end

remap{ from = "CapsLock", to = "Esc" }

-- 按程序摆放窗口：第一次出现时放到第 2 个虚拟桌面、第 2 块显示器上并最大化
window_rule{ process = "wezterm", desktop = 2, monitor = 2 }

-- 同一个程序的窗口规则、启动参数与唤起它的快捷键写在一起
app{
  process = "wps",
  launch = { program = [[C:\tools\wps.exe]] },
  window = { desktop = 3, monitor = 2 },
  hotkeys = { { keys = "Win+3", action = window("activate") } },
}
```

```lua
-- 2) 声明式：把一张表 return 出去（排在脚本体注册的条目之后）。
return {
  settings = { swallow = true },
  hotkeys = { { keys = "Ctrl+Alt+t", action = run("wt.exe") } },
  remaps = { { from = "CapsLock", to = "Esc" } },
  window_rules = { { process = "wezterm", desktop = 2, monitor = 2 } },
  apps = { { process = "wps", window = { desktop = 3, monitor = 2 } } },
}
```

校验、错误信息、`--list` 的形状与 oskeyd 完全一致：每个条目都可以有 `name`，
出错时会写成 `hotkey #3 (\`terminal\`): unknown field ...`，脚本本身的错误则带
`config.lua:行号`。

### Lua 的几个坑

* **`repeat` 是 Lua 关键字**，所以 `hotkey{ repeat = true }` 是语法错误。
  重复参数请写 `repeatable = true`（或者 `["repeat"] = true`）。
* **字符串里的反斜杠是转义序列。** Windows 路径用长字符串
  `[[C:\tools\my app\app.exe]]`，或者写成 `'C:\\tools\\app.exe'`。
  注意 `"C:\tools"` **不会报错**：`\t` 是制表符，它会静默变成 `C:` + TAB + `ools`；
  只有 `\p` 这类非法转义才会报错。
* **`{}` 既是空列表也是空表。** 配置里空表只应出现在列表位置（`args`），
  `env = {}` 会被判成空列表并报错，提示会说明这一点。
* **动作不能是 Lua 函数**：`action = function() ... end` 会被拒绝，
  因为钩子回调与工作线程只执行校验过的声明式动作。
* 带 UTF-8 BOM 的配置（记事本、`Set-Content -Encoding UTF8` 的产物）与 CRLF
  都能正常读：加载器会先剥掉 BOM。

### DSL 速查

除了直接写 `{ type = "..." }` 之外，[`src/lua/lua_prelude.lua`](src/lua/lua_prelude.lua)
还提供一组构造器（同时挂在 `flowkeyd.*` 下，免得与脚本自己的全局变量撞名）：

| 构造器                                        | 等价于                                                              |
| --------------------------------------------- | ------------------------------------------------------------------- |
| `settings{...}`、`hotkey{...}`、`remap{...}`、`window_rule{...}` | 四个注册入口                            |
| `app{...}`                                    | 同一个程序的窗口规则 + 快捷键（见下文）                              |
| `run(p[, args][, opts])`                      | `{ type = "run", program = p, args = args, ... }`                   |
| `send(keys[, opts])`、`type_text(text[, opts])` | `{ type = "send", keys = keys, ... }`、`{ type = "type", text = text, ... }` |
| `open(target[, opts])`、`notify(title[, body])` | `{ type = "open", target = target, ... }`、`{ type = "notify", ... }` |
| `volume(op[, opts])`、`media(op)`             | `{ type = "volume", op = op, ... }`、`{ type = "media", op = op }` |
| `window(op[, opts])`、`clipboard(op[, opts])` | `{ type = "window", op = op, ... }` 等                             |
| `caps_lock([state])`、`suspend([state])`       | `{ type = "caps_lock", state = ... }`、`{ type = "suspend", ... }`   |
| `desktop(n)`                                  | `{ type = "desktop", switch = n }`                                 |
| `power(op)`、`menu{...}`                       | `{ type = "power", op = op }`、`{ type = "menu", ... }`            |
| `help([title])`                                | `{ type = "help", title = title }`                                  |
| `windows([title])`                             | `{ type = "windows", title = title }`                               |
| `reload()`、`quit()`、`none()`                 | `{ type = "reload" }`、`{ type = "quit" }`、`{ type = "none" }`       |

`run("cmd /c echo hi", { shell = true, wait = true })` 的第二个参数既可以是参数列表
（有数组部分）也可以是选项表；`type_text` 不叫 `type`，因为 `type` 是 Lua 的内建函数。
空参数表会被省略，不必手写 `args = {}`。

### 配置文件在哪里

没有 `--config` 时，flowkeyd 按下面的顺序找配置文件，用第一个存在的：

1. `%USERPROFILE%\.config\flowkeyd\config.lua`（默认位置）
2. flowkeyd.exe 同目录下的 `config.lua`
3. `%APPDATA%\flowkeyd\config.lua`
4. 当前目录下的 `config.lua`

回退到非默认位置时会打一条 warning 指出真正使用的位置；每一个都不存在时，
报错会指向默认位置（也就是你该创建的那个文件）。

### 从 TOML 迁移到 Lua

oskeyd 0.2 起配置是 Lua；flowkeyd 继承了这个约定，`.toml` 后缀会被**明确拒绝**
（而不是拿它去喂 Lua 解析器，那只会得到一屏看不懂的语法错误）：

```console
$ flowkeyd --check
flowkeyd: C:\Users\me\.config\flowkeyd\config.toml is an old TOML config; flowkeyd reads
Lua configs now — port it to C:\Users\me\.config\flowkeyd\config.lua (see the “从 TOML
迁移到 Lua” section of README.md) or point --config at your .lua file
```

翻译规则几乎是一对一的，键名、取值、动作字段与校验规则都没有变：

| TOML                        | Lua                                                             |
| --------------------------- | --------------------------------------------------------------- |
| `[settings]`                | `settings{ ... }`（或 `settings = { ... }`）                     |
| `[[hotkey]]`                | `hotkey{ ... }`                                                 |
| `[[remap]]`                 | `remap{ ... }`                                                   |
| `x = [a, b]`                | `x = { a, b }`                                                   |
| `repeat = true`             | `repeatable = true`（`repeat` 是 Lua 关键字）                    |
| `'C:\path'`（字面字符串）   | `[[C:\path]]`（或 `'C:\\path'`）                                   |

最快的方法是把 `flowkeyd.lua.example` 当模板拄一遍，然后让 `--check` 指出还没
改完的地方。举个例子，这段 TOML：

```toml
[settings]
elevate = false

[[hotkey]]
name = "wezterm"
keys = "Win+s"
action = { type = "window", op = "activate", process = "wezterm",
           launch = { program = '{USERPROFILE}\scoop\shims\wezterm.exe', args = ["start"], wait_ms = 5000 } }
```

写成 Lua 就是：

```lua
settings{ elevate = false }

hotkey{
  name = "wezterm",
  keys = "Win+s",
  action = window("activate", {
    process = "wezterm",
    launch = {
      program = [[{USERPROFILE}\scoop\shims\wezterm.exe]],
      args = { "start" },
      wait_ms = 5000,
    },
  }),
}
```

改完存成同目录下的 `config.lua`（旧的 `.toml` 可以删掉或改名备份），再跑一次
`flowkeyd --check`。

### 管理员权限与托盘

* **提权。** 以守护进程模式启动时，如果进程没有管理员令牌，flowkeyd 会用
  `ShellExecuteW("runas")` 把同样的命令行转发给一个提权后的自己，然后退出。
  所以相对路径的 `--config` 和工作目录在重启后依然有效。UAC 提示被拒绝时
  它不会直接死掉：会打一条 warning 并以普通权限继续跑（钩子照样工作，
  只是驱动不了提权进程的窗口）。**本机实测：用户点“否”时 `ShellExecuteW("runas")`
  返回的是 5（拒绝访问），而不是 1223**，所以降级路径不能只判 `ERROR_CANCELLED`。
* **不提权的场合。** `--check`、`--list`、`--list-keys` 都是离线命令，永远不会
  弹 UAC。另外 `--no-elevate`（或 `settings.elevate = false`）能完全关掉提权。
* **托盘。** 右键菜单是 *查看日志*、*挂起/恢复快捷键*、*重载配置*、
  *打开配置文件*、*退出*，左键单击直接打开**日志窗口**（进程内的 QML 窗口，
  见上文）。悬停提示会显示挂起状态，因为“快捷键突然不响应”和
  “flowkeyd 挂起了”必须能一眼分开。
* **日志。** 守护进程模式下日志总是写两份：控制台（终端启动时）与
  `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`（目录会自动建）。后者就是日志
  窗口尾随的那个文件（没有它，隐藏控制台之后就彻底看不到任何信息了）；
  它与配置文件在同一个目录，排查时只需要看一个地方。

### `settings{ ... }`

```lua
settings{ log_level = "info", swallow = true, tick_ms = 15 }
```

| 键                   | 默认值   | 含义                                                                                         |
| -------------------- | -------- | -------------------------------------------------------------------------------------------- |
| `log_level`          | `"info"` | `trace`、`debug`、`info`、`warn`、`error`、`off`                                             |
| `swallow`            | `true`   | 默认是否把匹配到的按键对前台应用隐藏                                                         |
| `exact_modifiers`    | `false`  | `false` 时即使同时按着 Alt 也触发 `Ctrl+h`（AutoHotkey 行为）；`true` 要求修饰键集合完全一致 |
| `release_modifiers`  | `true`   | 在执行 `send`/`type` 前先松开你按住的修饰键，随后再按回去                                    |
| `repeat_interval_ms` | `50`     | 默认重复间隔                                                                                 |
| `repeat_delay_ms`    | `400`    | 默认的重复开始前延迟                                                                         |
| `tick_ms`            | `15`     | 引擎定时器粒度                                                                               |
| `input_backend`      | `"auto"` | `auto`、`user32`，或 `ntuser`（未公开的 `win32u!NtUserSendInput`）                           |
| `single_instance`    | `true`   | 另一个实例已占用同一配置时拒绝启动                                                           |
| `elevate`            | `true`   | 以守护进程模式启动时，没有管理员权限就自动提权重启（`--no-elevate` 覆盖）                     |

未知键会报错，因此拼写错误会被 `--check` 抓出来，而不是被静默忽略。

### `hotkey{ ... }`

```lua
hotkey{
  name = "terminal",
  keys = { "^!t", "^!j" },
  action = run("wt.exe"),
  on_release = notify("up"),
}
```

| 键           | 含义                                                          |
| ------------ | ------------------------------------------------------------- |
| `keys`       | 一个和弦或一组和弦：`"Ctrl+Alt+t"`、`"^!t"`、`{ "^!t", "^!j" }` |
| `name`       | 日志与 `{hotkey}` 模板中使用的标签（默认取第一个和弦）        |
| `trigger`    | 何时触发：`press`（默认）、`release`、`repeat`（见下表）      |
| `action`     | 按下时执行什么（`press` 和 `on_press` 是别名）                |
| `on_release` | 松开时执行什么                                                |
| `swallow`    | 为该快捷键覆盖 `settings.swallow`                             |
| `repeatable` | `true`，或 `{ interval_ms = 40, delay_ms = 300 }`（也认 `["repeat"]`） |
| `enabled`    | `false` 会在不删除条目的前提下禁用它                          |
| `comment`    | 由 `--list` 显示的自由文本备注                                |

`trigger` 决定一个快捷键在什么时候跑它的动作：

| 取值      | 含义                                                                                                        |
| --------- | ----------------------------------------------------------------------------------------------------------- |
| `press`   | **默认**。和弦的最后一个按键一到就派发，只派发一次；按住不放（操作系统的自动重复）不会重新触发。被吞掉的 `Win+…`/`Alt+…` 和弦也是立即派发，不会等到修饰键抬起 |
| `release` | 抬起时才派发；`action` 里的动作被挪到松开那一刻，`on_release` 排在它们后面                                  |
| `repeat`  | 按下即派发，之后按住期间按 `settings.repeat_interval_ms`/`repeat_delay_ms` 重复；等价于 `repeatable = true` |

`trigger = "repeat"` 与 `repeatable = true` 是同一件事的两种写法（可以叠加
`repeatable = { interval_ms = 40 }` 来给参数）；把它们写成互相矛盾的组合会被
`--check` 拒绝。

`trigger = "release"` 写在**单个修饰键**上（例如 `keys = "LWin"`）时是「轻碰」
语义：按下修饰键本身**照常放行给系统**（`Win+E` / `Win+L` 这些没被 flowkeyd
接管的系统组合不受影响），只有期间没按过别的键、松开时才触发 —— 窗口切换器就是
靠它绑在 Win 键上的（见[窗口切换器](#窗口切换器)）。单个修饰键配默认的
`trigger = "press"` 仍然是老行为。

和弦语法既接受名称也接受 AutoHotkey 前缀：

| 语法                | 含义                                  |
| ------------------- | ------------------------------------- |
| `Ctrl+Alt+h`、`^!h` | 修饰键在前，按键在后                  |
| `^!h`               | `^` Ctrl、`!` Alt、`+` Shift、`#` Win |
| `~F4`               | 即使快捷键触发也放行该按键            |
| `*F1`               | 即使额外按住了修饰键也触发            |

只有修饰键的和弦也能用：`Ctrl+Shift` 会在 Ctrl 按着时、Shift 按下时触发，
而通用的 `Shift`/`Ctrl`/`Alt` 名称会匹配键盘的任意一侧。

**单个字母的键名一律小写。** `h` 就是 H 键；想表达“按住 Shift 的 h”要显式写
`Shift+h`，直接写成大写的 `H` 会被 `--check` 拒绝：

```lua
keys = "h"            -- H 键（不按修饰键）
keys = "Alt+h"        -- Alt+H
keys = "Alt+Shift+h"  -- 按住 Shift 才是大写 H
keys = "Alt+H"        -- 报错：字母键名要小写（写 Shift+h）
```

同一条规则也适用于 `remap` 的 `from`/`to`、发送脚本里 `{...}` 中的键名
（`send("{S}")` 要写成 `send("{s}")`），以及选单条目的 `key`。
**发送脚本里的裸字符不受影响**：`send("A")` 仍然是 AutoHotkey 语义下的
“打出大写 A”（等价于 `send("+a")`）；要按字面输入任意文本用 `type("...")`
或 `send("{Text}...")`。

一个和弦里**只能有一个按键**（其余片段必须是修饰键），所以「两个普通键一起按」
是**不支持**的：`keys = "NumpadSub+NumpadAdd"` 会被拒绝，
`--check` 报 `` `NumpadSub` in chord `NumpadSub+NumpadAdd` is not a modifier ``。
要表达这种意图只能用修饰键组合（如 `Ctrl+NumpadMult`），或者换一个独立的键。

`Numpad*` 是小键盘上的那些键，与主键盘上的同名键是**不同的键**：
`NumpadSub`（`VK_SUBTRACT`）不会匹配主键盘的 `-`（那是 `Minus`，
`VK_OEM_MINUS`），`NumpadAdd`（`VK_ADD`）不会匹配主键盘的 `=`，
`NumpadEnter` 也不会匹配主键盘的 `Enter`。小键盘的 Enter 与主键盘的 Enter
共用 `VK_RETURN`，区分靠的是 Windows 的扩展键标志；而这个区别与 `NumLock`
完全无关：这几个键上报的 `VK` 不随 `NumLock` 变化（`NumLock` 只影响字符翻译），
所以开着或关着都一样能用。完整的按键名清单见 `flowkeyd --list-keys`。

**完全没有动作**的快捷键就是一个按键屏蔽器：它会吞掉它匹配到的按键。

### 动作

动作是 `{ type = "...", ... }`、上面那些构造器、一个简写字符串，或者它们的列表。
列表在工作线程上自上而下执行。

| 类型        | 字段                                                                                                      | 说明                                                                                                                                                                                                                                                                                      |
| ----------- | --------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `run`       | `program`、`args[]`、`cwd`、`shell`、`show`（`normal`/`hidden`/`minimized`/`maximized`）、`wait`、`env{}` | 启动一个进程；`shell = true` 时通过 `cmd.exe /C` 运行                                                                                                                                                                                                                                     |
| `send`      | `keys`、`delay_ms`、`release_modifiers`                                                                   | AutoHotkey 发送脚本：`^{c}`、`{Enter}`、`{Esc 3}`、`{Enter down}`、`{{}`、`{Text}hello`                                                                                                                                                                                                   |
| `caps_lock` | `state` = `off`                                                                                           | 仅当 CapsLock 当前开启时将其关闭；适合与 `send` 组合，实现 CapsLock 层快捷键的兜底逻辑                                                                                                                                                                                                    |
| `type`      | `text`、`delay_ms`、`release_modifiers`                                                                   | 字面 Unicode 注入，与键盘布局无关                                                                                                                                                                                                                                                         |
| `open`      | `target`、`args`、`cwd`、`show`                                                                           | `ShellExecuteW`：URL、文档、文件夹                                                                                                                                                                                                                                                        |
| `volume`    | `op` = `up`/`down`/`set`/`mute`/`unmute`/`toggle`、`level`（0-100）、`step`                               | 对默认输出设备使用 Core Audio 的 `IAudioEndpointVolume`                                                                                                                                                                                                                                   |
| `media`     | `op` = `play_pause`/`next`/`prev`/`stop`                                                                  |                                                                                                                                                                                                                                                                                           |
| `clipboard` | `op` = `get`/`set`/`append`/`clear`、`text`                                                               |                                                                                                                                                                                                                                                                                           |
| `window`    | `op` = `activate`/`minimize`/`maximize`/`restore`/`close`/`toggle_topmost`/`move_prev_desktop`/`move_next_desktop`/`move_left_monitor`/`move_right_monitor`、`target`、`process`、`launch`、`wait_ms`、`toggle`、`animate`、`follow` | `target` 匹配窗口标题的子串，`process` 匹配可执行文件名（`wezterm` 也能匹配 `wezterm-gui.exe`）；既没有 `target` 也没有 `process` 就表示前台窗口。`launch = { program, args[], cwd, show, shell, env{}, wait_ms }` 会在没有任何匹配时启动该程序，然后等待它的窗口（默认 3000 ms）并激活它。`toggle`（默认开，只对 `op = "activate"` 有意义）会在目标窗口已经在前台时改为最小化它。`animate`（默认**关**）控制这次状态变化要不要播放 DWM 的过渡动画。`wait_ms` 写在动作顶层时是 `launch.wait_ms` 的简写；在 `app{}` 里 `launch` 还会继承 app 的 `launch`（逐字段合并）。四个 `move_*` op 把**当前窗口**（不写 `target`/`process` 时）挪到相邻的虚拟桌面 / 显示器：`move_prev_desktop`/`move_next_desktop` 只动虚拟桌面且**首尾相接**（显示器上的几何不变，视图**不**跟着走），`move_left_monitor`/`move_right_monitor` 只动显示器（保留最大化状态，否则保持原有大小并居中到目标工作区；没有更左/更右那一块时失败，**不循环**）。这四个 op 都不套用 `toggle`、也不接受 `launch`。`follow = true` 只能写给 `move_prev_desktop`/`move_next_desktop`：搬完之后把视图也切到目标桌面并重新激活那个窗口（用户跟着窗口一起过去；不写时视图不动，与 Windows 自己的 `Win+Ctrl+Shift+←/→` 一致），写在其它的 op 上会被 `--check` 拒绝 |
| `notify`    | `title`、`body`                                                                                           | 托盘气泡提示                                                                                                                                                                                                                                                                              |
| `menu`      | `title`、`items[]`（每项 `{ key, label, hint, action }`）                                                  | 弹出一个 QML 选单让用户挑一项（见[选单与电源](#选单与电源)）；条目上的单字符 `key` 直接选中它，`↑`/`↓` + `Enter` 与鼠标也能选，`Esc`（或点到别的地方）只关窗口。没有 `action`（或 `none()`）的条目只是把选单关掉                                                                         |
| `help`      | `title`（可选，默认「快捷键」）                                                                            | 弹出一个**快捷键帮助**（同一套卡片风格，见[快捷键帮助](#快捷键帮助)）：列出当前配置里全部生效的快捷键与重映射；筛选框里直接输入（鼠标点一下就进去）就筛选；`↑`/`↓`、`PgUp`/`PgDn` 或滚轮/拖动滚动条滚动（滚动不改选中项）；**`Enter` 或双击一行 = 关掉窗口并执行那一行的动作**；左键点一行 = 选中它并把它的按键复制到剪贴板；`quit`/`suspend`/`power` 这类危险动作要按两次（第一次只是等确认）；`Esc` 依次是「取消确认 → 清筛选 → 关窗」。列表由配置本身生成，所以没有别的参数 |
| `windows`   | `title`（可选，只用作**窗口标题**）                                                                        | 弹出一个**窗口切换器**（同一套卡片风格，见[窗口切换器](#窗口切换器)）：列出当前所有打开的程序窗口（标题 + 进程名），输入按**进程名前缀**筛选（标题只显示、不参与匹配）；**筛选到只剩一个窗口就直接激活它**，多条时用 `↑`/`↓` + `Enter` 或鼠标点选，`Esc` 关窗；筛到一个进程、而它开了多个窗口时进入**数字选择模式**（前 10 行依次是 `1`..`9`、`0`，按数字直接切过去）。卡片**没有标题行**（筛选框就是第一行，列表与它同宽），打开的瞬间还会把输入法切成英文、关掉时把打开前的模式写回去；列表由当前枚举生成，所以没有别的参数 |
| `power`     | `op` = `sleep`/`hibernate`/`shutdown`/`restart`/`logoff`/`lock`/`screen_off`                                | 系统电源：睡眠、休眠、关机、重启、注销、锁定、关屏。关机/重启/注销需要管理员权限（flowkeyd 默认就提权运行），失败时会写日志并弹一个气泡而不是静悄悄地什么都不做；`screen_off` 只是关掉全部显示器（不睡眠），不需要权限，任意按键/鼠标动作都会把屏幕重新点亮                                                                                      |
| `desktop`   | `switch`（从 1 开始）                                                                                     | 切到第 N 个虚拟桌面（Task View 里从左到右的顺序）。用的是 shell 未公开的 `IVirtualDesktopManagerInternal`，所以可以按序号直达（详见[已知限制](#已知限制)）；版本对不上时错误会写进日志                                                                                                      |
| `suspend`   | `state` = `on`/`off`/`toggle`                                                                             | 暂停快捷键匹配；suspend 快捷键本身仍然可用                                                                                                                                                                                                                                                |
| `reload`    |                                                                                                           | 重新读取配置文件                                                                                                                                                                                                                                                                          |
| `quit`      |                                                                                                           | 退出 flowkeyd                                                                                                                                                                                                                                                                             |
| `none`      |                                                                                                           | 什么都不做（配合 `swallow` 很有用）                                                                                                                                                                                                                                                       |

简写：`"run:notepad.exe file.txt"`、`"send:^{c}"`、`"type:hello"`、
`"open:https://example.com"`、`"notify:title|body"`、`"volume:up"`、
`"media:next"`、`"clipboard:get"`、`"window:minimize"`、`"window:move_next_desktop"`、
`"desktop:1"`、`"power:sleep"`、`"reload"`、`"quit"`、`"help"`、`"windows"`、`"none"`。

可能有多个窗口匹配；最近使用过的那个（Z 序里最靠前的）胜出，
而已还原的窗口优于最小化的窗口。不可见窗口以及属于别的窗口的弹出窗口
（对话框、工具提示、菜单）会被忽略。匹配会同时使用 `target` 和 `process`，
这正是能可靠找到“那个 WezTerm 窗口”的方式——终端窗口的标题是里面运行的
shell 决定打印的任意内容。

```lua
-- Win+s 唤起 WezTerm，没有它的窗口时先启动它。
hotkey{
  name = "wezterm",
  keys = "Win+s",
  action = window("activate", {
    process = "wezterm",
    launch = {
      program = [[{USERPROFILE}\scoop\shims\wezterm.exe]],
      args = { "start" },
      wait_ms = 5000,
    },
  }),
}
```

`launch` 只在窗口确实不存在时才会尝试，因此一次被拒绝的激活永远不会留下
第二个正在运行的副本。

`op = "activate"` 默认带 `toggle`：要唤起的窗口**已经**在前台时就把它最小化，
于是同一个快捷键在唤起与收起之间切换——和点击任务栏按钮、以及 Windows 自己的
`Win+数字` 一样。已经最小化的窗口不算“已经在前台”，那种情况下按下去依然是
把它恢复出来。只想让快捷键永远把窗口往前抬（比如“随时把日志窗口调到眼前”）就
写 `toggle = false`。

**“已经在前台”还要看虚拟桌面。** 窗口不在你当前那张虚拟桌面上时不算“已经在前台”
（哪怕 Windows 仍然把它记成前台窗口 —— 被 `window_rule` 搬走之后就是这样），
按下去是**切到它所在的那张桌面并激活它**，视图跟着过去。于是某一个程序被
`window_rule` 固定在第 3 张桌面上时，用快捷键唤起它一次就能到位，不必先手动
切桌面；再按一次才是收起（那时它已经真的在前台了）。

（`target` 和 `process` 都不写时目标就是前台窗口本身，于是 `window("activate")`
会变成“最小化当前窗口”——那种场合本来就该写 `window("minimize")`。）

`launch` 那条路径**不**套用 `toggle`：刚启动的窗口往往自己就抢到了前台，
在那种时候把“启动它”变成“立刻收起它”毫无道理。

`launch.program` 是直接交给 `CreateProcess` 的（不像 `open` 那样走
`ShellExecuteW`），所以它**不会**查注册表里的 `App Paths`，也不做 `PATH` 之外的
搜索：Chrome 与 VS Code 这种不在 `PATH` 里的程序要写完整路径。
（`run`/`window.launch` 用 `CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW`，
**不用** `DETACHED_PROCESS`——后者会静默杀死控制台子进程。）

```lua
-- Win+1 激活 Chrome，没有窗口时先启动它。
-- 注意这会吞掉系统原本的 Win+1（任务栏第一个固定项）。
hotkey{
  name = "chrome",
  keys = "Win+1",
  action = window("activate", {
    process = "chrome",
    launch = {
      program = [[C:\Program Files\Google\Chrome\Application\chrome.exe]],
      args = { "--new-window" },
      wait_ms = 5000,
    },
  }),
}
```

`process` 写的是可执行文件名，所以得用那个**真正拥有窗口**的进程名：
微信 4.x 是 `Weixin.exe`（写 `weixin`；`WeChatAppEx.exe` 只是它的小程序子进程），
VS Code 的窗口属于 `Code.exe`。

### 把窗口挪到相邻的桌面 / 显示器

`window` 动作还有四个只针对**当前窗口**（不写 `target`/`process` 就是前台窗口）
的 `op`，用来把窗口搬到相邻的位置：

```lua
-- 只搬窗口，视图不动（和 Windows 自己的 Win+Ctrl+Shift+←/→ 一个语义）
hotkey{ keys = "Win+Shift+u", action = window("move_prev_desktop") }
hotkey{ keys = "Win+Shift+i", action = window("move_next_desktop") }
-- 搬过去，并把视图一起带过去、重新激活那个窗口
hotkey{ keys = "Win+u", action = window("move_prev_desktop", { follow = true }) }
hotkey{ keys = "Win+i", action = window("move_next_desktop", { follow = true }) }
-- 只动显示器
hotkey{ keys = "Win+y", action = window("move_left_monitor") }
hotkey{ keys = "Win+o", action = window("move_right_monitor") }
```

* `move_prev_desktop` / `move_next_desktop` 只动**虚拟桌面**，窗口在显示器上的
  大小与位置完全不变。默认视图也**不**跟着走（移动的是窗口，不是当前桌面；和
  Windows 自己的 `Win+Ctrl+Shift+←/→` 一个语义）。两张桌面**首尾相接**：在
  第一张再往前会到最右那一张，在最后一张再往后会回到第一张。
* 这两个虚拟桌面 `op` 还可以带 `follow = true`：搬完之后把**视图也切到目标
  桌面**，并让那个窗口重新拿到前台（`SwitchDesktop` 激活的是目标桌面上上次
  用过的窗口，不一定是它）。搬窗口与切视图在同一个 shell 会话里一次做完，
  所以不会出现“窗口搬走了、视图还留在原地”的中间态。“保持激活”是尽力而为：
  真抢不到前台时只写一条 warning，动作本身算成功（窗口确实已经搬过去、视图
  也确实跟过去了）。`Win+i` / `Win+u`（不按 Shift）用的就是这个，而
  `Win+Shift+i` / `Win+Shift+u` 只搬窗口 —— 两者的区别就是“把窗口带着一起走”
  与 Windows 自己的 `Win+Ctrl+Shift+←/→`。
  `follow` 只能写在这两个 `op` 上（写在别的 `op` 上会被 `--check` 拒绝），
  且**视图跟随必然伴随一次 `SwitchDesktop`**：如果你更想留在当前桌面，
  就别写 `follow`。
* `move_left_monitor` / `move_right_monitor` 只动**显示器**：按“先左后右、
  再上后下”的排列顺序找相邻那一块（和 `window_rule` 里 `monitor = 2` 的 1
  起序号是同一个排列）。虚拟桌面不变；**最大化窗口在新显示器上仍然最大化**
  （保留 `IsZoomed` 状态），普通窗口保持原有大小并**居中**到目标显示器的工作区，
  最小化的窗口只更新它的还原位置、不会被弹出来。已经在最左/最右那一块时
  按下去只会写一条日志，**不循环**（与虚拟桌面那两条不同）。
* 这四个 `op` 都不套用 `toggle`（不会因为窗口已经在前台就把它收起），也不接受
  `launch`；跨显示器移动会产生窗口过渡，所以 `animate` 对它有意义，而跨虚拟
  桌面移动不产生过渡、写 `animate` 会被 `--check` 拒绝。`follow` 也不影响
  这一条：它只决定要不要切视图，不产生窗口过渡。
* 底层：跨桌面走 shell 未公开的 `IVirtualDesktopManagerInternal::MoveViewToDesktop`
  （与 `window_rule` 的 `desktop` 同一套），跨显示器走 `SetWindowPlacement`
  （与 `window_rule` 的几何摆放同一套），所以两者的已知限制也一样。

### 前台窗口与覆盖层

不写 `target`/`process` 的 `window` 动作作用于**前台窗口**（`GetForegroundWindow()`），
但有一个例外：如果它是个**覆盖层**（`WS_EX_TOOLWINDOW`），flowkeyd 会沿 Z 序往下
找到第一个真正的主窗口（可见、无属主、非工具窗口、有标题、尺寸非零）再动手。

最典型的覆盖层是 **PowerToys 的「快捷键指南」**（`PowerToys.ShortcutGuide.exe`）：
按住 Win 约一秒它就会弹出来并成为 `GetForegroundWindow()`。以前这时按
`Win+i` / `Win+u` 这类“对前台窗口”的快捷键会去操控那个覆盖层（它不属于任何
虚拟桌面，跨桌面移动会报 `could not read the desktop id of the window`），
现象就是**按住 Win 连按下一个和弦没反应、把 Win 和那个键都松开再按才行**。
现在 flowkeyd 会跳过覆盖层，继续作用在你刚才那个窗口上。

### 窗口动画

flowkeyd 触发的窗口状态变化默认**不播放动画**：`minimize` 不再“缩”到任务栏，
`maximize`/`restore`/`activate` 也不再伸缩，窗口直接到位。写 `animate = true`
可以给某一条绑定把动画要回来：

```lua
hotkey{ keys = "Ctrl+Alt+f", action = window("maximize", { animate = true }) }
```

实现用的是**按窗口**的 `DwmSetWindowAttribute(hwnd,
DWMWA_TRANSITIONS_FORCEDISABLED, TRUE)`：调 `ShowWindow` 前设上、调完立刻设回，
所以

* **不改系统设置。** 系统里“辅助功能 / 视觉效果 → 最小化/最大化时播放动画”
  （`SPI_SETANIMATION`）不会被碰，其它程序、以及你手动点标题栏按钮时的动画
  都不受影响。
* 只有会改变窗口状态的 `op` 有这个效果；`close`、`toggle_topmost` 与跨虚拟桌面
  移动（`move_prev_desktop`/`move_next_desktop`）不产生过渡，写 `animate` 会被
  `--check` 拒绝；跨显示器移动（`move_left_monitor`/`move_right_monitor`）会改变
  几何，`animate` 对它有意义。
* 这个属性读不回来（`DwmGetWindowAttribute` 对它返回 `E_INVALIDARG`），
  所以 flowkeyd 在调用后只能把它设回 `FALSE`（即“按默认，带动画”）。
  `dwmapi.dll` 是用运行时解析的（见 `src/platform/win/dwm.cpp`），拿不到它时
  只是保留动画，动作不会失败。
* 动画这类**只能看得见的效果**只能靠肉眼：本项目没有屏幕采样脚本，
  改这块之后请自己最小化/还原一次看看。

### 选单与电源

`menu` 弹出一个 **QML（FluentWinUI3）的无边框圆角卡片**。列表是 Qt 自带的
`ListView` + 标准的 `ItemDelegate`（和帮助窗口同一套），所以悬停、按下与高亮
全部由 FluentWinUI3 的标准样式画，鼠标点击与滚轮也是标准列表控件的行为：

```lua
hotkey{
  name = "power-menu",
  comment = "Win+x：电源选单（S 睡眠 / P 关机 / R 重启 / L 锁定 / O 关屏 / Esc 关闭）",
  keys = "Win+x",
  action = menu{
    title = "电源",
    items = {
      { key = "s", label = "睡眠", hint = "Sleep",     action = power("sleep") },
      { key = "p", label = "关机", hint = "Shut down", action = power("shutdown") },
      { key = "r", label = "重启", hint = "Restart",   action = power("restart") },
      { key = "l", label = "锁定", hint = "Lock",      action = power("lock") },
      { key = "o", label = "关闭屏幕", hint = "Screen off", action = power("screen_off") },
      { label = "取消", hint = "Esc", action = none() },
    },
  },
}
```

* **键盘**：条目上的 `key`（一个字符，不区分大小写）直接选中它；`↑`/`↓` 移动高亮、
  `Enter` 执行高亮的条目、`Esc` 关闭。
* **鼠标**：悬停到哪一行，高亮就在哪一行（`Enter` 执行的就是它）；**左键单击一行
  = 执行它**；点到别的地方也会把选单关掉（和系统菜单一样）。指针离开卡片之后
  高亮回到键盘选中项。选单不滚动（条目数决定卡片高度），所以滚轮在它上面不做事，
  也不会把高亮从你的光标下拿走。
* 不写 `action`（或写 `none()`）的条目只是把选单关掉；不带 `key` 的条目只能鼠标/方向键选。
* 选中的动作照旧在**工作线程**上执行：窗口在 Qt GUI 线程上创建，用户选中后
  “选了第几项”回投给工作线程，所以条目里可以放任何动作，不只是 `power`。
* **配色跟随系统**：卡片全部走 Qt 的 `palette`（浅色/深色主题都会跟着变）。
  窗口尺寸、内边距、圆角与字号按所在显示器的 DPI 缩放；Qt 6 在 Windows 上默认
  就是 per-monitor DPI Aware V2。
* **中文字体是微软雅黑**（西文也跟着用 YaHei 自带的字形）：FluentWinUI3 默认的族
  是 `Segoe UI Variable`，它没有中文字形，不指定的话中文会落到宋体上，跟旁边的
  西文摆在一起很违和。字体是写死的，不跟随系统字体设置（`help` 窗口同）。
* 一次只会有一个选单：再按一次快捷键只是把它拿到前面。
* **卡片不会出现在任务栏里**（也不会进 `Alt+Tab`）：它是一个 `Qt.Tool` 窗口
  （= Windows 的 `WS_EX_TOOLWINDOW`），按完就没的东西不该在任务栏里留一个按钮。
  `help` 与窗口切换器是同一套标志，而且在启动时就已经预热好了（见
  [工作原理](#工作原理)）。

`power` 的取值：`sleep`（睡眠；现代待机的机器上就是“屏幕关掉、系统继续待机”）、
`hibernate`（休眠，先把内存写进磁盘）、`shutdown`、`restart`、`logoff`（注销）、
`lock`（锁定）、`screen_off`（关屏）。睡眠/休眠走 `powrprof!SetSuspendState`，
关机/重启/注销走 `user32!ExitWindowsEx`，只带 `EWX_FORCEIFHUNG`（只强杀已经卡住、
不响应 `WM_QUERYENDSESSION` 的程序），**不会**用 `EWX_FORCE`：有未保存内容的程序
照样会弹它自己的确认框，所以这个“关机”不会比开始菜单里的那个更粗暴。

`screen_off` 走的是 Windows 自己那条“屏幕超时后关屏”的路径——`WM_SYSCOMMAND` +
`SC_MONITORPOWER` 广播给所有顶层窗口（`SendMessageTimeoutW`，免得被某个卡住的窗口
挂住）——所以它只把**全部**显示器送进待机，系统、应用和 flowkeyd 的钩子都继续照常
运行（**不是**睡眠，也不锁屏）。点亮屏幕由内核的输入电源策略负责：随便按一个键或
动一下鼠标，显示器就回来了，这与 flowkeyd 的钩子吞不吞那个键无关，也不需要任何
额外的动作。

关机、重启、注销需要管理员权限（flowkeyd 默认就是提权运行的）；没提权时会在日志里
写一行错误并弹一个托盘气泡说明原因。睡眠、休眠、锁定与关屏不需要权限。

上面那条 `Win+x` 会吞掉 Windows 自己的“快捷链接菜单”，和例子里的 `Win+s` 取代系统
搜索是同一回事；想保留系统菜单就换一个键。

### 快捷键帮助

`help` 弹出同一套 QML 卡片，但它不是让你挑一项，而是把**当前配置里全部生效的
快捷键**列出来：左边是按键徽标，右边是配置里的 `comment`（没写就用 `name`）
和一行灰色小字（这个快捷键到底会做什么，由动作摘要生成）。

```lua
hotkey{
  name = "help",
  comment = "Win+/：列出当前所有快捷键（可输入筛选、Enter 执行）",
  keys = "Win+/",
  action = help(),
}
```

* **筛选**：筛选框就是一个普通的输入框（Qt 的 `TextField`）：**用鼠标点一下就能
  进去打字**，光标、选区、输入法、右键菜单、`Home`/`End`/左右箭头都是标准输入框
  的行为。输入就按子串过滤，和弦、`comment`/`name`、动作摘要都参与匹配；
  窗口会跟着结果变矮（顶边不动，所以不会跳），标题右侧显示 `可见 / 总数`。
  `Esc` 依次是「取消待确认 → 清筛选 → 关窗」。
* **滚动**：`↑`/`↓`、`PgUp`/`PgDn` 移动**键盘选中项**（高亮就是它）；鼠标滚轮与
  右侧的滚动条（Qt 自带的，**可以拖**）只滚视图，**不会**动键盘选中项 ——
  拖动滚动条时高亮不会跟着指针乱跳。条目比窗口高时滚动条才出现。
* **鼠标**：把鼠标移过某一行**不会**改变高亮；**左键点某一行 = 选中它 + 把它的
  按键文本复制走**（和系统列表控件一样，点选是唯一用鼠标改选中的方式）；
  **双击某一行 = 选中它并执行它的动作**。
* **执行**：`Enter`（或双击）= **执行键盘选中项那一行**。窗口会先关掉再执行 ——
  这样 `send`/`type`/`window` 这类动作作用在原来的前台应用上，而不是打回帮助
  窗口自己的筛选框。触发的效果等价于按一下那个快捷键（先执行按下时的动作、
  再执行松开时的动作）；重映射那一行等价于按一下源键
  （`CapsLock → Esc` 就会注入一次 `Esc`）。
* **危险动作要两次**：`quit`、`suspend` 与 `power`（睡眠/关机/重启/注销/锁定/关屏）
  这几类不会一按就执行 —— 第一次 `Enter`/双击只是把它标成「待确认」
  （那一行变色，底部提示换成确认文案），再按一次才真的执行。
  `Esc`、上下换行、改筛选都会取消确认。
* **复制**：左键点某一行把它的按键文本复制到剪贴板，例如 `Win+s`。
* 列表由配置本身生成（`--list` 看的就是同一批绑定，加上重映射），
  所以 `help()` 不需要任何参数；卡片顶部的标题可以换：`help("我的快捷键")`。
* 一次只会有一个帮助窗口：再按一次快捷键只是把它拿到前面并清空筛选。
* **卡片不会出现在任务栏里**（也不会进 `Alt+Tab`），与选单、窗口切换器同一套
  窗口标志（`Qt.Tool`）。
* 和 `Win+x`/`Win+s` 一样，`Win+/` 会吞掉 Windows 自己的那个快捷键
  （表情/输入法面板）；想保留就换一个键。

### 窗口切换器

`windows()` 弹出一张卡片，列出**当前所有打开的程序窗口**（标题 + 进程名），
输入进程名前缀就筛选、选中就切过去：

```lua
hotkey{
  name = "window-switcher",
  comment = "轻碰一下 Win：切换窗口",
  keys = "LWin",
  trigger = "release",
  action = windows(),
}
```

* **卡片没有标题行**：筛选框就是卡片的第一行，窗口列表紧跟在它下面而且
  **与筛选框一样宽**（行的高亮底不再比输入框宽出一截）；「N / M 个窗口」的
  计数在底部提示里，与键盘提示同一行。`windows("切换窗口")` 给的名字只用作
  **窗口标题**（卡片是无边框的，界面上看不到它；外面用它辨认这个窗口）。
* **一打开就把输入法切成英文**（鼠标点回筛选框时也会再确认一次）：筛选框匹配的
  是进程名（`chrome.exe` 这种），而用户经常正开着中文输入法 —— 那样打进去的是
  候选字，一条都筛不出来。**关掉卡片时会把打开前的模式写回去**：这个模式是本
  进程里这几个窗口共用的（实测在帮助窗口里按 `Shift` 切成中文之后，切换器窗口
  读到的也是中文），不还原的话你接着打开帮助 / 日志窗口打字时就变成英文了。
  它只影响 flowkeyd 自己这个**进程**，**不影响**你在别的应用里的中/英文状态；
  想在卡片里打中文仍然可以自己按 `Shift` 切过去（关掉卡片时会被还原成打开前的
  状态，所以不会留下痕迹）。
* **筛选**：筛选框就是一个普通的输入框（Qt 的 `TextField`，鼠标点一下就进去）。
  输入按**进程名的前缀**过滤（大小写无关）：打 `chr` 列出所有 Chrome 窗口，
  打 `flow` 列出 `flowkeyd.exe`；窗口标题只显示、**不参与匹配**。
* **自动激活**：筛选结果**只剩一个窗口**时直接激活它并把卡片关掉 —— 不必再按
  `Enter`。刚打开（筛选框为空）时不会自动激活，哪怕只有一个窗口。
* **数字选择模式**：筛选串命中的窗口**全属于同一个进程名**、而且不止一个时，
  每一行左边会出现一个数字快捷键 —— 前 10 行依次是 `1`..`9`、`0`，按数字就
  直接跳到那个窗口（超过 10 个的窗口不分到按键）。这正是「打 `chr` 筛出好几只
  Chrome 窗口」的场景：标题不参与筛选，在那里打不下去了，数字键是最省事的
  第二段输入。那种模式下数字归快捷键，**不会跑进筛选框**；没有对应行的数字
  （比如只有 3 个窗口时按 `0`）什么都不做。
* 多条时用 `↑`/`↓` 选择后 `Enter` 切换，或者把鼠标悬停在某一行（悬停即高亮）
  再左键单击；`Esc` 关掉卡片。窗口多于屏幕能放下的行数时右侧的 `ScrollBar`
  可以拖。数字选择模式下同样可以用这两套，不强制按数字。
* 列表按 Z 序（最近用过的在前）排列，**跨虚拟桌面的窗口也会列出来**：选中它会把
  视图切到那张桌面并激活它（与 `window` 动作的跨桌面唤醒同一条路）。
  工具窗口、没有标题的窗口、尺寸为空的窗口，以及 flowkeyd 自己的弹窗 / 日志窗口
  不在列表里。**被 Windows 藏起来的“假窗口”也不会列**：有些程序（最典型的是
  Windows 输入法的宿主 `TextInputHost.exe` 的「Windows 输入体验」）有一个
  `IsWindowVisible` 为真、坐标与尺寸都正常的顶层窗口，但它其实从来没有显示到
  屏幕上（DWM 的 cloaked 标记说得很清楚），切过去也没有意义 —— 这正是
  “只列真正有窗口的进程、与 `Alt+Tab` 同一套判据”。注意**在别的虚拟桌面上的
  窗口是另一种情况，仍然会列**（激活时会切过去）。
* 简写 `action = "windows"` 等价于 `windows()`（标题那个参数见上面第一条）。
* **「轻碰 Win」= 单个修饰键 + `trigger = "release"`**。它的语义是：按下 Win
  本身**照常传给系统**（所以 `Win+E`、`Win+L` 这些没被 flowkeyd 接管的系统组合
  完全不受影响），期间没有按过别的键、松开时才触发；触发时注入一个未分配的标记
  按键，挡掉 Windows 自己的开始菜单。也就是说：**只有“单独按一下 Win”被换成了
  窗口切换器**。单个修饰键配默认的 `trigger = "press"` 仍然是老行为，
  `Ctrl+Shift` 这类“只有修饰键的和弦”也不受影响。
* **卡片不会出现在任务栏里**（也不会进 `Alt+Tab`），与选单、帮助同一套窗口标志
  （`Qt.Tool`）。

### 模板

动作里的字符串会在执行时展开：

| 占位符                                                          | 取值                                               |
| --------------------------------------------------------------- | -------------------------------------------------- |
| `{clipboard}`                                                   | 剪贴板文本（按需读取）                             |
| `{selection}`                                                   | 选中文本：flowkeyd 发送 Ctrl+c、等待，然后读取剪贴板 |
| `{hotkey}` / `{name}`                                           | 快捷键的名字                                       |
| `{date}`、`{time}`、`{datetime}`、`{timestamp}`                 | 本地时间（`{timestamp}` 适合做文件名）             |
| `{unix}`                                                        | 自纪元起的秒数                                     |
| `{config_dir}`、`{exe_dir}`、`{cwd}`、`{temp}`、`{USERPROFILE}` | 路径                                               |
| `{env:NAME}`                                                    | 环境变量                                           |
| `{{` / `}}`                                                     | 字面大括号                                         |

未知占位符会原样保留，因此命令行里的字面 `{` 不会被吃掉。

### `remap{ ... }`

```lua
remap{
  from = "CapsLock",     -- 一个按键名或一个和弦
  to = "Esc",            -- 一个按键名，或者 "^{c}" 这样的发送脚本
  mode = "hold",         -- hold（默认）或 tap
  swallow = true,        -- 默认取 settings.swallow
}
```

`hold` 在 `from` 按下时按下目标、在 `from` 松开时松开目标，
这正是 `CapsLock -> Esc` 或 `CapsLock -> Ctrl` 想要的行为。
`tap` 每次按下只执行整个脚本一次。

`to = "Esc"` 表示 Escape 键，与 AutoHotkey 的 `CapsLock::Esc` 完全一致；
`to = "hello"` 是一个脚本，会输入五个字母。

### `window_rule{ ... }`

按程序摆放窗口：某个程序的窗口**第一次出现**时，把它放到指定的虚拟桌面 /
显示器上。

```lua
window_rule{
  name = "wezterm",      -- 可选，日志与 `--list` 里的名字
  process = "wezterm",   -- 可执行文件名子串（与 `window` 动作的 process 一致）
  title = "项目",        -- 可选，窗口标题子串；两个都给时都要满足
  desktop = 2,           -- 可选，虚拟桌面序号（1 起，Task View 顺序）
  all_desktops = true,   -- 可选，钉在所有虚拟桌面上（与 desktop 互斥）
  topmost = true,        -- 可选，始终在最上层
  monitor = 2,           -- 可选：2 / "primary" / "DISPLAY2"
  -- 下面这些不写时的默认是「铺满目标显示器的工作区」
  maximize = false,      -- 可选
  x = 0, y = 0,          -- 可选，相对目标显示器工作区左上角（像素）
  width = 1280, height = 800,  -- 可选，像素
  enabled = true,        -- 可选
}
```

* `process` / `title` 至少要写一个：前者匹配可执行文件名（写 `wezterm` 能匹配
  `wezterm-gui.exe`），后者匹配窗口标题，都是大小写无关子串。拿不到属主进程名
  （访问被拒）时 `process` 不算匹配。
* `desktop` 是 1 起的虚拟桌面序号（Task View 从左到右）。
* `monitor` 有三种写法：
  * `2` —— 1 起的序号，按显示器排列「先左后右、再上后下」；
  * `"primary"` —— 主显示器；
  * `"DISPLAY2"` —— `EnumDisplayMonitors` 的设备名（也可写全名 `\\.\DISPLAY2`）。

  找不到匹配的显示器时只记一条 warning，窗口留在原处（例如笔记本没插外接屏时）。
* `all_desktops = true` 把窗口**钉在所有虚拟桌面上**（Task View 里的「在所有桌面
  显示」）：切到哪张桌面都看得见它。`false` 是显式取消钉住，不写就不去碰它。
  它与 `desktop` 互斥（窗口既然在每张桌面上，就没有「搬到第几张」可言），
  两个都写会被 `--check` 拒绝。
* `topmost = true` 让窗口**始终在最上层**（`WS_EX_TOPMOST`）；`false` 是显式
  取消置顶，不写就不去碰它。
* `all_desktops` 与 `topmost` 都不算“几何”：只写它们（没写 `monitor`）时窗口的
  大小与位置保持不动，也不会因为默认最大化而突然变大。
* **默认最大化。** 只写了 `monitor`（而没写位置 / 大小）时，窗口会铺满那块显示器的
  **工作区**（扣掉任务栏）。只写 `desktop` 时窗口的大小与位置保持不动；
  `maximize = true` 与 `x`/`y`/`width`/`height` 不能同时写。
* `x`/`y` 是相对**目标显示器工作区左上角**的偏移，`width`/`height` 是像素；
  没给的项保持窗口原来的大小 / 居中。窗口比工作区还大时对齐工作区左上角，
  保证标题栏可见。
* 同一个窗口只处理一次：已经摆放过的窗口再次显示（从托盘还原、最小化后还原）
  不会再摆一次。规则按书写顺序匹配，**先写的赢**；两条规则匹配同一批窗口时
  `--check` 会给一条 warning。

**触发时机只有三个**：

1. 窗口第一次出现；
2. 之前断开的显示器重新接上（设备名从无到有）；
3. flowkeyd 启动时，对当前已经存在的窗口过一遍。

之后**不再干预**：你自己移动 / 缩放窗口、取消钉住、取消置顶都不会被纠正，
直到下次显示器重新接入或者重启 flowkeyd。被当成“主窗口”的条件是：可见、
没有属主、不是工具窗口（`WS_EX_TOOLWINDOW`）、有标题、尺寸非零 ——
应用的内部辅助窗口不会被误摆。

**规则真的把窗口移到了另一张桌面时，视图也跟着过去**（只限“窗口第一次出现”
那一遍）：flowkeyd 会切到目标桌面并重新激活那个窗口，于是“启动它”一次就能看到
它 —— `Win+3` 唤起 WPS 不会看起来毫无反应。启动与显示器重新接入那两遍只是重新
摆放已经在位的窗口，从不切视图；窗口本来就在目标桌面上时（例如一个已经在第 3 张
桌面的程序又开了一个窗口）也不算“搬迁”，同样不切。

`desktop` 走的是 shell 内部的 `MoveViewToDesktop`（已公开的
`IVirtualDesktopManager::MoveWindowToDesktop` 拒绝移动别的进程的窗口）。
窗口会先在当前桌面上出现一瞬间，然后被移到目标桌面。

### `app{ ... }`

把**同一个程序**的窗口摆放规则、启动参数与快捷键写在一起，省掉重复的 `process` /
`title` / `launch`。它本身不是新能力：加载配置时会展开成一条普通的 `window_rule`
与若干条普通的 `hotkey`，排在同一次注册顺序的最后。

```lua
app{
  name = "wps",                  -- 可选，默认用 process（再退到 title）
  process = "wps",               -- 与 window_rule / window 动作的 process 一致
  title = "WPS",                 -- 可选，窗口标题子串
  enabled = true,                -- 可选，false 表示整条 app 都忽略
  launch = {                     -- 可选：这个程序怎么启动（与 window 动作的 launch 字段一样）
    program = [[C:\tools\ksolaunch.exe]],
    args = { "/prometheus" },
    wait_ms = 10000,
  },
  window = {                     -- 可选：window_rule 的全部字段
    desktop = 3,
    monitor = 2,
  },
  hotkeys = {                    -- 可选：与 hotkey{} 字段完全相同
    {
      keys = "Win+3",
      -- launch 自动继承，所以只写要覆盖的部分（这里把等待时间改成 5 秒）
      action = window("activate", { wait_ms = 5000 }),
    },
  },
}
```

* `process` / `title` 至少要写一个（与 `window_rule` 一样）。
* `window` 就是一条 `window_rule`，字段完全一样；`process` / `title` / `name`
  自动继承，显式写在 `window` 里的优先。不写 `window` 就只展开快捷键。
* `launch` 就是这个程序怎么启动，字段与 `window` 动作的 `launch` 完全一样
  （`program`、`args[]`、`cwd`、`show`、`shell`、`env{}`、`wait_ms`）。
  `hotkeys` 里的 `window()` 动作会自动继承它；动作自己写了 `launch` 时**逐字段合并**
  （写了的覆盖，没写的继续继承），动作顶层的 `wait_ms` 覆盖 `launch.wait_ms`。
  于是最常见的写法就是 `action = window("activate")`。
* `hotkeys` 里每一项就是一条 `hotkey`；其中的 `window()` 动作会自动补上 app 的
  `process`（写到动作的 `process`）与 `title`（写到动作的 `target`），以及 app 的
  `launch`，显式写的优先。
  嵌套在 `menu` 条目里的 `window()` 动作同样继承。**只对表 / 构造器形式的
  `window` 动作生效**：简写字符串（`"window:activate"`）里没有可继承的字段。
* 名字：`name` 不写时用 `process`，再退到 `title`。这个名字会给展开出来的
  `window_rule` 用；app 里**只有一个 hotkey** 时也用它当这条绑定的默认名字
  （多个时用第一个和弦，免得重名）。`--list` 的快捷键那一列显示的仍然是和弦，
  但日志、`{name}` 模板与帮助窗口（没写 `comment` 时）用的是这个名字。
* `enabled = false` 把整条 app（规则 + 全部快捷键）都丢掉，并给一条 warning。
* 出错时的标签是 `app #1 (\`wps\`)`：不写 `name` 就用 `process`（再退到 `title`）。

**与全局条目的关系**：`hotkey{}` / `window_rule{}` 仍然照旧可用 —— 没有窗口规则的
快捷键（例如 `Ctrl+Alt+F4` 退出）、或没有快捷键的规则（例如只把某个程序钉在所有
桌面上），继续单独写。app 展开出来的条目排在全局条目**之后**（同一次注册顺序的
最后）：快捷键冲突时先注册的赢，窗口规则仍然是先写的赢，所以全局规则会先于
app 规则匹配。

## 工作原理

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

真正重要的设计要点：

* **钩子回调永不阻塞。** 它解码事件、问引擎要一个决定、注入重映射按键
  （已预先拆好，就是几条 `SendInput` 记录），其余一切排队给工作线程。
  回调超时 `LowLevelHooksTimeout`（默认 300 ms）的低级钩子会被 Windows
  静默移除——守护进程看起来还活着，却什么都不做。
* **注入事件被忽略。** flowkeyd 合成的每个事件都在 `dwExtraInfo` 里带着 `"FLOW"`；
  回调在触碰任何状态之前就把它们丢开，这也让 `SendInput` 从回调内部造成的
  重入变得安全。由*其它*程序注入的事件同样被忽略（`LLKHF_INJECTED`），
  因此 flowkeyd 永远不会对自动化脚本作出反应。
* **钩子装在独立线程上。** QML 渲染的一次慢帧、以及托盘菜单弹出时跑的模态循环
  都可能让主线程几十毫秒不回来，300 ms 的预算经不起这种抖动。独立线程还有第二个
  好处：钩子的生命周期与 Qt 事件循环解耦，`quit` 时能确定地先卸钩子再退循环。
* **自动重复不是快捷键触发。** 被按住按键的第二次及之后的 key-down 会被识别为
  操作系统的自动重复：如果最初那次按下被吞掉了，它们也会被吞掉，但绝不会重新匹配。
  也就是说默认的 `trigger = "press"` 每次按下只派发一次；需要按住重复时显式写
  `trigger = "repeat"`（或 `repeatable = true`），那套重复是 flowkeyd 自己的、
  由 `SetTimer`/`WM_TIMER` 驱动。
* **触发时机默认是按下。** 和弦的最后一个按键一到就派发动作，包括被吞掉的
  `Win+…` 和弦——`Win+s` 唤起窗口不会等到你松开 Windows 键。遮断标记（见
  [已知限制](#已知限制)）仍然在 Windows 键松开时注入，那是外壳唯一会看它的时刻。
* **发送前后会释放修饰键。** 否则 `Ctrl+Alt+t -> send:^{c}` 会发出 Ctrl+Alt+c。
  flowkeyd 会松开你按住的修饰键、注入、再按回去，并且按注入层的规矩在中间补一次
  菜单遮断空按键（`ModifierGuard` 注入的真实 key-up 引擎看不到）。
* **日志窗口是同一个进程里的 QML 窗口。** 它只通过日志文件与守护进程交接：
  `LogModel` 每 250 ms 按字节读一次文件，只消费能完整解码的 UTF-8 前缀，
  半行留到下一轮，最多 1000 行。关闭它不退出应用（`setQuitOnLastWindowClosed(false)`）。
  这与 oskeyd 有意不同：oskeyd 做成独立进程，是因为它的日志窗口就是守护进程自己
  的控制台，用户点叉会给它发 `CTRL_CLOSE_EVENT`；Qt 窗口没有这个问题。
* **选单与帮助窗口跑在 Qt GUI 线程上。** 钩子线程只把动作排给工作线程，
  工作线程向 GUI 线程发一个队列信号请求建窗并立刻返回；用户选中（或按 `Enter`/
  双击帮助里的一行）后，那一项的动作再回投给工作线程执行——两个窗口自己从头到尾
  不执行动作（帮助窗口只是在把活儿交出去之前先把自己藏起来，好让 `send`/`type`
  打在原来的前台应用上）。因为守护进程通常不持有前台锁，弹窗抢焦点走的是
  `requestActivate()` → `SetForegroundWindow` → `AttachThreadInput` 的三级绕行。
* **弹窗在启动时就已经预热好了。** 进程第一次渲染要初始化 QRhi/D3D11、建交换链、
  编译 Quick 自己那批材质着色器，冷启动实测要 **220 ms 左右**才能画出第一帧 ——
  用户看到的就是“第一次按快捷键卡一下”。所以守护进程启动、事件循环一开始转，
  就把三个卡片窗口建出来、填一份假数据各渲染一帧（**全透明 + 屏幕外**，用户
  看不到也点不到），然后藏起来；真正弹出时只剩 `setItems` + 显示 + 首帧，
  实测 **~30 ms**（与第二次、第三次一样）。三个卡片也因此不进任务栏
  （`Qt.Tool`）。
* **托盘图标上的数字来自一次 500 ms 的轮询。** 虚拟桌面没有任何“切换了”的通知
  （Windows 自己的 `Win+Ctrl+←/→`、`desktop` 动作、以及 `window_rule` 跟随窗口时的
  视图切换都会改它），所以动作线程上有一个定时器在问
  `IVirtualDesktopManagerInternal` 当前是第几张桌面，变了就投回 GUI 线程把图标
  换成对应的数字（画什么字由 `core::desktopBadgeText` 决定，画出来是
  `app::desktopIcon`）。查不到时保留上一次的数字，不会来回闪。
* **未公开 API 是可选的，并且会被探测。** `win32u!NtUserSendInput`、
  `NtUserGetAsyncKeyState` 用 `LoadLibraryW`/`GetProcAddress` 解析，
  `NtUserSendInput` 在使用前会用一次零输入调用验证。任何失败都会回退到 `user32`。
  本机实测默认选中 `win32u!NtUserSendInput`。
* **虚拟桌面走一次性 STA 线程。** 那些 shell 接口要 STA，而 Core Audio 要 MTA；
  与其让工作线程的单元模型取决于哪个后端先初始化，不如给每次桌面调用一条
  干净、用完即弃的 STA 线程。Qt 在 Windows 上会把主线程初始化成 STA（给 OLE/拖放），
  所以音频**绝不能**在主线程上初始化。
* **运行期没有第三方依赖。** Win32 声明是手写的（`src/platform/win/ffi.h`），
  Lua 5.5.1 静态编进二进制（`vendor/lua`），UI 用 Qt 自带的 QML 模块。
  刻意不引入会藏起未公开入口的封装层。

## 验证它能工作

```powershell
$C = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $C --build --preset debug
& $C --build --preset release
& ctest --test-dir build/windows-debug --output-on-failure     # 23 个测试目标

# 真实桌面后端（剪贴板 / 音量 / 窗口 / 虚拟桌面）
$env:FLOWKEYD_ALLOW_INTERACTIVE_TESTS = '1'
& build\windows-debug\tst_interactive.exe
```

* **单元测试**（Qt Test，23 个目标）覆盖按键/和弦解析、发送脚本解析、模板展开、
  配置校验（含 `menu` 的结构与条目动作、`help` 的标题、`window_rule` 的字段与
  几何默认值）、Lua 脚本的求值与转换
  （两种写法、DSL 构造器、内联函数被拒绝、空动作列表、UTF-8 BOM、`menu`/`power`/
  `help`/`window_rule` 助手）、整个匹配状态机（优先级、吞键、重复、挂起、
  重映射 hold/tap）、
  选单与帮助窗口的几何/筛选/选中项/计数/危险动作的两次确认、日志尾随（增量、
  半行、被截断的多字节
  UTF-8）、跨 `SendInput` 的结构体布局（`INPUT` 必须是 40 字节）、
  音量步进/钳位、窗口匹配、显示器选择与窗口摆放几何（`tst_placement`）、
  托盘数字徽标的文字与字号（`tst_desktop_badge`）、
  电源与虚拟桌面的纯逻辑表。
* **交互式测试**（`FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`，默认 skip，14 个用例）真的碰
  这台机器的剪贴板/音量/前台窗口：剪贴板往返、音量读写与钳位并恢复原值、
  启动记事本并按标题找到它、激活→最小化→恢复→关闭（这一步用测试进程自己的
  顶层窗口，理由见下）、`{selection}` 的 Ctrl+c 往返、
  虚拟桌面的只读探测 + 一次可逆的切换（切走再切回来；托盘数字图标用的那个
  精简接口必须与 `probe()` 给出同一组数字），
  以及 `window_rule` 的真机验证：把窗口移到另一个虚拟桌面再移回来
  （用**已公开**的 `GetWindowDesktopId` 确认桌面 GUID 真的变了）、
  跨桌面唤醒（把窗口搬到别的桌面后 `window::isActive` 必须为假、
  `window::raiseWindow` 必须把视图切回去并让它拿到前台 —— 本机实测
  `GetForegroundWindow()` 那时还指着那个看不见的窗口），
  把窗口摆到另一块显示器并最大化再还原。
  **“窗口后端”的那些断言作用在测试进程自己创建的顶层窗口上**（标题可控、进程
  独占、`WM_CLOSE` 就是销毁），不用记事本 —— 这台机器的记事本已经是单实例、
  带标签页与会话恢复的 Store 应用，拿它当载体时“关闭之后窗口消失”之类断言
  已经不可靠。
  **电源动作一个都不会被自动化测试触碰**：关机/重启/注销/睡眠/休眠/锁定/关屏
  都不进测试（`tst_power_table` 只测纯逻辑表，不碰真实调用），
  只能由你自己按键或点选单验证。
* **验收脚本**（`scripts\acceptance.ps1`）是“钩子真的吞了键”那类结论的**外部**
  证据：它用一个一次性配置起一个非提权的守护进程，从另一个上下文用 `SendInput`
  注入按键，再用一个获得焦点的 WinForms 窗口观察按键到底有没有到达前台
  （未绑定的键做正对照，所以“焦点没拿到”不会被误会成“吞键成功”）。
  它跑 116 项检查：吞键、被吞掉的 `Win+s`（常规 / 0 ms 轻按 / 一次、两次
  Windows 键自动重复）、自动重复只派发一次、重映射 hold/tap/`CapsLock -> Esc`、
  `send` 的修饰键释放、小键盘与主键盘互不触发、`window` 的
  启动→激活→收起→恢复、`menu` 弹窗的键盘选择、**鼠标悬停与左键单击一行**、
  鼠标滚轮、`help` 弹窗的鼠标语义与筛选（点筛选框、点列表项、拖滚动条、
  滚轮都只滚视图不改选中项）、
  **`Enter`/双击真的执行了选中那一行的动作**（先关窗再执行）、
  **危险动作的两次确认**（第一次 `Enter` 不执行、`Esc` 取消；用可逆的 `suspend`
  验证第二次真的执行）、`suspend`/`resume`/`reload`/`quit`，
  以及最后没有按键卡在按下状态。

  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1
  powershell.exe ... -Phase config                              # 只看配置，不注入按键
  ```

  它需要交互式桌面会话，并且约两分钟里会**持续注入按键、抢焦点**。

  钩子默认丢弃一切带 `LLKHF_INJECTED` 的事件，脚本伪造不了物理按键 ——
  所以守护进程用 `FLOWKEYD_ACCEPT_INJECTED=1` 启动，那是**只给测试用的后门**
  （与 oskeyd 的 `OSKEYD_ACCEPT_INJECTED` 同款），启用时日志里有一条警告。
  flowkeyd 自己注入的按键带着 `"FLOW"` 标记，在钩子回调的第一步就被丢掉，
  所以抬升这道过滤不会让重映射自己喂自己。
* **只能看得见的效果**（`animate`、弹窗的配色与布局）只能靠肉眼：
  改这块之后请自己试一遍。

触碰真实用户状态的测试默认跳过；它们不会在普通 `ctest` 里运行。

## 已知限制

* 配置文件是**脚本**，不是数据：`--check` 会执行它（`print` 会打到 stdout，
  `os.execute` 之类也真的会跑）。请只跑你信任的配置。
* **动作不能是 Lua 函数。** 这不是技术限制，而是刻意的：动作要能被 `--list`
  显示、要能在加载时校验完、还要在钩子回调与工作线程的边界上保持安全。
  想要“自定义逻辑”就用循环与表达式去生成声明式动作。
* DSL 报错时，如果那次 `hotkey{}`/`remap{}`/`settings{}` 调用正好是脚本的**最后
  一条语句**，Lua 的尾调用会丢掉调用者栈帧，错误信息里因此没有行号
  （消息本身仍然指名了出错的构造）。在它后面随便再写一条语句就能拿回行号。
* 快捷键的修饰键仍然会被送达到前台应用；只有和弦的最后一个按键被隐藏。
  AutoHotkey 会通过缓存修饰键按下、并在没有快捷键成形时重放来隐藏整个和弦。
  这是与 AutoHotkey 的主要行为差异，也是下一件要做的事。
* 重映射里的 `{Sleep}` 会被忽略（会睡觉的钩子会被 Windows 移除）；
  `--check` 会对它给出警告。
* 小键盘只区分了 `Enter`。`NumLock` 关闭时，小键盘的
  `8`/`2`/`4`/`6`/`0`/`.`/`Home`/`End`/`PgUp`/`PgDn` 会上报与主键盘方向键、
  `Insert`、`Delete` 相同的 `VK`（同样只能靠扩展键标志区分，而 flowkeyd
  目前没有为它们定义名字），因此绑定 `Up` 也会被小键盘的 `8` 触发。
  小键盘的 `-`/`+`/`Enter` 不受影响：前两个本来就是独立的 `VK`，
  Enter 已经按扩展键标志区分了，而且这三个键都与 `NumLock` 无关。
* 挂起期间快捷键不触发、也不吞任何键，这正是 AutoHotkey 的 `Suspend` 行为。
* 除非 flowkeyd 自己也提权，否则 `window` 动作无法驱动提权进程的窗口。
* 反过来，提权后的 flowkeyd 启动的子进程会继承管理员令牌（`run`、`open`、
  `window.launch` 都是）。这会造成一些奇怪的限制，比如从资源管理器往一个提权的
  终端窗口里拖文件会失败。需要普通权限时可以让 `explorer.exe` 代劳
  （`action = "run:explorer.exe path"`），或者直接用 `--no-elevate` 运行 flowkeyd。
* `window op = "activate"` 必须绕过 Windows 的前台锁。flowkeyd 会通过
  `AttachThreadInput` 重试（这正是你在另一个应用里打字时它也能生效的原因）；
  位于另一个虚拟桌面上的窗口仍然无法被唤起。
* 被吞掉的 `Win+…` 快捷键会在 Windows 键松开时注入一个未分配的按键
  （`VK 0xE8`）。没有它，外壳会看到一个“被单独按下”的 Windows 键，
  并在松开时打开开始菜单（对 `Win+s` 是搜索框）；这个遮断必须是外壳在 keyup
  之前看到的*最后*一件事，因为和弦键之后的 Windows 键自动重复会重新武装它。
  遮断只影响外壳怎么看那个修饰键：动作本身在按键按下时就跑了（`trigger = "press"`，
  默认），只有 `trigger = "release"` 的绑定才会等到松开。AutoHotkey 的 `#MenuMaskKey`
  做的就是同一件事，它同样是在按下时执行动作。
* 目前只挂钩键盘；鼠标按键和滚轮还不能做快捷键。
* **日志窗口是进程内的 QML 窗口**，不是命令行窗口：它最多显示最后 1000 行
  （完整内容见日志文件），带一个子串筛选框，但还没有 `--follow`/`--grep` 之类
  的命令行参数，也不做“INFO 与 DEBUG 分色”之外的渲染。打开着日志窗口时，
  `taskkill /PID`（**不带** `/F`）退不掉进程（`WM_CLOSE` 被它吃掉），要走托盘
  *退出*、`quit` 动作、`--quit`，或直接 `/F`。
* 守护进程**一直把日志文件开着写**，所以用 .NET 默认共享模式读它会报“文件正由
  另一进程使用”（`[System.IO.File]::ReadAllLines` / `ReadAllText`）：它们要的是
  `FileShare.Read`，与写句柄不兼容。用 `Get-Content -Encoding UTF8`，或者自己用
  `FileShare.ReadWrite` 打开。
* `menu` 与 `help` 的配色跟随系统 `palette`（比 oskeyd 的固定深色好），
  但**字体固定为微软雅黑**（不跟随系统字体），条目左侧还没有图标，动画也比较朴素。
* **三个弹窗在启动时就预热好了**（见 [工作原理](#工作原理)）：代价是常驻进程
  一启动就把 Qt Quick 的图形栈与三张卡片的 QML 树常驻下来（release 构建实测：
  工作集从 ~43 MB 升到 ~128 MB）。这笔钱其实躲不掉 —— 只要打开过一次日志窗口
  或任何弹窗，同一个图形栈也会常驻（旧构建 + 日志窗口实测 ~157 MB）；预热只是把
  它从“第一次用到的时候”挑到启动时，换来的是第一次弹出与之后一样快。
* 帮助窗口里 `Enter`/双击会**真的执行**那一行的动作（重映射那一行会真的注入
  按键，`quit`/`suspend`/`power` 两次确认之后也真的执行）。只想把快捷键抄走就
  用**左键单击**（写剪贴板，什么都不执行）。
* **「轻碰 Win」会接管 `LWin` 的“单独按一下”**（`keys = "LWin"` +
  `trigger = "release"`，见[窗口切换器](#窗口切换器)）：单独按一下 Win 不再打开
  开始菜单，而是弹窗口切换器。Win 的按下仍然照常传给系统，所以 `Win+E`、
  `Win+L`、`Win+Shift+S` 这些**没被 flowkeyd 接管**的系统组合不受影响；
  被 flowkeyd 绑定的 `Win+…` 和弦照旧由配置决定。
* 窗口切换器的筛选是按**进程名前缀**匹配（不是子串、不是模糊搜索），窗口标题
  不参与；`help` 的筛选仍然是子串匹配。筛到一个程序、而它开了多个窗口时进入
  **数字选择模式**：前 10 个窗口分到 `1`..`9`、`0`，超过 10 个的部分只能用
  `↑`/`↓` + `Enter` 或鼠标点选。两者的列表都是按下快捷键那一刻枚举出来
  的快照：之后新开的窗口要重新按一次才会出现。它按与 `Alt+Tab` 同一套「程序窗口」
  判据过滤（可见、无属主或带 `WS_EX_APPWINDOW`、非工具窗口、有标题、尺寸非零，
  并且没有被 shell 藏起来）——所以某些非标准程序的主窗口如果没标题就不会出现在
  列表里，而被 Windows 藏起来、点不到的“假窗口”（如 Windows 输入法的宿主）
  也不会列。卡片**没有标题行**（列表与筛选框同宽），`windows()` 的 `title`
  现在只用作窗口标题；打开卡片时会把 flowkeyd 自己这个进程的输入法切成英文，
  关掉卡片时再把它还原成打开前的状态（只影响这个进程，不影响你在别的应用里的
  中/英文状态）。
* `help` 的筛选是**子串匹配**（和弦、`comment`/`name`、动作摘要），不是模糊搜索。
  筛选框是标准 `TextField`，所以输入法（中文）能用了，但匹配仍然是子串。
  它列出的是当前配置里的绑定，改完配置要 `reload` 才会反映出来。
* **动作表顶层的拼写错误是静默的。** `{ type = "..." }` 这一层不支持
  未知字段报错（与 oskeyd 的 serde 内部标签枚举一致），所以
  `window("activate", { togle = false })` 里的 `togle` 会被直接忽略。
  `hotkey{}`、`settings{}`、`remap{}` 以及选单的**条目**都是严格检查的，
  只有动作表这一层没有。
* 关机/重启/注销需要 flowkeyd 提权（默认如此）；非提权的实例上按这些条目只会得到
  一条日志和一个气泡提示。睡眠/休眠与锁定不挑权限。
* 托盘菜单里的动作和控制台快捷键动作走同一条控制通道，但**菜单**本身没有自动化
  覆盖（那需要 UI 自动化）；自提权的 UAC 流程也只能手工验证。
* **托盘图标上的数字是轮询出来的**（每 500 ms 一次，见[工作原理](#工作原理)），
  所以拿它当“切成功了吗”的反馈时最多会晚半秒。它和 `desktop` 动作共用同一张
  未公开的版本表：锁屏、非交互会话或接口对不上时查不到当前桌面，这时图标退回
  应用图标、悬停提示里也没有桌面信息。桌面到两位数时显示 `9+`（16 逻辑像素的
  图标上两位数已经挤成一团），所以看不出具体是第几张。
* 托盘图标还不跟随 explorer 重启（没有处理 `TaskbarCreated`）。也因为这个，计划任务的登录触发器加了 15 秒延迟：
  启动得太早会拿不到托盘图标，而且本版本不会在 explorer 回来后自己补上。
* **计划任务的失败是静默的**：任务里的 exe 路径失效、单实例冲突、任务被禁用……
  表现都只是“没有托盘图标、快捷键不生效”，不会弹任何东西。排查看任务计划程序
  里 `flowkeyd` 那一条的*上次运行结果*，再看日志文件。
  自启任务指向当前运行的 exe（见[开机自启与更新](#开机自启与更新任务计划程序)），
  所以手动启动一次 flowkeyd 就会把失效的路径刷新回来。
* **没有 `--simulate` / `--selftest` / `--probe`。** 引擎与钩子的行为靠
  `scripts/acceptance.ps1`（注入按键的外部观察）与 Qt Test 单测来验证，
  但那三个开关仍然是明确的待办（见[路线图](#路线图)）：
  `--simulate` 不需要焦点、不装钩子，是更便宜的一条路。
* **`FLOWKEYD_ACCEPT_INJECTED=1` 是个测试后门**：设上它之后，**别的程序**
  合成的按键也会触发绑定（启用时日志里有一条警告）。日常使用不要设置它；
  它的存在理由是“物理按键”没法用脚本伪造。
* 按桌面编号跳转虚拟桌面（`desktop`）依赖 shell 的未公开 COM 接口
  `IVirtualDesktopManagerInternal`。它没有公开的 ABI 承诺：IID 与 vtable 布局
  会随 Windows 版本（甚至补丁修订号）变化，`src/platform/win/desktop.cpp` 里是
  一张按 `build.revision` 索引的表（事实与 AutoHotkey 的 VD.ahk 一致）。
  本机（build 26200.9457）验证过的是“plain”布局；新的 Windows 版本如果又改了
  接口，切换会以一条带 HRESULT 的日志失败。公开的 `IVirtualDesktopManager`
  做不到这件事——它只能查询和移动窗口。
* 跨桌面“唤醒”（`window` 动作发现目标窗口在别的虚拟桌面上时）还要把窗口所在
  桌面与内部的桌面对象对上号，这一步用未公开的 `IVirtualDesktop::GetID`
  （vtable 下标 4），并与**已公开**的 `GetWindowDesktopId` 逐个比对（事实同样来自
  VD.ahk 的 `VD_goToDesktopOfWindow`）。对不上时只记一条错误日志、不去切一张
  可能是错的桌面。另外，被 `window_rule` 搬到别的桌面之后，Windows 仍然把那个
  窗口记成**前台窗口**（本机实测），所以“是否已经激活”的判定必须把虚拟桌面
  一起算上——否则同一个快捷键会去*收起*一个用户根本看不见的窗口。
* `window_rule` 的 `desktop` 用的是同一个未公开接口的 `MoveViewToDesktop`
  （公开的 `IVirtualDesktopManager::MoveWindowToDesktop` 拒绝移动**别的进程**
  的窗口），因此它与 `desktop` 动作共享同一张版本表与同样的风险。另外两点：
  窗口会先在当前桌面上出现一瞬间、然后被移走；判断“这是不是主窗口”用的是
  可见 / 无属主 / 非工具窗口 / 有标题 / 尺寸非零这套启发式，偶尔会漏掉一个
  标题设置得很晚的窗口（`process` 匹配不受影响）。
* `window_rule` 的 `all_desktops` 走的是 shell 另一个未公开接口
  `IVirtualDesktopPinnedApps`（`{4CE81583-...}`，`QueryService` 的 SID 是
  `{B5A399E7-...}`）。与 `IVirtualDesktopManagerInternal` 不同，**这个 IID 自
  Windows 10 起就没变过**，所以它没有版本表；拿不到那个接口时只记一条 warning，
  规则里其余部分照常生效。本机（build 26200.9457）已用 `IsViewPinned` 验证过
  `PinView` / `UnpinView` 真的生效。`topmost` 用的是已公开的 `SetWindowPos`，
  没有这个风险。
* 跟着窗口切过去（`window_rule` 搬完窗口 / `window` 动作跨桌面唤起）是**故意的
  行为变化**，不是所有程序都欢迎它：某个后台程序如果在启动时开了自己的窗口，
  而你正好给它写了 `window_rule`，你的视图会被拽到那张桌面上。不想被拽就把
  `desktop` 从那条规则里去掉（只留 `monitor`），或者把规则整个 `enabled = false`。
* `window_rule` 只在**窗口第一次出现、显示器重新接入、以及 flowkeyd 启动时**
  生效；之后你手动移动 / 缩放窗口、取消钉住、取消置顶都不会被纠正。它也不管
  已经开着的窗口 ——
  想重新归位就按一下 `reload`（会重新读配置，但不会重摆已有窗口）或重启
  flowkeyd。

* **前台窗口可以是个“覆盖层”。** 例如按住 Win 弹出的 PowerToys「快捷键指南」
  （`WS_EX_TOOLWINDOW`）会变成 `GetForegroundWindow()`。`window` 动作会跳过它、
  沿 Z 序找第一个真正的主窗口；如果你的某个正常窗口本身就是工具窗口，
  它的快捷键可能就会落到别的窗口上（这是刻意与 `window_rule` 的“主窗口”判据
  保持一致）。

## 路线图

1. `--simulate <SCRIPT>`：把脚本化按键事件重放给真正的引擎（干跑）。
   这是最便宜的引擎验证手段，不需要焦点、不装钩子。
2. `--selftest` / `--probe`：各平台后端探测与自检（Core Audio 的 COM vtable、
   虚拟桌面接口表、未公开 API 的可用性）。
3. `scripts/e2e.ps1`：`scripts/acceptance.ps1` 已经覆盖了它的大部分
   （吞键、重映射、自动重复、挂起/重载/退出、`window`、`menu`/`help`、小键盘）；
   还缺的是动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程，
   以及把日志窗口那一套从外面断言。
4. 延迟修饰键抑制，让 `Ctrl+Alt+h` 也隐藏 Ctrl 和 Alt。
5. 托盘图标跟随 explorer 重启（`TaskbarCreated`）并支持自定义图标。
6. 通过 `WH_MOUSE_LL` 支持鼠标按键与滚轮快捷键。
7. 配置文件变化时热重载（去抖的 `ReadDirectoryChangesW`）。
8. 按应用限定的快捷键（只在匹配窗口获得焦点时才触发）。
9. 日志窗口的增强：`--follow`/`--grep` 之类的参数、更细的分色渲染。
10. 选单与帮助窗口的条目图标、更细的动画，以及帮助窗口的模糊搜索与 IME 输入。

## 许可证

MIT（与 oskeyd 相同）。
