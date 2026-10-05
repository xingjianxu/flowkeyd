<#
.SYNOPSIS
    从 mozillazg/pinyin-data 的 `pinyin.txt` 生成 `src/core/pinyin_data.cpp`。

.DESCRIPTION
    程序启动器的拼音筛选需要一份「汉字 → 读音」的表。这份表是**离线生成后提交进
    仓库**的（构建过程不联网、也不依赖任何运行时组件），生成器本身只是为了让这份
    表可复现。

    数据来源（MIT License，读音数据源自 Unicode Unihan 的 kMandarin 字段）：

        https://github.com/mozillazg/pinyin-data   （pinyin.txt）

    用法（在仓库根目录，Windows PowerShell 5.1 即可）：

        # 1. 取一份数据（版本号会被写进生成文件的注释里）
        curl.exe -L -o tmp\pinyin.txt `
            https://raw.githubusercontent.com/mozillazg/pinyin-data/master/pinyin.txt
        # 2. 生成
        powershell -NoProfile -ExecutionPolicy Bypass -File tools\pinyin_gen.ps1 `
            -InputPath tmp\pinyin.txt

    处理的四件事：
      * 只保留 U+4E00–U+9FFF（Ext A/B 等生僻字不进表：开始菜单里不会出现，代价是
        表大了一倍多）；区间内没有读音的码点记 `kNoSyllable`。
      * 去掉声调（ā→a、ǚ→v……），组合音符（U+0300 之类）直接丢掉。
      * ü 记作 `v`（拼音输入法里就是这么打的），**同时**额外给出把 v 写成 u 的
        那一条（女 → `nv` 与 `nu` 都认）。
      * 多音字：第一个读音是主读音（进 `kPrimary`），其余的进按码点升序的 `kExtra`。
