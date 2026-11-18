# Builds the showcase folder, C:\Showcase by default (a path without a user name, for screenshots and GIFs): a believable working folder of a
# freelance designer and developer, for a video of Explorer Extras and for the
# tests (Columns, and the details pane in "Edge cases\Pane previews"). About 900 items, real pictures, real PDFs, Word and Excel files, playable
# tones, real zip archives, shortcuts, a junction, dated over five years, and every
# edge case the mod meets, in the folder "Edge cases" (see SHOWCASE-TESTS.md).
#
#   pwsh explorer-extras\make-columns-showcase.ps1            (refuses if it exists)
#   pwsh explorer-extras\make-columns-showcase.ps1 -Force     (deletes and rebuilds)
#   -Lite           the folders for screenshots only (no "Edge cases", no huge folder): a few seconds
#   -Root D:\Demo   another place (default C:\Showcase)
#   -Remove         deletes the showcase folder and stops
#
# It also puts colour labels (the tags of Explorer Extras) on a believable set of items, in "Design review" and a few
# other folders: they are the NTFS stream ExplorerExtras.Labels, the mod does not need to be running to write them.
#   -Huge 3000      the size of "Huge folder" (default 1500)
#   -Stress         also a folder with 6000 files, over the mod's default limit of 5000

param([switch]$Force, [int]$Huge = 1500, [switch]$Stress, [string]$Root = 'C:\Showcase', [switch]$Lite, [switch]$Remove)
$ErrorActionPreference = 'Stop'
$started = Get-Date

$root = $Root
if ($Remove -and -not (Test-Path -LiteralPath $root)) {
    Write-Host "Nothing to remove: $root"
    exit 0
}
if (Test-Path -LiteralPath $root) {
    if (-not $Force -and -not $Remove) {
        Write-Host "Already there: $root  (use -Force to rebuild)"
        exit 1
    }
    # A junction inside must go on its own, never through its target.
    Get-ChildItem -LiteralPath $root -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
        ForEach-Object { cmd /c rmdir "`"$($_.FullName)`"" 2>$null | Out-Null }
    Remove-Item -LiteralPath $root -Recurse -Force
    if ($Remove) {
        Write-Host "Removed $root"
        exit 0
    }
}
try {
    New-Item -ItemType Directory -Path $root | Out-Null
} catch {
    Write-Host "Could not create $root ($($_.Exception.Message)). Run PowerShell as administrator, or give another place with -Root."
    exit 1
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -TypeDefinition @'
public static class Tone {
    // A short mono 16-bit WAV: a note with a soft fifth over it and a fade.
    public static byte[] Make(double freq, double seconds, int rate) {
        int n = (int)(rate * seconds);
        byte[] bytes = new byte[44 + n * 2];
        System.Text.Encoding.ASCII.GetBytes("RIFF").CopyTo(bytes, 0);
        System.BitConverter.GetBytes(36 + n * 2).CopyTo(bytes, 4);
        System.Text.Encoding.ASCII.GetBytes("WAVEfmt ").CopyTo(bytes, 8);
        System.BitConverter.GetBytes(16).CopyTo(bytes, 16);
        System.BitConverter.GetBytes((short)1).CopyTo(bytes, 20);
        System.BitConverter.GetBytes((short)1).CopyTo(bytes, 22);
        System.BitConverter.GetBytes(rate).CopyTo(bytes, 24);
        System.BitConverter.GetBytes(rate * 2).CopyTo(bytes, 28);
        System.BitConverter.GetBytes((short)2).CopyTo(bytes, 32);
        System.BitConverter.GetBytes((short)16).CopyTo(bytes, 34);
        System.Text.Encoding.ASCII.GetBytes("data").CopyTo(bytes, 36);
        System.BitConverter.GetBytes(n * 2).CopyTo(bytes, 40);
        for (int i = 0; i < n; i++) {
            double t = (double)i / rate;
            double fade = System.Math.Min(1.0, System.Math.Min(t * 20, (seconds - t) * 8));
            double v = 0.5 * System.Math.Sin(2 * System.Math.PI * freq * t)
                     + 0.2 * System.Math.Sin(2 * System.Math.PI * freq * 1.5 * t);
            short s = (short)(v * fade * 12000);
            System.BitConverter.GetBytes(s).CopyTo(bytes, 44 + i * 2);
        }
        return bytes;
    }
}
'@

$script:Base = $root
$now = Get-Date
$rng = [Random]::new(2026)
$utf8 = [Text.UTF8Encoding]::new($false)
$script:Count = 0
$script:Tagged = @{}  # The items that have their labels already.

# ---------------------------------------------------------------- helpers

function Full($rel) { Join-Path $script:Base $rel }

function Stamp($path, [double]$days) {
    # A negative number asks for a believable random age, mostly recent.
    if ($days -lt 0) {
        $days = [Math]::Pow($rng.NextDouble(), 2.2) * 900 + 2
    }
    $when = $now.AddDays(-$days).AddMinutes(-$rng.Next(0, 480))
    $item = Get-Item -LiteralPath $path -Force
    $item.CreationTime = $when.AddDays(-$rng.Next(0, 3))
    $item.LastWriteTime = $when
}

function MakeDir($rel, [double]$days = -1) {
    $path = Full $rel
    [IO.Directory]::CreateDirectory($path) | Out-Null
    Stamp $path $days
}

# Text or bytes. Bigger than a megabyte is a block of zeros, made instantly.
function Put($rel, $text = $null, [long]$bytes = 0, [double]$days = -1) {
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    if ($bytes -gt 1MB) {
        $stream = [IO.File]::Create($path); $stream.SetLength($bytes); $stream.Dispose()
    } elseif ($bytes -gt 0) {
        $buffer = New-Object byte[] $bytes; $rng.NextBytes($buffer)
        [IO.File]::WriteAllBytes($path, $buffer)
    } else {
        if ($null -eq $text) { $text = "$([IO.Path]::GetFileName($rel))`n" }
        [IO.File]::WriteAllText($path, $text, $utf8)
    }
    Stamp $path $days
    $script:Count++
    $script:LastPut = $path
}

function Hsv([double]$h, [double]$s, [double]$v) {
    $h = (($h % 360) + 360) % 360
    $c = $v * $s; $x = $c * (1 - [Math]::Abs((($h / 60) % 2) - 1)); $m = $v - $c
    switch ([int][Math]::Floor($h / 60)) {
        0 { $r, $g, $b = $c, $x, 0 } 1 { $r, $g, $b = $x, $c, 0 } 2 { $r, $g, $b = 0, $c, $x }
        3 { $r, $g, $b = 0, $x, $c } 4 { $r, $g, $b = $x, 0, $c } default { $r, $g, $b = $c, 0, $x }
    }
    [System.Drawing.Color]::FromArgb(255, [int](($r + $m) * 255), [int](($g + $m) * 255), [int](($b + $m) * 255))
}

$jpeg = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
$jpegParams = New-Object System.Drawing.Imaging.EncoderParameters 1
$jpegParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 84L

