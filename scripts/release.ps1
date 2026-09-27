param(
    [string]$Notes = '',
    [string]$NotesFile = '',
    [string]$WorkDir = "$env:TEMP\flowkeyd-release",
    [string]$DistDir = 'build\dist-release',
    [ValidateSet('Optimal', 'Fastest', 'NoCompression')]
    [string]$Compression = 'Optimal',
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipUpload,
    [switch]$Draft,
    [switch]$Prerelease,
    [switch]$Clobber,
    [switch]$Push,
    [switch]$AllowDirty,
    [switch]$SkipResident,
    [switch]$LeaveStopped,
    [switch]$Force
)

# =============================================================================
# flowkeyd 的发布脚本：构建 release -> 把 build/dist-release 打成一个 zip ->
# 生成 sha256 -> 用 GitHub CLI（gh）上传到 GitHub Release。
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1
#   powershell.exe ... -SkipUpload              # 只打包，不上传（先看一眼产物）
#   powershell.exe ... -SkipBuild               # 用现有的 build/dist-release 打包
#   powershell.exe ... -NotesFile notes.md      # 自定义说明（默认让 gh 自己生成）
#   powershell.exe ... -Draft -Prerelease       # 建草稿 / 标成预发布
#
# ### 需要先装好的东西（脚本会自己检查，缺了会直接告诉你）
#
#   * **GitHub CLI**：`scoop install gh`（或 `winget install GitHub.cli`），
#     然后 `gh auth login` 走一次浏览器登录。脚本用 `gh auth status` 检查。
#     `-SkipUpload`（只打包）时不需要 gh。
#   * CMake 在 `C:\Qt\Tools\CMake_64\bin\cmake.exe`，找不到时用 `PATH` 上的。
#
# ### 为什么要求工作区干净、HEAD 已推送
#
# 版本号里的 git 修订是**构建时的 HEAD**（AGENTS.md 第 2 节第 12 条），而 GitHub
# 建 tag 时那个提交必须已经在远端。所以脏工作区要显式 `-AllowDirty`、
# HEAD 没推到 origin 要显式 `-Push`（先用 git 自己的凭证推，失败再用 gh 的凭证
# 助手重试一次，见 Push-Branch）。
#
# ### 常驻实例会被短暂停掉（默认行为）
#
# 常驻实例从 `build\dist-release` 跑的时候会锁住那个 exe，release 链接会失败
# （`cannot open output file flowkeyd.exe: Permission denied`）。所以脚本在**要
# 构建**的时候先 `flowkeyd.exe --quit` 把它停掉，收尾（包括中途失败时）再
# `schtasks /Run /TN flowkeyd` 拉回来 —— 走计划任务，不弹 UAC。
# `-SkipBuild` 不动它；`-SkipResident` 完全不碰它；`-LeaveStopped` 停了不拉回。
#
# ### 退出码
#
#   0 = 成功（发布已建好，或 `-SkipUpload` 已打包完）；其它 = 失败。
#
# ### 编码
#
# 本文件必须以**带 BOM 的 UTF-8** 保存：PowerShell 5.1 会把没有 BOM 的 `.ps1`
# 按 ANSI 代码页解码，里面的中文会变乱码（与 acceptance.ps1 同一个坑）。
# =============================================================================

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $here

$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($found) { $cmake = $found.Source } else { $cmake = '' }
}
# ctest 与 cmake 在同一个 bin 目录里。
$ctest = if ($cmake) { Join-Path (Split-Path -Parent $cmake) 'ctest.exe' } else { '' }
if (-not (Test-Path -LiteralPath $ctest)) {
    $found = Get-Command ctest -ErrorAction SilentlyContinue
    if ($found) { $ctest = $found.Source } else { $ctest = '' }
}

$distRoot = if ([System.IO.Path]::IsPathRooted($DistDir)) { $DistDir } else { Join-Path $here $DistDir }
$distExe = Join-Path $distRoot 'flowkeyd.exe'
$releaseExe = Join-Path $here 'build\windows-release\flowkeyd.exe'

$script:residentStopped = $false
$script:exitCode = 0
$script:head = ''
$script:shortHead = ''
$script:version = ''
$script:tag = ''

# --- 输出 ---
function Step([string]$text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }
function Info([string]$text) { Write-Host "   $text" }
function Warn([string]$text) { Write-Host "   warning: $text" -ForegroundColor Yellow }

