# flowkeyd: install / update the resident instance and its logon autostart task.
#
# Why a task and not "shell:startup" or HKCU\...\Run (see AGENTS.md section 10):
# only a scheduled task can start a program elevated without a UAC prompt, and
# flowkeyd needs elevation to drive windows of elevated processes and to run the
# power actions. A service is not an option either: it would run in session 0,
# where a WH_KEYBOARD_LL hook sees no desktop and there is no tray.
#
# Why the install directory and not build\dist-release:
# the task action contains a path, and a task whose target disappears fails
# *silently* (no tray icon, no hotkeys, nobody notices). Pointing it at a stable
# install directory means updates only ever replace the file, never the path --
# and the build tree stops being locked by the running instance.
#
# This file is deliberately pure ASCII: PowerShell 5.1 decodes a .ps1 without a
# BOM as ANSI/GBK, which turns non-ASCII text into garbage (AGENTS.md section 10).
[CmdletBinding()]
param(
    # Build output to install from (default: build\dist-release, then build\windows-release).
    [string] $Source,
    # Stable install directory the scheduled task points at.
    [string] $InstallDir = "$env:ProgramFiles\flowkeyd",
    [string] $TaskName = 'flowkeyd',
    # Only refresh flowkeyd.exe; leave the Qt/MinGW runtime that is already there alone.
    [switch] $ExeOnly,
    # Do not start the task after installing.
    [switch] $NoStart,
    # Remove the task (and, with -RemoveFiles, the install directory).
    [switch] $Uninstall,
    # With -Uninstall: also delete the install directory.
    [switch] $RemoveFiles,
    # Stop flowkeyd processes that are running from somewhere else (e.g. the build tree).
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$LogonDelaySeconds = 15

# The daemon keeps the log file open for writing, so File.ReadAllLines (which asks
# for FileShare.Read) fails with a sharing violation -- use a FileShare.ReadWrite
# stream instead.
function Read-LogLines {
    param([string] $Path)
    $stream = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        return ($reader.ReadToEnd() -split "`r?`n")
    } finally {
        $stream.Close()
    }
}

function Write-Step([string] $text) { Write-Host "==> $text" }

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw ('This script must run elevated: registering a task with "run with highest ' +
               'privileges" needs administrator rights. Open an elevated PowerShell and re-run: ' +
               "`n  Start-Process -Verb RunAs -FilePath powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','$PSCommandPath'")
    }
}

# `$_.Path` throws for processes we cannot open, and StrictMode turns that into a
# hard error, so every read goes through this helper.
function Get-ProcessPath {
    param($Process)
    try { return $Process.Path } catch { return $null }
}

function Get-ManagedProcesses {
    param([string] $Directory)
    Get-Process flowkeyd -ErrorAction SilentlyContinue | Where-Object {
        $path = Get-ProcessPath $_
        $path -and $path.StartsWith($Directory, [StringComparison]::OrdinalIgnoreCase)
    }
}

function Get-ForeignProcesses {
    param([string] $Directory)
    Get-Process flowkeyd -ErrorAction SilentlyContinue | Where-Object {
        $path = Get-ProcessPath $_
        -not $path -or -not $path.StartsWith($Directory, [StringComparison]::OrdinalIgnoreCase)
    }
}

function Get-ProcessPathList {
    param($Processes)
    return (($Processes | ForEach-Object { Get-ProcessPath $_ }) -join ', ')
}

# Runs the CLI and returns @{ exit = ...; output = ... }. The GUI subsystem exe
# does not make cmd.exe wait, so this has to go through Start-Process -Wait.
function Invoke-Cli {
    param([string] $Exe, [string[]] $ArgumentList)
    $stdout = Join-Path $env:TEMP ('flowkeyd-cli-' + [guid]::NewGuid().ToString('N') + '.out')
    $stderr = $stdout + '.err'
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList $ArgumentList -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $text = ''
        if (Test-Path $stdout) { $text += [System.IO.File]::ReadAllText($stdout, [System.Text.Encoding]::UTF8) }
        if (Test-Path $stderr) { $text += [System.IO.File]::ReadAllText($stderr, [System.Text.Encoding]::UTF8) }
        return @{ exit = $process.ExitCode; output = $text }
    } finally {
        Remove-Item $stdout, $stderr -ErrorAction SilentlyContinue
    }
}

