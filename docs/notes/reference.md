# 参考资料（AGENTS.md 第 5 节后半 + 第 14 节）

> **schema 的权威定义在 `README.md`「配置」那一章与 `flowkeyd.lua.example`。** 这里只是速查、容易做漏的
> 语义、验收脚本的覆盖范围与手工冒烟清单。

## 配置 schema 速查

| 项 | 值 |
| -- | -- |
| 配置目录 / 日志文件 | `%USERPROFILE%\.config\flowkeyd\config.lua` / `…\flowkeyd.log` |
| 工作空间恢复日志 | `<配置目录>\workspace.json`（只有藏着窗口时存在；全部恢复后自动删） |
| DSL 全局表 | `flowkeyd.*`（挂在脚本全局） |
| 示例配置 | `flowkeyd.lua.example` |

CLI 开关：`-c/--config`、`--no-elevate`、`--console`、`--elevated`、`--check`、`--list`、`--list-keys`、
`--quit`、`--restore-windows`、`--no-autostart`、`--remove-autostart`、`--log-window`、`--log-level`、
`--log-file`、`--no-color`、`--allow-multi`、`--no-prompt`、`--updated-from`、`-h/--help`、`-V/--version`。

### 容易做漏的语义

* `settings{}`：`log_level`、`swallow`、`exact_modifiers`、`release_modifiers`、`repeat_interval_ms`、
  `repeat_delay_ms`、`tick_ms`、`input_backend`、`single_instance`、`elevate`、`remote_desktop`、
  `window_monitor`、`workspace`。**未知键报错。** `window_monitor`（默认 `"cursor"`）= 没写明 `monitor` 时
  摆到哪块屏。
* `hotkey{}`：`keys`（单个和弦或一组）、`name`、`trigger`（`press`/`release`/`repeat`）、`action`
  （别名 `press`、`on_press`）、`on_release`、`swallow`、`repeatable`（`true` 或
  `{ interval_ms, delay_ms }`；也认 `["repeat"]`）、`enabled`、`comment`、`remote_desktop`。
  `trigger = "repeat"` 与 `repeatable = true` 是同一件事；**互相矛盾的组合要被拒绝**。
* `remap{}`：`from`（键或和弦）、`to`（键名或发送脚本）、`mode`（`hold` 默认 / `tap`）、`swallow`、
  `name`、`enabled`、`remote_desktop`。
* `remote_desktop`（`settings` 一项 + 每个 `hotkey`/`remap` 一项）：`settings.remote_desktop` 是 `true` /
  `false`，或者 `{ enabled = …, processes = { … } }`；条目上的 `remote_desktop = true` = 在远程桌面里也
  照常拦（默认 `false` = 放行）。
* `window_rule{}`：字段见 `decisions.md` 第 13 条；`monitor` 的六种取值（含 `"cursor"` / `"last"` /
  `"none"`）与缺省行为见第 33 条；`workspace`（1..`settings.workspace.count`）见第 36 条。
* `app{}`：见 `decisions.md` 第 15 条；声明式写法叫 `apps`；再加一个 `workspace`（= `window.workspace` 的
  简写，只写它时会展开出一条只带归属的规则，见第 36 条）。
* `settings.workspace`：`enabled`（默认 `true`）、`count`（1–10，默认 9）、`switch_target`（`"cursor"` 默认 /
  `"focus"`）、`new_window`（`"active"` 默认 / 1..count；**只管运行期间新出现的窗口**，启动收编一律归入
  活动空间）、`exclude`（进程名子串，**空串报错**）；写 `workspace = false` 也能整个关。**空串、超出
  `count` 的索引都在 `--check` 报错**。归属优先级：命中窗口的第一条规则的 `workspace` → `new_window`
  → 那块屏的活动空间（`core::workspaceForNewWindow()`）。
* 和弦语法：`~` 放行原始按键、`*` 忽略额外修饰键；`Numpad*` 与主键盘同名键不同。
  **和弦的结构是 `Modifiers + 一个按键`**：`keys = "NumpadSub+NumpadAdd"` 是语法错（`NumpadSub` 不是
  修饰键），守护进程会直接拒绝启动。
