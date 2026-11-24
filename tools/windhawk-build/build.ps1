<#
.SYNOPSIS
    Compile a Windhawk mod with Windhawk's bundled clang and deploy it to the
    installed Windhawk (1.7.x) without touching the GUI.

.DESCRIPTION
    1. Reads the ==WindhawkMod== metadata block (id, version, include/exclude,
       architecture, compilerOptions) from the .wh.cpp file.
    2. Compiles one DLL per architecture, exactly like Windhawk's editor does.
       Compiler errors are printed straight to this terminal.
    3. Copies the DLLs to <AppData>\Engine\Mods\<arch>\ under a fresh file name
       and points the mod's registry key (LibraryFileName) at it. The engine in
       every injected process watches that key and hot-reloads the mod - this
       is the same thing "Compile Mod" in the editor does. A mod with no
       saved settings yet gets the defaults from ==WindhawkModSettings==,
       like installing it from Windhawk's UI does.
    4. Copies the source to <AppData>\ModsSource so the editor shows it too.

    Deploying writes to HKLM and ProgramData, so it needs admin rights. If the
    terminal isn't elevated, the deploy step re-launches itself elevated (one
    UAC prompt). Run from an elevated terminal to skip the prompt, or run
    -InstallDeployTask once: it registers a scheduled task that deploys
    elevated, and later runs use it instead of prompting.

    The task runs a copy of this script kept in
    %ProgramData%\WindhawkModDeploy, writable only by administrators; run
    -InstallDeployTask again after changing the deploy code here.
    -UninstallDeployTask removes it.

    The Windhawk install folder is read from WindhawkPath= in build.ini next
    to this script (see README.md in this folder).

.EXAMPLE
    build.bat start-menu-groups                  # compile + deploy (from the repo root)
    .\build\build.ps1 start-menu-groups          # same
    .\build\build.ps1 start-menu-groups -CompileOnly   # just check that it compiles
    .\build\build.ps1 -InstallDeployTask         # once: deploy without UAC prompts
#>
[CmdletBinding()]
param(
    # Mod folder name in the repo root: "start-menu-groups" builds
    # <repo>\start-menu-groups\start-menu-groups.wh.cpp.
    [Parameter(Position = 0)]
    [string]$Mod,
    # Mod source file; overrides -Mod.
    [string]$Source,
    # Windhawk install folder (where windhawk.exe and windhawk.ini live).
    [string]$WindhawkPath,
    # Registry/mod ID. Defaults to "local@<@id>", which is what the editor uses.
    [string]$ModId,
    [switch]$CompileOnly,
    # Build (and deploy) only these architectures, e.g. -OnlyArch arm64 on a Windows on ARM test machine: half the compile time.
    # The deployed mod then lists only them. For test builds, not for what is committed.
    [string[]]$OnlyArch,
    # Compile without the cached precompiled header (slower; for comparison
    # or if the header ever causes trouble).
    [switch]$NoPch,
    # Optimize like Windhawk's own compile (-O2). Without it, builds use -O0,
    # which compiles about twice as fast; fine for testing.
    [switch]$Release,
    # Print the default settings that a new mod entry would get, and exit.
    [switch]$ShowDefaults,
    # Register (or update) the elevated deploy task; one UAC prompt.
    [switch]$InstallDeployTask,
    [switch]$UninstallDeployTask,
    # Internal: used when the script re-launches itself elevated.
    [string]$DeployFrom,
    [string]$LogFile,
    [string]$InstallForUser,
    [switch]$RunDeployRequest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

# --- Configuration ----------------------------------------------------------

# Build settings (WindhawkPath, ...). See build.ini for the format.
$BuildIniFile = Join-Path $PSScriptRoot 'build.ini'

# Repo root: the folder that holds one sub-folder per mod.
$RepoRoot = Split-Path $PSScriptRoot -Parent

# Build output (per mod) and precompiled header cache.
$BuildTempRoot = Join-Path $env:TEMP 'windhawk-build'
$PchCacheRoot = Join-Path $env:LOCALAPPDATA 'windhawk-build\pch'

# Elevated deploy task (see -InstallDeployTask).
$DeployTaskName = 'WindhawkModDeploy'
$DeployTaskDir = Join-Path $env:ProgramData 'WindhawkModDeploy'
$DeployTaskScript = Join-Path $DeployTaskDir 'build.ps1'
$DeployRequestFile = Join-Path $env:LOCALAPPDATA 'WindhawkModDeploy\request.json'

# --- Locate Windhawk --------------------------------------------------------

# Windhawk install folder: -WindhawkPath, then $env:WINDHAWK_PATH, then
# WindhawkPath= in build.ini, then Program Files and the uninstall entry.
function Find-WindhawkPath {
    $fromIni = (Read-Ini $BuildIniFile)['WindhawkPath']
    if ($fromIni) { $fromIni = [Environment]::ExpandEnvironmentVariables($fromIni.Trim('"')) }
    $candidates = @(
        $WindhawkPath,
        $env:WINDHAWK_PATH,
        $fromIni,
        (Join-Path $env:ProgramFiles 'Windhawk')
    )
    $uninstall = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Windhawk'
    if (Test-Path $uninstall) {
        $candidates += (Get-ItemProperty $uninstall).InstallLocation
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'windhawk.exe'))) {
            return (Resolve-Path $c).Path
        }
    }
    throw "Windhawk not found. Set WindhawkPath= in $BuildIniFile, pass -WindhawkPath or set `$env:WINDHAWK_PATH."
}