# 原生命令都套一层小助手：`$ErrorActionPreference = 'Stop'` 下，原生命令往 stderr
# 写一个字就会被 PowerShell 包成**终止性异常**（`git push` 那次就是这么被打断的，
# 把脚本输出重定向到文件时尤其容易撞上），而 cmake / ninja / git / gh 正常都会
# 往 stderr 写警告。所以调用期间把 EAP 临时切成 `Continue`，调用完再切回来 ——
# 判定只看退出码。

# 实时输出（构建 / ctest / gh），退出码非 0 就抛。
function Invoke-Live {
    param([string]$Exe, [string[]]$ArgList)
    Info "> $Exe $($ArgList -join ' ')"
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @ArgList
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($code -ne 0) {
        throw "$Exe exited with code $code ($($ArgList -join ' '))"
    }
}

# 捕获输出（`git rev-parse`、`git ls-remote`、`schtasks /XML`、`gh release view`…），
# 退出码非 0 就抛。注意返回值**每行一项**，调用方一律用 `@(...)[0]` 取第一行：
# PowerShell 的函数返回单个元素时会把数组拆掉，直接下标会落到“字符串的第一个
# 字符”上。
function Invoke-Capture {
    param([string]$Exe, [string[]]$ArgList)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @(& $Exe @ArgList 2>&1 | ForEach-Object { "$_".Trim() })
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($code -ne 0) {
        throw "$Exe exited with code $code ($($ArgList -join ' '))"
    }
    return $lines
}

# 只关心成败、不要输出（`git rev-parse --git-dir`、`gh auth status`、
# `git merge-base --is-ancestor`、`schtasks`）：返回退出码。`-Stream` 让它把输出
# （含 stderr）照常打出来，用于 `git push` 这种要看现场的命令。
function Test-Native {
    param([string]$Exe, [string[]]$ArgList, [switch]$Stream)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($Stream) { & $Exe @ArgList } else { & $Exe @ArgList 2>&1 | Out-Null }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
}

# GUI 子系统的 exe（flowkeyd 自己）：用 `& exe` 调用时 PowerShell **不等它、
# 也拿不到它的输出**（与 cmd.exe 同一个坑，见 AGENTS.md 第 10 节），
# 所以统一走 Start-Process + 重定向文件。
function Invoke-Flowkeyd {
    param([string]$Path, [string[]]$ArgList)
    $stdout = Join-Path $WorkDir 'exe-out.txt'
    $stderr = Join-Path $WorkDir 'exe-err.txt'
    $p = Start-Process -FilePath $Path -ArgumentList $ArgList -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $out = @()
    if (Test-Path -LiteralPath $stdout) {
        $out = @(Get-Content -LiteralPath $stdout -Encoding UTF8 | Where-Object { "$_" -ne '' })
    }
    $err = @()
    if (Test-Path -LiteralPath $stderr) {
        $err = @(Get-Content -LiteralPath $stderr -Encoding UTF8 | Where-Object { "$_" -ne '' })
    }
    return [pscustomobject]@{ ExitCode = $p.ExitCode; StdOut = $out; StdErr = $err }
}

function Get-FlowkeydProcesses {
    return @(Get-Process -Name 'flowkeyd' -ErrorAction SilentlyContinue)
}

function Assert-Tools {
    Step 'preflight'
    if (-not $cmake -and -not $SkipBuild) {
        throw 'cmake not found (looked at C:\Qt\Tools\CMake_64\bin\cmake.exe and PATH)'
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'git not found on PATH'
    }
    if ((Test-Native -Exe 'git' -ArgList @('rev-parse', '--git-dir')) -ne 0) {
        throw "$here is not a git repository"
    }
    if ($SkipUpload) {
        Info '-SkipUpload: gh is not needed'
        return
    }
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw 'GitHub CLI (gh) not found. Install it first: scoop install gh   (or: winget install GitHub.cli)   then run: gh auth login'
    }
    if ((Test-Native -Exe 'gh' -ArgList @('auth', 'status')) -ne 0) {
        throw 'gh is installed but not logged in; run: gh auth login'
    }
    Info 'gh is installed and logged in'
}

