param(
    [string]$Exe = 'build\windows-release\flowkeyd.exe',
    [string]$WorkDir = "$env:TEMP\flowkeyd-accept",
    [ValidateSet('config', 'all')]
    [string]$Phase = 'all'
)

# =============================================================================
# flowkeyd 的验收脚本（AGENTS.md 第 5 节「手工冒烟清单」的自动化版本）。
#
# 它用一个**一次性配置**起一个非提权的守护进程，再从本进程用 SendInput 注入
# 按键，用一个获得焦点的 WinForms 窗口从外部观察「按键到底有没有到达前台」：
#
#   * `--check` / `--list` 的形状
#   * 单实例：同配置的第二个实例必须被拒
#   * 吞键：绑定的和弦永远不到达焦点窗口（正对照：未绑定的键必须到达，
#     否则就是焦点压根没拿到，本次验证无效）
#   * 被吞掉的 Win 和弦不会让外壳打开搜索（常规 / 0 ms 轻按 / 一次、两次
#     Windows 键自动重复四种情况）
#   * 按住不放只派发一次
#   * 重映射的 hold / tap / CapsLock -> Esc
#   * `send` 会先松开用户按住的修饰键（前台看到的是 Ctrl+C 而不是 Ctrl+Alt+C）
#   * 小键盘 `-`/`+`/`Enter` 与主键盘 `-`/`=`/`Enter` 互不触发
#   * `window` 的启动 → 激活 → 收起（默认 toggle）→ 恢复
#   * `menu` 弹窗：出现、拿到键盘焦点、条目键真的执行动作、Esc 只关窗、
#     再按一次不开第二个（重新打开的窗口也必须重新拿到焦点）
#   * `help` 弹窗：出现、拿到焦点、输入筛选后标题里的 `可见/总数` 变小、
#     Enter 把选中那行的按键写进剪贴板、第一下 Esc 只清筛选、第二下才关窗
#   * suspend / resume（挂起时别的绑定不触发，而 suspend 自己仍然可用）
#   * reload（改过的配置文本立刻生效）
#   * quit（钩子卸掉、之后按键重新到达前台、没有按键卡在按下状态）
#
# 需要交互式桌面会话，而且会真的注入按键、抢焦点（约两分钟）。按 AGENTS.md
# 工作约定第 6 条先提醒用户再跑。
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance.ps1
#   （默认就是 release 产物；AGENTS.md 工作约定第 2 条：只跑 release）
#
# 绝不碰系统电源动作（AGENTS.md 工作约定第 10 条）。
#
# 退出码 0 表示全部检查通过。
#
# ### 注入的按键为什么能触发绑定
#
# 钩子默认丢弃一切带 `LLKHF_INJECTED` 的事件（不变量 2），所以脚本没法伪造
# 物理按键。守护进程因此用 `FLOWKEYD_ACCEPT_INJECTED=1` 启动：那是个**只给
# 测试用的后门**（与 oskeyd 的 `OSKEYD_ACCEPT_INJECTED` 同款），它会抬升那道
# 过滤并在日志里打一条警告。flowkeyd 自己的注入带着 `"FLOW"` 标记，仍然在钩子
# 回调的第一步就被丢掉，所以重映射不会自己喂自己。
#
# ### 编码
#
# 本文件必须以**带 BOM 的 UTF-8** 保存：PowerShell 5.1 会把没有 BOM 的 `.ps1`
# 按 ANSI 代码页解码，里面的中文会变乱码（注释无所谓，但断言里的中文就废了）。
# 三个窗口标题因此是用 `[char]` 码点拼出来的，而不是中文字面量 —— 这样即使
# BOM 丢了，脚本的逻辑也仍然是对的。
# =============================================================================

$ErrorActionPreference = 'Stop'

# exe 需要 Qt 与 MinGW 的 DLL（ctest 那边由 CMake 配好了，手工跑要自己加）。
$env:PATH = 'C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH

$here = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not [System.IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $here $Exe
}
if (-not (Test-Path $Exe)) {
    Write-Error "找不到 flowkeyd 可执行文件：$Exe（先构建：cmake --build --preset release）"
    exit 2
}

$script:failures = @()
$script:checks = 0

function Check($name, $condition) {
    $script:checks++
    if ($condition) {
        Write-Host "  [ ok ] $name"
    } else {
        Write-Host "  [FAIL] $name"
        $script:failures += $name
    }
}

# --- 期望的窗口标题：用码点拼，理由见文件头 ---
$MENU_TITLE = 'flowkeyd ' + [char]0x9009 + [char]0x5355                                  # flowkeyd 选单
$HELP_TITLE = 'flowkeyd ' + [char]0x5FEB + [char]0x6377 + [char]0x952E                   # flowkeyd 快捷键

