#Requires -Version 5.1
<#
.SYNOPSIS
    一键安装 / 升级 flowkeyd：从 GitHub Release 下载完整包，解压到本机并启动。

.DESCRIPTION
    默认流程：
      1. 问 GitHub 要最新 Release 的版本号（也可以用 -Version 指定）；
      2. 下载完整包 flowkeyd-<版本>-windows-x64.zip，并按同名 .sha256 校验；
      3. 解压到安装目录（默认 %LOCALAPPDATA%\Programs\flowkeyd，可用 -InstallDir 改）；
      4. 让正在运行的实例干净退出（flowkeyd.exe --quit）；
      5. 覆盖安装，并创建开始菜单快捷方式；
      6. 启动 flowkeyd（守护进程首次启动会自提权，弹一次 UAC）。

    网络上有两个地址可以用：
      * raw.githubusercontent.com 上的本脚本；
      * GitHub Release 里的完整包与 .sha256。
    HTTPS 走系统自带的 Schannel，不需要任何额外的 PowerShell 模块。

    卸载：先运行 flowkeyd.exe --quit 让它退出，再运行
    flowkeyd.exe --remove-autostart（需要管理员，删除开机自启的计划任务），
    最后删掉安装目录与开始菜单里的 flowkeyd 快捷方式。

.EXAMPLE
    # 装最新版到默认目录并启动
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1

