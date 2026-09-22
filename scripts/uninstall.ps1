# flowkeyd: remove the logon autostart task (and, with -RemoveFiles, the install
# directory). Thin wrapper around install.ps1 -Uninstall so both directions live
# in one place. Pure ASCII on purpose (AGENTS.md section 10).
[CmdletBinding()]
param(
    [string] $InstallDir = "$env:ProgramFiles\flowkeyd",
    [string] $TaskName = 'flowkeyd',
    [switch] $RemoveFiles,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

# Hashtable splatting is the only reliable way to forward *named* parameters
# (array splatting passes the elements as positional arguments -- it cost me a
# confusing "a positional parameter cannot be found that accepts argument
# '-TaskName'" round trip).
$splat = @{ Uninstall = $true; InstallDir = $InstallDir; TaskName = $TaskName }
if ($RemoveFiles) { $splat['RemoveFiles'] = $true }
if ($Force) { $splat['Force'] = $true }

& (Join-Path $PSScriptRoot 'install.ps1') @splat
exit $LASTEXITCODE