function Read-Ini([string]$path) {
    $values = @{}
    if (Test-Path $path) {
        foreach ($line in Get-Content $path) {
            if ($line -match '^\s*([^;#\[][^=]*?)\s*=\s*(.*?)\s*$') {
                $values[$Matches[1]] = $Matches[2]
            }
        }
    }
    return $values
}

function Get-WindhawkConfig {
    $root = Find-WindhawkPath
    $ini = Read-Ini (Join-Path $root 'windhawk.ini')

    if ($ini['Portable'] -eq '1') {
        throw 'Portable Windhawk keeps mod config in .ini files; this script only supports the installed version.'
    }

    function Resolve-Rel([string]$p) {
        $p = [Environment]::ExpandEnvironmentVariables($p)
        if ([IO.Path]::IsPathRooted($p)) { return $p }
        return Join-Path $root $p
    }

    $appData = if ($ini['AppDataPath']) { Resolve-Rel $ini['AppDataPath'] } else { Join-Path $env:ProgramData 'Windhawk' }

    $engine = $null
    if ($ini['EnginePath']) { $engine = Resolve-Rel $ini['EnginePath'] }
    if (-not $engine -or -not (Test-Path $engine)) {
        # Engine\<version>; pick the newest one.
        $engine = Get-ChildItem (Join-Path $root 'Engine') -Directory |
            Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } |
            Select-Object -Last 1 -ExpandProperty FullName
    }

    $compiler = if ($ini['CompilerPath']) { Resolve-Rel $ini['CompilerPath'] } else { Join-Path $root 'Compiler' }

    $regKey = if ($ini['RegistryKey']) { $ini['RegistryKey'] } else { 'HKEY_LOCAL_MACHINE\SOFTWARE\Windhawk' }

    $cfg = [pscustomobject]@{
        Root     = $root
        AppData  = $appData
        Engine   = $engine
        Compiler = $compiler
        ModsKey  = "Registry::$regKey\Engine\Mods"
    }
    foreach ($p in 'AppData', 'Engine', 'Compiler') {
        if (-not (Test-Path $cfg.$p)) { throw "Windhawk $p folder not found: $($cfg.$p)" }
    }
    return $cfg
}

# --- Mod metadata -----------------------------------------------------------

function Read-ModMetadata([string]$path) {
    $meta = @{ include = @(); exclude = @(); architecture = @() }
    $inBlock = $false
    foreach ($line in Get-Content $path) {
        if ($line -match '^\s*//\s*==WindhawkMod==') { $inBlock = $true; continue }
        if ($line -match '^\s*//\s*==/WindhawkMod==') { break }
        if (-not $inBlock) { continue }
        # Localized keys (@name:zh-CN) don't match: no space after the key.
        if ($line -match '^\s*//\s*@(\w+)\s+(.*?)\s*$') {
            $key = $Matches[1]; $value = $Matches[2]
            if ($meta.ContainsKey($key) -and $meta[$key] -is [array]) {
                $meta[$key] += $value
            } else {
                $meta[$key] = $value
            }
        }
    }
    if (-not $meta['id']) { throw "No // @id found in $path" }
    if (-not $meta['version']) { $meta['version'] = '1.0.0' }
    if ($meta.architecture.Count -eq 0) { $meta.architecture = @('x86', 'x86-64') }
    # Local change: on Windows on ARM, Windhawk's editor also builds an arm64 DLL for a mod that
    # lists x86-64 (Explorer runs as arm64 there). Without it the deployed mod would never load.
    if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64' -and
        $meta.architecture -contains 'x86-64' -and $meta.architecture -notcontains 'arm64') {
        $meta.architecture = @($meta.architecture) + 'arm64'
    }
    if ($OnlyArch) {
        $meta.architecture = @($meta.architecture | Where-Object { $OnlyArch -contains $_ })
        if ($meta.architecture.Count -eq 0) { throw "-OnlyArch $($OnlyArch -join ',') matches none of the mod's architectures" }
    }
    return $meta
}

# --- Default settings -------------------------------------------------------
# Windhawk's UI writes a mod's default settings to the registry when the mod
# is installed; a mod entry created by this script would otherwise have none
# (every Wh_Get*Setting returns 0 / ""). This parses the YAML subset used in
# ==WindhawkModSettings== blocks and flattens it the way Windhawk stores it:
# section.key, array[i].key, list[i]. `$`-keys ($name, $options...) are
# skipped.