function Test-CliSupportsQuit {
    param([string] $Exe)
    $result = Invoke-Cli -Exe $Exe -ArgumentList @('--help')
    return ($result.output -match '--quit')
}

function Stop-ManagedInstance {
    param([string] $Directory)
    $running = @(Get-ManagedProcesses -Directory $Directory)
    if ($running.Count -eq 0) {
        Write-Host '    no installed instance is running'
        return
    }
    $exe = Join-Path $Directory 'flowkeyd.exe'
    $asked = $false
    if ((Test-Path $exe) -and (Test-CliSupportsQuit -Exe $exe)) {
        # --quit waits for the instance to drop its hook, so the exe is free afterwards.
        $result = Invoke-Cli -Exe $exe -ArgumentList @('--quit')
        Write-Host ('    ' + $result.output.Trim())
        $asked = $result.exit -eq 0
    }
    if (-not $asked) {
        Write-Warning 'the installed build has no --quit; falling back to Stop-Process (the tray icon may linger until you hover it)'
    }
    for ($i = 0; $i -lt 50; $i++) {
        if (@(Get-ManagedProcesses -Directory $Directory).Count -eq 0) {
            Write-Host '    instance stopped'
            return
        }
        Start-Sleep -Milliseconds 200
    }
    Write-Warning 'the instance is still running after 10 s; killing it'
    Get-ManagedProcesses -Directory $Directory | Stop-Process -Force
    Start-Sleep -Seconds 1
}

function New-TaskXml {
    param([string] $Exe, [string] $Directory, [string] $UserId, [int] $Delay)
    $xml = @'
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>flowkeyd: Lua-configured keyboard hook daemon, started at logon with the highest privileges (no UAC prompt). Managed by scripts/install.ps1.</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>__USERID__</UserId>
      <Delay>PT__DELAY__S</Delay>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>__USERID__</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>false</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <WakeToRun>false</WakeToRun>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>5</Priority>
    <RestartOnFailure>
      <Interval>PT1M</Interval>
      <Count>3</Count>
    </RestartOnFailure>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>__EXE__</Command>
      <WorkingDirectory>__DIR__</WorkingDirectory>
    </Exec>
  </Actions>
</Task>
'@
    $xml = $xml.Replace('__USERID__', $UserId)
    $xml = $xml.Replace('__EXE__', $Exe)
    $xml = $xml.Replace('__DIR__', $Directory)
    $xml = $xml.Replace('__DELAY__', [string] $Delay)
    return $xml
}

function Register-FlowkeydTask {
    param([string] $Exe, [string] $Directory)
    $userId = "$env:USERDOMAIN\$env:USERNAME"
    $xml = New-TaskXml -Exe $Exe -Directory $Directory -UserId $userId -Delay $LogonDelaySeconds
    $taskFile = Join-Path $env:TEMP 'flowkeyd-task.xml'
    [System.IO.File]::WriteAllText($taskFile, $xml, [System.Text.Encoding]::Unicode)
    Write-Host "    task XML: $taskFile"
    if (Get-Command Register-ScheduledTask -ErrorAction SilentlyContinue) {
        Register-ScheduledTask -Xml $xml -TaskName $TaskName -Force | Out-Null
    } else {
        & schtasks.exe /Create /TN $TaskName /XML $taskFile /F | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "schtasks /Create failed with exit code $LASTEXITCODE" }
    }
}

