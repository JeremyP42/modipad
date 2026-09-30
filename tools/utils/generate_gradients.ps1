# generate_gradients.ps1
# Generates 64 gradient button backgrounds per size (70x70 and 100x100),
# named gradient_auto_001..064, with clearly DISTINCT colours.
#
# The button-background picker also ships 24 hand-picked gradients + 8 solids +
# 4 patterns = 36 files, so 64 generated gradients bring each size to exactly
# 100 background tiles. Existing (named) gradients are never touched.
#
# Distinctness: 16 base hues x 4 tone variants (dark / vivid / pastel / medium),
# so neighbouring tiles differ in hue (22.5 deg) and/or tone. Only "left to
# right" (horizontal) and "diagonal" directions are used, and the two stops of a
# tile stay in the same hue family (smooth gradient).
#
# Usage:  pwsh -File tools/utils/generate_gradients.ps1

Add-Type -AssemblyName System.Drawing

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$baseDir = Join-Path $root "datadevice/images/buttons"
$count = 70
$hues = 10

function Save-Png($bmp, [string]$path) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# h in [0,360), s/v in [0,1]
function Hsv([double]$h, [double]$s, [double]$v) {
    $c = $v * $s
    $x = $c * (1 - [math]::Abs((($h / 60) % 2) - 1))
    $m = $v - $c
    if ($h -lt 60)      { $r = $c; $g = $x; $b = 0 }
    elseif ($h -lt 120) { $r = $x; $g = $c; $b = 0 }
    elseif ($h -lt 180) { $r = 0;  $g = $c; $b = $x }
    elseif ($h -lt 240) { $r = 0;  $g = $x; $b = $c }
    elseif ($h -lt 300) { $r = $x; $g = 0;  $b = $c }
    else                { $r = $c; $g = 0;  $b = $x }
    [System.Drawing.Color]::FromArgb(
        [int](($r + $m) * 255), [int](($g + $m) * 255), [int](($b + $m) * 255))
}

function Create-Gradient([string]$path, [int]$w, [int]$h, $start, $end, [string]$direction) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
    $angle = 0
    if ($direction -eq "diagonal") { $angle = 45 }
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $start, $end, $angle)
    $g.FillRectangle($brush, $rect)
    $brush.Dispose()
    $g.Dispose()
    Save-Png $bmp $path
}

$sizes = @(@{w = 100; h = 100 }, @{w = 70; h = 70 })

Write-Host "Generating $count distinct gradients per size under $baseDir ..."
$made = 0
for ($i = 1; $i -le $count; $i++) {
    $tone = [int][math]::Floor(($i - 1) / $hues)   # 0..3
    $hi = ($i - 1) % $hues
    $h1 = ($hi * (360.0 / $hues) + 7.5) % 360

    switch ($tone) {
        0 { $s1 = 0.95; $v1 = 0.25 }  # very dark
        1 { $s1 = 1.00; $v1 = 0.45 }  # dark vivid
        2 { $s1 = 0.95; $v1 = 0.68 }  # deep
        3 { $s1 = 1.00; $v1 = 0.92 }  # rich
        4 { $s1 = 0.60; $v1 = 0.95 }  # soft
        5 { $s1 = 0.42; $v1 = 0.96 }  # light
        default { $s1 = 0.26; $v1 = 0.97 } # pastel
    }
    # Smooth within-tile gradient: second stop stays near the same hue.
    $h2 = ($h1 + 18) % 360
    $s2 = [math]::Min(1.0, $s1 + 0.05)
    $v2 = if ($tone -lt 4) { [math]::Min(1.0, $v1 + 0.14) } else { [math]::Max(0.0, $v1 - 0.12) }
    $start = Hsv $h1 $s1 $v1
    $end = Hsv $h2 $s2 $v2
    $dir = if ($i % 2 -eq 0) { "horizontal" } else { "diagonal" }
    $name = "gradient_auto_{0:d3}.png" -f $i
    foreach ($sz in $sizes) {
        Create-Gradient (Join-Path $baseDir "$($sz.w)x$($sz.h)/$name") $sz.w $sz.h $start $end $dir
        $made++
    }
}
Write-Host "Done: $made files written."

$oxi = Join-Path $root "tools/oxipng/oxipng.exe"
if (Test-Path $oxi) {
    Write-Host "Optimising with Oxipng ..."
    & $oxi --quiet -o 3 "$baseDir\100x100\gradient_auto_*.png" "$baseDir\70x70\gradient_auto_*.png" 2>$null
}

$files = Get-ChildItem -Path $baseDir -Recurse -Filter "gradient_auto_*.png" -File
$total = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ("gradient_auto files: {0}, total {1:N1} KB" -f $files.Count, ($total / 1KB))
