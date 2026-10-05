# flowkeyd

一个用 **Lua 脚本**配置的 Windows 键盘钩子守护进程（C++20 + Qt 6）。

## 安装

**系统要求：Windows 10 或 11（64 位）。** 运行期不需要装 Qt 或任何其它依赖。

在 **PowerShell** 里执行下面这一行，即可从 GitHub Release 下载并安装（或升级）最新版：

```powershell
powershell -nop -c "irm https://raw.githubusercontent.com/xingjianxu/flowkeyd/master/install.ps1 | iex"
```

安装脚本会：

1. 问 GitHub 要最新 Release 的版本号；
2. 下载完整包 `flowkeyd-<版本>-windows-x64.zip`，并用同名 `.sha256` 校验；
3. 解压到 `%LOCALAPPDATA%\Programs\flowkeyd`；
4. 让正在运行的实例干净退出（`flowkeyd.exe --quit`）；
5. 覆盖安装，并创建开始菜单快捷方式；
6. 启动 flowkeyd（守护进程首次启动会自提权，弹一次 UAC）。

上面那种 `irm … | iex` 的写法传不了参数。要指定版本 / 安装目录、或不创建快捷方式、
不启动，就把脚本落盘（或直接在仓库里）带参数运行：

```powershell
# 先落盘（也可以直接用仓库里的 install.ps1）
curl.exe -fsSL https://raw.githubusercontent.com/xingjianxu/flowkeyd/master/install.ps1 -o "$env:TEMP\flowkeyd-install.ps1"
# 安装指定版本，装到别处，并且不创建快捷方式、不启动
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\flowkeyd-install.ps1" `
    -Version 26-09-27-dc6b332 -InstallDir D:\Tools\flowkeyd -NoShortcut -NoLaunch
```

| 参数            | 含义                                                     |
| --------------- | -------------------------------------------------------- |
| `-Version`      | 要安装的版本号（形如 `26-09-27-dc6b332`，可带前导 `v`）  |
| `-InstallDir`   | 安装目录（默认 `%LOCALAPPDATA%\Programs\flowkeyd`）      |
| `-NoLaunch`     | 安装完成后不启动                                         |
| `-NoShortcut`   | 不创建开始菜单快捷方式                                   |
| `-SkipChecksum` | 跳过 sha256 校验（不推荐）                               |

**手动安装：** 到
[Releases](https://github.com/xingjianxu/flowkeyd/releases/latest) 下载
`flowkeyd-<版本>-windows-x64.zip`（完整包），解压到任意目录，双击 `flowkeyd.exe`
即可。已经装过、只想升级时下载精简包
`flowkeyd-<版本>-windows-x64-slim.zip`（里面只有 `flowkeyd.exe`），先
`flowkeyd.exe --quit`，再用解压出的 exe 覆盖掉旧的即可。

**卸载：**

```powershell
& "$env:LOCALAPPDATA\Programs\flowkeyd\flowkeyd.exe" --quit
& "$env:LOCALAPPDATA\Programs\flowkeyd\flowkeyd.exe" --remove-autostart   # 删除开机自启，需要管理员
```

然后删掉程序目录与开始菜单里的 `flowkeyd` 快捷方式即可。配置与日志在
`%USERPROFILE%\.config\flowkeyd\`，不需要时可以一并删除。

## 功能概览

它安装一个 `WH_KEYBOARD_LL` 钩子，把你的配置与按键和弦进行匹配，在你需要时把匹配到的
按键对前台应用隐藏（也就是 AutoHotkey 的行为），然后执行你绑定的动作。动作默认在
**按下**的那一刻就跑（不等按键或修饰键抬起），而且只跑一次；想改成抬起时触发或按住
重复，用 `trigger` 设置即可。它还能把一个按键重映射为另一个按键、在按住源键期间一直
按住目标键。

动作可以是：启动程序、发送按键、输入文本、控制音量/媒体、操作窗口、把窗口挪到相邻的
虚拟桌面或显示器、切换虚拟桌面与剪贴板、弹出通知、执行电源动作，或者**弹出一张卡片**
——电源选单（睡眠 / 关机 / 重启，按 `S`/`P`/`R` 选择、`Esc` 关闭）、快捷键帮助
（`Win+/`，可直接输入筛选，`Enter` 或双击一行就直接执行它）、窗口切换器
（`windows()`，列出当前打开的程序窗口，按进程名前缀筛选，常见绑法是「轻碰一下 Win」），
以及程序启动器（`apps()`，把开始菜单里的程序摆成一张图标网格，输入即筛、`Enter` 启动，
筛选之后前 10 个结果还能用 `0`–`9` 一键启动）。

它还能按程序摆放窗口：`window_rule{...}` 让某个程序的窗口第一次出现时落到指定的虚拟
桌面 / 显示器上（默认铺满那块显示器的工作区），并在之前断开的显示器重新接上时重新
归位；规则还能把窗口**钉在所有虚拟桌面上**（`all_desktops = true`）或让它**始终在最
上层**（`topmost = true`）。要给同一个程序同时配「窗口规则」与「唤起它的快捷键」，
用 `app{...}` 写到一起即可，`process` / `title` / `launch` 只写一遍。

它也认得**远程桌面**：前台窗口是 RDP 客户端（远程桌面连接 / Windows App）时，所有快捷键
与重映射都**放行**，键原样送给对面那台机器（想保留哪一条就在它自己身上写
`remote_desktop = true`，见[远程桌面](#远程桌面)）。

托盘通知区域里的图标平时是应用图标，但**主体内容是当前是第几号虚拟桌面**：守护进程
每 500 ms 问一次 shell 现在在第几张桌面，把图标换成对应的数字（蓝底白字，`10` 以上
显示 `9+`），悬停提示里也写着 `桌面 2/4`。查不到当前桌面时（锁屏、非交互会话）退回
应用图标。

托盘右键菜单里还有**在线更新**：点一下就去 GitHub 问一次最新发布，有新版本时弹出一张
卡片（版本号 + 这次改了什么），确认后带进度条下载、校验，然后自己换上新的 exe 并重启，
重启后弹一条 Windows 通知告诉你更新成功（见[在线更新](#在线更新)）。

## 快速上手

配置文件是 `%USERPROFILE%\.config\flowkeyd\config.lua`（一段真正的 Lua 脚本）。
一个覆盖全部特性的参考配置见仓库里的
[`flowkeyd.lua.example`](flowkeyd.lua.example)。最小的例子：

```lua
settings{ swallow = true, tick_ms = 15 }

hotkey{
  name = "terminal",
  keys = "Ctrl+Alt+t",
  action = run("wt.exe"),
}

remap{ from = "CapsLock", to = "Esc" }

-- 按程序摆放窗口：第一次出现时放到第 2 个虚拟桌面的右屏并最大化
window_rule{ process = "wezterm", desktop = 2, monitor = 2 }

-- 同一个程序的窗口规则 + 快捷键 + 启动参数写在一起
app{
  process = "wps",
  launch = { program = [[C:\tools\wps.exe]], wait_ms = 10000 },
  window = { desktop = 3, monitor = 2 },
  hotkeys = {
    { keys = "Win+3", action = window("activate") },
  },
}
```

改完配置后运行 `flowkeyd --check --config <路径>` 校验，或用托盘菜单的*重载配置* /
`reload` 快捷键让正在运行的实例重新读取。

启动后 flowkeyd 是一个常驻托盘的程序：右键菜单是 *查看日志*、*挂起/恢复快捷键*、
*重载配置*、*打开配置文件*、*检查更新...*、*版本 ...*（不可点的信息项）、*退出*；
左键单击直接打开日志窗口。悬停提示会显示构建版本、当前虚拟桌面与挂起状态。

> 双击 `flowkeyd.exe` 等价于不带任何参数启动：它没有控制台，日志只进
> `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`（用托盘「查看日志」看），并且默认会
> 弹一次 UAC 自提权（不想提权就在配置里写 `elevate = false`）。

## 命令行

```
flowkeyd [选项]