function Read-SettingsYaml([string]$path) {
    $text = Get-Content $path -Raw -Encoding UTF8
    $m = [regex]::Match($text,
        '//\s*==WindhawkModSettings==\s*/\*(.*?)\*/\s*//\s*==/WindhawkModSettings==',
        'Singleline')
    if (-not $m.Success) { return $null }
    $lines = [Collections.Generic.List[object]]::new()
    foreach ($raw in ($m.Groups[1].Value -split "`r?`n")) {
        if ($raw.Trim() -eq '' -or $raw.TrimStart().StartsWith('#')) { continue }
        $content = $raw.TrimStart()
        $lines.Add([pscustomobject]@{ Indent = $raw.Length - $content.Length; Text = $content.TrimEnd() })
    }
    $script:yamlLines = $lines
    $script:yamlPos = 0
    if ($lines.Count -eq 0) { return $null }
    return Read-YamlBlock $lines[0].Indent
}

function ConvertFrom-YamlScalar([string]$s) {
    $s = $s.Trim()
    if ($s -match '^"(.*)"$') { return [pscustomobject]@{ Str = ($Matches[1] -replace '\\"', '"') } }
    if ($s -match "^'(.*)'$") { return [pscustomobject]@{ Str = ($Matches[1] -replace "''", "'") } }
    if ($s -eq 'true') { return 1 }
    if ($s -eq 'false') { return 0 }
    if ($s -match '^-?\d+$') { return [int]$s }
    return [pscustomobject]@{ Str = $s }
}

function ConvertFrom-YamlFlowList([string]$s) {
    $inner = $s.Trim().TrimStart('[').TrimEnd(']').Trim()
    $items = [Collections.Generic.List[object]]::new()
    if ($inner -eq '') { return , $items }
    foreach ($part in [regex]::Matches($inner, '"[^"]*"|''[^'']*''|[^,]+')) {
        if ($part.Value.Trim() -ne '') { $items.Add((ConvertFrom-YamlScalar $part.Value)) }
    }
    return , $items
}

# Parses the block starting at the current line with the given indent: a
# sequence ("- ...") or a mapping ("key: value").
function Read-YamlBlock([int]$indent) {
    $lines = $script:yamlLines
    $first = $lines[$script:yamlPos]
    if ($first.Text -eq '-' -or $first.Text.StartsWith('- ')) {
        $list = [Collections.Generic.List[object]]::new()
        while ($script:yamlPos -lt $lines.Count) {
            $l = $lines[$script:yamlPos]
            if ($l.Indent -ne $indent -or -not ($l.Text -eq '-' -or $l.Text.StartsWith('- '))) { break }
            $rest = if ($l.Text -eq '-') { '' } else { $l.Text.Substring(2).TrimStart() }
            if ($rest -ne '' -and $rest -notmatch '^(- |-$)' -and $rest -notmatch '^[^"''\[][^:]*:(\s|$)') {
                # A plain item: "- value".
                $list.Add((ConvertFrom-YamlScalar $rest))
                $script:yamlPos++
                continue
            }
            # "- key: v" / "- - ...": re-read the rest as a block indented
            # past the dash, together with the lines that follow it.
            $itemIndent = $indent + ($l.Text.Length - $rest.Length)
            $lines[$script:yamlPos] = [pscustomobject]@{ Indent = $itemIndent; Text = $rest }
            $list.Add((Read-YamlBlock $itemIndent))
        }
        return , $list
    }

    $map = [ordered]@{}
    while ($script:yamlPos -lt $lines.Count) {
        $l = $lines[$script:yamlPos]
        if ($l.Indent -ne $indent -or $l.Text.StartsWith('- ')) { break }
        if ($l.Text -notmatch '^([^:]+?):(?:\s+(.*))?$') { throw "Can't parse settings line: $($l.Text)" }
        $key = $Matches[1].Trim('"', "'")
        $value = if ($Matches.Count -gt 2) { $Matches[2] } else { $null }
        $script:yamlPos++
        if ($value -and $value -match '^[>|]') {
            # Block scalar: the more-indented lines that follow.
            $parts = @()
            while ($script:yamlPos -lt $lines.Count -and $lines[$script:yamlPos].Indent -gt $indent) {
                $parts += $lines[$script:yamlPos].Text
                $script:yamlPos++
            }
            $map[$key] = [pscustomobject]@{ Str = ($parts -join ' ') }
        } elseif ($value -and $value.StartsWith('[')) {
            $map[$key] = ConvertFrom-YamlFlowList $value
        } elseif ($value) {
            $map[$key] = ConvertFrom-YamlScalar $value
        } elseif ($script:yamlPos -lt $lines.Count -and
                  ($lines[$script:yamlPos].Indent -gt $indent -or
                   ($lines[$script:yamlPos].Indent -eq $indent -and $lines[$script:yamlPos].Text.StartsWith('- ')))) {
            $map[$key] = Read-YamlBlock $lines[$script:yamlPos].Indent
        } else {
            $map[$key] = [pscustomobject]@{ Str = '' }
        }
    }
    return $map
}

