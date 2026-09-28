#Requires -Version 5.1
<#
.SYNOPSIS
    One-click install / upgrade for flowkeyd: download the full package from the
    GitHub Release, extract it locally and launch it.

.DESCRIPTION
    Default flow:
      1. ask GitHub for the latest release tag (or pass -Version);
      2. download the full package flowkeyd-<version>-windows-x64.zip and verify
         it against the matching .sha256;
      3. extract it into the install directory
         (default %LOCALAPPDATA%\Programs\flowkeyd, -InstallDir overrides it);
      4. let a running instance exit cleanly (flowkeyd.exe --quit);
      5. install over the old copy and create a Start Menu shortcut;
      6. launch flowkeyd (the daemon elevates itself on first start, so Windows
         will show one UAC prompt).

    Two hosts are used: raw.githubusercontent.com for this script, and the
    GitHub release download pages for the package and its .sha256 file. HTTPS
    goes through the system Schannel stack, so no extra PowerShell module is
    needed.

    This file is deliberately pure ASCII and has NO UTF-8 BOM: it is meant to be
    run straight from the web, for example

        powershell -nop -c "irm https://raw.githubusercontent.com/xingjianxu/flowkeyd/master/install.ps1 | iex"

    and a leading BOM would make the PowerShell parser treat the param() block
    as an argument list of a command (Invoke-Expression then fails with
    "Invalid left-hand side of assignment" on the first parameter). Keep it that
    way when editing: no BOM, no non-ASCII characters.

.EXAMPLE
    # install the latest version into the default directory and launch it
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1

.EXAMPLE
    # install a specific version, no shortcut, no launch
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 `
        -Version 26-09-27-dc6b332 -NoShortcut -NoLaunch
#>
param(
    # Version to install (like 26-09-27-dc6b332, an optional leading "v" is
    # fine). Defaults to the latest release.
    [string]$Version = '',
    # Install directory. Defaults to %LOCALAPPDATA%\Programs\flowkeyd.
    [string]$InstallDir = '',
    # Do not launch flowkeyd after installing.
    [switch]$NoLaunch,
    # Do not create a Start Menu shortcut.
    [switch]$NoShortcut,
    # Skip the sha256 check (not recommended).
    [switch]$SkipChecksum
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # the progress bar makes Invoke-WebRequest several times slower

$Repo = 'xingjianxu/flowkeyd'
$UserAgent = 'flowkeyd-installer'
$ApiLatest = "https://api.github.com/repos/$Repo/releases/latest"
$DownloadBase = "https://github.com/$Repo/releases/download"

function Write-Step { param([string]$Text) Write-Host ''; Write-Host "== $Text" -ForegroundColor Cyan }
function Write-Info { param([string]$Text) Write-Host "   $Text" }
function Write-Warn { param([string]$Text) Write-Host "   warning: $Text" -ForegroundColor Yellow }

# PowerShell 5.1 may still have TLS 1.2 disabled, and GitHub only accepts 1.2+.
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
    Write-Warn "could not enable TLS 1.2: $($_.Exception.Message)"
}

function Get-RemoteFile {
    param([string]$Uri, [string]$Path)
    Invoke-WebRequest -Uri $Uri -OutFile $Path -UseBasicParsing -Headers @{ 'User-Agent' = $UserAgent }
}

# Same as Get-RemoteFile, but returns $false on 404 instead of throwing (older
# releases may not have a .sha256 file).
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
    Write-Info "querying the latest release of $Repo"
    $headers = @{ 'User-Agent' = $UserAgent; 'Accept' = 'application/vnd.github+json' }
    $release = Invoke-RestMethod -Uri $ApiLatest -Headers $headers
    if (-not $release.tag_name) { throw 'GitHub did not return a tag_name' }
    return ("$($release.tag_name)").TrimStart('v', 'V')
}

function Test-Checksum {
    param([string]$Zip, [string]$Sum)
    $expected = (((Get-Content -LiteralPath $Sum -Raw) -split '\s+')[0]).ToLowerInvariant()
    if ($expected -notmatch '^[0-9a-f]{64}$') { throw "cannot parse the checksum file: $Sum" }
    $actual = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) {
        throw "sha256 mismatch (expected $expected, got $actual). The download may be incomplete, please retry."
    }
    Write-Info "sha256 verified: $actual"
}

function Get-RunningInstance {
    return @(Get-Process -Name 'flowkeyd' -ErrorAction SilentlyContinue)
}

# Let a running instance take the clean exit path (--quit uses a named event and
# works across integrity levels). Kill it if it does not react, and finally tell
# the user to handle it by hand.
function Stop-RunningInstance {
    param([string]$QuitExe, [string]$TempDir)

    $running = Get-RunningInstance
    if ($running.Count -eq 0) {
        Write-Info 'no flowkeyd instance is running'
        return
    }
    $pids = ($running | ForEach-Object { $_.Id }) -join ', '
    Write-Info "running instance(s): pid $pids"

    if (Test-Path -LiteralPath $QuitExe) {
        Write-Info 'asking it to exit cleanly (--quit)'
        $stdout = Join-Path $TempDir 'quit-out.txt'
        $stderr = Join-Path $TempDir 'quit-err.txt'
        try {
            Start-Process -FilePath $QuitExe -ArgumentList @('--quit', '--no-prompt') -NoNewWindow -Wait -PassThru `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr | Out-Null
        } catch {
            Write-Warn "calling --quit failed: $($_.Exception.Message)"
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
            Write-Info 'the instance exited'
            return
        }
        Start-Sleep -Milliseconds 500
    }

    $still = Get-RunningInstance
    $stillPids = ($still | ForEach-Object { $_.Id }) -join ', '
    Write-Warn "the instance did not exit within 15 seconds (pid $stillPids), killing it"
    try {
        $still | Stop-Process -Force -ErrorAction Stop
    } catch {
        throw "flowkeyd is still running (pid $stillPids) and this script is not allowed to stop it. Quit it from the tray menu (or run flowkeyd.exe --quit) and run this installer again."
    }
    Start-Sleep -Seconds 1
    if ((Get-RunningInstance).Count -gt 0) {
        throw 'flowkeyd is still running. Quit it from the tray menu and run this installer again.'
    }
    Write-Info 'the instance exited'
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
        $shortcut.Description = 'flowkeyd - a Windows keyboard hook daemon configured with Lua scripts'
        $shortcut.Save()
        Write-Info "Start Menu shortcut: $lnk"
    } catch {
        Write-Warn "could not create the Start Menu shortcut: $($_.Exception.Message)"
    }
}

