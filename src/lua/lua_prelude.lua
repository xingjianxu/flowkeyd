-- flowkeyd 配置脚本的 DSL 预置环境。
--
-- 本文件由 src/lua/lua_config.cpp 注入到每个配置脚本的全局环境里；
-- 最上面的 `...` 是 C++ 传进来的注册表（settings / hotkeys / remaps /
-- window_rules / apps 五个列表）。
--
-- 两种写法可以混用：
--   * 命令式：hotkey{...} / remap{...} / window_rule{...} / app{...} / settings{...}
--   * 声明式：return { settings = {...}, hotkeys = {...}, remaps = {...},
--                      window_rules = {...}, apps = {...} }
-- 返回值里的条目排在脚本体注册的条目之后（快捷键按注册顺序匹配，先者优先）；
-- app{...} 展开出来的 hotkey / window_rule 也排在同一次注册顺序的最后。
--
-- 动作一律是「表」或「简写字符串」，不接受 Lua 函数：钩子回调与工作线程
-- 只执行已经校验过的声明式动作。
--
-- 两个 Lua 语言本身的坑（配置里一定会遇到）：
--   * `repeat` 是 Lua 关键字，`hotkey{ repeat = true }` 是语法错误；
--     请写 `repeatable = true`（推荐），或者 `["repeat"] = true`。
--   * 字符串里的反斜杠要转义：Windows 路径用长字符串
--     `[[C:\tools\flowkeyd.exe]]` 或者写成 `'C:\\tools\\flowkeyd.exe'`。

local state = ...

local function is_table(value)
  return type(value) == "table"
end

-- 动作（或动作列表里的某一项）写成 Lua 函数时，给出比
-- 「unsupported value type `function`」说得更清楚的提示。
local function reject_function(value, label, what)
  if type(value) ~= "function" then
    return
  end
  error(what .. ": " .. label .. " cannot be a Lua function; actions are declarative " ..
    "(use send(\"^{c}\"), run(\"notepad.exe\"), notify(\"done\"), ...), see README.md", 2)
end

local function reject_functions(t, what)
  for _, field in ipairs({ "action", "on_release", "press" }) do
    local value = t[field]
    reject_function(value, "`" .. field .. "`", what)
    if is_table(value) then
      for index, item in ipairs(value) do
        reject_function(item, "`" .. field .. "[" .. index .. "]`", what)
      end
    end
  end
  -- `menu` 的条目也是声明式的：`items[i].action` 同样不能是函数。
  if is_table(t.items) then
    for index, item in ipairs(t.items) do
      if is_table(item) then
        reject_function(item.action, "`items[" .. index .. "].action`", what)
        if is_table(item.action) then
          for step, nested in ipairs(item.action) do
            reject_function(nested, "`items[" .. index .. "].action[" .. step .. "]`", what)
          end
        end
      end
    end
  end
end

local function registration(t, what)
  if not is_table(t) then
    error("`" .. what .. "` expects a table, got " .. type(t), 2)
  end
  reject_functions(t, what)
  return t
end