# A settings list is a sequence of one-key mappings (plus $-keys).
function Add-FlatSettings($list, [string]$prefix, $out) {
    foreach ($entry in $list) {
        if ($entry -isnot [Collections.Specialized.OrderedDictionary]) { continue }
        foreach ($key in @($entry.Keys)) {
            if ($key.StartsWith('$')) { continue }
            $name = $prefix + $key
            $v = $entry[$key]
            if ($v -is [Collections.Generic.List[object]]) {
                if ($v.Count -eq 0) { continue }
                if ($v[0] -is [Collections.Generic.List[object]]) {
                    for ($i = 0; $i -lt $v.Count; $i++) { Add-FlatSettings $v[$i] "$name[$i]." $out }
                } elseif ($v[0] -is [Collections.Specialized.OrderedDictionary]) {
                    Add-FlatSettings $v "$name." $out
                } else {
                    for ($i = 0; $i -lt $v.Count; $i++) { $out["$name[$i]"] = $v[$i] }
                }
            } else {
                $out[$name] = $v
            }
        }
    }
}

function Get-DefaultSettings([string]$path) {
    $root = Read-SettingsYaml $path
    $out = [ordered]@{}
    if ($root) { Add-FlatSettings $root '' $out }
    return $out
}

$Arches = @{
    'x86'    = @{ Triple = 'i686-w64-mingw32';    Dir = '32' }
    'x86-64' = @{ Triple = 'x86_64-w64-mingw32';  Dir = '64' }
    'arm64'  = @{ Triple = 'aarch64-w64-mingw32'; Dir = 'arm64' }
}

# --- Compile ----------------------------------------------------------------