* **单个字母的键名必须小写**（`decisions.md` 第 17 条）。
* 动作：`run`/`send`/`type`/`open`/`volume`/`media`/`clipboard`/`window`/`notify`/`menu`/`help`/
  `windows`/`apps`/`workspace`/`power`/`desktop`/`caps_lock`/`suspend`/`reload`/`quit`/`none`，字段逐条见
  README 的动作表。简写字符串：`"run:…"`、`"send:…"`、`"type:…"`、`"open:…"`、`"notify:t|b"`、
  `"volume:up"`、`"media:next"`、`"clipboard:get"`、`"window:minimize"`、`"desktop:1"`、`"power:sleep"`、
  裸关键字 `reload`/`quit`/`help`/`windows`/`apps`/`none`。`workspace` **没有简写**（要两个参数：
  `workspace("switch", 2)`）；它不算危险动作。`switch` 作用在**哪块屏**由
  `settings.workspace.switch_target` 决定（`"cursor"` 默认 / `"focus"`），`move` 的目标是**当前前台窗口**
  （前台是覆盖层时跳过它）。显隐只对**当前虚拟桌面**上的窗口做（记录过时的窗口会被跳过）。
* **启动权限**：`run` 与 `launch`（`window` 的 / `app{}` 的，共用一份字段块）都有 `elevated`，**默认
  `false` = 当前登录用户的普通权限**；`open` 与程序启动器没有这个字段（它们总是普通权限）。
* **完全没有动作**的快捷键就是一个按键屏蔽器（会吞掉它匹配到的按键）。
* `window` 的 `toggle`（默认**开**）只对 `op = "activate"` 有意义；`launch` 回退不套用它；显式
  `toggle = false` 才关闭。"已经激活"要同时满足：前台、未最小化、**就在当前虚拟桌面上**。
* `window` 动作顶层的 `wait_ms` 是 `launch.wait_ms` 的简写；没有 `launch` 可覆盖时要报错。
* `window` 的 `animate`（默认**关**）只对会改变窗口状态的 `op` 有意义。
* `window` 的 `monitor` 只对 `op = "activate"` 有意义：显式指定这次把窗口摆到哪块屏（不写就走「命中的
  `window_rule` 的 `monitor` → `settings.window_monitor`」）；写在其它 op 上会被 `--check` 拒绝。
* `windows([title])` 是窗口切换器。常见绑法是 `keys = "LWin"` + `trigger = "release"`（「轻碰 Win」）。
* `apps([title])` 是程序启动器：列表来自**三份来源** —— `shell:AppsFolder`（含商店 / UWP 与 `.msc` 系统
  工具；文档 / 帮助 / 网址与卸载程序被滤掉）、控制面板的「所有控制面板项」、以及内置的 26 个常用
  `ms-settings:` 设置页（名字带「设置：」/ `Settings: ` 前缀）；启动、图标与右键菜单都交给 shell；筛选是
  **名字 / 全拼 / 首字母的子串**（不是前缀，也不是模糊搜索）、筛到一个也**不**自动启动；**不切输入法**
  （与 `windows` 相反）。**筛选之后前 10 条各带一个 `0`–`9` 的快速启动键**（没有对应条目的号码被吃掉、
  不漏进筛选框），**最近使用程序另有 `Alt` + 数字、已固定程序另有 `Alt` + 功能键**的跳转键（只在卡片里
  生效、不随筛选变化）。

## 验收脚本（`scripts/acceptance.ps1`）

166 项检查（实跑 163，3 条数字键检查常被跳过），约三分钟，需要交互式桌面，**不属于 `ctest`**。它自己生成
一份**一次性配置**，起一个非提权的守护进程，从另一个上下文用 `SendInput` 注入按键，再用一个获得焦点的
WinForms 窗口从外面观察"按键到底有没有到达前台"（未绑定的键做正对照）。要让注入的按键触发绑定，守护进程
得用 `FLOWKEYD_ACCEPT_INJECTED=1` 启动（只给测试用的后门，启用时日志里有一条警告）。

```powershell
# 先按工作约定第 6 条提醒用户（会持续注入按键/抢焦点），不等回答
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1
powershell.exe ... -Phase config      # 只看配置，不注入按键
```

它**不做**：动画的屏幕采样、托盘菜单点击、自提权的 UAC 流程、"托盘图标真的消失了"的直接观察，以及
**一切电源动作**。这几项仍然只能靠人的手。

覆盖范围：

* 启动器有一段**弹窗缩放过期的回归守卫**：先把预热好的启动器窗口 `SetWindowPos` 成 800×622 **物理**
  像素，再按快捷键弹出 —— 断言卡片真的被重建了，而且宽度等于 `800 × GetDpiForWindow()/96`（现取窗口
  所在屏的 DPI 当独立参照）。