-- 注册一个快捷键：hotkey{ keys = "Ctrl+Alt+t", action = run("wt.exe") }
function hotkey(t)
  state.hotkeys[#state.hotkeys + 1] = registration(t, "hotkey")
end

-- 注册一个重映射：remap{ from = "CapsLock", to = "Esc" }
function remap(t)
  state.remaps[#state.remaps + 1] = registration(t, "remap")
end

-- 注册一条窗口摆放规则：某个程序的窗口出现时放到哪个虚拟桌面 / 显示器。
--   window_rule{ process = "wezterm", desktop = 2, monitor = 2 }
--   window_rule{ process = "code", monitor = "primary", maximize = false,
--                x = 0, y = 0, width = 1280, height = 800 }
--   window_rule{ process = "wezterm", all_desktops = true }   -- 钉在所有虚拟桌面上
--   window_rule{ process = "code", topmost = true }           -- 始终在最上层
-- 触发时机只有三个：窗口第一次出现、断开的显示器重新接上、flowkeyd 启动时；
-- 之后不再干预（用户自己移动/缩放窗口不会被纠正）。
-- all_desktops = true 与 desktop 互斥；all_desktops / topmost 写 false 就是
-- 取消钉住 / 取消置顶，不写则不去碰它们。
function window_rule(t)
  state.window_rules[#state.window_rules + 1] = registration(t, "window_rule")
end

-- 把一个程序（进程名 / 窗口标题）的窗口摆放规则与唤起它的快捷键写在一起，
-- 省掉在 hotkey 与 window_rule 里各写一遍 process / title：
--   app{
--     process = "wps",                     -- 也可只写 title；至少写一个
--     name = "wps",                        -- 可选，默认用 process（再退到 title）
--     launch = {                           -- 可选：这个程序怎么启动
--       program = [[C:\tools\ksolaunch.exe]],
--       args = { "/prometheus" },
--       wait_ms = 10000,
--     },
--     window = { desktop = 3, monitor = 2 },
--     hotkeys = {
--       -- launch 自动继承，所以这里只写要覆盖的部分
--       { keys = "Win+3", action = window("activate", { wait_ms = 5000 }) },
--     },
--   }
-- `window` 的字段与 window_rule 完全相同（process / title / name 自动继承）；
-- `hotkeys` 里每一项与 hotkey 完全相同，其中的 window 动作会自动补上
-- process / title / launch（显式写的优先：动作自己的 launch 逐字段覆盖 app 的，
-- 顶层的 wait_ms 覆盖 launch.wait_ms）。
-- 全局的 hotkey{} / window_rule{} 仍然保留：没有窗口规则的快捷键、
-- 或没有快捷键的规则，照旧单独写。
function app(t)
  state.apps[#state.apps + 1] = registration(t, "app")
end

-- 注册一组全局设置。可以多次调用，后写的键覆盖先写的。
function settings(t)
  state.settings[#state.settings + 1] = registration(t, "settings")
end

-- 把 opts 里的键合并进动作表；opts 为 nil 时原样返回。
local function merge(action, opts)
  if opts == nil then
    return action
  end
  if not is_table(opts) then
    error("expected a table of options, got " .. type(opts), 3)
  end
  for key, value in pairs(opts) do
    action[key] = value
  end
  return action
end

-- 动作构造器。它们等价于手写 `{ type = "...", ... }`，
-- 放在循环和表达式里更顺手：
--   for i = 1, 4 do hotkey{ keys = "Win+F" .. i, action = desktop(i) } end

-- run(program[, args][, opts])
--   run("wt.exe")
--   run("wt.exe", { "-w", "0" })
--   run("git status --short", { shell = true, wait = true })
function run(program, args, opts)
  if type(program) ~= "string" then
    error("run() expects a program string, got " .. type(program), 2)
  end
  local list, options = {}, {}
  if is_table(args) then
    -- 有数组部分（或就是个空表）时当作参数列表，否则整张表都是选项。
    if #args > 0 or next(args) == nil then
      list = args
    else
      options = args
    end
  elseif args ~= nil then
    error("run() expects a table of arguments as its second parameter", 2)
  end
  local action = merge({ type = "run", program = program }, options)
  merge(action, opts)
  -- 空参数表就别写进去了：空表在 Lua 里含义模糊，而且默认值本来就是空。
  if next(list) ~= nil then
    action.args = list
  end
  return action
end

function send(keys, opts)
  if type(keys) ~= "string" then
    error("send() expects a key script string, got " .. type(keys), 2)
  end
  return merge({ type = "send", keys = keys }, opts)
end

function type_text(text, opts)
  if type(text) ~= "string" then
    error("type_text() expects a string, got " .. type(text), 2)
  end
  return merge({ type = "type", text = text }, opts)
end

function open(target, opts)
  if type(target) ~= "string" then
    error("open() expects a target string, got " .. type(target), 2)
  end
  return merge({ type = "open", target = target }, opts)
end

function notify(title, body)
  if type(title) ~= "string" then
    error("notify() expects a title string, got " .. type(title), 2)
  end
  return { type = "notify", title = title, body = body }
end

function volume(op, opts)
  if type(op) ~= "string" then
    error("volume() expects one of up|down|set|mute|unmute|toggle, got " .. type(op), 2)
  end
  return merge({ type = "volume", op = op }, opts)
end

function media(op)
  if type(op) ~= "string" then
    error("media() expects one of play_pause|next|prev|stop, got " .. type(op), 2)
  end
  return { type = "media", op = op }
end

-- window(op[, opts])
--   window("activate", { process = "wezterm" })
--   window("activate", { target = "Notepad", toggle = false })
--   window("maximize", { animate = true })
--
-- 把窗口挪到相邻的桌面 / 显示器（不写 target / process 就是前台窗口）：
--   window("move_prev_desktop")   -- 上一张虚拟桌面（第一张再往前 = 最后一张，首尾相接）
--   window("move_next_desktop")   -- 下一张虚拟桌面（显示器上的几何不变）
--   window("move_left_monitor")   -- 左边的显示器（保留最大化，否则保持大小并居中）
--   window("move_right_monitor")  -- 右边的显示器（没有更左/更右的显示器时会失败）
--
-- 这两个虚拟桌面 op 还可以带 `follow = true`：搬完窗口之后把**视图也切到目标
-- 桌面**并重新激活那个窗口（Windows 自己的 Win+Ctrl+Shift+←/→ 不跟随视图）：
--   window("move_next_desktop", { follow = true })
-- `follow` 只能写在这两个 op 上，写在其它的 op 上会被 `--check` 拒绝。
--
-- `launch = { program, args[], cwd, show, shell, env{}, wait_ms }` 只在没有窗口
-- 匹配时执行；`wait_ms` 也可以写在动作顶层（它是 `launch.wait_ms` 的简写），
-- 在 app{} 里还会继承 app 的 launch。
--
-- `op = "activate"` 默认带 `toggle`：目标窗口已经在前台时就把它最小化，
-- 于是同一个快捷键在唤起与收起之间切换（任务栏按钮的行为）。
-- `toggle = false` 表示“永远只往前抬，从不收起”。
-- 动画默认关：最小化/最大化/还原不播放 DWM 的过渡（按窗口的
-- DWMWA_TRANSITIONS_FORCEDISABLED，不改系统设置）；`animate = true` 恢复。
function window(op, opts)
  if type(op) ~= "string" then
    error(
      "window() expects one of activate|minimize|maximize|restore|close|toggle_topmost|"
        .. "move_prev_desktop|move_next_desktop|move_left_monitor|move_right_monitor",
      2
    )
  end
  return merge({ type = "window", op = op }, opts)
end

function clipboard(op, opts)
  if type(op) ~= "string" then
    error("clipboard() expects one of get|set|append|clear, got " .. type(op), 2)
  end
  return merge({ type = "clipboard", op = op }, opts)
end

function caps_lock(mode)
  return { type = "caps_lock", state = mode or "off" }
end

function suspend(mode)
  return { type = "suspend", state = mode or "toggle" }
end

function desktop(number)
  if type(number) ~= "number" then
    error("desktop() expects a desktop number (1 is the leftmost), got " .. type(number), 2)
  end
  return { type = "desktop", switch = number }
end

-- 系统电源：睡眠 / 休眠 / 关机 / 重启 / 注销 / 锁定 / 关屏。
--   power("sleep")
-- 关机、重启、注销需要管理员权限（flowkeyd 默认就是提权运行的），
-- 没提权时会失败并弹出气泡提示。
-- screen_off 只关显示器（所有屏幕）而不睡眠，任意按键或鼠标动作都会把屏幕
-- 重新点亮，也不需要任何权限。
function power(op)
  if type(op) ~= "string" then
    error(
      "power() expects one of sleep|hibernate|shutdown|restart|logoff|lock|screen_off, got "
        .. type(op),
      2
    )
  end
  return { type = "power", op = op }
end

-- 弹出一个选单（FluentWinUI3 的 QML 窗口，适配 HiDPI）：
--   menu{
--     title = "电源",
--     items = {
--       { key = "s", label = "睡眠", hint = "Sleep",     action = power("sleep") },
--       { key = "p", label = "关机", hint = "Shut down", action = power("shutdown") },
--     },
--   }
-- 条目上的单字符 key 直接选中它，↑/↓ + Enter 与鼠标也能选；
-- Esc（或点到别的地方）只是把选单关掉。省略 action（或写 none()）的条目同样
-- 只是关掉选单。选中的动作在动作线程上执行，选单窗口只负责选。
function menu(t)
  if not is_table(t) then
    error("`menu` expects a table (`menu{ title = ..., items = { ... } }`)", 2)
  end
  -- `registration` 负责拒绝函数、并让 `items` 保持原生形状；
  -- `type` 是在这里补上的，所以用户不必自己写。
  registration(t, "menu")
  t.type = "menu"
  return t
end

function reload()
  return { type = "reload" }
end

-- 弹出快捷键帮助（见 app/dispatcher + qml/HelpPopup.qml）：一张列出
-- **当前配置里全部快捷键**的卡片，风格与 `menu` 一致。
--   help()
--   help("快捷键")
-- 列表由配置本身生成，所以不需要参数；卡片顶部的标题默认是「快捷键」。
-- 直接输入就筛选（和弦、名字、动作摘要都参与匹配），↑/↓、PgUp/PgDn、
-- Home/End 与鼠标滚轮滚动，Enter（或点某一行）把那一行的按键复制到剪贴板，
-- Esc 先清筛选、再按一次才关窗。
function help(title)
  if title ~= nil and type(title) ~= "string" then
    error("help() expects an optional title string, got " .. type(title), 2)
  end
  return { type = "help", title = title }
end

-- 弹出窗口切换器：列出当前所有可见的程序窗口，直接输入就把**进程名**
-- （以及窗口标题）筛选掉；只剩一个窗口时直接激活它，否则用 ↑/↓ + Enter
-- 或鼠标点选（Esc 关闭）。
--   windows()
--   windows("窗口")
-- 常见的绑法是「轻碰一下 Windows 键」：`keys = "LWin"` 配上
-- `trigger = "release"`（按下 Win 本身**放行**，所以 Win+E / Win+L 这些系统
-- 组合照常工作；只有单独按一下 Win 才会触发，见 README）。
function windows(title)
  if title ~= nil and type(title) ~= "string" then
    error("windows() expects an optional title string, got " .. type(title), 2)
  end
  return { type = "windows", title = title }
end

function quit()
  return { type = "quit" }
end

-- 什么也不做：配合 `swallow = false` 就能屏蔽一个按键。
function none()
  return { type = "none" }
end

-- 同一套函数也挂在 `flowkeyd` 表下，这样脚本自己的全局变量不会把它们遮住。
flowkeyd = {
  settings = settings,
  hotkey = hotkey,
  remap = remap,
  window_rule = window_rule,
  app = app,
  run = run,
  send = send,
  type_text = type_text,
  open = open,
  notify = notify,
  volume = volume,
  media = media,
  window = window,
  clipboard = clipboard,
  caps_lock = caps_lock,
  suspend = suspend,
  desktop = desktop,
  power = power,
  menu = menu,
  help = help,
  windows = windows,
  reload = reload,
  quit = quit,
  none = none,
}