# --- 用到的虚拟键码 ---
$VK_SHIFT = 0x10
$VK_CTRL = 0x11
$VK_ALT = 0x12
$VK_LWIN = 0x5B
$VK_S = 0x53
$VK_ESC = 0x1B
$VK_RETURN = 0x0D
$VK_F6 = 0x75
$VK_F8 = 0x77
$VK_F9 = 0x78
$VK_F10 = 0x79
$VK_F11 = 0x7A
$VK_F12 = 0x7B
$VK_F15 = 0x7E
$VK_F17 = 0x80
$VK_F18 = 0x81
$VK_F19 = 0x82
$VK_F20 = 0x83
$VK_F22 = 0x85
$VK_F24 = 0x87
$VK_CAPSLOCK = 0x14
$VK_OEM_MINUS = 0xBD
$VK_OEM_PLUS = 0xBB
$VK_NUMPAD_SUB = 0x6D
$VK_NUMPAD_ADD = 0x6B

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$config = Join-Path $WorkDir 'accept.lua'
$daemonLog = Join-Path $WorkDir 'daemon.log'
$daemonOut = Join-Path $WorkDir 'daemon.out'
$onceLog = Join-Path $WorkDir 'once.log'
$targetTitle = 'flowkeyd-accept-target'
$targetScript = Join-Path $WorkDir 'target-window.ps1'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class FlowInject {
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    // INPUT 在 x64 上是 40 字节：4 字节 type、4 字节填充、32 字节联合体。
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public uint type;
        public KEYBDINPUT ki;
        public long padding;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    private const uint INPUT_KEYBOARD = 1;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const uint KEYEVENTF_EXTENDEDKEY = 0x0001;

    // 扩展键标志由调用者指定：小键盘的 Enter 与主键盘的 Enter 共用 VK_RETURN，
    // 唯一区别就是它（真实键盘也是这么上报的）。
    public static void KeyEx(ushort vk, bool down, bool extended) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vk;
        inputs[0].ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        if (extended) { inputs[0].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY; }
        if (SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT))) != 1) {
            throw new Exception("SendInput failed: " + Marshal.GetLastWin32Error());
        }
    }

    public static void Key(ushort vk, bool down) { KeyEx(vk, down, false); }

    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
    public static bool IsDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    [DllImport("user32.dll")] private static extern short GetKeyState(int vKey);
    public static bool CapsLockOn() { return (GetKeyState(0x14) & 1) != 0; }

    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextLengthW(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);

    public static string ForegroundTitle() {
        IntPtr h = GetForegroundWindow();
        if (h == IntPtr.Zero) { return ""; }
        int n = GetWindowTextLengthW(h);
        if (n <= 0) { return ""; }
        StringBuilder sb = new StringBuilder(n + 2);
        GetWindowTextW(h, sb, sb.Capacity);
        return sb.ToString();
    }

    public static string ForegroundClass() {
        StringBuilder sb = new StringBuilder(256);
        GetClassNameW(GetForegroundWindow(), sb, 256);
        return sb.ToString();
    }

    public static bool IsWindowMinimized(IntPtr h) { return IsIconic(h); }

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);

    // 某个进程拥有的、可见的顶层窗口标题。
    public static string[] TitlesOfPid(int pid) {
        var list = new System.Collections.Generic.List<string>();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint wpid;
            GetWindowThreadProcessId(h, out wpid);
            if (wpid != (uint)pid) { return true; }
            if (!IsWindowVisible(h)) { return true; }
            int n = GetWindowTextLengthW(h);
            if (n <= 0) { return true; }
            StringBuilder sb = new StringBuilder(n + 2);
            GetWindowTextW(h, sb, sb.Capacity);
            list.Add(sb.ToString());
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }

    public static bool HasWindowTitled(int pid, string prefix) {
        foreach (string t in TitlesOfPid(pid)) {
            if (t.StartsWith(prefix)) { return true; }
        }
        return false;
    }

    [DllImport("user32.dll")] private static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool BringWindowToTop(IntPtr h);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();

    // 前台锁会拒绝“不在前台的那个进程”调用 SetForegroundWindow（这正是守护
    // 进程自己要有 raiseWindow 的原因）。这里用同样的办法：先 AttachThreadInput
    // 到当前前台线程再 SetForegroundWindow。没有它的话，用户在我们跑测试时点了
    // 别的窗口就会让捕捉窗口悄悄失去焦点，后面每一条按键检查都会看起来像产品 bug。
    public static bool ForceForeground(IntPtr hwnd) {
        IntPtr fg = GetForegroundWindow();
        uint fgPid;
        uint fgThread = fg == IntPtr.Zero ? 0 : GetWindowThreadProcessId(fg, out fgPid);
        uint myThread = GetCurrentThreadId();
        bool attached = false;
        if (fgThread != 0 && fgThread != myThread) {
            attached = AttachThreadInput(myThread, fgThread, true);
        }
        bool ok = SetForegroundWindow(hwnd);
        if (ok) { BringWindowToTop(hwnd); }
        if (attached) { AttachThreadInput(myThread, fgThread, false); }
        return ok;
    }
}
"@