# A landscape made of a sky, a sun and layered hills, never the same twice.
function Photo($rel, [int]$w = 1280, [int]$h = 853, [int]$seed = 1, [double]$days = -1, [string]$kind = 'landscape') {
    $r = [Random]::new($seed)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $hue = $r.Next(0, 360)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
    $sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, (Hsv ($hue + 200) 0.55 0.55), (Hsv ($hue + 20) 0.45 1.0), ([single]90)
    $g.FillRectangle($sky, $rect)
    if ($kind -eq 'landscape') {
        $sun = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(230, 255, 244, 214))
        $size = [int]($h * (0.14 + $r.NextDouble() * 0.12))
        $g.FillEllipse($sun, [int]($w * (0.2 + $r.NextDouble() * 0.6)), [int]($h * (0.18 + $r.NextDouble() * 0.2)), $size, $size)
        for ($layer = 0; $layer -lt 4; $layer++) {
            $base = $h * (0.5 + $layer * 0.12)
            $amp = $h * (0.10 - $layer * 0.015); $phase = $r.NextDouble() * 6; $freq = 1.5 + $r.NextDouble() * 2
            $points = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            for ($x = 0; $x -le $w; $x += 16) {
                $points.Add([System.Drawing.PointF]::new($x, [float]($base + $amp * [Math]::Sin($phase + $x / $w * $freq * 6.28))))
            }
            $points.Add([System.Drawing.PointF]::new($w, $h)); $points.Add([System.Drawing.PointF]::new(0, $h))
            $brush = [System.Drawing.SolidBrush]::new((Hsv ($hue + 120 + $layer * 14) (0.35 + $layer * 0.1) (0.62 - $layer * 0.12)))
            $g.FillPolygon($brush, $points.ToArray())
        }
    } else {
        # A frame of a screen: window, title bar, lines of text.
        $g.Clear([System.Drawing.Color]::FromArgb(245, 246, 248))
        $g.FillRectangle([System.Drawing.Brushes]::White, 40, 40, $w - 80, $h - 80)
        $g.FillRectangle([System.Drawing.SolidBrush]::new((Hsv $hue 0.5 0.85)), 40, 40, $w - 80, 36)
        for ($line = 0; $line -lt 9; $line++) {
            $g.FillRectangle([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(210, 214, 220)),
                72, 110 + $line * 34, [int](($w - 200) * (0.35 + $r.NextDouble() * 0.6)), 14)
        }
    }
    $g.Dispose()
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    if ($rel -like '*.png') { $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) }
    else { $bmp.Save($path, $jpeg, $jpegParams) }
    $bmp.Dispose()
    Stamp $path $days
    $script:Count++
}

