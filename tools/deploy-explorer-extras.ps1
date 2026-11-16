<#
.SYNOPSIS
    Builds Explorer Extras (debug build with a log file), compiles it with Windhawk's compiler, and
    deploys it to the installed Windhawk. One command, one UAC prompt.

.DESCRIPTION
    1. python explorer-extras/build.py            (the file for the commit; --check keeps it current)
    2. python explorer-extras/build.py --debug    (explorer-extras.debug.wh.cpp: every Wh_Log line is
                                                   also appended to the log file)
    3. tools/windhawk-build/build.ps1 -Source ... (compiles x86-64 and arm64, then deploys; the deploy
                                                   asks for administrator rights once)

    The mod reloads in every Explorer process by itself. A window that was open before the reload
    keeps what Explorer built: open a new one to test code that runs when a window is created.

.PARAMETER Log
    Where the debug build writes its log. Explorer must be able to write there (a folder of the user).

.PARAMETER CompileOnly
    Build and compile, do not deploy (no administrator rights needed).
#>
param(
    [string]$Log = (Join-Path $env:LOCALAPPDATA 'ExplorerExtras\debug.log'),
    [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $repo 'explorer-extras'
$pwsh = if (Get-Command pwsh -ErrorAction SilentlyContinue) { 'pwsh' } else { 'powershell' }

New-Item -ItemType Directory -Force (Split-Path -Parent $Log) | Out-Null

Write-Host "1/3 joining the parts"
python (Join-Path $mod 'build.py')
if ($LASTEXITCODE -ne 0) { throw 'build.py failed' }

Write-Host "2/3 debug build, log: $Log"
python (Join-Path $mod 'build.py') --debug $Log
if ($LASTEXITCODE -ne 0) { throw 'build.py --debug failed' }
$source = Join-Path $mod 'explorer-extras.debug.wh.cpp'
$version = (Select-String -Path $source -Pattern '^// @version\s+(\S+)').Matches[0].Groups[1].Value
Write-Host "    version $version"

Write-Host "3/3 compile and deploy"
$args = @('-NoProfile', '-File', (Join-Path $repo 'tools\windhawk-build\build.ps1'), '-Source', $source)
if ($CompileOnly) { $args += '-CompileOnly' }
& $pwsh @args
if ($LASTEXITCODE -ne 0) { throw 'the Windhawk build or deploy failed' }
Write-Host "done: $version. Open a new Explorer window to test; the log is $Log"
