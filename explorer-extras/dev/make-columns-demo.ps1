# Rebuilds the "Columns Demo" test folder on the Desktop, the one the Explorer
# Extras tests use: about 90 items, six levels deep, several file types, five
# real pictures, a folder of 40 files, long names, an empty folder.
#
#   pwsh explorer-extras\dev\make-columns-demo.ps1            (refuses if it exists)
#   pwsh explorer-extras\dev\make-columns-demo.ps1 -Force     (deletes and rebuilds)

param([switch]$Force)

$root = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Columns Demo'
if (Test-Path $root) {
    if (-not $Force) {
        Write-Host "Already there: $root  (use -Force to rebuild)"
        exit 1
    }
    Remove-Item $root -Recurse -Force
}
New-Item -ItemType Directory -Path $root | Out-Null

function MakeDir($path)  { New-Item -ItemType Directory -Path (Join-Path $root $path) -Force | Out-Null }
function MakeFile($path, $text = $null, [int]$bytes = 0) {
    $full = Join-Path $root $path
    New-Item -ItemType Directory -Path (Split-Path $full) -Force | Out-Null
    if ($bytes -gt 0) {
        $buffer = New-Object byte[] $bytes
        (New-Object Random 42).NextBytes($buffer)
        [IO.File]::WriteAllBytes($full, $buffer)
    } else {
        Set-Content -Path $full -Value ($text ?? "Columns Demo`n$path`n") -Encoding UTF8
    }
}

# Top level: folders and files of several kinds.
MakeDir 'Empty folder'
MakeDir 'A very long folder name to test how columns cut the text in the middle of a name'
MakeFile 'A very long folder name to test how columns cut the text in the middle of a name\inside.txt'
MakeFile 'A very long folder name to test how columns cut the text in the middle of a name\another file with quite a long name to cut as well, in the middle.md'
MakeFile 'README.txt' "Explorer Extras test folder.`nSix levels, many types, a big folder, long names.`n"
MakeFile 'Notes.md' "# Notes`n`n- one`n- two`n"
MakeFile 'data.csv' "id,name,size`n1,alpha,10`n2,beta,20`n3,gamma,30`n"
MakeFile 'Zebra file with no extension' "no extension`n"
MakeFile 'config.json' '{ "demo": true, "levels": 6 }'
MakeFile 'page.html' '<html><body><h1>Columns Demo</h1></body></html>'
MakeFile 'script.ps1' 'Write-Host "demo"'
MakeFile 'archive-sample.bin' -bytes 40960

# Documents, Music, Archive.
MakeFile 'Documents\report.pdf' -bytes 20480
MakeFile 'Documents\letter.docx' -bytes 12288
MakeFile 'Documents\Reports\2025\q1.xlsx' -bytes 8192
MakeFile 'Documents\Reports\2025\q2.xlsx' -bytes 8192
MakeFile 'Documents\Reports\2024\summary.txt'
MakeFile 'Music\song one.mp3' -bytes 30000
MakeFile 'Music\song two.mp3' -bytes 30000
MakeFile 'Archive\old.zip' -bytes 5000
MakeFile 'Archive\older.7z' -bytes 5000
MakeFile 'Archive\2023\backup.zip' -bytes 5000

# Six levels.
MakeFile 'Projects\Alpha\Beta\Gamma\Delta\Epsilon\deep file.txt' "six levels down`n"
MakeFile 'Projects\Alpha\Beta\notes.txt'
MakeFile 'Projects\Alpha\readme.md' "# Alpha`n"

# Five real pictures.
MakeDir 'Pictures'
try {
    Add-Type -AssemblyName System.Drawing
    $colors = 'Crimson', 'SteelBlue', 'SeaGreen', 'Goldenrod', 'MediumOrchid'
    for ($i = 0; $i -lt 5; $i++) {
        $bitmap = New-Object System.Drawing.Bitmap 320, 200
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush `
            ([System.Drawing.Point]::new(0, 0)), ([System.Drawing.Point]::new(320, 200)),
            ([System.Drawing.Color]::FromName($colors[$i])), ([System.Drawing.Color]::White)
        $graphics.FillRectangle($brush, 0, 0, 320, 200)
        $graphics.DrawString("Picture $($i + 1)", (New-Object System.Drawing.Font 'Segoe UI', 24),
            [System.Drawing.Brushes]::Black, 20, 80)
        $bitmap.Save((Join-Path $root "Pictures\picture $($i + 1).png"),
            [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose(); $bitmap.Dispose()
    }
} catch {
    # Fallback: the same one-pixel picture five times.
    $png = [Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==')
    1..5 | ForEach-Object { [IO.File]::WriteAllBytes((Join-Path $root "Pictures\picture $_.png"), $png) }
}

# A big folder: 40 files, mixed names and sizes.
1..40 | ForEach-Object {
    $kind = @('txt', 'md', 'csv', 'log')[$_ % 4]
    MakeFile ('Many files\file {0:D2}.{1}' -f $_, $kind)
}

$count = (Get-ChildItem $root -Recurse | Measure-Object).Count
Write-Host "Rebuilt $root with $count items."