* 「轻碰 Win」：一次轻碰弹卡片、**再轻碰一次关掉**、卡片不在任务栏里、Win 松开后前台没有被外壳抢走。
* 远程桌面放行 9 条：名单里写的是捕捉窗口自己那个进程，因此不碰真 RDP 客户端。真机确认过：聚焦 Windows
  App 时出现 `remote desktop detected (msrdc.exe)`，切回普通窗口后 `left the remote desktop (…)`。
* 程序启动器：弹出 / 不在任务栏 / 拿到键盘焦点 / 标题里读得出条数 / **在卡片里打字真的筛掉条目** /
  再按一次关掉 / 重开时筛选已复位 / `Esc` 关掉 / 关掉后前台回到捕捉窗口；**右键菜单 5 条**（卡片矩形、
  右键一格后本进程多出一个 `#32768` 系统菜单窗口、位置在光标附近、尺寸是「每条约 22 **逻辑**像素」、
  `Esc` 关掉菜单、取消之后卡片还在）；**已固定/最近使用/全部程序 6 条**（`Space` 固定后标题里条数变化、
  **卡片高度不变**、关掉再打开固定还在、`End`+`Enter` 进「全部程序」列表、列表里 `Esc` 是返回概览、再按
  `Space` 取消固定）；「全部程序」列表的**滚轮**检查（滚 3 格 → 抓列表左侧一条竖带的像素指纹前后比对）；
  **数字快速启动键 3 条**（找一个只匹配 1..8 个程序的单字母筛选串，再按 `9` —— 编号从 `0` 起，`9` 必定
  没有对应条目；卡片还在、条数不变就说明它被吃掉了）；启动器那一段还加了 3 条拼音筛选（`dyxx` / `sz`）。
  **卡片里绝不能按 `Enter`** —— 那会真的启动列表里第一个程序（`End` + `Enter` 落在「全部程序」按钮上
  是安全的）。
* `-Phase config` 覆盖 `--check`（17 个快捷键）/ `--list`。脚本开头会先把 `$WorkDir\launcher.json` 删掉，
  所以每次跑都从"什么都没固定"开始。
* **脚本从头到尾不碰虚拟桌面**（要自己造第二张桌面、还要把用户的窗口搬来搬去，代价与风险都不划算）：
  跨桌面唤起那条路的自动化覆盖在 `tst_interactive`（`bringsAWindowOnAnotherDesktopToTheCurrentOne()` 盯
  "窗口被搬到当前桌面、而**视图没动**"，外加"已经在当前桌面时是空操作"；跑之前先建一次窗口、等 shell
  登记它 —— 同文件的 `moveWindowToAnotherDesktopAndBack()` 没等，**它是 flaky 的**；
  `activatesAWindowThatIsOnAnotherDesktop()` 盯另一个方向）。
* **锁屏时（`LogonUI` 在跑）脚本必然挂**：`GetForegroundWindow()` 返回 0，`SendInput` 报 5
  （ACCESS_DENIED）。还有另一种"判据全对、注入就是不落地"的状态（RDP 会话没真正接收输入）：先看
  `GetForegroundWindow()` 是不是 0，是的话**别在产品代码里找原因**，先去解锁 / 重连。
* **RDP 会话里注入的「相对鼠标移动」不会动光标**（`SetCursorPos` 成功、`GetCursorPos` 也对，可是一条
  `MOUSEEVENTF_MOVE` 的 `SendInput` 返回 1 而光标纹丝不动）→ 脚本里要用绝对定位
  （`SetCursorPos`，即 `[FlowInject]::Cursor()`）走路。
* 一条已修的 flake：右键弹出的原生 shell 菜单只能拿「类名是 `#32768` 的可见窗口存在」作判据，而解析
  shell 条目 + `QueryContextMenu` 要 100~200 ms，这段时间里别的 `#32768` 窗口会让"存在"提前成立 →
  现在判据是三条一起看：**可见窗口存在 + 矩形非空 + 守护进程日志里已经有 `shell menu: N item(s)`**。
* 往脚本里插诊断时：把脚本复制到 `tmp/`，用 `[System.IO.File]::ReadAllText/WriteAllText(...,
  UTF8Encoding($true))` 做替换（**必须保 BOM**），再加一条只含 ASCII 的探针把失败的检查名写进 diag。
  **把临时脚本的中文检查名打到控制台会被代码页弄成乱码，很容易误判成另一条检查挂了。**