-c, --config <PATH>     配置文件（默认：%USERPROFILE%\.config\flowkeyd\config.lua，
                        找不到时依次回退到 exe 同目录、%APPDATA%\flowkeyd、
                        当前目录下的 config.lua）
    --no-elevate        不自动提权，直接以当前权限运行
    --console           保留控制台输出
    --elevated          内部标记：已经提权，不要再重启自己
    --updated-from <V>  内部标记：本进程是被在线更新重启起来的（重启前是 V），
                        启动后弹一条“更新成功”通知
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
flowkeyd.lua.example: OK (46 hotkey(s), 3 remap(s), 7 window rule(s))
```

（`window rule(s)` 只在配置里真的写了 `window_rule` 时才出现，没有摆放规则的配置仍然
输出老样子。）

**离线命令**（`--check` / `--list` / `--list-keys` / `--help` / `--version`）永远不会
弹 UAC，也绝不安装钩子。`--quit` 同样不装钩子、不提权（它只去通知一个已经在跑的
实例）。`--remove-autostart` 需要管理员权限才能删任务。

守护进程模式下配置读不出来或校验不过时，flowkeyd 除了把错误写进 `stderr`，还会弹一个
Qt 标准消息框（标题 `flowkeyd 配置错误`，错误文本可选中复制），点确定后以退出码 1
退出。双击启动时进程没有控制台，只有弹窗能让你知道到底出了什么事。

守护进程模式下同一份配置只允许一个实例。再启动一次时会**先弹一个原生提示框**
（`flowkeyd 已在运行`），点确定后以退出码 1 退出；这一步刻意放在 UAC 提权之前，所以
重复双击不会白白弹一次 UAC，也不会动到正在运行的那个实例。（`--allow-multi` 跳过单
实例检查；`--no-prompt` 只跳过提示、检查照做。）

**构建版本号**长这样：`26-09-27-dc6b332` —— 前半是**构建这个 exe 的日期**
（`yy-MM-dd`，取运行中的可执行文件自己的最后写入时间），后半是**构建时源码的 git
提交缩写**。启动日志的第一行、`--version` 与 `--help` 打印的也是它：

```console
$ flowkeyd --version
flowkeyd 26-09-27-dc6b332
Lua 5.5.1
```

## 配置

配置文件是**一段真正的 Lua 5.5.1 脚本**，由 flowkeyd 以你的权限执行：可以算表达式、
写循环、用 `os.getenv` / `string.format` 之类的标准库。它也能做任何你的账号能做的事，
所以别去运行来路不明的配置。

### 配置脚本的写法

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

window_rule{ process = "wezterm", desktop = 2, monitor = 2 }

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

每个条目都可以有 `name`，出错时会写成 `hotkey #3 (\`terminal\`): unknown field ...`，
脚本本身的错误则带 `config.lua:行号`。**未知字段一律报错**，拼写错误会被 `--check`
抓出来而不是被静默忽略。

### Lua 的几个坑

* **`repeat` 是 Lua 关键字**，所以 `hotkey{ repeat = true }` 是语法错误。
  重复参数请写 `repeatable = true`（或者 `["repeat"] = true`）。
* **字符串里的反斜杠是转义序列。** Windows 路径用长字符串
  `[[C:\tools\my app\app.exe]]`，或者写成 `'C:\\tools\\app.exe'`。
  注意 `"C:\tools"` **不会报错**：`\t` 是制表符，它会静默变成 `C:` + TAB + `ools`；
  只有 `\p` 这类非法转义才会报错。
* **`{}` 既是空列表也是空表。** 配置里空表只应出现在列表位置（`args`），
  `env = {}` 会被判成空列表并报错，提示会说明这一点。
* **动作不能是 Lua 函数**：`action = function() ... end` 会被拒绝，因为钩子回调与
  工作线程只执行校验过的声明式动作。想要“自定义逻辑”就用循环与表达式去生成声明式动作。
* 带 UTF-8 BOM 的配置（记事本、`Set-Content -Encoding UTF8` 的产物）与 CRLF 都能
  正常读：加载器会先剥掉 BOM。

### DSL 速查

除了直接写 `{ type = "..." }`，还提供一组构造器（同时挂在 `flowkeyd.*` 下，免得与
脚本自己的全局变量撞名）：

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
| `apps([title])`                                | `{ type = "apps", title = title }`                                  |
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