#>
[CmdletBinding()]
param(
    # mozillazg/pinyin-data 的 `pinyin.txt`。
    [Parameter(Mandatory = $true)][string]$InputPath,
    # 生成的文件；默认写到仓库的 `src/core/pinyin_data.cpp`（用脚本自己所在的位置推）。
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'

# `$PSScriptRoot` 在 `param()` 的默认值里是空的（PS 5.1 的坑），所以在这里补。
if ($OutputPath.Length -eq 0) {
    $OutputPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'src\core\pinyin_data.cpp'
}

$firstCodePoint = 0x4E00
$lastCodePoint = 0x9FFF

# 带声调的字母 → 去掉声调的那一个字母；`ü` 一律记作 `v`。
$toneMap = @{
    0x00E0 = 'a'; 0x00E1 = 'a'; 0x0101 = 'a'; 0x01CE = 'a'
    0x00E8 = 'e'; 0x00E9 = 'e'; 0x00EA = 'e'; 0x0113 = 'e'; 0x011B = 'e'; 0x1EBF = 'e'; 0x1EC1 = 'e'
    0x00EC = 'i'; 0x00ED = 'i'; 0x012B = 'i'; 0x01D0 = 'i'
    0x00F2 = 'o'; 0x00F3 = 'o'; 0x014D = 'o'; 0x01D2 = 'o'
    0x00F9 = 'u'; 0x00FA = 'u'; 0x016B = 'u'; 0x01D4 = 'u'
    0x00FC = 'v'; 0x01D6 = 'v'; 0x01D8 = 'v'; 0x01DA = 'v'; 0x01DC = 'v'
    0x0144 = 'n'; 0x0148 = 'n'; 0x01F9 = 'n'
    0x1E3F = 'm'
}
# 组合音符（跟在字母后面，基础字母已经给出了结果）直接丢掉。
$combiningMarks = @(0x0300, 0x0301, 0x0304, 0x0306, 0x0308, 0x030C)

function ConvertTo-Syllable {
    param([string]$Text)

    $builder = New-Object System.Text.StringBuilder
    foreach ($ch in $Text.ToCharArray()) {
        $code = [int]$ch
        if ($code -ge 0x61 -and $code -le 0x7A) {
            [void]$builder.Append($ch)
        } elseif ($toneMap.ContainsKey($code)) {
            [void]$builder.Append($toneMap[$code])
        } elseif ($combiningMarks -contains $code) {
            continue
        } else {
            # 认不出来的字符：整条读音丢掉（真机上只有极少数条目会走到这里）。
            return ''
        }
    }
    return $builder.ToString()
}

$lines = [System.IO.File]::ReadAllLines($InputPath, (New-Object System.Text.UTF8Encoding($false)))

$sourceVersion = 'unknown'
foreach ($line in $lines) {
    $versionMatch = [regex]::Match($line, '^#\s*version:\s*(\S+)')
    if ($versionMatch.Success) {
        $sourceVersion = $versionMatch.Groups[1].Value
        break
    }
}

$slotCount = $lastCodePoint - $firstCodePoint + 1
# 先记读音字符串（音节下标要等音节表排好之后才有），$null = 没有读音。
$primary = New-Object 'object[]' $slotCount
$extras = New-Object 'System.Collections.Generic.List[object]'
$syllables = New-Object 'System.Collections.Generic.HashSet[string]'

foreach ($line in $lines) {
    $match = [regex]::Match($line, '^U\+([0-9A-Fa-f]+):\s*([^#]*)#')
    if (-not $match.Success) { continue }
    $codePoint = [Convert]::ToInt32($match.Groups[1].Value, 16)
    if ($codePoint -lt $firstCodePoint -or $codePoint -gt $lastCodePoint) { continue }

    $readings = New-Object 'System.Collections.Generic.List[string]'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($raw in $match.Groups[2].Value.Split(',')) {
        $syllable = ConvertTo-Syllable $raw.Trim()
        if ($syllable.Length -eq 0) { continue }
        if ($seen.Add($syllable)) { $readings.Add($syllable) }
    }
    # ü 的另一个写法：`nv` 之外也认 `nu`（拼音输入法两种都有人打）。
    foreach ($reading in @($readings)) {
        if ($reading.Contains('v')) {
            $alternative = $reading.Replace('v', 'u')
            if ($seen.Add($alternative)) { $readings.Add($alternative) }
        }
    }
    if ($readings.Count -eq 0) { continue }

    $slot = $codePoint - $firstCodePoint
    $primary[$slot] = $readings[0]
    for ($i = 1; $i -lt $readings.Count; $i++) {
        $extras.Add(@($slot, $readings[$i]))
    }
    foreach ($reading in $readings) { [void]$syllables.Add($reading) }
}

# 音节表：按 ASCII 升序排（生成的表要确定，重新跑一遍结果逐字节相同）。
$syllableList = @($syllables) | Sort-Object
$syllableIndex = @{}
for ($i = 0; $i -lt $syllableList.Count; $i++) { $syllableIndex[$syllableList[$i]] = $i }

$out = New-Object System.Text.StringBuilder

function Write-Line {
    param([string]$Text = '')
    [void]$out.Append($Text)
    [void]$out.Append("`n")
}

Write-Line '// **本文件是生成的，不要手改。**'
Write-Line ('// 生成器：`tools/pinyin_gen.ps1`（用法写在那个脚本的注释里）。')
Write-Line '//'
Write-Line ('// 数据来源：mozillazg/pinyin-data v' + $sourceVersion + ' 的 `pinyin.txt`（MIT License）：')
Write-Line '//     https://github.com/mozillazg/pinyin-data'
Write-Line '//     Copyright (c) 2016 mozillazg'
Write-Line '//     本表是那份数据的衍生作品，按同一许可（MIT）分发；读音数据源自 Unicode'
Write-Line '//     Unihan 数据库的 kMandarin 字段（Unicode License）。'
Write-Line '//'
Write-Line '// 处理：只保留 U+4E00–U+9FFF；去声调；`ü` 记作 `v` 并额外认写成 `u` 的写法；'
Write-Line '// 多音字的第一个读音是主读音，其余按码点升序排在 kExtra 里。'
Write-Line ''
Write-Line '#include "core/pinyin_data.h"'
Write-Line ''
Write-Line 'namespace flowkeyd::core::pinyin_data {'
Write-Line ''
Write-Line ('// ' + $syllableList.Count + ' 个音节（去掉声调、去重、ASCII 升序）。')
Write-Line 'const char *const kSyllables[] = {'
$perLine = 12
$line = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt $syllableList.Count; $i++) {
    [void]$line.Append('"')
    [void]$line.Append($syllableList[$i])
    [void]$line.Append('", ')
    if ((($i + 1) % $perLine) -eq 0 -or ($i + 1) -eq $syllableList.Count) {
        Write-Line ('    ' + $line.ToString().TrimEnd())
        $line = New-Object System.Text.StringBuilder
    }
}
Write-Line '};'
Write-Line ('const int kSyllableCount = ' + $syllableList.Count + ';')
Write-Line ''
Write-Line ('// U+4E00..U+9FFF 的主读音（共 ' + $slotCount + ' 项），`kNoSyllable` = 这个码点没有读音。')
Write-Line 'const std::uint16_t kPrimary[] = {'
$perLine = 20
$line = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt $slotCount; $i++) {
    if ($null -eq $primary[$i]) {
        [void]$line.Append('kNoSyllable, ')
    } else {
        [void]$line.Append($syllableIndex[$primary[$i]].ToString())
        [void]$line.Append(', ')
    }
    if ((($i + 1) % $perLine) -eq 0 -or ($i + 1) -eq $slotCount) {
        Write-Line ('    ' + $line.ToString().TrimEnd())
        $line = New-Object System.Text.StringBuilder
    }
}
Write-Line '};'
Write-Line ('const int kPrimaryCount = ' + $slotCount + ';')
Write-Line ''
Write-Line ('// 主读音之外的读音（共 ' + $extras.Count + ' 条），按 `index` 升序；')
Write-Line '// `index` = 码点 - U+4E00，`syllable` = kSyllables 的下标。'
Write-Line 'const Extra kExtra[] = {'
$line = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt $extras.Count; $i++) {
    $slotHex = '{0:x4}' -f $extras[$i][0]
    $syllableHex = '{0:x4}' -f $syllableIndex[$extras[$i][1]]
    [void]$line.Append('{0x' + $slotHex + ', 0x' + $syllableHex + '}, ')
    if ((($i + 1) % 6) -eq 0 -or ($i + 1) -eq $extras.Count) {
        Write-Line ('    ' + $line.ToString().TrimEnd())
        $line = New-Object System.Text.StringBuilder
    }
}
Write-Line '};'
Write-Line ('const int kExtraCount = ' + $extras.Count + ';')
Write-Line ''
Write-Line '} // namespace flowkeyd::core::pinyin_data'

[System.IO.File]::WriteAllText($OutputPath, $out.ToString(), (New-Object System.Text.UTF8Encoding($false)))

Write-Host ('wrote ' + $OutputPath)
Write-Host ('  source version : ' + $sourceVersion)
Write-Host ('  syllables      : ' + $syllableList.Count)
Write-Host ('  primary slots  : ' + $slotCount + ' (' + @($primary | Where-Object { $null -ne $_ }).Count + ' with readings)')
Write-Host ('  extra readings : ' + $extras.Count)