* **工作空间那一段（M11 新增，约 15 秒，26 项）会真的藏窗口**：三个一次性实例（交互 / `new_window =`
  3 / `window_rule{ workspace = 2 }`），载体是 `charmap.exe`（普通 Win32、每个实例自己开窗；**别换成
  记事本** —— 本机那个是单实例带标签页的 Store 应用），配置里 `exclude` 排掉了捕捉窗口自己的
  `powershell` 与控制台（否则一切空间就把测试自己藏了）、并把 `remote_desktop` 关掉（不关的话真机上
  一个远程桌面客户端窗口抢到前台就会把所有绑定放行）。覆盖：`workspace("switch"|"move", N)` 的
  显隐与日志、`window("activate")` 唤起藏在别的工作空间的窗口、**切换器里的别进程窗口**（筛到它就
  “先切空间再激活”）、预写恢复日志 → 强杀 → `--restore-windows` 按日志恢复、`new_window` 与规则的
  新窗口归属、`quit` 前恢复可见。脚本里**不断言“隐藏前台窗口之后前台没变”**（Windows 自己会把前台
  交给别的窗口），那条保证在 `tst_workspace` 里。
* 最近一次实跑：`checks: 189, failures: 0`（release 产物，M11 收尾）。比 192 少的 3 条就是上面那组数字键
  检查 —— 真机目录约 200 条时，26 个单字母里可能没有一个命中 1..8 个程序，脚本打一条诊断（写进
  `$WorkDir\diag.txt`，UTF-8 无 BOM）**跳过**它们：是跳过，不是失败。

> **M8 / M9 / M10 当时都只跑了双构建 + `ctest` + 定向冒烟，没跑 acceptance.ps1**（项目所有者指示不做
> 完整冒烟；工作空间那几条验收项统一推到 M11）。M11 已把那些项全部补进脚本，并用一次完整实跑收尾
> （上面那行 `checks: 189, failures: 0`）。

## 手工冒烟清单（脚本覆盖不到的部分）

> 验收脚本里**故意不做**的两件事：显示器拔插 / 拖屏（要真断一块屏）与“空空间不抢焦点”的肉眼确认
> （隐藏前台窗口之后 Windows 会把前台交给别的窗口，外面分不出是谁干的）。它们在下面第 15–19 条里。

1. `--check --config tmp/smoke.lua` 通过，`--list` 形状与 README 一致。
2. 被吞掉的 `Win+…` 和弦：按完之后 Windows **不能**弹出开始菜单/搜索；按住 Windows 键超过自动重复延迟
   再松开，**仍然不能**弹。
3. 自动重复：按住和弦键 2 秒，动作只执行一次。
4. `suspend`：挂起之后所有绑定都不触发，**suspend 自己那条仍然可用**。
5. `quit`：托盘图标消失、钩子卸掉、进程干净退出。
6. `animate = true/false` 的肉眼区别；`volume` 的实际听感；托盘菜单点击。
7. `window_rule`：`DisplaySwitch.exe /internal` → `/extend` 制造一次"显示器重新接入"，确认日志里出现
   `monitor connected: ...; re-applying window rules` 且手工挪走的窗口被摆回。
8. **远程桌面放行**（脚本用的是假名单）：用 `mstsc.exe` / Windows App 连上一台机器，聚焦那个窗口，按一个
   被绑定的和弦（如 `Ctrl+Alt+t`）—— 对面那台机器应当收到它、本机不该有任何动作；日志里出现
   `remote desktop detected (mstsc.exe)`。把焦点切回本机窗口，日志出现 `left the remote desktop (…)`，
   和弦又回到本机行为；示例配置里 `remote_desktop = true` 的 `Ctrl+Alt+m`（静音）在远程桌面里仍然生效。
9. **程序启动器的右键菜单**（脚本只能断言"系统菜单窗口出现了"与「每条约 22 逻辑像素」）：弹出启动器，
   右键一格 —— 菜单应该长在光标处、字号与系统其它菜单一致（200% 缩放下每条约 44 物理 / 22 逻辑像素高；
   **不能**是两倍大的那种），不同的格子内容会变；挑一条无害的（「属性」或「打开文件位置」）真按一次，确认
   卡片在命令跑之前就收了、shell 开出来的窗口在前台；再右键一次按 `Esc`，卡片应当还留着。