# =============================================================================
# 一次性配置：所有用到的和弦都是真实应用不会碰的键（F13..F24 / Ctrl+Alt+Fx），
# 唯一例外是 `Win+S` —— 它就是要测“被吞掉的 Win 和弦外壳看不见”这件事，
# 万一遮断失效，代价只是弹一个搜索框（oskeyd 的 e2e 用的也是这个和弦）。
# 选单里**只有无害的剪贴板动作**：睡眠/关机/重启这类条目在验收里永远不会被按下。
# =============================================================================
$targetScriptBody = @'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$form = New-Object System.Windows.Forms.Form
$form.Text = "flowkeyd-accept-target"
$form.ClientSize = New-Object System.Drawing.Size(320, 200)
[void]$form.ShowDialog()
'@

function Write-Config([string]$marker) {
    $text = @"
settings {
  log_level = "debug",
  tick_ms = 15,
}

-- 1. 只吞不做：这个键永远不该到达焦点窗口。
hotkey { name = "accept-swallow", comment = "swallow", keys = "F18", action = none() }

-- 2. 按住不放只派发一次（行数就是外部证据）。
hotkey { name = "accept-once", comment = "once", keys = "F15",
  action = run("cmd.exe", { "/c", [[echo once>>$onceLog]] }, { wait = true }) }

-- 3. 被吞掉的 Win 和弦：动作必须在*按下*时就派发（那时 Win 还按着）。
hotkey { name = "accept-win", comment = "win chord", keys = "Win+S",
  action = clipboard("set", { text = "WIN-S-OK" }) }

-- 4. window 动作 + 默认的 toggle。
hotkey { name = "accept-window", comment = "window toggle", keys = "Ctrl+Alt+F17",
  action = window("activate", {
    process = "powershell",
    target = "$targetTitle",
    launch = {
      program = "powershell.exe",
      args = { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", [[$targetScript]] },
      wait_ms = 20000,
    },
  }) }

-- 5. send：必须先把用户物理按住的修饰键松开。
hotkey { name = "accept-send", comment = "send", keys = "Ctrl+Alt+F12", action = send("^{c}") }

-- 6. 给 reload 用的标记。
hotkey { name = "accept-marker", comment = "marker", keys = "Ctrl+Alt+F24",
  action = clipboard("set", { text = "$marker" }) }

-- 7. 选单：只会按到那个无害的剪贴板条目。
hotkey { name = "accept-menu", comment = "menu", keys = "Ctrl+Alt+F9", action = menu{
  title = "accept",
  items = {
    { key = "t", label = "clipboard", hint = "MENU-OK", action = clipboard("set", { text = "MENU-OK" }) },
    { key = "c", label = "cancel", hint = "Esc", action = none() },
  },
} }

-- 8. 帮助窗口。
hotkey { name = "accept-help", comment = "help", keys = "Ctrl+Alt+F8", action = help() }

-- 9. 挂起开关（挂起时它自己必须仍然可用）。
hotkey { name = "accept-suspend", comment = "suspend", keys = "Ctrl+Alt+F11", action = suspend("toggle") }

-- 10. 重载。
hotkey { name = "accept-reload", comment = "reload", keys = "Ctrl+Alt+F6", action = reload() }

-- 11. 退出。
hotkey { name = "accept-quit", comment = "quit", keys = "Ctrl+Alt+F10", action = quit() }

-- 12. 小键盘与主键盘必须分得开。
hotkey { name = "accept-numpad-sub", comment = "numpad minus", keys = "NumpadSub",
  action = clipboard("set", { text = "NUMPAD-SUB" }) }
hotkey { name = "accept-numpad-add", comment = "numpad plus", keys = "NumpadAdd",
  action = clipboard("set", { text = "NUMPAD-ADD" }) }
hotkey { name = "accept-numpad-enter", comment = "numpad enter", keys = "NumpadEnter",
  action = clipboard("set", { text = "NUMPAD-ENTER" }) }

-- 13. 重映射：hold、tap，以及经典的 CapsLock -> Esc。
remap { name = "accept-remap-hold", from = "F20", to = "F21" }
remap { name = "accept-remap-tap", from = "F22", to = "F23", mode = "tap" }
remap { name = "accept-caps", from = "CapsLock", to = "Esc" }
"@
    # `-Encoding UTF8` 会写出 BOM：顺带把“带 BOM 的配置也能读”一起覆盖了。
    Set-Content -Path $config -Value $text -Encoding UTF8
}

Set-Content -Path $targetScript -Value $targetScriptBody -Encoding UTF8
Write-Config 'MARKER-1'

if ($Phase -eq 'config') {
    # 只看配置本身（不起守护进程、不注入按键），改配置时很方便。
    Write-Host "wrote $config"
    foreach ($cmd in @('--check', '--list')) {
        $o = Join-Path $WorkDir "cfg$cmd.out"
        $e = Join-Path $WorkDir "cfg$cmd.err"
        $proc = Start-Process -FilePath $Exe -ArgumentList @($cmd, '--config', $config) `
            -RedirectStandardOutput $o -RedirectStandardError $e -Wait -PassThru -NoNewWindow
        Write-Host "--- $cmd (exit $($proc.ExitCode)) ---"
        Write-Host (Get-Content $o -Raw)
        Write-Host (Get-Content $e -Raw)
    }
    exit 0
}

Write-Host 'NOTE: the next couple of minutes inject keystrokes and steal focus.'
Start-Sleep -Seconds 3

# =============================================================================
# 离线命令（绝不允许提权）
# =============================================================================
Write-Host '--- CLI ---'
$checkOut = Join-Path $WorkDir 'check.out'
$checkErr = Join-Path $WorkDir 'check.err'
$p = Start-Process -FilePath $Exe -ArgumentList @('--check', '--config', $config) `
    -RedirectStandardOutput $checkOut -RedirectStandardError $checkErr -Wait -PassThru -NoNewWindow
$checkText = (Get-Content $checkOut -Raw -ErrorAction SilentlyContinue) + (Get-Content $checkErr -Raw -ErrorAction SilentlyContinue)
Check '--check 接受验收配置' ($p.ExitCode -eq 0 -and $checkText -match 'OK \(\d+ hotkey')
Check '--check 报告 14 个快捷键' ($checkText -match 'OK \(14 hotkey')

$listOut = Join-Path $WorkDir 'list.out'
$listErr = Join-Path $WorkDir 'list.err'
$p = Start-Process -FilePath $Exe -ArgumentList @('--list', '--config', $config) `
    -RedirectStandardOutput $listOut -RedirectStandardError $listErr -Wait -PassThru -NoNewWindow
$listText = (Get-Content $listOut -Raw -ErrorAction SilentlyContinue)
Check '--list 退出码为 0' ($p.ExitCode -eq 0)
Check '--list 打出了选单与条目数' ($listText -match 'menu "' -and $listText -match '\(2 item\(s\)\)')
Check '--list 打出了 help' ($listText -match 'help')
Check '--list 打出了两个重映射' ($listText -match 'F20' -and $listText -match 'CapsLock')

# =============================================================================
# 守护进程 + 焦点捕捉窗口
# =============================================================================
Remove-Item $daemonLog, $daemonOut, $onceLog -ErrorAction SilentlyContinue
$env:FLOWKEYD_ACCEPT_INJECTED = '1'
$daemon = Start-Process -FilePath $Exe `
    -ArgumentList @('--config', $config, '--no-elevate', '--console', '--log-file', $daemonLog) `
    -RedirectStandardOutput $daemonOut -RedirectStandardError "$daemonOut.err" `
    -PassThru -NoNewWindow

$form = New-Object System.Windows.Forms.Form
$form.Text = 'flowkeyd-accept-catcher'
$form.Width = 360
$form.Height = 140
$form.TopMost = $true
$form.KeyPreview = $true
$seen = New-Object 'System.Collections.Generic.List[string]'
$form.Add_KeyDown({ param($sender, $event) $seen.Add("D:$($event.KeyCode)|c=$($event.Control)|a=$($event.Alt)|s=$($event.Shift)") })
$form.Add_KeyUp({ param($sender, $event) $seen.Add("U:$($event.KeyCode)") })
$form.Show() | Out-Null
$form.Activate() | Out-Null
[void]$form.Focus()

# 边等边泵 WinForms 的消息循环，否则捕捉窗口会假装死掉。
function Pump([int]$milliseconds) {
    $deadline = (Get-Date).AddMilliseconds($milliseconds)
    while ((Get-Date) -lt $deadline) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 10
    }
}
function WaitUntil([scriptblock]$condition, [int]$timeoutMs) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        Pump 100
        if (& $condition) { return $true }
    }
    return $false
}
function TapKey([int]$vk) { [FlowInject]::Key($vk, $true); Start-Sleep -Milliseconds 40; [FlowInject]::Key($vk, $false) }
function TapKeyEx([int]$vk, [bool]$extended) {
    [FlowInject]::KeyEx($vk, $true, $extended); Start-Sleep -Milliseconds 40; [FlowInject]::KeyEx($vk, $false, $extended)
}
function Chord([int[]]$mods, [int]$vk) {
    foreach ($m in $mods) { [FlowInject]::Key($m, $true) }
    Start-Sleep -Milliseconds 30
    [FlowInject]::Key($vk, $true)
    Start-Sleep -Milliseconds 30
    [FlowInject]::Key($vk, $false)
    Start-Sleep -Milliseconds 30
    for ($i = $mods.Length - 1; $i -ge 0; $i--) { [FlowInject]::Key($mods[$i], $false) }
}
function CtrlAlt([int]$vk) { Chord @($VK_CTRL, $VK_ALT) $vk }
function Clear-Seen { $seen.Clear() }
function SeenHas([string]$fragment) { return ($seen | Where-Object { $_ -like "*$fragment*" } | Measure-Object).Count -gt 0 }
function SeenDump { return ($seen -join ' ') }
function FocusCatcher {
    for ($i = 0; $i -lt 4; $i++) {
        if ([FlowInject]::ForegroundTitle() -eq 'flowkeyd-accept-catcher') { break }
        $form.Activate() | Out-Null
        [FlowInject]::ForceForeground($form.Handle) | Out-Null
        Pump 220
    }
}
# 每个依赖焦点的检查组都先调这个：如果捕捉窗口拿不回焦点，这次验证就是无效的，
# 必须让它明确地表现出来，而不是伪装成产品 bug。
function NeedFocus([string]$where) {
    FocusCatcher
    if ([FlowInject]::ForegroundTitle() -eq 'flowkeyd-accept-catcher') { return }
    Diag "focus precondition failed ($where): $(FgInfo)"
    Check "捕捉窗口拿到了键盘焦点（$where）" $false
}
# 关掉可能被外壳打开的界面（搜索/开始），再把焦点还给捕捉窗口。
function Dismiss-ShellUi {
    for ($i = 0; $i -lt 3; $i++) {
        if ([FlowInject]::ForegroundTitle() -eq 'flowkeyd-accept-catcher') { break }
        TapKey $VK_ESC
        Pump 350
    }
    FocusCatcher
}
function ClipGet { return (Get-Clipboard -Raw -ErrorAction SilentlyContinue) }
function ClipSet([string]$value) { Set-Clipboard -Value $value; Start-Sleep -Milliseconds 80 }
function DaemonText {
    $a = ''
    if (Test-Path $daemonLog) { $a = (Get-Content $daemonLog -Raw -ErrorAction SilentlyContinue) }
    $b = ''
    if (Test-Path $daemonOut) { $b = (Get-Content $daemonOut -Raw -ErrorAction SilentlyContinue) }
    return "$a$b"
}
# 诊断信息写进 UTF-8 文件：控制台的代码页会把中文窗口标题糟蹋掉，
# 而这个文件是给人（或 agent）事后看的。
function Diag([string]$text) {
    [System.IO.File]::AppendAllText((Join-Path $WorkDir 'diag.txt'), $text + "`r`n",
        (New-Object System.Text.UTF8Encoding($false)))
}
function FgInfo {
    $t = [FlowInject]::ForegroundTitle()
    return "class=[$([FlowInject]::ForegroundClass())] cp=[$(($t.ToCharArray() | ForEach-Object { [int]$_ }) -join ',')]"
}