function PdfEscape([string]$text) { $text.Replace('\', '\\').Replace('(', '\(').Replace(')', '\)') }

# A one page PDF with a title and some lines, valid for any reader.
function Pdf($rel, [string]$title, [string[]]$lines = @(), [double]$days = -1) {
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $content = "BT /F1 24 Tf 72 760 Td ($(PdfEscape $title)) Tj ET`n"
    $y = 716
    foreach ($line in $lines) {
        $content += "BT /F1 12 Tf 72 $y Td ($(PdfEscape $line)) Tj ET`n"
        $y -= 20
    }
    $objects = @(
        '<< /Type /Catalog /Pages 2 0 R >>',
        '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
        "<< /Length $($latin1.GetByteCount($content)) >>`nstream`n$content`nendstream",
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>')
    $body = "%PDF-1.4`n"
    $offsets = @()
    for ($i = 0; $i -lt $objects.Count; $i++) {
        $offsets += $latin1.GetByteCount($body)
        $body += "$($i + 1) 0 obj`n$($objects[$i])`nendobj`n"
    }
    $xref = $latin1.GetByteCount($body)
    $body += "xref`n0 $($objects.Count + 1)`n0000000000 65535 f `n"
    foreach ($offset in $offsets) { $body += ('{0:D10} 00000 n ' -f $offset) + "`n" }
    $body += "trailer`n<< /Size $($objects.Count + 1) /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    [IO.File]::WriteAllBytes($path, $latin1.GetBytes($body))
    Stamp $path $days
    $script:Count++
}

function Xml([string]$text) { [Security.SecurityElement]::Escape($text) }

function ZipEntry($archive, [string]$name, [string]$text) {
    $entry = $archive.CreateEntry($name)
    $writer = [IO.StreamWriter]::new($entry.Open(), $utf8)
    $writer.Write($text); $writer.Dispose()
}

# A minimal Word document that Word opens.
function Docx($rel, [string[]]$paragraphs, [double]$days = -1) {
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    $archive = [IO.Compression.ZipFile]::Open($path, 'Create')
    ZipEntry $archive '[Content_Types].xml' '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>'
    ZipEntry $archive '_rels/.rels' '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>'
    $body = ($paragraphs | ForEach-Object { "<w:p><w:r><w:t xml:space=`"preserve`">$(Xml $_)</w:t></w:r></w:p>" }) -join ''
    ZipEntry $archive 'word/document.xml' "<?xml version=`"1.0`" encoding=`"UTF-8`"?><w:document xmlns:w=`"http://schemas.openxmlformats.org/wordprocessingml/2006/main`"><w:body>$body</w:body></w:document>"
    $archive.Dispose()
    Stamp $path $days
    $script:Count++
}

# A minimal Excel workbook with one sheet; $rows is an array of arrays.
function Xlsx($rel, $rows, [double]$days = -1) {
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    $archive = [IO.Compression.ZipFile]::Open($path, 'Create')
    ZipEntry $archive '[Content_Types].xml' '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>'
    ZipEntry $archive '_rels/.rels' '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'
    ZipEntry $archive 'xl/workbook.xml' '<?xml version="1.0" encoding="UTF-8"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>'
    ZipEntry $archive 'xl/_rels/workbook.xml.rels' '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>'
    $sheet = ''
    $rowNumber = 1
    foreach ($row in $rows) {
        $cells = ''
        $column = 0
        foreach ($value in $row) {
            $reference = [string][char](65 + $column) + $rowNumber
            if ($value -is [string]) { $cells += "<c r=`"$reference`" t=`"inlineStr`"><is><t>$(Xml $value)</t></is></c>" }
            else { $cells += "<c r=`"$reference`"><v>$value</v></c>" }
            $column++
        }
        $sheet += "<row r=`"$rowNumber`">$cells</row>"
        $rowNumber++
    }
    ZipEntry $archive 'xl/worksheets/sheet1.xml' "<?xml version=`"1.0`" encoding=`"UTF-8`"?><worksheet xmlns=`"http://schemas.openxmlformats.org/spreadsheetml/2006/main`"><sheetData>$sheet</sheetData></worksheet>"
    $archive.Dispose()
    Stamp $path $days
    $script:Count++
}

function Wav($rel, [double]$freq, [double]$seconds = 2.5, [double]$days = -1) {
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    [IO.File]::WriteAllBytes($path, [Tone]::Make($freq, $seconds, 22050))
    Stamp $path $days
    $script:Count++
}

# A real archive of what the block builds, with the files' own dates inside.
function Zip($rel, [scriptblock]$fill, [double]$days = -1) {
    $work = Join-Path $env:TEMP ('showcase-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    $saved = $script:Base; $script:Base = $work
    try { & $fill } finally { $script:Base = $saved }
    $target = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $target)) | Out-Null
    [IO.Compression.ZipFile]::CreateFromDirectory($work, $target)
    Remove-Item -LiteralPath $work -Recurse -Force
    Stamp $target $days
    $script:Count++
}

function Lnk($rel, [string]$target, [double]$days = -1) {
    $shell = New-Object -ComObject WScript.Shell
    $path = Full $rel
    [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $target
    $link.Save()
    Stamp $path $days
    $script:Count++
}

function Svg($rel, [string]$word, [int]$hue, [double]$days = -1) {
    $a = (Hsv $hue 0.6 0.9); $b = (Hsv ($hue + 40) 0.7 0.75)
    $hexA = '#{0:X2}{1:X2}{2:X2}' -f $a.R, $a.G, $a.B; $hexB = '#{0:X2}{1:X2}{2:X2}' -f $b.R, $b.G, $b.B
    Put $rel "<svg xmlns=`"http://www.w3.org/2000/svg`" width=`"480`" height=`"160`" viewBox=`"0 0 480 160`"><defs><linearGradient id=`"g`" x1=`"0`" y1=`"0`" x2=`"1`" y2=`"1`"><stop offset=`"0`" stop-color=`"$hexA`"/><stop offset=`"1`" stop-color=`"$hexB`"/></linearGradient></defs><circle cx=`"80`" cy=`"80`" r=`"56`" fill=`"url(#g)`"/><text x=`"160`" y=`"96`" font-family=`"Segoe UI`" font-size=`"44`" font-weight=`"600`" fill=`"#222`">$(Xml $word)</text></svg>" -days $days | Out-Null
}

$month = @('January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December')

# A named stream on a file or a folder (a folder needs the backup semantics flag, which Set-Content does not give).
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LabelStream {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteFile(IntPtr file, byte[] data, uint count, out uint written, IntPtr overlapped);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    public static void Write(string path, byte[] data) {
        IntPtr file = CreateFileW(path, 0x40000000, 7, IntPtr.Zero, 2, 0x02000000, IntPtr.Zero);  // GENERIC_WRITE, CREATE_ALWAYS, BACKUP_SEMANTICS
        if (file == (IntPtr)(-1)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        uint written;
        WriteFile(file, data, (uint)data.Length, out written, IntPtr.Zero);
        CloseHandle(file);
    }
}
'@

# Colour labels, the stream Explorer Extras keeps. $tags is @(@(colour, 'name'), ...): the colours are 0 gray, 1 red, 2 orange,
# 3 yellow, 4 green, 5 teal, 6 blue, 7 purple, and an empty name shows the colour's own name. The dates are put back after the write.
function Tag($rel, $tags) {
    $path = Full $rel
    if (-not (Test-Path -LiteralPath $path)) { return }
    $item = Get-Item -LiteralPath $path -Force
    $written = $item.LastWriteTime
    $created = $item.CreationTime
    if ($tags[0] -isnot [array]) { $tags = , $tags }  # One tag given as @(colour, name) is not a list of tags.
    $script:Tagged[$rel] = $true
    $text = "v1`n"
    foreach ($tag in $tags) { $text += ('{0}{1}{2}{3}' -f $tag[0], "`t", $tag[1], "`n") }
    [LabelStream]::Write($path + ':ExplorerExtras.Labels', $utf8.GetBytes($text))
    $item = Get-Item -LiteralPath $path -Force
    $item.CreationTime = $created
    $item.LastWriteTime = $written
}

# ---------------------------------------------------------------- the top

Put 'Welcome.md' "# Showcase`n`nA working folder, as a freelance designer and developer keeps it.`n`n- **Clients**, **Projects**, **Photos**, **Music**, **Documents**, **Videos**`n- **Inbox** is the mess that builds up, **Archive** is what was zipped away`n- **Drop zones** are folders to drag things into`n- **Edge cases** is for the tests`n" -days 1 | Out-Null

# ---------------------------------------------------------------- Clients

$lumiere = 'Clients\Atelier Lumière'
Svg "$lumiere\Brand\Logo\logo-primary.svg" 'Atelier Lumière' 28 -days 160
Svg "$lumiere\Brand\Logo\logo-mono.svg" 'Atelier Lumière' 0 -days 160
Photo "$lumiere\Brand\Logo\logo-primary.png" 960 320 11 160 'screen'
Pdf "$lumiere\Brand\Guidelines\Brand guidelines.pdf" 'Atelier Lumière, brand guidelines' @('Version 2.1', 'Logo, colours, typography, tone of voice', 'Prepared by Studio Aurora') 150
Pdf "$lumiere\Brand\Guidelines\Typography specimen.pdf" 'Typography specimen' @('Headings: Fraunces', 'Body: Inter') 150
Photo "$lumiere\Brand\Guidelines\Colour palette.png" 1280 720 12 150 'screen'
1..4 | ForEach-Object { Photo "$lumiere\Brand\Photography\Shop front $_.jpg" 1280 853 (20 + $_) (140 - $_ * 3) }
Pdf "$lumiere\Contracts\Contract 2025-03 (signed).pdf" 'Services agreement' @('Atelier Lumière and Studio Aurora', 'Signed 12 March 2025') 120
Pdf "$lumiere\Contracts\NDA.pdf" 'Non-disclosure agreement' @('Mutual, two years') 130
1..6 | ForEach-Object { Pdf ("$lumiere\Invoices\2024\Invoice 2024-{0:D3}.pdf" -f $_) ("INVOICE 2024-{0:D3}" -f $_) @('Atelier Lumière', "Amount: $(1200 + $_ * 340) EUR", 'Paid') (420 - $_ * 28) }
1..14 | ForEach-Object { Pdf ("$lumiere\Invoices\2025\Invoice 2025-{0:D3}.pdf" -f $_) ("INVOICE 2025-{0:D3}" -f $_) @('Atelier Lumière', "Amount: $(900 + $_ * 175) EUR", $(if ($_ -lt 13) { 'Paid' } else { 'Due in 30 days' })) (240 - $_ * 16) }
Put "$lumiere\Meetings\2025-01-14 Kickoff.md" "# Kickoff`n`n- Scope: identity, packaging, website`n- Deadline: end of April`n" -days 118
Put "$lumiere\Meetings\2025-02-04 Review.md" "# Review`n`nRound one of the logo. Option B wins.`n" -days 97
Put "$lumiere\Meetings\2025-03-11 Handoff.md" "# Handoff`n`nFiles go to the printer on Friday.`n" -days 70
Docx "$lumiere\Briefs\Brief v1.docx" @('Brief', 'A warm, modern identity for a lamp maker.') 140
Docx "$lumiere\Briefs\Brief v2 (final).docx" @('Brief, final', 'Warm, modern, a little playful.', 'Deliverables: logo, packaging, website.') 125
Photo "$lumiere\Briefs\Moodboard.png" 1600 900 31 125 'screen'

$nord = 'Clients\Nordlicht GmbH'
Photo "$nord\Website\Wireframes\wireframe-home.png" 1440 900 41 60 'screen'
Photo "$nord\Website\Wireframes\wireframe-product.png" 1440 900 42 58 'screen'
Photo "$nord\Website\Wireframes\wireframe-contact.png" 1440 900 43 55 'screen'
Docx "$nord\Website\Copy\Homepage copy (DE).docx" @('Willkommen bei Nordlicht', 'Licht, das bleibt.') 52
Docx "$nord\Website\Copy\Homepage copy (EN).docx" @('Welcome to Nordlicht', 'Light that stays.') 50
Pdf "$nord\Quote 2025-02.pdf" 'Quote 2025-02' @('Website, 5 pages', 'Total: 6 400 EUR') 90
1..5 | ForEach-Object { Pdf ("$nord\Invoices\Rechnung 2025-{0:D2}.pdf" -f $_) ("RECHNUNG 2025-{0:D2}" -f $_) @('Nordlicht GmbH') (80 - $_ * 9) }
Put "$nord\Kontakt.vcf" "BEGIN:VCARD`nVERSION:3.0`nFN:Anja Brandt`nORG:Nordlicht GmbH`nEMAIL:anja@nordlicht.example`nEND:VCARD`n" -days 200

$oceane = 'Clients\Café Océane'
Pdf "$oceane\Menu\Spring 2025\Menu printed.pdf" 'Menu, spring 2025' @('Starters', 'Mains', 'Desserts') 40
1..5 | ForEach-Object { Photo "$oceane\Menu\Spring 2025\Dish $_.jpg" 1200 800 (50 + $_) (45 - $_) }
Pdf "$oceane\Menu\Summer 2025\Menu draft.pdf" 'Menu, summer 2025 (draft)' @('To be confirmed') 6
1..3 | ForEach-Object { Photo "$oceane\Menu\Summer 2025\Dish $_.jpg" 1200 800 (60 + $_) (5 - $_ * 1.2) }
Photo "$oceane\Photos\Terrace.jpg" 1600 1067 71 30
Put "$oceane\Opening hours.txt" "Mon-Fri 8:00-18:00`nSat-Sun 9:00-19:00`n" -days 33

Put 'Clients\Bluebird Studio\Only file here.txt' "A folder with a single file.`n" -days 9
MakeDir 'Clients\Bluebird Studio'

# ---------------------------------------------------------------- Projects

$site = 'Projects\aurora-site'
Put "$site\package.json" '{ "name": "aurora-site", "version": "2.4.0", "private": true, "scripts": { "dev": "vite", "build": "vite build" } }' -days 3 | Out-Null
Put "$site\README.md" "# aurora-site`n`nThe studio website.`n`n    npm install`n    npm run dev`n" -days 3 | Out-Null
Put "$site\tsconfig.json" '{ "compilerOptions": { "strict": true, "target": "ES2022" } }' -days 40 | Out-Null
Put "$site\.gitignore" "node_modules`ndist`n.env`n" -days 40 | Out-Null
Put "$site\.env.example" "API_URL=https://api.example.com`n" -days 40 | Out-Null
(Get-Item -LiteralPath (Full "$site\.gitignore") -Force).Attributes = 'Hidden'
Put "$site\.git\HEAD" "ref: refs/heads/main`n" -days 3 | Out-Null
Put "$site\.git\config" "[core]`n`trepositoryformatversion = 0`n" -days 3 | Out-Null
(Get-Item -LiteralPath (Full "$site\.git") -Force).Attributes = 'Hidden, Directory'
foreach ($name in 'Button', 'Card', 'Modal', 'Navbar', 'Footer', 'Hero', 'Gallery', 'ContactForm') {
    Put "$site\src\components\ui\$name.tsx" "export function $name() {`n  return <div className=`"$($name.ToLower())`">$name</div>`n}`n" -days (2 + $rng.Next(0, 90)) | Out-Null
}
foreach ($name in 'useScroll', 'useTheme', 'useMedia') { Put "$site\src\hooks\$name.ts" "export function $name() {}`n" -days (5 + $rng.Next(0, 60)) | Out-Null }
foreach ($name in 'index', 'about', 'work', 'contact') { Put "$site\src\pages\$name.tsx" "export default function Page() { return null }`n" -days (4 + $rng.Next(0, 50)) | Out-Null }
Put "$site\src\styles\global.css" "body { font-family: Inter, sans-serif; }`n" -days 12 | Out-Null
Put "$site\src\styles\tokens.css" ":root { --accent: #e4572e; }`n" -days 12 | Out-Null
Photo "$site\public\images\hero.jpg" 1600 900 81 20
Photo "$site\public\images\team.jpg" 1600 900 82 21
Put "$site\public\favicon.svg" "<svg xmlns=`"http://www.w3.org/2000/svg`" viewBox=`"0 0 32 32`"><circle cx=`"16`" cy=`"16`" r=`"14`" fill=`"#e4572e`"/></svg>" -days 90 | Out-Null
Put "$site\tests\button.test.tsx" "test('renders', () => {})`n" -days 30 | Out-Null
Put "$site\docs\architecture.md" "# Architecture`n`nVite, React, a headless CMS.`n" -days 75 | Out-Null

$app = 'Projects\mobile-app\android\app\src\main\java\com\aurora\app\ui\screens\home'
Put "$app\HomeScreen.kt" "package com.aurora.app.ui.screens.home`n" -days 8 | Out-Null
Put "$app\HomeViewModel.kt" "package com.aurora.app.ui.screens.home`n" -days 8 | Out-Null
Put 'Projects\mobile-app\README.md' "# mobile-app`n" -days 8 | Out-Null
Put 'Projects\mobile-app\ios\Podfile' "platform :ios, '15.0'`n" -days 60 | Out-Null

Put 'Projects\scripts\backup.ps1' "robocopy `$env:USERPROFILE\Documents D:\Backup /MIR`n" -days 140 | Out-Null
Put 'Projects\scripts\rename-photos.ps1' "Get-ChildItem *.jpg | Rename-Item -NewName { `$_.LastWriteTime.ToString('yyyy-MM-dd HHmm') + '.jpg' }`n" -days 66 | Out-Null
Put 'Projects\scripts\build.sh' "#!/bin/sh`nnpm run build`n" -days 66 | Out-Null
Put 'Projects\scripts\deploy.cmd' "@echo off`nrsync -av dist/ server:/var/www/`n" -days 66 | Out-Null

$rows = @(, @('Month', 'Visitors', 'Orders', 'Revenue'))
0..11 | ForEach-Object { $rows += , @($month[$_], (3000 + $rng.Next(0, 6000)), (40 + $rng.Next(0, 90)), (4000 + $rng.Next(0, 9000))) }
Xlsx 'Projects\notebooks\results.xlsx' $rows 25
$csv = "month,visitors,orders`n" + ((1..2500 | ForEach-Object { '2024-{0:D2}-{1:D2},{2},{3}' -f (1 + $_ % 12), (1 + $_ % 28), $rng.Next(100, 900), $rng.Next(0, 40) }) -join "`n")
Put 'Projects\notebooks\data\sales-2024.csv' $csv -days 120 | Out-Null
Put 'Projects\notebooks\analysis.ipynb' '{ "cells": [], "metadata": {}, "nbformat": 4, "nbformat_minor": 5 }' -days 25 | Out-Null

# ---------------------------------------------------------------- Photos

$place = @{ 'Iceland' = 8; 'Lisbon' = 10; 'Wedding Sophie & Marc' = 12; 'Family' = 9 }
foreach ($n in 1..8) { Photo "Photos\2023\Iceland\IMG_$('{0:D4}' -f (1204 + $n * 3)).jpg" 1600 1067 (100 + $n) (700 - $n * 2) }
foreach ($n in 1..10) { Photo "Photos\2024\Lisbon\Lisbon $('{0:D2}' -f $n).jpg" 1600 1067 (120 + $n) (420 - $n * 2) }
foreach ($n in 1..12) { Photo "Photos\2024\Wedding Sophie & Marc\DSC_$('{0:D4}' -f (3300 + $n * 7)).jpg" 1600 1067 (140 + $n) (330 - $n) }
foreach ($n in 1..9) { Photo "Photos\2024\Family\$($month[$n % 12]) $n.jpg" 1200 1600 (160 + $n) (250 - $n * 5) }  # Portrait.
foreach ($n in 1..8) { Photo "Photos\2025\Spring\Spring $n.jpg" 1600 1067 (180 + $n) (60 - $n * 3) }
foreach ($n in 1..12) {
    $when = $now.AddDays(-$n * 4).AddHours(-$rng.Next(0, 12))
    Photo ("Photos\2025\Screenshots\Screenshot {0} {1}.png" -f $when.ToString('yyyy-MM-dd'), $when.ToString('HHmmss')) 1440 900 (200 + $n) ($n * 4) 'screen'
}
foreach ($n in 1..30) { Photo ("Photos\Unsorted\IMG_{0:D4}.jpg" -f (4100 + $n * 5)) (800 + 40 * ($n % 5)) (600 + 30 * ($n % 3)) (300 + $n) ($n * 11) }

# ---------------------------------------------------------------- Music

$albums = @(
    @{ Artist = 'Luna Harbor'; Album = 'Salt & Static (2023)'; Tracks = 'Intro', 'Low Tide', 'Static', 'Harbor Lights', 'Salt', 'Outro'; Base = 196 },
    @{ Artist = 'The Quiet Engines'; Album = 'Night Shift (2024)'; Tracks = 'Clock In', 'Sodium', 'Empty Platform', 'Last Train', 'Clock Out'; Base = 164 },
    @{ Artist = 'Various'; Album = 'Studio playlist'; Tracks = 'Focus', 'Warm up', 'Deep work', 'Cool down'; Base = 220 })
$albumNumber = 0
foreach ($album in $albums) {
    $albumNumber++
    $track = 0
    foreach ($title in $album.Tracks) {
        $track++
        Wav ("Music\Artists\$($album.Artist)\$($album.Album)\{0:D2} $title.wav" -f $track) ($album.Base * [Math]::Pow(1.122, $track)) (2 + $track * 0.4) (500 - $albumNumber * 90 - $track)
    }
    Photo "Music\Artists\$($album.Artist)\$($album.Album)\cover.jpg" 600 600 (400 + $albumNumber) (500 - $albumNumber * 90)
}
Put 'Music\Playlists\Focus.m3u' "#EXTM3U`n..\Artists\Various\Studio playlist\01 Focus.wav`n..\Artists\Various\Studio playlist\03 Deep work.wav`n" -days 60 | Out-Null
Put 'Music\Playlists\Road trip.m3u8' "#EXTM3U`n..\Artists\Luna Harbor\Salt & Static (2023)\02 Low Tide.wav`n" -days 300 | Out-Null

# ---------------------------------------------------------------- Documents

foreach ($year in 2023, 2024) {
    Pdf "Documents\Taxes\$year\Tax notice $year.pdf" "Tax notice $year" @('Income', 'Deductions', 'Balance') (($now.Year - $year) * 365 - 30)
    Xlsx "Documents\Taxes\$year\Expenses $year.xlsx" @(@('Category', 'Amount'), @('Software', 1840), @('Hardware', 2290), @('Travel', 760)) (($now.Year - $year) * 365 - 45)
    1..5 | ForEach-Object { Pdf "Documents\Taxes\$year\Receipt $_.pdf" "Receipt $_" @('Hardware store') (($now.Year - $year) * 365 - 60 - $_ * 9) }
}
Pdf 'Documents\Insurance\Home insurance policy.pdf' 'Home insurance policy' @('Renewal every January') 330
Pdf 'Documents\Insurance\Professional liability.pdf' 'Professional liability' @('Studio Aurora') 330
$shops = 'Hardware store', 'Art supplies', 'Coffee', 'Train ticket', 'Printer ink', 'Domain renewal', 'Hosting', 'Lunch'
1..48 | ForEach-Object {
    $when = $now.AddDays(-$_ * 3.3)
    Pdf ("Documents\Receipts\Receipt {0} {1}.pdf" -f $when.ToString('yyyy-MM-dd'), $shops[$_ % $shops.Count]) 'Receipt' @($shops[$_ % $shops.Count]) ($_ * 3.3)
}
foreach ($name in 'Shakshuka', 'Lemon tart', 'Slow ragù', 'Granola', 'Dal') { Put "Documents\Recipes\$name.md" "# $name`n`n- ingredients`n- method`n" -days (30 + $rng.Next(0, 600)) | Out-Null }
Docx 'Documents\Résumé\CV 2025.docx' @('Studio Aurora', 'Designer and developer', 'Eight years of work for small brands.') 20
Pdf 'Documents\Résumé\CV 2025.pdf' 'CV 2025' @('Studio Aurora') 20
Xlsx 'Documents\Budget 2025.xlsx' @(@('Month', 'Income', 'Costs'), @('January', 4200, 2100), @('February', 3900, 2050), @('March', 5100, 2300)) 12
Docx 'Documents\Contracts & Legal\Freelance template.docx' @('Freelance agreement', 'Template, reviewed by a lawyer') 400

# ---------------------------------------------------------------- Videos

Put 'Videos\Showreel 2025.mp4' -bytes 85MB -days 16 | Out-Null
Put 'Videos\Interview raw.mov' -bytes 34MB -days 38 | Out-Null
Put 'Videos\Timelapse studio.mp4' -bytes 12MB -days 210 | Out-Null
1..4 | ForEach-Object { Put "Videos\Exports\Social cut $_.mp4" -bytes 3MB -days (10 + $_ * 6) | Out-Null }
Put 'Videos\Exports\Subtitles.srt' "1`n00:00:01,000 --> 00:00:03,000`nStudio Aurora`n" -days 11 | Out-Null

# ---------------------------------------------------------------- Inbox, the mess

MakeDir 'Inbox'
Put 'Inbox\Setup-DesignTool-3.2.msi' -bytes 6MB -days 0.5 | Out-Null
Put 'Inbox\report.pdf' -bytes 18000 -days 0.2 | Out-Null
Put 'Inbox\report (1).pdf' -bytes 18000 -days 0.2 | Out-Null
Put 'Inbox\report (2).pdf' -bytes 18400 -days 0.1 | Out-Null
Put 'Inbox\document.pdf' -bytes 52000 -days 1.3 | Out-Null
Put 'Inbox\untitled.txt' "" -days 1.1 | Out-Null
Put 'Inbox\New Text Document.txt' "to do`n" -days 2.4 | Out-Null
Put 'Inbox\unconfirmed 481772.crdownload' -bytes 400000 -days 0.05 | Out-Null
Photo 'Inbox\IMG_20250314_182233.jpg' 1280 960 501 3.2
Photo 'Inbox\IMG_20250314_182240.jpg' 1280 960 502 3.2
Put 'Inbox\fonts.zip' -bytes 90000 -days 4 | Out-Null
Put 'Inbox\~$Budget 2025.xlsx' -bytes 165 -days 0.1 | Out-Null
(Get-Item -LiteralPath (Full 'Inbox\~$Budget 2025.xlsx') -Force).Attributes = 'Hidden'
Put 'Inbox\Thumbs.db' -bytes 4096 -days 40 | Out-Null
(Get-Item -LiteralPath (Full 'Inbox\Thumbs.db') -Force).Attributes = 'Hidden, System'
Photo 'Inbox\Screenshot 2025-03-14 091122.png' 1440 900 503 7.5 'screen'

# ---------------------------------------------------------------- Archive, real zips

Zip 'Archive\Projects 2022.zip' {
    Put 'client-a\Brand\logo.svg' '<svg xmlns="http://www.w3.org/2000/svg"/>' -days 1200 | Out-Null
    Put 'client-a\Brand\notes.md' "# Client A`n" -days 1200 | Out-Null
    Put 'client-a\Web\index.html' '<h1>Client A</h1>' -days 1180 | Out-Null
    Put 'client-b\Packaging\dieline.pdf' -bytes 24000 -days 1100 | Out-Null
    Put 'client-b\Packaging\artwork\front.png' -bytes 40000 -days 1090 | Out-Null
    Put 'client-b\Packaging\artwork\back.png' -bytes 41000 -days 1090 | Out-Null
    Put 'README.txt' "Projects of 2022, archived.`n" -days 1000 | Out-Null
} 1000
Zip 'Archive\Photos Backup 2021.zip' {
    1..6 | ForEach-Object { Photo "2021\Trip\IMG_$('{0:D4}' -f (900 + $_)).jpg" 800 533 (600 + $_) (1500 + $_) }
    Put '2021\notes.txt' "Backup of the 2021 pictures.`n" -days 1400 | Out-Null
} 1400
Zip 'Archive\site-export (2023-11).zip' {
    Put 'wp-content\uploads\2023\11\header.jpg' -bytes 30000 -days 800 | Out-Null
    Put 'wp-content\themes\aurora\style.css' "/* Theme */`n" -days 810 | Out-Null
    Put 'database.sql' -bytes 120000 -days 800 | Out-Null
} 800
Zip 'Archive\Backups\backup-2025-01-01.zip' {
    Put 'Documents\Budget.xlsx' -bytes 9000 -days 300 | Out-Null
    Put 'Documents\Notes.md' "# Notes`n" -days 300 | Out-Null
    # An archive in an archive.
    $inner = Join-Path $script:Base 'inner'
    New-Item -ItemType Directory $inner | Out-Null
    [IO.File]::WriteAllText((Join-Path $inner 'deep.txt'), "inside two archives`n")
    [IO.Compression.ZipFile]::CreateFromDirectory($inner, (Join-Path $script:Base 'Nested archive.zip'))
    Remove-Item -LiteralPath $inner -Recurse
} 300
Put 'Archive\Old laptop.7z' -bytes 700000 -days 1600 | Out-Null
Put 'Archive\2019 invoices.rar' -bytes 220000 -days 2200 | Out-Null

# ---------------------------------------------------------------- Drop zones

Put 'Drop zones\Shared with team\Invoice 2025-014.pdf' -bytes 9000 -days 50 | Out-Null      # Same name as a client invoice: the conflict dialog.
Docx 'Drop zones\Shared with team\Brief v2 (final).docx' @('An older copy, to see the conflict dialog.') 160
Put 'Drop zones\Shared with team\Team calendar.xlsx' -bytes 7000 -days 15 | Out-Null
MakeDir 'Drop zones\To sort'
MakeDir 'Drop zones\Send to the printer'
MakeDir 'Drop zones\Archive 2025'
MakeDir 'Drop zones\Copy here (empty)'
Put 'Drop zones\Locked\Do not overwrite.txt' "Read-only file for the overwrite dialog.`n" -days 30 | Out-Null
(Get-Item -LiteralPath (Full 'Drop zones\Locked\Do not overwrite.txt')).Attributes = 'ReadOnly'

# ---------------------------------------------------------------- Design review, with labels

$dr = 'Design review'
Pdf "$dr\Brief - Atelier Lumière.pdf" 'Brief, Atelier Lumière' @('Rebrand of the shop and the web site.', 'First round due on Friday.') -days 2
Photo "$dr\Hero banner v3.jpg" 1600 900 31 -days 3
Photo "$dr\Hero banner v2.jpg" 1600 900 32 -days 9
Photo "$dr\Moodboard.jpg" 1600 1000 33 -days 12
Docx "$dr\Meeting notes.docx" @('Meeting notes', 'Atelier Lumière, kick-off', 'The logo stays, the palette changes.') -days 4
Xlsx "$dr\Budget.xlsx" @(@('Item', 'Days', 'Rate'), @('Brand guide', 6, 520), @('Web site', 12, 520), @('Print', 3, 480)) -days 5
Svg "$dr\Logo draft.svg" 'Lumière' 28 -days 6
Put "$dr\Copy deck.md" "# Copy deck`n`nHeadline, subhead and the three promises.`n" -days 7 | Out-Null
Pdf "$dr\Invoice 2025-021.pdf" 'Invoice 2025-021' @('Brand guide, first part.') -days 8
Pdf "$dr\Contract.pdf" 'Contract' @('Signed by both sides.') -days 20
Put "$dr\Receipt - printer.txt" "Printer, 3 proofs.`n" -days 10 | Out-Null

# The labels: named ones, colour-only ones, two on some items. They stay in the files.
Tag "$dr\Brief - Atelier Lumière.pdf" @(@(1, 'Urgent'), @(6, 'Client'))
Tag "$dr\Hero banner v3.jpg" @(@(4, 'Approved'), @(6, 'Client'))
Tag "$dr\Hero banner v2.jpg" @(@(3, 'Draft'))
Tag "$dr\Moodboard.jpg" @(@(4, 'Approved'))
Tag "$dr\Meeting notes.docx" @(@(2, 'To review'))
Tag "$dr\Budget.xlsx" @(@(1, 'Urgent'))
Tag "$dr\Logo draft.svg" @(@(3, 'Draft'), @(7, 'Print'))
Tag "$dr\Copy deck.md" @(@(2, 'To review'), @(6, 'Client'))
Tag "$dr\Invoice 2025-021.pdf" @(@(1, ''))
Tag "$dr\Contract.pdf" @(@(4, ''))
Tag "$dr\Receipt - printer.txt" @(@(3, ''))
Tag $dr @(@(2, ''))
Tag 'Clients\Atelier Lumière' @(@(6, 'Client'))
Tag 'Projects\aurora-site' @(@(2, ''), @(7, 'Print'))
Tag 'Photos\2024\Lisbon' @(@(5, 'Personal'))
Tag 'Documents\Receipts' @(@(3, ''))
Tag 'Welcome.md' @(@(7, ''))

if (-not $Lite) {
# ---------------------------------------------------------------- Edge cases

$edge = 'Edge cases'
MakeDir "$edge\A folder with a very long name to see how the middle ellipsis works when there is no room left for the text"
Put "$edge\A folder with a very long name to see how the middle ellipsis works when there is no room left for the text\a file whose name is as long as the folder that holds it, to cut in the middle too.md" -days 5 | Out-Null
Put "$edge\A folder with a very long name to see how the middle ellipsis works when there is no room left for the text\short.txt" -days 5 | Out-Null

# Names: other scripts, emoji, symbols, case, dots, spaces.
Put "$edge\Names\日本語のフォルダー\写真 001.txt" -days 20 | Out-Null
Put "$edge\Names\Dossier été — 2025 (À trier)\Facture n°1 – œuvre.txt" -days 20 | Out-Null
Put "$edge\Names\🎬 Video project 🎞️\Scene 1 🎥.txt" -days 20 | Out-Null
Put "$edge\Names\Q&A #1 (final) [v2] 50% done\notes; draft, 'old'+new=@!~.txt" -days 20 | Out-Null
Put "$edge\Names\no-extension" "A file with no extension.`n" -days 20 | Out-Null
Put "$edge\Names\archive.tar.gz" -bytes 3000 -days 20 | Out-Null
Put "$edge\Names\.hidden-looking-config" "starts with a dot`n" -days 20 | Out-Null
Put "$edge\Names\UPPERCASE.TXT" -days 20 | Out-Null
Put "$edge\Names\mixedCase.Txt" -days 20 | Out-Null
Put "$edge\Names\file.with.many.dots.v1.2.3.final.txt" -days 20 | Out-Null
Put "$edge\Names\Name   with    several   spaces.txt" -days 20 | Out-Null

# Emptiness.
MakeDir "$edge\Empty\Empty folder"
MakeDir "$edge\Empty\Empty folder 2"
MakeDir "$edge\Empty\Folder with only folders\a\b\c"
Put "$edge\Empty\Folder with only an empty file\zero bytes.txt" "" -days 10 | Out-Null
Put "$edge\Empty\Zero bytes.dat" "" -days 10 | Out-Null

# Depth: twelve levels.
$deep = 'Deep nesting'
1..12 | ForEach-Object { $deep += '\Level {0:D2}' -f $_ }
Put "$edge\$deep\The bottom.txt" "Twelve levels down.`n" -days 15 | Out-Null
Put "$edge\Deep nesting\Level 01\Level 02\Level 03\notes at level 3.txt" -days 15 | Out-Null

# Hidden, system, read-only.
Put "$edge\Hidden and read-only\visible.txt" -days 7 | Out-Null
Put "$edge\Hidden and read-only\hidden.txt" -days 7 | Out-Null
Put "$edge\Hidden and read-only\hidden and system.dat" -bytes 1000 -days 7 | Out-Null
Put "$edge\Hidden and read-only\read-only.txt" -days 7 | Out-Null
MakeDir "$edge\Hidden and read-only\Hidden folder" 7
Put "$edge\Hidden and read-only\Hidden folder\inside a hidden folder.txt" -days 7 | Out-Null
(Get-Item -LiteralPath (Full "$edge\Hidden and read-only\hidden.txt")).Attributes = 'Hidden'
(Get-Item -LiteralPath (Full "$edge\Hidden and read-only\hidden and system.dat")).Attributes = 'Hidden, System'
(Get-Item -LiteralPath (Full "$edge\Hidden and read-only\read-only.txt")).Attributes = 'ReadOnly'
(Get-Item -LiteralPath (Full "$edge\Hidden and read-only\Hidden folder") -Force).Attributes = 'Hidden, Directory'

# Shortcuts and links.
Lnk "$edge\Shortcuts and links\To Atelier Lumière.lnk" (Full 'Clients\Atelier Lumière') 14
Lnk "$edge\Shortcuts and links\To Welcome.lnk" (Full 'Welcome.md') 14
Lnk "$edge\Shortcuts and links\To Photos.lnk" (Full 'Photos') 14
Put "$edge\Shortcuts and links\temporary target.txt" -days 14
$gone = $script:LastPut
Lnk "$edge\Shortcuts and links\Broken shortcut.lnk" $gone 14
Remove-Item -LiteralPath $gone
try {
    New-Item -ItemType Junction -Path (Full "$edge\Shortcuts and links\Junction to Music") -Target (Full 'Music') | Out-Null
} catch { Write-Host "No junction made: $($_.Exception.Message)" }
try {
    New-Item -ItemType SymbolicLink -Path (Full "$edge\Shortcuts and links\Symlink to Welcome.md") -Target (Full 'Welcome.md') | Out-Null
} catch { Write-Host 'No symbolic link made (needs Developer Mode or an administrator), skipped.' }

# Sizes, from nothing to big, for the Size grouping.
Put "$edge\Sizes\0 bytes.bin" "" -days 30 | Out-Null
Put "$edge\Sizes\1 byte.bin" -bytes 1 -days 30 | Out-Null
Put "$edge\Sizes\1 KB.bin" -bytes 1KB -days 30 | Out-Null
Put "$edge\Sizes\120 KB.bin" -bytes 120KB -days 30 | Out-Null
Put "$edge\Sizes\900 KB.bin" -bytes 900KB -days 30 | Out-Null
Put "$edge\Sizes\6 MB.bin" -bytes 6MB -days 30 | Out-Null
Put "$edge\Sizes\60 MB.bin" -bytes 60MB -days 30 | Out-Null
Put "$edge\Sizes\400 MB.bin" -bytes 400MB -days 30 | Out-Null

# Dates, from this hour to five years, for the Date grouping.
$ages = @{ 'an hour ago' = 0.04; 'this morning' = 0.3; 'yesterday' = 1.2; '3 days ago' = 3; 'last week' = 9; 'three weeks ago' = 21; '2 months ago' = 62; 'last year' = 400; 'two years ago' = 800; 'five years ago' = 1900 }
foreach ($key in $ages.Keys) { Put "$edge\Dates\Modified $key.txt" "$key`n" -days $ages[$key] | Out-Null }

# One of every kind, for the Type grouping and the icons.
foreach ($extension in 'txt', 'md', 'json', 'csv', 'html', 'xml', 'ps1', 'py', 'js', 'css', 'log', 'ini', 'bat') {
    Put "$edge\Types\sample.$extension" "sample $extension`n" -days 20 | Out-Null
}
Pdf "$edge\Types\sample.pdf" 'Sample PDF' @('A real page') 20
Docx "$edge\Types\sample.docx" @('Sample Word document') 20
Xlsx "$edge\Types\sample.xlsx" @(@('A', 'B'), @(1, 2)) 20
Photo "$edge\Types\sample.jpg" 640 427 901 20
Photo "$edge\Types\sample.png" 640 427 902 20 'screen'
Wav "$edge\Types\sample.wav" 440 2 20
foreach ($extension in 'mp3', 'mp4', 'mkv', 'iso', 'dll', 'exe', 'ttf', 'psd') { Put "$edge\Types\sample.$extension" -bytes 8000 -days 20 | Out-Null }
Svg "$edge\Types\sample.svg" 'SVG' 200 20

# The order of names: natural sort and type to jump.
foreach ($n in 1, 2, 3, 10, 11, 20, 100, 101) { Put "$edge\Natural sort\file $n.txt" -days 5 | Out-Null }
foreach ($name in 'apple', 'apricot', 'avocado', 'banana', 'blueberry', 'cherry', 'clementine', 'cranberry', 'date', 'fig', 'grape', 'guava') {
    Put "$edge\Type to jump\$name.txt" -days 5 | Out-Null
}

# Several items to rename at once, and conflicts with the Drop zones.
foreach ($n in 1..6) { Put "$edge\Rename these\holiday photo $n.txt" -days 3 | Out-Null }
Put "$edge\Conflicts\Invoice 2025-014.pdf" -bytes 14000 -days 2 | Out-Null
Put "$edge\Conflicts\Team calendar.xlsx" -bytes 11000 -days 2 | Out-Null
Put "$edge\Conflicts\Do not overwrite.txt" "A newer copy.`n" -days 1 | Out-Null

# The details pane: a Markdown file with everything the pane draws, code and data files, a PDF,
# a picture over the 40 megapixels that open by themselves, a sound, and a folder to size.
$pane = "$edge\Pane previews"
$markdown = @'
# Details pane, Markdown

A paragraph with **bold**, *italic*, `inline code` and a [link](https://github.com/AristideBH/windhawk-lib).

## A list

- one
- two
  - nested
- three

1. first
2. second

> A quote, set apart with a bar.

---

```
code block
  kept as it is
```

| a | b |
| - | - |
| 1 | 2 |
'@
Put "$pane\Rich.md" $markdown -days 3 | Out-Null
Put "$pane\sample.cpp" "#include <cstdio>`n`n// A small file to see the text preview.`nint main() {`n    std::puts(`"hello`");`n    return 0;`n}`n" -days 4 | Out-Null
Put "$pane\sample.json" "{`n  `"name`": `"Explorer Extras`",`n  `"areas`": [`"Columns`", `"Details pane`"],`n  `"beta`": true`n}`n" -days 4 | Out-Null
Pdf "$pane\Two pages of nothing.pdf" 'Pane preview' @('The PDF is drawn by the pane.', 'It scrolls with the wheel.') -days 6
Wav "$pane\Tone for the player.wav" 330 4 7
Photo "$pane\Normal picture.jpg" 2400 1600 11 8
$bigPath = Full "$pane\Huge picture 42 megapixels.jpg"
[IO.Directory]::CreateDirectory((Split-Path $bigPath)) | Out-Null
$bitmap = New-Object Drawing.Bitmap 7000, 6000
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$rectangle = New-Object Drawing.Rectangle 0, 0, 7000, 6000
$gradient = New-Object Drawing.Drawing2D.LinearGradientBrush $rectangle, ([Drawing.Color]::FromArgb(255, 20, 60, 140)), ([Drawing.Color]::FromArgb(255, 250, 170, 80)), 35.0
$graphics.FillRectangle($gradient, $rectangle)
foreach ($i in 1..60) {
    $spot = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(70, $rng.Next(255), $rng.Next(255), $rng.Next(255)))
    $graphics.FillEllipse($spot, $rng.Next(0, 6000), $rng.Next(0, 5000), $rng.Next(300, 1500), $rng.Next(300, 1500))
    $spot.Dispose()
}
$bitmap.Save($bigPath, [Drawing.Imaging.ImageFormat]::Jpeg)
$gradient.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
Stamp $bigPath 9
$script:Count++
Put "$pane\Folder to size\a.txt" "a`n" -days 10 | Out-Null
foreach ($n in 1..40) { Put ("$pane\Folder to size\{0}\file-{1:D2}.{2}" -f (('docs', 'images', 'code')[$n % 3]), $n, (('pdf', 'jpg', 'cpp', 'txt')[$n % 4])) -bytes ($n * 7KB) -days ($n + 10) | Out-Null }

# A big folder, and with -Stress one over the limit of the mod.
foreach ($n in 1..$Huge) { Put ("$edge\Huge folder ($Huge files)\photo-{0:D4}.jpg" -f $n) -bytes 1KB -days ($n % 600 + 1) | Out-Null }
if ($Stress) {
    foreach ($n in 1..6000) { Put ("$edge\Over the limit (6000 files)\item-{0:D4}.txt" -f $n) "x" -days ($n % 900 + 1) | Out-Null }
}

}  # -not $Lite

# ---------------------------------------------------------------- labels all through the folder

# Every folder of the showcase gets its share of labels, each kind of work its own habits: about a third of the files and half of the
# folders, one label mostly, two or three now and then. The big generated folders and the links are left alone.
$pools = @{
    'Clients'   = @(@(6, 'Client'), @(1, 'Urgent'), @(4, 'Approved'), @(2, 'To review'), @(6, ''))
    'Projects'  = @(@(3, 'Draft'), @(2, 'To review'), @(4, 'Approved'), @(5, ''), @(7, 'Print'))
    'Photos'    = @(@(5, 'Personal'), @(4, 'Approved'), @(3, ''), @(2, ''), @(7, ''))
    'Music'     = @(@(7, ''), @(4, ''), @(5, 'Personal'), @(3, ''))
    'Documents' = @(@(1, 'Urgent'), @(7, 'Print'), @(3, ''), @(6, 'Client'), @(4, 'Approved'))
    'Inbox'     = @(@(2, 'To review'), @(1, 'Urgent'), @(0, ''), @(3, 'Draft'))
    'Archive'   = @(@(0, ''), @(4, 'Approved'), @(6, ''))
    'Videos'    = @(@(3, 'Draft'), @(6, 'Client'), @(4, 'Approved'), @(7, ''))
}
$general = @(@(1, 'Urgent'), @(2, 'To review'), @(3, 'Draft'), @(4, 'Approved'), @(5, 'Personal'), @(6, 'Client'), @(7, 'Print'), @(0, ''))
$skip = @('*\Huge folder*', '*\Over the limit*', '*Junction to Music*', '*\Pane previews\Folder to size*')
$labelled = 0
foreach ($entry in (Get-ChildItem -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue)) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
    $rel = $entry.FullName.Substring($root.Length).TrimStart('\')
    if ($script:Tagged.ContainsKey($rel)) { continue }
    $skipIt = $false
    foreach ($pattern in $skip) { if ($entry.FullName -like $pattern) { $skipIt = $true } }
    if ($skipIt) { continue }
    $chance = if ($entry.PSIsContainer) { 0.5 } else { 0.32 }
    if ($rng.NextDouble() -ge $chance) { continue }
    $pool = $pools[($rel -split '\\')[0]]
    if (-not $pool) { $pool = $general }
    $roll = $rng.NextDouble()
    $howMany = if ($roll -lt 0.72) { 1 } elseif ($roll -lt 0.94) { 2 } else { 3 }
    $picked = @()
    $order = $pool | Sort-Object { $rng.Next() }
    foreach ($tag in ($order | Select-Object -First $howMany)) { $picked += , $tag }
    try { Tag $rel $picked; $labelled++ } catch { }
}
Write-Host "Labels put on $labelled more items."

# ---------------------------------------------------------------- done

# Folder dates last, since adding files changed them.
Get-ChildItem -LiteralPath $root -Recurse -Directory -Force | Where-Object { $_.FullName -notlike '*Junction to Music*' } |
    ForEach-Object { Stamp $_.FullName ($rng.Next(2, 240)) }

$total = $script:Count
$size = (Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notlike '*Junction to Music*' } | Measure-Object Length -Sum).Sum
Write-Host ("Built {0}: {1} files, {2:N0} MB, in {3:N0} seconds." -f $root, $total, ($size / 1MB), ((Get-Date) - $started).TotalSeconds)