10. 按顺序做完以上之后，**检查没有任何按键卡在按下状态**。
11. **程序启动器的 `Alt` + 数字 / 功能键**（脚本只能按没分配到的键）：固定两三条、再启动两三条之后打开
    卡片 —— 角上应当是 `Alt+F1` / `Alt+F2` 与 `Alt+1` / `Alt+2` 这样的徽标（宽度跟着文字走），固定项
    右边那个「已固定」小圆点让到图标左上角；按 `Alt+F1` 启动第 1 个固定项、`Alt+1` 启动第 1 个最近项；
    **筛选一个筛不到那条固定项的串之后再按 `Alt+F1`，照样应该启动它**；`Ctrl+数字`（如全选）与裸字母 /
    数字照旧进筛选框；卡片关着时这些组合在本机**不再有任何效果**。注意 `Alt+F4` / `Alt+F5`…
    `Alt+F10` 在拥有系统菜单的窗口上是 Windows 自己的命令（关窗 / 还原 / 移动 / 大小 / 最小化 / 最大化），
    可能先被 win32k 处理。
12. **启动器里的控制面板项与设置页真的能启动**（脚本只验到"筛选能筛到它们"）：开一个控制面板项（筛
    `dyxx` →「电源选项」→ `Enter`）与一个设置页（筛 `sz` →「设置：显示」→ `Enter`）；再右键一个控制面板
    格，菜单里应当有「打开」这类正常条目。
13. **跨桌面唤起**的真机手感：切到第 2 张虚拟桌面，按下一个**没写 `window_rule` `desktop`** 的程序（例如
    记事本）的 `window("activate")` 快捷键 —— 窗口应该被**拉到第 2 张桌面**、视图**不切**，日志里有
    `(brought to desktop 2/2)`；再给同一个程序写一条 `window_rule{ desktop = 1 }` 重载配置、再按一次 ——
    这次应该是**切到第 1 张桌面**、窗口留在那儿；最后确认窗口不是最大化/最小化状态、也没有"卡在按下
    状态"的修饰键。
14. **窗口摆到光标那块屏**的真机手感：把鼠标放到**左边**那块屏，按下某个**没写 `monitor`** 的程序的
    activate 快捷键（它的窗口本来在右边那块屏上）—— 窗口应该被**搬到鼠标那一块**（保持大小并居中，不
    跟着最大化），日志里多一段 `placed 1280x800 at … on monitor 1/2`；再保持鼠标不动按一次（先 `toggle`
    收起、再按一次恢复）—— 这次日志里是 `already on monitor 1/2`，而**窗口的位置没有被重新居中**。然后
    把鼠标放到右边那块屏、再按一次 —— 窗口跟过去。再把配置改成 `settings{ window_monitor = "last" }`
    （或给那一条写 `monitor = "last"`）重载：第一次按**不会动窗口**（日志 `no monitor remembered yet;
    noted monitor 1/2`，旁边多出一个 `windows.json`），把它拖到另一块屏再按一次 —— 从此它每次都回到那块
    屏；把那块屏的显示器断开（或把 `windows.json` 里的设备名改错）后按一次：只记一条 warning（`the
    remembered monitor … is not connected`）、窗口留在原处。靠 `settings{ window_monitor = "none" }` 把
    整项关掉后重载，再按一次应当**完全不动窗口**。
15. **工作空间的显示器拔插与拖屏**（两块 1920x1080@200% 就能做；脚本完全不覆盖）：把某块屏上的几个窗口
    拖好（含一个最大化、一个最小化），用 `DisplaySwitch.exe /internal` 或断线把它们那块屏禁掉 —— 那些
    窗口应当出现在剩下的那块屏上（最大化仍是最大化、最小化仍然最小化，日志里是
    `workspace: moved N window(s) back onto their monitor`）；把屏接回来 —— 它们应当**回到原来那块屏**
    （非活动空间里的仍然藏着）。然后把一个窗口用鼠标（或 `Win+Shift+←/→`，**后者没有
    `EVENT_SYSTEM_MOVESIZEEND`**，只能靠 `LOCATIONCHANGE` + 防抖）拖到另一块屏 —— 日志里应当有
    `workspace: ... moved from \\.\DISPLAYx to \\.\DISPLAYy`，之后在那块屏上切空间，它跟着它新归属的
    那块屏显隐（不再跟着旧屏）。最后改一下分辨率 / 缩放：归属不应发生任何变化（日志里既没有
    `monitors changed` 下的 `moved`，也没有窗口被弹出 / 藏起来）。