# Runs clang with its arguments in a response file: passing L"..." directly
# loses the inner quotes in Windows PowerShell 5.1 (and some pwsh argument
# passing modes), which turns WH_MOD_ID into a bare identifier.
function Invoke-Clang([string]$clang, [string[]]$clangArgs, [string]$rsp) {
    $quoted = foreach ($arg in $clangArgs) {
        '"' + ([string]$arg).Replace('\', '\\').Replace('"', '\"') + '"'
    }
    [IO.File]::WriteAllLines($rsp, [string[]]$quoted, [Text.UTF8Encoding]::new($false))
    # clang-NN.exe (rather than the clang++.exe wrapper) needs the C++ driver
    # mode spelled out.
    # (@() around the if: a one-element result would otherwise unwrap to a
    # string, and splatting a string passes it character by character.)
    $driver = @(if ((Split-Path -Leaf $clang) -notlike 'clang++*') { '--driver-mode=g++' })
    # Windhawk runs clang from the compiler folder; do the same.
    Push-Location (Split-Path (Split-Path $clang))
    try {
        & $clang @driver '--rsp-quoting=posix' "@$rsp" | Out-Host
        return $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

# Precompiled header: windhawk_api.h plus the mod's own system/library
# includes (the #include block after the settings), which is where nearly
# all of the compile time goes (C++/WinRT XAML headers). The mod's #include
# lines then hit include guards. Cached by content and flags, so it's only
# rebuilt when the includes, the flags or the compiler change.
# windhawk_utils.h stays out: it depends on WH_MOD_ID and is cheap.
function Get-Pch([string]$clang, [string[]]$commonArgs, [string]$src, [string]$archDir) {
    $lines = Get-Content $src -Encoding UTF8
    $start = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*//\s*==/WindhawkModSettings==') { $start = $i + 1; break }
    }
    $header = @('#include <windhawk_api.h>')
    for ($i = $start; $i -lt $lines.Count; $i++) {
        $l = $lines[$i].Trim()
        if ($l -eq '' -or $l.StartsWith('//')) { continue }
        if ($l -match '^#\s*(include|undef|define|pragma)\b') {
            if ($l -notmatch 'windhawk_utils\.h') { $header += $l }
            continue
        }
        break
    }
    if ($header.Count -le 1) { return $null }

    $clangInfo = Get-Item $clang
    # 'pch3': bump when the way the header is built changes.
    $keySource = (@('pch3') +$header + $commonArgs + @($clangInfo.FullName, $clangInfo.LastWriteTimeUtc.Ticks)) -join "`n"
    $hash = [BitConverter]::ToString(
        [Security.Cryptography.SHA256]::Create().ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($keySource))).Replace('-', '').Substring(0, 16)
    $pchDir = Join-Path $PchCacheRoot $archDir
    New-Item -ItemType Directory -Force $pchDir | Out-Null
    $pch = Join-Path $pchDir "$hash.pch"
    if (Test-Path $pch) { return $pch }
    # Failed before with these exact includes and flags: don't spend the
    # time again on every build.
    $failed = Join-Path $pchDir "$hash.failed"
    if (Test-Path $failed) { return $null }

    Write-Host "  building precompiled header (once per include/flag change)..." -ForegroundColor DarkGray
    $hdr = Join-Path $pchDir "$hash.h"
    [IO.File]::WriteAllLines($hdr, [string[]]$header, [Text.UTF8Encoding]::new($false))
    $tmp = "$pch.tmp"
    # Needs the large-address-aware clang (Get-LaaClang): the stock 32-bit
    # one runs out of its 2 GB writing this header.
    # (-fpch-instantiate-templates was measured: ~3% faster, not worth it.)
    $code =Invoke-Clang $clang ($commonArgs + @(
            '-x', 'c++-header', $hdr, '-o', $tmp)) (Join-Path $pchDir "$hash.rsp")
    if ($code -ne 0 -or -not (Test-Path $tmp)) {
        Write-Host "  precompiled header failed; compiling without it (not retried until the includes or flags change)." -ForegroundColor Yellow
        Remove-Item $tmp -ErrorAction SilentlyContinue
        New-Item -ItemType File -Force $failed | Out-Null
        return $null
    }
    Move-Item $tmp $pch -Force
    # Headers of other include sets / flags that haven't been used for a
    # week (each .pch is ~200 MB). The dev and -Release ones stay side by
    # side, so switching between them doesn't rebuild.
    (Get-Item $pch).LastWriteTime = Get-Date
    Get-ChildItem $pchDir -File |
        Where-Object { $_.BaseName -notlike "$hash*" -and $_.LastWriteTime -lt (Get-Date).AddDays(-7) } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    return $pch
}

# Windhawk's clang is a 32-bit program without the large-address-aware flag,
# so Windows caps it at 2 GB - too little to write the precompiled header.
# A copy with the flag set (4 GB on 64-bit Windows) is kept next to the
# original, since clang finds its headers and config relative to itself.
# Windhawk's own files aren't touched; the copy is refreshed when Windhawk
# updates its compiler. Returns $null if it can't be made.
function Get-LaaClang($cfg) {
    $bin = Join-Path $cfg.Compiler 'bin'
    $orig = Get-ChildItem $bin -Filter 'clang-*.exe' |
        Where-Object { $_.Name -match '^clang-\d+\.exe$' } | Select-Object -First 1
    if (-not $orig) { return $null }
    $laa = Join-Path $bin ($orig.BaseName + '-laa.exe')
    if ((Test-Path $laa) -and (Get-Item $laa).LastWriteTimeUtc -eq $orig.LastWriteTimeUtc) {
        return $laa
    }
    try {
        $bytes = [IO.File]::ReadAllBytes($orig.FullName)
        $pe = [BitConverter]::ToInt32($bytes, 0x3c)
        $characteristics = [BitConverter]::ToUInt16($bytes, $pe + 22)
        $characteristics = $characteristics -bor 0x20  # IMAGE_FILE_LARGE_ADDRESS_AWARE
        [BitConverter]::GetBytes([uint16]$characteristics).CopyTo($bytes, $pe + 22)
        [IO.File]::WriteAllBytes($laa, $bytes)
        (Get-Item $laa).LastWriteTimeUtc = $orig.LastWriteTimeUtc
        return $laa
    } catch {
        Write-Host "  couldn't create $laa ($_); compiling without a precompiled header." -ForegroundColor Yellow
        return $null
    }
}

function Invoke-Compile($cfg, $meta, [string]$src, [string]$modId, [string]$outDir) {
    $clang = Join-Path $cfg.Compiler 'bin\clang++.exe'
    if (-not (Test-Path $clang)) { throw "clang++ not found: $clang" }
    $usePch = -not $NoPch
    if ($usePch) {
        $laa = Get-LaaClang $cfg
        if ($laa) { $clang = $laa } else { $usePch = $false }
    }
    # -O2 like Windhawk's own compile for -Release; -O0 otherwise, which
    # compiles about twice as fast (measured 8 s vs 20 s with the header).
    $opt = if ($Release) { '-O2' } else { '-O0' }

    $extra = @()
    if ($meta['compilerOptions']) {
        $extra = [regex]::Matches($meta['compilerOptions'], '"[^"]*"|\S+') |
            ForEach-Object { $_.Value.Trim('"') }
    }

    foreach ($arch in $meta.architecture) {
        if (-not $Arches.ContainsKey($arch)) { throw "Unknown @architecture '$arch'" }
        $a = $Arches[$arch]
        $lib = Join-Path $cfg.Engine "$($a.Dir)\windhawk.lib"
        if (-not (Test-Path $lib)) { throw "windhawk.lib not found: $lib" }

        $dllDir = Join-Path $outDir $a.Dir
        New-Item -ItemType Directory -Force $dllDir | Out-Null
        $dll = Join-Path $dllDir 'mod.dll'

        $commonArgs = @(
            '-std=c++23', $opt,
            '-target', $a.Triple,
            '-DUNICODE', '-D_UNICODE',
            '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00',
            '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0',
            '-DWH_MOD',
            "-DWH_MOD_ID=L`"$modId`"",
            "-DWH_MOD_VERSION=L`"$($meta['version'])`"",
            '-I', (Join-Path $cfg.Compiler 'include'),
            '-fdiagnostics-color=always', '-fansi-escape-codes'
        )
        $pch = if ($usePch) { Get-Pch $clang $commonArgs $src $a.Dir } else { $null }
        $prefix = if ($pch) { @('-include-pch', $pch) } else { @('-include', 'windhawk_api.h') }
        $clangArgs = $commonArgs + $prefix + @(
            '-shared',
            # Required: windhawk_api.h doesn't dllexport Wh_ModInit & co, and
            # without exports the engine loads the DLL but never calls into it
            # (Windhawk's own compile passes this too).
            '-Wl,--export-all-symbols',
            '-o', $dll,
            '-x', 'c++', $src,
            '-x', 'none', $lib
        ) + $extra

        $mode = if ($Release) { 'release, -O2' } else { 'dev, -O0; -Release for -O2' }
        Write-Host "Compiling $arch ($mode)..." -ForegroundColor Cyan
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $code = Invoke-Clang $clang $clangArgs (Join-Path $outDir "clang-$($a.Dir).rsp")
        if ($code -ne 0) {
            Write-Host "Compilation failed ($arch)." -ForegroundColor Red
            exit 1
        }
        Write-Host ("  ok ({0:n1}s)" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
    }
}

# --- Deploy (needs admin) ---------------------------------------------------

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

# The elevated deploy runs in a hidden window, so it also writes to a log
# that the non-elevated caller prints afterwards.
function Say([string]$text, [string]$color = 'Gray') {
    Write-Host $text -ForegroundColor $color
    if ($LogFile) { Add-Content -Path $LogFile -Value "$color`t$text" }
}

function Invoke-Deploy($cfg, $meta, [string]$src, [string]$modId, [string]$outDir) {
    $fileName = '{0}_{1}_{2}.dll' -f $modId, $meta['version'], (Get-Random -Minimum 100000 -Maximum 999999)

    foreach ($arch in $meta.architecture) {
        $dir = $Arches[$arch].Dir
        $modsDir = Join-Path $cfg.AppData "Engine\Mods\$dir"
        New-Item -ItemType Directory -Force $modsDir | Out-Null
        Copy-Item (Join-Path $outDir "$dir\mod.dll") (Join-Path $modsDir $fileName) -Force
    }

    $key = Join-Path $cfg.ModsKey $modId
    $isNew = -not (Test-Path $key)
    if ($isNew) {
        New-Item -Path $key -Force | Out-Null
        New-ItemProperty $key -Name Disabled -PropertyType DWord -Value 0 | Out-Null
        New-ItemProperty $key -Name LoggingEnabled -PropertyType DWord -Value 0 | Out-Null
        New-ItemProperty $key -Name DebugLoggingEnabled -PropertyType DWord -Value 0 | Out-Null
    }
    Set-ItemProperty $key -Name Version -Value $meta['version'] -Type String
    Set-ItemProperty $key -Name Include -Value ($meta.include -join '|') -Type String
    Set-ItemProperty $key -Name Exclude -Value ($meta.exclude -join '|') -Type String
    Set-ItemProperty $key -Name Architecture -Value ($meta.architecture -join '|') -Type String

    # Default settings, as Windhawk's UI writes them on install and on update: the ones the mod has
    # no value for yet (a new mod, or settings that were added or renamed). Saved values are never touched.
    $settingsKey = Join-Path $key 'Settings'
    $defaults = Get-DefaultSettings $src
    if (-not (Test-Path $settingsKey)) { New-Item -Path $settingsKey -Force | Out-Null }
    $have = @((Get-Item $settingsKey).GetValueNames())
    $added = 0
    foreach ($name in $defaults.Keys) {
        if ($have -contains $name) { continue }
        $v = $defaults[$name]
        if ($v -is [int]) {
            New-ItemProperty $settingsKey -Name $name -PropertyType DWord -Value $v -Force | Out-Null
        } else {
            New-ItemProperty $settingsKey -Name $name -PropertyType String -Value $v.Str -Force | Out-Null
        }
        $added++
    }
    if ($added -gt 0) { Say "  wrote $added default settings (the ones that were missing)" Yellow }

    # Written last: this is the value the engine reacts to.
    Set-ItemProperty $key -Name LibraryFileName -Value $fileName -Type String

    # Keep the editor in sync.
    $sourceDir = Join-Path $cfg.AppData 'ModsSource'
    if (Test-Path $sourceDir) {
        Copy-Item $src (Join-Path $sourceDir "$modId.wh.cpp") -Force
    }

    Say "Deployed $modId $($meta['version']) -> $fileName" Green
    if ($isNew) { Say '  (new mod entry created; restart Windhawk if it is not listed)' Yellow }

    # Remove older builds of this mod. Ones still loaded in a process are
    # locked; those are skipped and cleaned up on a later run.
    Start-Sleep -Milliseconds 1500
    foreach ($arch in $meta.architecture) {
        $modsDir = Join-Path $cfg.AppData "Engine\Mods\$($Arches[$arch].Dir)"
        Get-ChildItem $modsDir -Filter "${modId}_*.dll" |
            Where-Object { $_.Name -ne $fileName } |
            ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
    }
}

# --- Deploy task (deploy without UAC prompts) --------------------------------
# A scheduled task that runs elevated as the current user. It runs a copy of
# this script in a folder only administrators can write to, so it can't be
# used to run other code elevated. The non-elevated build writes what to
# deploy to a request file and starts the task.

function Get-DeployTask {
    Get-ScheduledTask -TaskName $DeployTaskName -ErrorAction SilentlyContinue
}

function Install-DeployTask([string]$user) {
    New-Item -ItemType Directory -Force $DeployTaskDir | Out-Null
    # Administrators and SYSTEM: full control; users: read only.
    & icacls $DeployTaskDir /inheritance:r /grant:r '*S-1-5-32-544:(OI)(CI)F' `
        '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    Copy-Item $PSCommandPath $DeployTaskScript -Force
    $shell = (Get-Process -Id $PID).Path
    $action = New-ScheduledTaskAction -Execute $shell -Argument (
        '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden ' +
        "-File `"$DeployTaskScript`" -RunDeployRequest")
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive `
        -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew `
        -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
    Register-ScheduledTask -TaskName $DeployTaskName -Action $action `
        -Principal $principal -Settings $settings -Force `
        -Description 'Deploys locally built Windhawk mods (build.ps1).' | Out-Null
    Write-Host "Deploy task installed for $user." -ForegroundColor Green
}

# Deploys through the task. Returns the exit code, or $null without a task.
function Invoke-DeployTask([string]$outDir, [string]$log) {
    $task = Get-DeployTask
    if (-not $task) { return $null }
    if ((Test-Path $DeployTaskScript) -and
        (Get-FileHash $DeployTaskScript).Hash -ne (Get-FileHash $PSCommandPath).Hash) {
        Write-Host '  (the deploy task uses an older build.ps1; run -InstallDeployTask to update it)' -ForegroundColor Yellow
    }
    New-Item -ItemType Directory -Force (Split-Path $DeployRequestFile) | Out-Null
    @{
        Source = $Source; ModId = $ModId; WindhawkPath = $cfg.Root
        DeployFrom = $outDir; LogFile = $log
    } | ConvertTo-Json | Set-Content $DeployRequestFile -Encoding UTF8
    Write-Host 'Deploying (deploy task)...' -ForegroundColor Cyan
    $started = (Get-Date).AddSeconds(-1)
    Start-ScheduledTask -TaskName $DeployTaskName
    $deadline = (Get-Date).AddMinutes(2)
    # 267009 (0x41301, SCHED_S_TASK_RUNNING) is the result while it still runs.
    do {
        Start-Sleep -Milliseconds 300
        $state = (Get-DeployTask).State
        $info = Get-ScheduledTaskInfo -TaskName $DeployTaskName
    } while ((Get-Date) -lt $deadline -and
             ($state -eq 'Running' -or $info.LastRunTime -lt $started -or
              $info.LastTaskResult -eq 267009))
    if (Test-Path $log) {
        foreach ($line in Get-Content $log) {
            $color, $text = $line -split "`t", 2
            Write-Host $text -ForegroundColor $color
        }
    }
    if ($state -eq 'Running') {
        Write-Host 'Deploy task timed out.' -ForegroundColor Red
        return 1
    }
    return [int]$info.LastTaskResult
}

# --- Main -------------------------------------------------------------------

if ($InstallDeployTask -or $UninstallDeployTask) {
    if (-not (Test-Admin)) {
        $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
        $shell = (Get-Process -Id $PID).Path
        $mode = if ($InstallDeployTask) { '-InstallDeployTask' } else { '-UninstallDeployTask' }
        $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                     "`"$PSCommandPath`"", '-InstallForUser', "`"$user`"", $mode)
        $p = Start-Process $shell -Verb RunAs -Wait -PassThru -ArgumentList $argList
        if ($p.ExitCode -eq 0) {
            $done = if ($InstallDeployTask) { 'Deploy task installed.' } else { 'Deploy task removed.' }
            Write-Host $done -ForegroundColor Green
        }
        exit $p.ExitCode
    }
    if ($UninstallDeployTask) {
        Unregister-ScheduledTask -TaskName $DeployTaskName -Confirm:$false -ErrorAction SilentlyContinue
        Remove-Item $DeployTaskDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host 'Deploy task removed.' -ForegroundColor Green
        exit 0
    }
    $user = if ($InstallForUser) { $InstallForUser } else {
        [Security.Principal.WindowsIdentity]::GetCurrent().Name
    }
    Install-DeployTask $user
    exit 0
}

# Started by the deploy task: deploy what the request file names.
if ($RunDeployRequest) {
    $req = Get-Content $DeployRequestFile -Raw | ConvertFrom-Json
    Remove-Item $DeployRequestFile -Force -ErrorAction SilentlyContinue
    $buildRoot = [IO.Path]::GetFullPath($BuildTempRoot)
    $deployFull = [IO.Path]::GetFullPath([string]$req.DeployFrom)
    if (-not $deployFull.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not ([string]$req.Source).EndsWith('.wh.cpp')) {
        throw 'Invalid deploy request.'
    }
    $Source = [string]$req.Source
    $ModId = [string]$req.ModId
    $WindhawkPath = [string]$req.WindhawkPath
    $DeployFrom = $deployFull
    $LogFile = [string]$req.LogFile
}

function Get-ModNames {
    Get-ChildItem $RepoRoot -Directory |
        Where-Object { Get-ChildItem $_.FullName -Filter '*.wh.cpp' -File } |
        ForEach-Object Name
}

if (-not $Source) {
    if (-not $Mod) {
        Write-Host 'Usage: build.ps1 <mod-folder> [-CompileOnly] [-Release] ...' -ForegroundColor Yellow
        Write-Host "Mods in ${RepoRoot}:"
        Get-ModNames | ForEach-Object { Write-Host "  $_" }
        exit 1
    }
    # Accept tab-completed paths too (".\start-menu-groups\").
    $modName = Split-Path -Leaf ($Mod.TrimEnd('\', '/'))
    $modDir = Join-Path $RepoRoot $modName
    if (-not (Test-Path $modDir -PathType Container)) {
        Write-Host "Mod folder not found: $modDir" -ForegroundColor Red
        Write-Host 'Available mods:'
        Get-ModNames | ForEach-Object { Write-Host "  $_" }
        exit 1
    }
    $Source = Join-Path $modDir "$modName.wh.cpp"
    if (-not (Test-Path $Source)) {
        # Fall back to the folder's only *.wh.cpp.
        $found = @(Get-ChildItem $modDir -Filter '*.wh.cpp' -File)
        if ($found.Count -ne 1) { throw "Expected $modName.wh.cpp (or a single *.wh.cpp) in $modDir; pass -Source." }
        $Source = $found[0].FullName
    }
}
$Source = (Resolve-Path $Source).Path

$cfg = Get-WindhawkConfig
$meta = Read-ModMetadata $Source
if (-not $ModId) { $ModId = "local@$($meta['id'])" }

if ($ShowDefaults) {
    $defaults = Get-DefaultSettings $Source
    foreach ($name in $defaults.Keys) {
        $v = $defaults[$name]
        if ($v -is [int]) { '{0} = {1} (DWORD)' -f $name, $v } else { '{0} = "{1}"' -f $name, $v.Str }
    }
    exit 0
}

# Elevated re-entry: only deploy what the non-elevated run compiled.
if ($DeployFrom) {
    try {
        # The architectures that were compiled (a -OnlyArch build has fewer than the mod lists).
        $archFile = Join-Path $DeployFrom 'arch.txt'
        if (Test-Path $archFile) { $meta.architecture = @((Get-Content $archFile -Raw).Trim() -split '\|') }
        Invoke-Deploy $cfg $meta $Source $ModId $DeployFrom
    } catch {
        Say "Deploy failed: $_" Red
        exit 1
    }
    exit 0
}

$outDir = Join-Path $BuildTempRoot $meta['id']
if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }
New-Item -ItemType Directory -Force $outDir | Out-Null

Write-Host "Mod:      $ModId $($meta['version']) [$($meta.architecture -join ', ')]"
Write-Host "Windhawk: $($cfg.Root)"
Invoke-Compile $cfg $meta $Source $ModId $outDir
Set-Content (Join-Path $outDir 'arch.txt') ($meta.architecture -join '|') -NoNewline

if ($CompileOnly) { exit 0 }

if (Test-Admin) {
    Invoke-Deploy $cfg $meta $Source $ModId $outDir
} else {
    $log = Join-Path $outDir 'deploy.log'
    $code = Invoke-DeployTask $outDir $log
    if ($null -ne $code) { exit $code }
    Write-Host 'Deploying (elevation required)...' -ForegroundColor Cyan
    $shell = (Get-Process -Id $PID).Path
    $argList = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
        '-Source', "`"$Source`"", '-ModId', "`"$ModId`"",
        '-WindhawkPath', "`"$($cfg.Root)`"",
        '-DeployFrom', "`"$outDir`"", '-LogFile', "`"$log`""
    )
    $p = Start-Process $shell -Verb RunAs -WindowStyle Hidden -Wait -PassThru -ArgumentList $argList
    if (Test-Path $log) {
        foreach ($line in Get-Content $log) {
            $color, $text = $line -split "`t", 2
            Write-Host $text -ForegroundColor $color
        }
    }
    exit $p.ExitCode
}
