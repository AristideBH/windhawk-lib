# Sidebar probe: Quick access pins from a normal process (the same shell calls a mod can make).
# Answers: can pins be listed, pinned, unpinned, reordered, and where do they live (to watch them)?
$ErrorActionPreference = 'Stop'
$qa = '::{679f85cb-0220-4080-b29b-5540cc05aab6}'
$sh = New-Object -ComObject Shell.Application

function Get-Pinned {
    $items = $sh.Namespace("shell:$qa").Items()
    foreach ($i in $items) {
        $verbs = @($i.Verbs() | ForEach-Object { $_.Name.Replace('&', '') })
        [pscustomobject]@{ Name = $i.Name; Path = $i.Path; IsFolder = $i.IsFolder
                           CanUnpin = [bool]($verbs | Where-Object { $_ -match 'Détacher|Unpin|Désépingler|Retirer' }) }
    }
}

Write-Host '--- LIST (as the shell returns them)'
Get-Pinned | Format-Table -AutoSize

$test = Join-Path $env:TEMP 'extras-fav-test'
New-Item -ItemType Directory -Force $test | Out-Null
$autoDest = Join-Path $env:APPDATA 'Microsoft\Windows\Recent\AutomaticDestinations\f01b4d95cf55d32a.automaticDestinations-ms'
$before = (Get-Item $autoDest -ErrorAction SilentlyContinue).LastWriteTimeUtc
Write-Host "--- store file: $autoDest  (exists: $([bool]$before))  last write: $before"

Write-Host '--- PIN through the folder verbs'
$parent = $sh.Namespace((Split-Path $test)).ParseName('extras-fav-test')
Write-Host ('verbs on the folder: ' + (($parent.Verbs() | ForEach-Object { $_.Name.Replace('&','') }) -join ' | '))
$parent.InvokeVerb('pintohome')
Start-Sleep -Milliseconds 1500
$after = (Get-Item $autoDest -ErrorAction SilentlyContinue).LastWriteTimeUtc
Write-Host "store last write after pin: $after  (changed: $($after -ne $before))"
$now = Get-Pinned
Write-Host ("pinned now contains the test folder: " + [bool]($now | Where-Object { $_.Path -eq $test }))

Write-Host '--- UNPIN'
$qaItem = $sh.Namespace("shell:$qa").Items() | Where-Object { $_.Path -eq $test } | Select-Object -First 1
if ($qaItem) {
    $qaItem.InvokeVerb('unpinfromhome')
    Start-Sleep -Milliseconds 1500
    Write-Host ("still pinned after unpin: " + [bool](Get-Pinned | Where-Object { $_.Path -eq $test }))
} else { Write-Host 'test folder not found in Quick access, nothing to unpin' }

Remove-Item $test -Force -Recurse -ErrorAction SilentlyContinue
Write-Host '--- DONE'