.EXAMPLE
    # 装指定版本，不创建快捷方式、也不启动
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 `
        -Version 26-09-27-dc6b332 -NoShortcut -NoLaunch
#>
param(
    # 要安装的版本号（形如 26-09-27-dc6b332，带不带前导 v 都行）。默认安装最新版。
    [string]$Version = '',
    # 安装目录。默认 %LOCALAPPDATA%\Programs\flowkeyd。
    [string]$InstallDir = '',
    # 安装完成后不启动 flowkeyd。
    [switch]$NoLaunch,
    # 不创建开始菜单快捷方式。
    [switch]$NoShortcut,
    # 跳过 sha256 校验（不推荐）。
    [switch]$SkipChecksum
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # 关掉进度条：它会让 Invoke-WebRequest 慢好几倍

$Repo = 'xingjianxu/flowkeyd'
$UserAgent = 'flowkeyd-installer'
$ApiLatest = "https://api.github.com/repos/$Repo/releases/latest"
$DownloadBase = "https://github.com/$Repo/releases/download"

function Write-Step { param([string]$Text) Write-Host ''; Write-Host "== $Text" -ForegroundColor Cyan }
function Write-Info { param([string]$Text) Write-Host "   $Text" }
function Write-Warn { param([string]$Text) Write-Host "   warning: $Text" -ForegroundColor Yellow }

# PowerShell 5.1 默认可能还禁用 TLS 1.2，GitHub 只接受 1.2+。
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
    Write-Warn "无法设置 TLS 1.2：$($_.Exception.Message)"
}

function Get-RemoteFile {
    param([string]$Uri, [string]$Path)
    Invoke-WebRequest -Uri $Uri -OutFile $Path -UseBasicParsing -Headers @{ 'User-Agent' = $UserAgent }
}

# 和 Get-RemoteFile 一样，但 404 时返回 $false 而不是抛异常（老发布可能没有 .sha256）。
function Get-RemoteFileOptional {
    param([string]$Uri, [string]$Path)
    try {
        Get-RemoteFile -Uri $Uri -Path $Path
        return $true
    } catch {
        $status = 0
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            $status = [int]$_.Exception.Response.StatusCode
        }
        if ($status -eq 404) { return $false }
        throw
    }
}

function Resolve-Version {
    param([string]$Requested)
    if ($Requested) { return $Requested.TrimStart('v', 'V') }
    Write-Info "查询 $Repo 的最新发布"
    $headers = @{ 'User-Agent' = $UserAgent; 'Accept' = 'application/vnd.github+json' }
    $release = Invoke-RestMethod -Uri $ApiLatest -Headers $headers
    if (-not $release.tag_name) { throw 'GitHub 没有返回 tag_name' }
    return ("$($release.tag_name)").TrimStart('v', 'V')
}

function Test-Checksum {
    param([string]$Zip, [string]$Sum)
    $expected = (((Get-Content -LiteralPath $Sum -Raw) -split '\s+')[0]).ToLowerInvariant()
    if ($expected -notmatch '^[0-9a-f]{64}$') { throw "无法解析校验文件：$Sum" }
    $actual = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) {
        throw "sha256 校验失败（期望 $expected，实际 $actual）。文件可能下载不完整，请重试。"
    }
    Write-Info "sha256 校验通过：$actual"
}

function Get-RunningInstance {
    return @(Get-Process -Name 'flowkeyd' -ErrorAction SilentlyContinue)
}

# 让正在运行的实例走干净退出路径（--quit 走的是命名事件，跨权限也能用）。
# 实在不走就强制结束；再不行就报错让人自己处理。
function Stop-RunningInstance {
    param([string]$QuitExe, [string]$TempDir)

    $running = Get-RunningInstance
    if ($running.Count -eq 0) {
        Write-Info '没有正在运行的 flowkeyd'
        return
    }
    $pids = ($running | ForEach-Object { $_.Id }) -join ', '
    Write-Info "正在运行的实例：pid $pids"

    if (Test-Path -LiteralPath $QuitExe) {
        Write-Info '请求它干净退出（--quit）'
        $stdout = Join-Path $TempDir 'quit-out.txt'
        $stderr = Join-Path $TempDir 'quit-err.txt'
        try {
            Start-Process -FilePath $QuitExe -ArgumentList @('--quit', '--no-prompt') -NoNewWindow -Wait -PassThru `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr | Out-Null
        } catch {
            Write-Warn "调用 --quit 失败：$($_.Exception.Message)"
        }
        foreach ($file in @($stdout, $stderr)) {
            if (Test-Path -LiteralPath $file) {
                foreach ($line in @(Get-Content -LiteralPath $file -Encoding UTF8)) {
                    if ("$line") { Write-Info "$line" }
                }
            }
        }
    }

    for ($i = 0; $i -lt 30; $i++) {
        if ((Get-RunningInstance).Count -eq 0) {
            Write-Info '实例已退出'
            return
        }
        Start-Sleep -Milliseconds 500
    }

    $still = Get-RunningInstance
    $stillPids = ($still | ForEach-Object { $_.Id }) -join ', '
    Write-Warn "实例没有在 15 秒内退出（pid $stillPids），强制结束"
    try {
        $still | Stop-Process -Force -ErrorAction Stop
    } catch {
        throw "flowkeyd 还在运行（pid $stillPids），而且没有权限结束它。请先从托盘菜单退出（或运行 flowkeyd.exe --quit），再重新运行本安装脚本。"
    }
    Start-Sleep -Seconds 1
    if ((Get-RunningInstance).Count -gt 0) {
        throw 'flowkeyd 还在运行。请先从托盘菜单退出，再重新运行本安装脚本。'
    }
    Write-Info '实例已退出'
}