function Assert-GitState {
    $dirty = @(Invoke-Capture -Exe 'git' -ArgList @('status', '--porcelain'))
    if ($dirty.Count -gt 0) {
        if (-not $AllowDirty) {
            Warn "$($dirty.Count) uncommitted path(s) in the working tree"
            throw 'the working tree is dirty; the version number hashes HEAD, so the package would not match the tag. Commit first, or re-run with -AllowDirty'
        }
        Warn "continuing with a dirty working tree (-AllowDirty): $($dirty.Count) path(s)"
    } else {
        Info 'the working tree is clean'
    }

    $script:head = @(Invoke-Capture -Exe 'git' -ArgList @('rev-parse', 'HEAD'))[0]
    $script:shortHead = @(Invoke-Capture -Exe 'git' -ArgList @('rev-parse', '--short', 'HEAD'))[0]
    $branch = @(Invoke-Capture -Exe 'git' -ArgList @('rev-parse', '--abbrev-ref', 'HEAD'))[0]

    if ($branch -eq 'HEAD') {
        Warn "HEAD is detached; the release will be created at $($script:shortHead)"
        return
    }

    $remoteLines = @(Invoke-Capture -Exe 'git' -ArgList @('ls-remote', 'origin', "refs/heads/$branch"))
    $remoteSha = ''
    if ($remoteLines.Count -gt 0 -and $remoteLines[0]) {
        $remoteSha = ($remoteLines[0] -split '\s+')[0]
    }
    if ($remoteSha -eq $script:head) {
        Info "HEAD $($script:shortHead) is pushed to origin/$branch"
        return
    }
    $contained = $false
    if ($remoteSha) {
        $contained = ((Test-Native -Exe 'git' -ArgList @('merge-base', '--is-ancestor', $script:head, $remoteSha)) -eq 0)
    }
    if ($contained) {
        Info "HEAD $($script:shortHead) is already contained in origin/$branch"
        return
    }
    if ($Push) {
        Push-Branch -Branch $branch
        return
    }
    throw "HEAD $($script:shortHead) is not pushed to origin/$branch; run 'git push' first, or re-run with -Push"
}

# `-Push` 用得上：先按 git 自己的凭证推（交互式 shell 里通常就是用户的 credential
# manager），失败**再用 gh 的凭证助手重试一次**。agent / 无交互 shell 里 git 的
# credential manager 往往起不来（`Unable to persist credentials with the
# 'wincredman' credential store` 这种），而 gh 已经登录好了。
# 那个空的 `credential.helper=` 是必需的：`-c` 是**追加**，清不掉 git 自己配的
# helper，不清就还是那个会失败的老路子先被问到。
function Push-Branch {
    param([string]$Branch)
    Info "> git push origin $Branch"
    $code = Test-Native -Exe 'git' -ArgList @('push', 'origin', $Branch) -Stream
    if ($code -eq 0) { return }
    Warn "git push exited with code $code; retrying with gh's credential helper"
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw 'git push failed and gh is not available to provide credentials'
    }
    $helper = 'credential.helper=!gh auth git-credential'
    Invoke-Live -Exe 'git' -ArgList @('-c', 'credential.helper=', '-c', $helper, 'push', 'origin', $Branch)
}

function Stop-Resident {
    $procs = Get-FlowkeydProcesses
    if ($procs.Count -eq 0) {
        Info 'no flowkeyd instance is running'
        return
    }
    if ($SkipResident) {
        Warn "a flowkeyd instance is running (pid $($procs.Id -join ', ')) and -SkipResident was given; the release link may fail"
        return
    }
    $quitExe = $distExe
    if (-not (Test-Path -LiteralPath $quitExe)) { $quitExe = $releaseExe }
    if (-not (Test-Path -LiteralPath $quitExe)) {
        throw 'a flowkeyd instance is running but no built exe is available to ask it to quit'
    }
    Info "asking the running instance (pid $($procs.Id -join ', ')) to quit"
    $r = Invoke-Flowkeyd -Path $quitExe -ArgList @('--quit', '--no-prompt')
    foreach ($line in $r.StdOut) { Info $line }
    foreach ($line in $r.StdErr) { Info $line }
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        if ((Get-FlowkeydProcesses).Count -eq 0) {
            Info 'the running instance exited'
            $script:residentStopped = $true
            return
        }
    }
    $detail = ''
    if ($r.StdErr.Count -gt 0) { $detail = ' : ' + ($r.StdErr -join ' | ') }
    throw "flowkeyd did not exit within 15 s (quit exited with code $($r.ExitCode)$detail). It may be running with a different --config path"
}

