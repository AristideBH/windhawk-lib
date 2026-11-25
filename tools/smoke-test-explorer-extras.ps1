<#
.SYNOPSIS
    A quick check of Explorer Extras in a live Explorer, after a deploy: opens a window, looks for what
    the mod must have placed without a click, closes the window, and reads the log and the crash dumps.

.DESCRIPTION
    Needs the debug build (it writes the log file) and the mod enabled. It does not click anything in
    your windows: it opens one new Explorer window of its own and closes it at the end.

    Checks, each printed as PASS or FAIL:
      1. the command bar was built, and the mod saw the window (log)
      2. the details pane's toolbar toggle (WindhawkPaneToggle) is in the window, with no click (UIA)
      3. the Columns entry was added to the View menu (log)
      4. closing the window released the mod's objects (log)
      5. no error line in the log, and no new crash dump of explorer.exe since the start

    The exit code is the number of failed checks.

.PARAMETER Log
    The log file of the debug build (the same path given to deploy-explorer-extras.ps1).

.PARAMETER Path
    The folder the test window opens. By default a new empty folder of its own in %TEMP%, removed at the end:
    Explorer does not open a second window for a folder that one of your windows already shows.
#>
param(
    [string]$Log = (Join-Path $env:LOCALAPPDATA 'ExplorerExtras\debug.log'),
    [string]$Path = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$failed = 0
function Check([string]$name, [bool]$ok, [string]$detail = '') {
    if ($ok) { Write-Host "PASS  $name" } else { Write-Host "FAIL  $name  $detail"; $script:failed++ }
}

if (-not (Test-Path $Log)) { Write-Host "FAIL  no log file at $Log (is it the debug build, and is the mod enabled?)"; exit 1 }
$ownFolder = $false
if (-not $Path) {
    $Path = Join-Path $env:TEMP ('ExplorerExtrasSmoke-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $Path | Out-Null
    $ownFolder = $true
}
$dumpDir = Join-Path $env:LOCALAPPDATA 'CrashDumps'
$began = Get-Date

# a new window of its own
$before = @([System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children',
    (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty, 'CabinetWClass'))))
Start-Process explorer.exe -ArgumentList "`"$Path`""
$window = $null
for ($i = 0; $i -lt 50 -and -not $window; $i++) {
    Start-Sleep -Milliseconds 200
    $now = [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children',
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty, 'CabinetWClass')))
    foreach ($candidate in $now) {
        if (-not ($before | Where-Object { $_.Current.NativeWindowHandle -eq $candidate.Current.NativeWindowHandle })) { $window = $candidate }
    }
}
if (-not $window) {
    if ($ownFolder) { Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Host 'FAIL  the test window did not open'; exit 1
}

# the toggle, with no click: waited for up to 4 s
$toggle = $null
for ($i = 0; $i -lt 20 -and -not $toggle; $i++) {
    $toggle = $window.FindFirst('Descendants',
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'WindhawkPaneToggle')))
    if (-not $toggle) { Start-Sleep -Milliseconds 200 }
}

$handle = $window.Current.NativeWindowHandle
$window.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close()
Start-Sleep -Milliseconds 1500

# the lines of this run: from the last "command bar" line, which the new window made
$lines = Get-Content $Log -Encoding UTF8
$firstOfRun = ($lines | Select-String -Pattern 'command bar "' | Select-Object -Last 1)
$run = if ($firstOfRun) { $lines[($firstOfRun.LineNumber - 1)..($lines.Count - 1)] } else { @() }

Check 'command bar built, window seen by the mod' ($firstOfRun -and ($run | Select-String 'window created')) 'no "command bar ... was built" / "window created" in the log'
Check 'toolbar toggle in the window, no click' ([bool]$toggle) 'WindhawkPaneToggle not found within 4 s'
Check 'Columns entry added to the View menu' ([bool]($run | Select-String 'Columns entry added')) 'no "Columns entry added" line'
Check 'closing the window released the mod''s objects' ([bool]($run | Select-String 'its objects are released')) 'no "its objects are released" line'
$errors = @($run | Select-String -Pattern '0x8[0-9A-Fa-f]{7}|not usable|could not|did not open in time' | Where-Object { $_ -notmatch 'CreateCategory|CanCategorize' })
$labelsLine = $lines | Select-String 'LABELS SELFTEST' | Select-Object -Last 1
Check 'labels self-test passed' ($labelsLine -and $labelsLine.Line -match 'LABELS SELFTEST pass') ($(if ($labelsLine) { $labelsLine.Line } else { 'no LABELS SELFTEST line in the log' }))
$dotsLine = $lines | Select-String 'LABELS DOTS SELFTEST' | Select-Object -Last 1
Check 'labels dots self-test passed' ($dotsLine -and $dotsLine.Line -match 'LABELS DOTS SELFTEST pass') ($(if ($dotsLine) { $dotsLine.Line } else { 'no LABELS DOTS SELFTEST line in the log' }))
$cacheLine = $lines | Select-String 'LABELS CACHE SELFTEST' | Select-Object -Last 1
Check 'labels cache self-test passed' ($cacheLine -and $cacheLine.Line -match 'LABELS CACHE SELFTEST pass') ($(if ($cacheLine) { $cacheLine.Line } else { 'no LABELS CACHE SELFTEST line in the log' }))
$placeLine = $lines | Select-String 'LABELS PLACE SELFTEST' | Select-Object -Last 1
Check 'labels placement self-test passed' ($placeLine -and $placeLine.Line -match 'LABELS PLACE SELFTEST pass') ($(if ($placeLine) { $placeLine.Line } else { 'no LABELS PLACE SELFTEST line in the log' }))
$paneLabelsLine = $lines | Select-String 'LABELS PANE SELFTEST' | Select-Object -Last 1
Check 'labels pane self-test passed' ($paneLabelsLine -and $paneLabelsLine.Line -match 'LABELS PANE SELFTEST pass') ($(if ($paneLabelsLine) { $paneLabelsLine.Line } else { 'no LABELS PANE SELFTEST line in the log' }))
Check 'no error line in the log' ($errors.Count -eq 0) ($errors | Select-Object -First 2 | ForEach-Object { $_.Line })
$dumps = @(Get-ChildItem $dumpDir -Filter 'explorer.exe.*.dmp' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $began })
Check 'no new crash dump of explorer.exe' ($dumps.Count -eq 0) ($dumps | ForEach-Object { $_.Name })

if ($ownFolder) { Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue }
Write-Host ("{0} check(s) failed" -f $failed)
exit $failed