function New-StartMenuShortcut {
    param([string]$ExePath, [string]$WorkDir)
    try {
        $programs = [Environment]::GetFolderPath('Programs')
        if (-not $programs) { return }
        if (-not (Test-Path -LiteralPath $programs)) { New-Item -ItemType Directory -Force -Path $programs | Out-Null }
        $lnk = Join-Path $programs 'flowkeyd.lnk'
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($lnk)
        $shortcut.TargetPath = $ExePath
        $shortcut.WorkingDirectory = $WorkDir
        $shortcut.IconLocation = "$ExePath,0"
        $shortcut.Description = 'flowkeyd — 由 Lua 配置驱动的 Windows 键盘钩子守护进程'
        $shortcut.Save()
        Write-Info "开始菜单快捷方式：$lnk"
    } catch {
        Write-Warn "创建开始菜单快捷方式失败：$($_.Exception.Message)"
    }
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

if (-not $env:OS -or $env:OS -ne 'Windows_NT') { throw '本安装脚本只能在 Windows 上运行。' }
if (-not [Environment]::Is64BitOperatingSystem) { throw 'flowkeyd 目前只提供 64 位版本。' }

if (-not $InstallDir) {
    $local = $env:LOCALAPPDATA
    if (-not $local) { $local = Join-Path $env:USERPROFILE 'AppData\Local' }
    $InstallDir = Join-Path $local 'Programs\flowkeyd'
}
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ('flowkeyd-install-' + [guid]::NewGuid().ToString('N'))
$installedExe = Join-Path $InstallDir 'flowkeyd.exe'
$configPath = Join-Path $env:USERPROFILE '.config\flowkeyd\config.lua'
$logPath = Join-Path $env:USERPROFILE '.config\flowkeyd\flowkeyd.log'

try {
    Write-Step 'flowkeyd 安装程序'
    Write-Info "安装目录：$InstallDir"
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    Write-Step '解析版本'
    $resolved = Resolve-Version -Requested $Version
    Write-Info "将安装：flowkeyd $resolved"

    $zipName = "flowkeyd-$resolved-windows-x64.zip"
    $zipUri = "$DownloadBase/v$resolved/$zipName"
    $zipPath = Join-Path $tempDir $zipName
    $sumPath = "$zipPath.sha256"

    Write-Step '下载完整包（含 Qt / MinGW 运行时，约 25 MB）'
    try {
        Get-RemoteFile -Uri $zipUri -Path $zipPath
    } catch {
        throw "下载失败：$zipUri`n       请确认版本号 $resolved 存在，或稍后重试。原始错误：$($_.Exception.Message)"
    }
    $sizeMb = [math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB, 1)
    Write-Info "下载完成：$zipPath（$sizeMb MB）"

    if ($SkipChecksum) {
        Write-Warn '已跳过 sha256 校验（-SkipChecksum）'
    } else {
        if (Get-RemoteFileOptional -Uri "$zipUri.sha256" -Path $sumPath) {
            Test-Checksum -Zip $zipPath -Sum $sumPath
        } else {
            Write-Warn '这个发布没有提供 .sha256，跳过校验'
        }
    }

    Write-Step '解压'
    $extractDir = Join-Path $tempDir 'extract'
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force
    $exeItem = Get-ChildItem -LiteralPath $extractDir -Recurse -Filter 'flowkeyd.exe' -File | Select-Object -First 1
    if (-not $exeItem) { throw '下载的包里没有 flowkeyd.exe' }
    $packageRoot = $exeItem.DirectoryName

    Write-Step '停止正在运行的实例'
    $quitExe = $installedExe
    if (-not (Test-Path -LiteralPath $quitExe)) { $quitExe = $exeItem.FullName }
    Stop-RunningInstance -QuitExe $quitExe -TempDir $tempDir

    Write-Step '安装'
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Path (Join-Path $packageRoot '*') -Destination $InstallDir -Recurse -Force
    if (-not (Test-Path -LiteralPath $installedExe)) { throw "安装后没有找到 $installedExe" }
    Write-Info "已安装：$installedExe"

    if (-not $NoShortcut) { New-StartMenuShortcut -ExePath $installedExe -WorkDir $InstallDir }

    Write-Step '完成'
    Write-Output ("  程序目录：$InstallDir")
    Write-Output ("  主程序　：$installedExe")
    Write-Output ("  配置文件：$configPath")
    Write-Output ("  日志文件：$logPath")
    Write-Output ("  停止：    `"$installedExe`" --quit")
    Write-Output ("  卸载：    `"$installedExe`" --quit，再 `"$installedExe`" --remove-autostart（需管理员），")
    Write-Output  '            最后删掉程序目录与开始菜单里的 flowkeyd 快捷方式。'
    Write-Output  '  提示：    首次启动 flowkeyd 会问你要不要注册「登录时自启」的计划任务。'

    if ($NoLaunch) {
        Write-Info '已按 -NoLaunch 跳过启动'
    } else {
        Write-Info '正在启动 flowkeyd（守护进程会自提权，可能弹一次 UAC）'
        Start-Process -FilePath $installedExe
        Write-Output ''
        Write-Host '  安装成功。' -ForegroundColor Green
    }
} catch {
    Write-Host ''
    Write-Host "安装失败：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if (Test-Path -LiteralPath $tempDir) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