function Restore-Resident {
    if (-not $script:residentStopped) { return }
    if ($LeaveStopped) {
        Warn 'the resident instance stays stopped (-LeaveStopped)'
        return
    }
    Step 'restarting the resident instance'

    if ((Test-Native -Exe 'schtasks' -ArgList @('/Query', '/TN', 'flowkeyd')) -ne 0) {
        Warn 'the scheduled task `flowkeyd` is not registered'
        Warn "start it yourself, e.g.: Start-Process -Verb RunAs `"$distExe`""
        return
    }

    # 任务指向的 exe 与刚打包的那份不一致时提醒一下（守护进程下次启动会自己刷新）。
    $xml = (Invoke-Capture -Exe 'schtasks' -ArgList @('/Query', '/TN', 'flowkeyd', '/XML')) -join "`n"
    if ($xml -match '<Command>(.*?)</Command>') {
        $taskExe = $Matches[1].Trim()
        if (-not [string]::Equals($taskExe, $distExe, [System.StringComparison]::OrdinalIgnoreCase)) {
            Warn "the scheduled task starts `"$taskExe`" instead of the packaged $distExe"
        }
    }

    Info '> schtasks /Run /TN flowkeyd'
    if ((Test-Native -Exe 'schtasks' -ArgList @('/Run', '/TN', 'flowkeyd')) -ne 0) {
        Warn 'schtasks /Run failed (it needs administrator rights for a highest-privilege task)'
        Warn "start it yourself, e.g.: Start-Process -Verb RunAs `"$distExe`""
        return
    }
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        if ((Get-FlowkeydProcesses).Count -gt 0) {
            Info 'the resident instance is running again'
            return
        }
    }
    Warn 'the scheduled task was started but no flowkeyd process showed up yet'
}

function Assert-DistIsClean {
    Step "checking $DistDir"
    if (-not (Test-Path -LiteralPath $distExe)) {
        throw "the release build did not produce $distExe"
    }
    $junk = @(Get-ChildItem -LiteralPath $distRoot -Recurse -Force | Where-Object {
            $_.Name -like 'tst_*.exe' -or
            $_.Name -eq 'CMakeCache.txt' -or
            $_.Name -eq 'build.ninja' -or
            $_.Name -eq 'CTestTestfile.cmake' -or
            $_.Name -eq 'CMakeFiles' -or
            $_.Name -eq 'Testing' -or
            $_.Extension -eq '.a'
        })
    if ($junk.Count -gt 0) {
        $names = ($junk | Select-Object -First 5 | ForEach-Object { $_.FullName }) -join ', '
        if (-not $Force) {
            throw "the release directory contains build artifacts: $names (re-run with -Force to package it anyway)"
        }
        Warn "the release directory contains build artifacts: $names"
    }
    $files = @(Get-ChildItem -LiteralPath $distRoot -Recurse -Force -File)
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    Info ("{0} files, {1:N1} MB" -f $files.Count, ($bytes / 1MB))
}

function New-Package {
    param([string]$Version)
    Step 'staging the release package'
    $stage = Join-Path $WorkDir "flowkeyd-$Version"
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item -Path (Join-Path $distRoot '*') -Destination $stage -Recurse -Force

    $zip = Join-Path $WorkDir "flowkeyd-$Version-windows-x64.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Info "compressing (Compress-Archive -CompressionLevel $Compression); this can take a while"
    Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel $Compression

    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $shaFile = "$zip.sha256"
    $shaLine = "$hash  flowkeyd-$Version-windows-x64.zip`n"
    [System.IO.File]::WriteAllText($shaFile, $shaLine, (New-Object System.Text.UTF8Encoding($false)))
    Info ("zip:    {0} ({1:N1} MB)" -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB))
    Info "sha256: $hash"
    return @($zip, $shaFile)
}