try {
    Remove-Item (Join-Path $WorkDir 'diag.txt') -ErrorAction SilentlyContinue
    Pump 1500
    if ($daemon.HasExited) { throw "守护进程立刻退出了：`n$(Get-Content $daemonOut -Raw)" }
    Write-Host "daemon started (pid $($daemon.Id))"
    Check '日志里有测试模式的警告' ((DaemonText) -match 'FLOWKEYD_ACCEPT_INJECTED is set')

    # --- 单实例 --------------------------------------------------------------
    Write-Host '--- 单实例 ---'
    $secondOut = Join-Path $WorkDir 'second.out'
    $second = Start-Process -FilePath $Exe `
        -ArgumentList @('--config', $config, '--no-elevate', '--console') `
        -RedirectStandardOutput $secondOut -RedirectStandardError "$secondOut.err" `
        -Wait -PassThru -NoNewWindow
    $secondText = (Get-Content $secondOut -Raw -ErrorAction SilentlyContinue) + (Get-Content "$secondOut.err" -Raw -ErrorAction SilentlyContinue)
    Check '同配置的第二个实例被拒绝' ($second.ExitCode -eq 1)
    Check '拒绝信息里带着配置路径' ($secondText -match 'already owns')

    # --- 焦点正对照 ----------------------------------------------------------
    Write-Host '--- 焦点正对照 ---'
    NeedFocus '正对照'
    Clear-Seen
    TapKey $VK_F19
    Pump 300
    if (-not (SeenHas 'D:F19')) { Diag "positive control failed: seen=[$(SeenDump)] $(FgInfo)" }
    Check '正对照：未绑定的键到达了焦点窗口' (SeenHas 'D:F19')

    # --- 吞键 ----------------------------------------------------------------
    Write-Host '--- 吞键 ---'
    NeedFocus '吞键'
    Clear-Seen
    TapKey $VK_F18
    Pump 400
    Check '绑定的和弦永远不到达焦点窗口' (-not (SeenHas 'D:F18'))

    # --- 被吞掉的 Win 和弦 ---------------------------------------------------
    Write-Host '--- 被吞掉的 Win 和弦 ---'
    # 外壳真的看到了 `Win+S`（或看到了一个“单独的 Win”）就会弹出搜索面板，
    # 所以失败很响但无害。这里**绝对不要**换成 `Win+F16`：本机那个组合在某些
    # 外壳状态下会拉出 SlideToShutDownHost（滑动以关机）。
    function Test-WinChord([string]$label, [int]$gap, [int]$repeats) {
        Dismiss-ShellUi
        ClipSet 'CLIP-BEFORE-WIN'
        if ([FlowInject]::ForegroundTitle() -ne 'flowkeyd-accept-catcher') {
            Diag "winchord '$label': the catcher lost focus before the chord: $(FgInfo)"
        }
        [FlowInject]::Key($VK_LWIN, $true)
        Start-Sleep -Milliseconds $gap
        [FlowInject]::Key($VK_S, $true)
        Start-Sleep -Milliseconds $gap
        [FlowInject]::Key($VK_S, $false)
        Start-Sleep -Milliseconds $gap
        for ($i = 0; $i -lt $repeats; $i++) {
            # 一次自动重复 = 一个已经按下的键又收到一次 key-down。
            [FlowInject]::Key($VK_LWIN, $true)
            Start-Sleep -Milliseconds 30
        }
        Start-Sleep -Milliseconds $gap
        [FlowInject]::Key($VK_LWIN, $false)
        Pump 900
        $fg = [FlowInject]::ForegroundTitle()
        $ok = $fg -eq 'flowkeyd-accept-catcher'
        if (-not $ok) { Diag "winchord '$label': the shell reacted: $(FgInfo)" }
        Check "外壳看不见被吞掉的 Win 和弦（$label）" $ok
        Check "Win 和弦在按下时就派发了动作（$label）" ((ClipGet) -eq 'WIN-S-OK')
    }
    Test-WinChord '常规' 60 0
    Test-WinChord '0 ms 轻按' 0 0
    Test-WinChord '一次 Win 自动重复' 40 1
    Test-WinChord '两次 Win 自动重复' 40 2

    # --- 自动重复 ------------------------------------------------------------
    Write-Host '--- 自动重复 ---'
    Remove-Item $onceLog -ErrorAction SilentlyContinue
    [FlowInject]::Key($VK_F15, $true)
    for ($i = 0; $i -lt 12; $i++) { Start-Sleep -Milliseconds 120; [FlowInject]::Key($VK_F15, $true) }
    [FlowInject]::Key($VK_F15, $false)
    Pump 1200
    $onceLines = @()
    if (Test-Path $onceLog) { $onceLines = @(Get-Content $onceLog) }
    Check '按住约 1.5 秒只派发一次' ($onceLines.Count -eq 1)

    # --- 重映射 --------------------------------------------------------------
    Write-Host '--- 重映射 ---'
    NeedFocus '重映射'
    Clear-Seen
    [FlowInject]::Key($VK_F20, $true)
    Pump 250
    [FlowInject]::Key($VK_F20, $false)
    Pump 250
    if (-not (SeenHas 'D:F21')) { Diag "remap hold: seen=[$(SeenDump)] $(FgInfo)" }
    Check '重映射 hold：目标键被按下又被松开' ((SeenHas 'D:F21') -and (SeenHas 'U:F21'))
    Check '重映射 hold：源键从不到达' (-not (SeenHas 'F20'))
    FocusCatcher
    Clear-Seen
    TapKey $VK_F22
    Pump 400
    $tapDowns = @($seen | Where-Object { $_ -like 'D:F23|*' }).Count
    $tapUps = @($seen | Where-Object { $_ -eq 'U:F23' }).Count
    if ($tapDowns -ne 1 -or $tapUps -ne 1) { Diag "remap tap: downs=$tapDowns ups=$tapUps seen=[$(SeenDump)] $(FgInfo)" }
    Check '重映射 tap：一次按下只产生一次目标敲击' ($tapDowns -eq 1 -and $tapUps -eq 1)
    $capsBefore = [FlowInject]::CapsLockOn()
    FocusCatcher
    Clear-Seen
    TapKey $VK_CAPSLOCK
    Pump 400
    if (-not (SeenHas 'D:Escape')) { Diag "remap caps: seen=[$(SeenDump)] $(FgInfo)" }
    Check '重映射 CapsLock -> Esc：目标键到达' (SeenHas 'D:Escape')
    Check '重映射 CapsLock -> Esc：锁定状态没有变' ($capsBefore -eq [FlowInject]::CapsLockOn())

    # --- send 与修饰键释放 ---------------------------------------------------
    Write-Host '--- send ---'
    NeedFocus 'send'
    Clear-Seen
    CtrlAlt $VK_F12
    Pump 700
    $cLine = $seen | Where-Object { $_ -match 'D:C\|' } | Select-Object -First 1
    if ($null -eq $cLine) { Diag "send: seen=[$(SeenDump)] $(FgInfo)" }
    Check 'send：目标键到达' ($null -ne $cLine)
    Check 'send：带着 Ctrl 而没有 Alt' ($cLine -match 'c=True' -and $cLine -match 'a=False')

    # --- 小键盘 vs 主键盘 ----------------------------------------------------
    Write-Host '--- 小键盘 ---'
    NeedFocus '小键盘'
    ClipSet 'SENTINEL'
    TapKeyEx $VK_NUMPAD_SUB $false
    Pump 400
    Check '小键盘 - 触发它的绑定' ((ClipGet) -eq 'NUMPAD-SUB')
    ClipSet 'SENTINEL'
    TapKeyEx $VK_OEM_MINUS $false
    Pump 400
    Check '主键盘的 - 不会触发它' ((ClipGet) -eq 'SENTINEL')
    ClipSet 'SENTINEL'
    TapKeyEx $VK_NUMPAD_ADD $false
    Pump 400
    Check '小键盘 + 触发它的绑定' ((ClipGet) -eq 'NUMPAD-ADD')
    ClipSet 'SENTINEL'
    TapKeyEx $VK_OEM_PLUS $false
    Pump 400
    Check '主键盘的 = 不会触发它' ((ClipGet) -eq 'SENTINEL')
    ClipSet 'SENTINEL'
    TapKeyEx $VK_RETURN $true
    Pump 400
    Check '小键盘 Enter 触发它的绑定' ((ClipGet) -eq 'NUMPAD-ENTER')
    ClipSet 'SENTINEL'
    TapKeyEx $VK_RETURN $false
    Pump 400
    Check '主键盘的 Enter 不会触发它' ((ClipGet) -eq 'SENTINEL')

    # --- window 动作 ---------------------------------------------------------
    Write-Host '--- window 动作 ---'
    NeedFocus 'window 动作'
    CtrlAlt $VK_F17
    $appeared = WaitUntil { @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -eq $targetTitle }).Count -ge 1 } 25000
    Check 'window activate 启动了目标窗口' $appeared
    Check 'window activate 把它拿到前台' (WaitUntil { [FlowInject]::ForegroundTitle() -eq $targetTitle } 8000)
    $targetProc = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -eq $targetTitle } | Select-Object -First 1
    if ($null -eq $targetProc) { throw '目标窗口在做 toggle 检查之前就没了' }
    CtrlAlt $VK_F17
    Check '再按一次把它收起（默认 toggle 是开的）' (
        (WaitUntil { $targetProc.Refresh(); [FlowInject]::IsWindowMinimized($targetProc.MainWindowHandle) } 8000))
    CtrlAlt $VK_F17
    Check '再按一次把它恢复出来' (
        (WaitUntil { $targetProc.Refresh(); -not [FlowInject]::IsWindowMinimized($targetProc.MainWindowHandle) } 8000))
    Check '恢复之后它又在前台' (WaitUntil { [FlowInject]::ForegroundTitle() -eq $targetTitle } 8000)
    foreach ($proc in @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -eq $targetTitle })) {
        $proc.CloseMainWindow() | Out-Null
    }
    Pump 800

    # --- 选单弹窗 ------------------------------------------------------------
    Write-Host '--- 选单弹窗 ---'
    FocusCatcher
    ClipSet 'SENTINEL'
    CtrlAlt $VK_F9
    $menuUp = WaitUntil { [FlowInject]::HasWindowTitled($daemon.Id, $MENU_TITLE) } 5000
    Check '选单窗口出现了' $menuUp
    $menuTitles = [FlowInject]::TitlesOfPid($daemon.Id)
    Write-Host "         daemon windows: $($menuTitles -join ' | ')"
    Check '选单窗口拿到了键盘焦点' (WaitUntil { [FlowInject]::ForegroundTitle() -eq $MENU_TITLE } 4000)
    Clear-Seen
    TapKey 0x54   # 't'
    Pump 900
    Check '按条目键真的执行了那条动作' ((ClipGet) -eq 'MENU-OK')
    Check '选完之后选单关掉了' (-not [FlowInject]::HasWindowTitled($daemon.Id, $MENU_TITLE))
    FocusCatcher
    CtrlAlt $VK_F9
    $menuUp2 = WaitUntil { [FlowInject]::HasWindowTitled($daemon.Id, $MENU_TITLE) } 5000
    Check '再按一次能把选单重新打开（不是新开一个窗口）' $menuUp2
    $menuFocused = WaitUntil { [FlowInject]::ForegroundTitle() -eq $MENU_TITLE } 4000
    if (-not $menuFocused) { Diag "menu: the reopened window did not take focus: $(FgInfo)" }
    Check '重新打开的选单也拿到了键盘焦点' $menuFocused
    TapKey $VK_ESC
    Pump 700
    Check 'Esc 只关窗、什么都不选' (WaitUntil { -not [FlowInject]::HasWindowTitled($daemon.Id, $MENU_TITLE) } 3000)
    Check 'Esc 没动剪贴板' ((ClipGet) -eq 'MENU-OK')

    # --- 帮助弹窗 ------------------------------------------------------------
    Write-Host '--- 帮助弹窗 ---'
    FocusCatcher
    ClipSet 'SENTINEL'
    CtrlAlt $VK_F8
    $helpUp = WaitUntil { [FlowInject]::HasWindowTitled($daemon.Id, $HELP_TITLE) } 5000
    Check '帮助窗口出现了' $helpUp
    Check '帮助窗口拿到了键盘焦点' (WaitUntil { [FlowInject]::ForegroundTitle() -like "$HELP_TITLE*" } 4000)
    function HelpCounts {
        $t = [FlowInject]::TitlesOfPid($daemon.Id) | Where-Object { $_ -like "$HELP_TITLE*" } | Select-Object -First 1
        if ($t -match '(\d+)/(\d+)') { return @{ Visible = [int]$Matches[1]; Total = [int]$Matches[2] } }
        return $null
    }
    $full = HelpCounts
    Write-Host "         help caption: $($full.Visible)/$($full.Total)"
    Check '标题里的可见/总数是满的' ($null -ne $full -and $full.Total -eq 17)
    TapKey 0x46   # 'f'
    TapKey 0x31   # '1'
    TapKey 0x38   # '8'
    Pump 800
    $filtered = HelpCounts
    Write-Host "         筛选 F18 之后: $($filtered.Visible)/$($filtered.Total)"
    Check '输入就筛选，标题里的计数变小' (
        $null -ne $filtered -and $filtered.Visible -lt $full.Visible -and $filtered.Visible -ge 1)
    TapKey $VK_RETURN
    Pump 800
    Check 'Enter 把选中那行的按键写进了剪贴板' ((ClipGet) -match 'F18')
    TapKey $VK_ESC
    Pump 600
    $cleared = HelpCounts
    Check '第一下 Esc 只清筛选' ($null -ne $cleared -and $cleared.Visible -eq $full.Visible)
    TapKey $VK_ESC
    Pump 700
    Check '第二下 Esc 才关窗' (-not [FlowInject]::HasWindowTitled($daemon.Id, $HELP_TITLE))
    FocusCatcher

    # --- 挂起 / 恢复 ---------------------------------------------------------
    Write-Host '--- 挂起 / 恢复 ---'
    NeedFocus '挂起'
    ClipSet 'BEFORE-SUSPEND'
    CtrlAlt $VK_F11
    $suspended = WaitUntil { (DaemonText) -match 'hotkeys suspended' } 4000
    Check '挂起写进了日志' $suspended
    ClipSet 'STILL-SUSPENDED'
    CtrlAlt $VK_F24
    Pump 700
    Check '挂起时别的绑定都不触发' ((ClipGet) -eq 'STILL-SUSPENDED')
    CtrlAlt $VK_F11
    $resumed = WaitUntil { (DaemonText) -match 'hotkeys resumed' } 4000
    Check '挂起快捷键自己仍然可用，所以能恢复' $resumed
    CtrlAlt $VK_F24
    Pump 700
    Check '恢复之后绑定又生效了' ((ClipGet) -eq 'MARKER-1')

    # --- 重载 ----------------------------------------------------------------
    Write-Host '--- 重载 ---'
    Write-Config 'MARKER-2'
    Pump 300
    NeedFocus '重载'
    CtrlAlt $VK_F6
    $reloaded = WaitUntil { (DaemonText) -match 'reloaded' } 5000
    Check '重载写进了日志' $reloaded
    ClipSet 'SENTINEL'
    CtrlAlt $VK_F24
    Pump 800
    Check '重载之后改过的配置文本立刻生效' ((ClipGet) -eq 'MARKER-2')

    # --- 退出 ----------------------------------------------------------------
    Write-Host '--- 退出 ---'
    NeedFocus '退出'
    CtrlAlt $VK_F10
    $gone = $daemon.WaitForExit(6000)
    Check 'quit 快捷键停掉了守护进程' $gone
    if ($gone) {
        # PowerShell 读不到用 `-PassThru` 启动的进程的退出码（只有 `-Wait` 才有），
        # 所以和 oskeyd 的 e2e 一样：干净的退出靠日志与进程表来判定。
        $logText = DaemonText
        Check '退出过程里没有 ERROR' ($logText -notmatch 'ERROR')
        $lines = @(Get-Content $daemonLog | Where-Object { $_.Trim() -ne '' })
        Check '最后一行日志是钩子被卸掉' ($lines[-1] -match 'keyboard hook removed')
        Check '进程表里已经没有它了' (
            -not (@(Get-Process -Id $daemon.Id -ErrorAction SilentlyContinue)).Count)
        FocusCatcher
        Clear-Seen
        TapKey $VK_F18
        Pump 500
        Check '钩子卸掉之后按键重新到达焦点窗口' (SeenHas 'D:F18')
    }

    # --- 没有按键卡在按下状态 ------------------------------------------------
    Write-Host '--- 卡键检查 ---'
    foreach ($pair in @(@('Ctrl', $VK_CTRL), @('Alt', $VK_ALT), @('Shift', $VK_SHIFT), @('Win', $VK_LWIN))) {
        Check "没有卡住的 $($pair[0]) 键" (-not [FlowInject]::IsDown($pair[1]))
    }
}
catch {
    Write-Host "EXCEPTION: $_"
    $script:failures += "exception: $_"
}
finally {
    if (-not $daemon.HasExited) { $daemon.Kill() }
    foreach ($proc in @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -eq $targetTitle })) {
        $proc.Kill()
    }
    $form.Close()
    Remove-Item Env:\FLOWKEYD_ACCEPT_INJECTED -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "checks: $script:checks, failures: $($script:failures.Count)"
foreach ($f in $script:failures) { Write-Host "  FAILED: $f" }
if ($script:failures.Count -gt 0) { exit 1 }
exit 0