回退到非默认位置时会打一条 warning 指出真正使用的位置；每一个都不存在时，报错会指向
默认位置（也就是你该创建的那个文件）。`.toml` 后缀会被明确拒绝并提示迁移。

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
| `remote_desktop`     | `true`   | 前台窗口是远程桌面客户端时**放行**（不拦截、不触发）；`false` 关掉检测；写成表可以换进程名单（见[远程桌面](#远程桌面)） |

### 远程桌面

你在本机开着 RDP 客户端（`mstsc.exe`、Windows App）连到另一台机器时，按键本来是发给
**对面那台**的：flowkeyd 不该把它们吞掉、也不该触发本机的动作。所以只要前台窗口属于
远程桌面客户端，所有快捷键与重映射就一律**放行**（不拦截、不触发），键原样送到对面。

```lua
settings{ remote_desktop = true }        -- 默认就是 true
settings{ remote_desktop = false }       -- 关掉这项检测（行为与本功能存在之前完全一样）

settings{                                -- 换进程名单（整体替换内置的那份）
  remote_desktop = {
    enabled = true,
    processes = { "mstsc.exe", "ToDesk.exe", "SunloginClient.exe" },
  },
}
```

| 项                       | 默认 | 含义                                                                                                                                     |
| ------------------------ | ---- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `remote_desktop`         | `true` | 检测开关；`false` 表示完全不去判断前台是什么                                                                                            |
| `remote_desktop.processes` | 微软 RDP 客户端 | 视为远程桌面的可执行文件名，大小写无关的**子串**匹配（与 `window_rule` 的 `process` 同一套）；写了就整体替换默认名单，`{}` 表示谁都不算 |

默认名单是 **`mstsc.exe`、`msrdc.exe`、`msrdcw.exe`、`RdClient.Windows.exe`**（经典远程
桌面连接与「Windows App」）。第三方远程控制软件（ToDesk / 向日葵 / AnyDesk /
TeamViewer / RustDesk…）**不在**默认名单里：把它们的窗口也算成远程桌面是另一种口味，
需要时写进 `processes`。

想**在远程桌面里也照常拦截、照常触发**的快捷键或重映射，在它自己身上写
`remote_desktop = true`：

```lua
-- 音量是**本机**的事：前台是远程桌面时这一条也照常拦下来、照常执行
hotkey{ keys = "Ctrl+Alt+m", remote_desktop = true, action = volume("toggle") }

remap{ from = "CapsLock", to = "Esc", remote_desktop = true }
```

判定只看**前台窗口的属主进程名**（不看标题，也不管是否全屏），而且只在**前台窗口换了**
时做一次（外加一个 350 ms 的兜底轮询），所以按键路径上没有任何额外开销。日志里会看到
`remote desktop detected (mstsc.exe): hotkeys and remaps pass through` 与
`left the remote desktop (…): hotkeys and remaps are active again`；`--list` 会把它当前
认的名单与例外一起打出来。

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
| `remote_desktop` | `true` 表示「在远程桌面里也照常拦截、照常触发」（默认 `false`，见[远程桌面](#远程桌面)） |
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
`repeatable = { interval_ms = 40 }` 来给参数）；把它们写成互相矛盾的组合会被 `--check`
拒绝。

`trigger = "release"` 写在**单个修饰键**上（例如 `keys = "LWin"`）时是「轻碰」语义：
按下修饰键本身**照常放行给系统**（`Win+E` / `Win+L` 这些没被 flowkeyd 接管的系统组合
不受影响），只有期间没按过别的键、松开时才触发 —— 窗口切换器就是靠它绑在 Win 键上的
（见[窗口切换器](#窗口切换器)）。单个修饰键配默认的 `trigger = "press"` 仍然是老行为。

和弦语法既接受名称也接受 AutoHotkey 前缀：

| 语法                | 含义                                  |
| ------------------- | ------------------------------------- |
| `Ctrl+Alt+h`、`^!h` | 修饰键在前，按键在后                  |
| `^!h`               | `^` Ctrl、`!` Alt、`+` Shift、`#` Win |
| `~F4`               | 即使快捷键触发也放行该按键            |
| `*F1`               | 即使额外按住了修饰键也触发            |

只有修饰键的和弦也能用：`Ctrl+Shift` 会在 Ctrl 按着时、Shift 按下时触发，而通用的
`Shift`/`Ctrl`/`Alt` 名称会匹配键盘的任意一侧。

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
**发送脚本里的裸字符不受影响**：`send("A")` 仍然是 AutoHotkey 语义下的“打出大写 A”
（等价于 `send("+a")`）；要按字面输入任意文本用 `type("...")` 或 `send("{Text}...")`。

一个和弦里**只能有一个按键**（其余片段必须是修饰键），所以「两个普通键一起按」是
**不支持**的：`keys = "NumpadSub+NumpadAdd"` 会被拒绝。要表达这种意图只能用修饰键
组合（如 `Ctrl+NumpadMult`），或者换一个独立的键。

`Numpad*` 是小键盘上的那些键，与主键盘上的同名键是**不同的键**：`NumpadSub`
（`VK_SUBTRACT`）不会匹配主键盘的 `-`（那是 `Minus`，`VK_OEM_MINUS`），`NumpadAdd`
（`VK_ADD`）不会匹配主键盘的 `=`，`NumpadEnter` 也不会匹配主键盘的 `Enter`。小键盘的
Enter 与主键盘的 Enter 共用 `VK_RETURN`，区分靠的是 Windows 的扩展键标志；而这个区别
与 `NumLock` 完全无关。完整的按键名清单见 `flowkeyd --list-keys`。

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
| `volume`    | `op` = `up`/`down`/`set`/`mute`/`unmute`/`toggle`、`level`（0-100）、`step`                               | 对默认输出设备使用 Core Audio                                                                                                                                                                                                                                                            |
| `media`     | `op` = `play_pause`/`next`/`prev`/`stop`                                                                  |                                                                                                                                                                                                                                                                                           |
| `clipboard` | `op` = `get`/`set`/`append`/`clear`、`text`                                                               |                                                                                                                                                                                                                                                                                           |
| `window`    | `op` = `activate`/`minimize`/`maximize`/`restore`/`close`/`toggle_topmost`/`move_prev_desktop`/`move_next_desktop`/`move_left_monitor`/`move_right_monitor`、`target`、`process`、`launch`、`wait_ms`、`toggle`、`animate`、`follow` | `target` 匹配窗口标题的子串，`process` 匹配可执行文件名（`wezterm` 也能匹配 `wezterm-gui.exe`）；既没有 `target` 也没有 `process` 就表示前台窗口。`launch = { program, args[], cwd, show, shell, env{}, wait_ms }` 会在没有任何匹配时启动该程序，然后等待它的窗口（默认 3000 ms）并激活它。`toggle`（默认开，只对 `op = "activate"` 有意义）会在目标窗口已经在前台时改为最小化它。`animate`（默认**关**）控制这次状态变化要不要播放 DWM 的过渡动画。`wait_ms` 写在动作顶层时是 `launch.wait_ms` 的简写；在 `app{}` 里 `launch` 还会继承 app 的 `launch`（逐字段合并）。四个 `move_*` op 把**当前窗口**（不写 `target`/`process` 时）挪到相邻的虚拟桌面 / 显示器：`move_prev_desktop`/`move_next_desktop` 只动虚拟桌面且**首尾相接**（显示器上的几何不变，视图**不**跟着走），`move_left_monitor`/`move_right_monitor` 只动显示器（保留最大化状态，否则保持原有大小并居中到目标工作区；没有更左/更右那一块时失败，**不循环**）。这四个 op 都不套用 `toggle`、也不接受 `launch`。`follow = true` 只能写给 `move_prev_desktop`/`move_next_desktop`：搬完之后把视图也切到目标桌面并重新激活那个窗口（用户跟着窗口一起过去；不写时视图不动，与 Windows 自己的 `Win+Ctrl+Shift+←/→` 一致），写在其它的 op 上会被 `--check` 拒绝 |
| `notify`    | `title`、`body`                                                                                           | 托盘气泡提示                                                                                                                                                                                                                                                                              |
| `menu`      | `title`、`items[]`（每项 `{ key, label, hint, action }`）                                                  | 弹出一个选单让用户挑一项（见[选单与电源](#选单与电源)）；条目上的单字符 `key` 直接选中它，`↑`/`↓` + `Enter` 与鼠标也能选，`Esc`（或点到别的地方）只关窗口。没有 `action`（或 `none()`）的条目只是把选单关掉                                                                         |
| `help`      | `title`（可选，默认「快捷键」）                                                                            | 弹出一个**快捷键帮助**（见[快捷键帮助](#快捷键帮助)）：列出当前配置里全部生效的快捷键与重映射；筛选框里直接输入（鼠标点一下就进去）就筛选；`↑`/`↓`、`PgUp`/`PgDn` 或滚轮/拖动滚动条滚动（滚动不改选中项）；**`Enter` 或双击一行 = 关掉窗口并执行那一行的动作**；左键点一行 = 选中它并把它的按键复制到剪贴板；`quit`/`suspend`/`power` 这类危险动作要按两次；`Esc` 依次是「取消确认 → 清筛选 → 关窗」。列表由配置本身生成，所以没有别的参数 |
| `windows`   | `title`（可选，只用作**窗口标题**）                                                                        | 弹出一个**窗口切换器**（见[窗口切换器](#窗口切换器)）：列出当前所有打开的程序窗口（**进程名在上、窗口标题在下**），输入按**进程名前缀**筛选（标题只显示、不参与匹配）；**筛选到只剩一个窗口就直接激活它**，多条时用 `↑`/`↓` + `Enter` 或鼠标点选，`Esc` 关窗；筛到一个进程、而它开了多个窗口时进入**数字选择模式**（前 10 行依次是 `1`..`9`、`0`，按数字直接切过去）。卡片**没有标题行**（筛选框就是第一行，列表与它同宽），打开的瞬间还会把输入法切成英文、关掉时把打开前的模式写回去；卡片开着时**再按一次同一个快捷键就是关掉它**（与 `Esc` 同义） |
| `power`     | `op` = `sleep`/`hibernate`/`shutdown`/`restart`/`logoff`/`lock`/`screen_off`                                | 系统电源：睡眠、休眠、关机、重启、注销、锁定、关屏。关机/重启/注销需要管理员权限（flowkeyd 默认就提权运行），失败时会写日志并弹一个气泡；`screen_off` 只是关掉全部显示器（不睡眠），不需要权限，任意按键/鼠标动作都会把屏幕重新点亮                                                                                      |
| `apps`      | `title`（可选，只用作**窗口标题**）                                                                        | 弹出一个**程序启动器**（见[程序启动器](#程序启动器)）：开始菜单里的程序分三段摆出来 —— 「已固定」「最近使用」（最多两行）与一个「全部程序」按钮；输入按**名字 / 拼音 / 首字母的子串**筛选（`code`、`jishiben`、`jsb` 都能找到「记事本」与 `Visual Studio Code`）；用 `↑`/`↓`/`←`/`→` + `Enter`（或鼠标点一下）启动高亮那一个，**筛选之后前 10 个结果各带一个 `0`–`9` 的快速启动键**（图标右上角，按一下直接启动，easymotion 风格），`Space` 固定 / 取消固定，**右键某一格弹出那个程序的原生 Windows 右键菜单**，`Esc` 关窗；卡片开着时**再按一次同一个快捷键就是关掉它**。列表来自开始菜单扫描（只列程序），启动走的就是那条快捷方式本身 |
| `desktop`   | `switch`（从 1 开始）                                                                                     | 切到第 N 个虚拟桌面（Task View 里从左到右的顺序）                                                                                                                                                                                                                                          |
| `suspend`   | `state` = `on`/`off`/`toggle`                                                                             | 暂停快捷键匹配；suspend 快捷键本身仍然可用                                                                                                                                                                                                                                                |
| `reload`    |                                                                                                           | 重新读取配置文件                                                                                                                                                                                                                                                                          |
| `quit`      |                                                                                                           | 退出 flowkeyd                                                                                                                                                                                                                                                                             |
| `none`      |                                                                                                           | 什么都不做（配合 `swallow` 很有用）                                                                                                                                                                                                                                                       |

简写：`"run:notepad.exe file.txt"`、`"send:^{c}"`、`"type:hello"`、
`"open:https://example.com"`、`"notify:title|body"`、`"volume:up"`、`"media:next"`、
`"clipboard:get"`、`"window:minimize"`、`"window:move_next_desktop"`、`"desktop:1"`、
`"power:sleep"`、`"reload"`、`"quit"`、`"help"`、`"windows"`、`"apps"`、`"none"`。

可能有多个窗口匹配；最近使用过的那个（Z 序里最靠前的）胜出，而已还原的窗口优于最小化
的窗口。不可见窗口以及属于别的窗口的弹出窗口（对话框、工具提示、菜单）会被忽略。匹配
会同时使用 `target` 和 `process`，这正是能可靠找到“那个 WezTerm 窗口”的方式——终端
窗口的标题是里面运行的 shell 决定打印的任意内容。

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

`launch` 只在窗口确实不存在时才会尝试，因此一次被拒绝的激活永远不会留下第二个正在运行
的副本。

`op = "activate"` 默认带 `toggle`：要唤起的窗口**已经**在前台时就把它最小化，于是同一
个快捷键在唤起与收起之间切换——和点击任务栏按钮、以及 Windows 自己的 `Win+数字`
一样。已经最小化的窗口不算“已经在前台”，那种情况下按下去依然是把它恢复出来。只想让
快捷键永远把窗口往前抬（比如“随时把日志窗口调到眼前”）就写 `toggle = false`。

**“已经在前台”还要看虚拟桌面。** 窗口不在你当前那张虚拟桌面上时不算“已经在前台”
（哪怕 Windows 仍然把它记成前台窗口），按下去是**切到它所在的那张桌面并激活它**，
视图跟着过去。于是某一个程序被 `window_rule` 固定在第 3 张桌面上时，用快捷键唤起它
一次就能到位，不必先手动切桌面；再按一次才是收起。

（`target` 和 `process` 都不写时目标就是前台窗口本身，于是 `window("activate")` 会变成
“最小化当前窗口”——那种场合本来就该写 `window("minimize")`。）

`launch` 那条路径**不**套用 `toggle`：刚启动的窗口往往自己就抢到了前台，在那种时候把
“启动它”变成“立刻收起它”毫无道理。

`launch.program` 是直接交给 `CreateProcess` 的（不像 `open` 那样走 `ShellExecuteW`），
所以它**不会**查注册表里的 `App Paths`，也不做 `PATH` 之外的搜索：Chrome 与 VS Code
这种不在 `PATH` 里的程序要写完整路径。

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

`process` 写的是可执行文件名，所以得用那个**真正拥有窗口**的进程名：微信 4.x 是
`Weixin.exe`（写 `weixin`；`WeChatAppEx.exe` 只是它的小程序子进程），VS Code 的窗口
属于 `Code.exe`。

### 把窗口挪到相邻的桌面 / 显示器

`window` 动作还有四个只针对**当前窗口**（不写 `target`/`process` 就是前台窗口）的
`op`，用来把窗口搬到相邻的位置：

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

* `move_prev_desktop` / `move_next_desktop` 只动**虚拟桌面**，窗口在显示器上的大小与
  位置完全不变。默认视图也**不**跟着走（移动的是窗口，不是当前桌面）。两张桌面
  **首尾相接**：在第一张再往前会到最右那一张，在最后一张再往后会回到第一张。
* 这两个虚拟桌面 `op` 还可以带 `follow = true`：搬完之后把**视图也切到目标桌面**，
  并让那个窗口重新拿到前台。“保持激活”是尽力而为：真抢不到前台时只写一条 warning，
  动作本身算成功。`Win+i` / `Win+u`（不按 Shift）用的就是这个，而 `Win+Shift+i` /
  `Win+Shift+u` 只搬窗口。
* `move_left_monitor` / `move_right_monitor` 只动**显示器**：按“先左后右、再上后下”
  的排列顺序找相邻那一块（和 `window_rule` 里 `monitor = 2` 的 1 起序号是同一个排列）。
  虚拟桌面不变；**最大化窗口在新显示器上仍然最大化**，普通窗口保持原有大小并**居中**
  到目标显示器的工作区，最小化的窗口只更新它的还原位置。已经在最左/最右那一块时按下去
  只会写一条日志，**不循环**（与虚拟桌面那两条不同）。
* 这四个 `op` 都不套用 `toggle`（不会因为窗口已经在前台就把它收起），也不接受
  `launch`；跨显示器移动会产生窗口过渡，所以 `animate` 对它有意义，而跨虚拟桌面移动
  不产生过渡、写 `animate` 会被 `--check` 拒绝。

### 前台窗口与覆盖层

不写 `target`/`process` 的 `window` 动作作用于**前台窗口**（`GetForegroundWindow()`），
但有一个例外：如果它是个**覆盖层**（`WS_EX_TOOLWINDOW`），flowkeyd 会沿 Z 序往下找到
第一个真正的主窗口（可见、无属主、非工具窗口、有标题、尺寸非零）再动手。

最典型的覆盖层是 **PowerToys 的「快捷键指南」**：按住 Win 约一秒它就会弹出来并成为
`GetForegroundWindow()`。flowkeyd 会跳过它，继续作用在你刚才那个窗口上。

### 窗口动画

flowkeyd 触发的窗口状态变化默认**不播放动画**：`minimize` 不再“缩”到任务栏，
`maximize`/`restore`/`activate` 也不再伸缩，窗口直接到位。写 `animate = true` 可以给
某一条绑定把动画要回来：

```lua
hotkey{ keys = "Ctrl+Alt+f", action = window("maximize", { animate = true }) }
```

实现用的是**按窗口**的 `DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
TRUE)`：调 `ShowWindow` 前设上、调完立刻设回，所以**不改系统设置**——系统里“辅助功能 /
视觉效果 → 最小化/最大化时播放动画”不会被碰，其它程序、以及你手动点标题栏按钮时的
动画都不受影响。只有会改变窗口状态的 `op` 有这个效果；`close`、`toggle_topmost` 与跨
虚拟桌面移动（`move_prev_desktop`/`move_next_desktop`）不产生过渡，写 `animate` 会被
`--check` 拒绝；跨显示器移动（`move_left_monitor`/`move_right_monitor`）会改变几何，
`animate` 对它有意义。

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
  remote_desktop = true, -- 可选：在远程桌面里也照常生效（默认 false，见[远程桌面](#远程桌面)）
}
```

`hold` 在 `from` 按下时按下目标、在 `from` 松开时松开目标，这正是 `CapsLock -> Esc`
或 `CapsLock -> Ctrl` 想要的行为。`tap` 每次按下只执行整个脚本一次。

`to = "Esc"` 表示 Escape 键，与 AutoHotkey 的 `CapsLock::Esc` 完全一致；
`to = "hello"` 是一个脚本，会输入五个字母。

### `window_rule{ ... }`

按程序摆放窗口：某个程序的窗口**第一次出现**时，把它放到指定的虚拟桌面 / 显示器上。

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
  * `"DISPLAY2"` —— 显示器设备名（也可写全名 `\\.\DISPLAY2`）。

  找不到匹配的显示器时只记一条 warning，窗口留在原处（例如笔记本没插外接屏时）。
* `all_desktops = true` 把窗口**钉在所有虚拟桌面上**（Task View 里的「在所有桌面
  显示」）：切到哪张桌面都看得见它。`false` 是显式取消钉住，不写就不去碰它。它与
  `desktop` 互斥，两个都写会被 `--check` 拒绝。
* `topmost = true` 让窗口**始终在最上层**；`false` 是显式取消置顶，不写就不去碰它。
* `all_desktops` 与 `topmost` 都不算“几何”：只写它们（没写 `monitor`）时窗口的大小
  与位置保持不动，也不会因为默认最大化而突然变大。
* **默认最大化。** 只写了 `monitor`（而没写位置 / 大小）时，窗口会铺满那块显示器的
  **工作区**（扣掉任务栏）。只写 `desktop` 时窗口的大小与位置保持不动；
  `maximize = true` 与 `x`/`y`/`width`/`height` 不能同时写。
* `x`/`y` 是相对**目标显示器工作区左上角**的偏移，`width`/`height` 是像素；没给的项
  保持窗口原来的大小 / 居中。窗口比工作区还大时对齐工作区左上角，保证标题栏可见。
* 同一个窗口只处理一次：已经摆放过的窗口再次显示（从托盘还原、最小化后还原）不会再摆
  一次。规则按书写顺序匹配，**先写的赢**；两条规则匹配同一批窗口时 `--check` 会给一条
  warning。

**触发时机只有三个：**

1. 窗口第一次出现；
2. 之前断开的显示器重新接上（设备名从无到有）；
3. flowkeyd 启动时，对当前已经存在的窗口过一遍。

之后**不再干预**：你自己移动 / 缩放窗口、取消钉住、取消置顶都不会被纠正，直到下次
显示器重新接入或者重启 flowkeyd。被当成“主窗口”的条件是：可见、没有属主、不是工具
窗口（`WS_EX_TOOLWINDOW`）、有标题、尺寸非零 —— 应用的内部辅助窗口不会被误摆。

**规则真的把窗口移到了另一张桌面时，视图也跟着过去**（只限“窗口第一次出现”那一遍）：
flowkeyd 会切到目标桌面并重新激活那个窗口，于是“启动它”一次就能看到它 —— `Win+3`
唤起 WPS 不会看起来毫无反应。启动与显示器重新接入那两遍只是重新摆放已经在位的窗口，
从不切视图；窗口本来就在目标桌面上时（例如一个已经在第 3 张桌面的程序又开了一个窗口）
也不算“搬迁”，同样不切。

### `app{ ... }`

把**同一个程序**的窗口摆放规则、启动参数与快捷键写在一起，省掉重复的 `process` /
`title` / `launch`。它本身不是新能力：加载配置时会展开成一条普通的 `window_rule` 与
若干条普通的 `hotkey`，排在同一次注册顺序的最后。

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
* `window` 就是一条 `window_rule`，字段完全一样；`process` / `title` / `name` 自动
  继承，显式写在 `window` 里的优先。不写 `window` 就只展开快捷键。
* `launch` 就是这个程序怎么启动，字段与 `window` 动作的 `launch` 完全一样
  （`program`、`args[]`、`cwd`、`show`、`shell`、`env{}`、`wait_ms`）。`hotkeys` 里的
  `window()` 动作会自动继承它；动作自己写了 `launch` 时**逐字段合并**（写了的覆盖，
  没写的继续继承），动作顶层的 `wait_ms` 覆盖 `launch.wait_ms`。于是最常见的写法就是
  `action = window("activate")`。
* `hotkeys` 里每一项就是一条 `hotkey`；其中的 `window()` 动作会自动补上 app 的
  `process`（写到动作的 `process`）与 `title`（写到动作的 `target`），以及 app 的
  `launch`，显式写的优先。嵌套在 `menu` 条目里的 `window()` 动作同样继承。**只对
  表 / 构造器形式的 `window` 动作生效**：简写字符串里没有可继承的字段。
* 名字：`name` 不写时用 `process`，再退到 `title`。这个名字会给展开出来的
  `window_rule` 用；app 里**只有一个 hotkey** 时也用它当这条绑定的默认名字（多个时用
  第一个和弦，免得重名）。
* `enabled = false` 把整条 app（规则 + 全部快捷键）都丢掉，并给一条 warning。

**与全局条目的关系**：`hotkey{}` / `window_rule{}` 仍然照旧可用 —— 没有窗口规则的
快捷键、或没有快捷键的规则，继续单独写。app 展开出来的条目排在全局条目**之后**：
快捷键冲突时先注册的赢，窗口规则仍然是先写的赢，所以全局规则会先于 app 规则匹配。

## 托盘、日志窗口与弹窗

托盘图标、日志窗口与五个弹窗（选单 / 帮助 / 窗口切换器 / 程序启动器 / 在线更新）用的是
**应用自己的图标**。exe 文件本身在资源管理器/任务栏里显示的那个图标是另一份东西——嵌在 PE 资源里
的 `.ico`。

### 日志窗口

**日志窗口**（左键单击托盘图标）是一个**进程内的窗口**：它尾随日志文件，每 250 ms 追
一次新行，最多显示最后 1000 行，按级别配色，带一个子串筛选框，新行会自动滚到底（你往
上翻时不会打扰你）。窗口标题带着已显示的行数（`flowkeyd 日志 — 128 行`）。

它是**同一个进程里的普通窗口**，所以：关掉它不会退出守护进程；再点一次托盘图标只会把
它抬到前面，不会开出第二个；退出只能走托盘菜单的*退出*、`quit` 动作，或 `--quit`。

日志写控制台，同时（守护进程模式下总是）追加写到
`%USERPROFILE%\.config\flowkeyd\flowkeyd.log`（`--log-file` 可以指定别的地方，
`--log-level` 调整级别）。

### 选单与电源

`menu` 弹出一个无边框圆角卡片。列表是 Qt 自带的 `ListView` + 标准 `ItemDelegate`，
所以悬停、按下与高亮全部由标准样式画，鼠标点击与滚轮也是标准列表控件的行为：

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
* **鼠标**：悬停到哪一行，高亮就在哪一行（`Enter` 执行的就是它）；**左键单击一行 =
  执行它**；点到别的地方也会把选单关掉（和系统菜单一样）。指针离开卡片之后高亮回到
  键盘选中项。选单不滚动（条目数决定卡片高度），所以滚轮在它上面不做事。
* 不写 `action`（或写 `none()`）的条目只是把选单关掉；不带 `key` 的条目只能鼠标/方向
  键选。
* **配色跟随系统**（浅色/深色主题都会跟着变），窗口尺寸、内边距、圆角与字号按所在
  显示器的 DPI 缩放；**中文字体是微软雅黑**，不跟随系统字体设置。
* 一次只会有一个选单：再按一次快捷键只是把它拿到前面。
* **卡片不会出现在任务栏里**（也不会进 `Alt+Tab`）：它是一个工具窗口。`help` 与窗口
  切换器是同一套标志。

`power` 的取值：`sleep`（睡眠）、`hibernate`（休眠）、`shutdown`、`restart`、
`logoff`（注销）、`lock`（锁定）、`screen_off`（关屏）。关机/重启/注销只带
`EWX_FORCEIFHUNG`（只强杀已经卡住、不响应 `WM_QUERYENDSESSION` 的程序），**不会**用
`EWX_FORCE`：有未保存内容的程序照样会弹它自己的确认框。

`screen_off` 只把**全部**显示器送进待机，系统、应用和 flowkeyd 的钩子都继续照常运行
（**不是**睡眠，也不锁屏）。随便按一个键或动一下鼠标，显示器就回来了。

关机、重启、注销需要管理员权限（flowkeyd 默认就是提权运行的）；没提权时会在日志里写
一行错误并弹一个托盘气泡说明原因。睡眠、休眠、锁定与关屏不需要权限。

上面那条 `Win+x` 会吞掉 Windows 自己的“快捷链接菜单”；想保留系统菜单就换一个键。

### 快捷键帮助

`help` 弹出同一套卡片，但它不是让你挑一项，而是把**当前配置里全部生效的快捷键**列出
来：左边是按键徽标，右边是配置里的 `comment`（没写就用 `name`）和一行灰色小字（这个
快捷键到底会做什么）。

```lua
hotkey{
  name = "help",
  comment = "Win+/：列出当前所有快捷键（可输入筛选、Enter 执行）",
  keys = "Win+/",
  action = help(),
}
```

* **筛选**：筛选框就是一个普通的输入框（鼠标点一下就能进去打字，光标、选区、输入法、
  右键菜单、`Home`/`End`/左右箭头都是标准行为）。输入就按子串过滤，和弦、
  `comment`/`name`、动作摘要都参与匹配；窗口会跟着结果变矮（顶边不动），标题右侧显示
  `可见 / 总数`。`Esc` 依次是「取消待确认 → 清筛选 → 关窗」。
* **滚动**：`↑`/`↓`、`PgUp`/`PgDn` 移动**键盘选中项**（高亮就是它）；鼠标滚轮与右侧
  的滚动条（可以拖）只滚视图，**不会**动键盘选中项。条目比窗口高时滚动条才出现。
* **鼠标**：把鼠标移过某一行**不会**改变高亮；**左键点某一行 = 选中它 + 把它的按键
  文本复制走**；**双击某一行 = 选中它并执行它的动作**。
* **执行**：`Enter`（或双击）= **执行键盘选中项那一行**。窗口会先关掉再执行 —— 这样
  `send`/`type`/`window` 这类动作作用在原来的前台应用上，而不是打回帮助窗口自己的筛选
  框。触发的效果等价于按一下那个快捷键（先执行按下时的动作、再执行松开时的动作）；
  重映射那一行等价于按一下源键（`CapsLock → Esc` 就会注入一次 `Esc`）。
* **危险动作要两次**：`quit`、`suspend` 与 `power` 这几类不会一按就执行 —— 第一次
  `Enter`/双击只是把它标成「待确认」（那一行变色，底部提示换成确认文案），再按一次才
  真的执行。`Esc`、上下换行、改筛选都会取消确认。
* 列表由配置本身生成，所以 `help()` 不需要任何参数；卡片顶部的标题可以换：
  `help("我的快捷键")`。
* 一次只会有一个帮助窗口：再按一次快捷键只是把它拿到前面并清空筛选。
* 和 `Win+x`/`Win+s` 一样，`Win+/` 会吞掉 Windows 自己的那个快捷键（表情/输入法
  面板）；想保留就换一个键。

### 窗口切换器

`windows()` 弹出一张卡片，列出**当前所有打开的程序窗口**（进程名 + 标题），输入进程名
前缀就筛选、选中就切过去：

```lua
hotkey{
  name = "window-switcher",
  comment = "轻碰一下 Win：切换窗口",
  keys = "LWin",
  trigger = "release",
  action = windows(),
}
```

* **卡片没有标题行**：筛选框就是卡片的第一行，窗口列表紧跟在它下面而且**与筛选框一样
  宽**；「N / M 个窗口」的计数在底部提示里。`windows("切换窗口")` 给的名字只用作
  **窗口标题**（卡片是无边框的，界面上看不到它）。
* **每一行是「进程名在上、窗口标题在下」**：进程名是主行（筛选按它匹配），窗口标题
  是下面的浅色小字（只用来分辨同一个程序的多个窗口）。
* **一打开就把输入法切成英文**（鼠标点回筛选框时也会再确认一次）：筛选框匹配的是进程
  名，而用户经常正开着中文输入法 —— 那样打进去的是候选字，一条都筛不出来。**关掉卡片
  时会把打开前的模式写回去**。它只影响 flowkeyd 自己这个**进程**，**不影响**你在别的
  应用里的中/英文状态；想在卡片里打中文仍然可以自己按 `Shift` 切过去。
* **筛选**：筛选框就是一个普通的输入框，输入按**进程名的前缀**过滤（大小写无关）：
  打 `chr` 列出所有 Chrome 窗口，打 `flow` 列出 `flowkeyd.exe`；窗口标题只显示、
  **不参与匹配**。
* **自动激活**：筛选结果**只剩一个窗口**时直接激活它并把卡片关掉 —— 不必再按 `Enter`。
  刚打开（筛选框为空）时不会自动激活，哪怕只有一个窗口。
* **数字选择模式**：筛选串命中的窗口**全属于同一个进程名**、而且不止一个时，每一行
  左边会出现一个数字快捷键 —— 前 10 行依次是 `1`..`9`、`0`，按数字就直接跳到那个
  窗口（超过 10 个的窗口不分到按键）。那种模式下数字归快捷键，**不会跑进筛选框**。
* 多条时用 `↑`/`↓` 选择后 `Enter` 切换，或者把鼠标悬停在某一行（悬停即高亮）再左键
  单击；`Esc` 关掉卡片。窗口多于屏幕能放下的行数时右侧的滚动条可以拖。
* **再按一次触发它的快捷键 = 关掉卡片**（与 `Esc` 同义）：对上面那种「轻碰 Win」的
  绑法来说就是**再轻碰一下 Win**（第二次轻碰时卡片已经开着，于是把它收起来，而不是
  重新弹一次）。
* 列表按 Z 序（最近用过的在前）排列，**跨虚拟桌面的窗口也会列出来**：选中它会把视图
  切到那张桌面并激活它。工具窗口、没有标题的窗口、尺寸为空的窗口，以及 flowkeyd 自己
  的弹窗 / 日志窗口不在列表里。**被 Windows 藏起来的“假窗口”也不会列**（例如 Windows
  输入法的宿主 `TextInputHost.exe` 的「Windows 输入体验」）—— 这正是“只列真正有窗口
  的进程、与 `Alt+Tab` 同一套判据”。**在别的虚拟桌面上的窗口是另一种情况，仍然会
  列**（激活时会切过去）。
* 简写 `action = "windows"` 等价于 `windows()`。
* **「轻碰 Win」= 单个修饰键 + `trigger = "release"`**：按下 Win 本身照常传给系统
  （所以 `Win+E`、`Win+L` 这些没被 flowkeyd 接管的系统组合完全不受影响），期间没有按
  过别的键、松开时才触发；触发时注入一个未分配的标记按键，挡掉 Windows 自己的开始
  菜单。也就是说：**只有“单独按一下 Win”被换成了窗口切换器**。
* **卡片不会出现在任务栏里**（也不会进 `Alt+Tab`）。

### 程序启动器

`apps()` 弹出一张卡片，把开始菜单里的程序分成三段（每一项是图标 + 名字）：**已固定**、
**最近使用**（最多两行）、以及一个 **全部程序（N）** 按钮。输入就按名字筛选、`Enter`
或左键单击启动它：

```lua
hotkey{
  name = "app-launcher",
  comment = "Win+Space：程序启动器",
  keys = "Win+Space",
  action = apps(),
}
```

* **列表就是开始菜单**：递归扫「全局开始菜单」与「当前用户开始菜单」两个
  `…\Start Menu\Programs` 目录，用 `IShellLink` 解析每一条快捷方式，**只留程序**——
  目标以 `.exe` 结尾的（绝大多数），以及只有 IDList 的商店/UWP 应用；文件夹、文档、
  网址、坏掉的快捷方式不会进来。清单与你在开始菜单里看到的是同一份，装完新程序按一下
  就会出现（扫描结果最多缓存 30 秒）。
* **名字就是快捷方式的文件名**（去掉 `.lnk`），与资源管理器里看到的一致；**图标是
  shell 给的那一个**，按当前屏幕缩放取合适尺寸（200% 下取 80 像素的图，而不是把 40
  像素的放大揉掉），而且是**弹出之后一格格异步长出来的**（每个约 3 ms，不会让卡片
  等在那里）。
* **启动走的就是那条快捷方式**（`ShellExecuteW "open"`）：参数、工作目录、`runas`
  标记、商店应用的激活全部交给 shell，与点开始菜单一样。
* **卡片分三段（从上到下）**：
  * **已固定** —— 你按 `Space` 固定过的那些程序（网格，顺序就是你固定的顺序）；
  * **最近使用** —— 真的从启动器里启动过的程序，最多两行（12 个），最近的在前；
  * **全部程序（N）** —— 一个按钮。点它（或把高亮移到它上面再按 `Enter`）就把同一张
    卡片换成**全部程序的列表**：按**名字 / 拼音首字母分组**（`A`–`Z`；数字、符号、
    拼音表外的生僻字开头的一律归 `#`，例如 `7-Zip`、`【小狼毫】…`），一行一个程序、
    带滚动条 —— 与 Windows 10 开始菜单的「所有应用」同一手感；`Esc` 或列表最上面那个
    「← 返回」回到上面那两段。

    **还什么都没固定、也没启动过任何程序时**（第一次用），卡片直接就是全部程序的
    网格，不用先点按钮。
* **`Space` = 固定 / 取消固定**当前高亮的那个程序（固定住的图标右上角会有一个小圆点）。
  固定列表与「最近使用」都存在配置文件旁边的 **`launcher.json`** 里，重启 flowkeyd 也还在；
  目录里已经没有的程序（卸载了）只是暂时不显示，不会从文件里被删掉。
  **筛选框里有字的时候 `Space` 是打空格**（不然打不出 `visual studio` 这种带空格的名字）。
* **网格是 6 列**（格子 126 × 88，卡片宽 800；高度跟着当前内容走，超出一屏用右侧滚动条滚）。
  `↑`/`↓` 走一整行（**会跳过表头**）、`←`/`→` 走一格、`PgUp`/`PgDn` 翻页、
  `Home`/`End` 到头尾；到边界夹住、不回绕。
* **筛选**：输入框里直接打，按**名字的子串**过滤（大小写无关，中文名字照常匹配），
  所以打 `code` 能找到 `Visual Studio Code`。**汉语拼音也能搜**：
  * **全拼**：`jishiben` 找到「记事本」，`jis`、`ben` 这类片段也行；多音字的每一种读音
    都认（「网易云音乐」用 `yinle` 与 `yinyue` 都能搜到）。
  * **首字母**：`jsb` 找到「记事本」，`wx` 找到「微信」，拉丁名字也一样 ——
    `vsc` 找到 `Visual Studio Code`（中文与拉丁混排的 `QQ音乐` 是 `qyy`）。
  * 拼音表覆盖 GB 里的常用汉字（U+4E00–U+9FFF）；表外的生僻字只按字面匹配，
    其它字符（空格、标点、数字）同时按字面保留。
  筛选到只剩一个也**不会**自动启动 —— 启动一个新程序比切一个窗口慎重得多。
* **`Enter` 或左键点一下 = 启动它**（窗口先关掉、再启动），`Esc` 关窗；卡片开着时
  **再按一次同一个快捷键就是关掉它**（与 `Esc` 同义）。在「全部程序」列表里 `Esc` 是
  「返回概览」，再按一次才关窗。
* **筛选之后的快速启动键**（easymotion 风格）：输入筛选串之后，最前面 10 个结果会在
  **图标右上角**各带一个数字（第 1 个是 `0`、第 2 个是 `1`……第 10 个是 `9`），**按一下
  就直接启动它** —— 想开 `Visual Studio Code`，打 `vsc` 再按 `0` 就行。没有对应结果的
  数字被吃掉但什么都不做（**不会**漏进筛选框把列表筛空）；筛选框为空时数字键照常输入
  （`7-Zip` 这类名字要用数字筛）。号码只给前 10 个，多出来的没有。
* **右键点一格 = 那个程序的 Windows 右键菜单**（与资源管理器、开始菜单逐条一致）：
  「打开」「以管理员身份运行」「打开文件位置」「固定到…」「属性」「卸载」这些条目全部
  由 shell 自己生成、自己执行，flowkeyd 不去猜也不去实现它们。菜单出现在光标处，选中
  一条就把卡片收掉（让 shell 打开的东西自己拿前台），按 `Esc` 或点菜单外面取消则卡片
  留着、可以接着选下一格。
* **不切输入法**（与窗口切换器相反）：这里的名字可能是中文（「记事本」），一打开就把
  输入法切成英文反而筛不出东西。
* 简写 `action = "apps"`；`"launcher"` 与 `"programs"` 是它的别名。`apps("程序")`
  给的名字只用作**窗口标题**（卡片是无边框的，界面上看不到它）。
* 注意区分 `apps()` 与 `app{...}`：前者是**圆括号**的动作（弹出启动器），后者是
  **花括号**的注册构造器（把某个程序的窗口规则与快捷键写在一起）。

## 管理员权限与开机自启

* **提权。** 以守护进程模式启动时，如果进程没有管理员令牌，flowkeyd 会用
  `ShellExecuteW("runas")` 把同样的命令行转发给一个提权后的自己，然后退出。UAC 提示
  被拒绝时它不会直接死掉：会打一条 warning 并以普通权限继续跑（钩子照样工作，只是驱动
  不了提权进程的窗口）。
* **不提权的场合。** `--check`、`--list`、`--list-keys` 都是离线命令，永远不会弹 UAC。
  另外 `--no-elevate`（或 `settings.elevate = false`）能完全关掉提权。

flowkeyd **自己**会注册一个**登录时触发**的计划任务（*使用最高权限运行*）：每次启动时
它检查这个任务，**不存在、或指向的 exe 与当前正在运行的这个不是同一个，就先弹一个确认
框问你要不要注册 / 更新**，同意之后才用当前路径重新注册一次（不同意就保持原样，下次
启动会再问）。之后每次登录、以及每次机器重启，flowkeyd 都会以管理员权限起来，**不弹
UAC**。

**没有安装目录**：自启跟着你运行的那个 `flowkeyd.exe` 走。把 exe 放到
`D:\Tools\flowkeyd\flowkeyd.exe` 并运行一次，任务就指向那里；换到别处再运行一次，在
确认框里同意之后任务就更新。

为什么必须是计划任务，而不是 `shell:startup` 快捷方式或 `HKCU\...\Run`：

* flowkeyd 需要管理员权限才能驱动提权进程的窗口、才能执行电源动作；只有计划任务能做到
  「提权启动且不弹 UAC」。也**不要**给 exe 登记 `RUNASADMIN` 兼容性标记：那会让**任何**
  调用都提权，连 `flowkeyd --check` 都会弹 UAC —— 离线命令本就不该弹 UAC。
* 服务（Windows Service）不行：它跑在 session 0，`WH_KEYBOARD_LL` 看不到桌面的按键，
  也没有托盘图标。

```powershell
# 删掉自启（需要管理员）。先停实例，否则它下次启动会把任务注册回来。
& D:\Tools\flowkeyd\flowkeyd.exe --quit
& D:\Tools\flowkeyd\flowkeyd.exe --remove-autostart
```

`--no-autostart` 可以临时关掉自启管理；开发 / 测试实例（`--no-elevate`、
`--allow-multi`、或没有提权的进程）本来就**不会**动这个任务。`--no-prompt` 则不弹任何
交互提示：「已在运行」只记日志，自启按默认的「注册 / 更新」处理。

**任务失败是静默的**：路径写错、exe 被删、单实例冲突……结果都只是「没有托盘图标、快捷键
不生效」，不会弹任何东西。排查顺序：任务计划程序里看 `flowkeyd` 这个任务（*上次运行
结果*）、看 `%USERPROFILE%\.config\flowkeyd\flowkeyd.log`、再手动跑一次
`Start-ScheduledTask -TaskName flowkeyd`。

想立刻关掉正在运行的实例：

```powershell
& 'D:\Tools\flowkeyd\flowkeyd.exe' --quit   # 换成你自己的 exe 路径
```

`--quit` 按**配置文件路径**匹配实例（`--config` 可选），最多等 10 秒；它走的是一条命名
的事件通道，让守护进程走**干净的退出路径**（卸钩子、退循环），而不是 `taskkill /F` ——
后者会留下一个幽灵托盘图标。没有在跑的实例时它返回 1，不算错误。不提权的调用方也能请
提权的守护进程退出。

## 在线更新

托盘右键菜单里的 *检查更新(&U)...* 会弹出一张「在线更新」卡片：它问一次 GitHub 上最新
Release 的版本号与发布说明（这次更新大概改了什么），显示给你看；发现新版本时点「立即
更新」，卡片里出现**下载进度条**，下载完校验通过之后 flowkeyd 会**自己把 exe 换掉并
重启**，重启起来的那个新实例再弹一条 Windows 通知告诉你更新成功了。

* **来源就是发布那套东西**：`https://api.github.com/repos/xingjianxu/flowkeyd/releases/latest`。
  下载的是发布脚本传上去的**精简升级包**（`*-slim-windows-x64.zip`，里面只有
  `flowkeyd.exe`）；某次发布万一没有精简包，会退回到完整包。HTTPS 由 Windows 自带的
  Schannel 提供，并用发布资产里的 **sha256** 校验下载结果。
* **只换 exe，不动运行时**：这就是「第一次安装用完整包、以后升级用精简包」那条约定。
  所以**如果某个新版本依赖一个新的运行时文件，光换 exe 是起不来的**；那种情况下替换会
  **自动回滚**，你应该手动下载完整包。
* **替换是原子的，而且会回滚**：顺序是「旧的 `flowkeyd.exe` 改名成 `flowkeyd.exe.old`
  → 新的改名就位 → 启动新实例 → 等 2.5 秒确认它还活着」。任何一步失败（安装目录写不
  进去、新 exe 起不来）都会把旧的那份改回来，日志里有一条
  `ERROR could not apply the update: ...` 并弹一个提示框（`--no-prompt` 时只写日志）。
  新实例启动后会删掉 `flowkeyd.exe.old`。
* **不会自动检查**：只有你点菜单才联网，不需要 token（公开仓库的匿名请求）。
* **权限**：替换 exe 需要能写安装目录，所以安装目录在 `C:\Program Files` 这类地方时要
  保持提权运行（flowkeyd 默认就是提权的）。
* **不动你的东西**：配置文件、日志文件、开机自启的计划任务都原样保留。
* 没有新版本时卡片里写「已经是最新版本」；检查失败（断网 / 被 GitHub 限流 / 这次发布
  没有可下载的资产）时写一句中文人话，下面一行是英文的技术原因，旁边还有 *打开发布页*
  可以手动下载。

## 已知限制

* **快捷键的修饰键仍然会被送达到前台应用**；只有和弦的最后一个按键被隐藏。AutoHotkey
  会通过缓存修饰键按下、并在没有快捷键成形时重放来隐藏整个和弦。这是与 AutoHotkey 的
  主要行为差异。
* **动作不能是 Lua 函数**：动作要能被 `--list` 显示、要能在加载时校验完、还要在钩子
  回调与工作线程的边界上保持安全。想要“自定义逻辑”就用循环与表达式去生成声明式动作。
* DSL 报错时，如果那次 `hotkey{}`/`remap{}`/`settings{}` 调用正好是脚本的**最后一条
  语句**，Lua 的尾调用会丢掉调用者栈帧，错误信息里因此没有行号（消息本身仍然指名了
  出错的构造）。在它后面随便再写一条语句就能拿回行号。
* 重映射里的 `{Sleep}` 会被忽略（会睡觉的钩子会被 Windows 移除）；`--check` 会对它
  给出警告。
* 小键盘只区分了 `Enter`。`NumLock` 关闭时，小键盘的
  `8`/`2`/`4`/`6`/`0`/`.`/`Home`/`End`/`PgUp`/`PgDn` 会上报与主键盘方向键、`Insert`、
  `Delete` 相同的 `VK`，因此绑定 `Up` 也会被小键盘的 `8` 触发。小键盘的
  `-`/`+`/`Enter` 不受影响。
* 挂起期间快捷键不触发、也不吞任何键，这正是 AutoHotkey 的 `Suspend` 行为。
* **远程桌面放行只看前台窗口的属主进程名**（大小写无关的子串匹配）：不看窗口标题，也不管
  它是不是全屏。名单写得太宽（比如 `processes = { "rdp" }`）会命中一堆无关程序。
  第三方远程控制软件（ToDesk / 向日葵 / AnyDesk…）不在默认名单里，要自己写。
* 进入 / 离开远程桌面那一刻，**长按重复**与待定的「轻碰 Win」会作废；已经被呑掉、还按着
  的键在松开时仍然呑掉（否则前台会看到一个孤立的 key-up），而之前被某条重映射按住的目标键
  会立刻松开（`remote_desktop = true` 的例外不动它）。
* 除非 flowkeyd 自己也提权，否则 `window` 动作无法驱动提权进程的窗口。反过来，提权后
  的 flowkeyd 启动的子进程会继承管理员令牌（`run`、`open`、`window.launch`、以及**程序
  启动器启动的程序**都是）。
  需要普通权限时可以让 `explorer.exe` 代劳（`action = "run:explorer.exe path"`），
  或者直接用 `--no-elevate` 运行 flowkeyd。
* **程序启动器列的是开始菜单里的条目本身**：卸载程序（「卸载微信」）、输入法的工具项
  （「【小狼毫】输入法设定」）这类名字也会在列表里 —— 它们确实是指向 exe 的程序，
  所以 flowkeyd 不按名字猜“这算不算噪音”。按名字筛一下就能排开。第一次打开（或者隔了
  30 秒以上）会现场重扫开始菜单，那一瞬间多花几十毫秒。
* **程序启动器的「固定」与「最近使用」存在配置文件旁边的 `launcher.json`**（不是配置
  文件本身）。它是 flowkeyd 自己写的运行时状态：坏掉时只会当作空的重新开始（日志里一条
  warning），不会影响启动；里面存的是快捷方式路径的哈希，不是名字 —— 所以改名字、挪
  开始菜单目录都还认得出来，而删掉它只会丢掉“固定了哪些”这个记忆。
  「全部程序」列表的分组按**拼音首字母**：拉丁名字用第一个字母，数字 / 符号开头的
  （`7-Zip`、`【小狼毫】…`）统统归到 `#` 那一组，排在字母组前面。
* 被吞掉的 `Win+…` 快捷键会在 Windows 键松开时注入一个未分配的按键，挡掉开始菜单。
  遮断只影响外壳怎么看那个修饰键：动作本身在按键按下时就跑了（`trigger = "press"`，
  默认），只有 `trigger = "release"` 的绑定才会等到松开。
* 目前只挂钩键盘；鼠标按键和滚轮还不能做快捷键。
* **日志窗口是进程内的窗口**，不是命令行窗口：它最多显示最后 1000 行（完整内容见日志
  文件），带一个子串筛选框，但还没有 `--follow`/`--grep` 之类的命令行参数。打开着日志
  窗口时，`taskkill /PID`（**不带** `/F`）退不掉进程，要走托盘 *退出*、`quit` 动作、
  `--quit`，或直接 `/F`。
* 守护进程**一直把日志文件开着写**，所以用 .NET 默认共享模式读它会报“文件正由另一进程
  使用”：用 `Get-Content -Encoding UTF8`，或者自己用 `FileShare.ReadWrite` 打开。
* **五个弹窗在启动时就预热好了**：代价是常驻进程一启动就把图形栈与五张卡片的界面常驻
  下来（2026-10 零实测：工作集约 200 MB、私有内存约 300 MB）。这笔钱其实躲不掉 —— 只要打开过一次日志
  窗口或任何弹窗，同一个图形栈也会常驻；预热只是把它从“第一次用到的时候”挑到启动时，
  换来的是第一次弹出与之后一样快。
* 帮助窗口里 `Enter`/双击会**真的执行**那一行的动作。只想把快捷键抄走就用**左键单击**。
* **在线更新只换 `flowkeyd.exe`**：它**不自带签名**（Windows SmartScreen 可能对下载
  下来的 exe 有意见），也**不会**帮你更新 Qt / MinGW 运行时；如果某个新版本新增了运行
  时依赖，替换会回滚（程序仍在，但不会变新）—— 那时要手动下载完整包。
* **「轻碰 Win」会接管 `LWin` 的“单独按一下”**：单独按一下 Win 不再打开开始菜单，而是
  弹窗口切换器（卡片开着时再轻碰一下就是把卡片关掉）。Win 的按下仍然照常传给系统，
  所以 `Win+E`、`Win+L`、`Win+Shift+S` 这些**没被 flowkeyd 接管**的系统组合不受影响。
* 窗口切换器的筛选是按**进程名前缀**匹配（不是子串、不是模糊搜索），窗口标题不参与；
  `help` 的筛选仍然是子串匹配。两者的列表都是按下快捷键那一刻枚举出来的快照：之后新
  开的窗口要重新按一次才会出现。`help` 列出的是当前配置里的绑定，改完配置要 `reload`
  才会反映出来。
* **动作表顶层的拼写错误是静默的**：`window("activate", { togle = false })` 里的
  `togle` 会被直接忽略。`hotkey{}`、`settings{}`、`remap{}` 以及选单的**条目**都是
  严格检查的，只有动作表这一层没有。
* 关机/重启/注销需要 flowkeyd 提权（默认如此）；非提权的实例上按这些条目只会得到一条
  日志和一个气泡提示。睡眠/休眠与锁定不挑权限。
* **托盘图标上的数字是轮询出来的**（每 500 ms 一次），所以拿它当“切成功了吗”的反馈时
  最多会晚半秒。锁屏、非交互会话或接口对不上时查不到当前桌面，这时图标退回应用图标、
  悬停提示里也没有桌面信息。桌面到两位数时显示 `9+`。
* 托盘图标还不跟随 explorer 重启，所以计划任务的登录触发器加了 15 秒延迟：启动得太早
  会拿不到托盘图标，而且本版本不会在 explorer 回来后自己补上。
* 按桌面编号跳转虚拟桌面（`desktop`）与 `window_rule` 的 `desktop`、跨桌面唤醒依赖
  shell 未公开的 COM 接口。它们没有公开的 ABI 承诺：IID 与 vtable 布局会随 Windows
  版本变化，flowkeyd 里是一张按 `build.revision` 索引的表。新的 Windows 版本如果又改了
  接口，切换会以一条带 HRESULT 的日志失败。
* `window_rule` 的 `all_desktops` 走的是 shell 另一个未公开接口；**这个 IID 自 Windows
  10 起就没变过**，所以它没有版本表。拿不到那个接口时只记一条 warning，规则里其余部分
  照常生效。`topmost` 用的是公开的 `SetWindowPos`，没有这个风险。
* 跟着窗口切过去（`window_rule` 搬完窗口 / `window` 动作跨桌面唤起）是**故意的行为
  变化**，不是所有程序都欢迎它：某个后台程序如果在启动时开了自己的窗口，而你正好给它写
  了 `window_rule`，你的视图会被拽到那张桌面上。不想被拽就把 `desktop` 从那条规则里
  去掉（只留 `monitor`），或者把规则整个 `enabled = false`。
* `window_rule` 只在**窗口第一次出现、显示器重新接入、以及 flowkeyd 启动时**生效；之后
  你手动移动 / 缩放窗口、取消钉住、取消置顶都不会被纠正。它也不管已经开着的窗口 ——
  想重新归位就重启 flowkeyd。
* **前台窗口可以是个“覆盖层”**：`window` 动作会跳过 `WS_EX_TOOLWINDOW` 的覆盖层、沿
  Z 序找第一个真正的主窗口；如果你的某个正常窗口本身就是工具窗口，它的快捷键可能就会
  落到别的窗口上。
* 目前**没有** `--simulate` / `--selftest` / `--probe` 这些调试开关；鼠标钩子、延迟
  修饰键抑制（让 `Ctrl+Alt+h` 也隐藏 Ctrl 和 Alt）、配置文件热重载、按应用限定的快捷
  键也都在待办里。

## 许可证

MIT。

程序启动器的拼音筛选用到的「汉字 → 读音」表（`src/core/pinyin_data.cpp`）是从
[mozillazg/pinyin-data](https://github.com/mozillazg/pinyin-data)（MIT，读音源自
Unicode Unihan 数据库的 `kMandarin` 字段）离线生成后提交进仓库的；生成脚本在
`tools/pinyin_gen.ps1`，重新生成的方法写在那个脚本的注释里。