function Unregister-FlowkeydTask {
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if (-not $existing) {
        Write-Host '    no scheduled task to remove'
        return
    }
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

function Resolve-Source {
    param([string] $Explicit)
    $root = Split-Path -Parent $PSScriptRoot
    $candidates = @()
    if ($Explicit) { $candidates += $Explicit }
    $candidates += (Join-Path $root 'build\dist-release')
    $candidates += (Join-Path $root 'build\windows-release')
    foreach ($candidate in $candidates) {
        $exe = Join-Path $candidate 'flowkeyd.exe'
        if (Test-Path $exe) { return (Resolve-Path $candidate).Path }
    }
    throw ("no flowkeyd.exe found; build the release profile first (cmake --build --preset release) " +
           "or pass -Source <dir>. Looked in:`n  " + ($candidates -join "`n  "))
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

Assert-Admin

if ($Uninstall) {
    Write-Step "stopping the instance in $InstallDir"
    Stop-ManagedInstance -Directory $InstallDir
    Write-Step "removing the scheduled task `"$TaskName`""
    Unregister-FlowkeydTask
    if ($RemoveFiles) {
        Write-Step "deleting $InstallDir"
        Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    $foreign = @(Get-ForeignProcesses -Directory $InstallDir)
    if ($foreign.Count -gt 0) {
        Write-Warning ('flowkeyd is still running from another directory: ' + (Get-ProcessPathList $foreign))
    }
    Write-Host 'flowkeyd autostart removed.'
    exit 0
}

$sourceDir = Resolve-Source -Explicit $Source
$exePath = Join-Path $InstallDir 'flowkeyd.exe'

Write-Step "installing $sourceDir -> $InstallDir"
Write-Host "    task name: $TaskName, logon delay: $LogonDelaySeconds s, run level: highest"

Stop-ManagedInstance -Directory $InstallDir

$foreign = @(Get-ForeignProcesses -Directory $InstallDir)
if ($foreign.Count -gt 0) {
    $message = 'another flowkeyd.exe is running from outside the install directory: ' +
               (Get-ProcessPathList $foreign)
    if ($Force) {
        Write-Warning ($message + ' -- stopping it (-Force)')
        $foreign | Stop-Process -Force
        Start-Sleep -Seconds 1
    } else {
        Write-Warning ($message + '; the new instance will refuse to start (single-instance check). Re-run with -Force to stop it.')
    }
}

if ($ExeOnly -and (Test-Path $exePath)) {
    Write-Step 'refreshing flowkeyd.exe only (-ExeOnly)'
    Copy-Item (Join-Path $sourceDir 'flowkeyd.exe') $exePath -Force
} else {
    Write-Step 'copying the release (robocopy /MIR)'
    if (-not (Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir | Out-Null }
    & robocopy.exe $sourceDir $InstallDir /MIR /R:2 /W:1 /NJH /NJS /NDL /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
}

Write-Step "registering the scheduled task `"$TaskName`""
Register-FlowkeydTask -Exe $exePath -Directory $InstallDir
$check = Invoke-Cli -Exe $exePath -ArgumentList @('--check')
Write-Host ('    config check: ' + $check.output.Trim())

if ($NoStart) {
    Write-Host 'not starting (as requested); start it with: Start-ScheduledTask -TaskName ' -NoNewline
    Write-Host $TaskName
    exit 0
}

Write-Step "starting the task"
Start-ScheduledTask -TaskName $TaskName
$started = $null
for ($i = 0; $i -lt 40; $i++) {
    $started = @(Get-ManagedProcesses -Directory $InstallDir) | Select-Object -First 1
    if ($started) { break }
    Start-Sleep -Milliseconds 250
}
if (-not $started) {
    throw "the task started but no flowkeyd.exe is running from $InstallDir; check the log file and the task history"
}
Write-Host ("    running: pid " + $started.Id + "  " + $started.Path)

$logPath = Join-Path $env:USERPROFILE '.config\flowkeyd\flowkeyd.log'
if (Test-Path $logPath) {
    Write-Step "last log lines ($logPath)"
    $lines = @(Read-LogLines -Path $logPath | Where-Object { $_ -ne '' })
    $take = [Math]::Min(5, $lines.Count)
    if ($take -gt 0) {
        $lines[($lines.Count - $take)..($lines.Count - 1)] | ForEach-Object { Write-Host "    $_" }
    }
}

Write-Host ''
Write-Host "flowkeyd is installed and will start at every logon (task `"$TaskName`", highest privileges)."
Write-Host '  update:  cmake --build --preset release; powershell -File scripts\install.ps1   (or -ExeOnly for speed)'
Write-Host '  stop:    "C:\Program Files\flowkeyd\flowkeyd.exe" --quit'
Write-Host '  remove:  powershell -File scripts\uninstall.ps1 -RemoveFiles'