# ---------------------------------------------------------------------------
# main flow
# ---------------------------------------------------------------------------

if (-not $env:OS -or $env:OS -ne 'Windows_NT') { throw 'this installer only runs on Windows.' }
if (-not [Environment]::Is64BitOperatingSystem) { throw 'flowkeyd is currently only available for 64-bit Windows.' }

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
    Write-Step 'flowkeyd installer'
    Write-Info "install directory: $InstallDir"
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    Write-Step 'resolving the version'
    $resolved = Resolve-Version -Requested $Version
    Write-Info "installing: flowkeyd $resolved"

    $zipName = "flowkeyd-$resolved-windows-x64.zip"
    $zipUri = "$DownloadBase/v$resolved/$zipName"
    $zipPath = Join-Path $tempDir $zipName
    $sumPath = "$zipPath.sha256"

    Write-Step 'downloading the full package (Qt / MinGW runtime included, about 25 MB)'
    try {
        Get-RemoteFile -Uri $zipUri -Path $zipPath
    } catch {
        throw "download failed: $zipUri`n       check that version $resolved exists, or retry later. Original error: $($_.Exception.Message)"
    }
    $sizeMb = [math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB, 1)
    Write-Info "downloaded: $zipPath ($sizeMb MB)"

    if ($SkipChecksum) {
        Write-Warn 'sha256 check skipped (-SkipChecksum)'
    } else {
        if (Get-RemoteFileOptional -Uri "$zipUri.sha256" -Path $sumPath) {
            Test-Checksum -Zip $zipPath -Sum $sumPath
        } else {
            Write-Warn 'this release does not provide a .sha256 file, skipping the check'
        }
    }

    Write-Step 'extracting'
    $extractDir = Join-Path $tempDir 'extract'
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force
    $exeItem = Get-ChildItem -LiteralPath $extractDir -Recurse -Filter 'flowkeyd.exe' -File | Select-Object -First 1
    if (-not $exeItem) { throw 'the downloaded package does not contain flowkeyd.exe' }
    $packageRoot = $exeItem.DirectoryName

    Write-Step 'stopping a running instance'
    $quitExe = $installedExe
    if (-not (Test-Path -LiteralPath $quitExe)) { $quitExe = $exeItem.FullName }
    Stop-RunningInstance -QuitExe $quitExe -TempDir $tempDir

    Write-Step 'installing'
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Path (Join-Path $packageRoot '*') -Destination $InstallDir -Recurse -Force
    if (-not (Test-Path -LiteralPath $installedExe)) { throw "flowkeyd.exe was not found in $InstallDir after installing" }
    Write-Info "installed: $installedExe"

    if (-not $NoShortcut) { New-StartMenuShortcut -ExePath $installedExe -WorkDir $InstallDir }

    Write-Step 'done'
    Write-Output ("  program directory: $InstallDir")
    Write-Output ("  executable       : $installedExe")
    Write-Output ("  config file      : $configPath")
    Write-Output ("  log file         : $logPath")
    Write-Output ("  stop             : `"$installedExe`" --quit")
    Write-Output ("  uninstall        : `"$installedExe`" --quit, then `"$installedExe`" --remove-autostart (administrator),")
    Write-Output  '                     finally delete the program directory and the Start Menu shortcut.'
    Write-Output  '  note             : the first start of flowkeyd asks whether to register the logon autostart task.'

    if ($NoLaunch) {
        Write-Info 'not launching (-NoLaunch)'
    } else {
        Write-Info 'launching flowkeyd (the daemon elevates itself, Windows may show one UAC prompt)'
        Start-Process -FilePath $installedExe
        Write-Output ''
        Write-Host '  installed successfully.' -ForegroundColor Green
    }
} catch {
    Write-Host ''
    Write-Host "installation failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if (Test-Path -LiteralPath $tempDir) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
