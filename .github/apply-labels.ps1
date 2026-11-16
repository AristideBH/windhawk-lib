# Creates or updates the labels listed in .github/labels.yml with the GitHub CLI (gh).
#   pwsh .github/apply-labels.ps1
$ErrorActionPreference = 'Stop'
$file = Join-Path $PSScriptRoot 'labels.yml'
foreach ($line in Get-Content $file) {
    if ($line -notmatch '^- \{name: "(?<name>[^"]+)", color: "(?<color>[0-9a-f]{6})", description: "(?<description>[^"]*)"\}') { continue }
    gh label create $Matches.name --color $Matches.color --description $Matches.description --force | Out-Null
    Write-Host "label: $($Matches.name)"
}