16. **工作空间的安全与恢复**（M8；脚本不覆盖）：
    1. 把几个窗口 `workspace("move", 2)` 藏到非活动空间，然后 `taskkill /PID <pid> /F` 强杀守护进程
       —— `<配置目录>\workspace.json` 应该已经存在（里面是那几条的 `hwnd` / `pid` / `class`）；重新启动
       flowkeyd（或先跑 `flowkeyd --restore-windows`），窗口全部回来，文件被删掉；日志里是
       `workspace: restored N window(s) from the recovery log`。
    2. 同上先把窗口藏起来，然后从托盘菜单点 *恢复全部窗口* —— 立刻全部回来；再试一次 `--restore-windows`
       （守护进程在跑时它会请那个实例去做，没有实例时自己按日志恢复）。
    3. 同上先把窗口藏起来，然后**注销 / 关机** —— 注销前那一下窗口已经回来了（日志里
       `session is ending; restoring the windows hidden by workspaces`）；做完这一条**必须确认没有任何窗口
       卡在“已被我们藏起来”的状态**（`workspace.json` 应当已经不存在）。
    4. 手动把 `workspace.json` 改坏（或写一条不存在的 `hwnd`）后重启：只记 warning + 丢掉那几条，
       其它窗口照常恢复，进程不崩。
17. **启动权限**（常驻实例是提权的，所以拿它验反而最直接）：
    1. 按一个 `action = run("notepad.exe")` 的快捷键，然后在**任务管理器**里看那个 `notepad.exe`：
       「详细信息」页的 `notepad.exe` 行右键只能看到「结束任务」而**没有** UAC 盾牌图标；命令行最快：
       `& build\dist-release\flowkeyd.exe --list` 里那条摘要应当**没有** `(elevated)`。
    2. 同一个键写 `run("notepad.exe", nil, { elevated = true })`（或给 `launch` 加 `elevated = true`）
       重载 —— 从**非提权**的实例按下去应当弹一次 UAC（agent 验证不了，必须人按）；从提权实例按下去
       **不弹**。
    3. `open([[...\x.docx]])` 与从启动器点一个程序：开出来的程序同样应当是普通权限；但启动器**右键
       菜单**里的「打开」开出来的是提权的（已知限制）。
18. **工作空间与其它功能的联动**（M9；脚本不覆盖跨进程那一半）：
    1. 给记事本类程序写一条 `window("activate")` 快捷键，把它的窗口 `workspace("move", 2)` 藏到
       非活动空间；再按那个快捷键 —— 那块屏应**切到空间 2**、窗口回来并拿到前台，日志里多一段
       `(workspace 2 on \\.\DISPLAYx)`；再按一次（此时它已在活动空间里）应当是正常的 `toggle` 收起。
    2. 同上把窗口藏到空间 2，然后开 `windows()` 切换器 —— 那个窗口**应该在列表里**（进程名 / 标题
       照旧）；选中它 —— 同样先切到空间 2 再激活。
    3. 给那个程序写 `window_rule{ process = "...", workspace = 2 }` 重载配置，然后在空间 1 里启动它
       —— 新窗口应当**直接出现在空间 2、立即被藏起来**，焦点不被抢；再手动 `workspace("move", 1)`
       把它搬回空间 1，切换几次空间也不再被规则拉回空间 2（规则只在三个时机生效）。
    4. 把一个**看得见的**窗口用 `window("move_right_monitor")` 挪到另一块屏 —— 日志里应当有
       `workspace ... on \\.\DISPLAYy`，之后在那块屏上切空间时它跟着新屏显隐（不再跟着旧屏）。
    5. 把一个窗口藏到非活动空间，再对同一个程序按 `move_right_monitor` / `minimize` 的快捷键 ——
       应该**什么都不发生**，只多一条 warning（`is hidden in another workspace`）；窗口没有被弹出来、
       也没有卡在一种“看得见却被记录当藏着”的状态。
19. **工作空间的切换与移动**（M6/M10 的基本手感，脚本只验单屏、普通窗口）：
    1. 双屏：A 屏切到空间 2，B 屏的窗口一个不动；把鼠标移到 B 屏再切，只动 B 屏。`switch_target =
       "focus"` 时，鼠标在 A、焦点在 B 就作用于 B。
    2. 最大化 / 最小化 / 普通窗口各一个，来回切几次之后位置、大小、最大化、最小化都不变（最小化的
       切回来仍是最小化）。
    3. 空空间不抢焦点（前景窗口不变）；非空空间回到最近用过的那个窗口；Z 序还原。
    4. `move` 到非活动空间：窗口立即消失、用户留在原空间；日志里那条以 `hidden)` 结尾。
    5. 远程桌面前台（`mstsc.exe` / Windows App）时按工作空间快捷键：**放行、什么都不切**。