function Publish-Release {
    param([string]$Tag, [string]$Version, [string[]]$Assets)
    Step 'uploading to GitHub Releases'
    $gh = (Get-Command gh).Source
    $remote = @(Invoke-Capture -Exe 'git' -ArgList @('ls-remote', '--tags', 'origin', "refs/tags/$Tag"))
    $tagExists = ($remote.Count -gt 0 -and $remote[0])

    if ($tagExists) {
        if (-not $Clobber) {
            throw "tag $Tag already exists on origin; delete that release on GitHub first, or re-run with -Clobber to replace its assets"
        }
        Info "tag $Tag already exists; replacing its assets (-Clobber)"
        Invoke-Live -Exe $gh -ArgList (@('release', 'upload', $Tag) + $Assets + @('--clobber'))
    } else {
        $ghArgs = @('release', 'create', $Tag) + $Assets + @('--title', "flowkeyd $Version", '--target', $script:head)
        if ($NotesFile) {
            if (-not (Test-Path -LiteralPath $NotesFile)) {
                throw "the notes file does not exist: $NotesFile"
            }
            $ghArgs += @('--notes-file', (Resolve-Path -LiteralPath $NotesFile).Path)
        } elseif ($Notes) {
            $notePath = Join-Path $WorkDir 'notes.md'
            [System.IO.File]::WriteAllText($notePath, $Notes, (New-Object System.Text.UTF8Encoding($false)))
            $ghArgs += @('--notes-file', $notePath)
        } else {
            $ghArgs += '--generate-notes'
        }
        if ($Draft) { $ghArgs += '--draft' }
        if ($Prerelease) { $ghArgs += '--prerelease' }
        Invoke-Live -Exe $gh -ArgList $ghArgs
    }

    $url = @(Invoke-Capture -Exe $gh -ArgList @('release', 'view', $Tag, '--json', 'url', '--jq', '.url'))
    if ($url.Count -gt 0 -and $url[0]) { Info "release: $($url[0])" }
}

try {
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

    Assert-Tools
    Assert-GitState

    if ($SkipBuild) {
        Info '-SkipBuild: using the existing release build (the resident instance is left alone)'
    } else {
        Step 'stopping the resident instance'
        Stop-Resident

        Step 'building'
        if ($SkipTests) {
            Info '-SkipTests: skipping the debug build and ctest'
        } else {
            if (-not (Test-Path -LiteralPath (Join-Path $here 'build\windows-debug\build.ninja'))) {
                Invoke-Live -Exe $cmake -ArgList @('--preset', 'windows-debug')
            }
            Invoke-Live -Exe $cmake -ArgList @('--build', '--preset', 'debug')
            if (-not $ctest) { throw 'ctest not found (looked next to cmake and on PATH)' }
            Invoke-Live -Exe $ctest -ArgList @('--test-dir', 'build/windows-debug', '--output-on-failure')
        }
        if (-not (Test-Path -LiteralPath (Join-Path $here 'build\windows-release\build.ninja'))) {
            Invoke-Live -Exe $cmake -ArgList @('--preset', 'windows-release')
        }
        Invoke-Live -Exe $cmake -ArgList @('--build', '--preset', 'release')
    }

    Assert-DistIsClean

    Step 'reading the build version'
    $v = Invoke-Flowkeyd -Path $distExe -ArgList @('--version')
    if ($v.StdOut.Count -eq 0) {
        throw "could not read a build version from $distExe (it exited with code $($v.ExitCode))"
    }
    $first = $v.StdOut[0]
    if ($first -notmatch '^flowkeyd\s+(\S+)\s*$') {
        throw "unexpected --version output: $first"
    }
    $script:version = $Matches[1]
    $script:tag = "v$($script:version)"
    Info "build version: $($script:version)"
    Info "release tag:   $($script:tag)"

    if ($script:version -notmatch '^\d\d-\d\d-\d\d-[0-9a-fA-F]+$') {
        Warn "the build version does not look like yy-MM-dd-<git revision>: $($script:version)"
    }
    $rev = ($script:version -split '-')[-1]
    if ($script:shortHead -and $rev -ne $script:shortHead) {
        Warn "the packaged exe was built from revision $rev but HEAD is $($script:shortHead); use -SkipBuild only if that is intentional"
    }

    $assets = New-Package -Version $script:version

    if ($SkipUpload) {
        Step 'upload skipped (-SkipUpload)'
        foreach ($asset in $assets) { Info $asset }
        Info 'install gh (scoop install gh) and run gh auth login, then re-run without -SkipUpload'
    } else {
        Publish-Release -Tag $script:tag -Version $script:version -Assets $assets
        Step 'done'
    }
} catch {
    Write-Host ''
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    $script:exitCode = 1
} finally {
    Restore-Resident
}

if ($script:exitCode -eq 0) {
    if ($SkipUpload) {
        Write-Host "   packaged flowkeyd $($script:version) (not uploaded)." -ForegroundColor Green
    } else {
        Write-Host "   released flowkeyd $($script:version) as $($script:tag)." -ForegroundColor Green
    }
}
exit $script:exitCode
