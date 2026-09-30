# generate_assets.ps1
# Generates the placeholder PNG library for ModiPAD_custom (v2 - 20 gradients).
#
# Output: datadevice/images/{templates/{gradients,patterns,solid},buttons/<WxH>,pages,system,icons}
#
# PNG is lossless, so GDI+ ignores any "compression" parameter; for smaller
# files run scripts\optimize_images.bat (OptiPNG) after generation.
#
# Usage:  pwsh -File tools/utils/generate_assets.ps1

Add-Type -AssemblyName System.Drawing

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$baseDir = Join-Path $root "datadevice/images"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Save-Png($bmp, [string]$path) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# direction: vertical | horizontal | diagonal (45 deg)
function Create-Gradient([string]$path, [int]$w, [int]$h, $start, $end, [string]$direction = "vertical") {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
    $angle = 90
    if ($direction -eq "horizontal") { $angle = 0 }
    elseif ($direction -eq "diagonal") { $angle = 45 }
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $start, $end, $angle)
    $g.FillRectangle($brush, $rect)
    $brush.Dispose()
    $g.Dispose()
    Save-Png $bmp $path
}

function Create-Solid([string]$path, [int]$w, [int]$h, $color) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear($color)
    $g.Dispose()
    Save-Png $bmp $path
}

function Create-Noise([string]$path, [int]$w, [int]$h, [int]$baseR, [int]$baseG, [int]$baseB, [int]$spread, [int]$seed = 1234) {
    $rnd = New-Object System.Random $seed
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $n = $rnd.Next(-$spread, $spread)
            $r = [Math]::Max(0, [Math]::Min(255, $baseR + $n))
            $gg = [Math]::Max(0, [Math]::Min(255, $baseG + $n))
            $b = [Math]::Max(0, [Math]::Min(255, $baseB + $n))
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($r, $gg, $b))
        }
    }
    Save-Png $bmp $path
}

function Create-Wood([string]$path, [int]$w, [int]$h) {
    $rnd = New-Object System.Random 4321
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    for ($y = 0; $y -lt $h; $y++) {
        $band = [int]([Math]::Sin($y * 0.08) * 20)
        $rb = 150 + $band
        $gb = 95 + [int]($band * 0.6)
        $bb = 45 + [int]($band * 0.3)
        for ($x = 0; $x -lt $w; $x++) {
            $n = $rnd.Next(-10, 10)
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(
                [Math]::Max(0, [Math]::Min(255, $rb + $n)),
                [Math]::Max(0, [Math]::Min(255, $gb + $n)),
                [Math]::Max(0, [Math]::Min(255, $bb + $n))))
        }
    }
    Save-Png $bmp $path
}

function Create-Carbon([string]$path, [int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear(([System.Drawing.Color]::FromArgb(24, 26, 30)))
    $cell = 8
    for ($y = 0; $y -lt $h; $y += $cell) {
        for ($x = 0; $x -lt $w; $x += $cell) {
            $isDark = ((($x / $cell) + ($y / $cell)) % 2) -eq 0
            $c = if ($isDark) { [System.Drawing.Color]::FromArgb(30, 32, 36) } else { [System.Drawing.Color]::FromArgb(52, 56, 62) }
            $brush = New-Object System.Drawing.SolidBrush $c
            $g.FillRectangle($brush, $x, $y, $cell - 1, $cell - 1)
            $brush.Dispose()
        }
    }
    $g.Dispose()
    Save-Png $bmp $path
}

function Create-Icon([string]$path, [int]$size, $bg, [string]$glyph, $fg) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $pad = [int]($size * 0.06)
    $brush = New-Object System.Drawing.SolidBrush $bg
    $g.FillEllipse($brush, $pad, $pad, ($size - 2 * $pad), ($size - 2 * $pad))
    $brush.Dispose()
    $font = [System.Drawing.Font]::new("Segoe UI", [float]($size * 0.36), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $fgBrush = New-Object System.Drawing.SolidBrush $fg
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rectF = [System.Drawing.RectangleF]::new(0, 0, [float]$size, [float]$size)
    $g.DrawString($glyph, $font, $fgBrush, $rectF, $sf)
    $font.Dispose(); $fgBrush.Dispose(); $g.Dispose()
    Save-Png $bmp $path
}

# White glyphs (transparent background) for hotkey buttons, 64x64.
function New-ButtonIcon([string]$path, [string]$name) {
    $size = 64
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $w = [System.Drawing.Color]::White
    $pen = New-Object System.Drawing.Pen $w, 4
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $brush = New-Object System.Drawing.SolidBrush $w
    switch ($name) {
        "copy"     { $g.DrawRectangle($pen, 22, 20, 28, 28); $g.DrawRectangle($pen, 14, 12, 28, 28) }
        "paste"    { $g.DrawRectangle($pen, 16, 16, 32, 38); $g.FillRectangle($brush, 26, 8, 12, 10) }
        "save"     { $g.DrawRectangle($pen, 14, 14, 36, 36); $g.DrawRectangle($pen, 22, 20, 20, 12); $g.FillRectangle($brush, 22, 40, 20, 8) }
        "play"     { $g.FillPolygon($brush, @(([System.Drawing.Point]::new(22,16)), ([System.Drawing.Point]::new(48,32)), ([System.Drawing.Point]::new(22,48)))) }
        "stop"     { $g.FillRectangle($brush, 20, 20, 24, 24) }
        "undo"     { $g.DrawArc($pen, 14, 18, 36, 26, 10, 160); $g.FillPolygon($brush, @(([System.Drawing.Point]::new(10,26)), ([System.Drawing.Point]::new(24,18)), ([System.Drawing.Point]::new(24,34)))) }
        "redo"     { $g.DrawArc($pen, 14, 18, 36, 26, 10, 160); $g.FillPolygon($brush, @(([System.Drawing.Point]::new(54,26)), ([System.Drawing.Point]::new(40,18)), ([System.Drawing.Point]::new(40,34)))) }
        "zoom_in"  { $g.DrawEllipse($pen, 14, 14, 28, 28); $g.DrawLine($pen, 40, 40, 52, 52); $g.DrawLine($pen, 22, 28, 34, 28); $g.DrawLine($pen, 28, 22, 28, 34) }
        "zoom_out" { $g.DrawEllipse($pen, 14, 14, 28, 28); $g.DrawLine($pen, 40, 40, 52, 52); $g.DrawLine($pen, 22, 28, 34, 28) }
        "brush"    { $g.FillPolygon($brush, @(([System.Drawing.Point]::new(14,50)), ([System.Drawing.Point]::new(20,36)), ([System.Drawing.Point]::new(34,50)))); $g.DrawLine($pen, 30, 36, 48, 14) }
        "text"     { $g.DrawLine($pen, 18, 18, 46, 18); $g.DrawLine($pen, 32, 18, 32, 48) }
        "layers"   { $p1 = [System.Drawing.Point[]]@([System.Drawing.Point]::new(32,12),[System.Drawing.Point]::new(52,22),[System.Drawing.Point]::new(32,32),[System.Drawing.Point]::new(12,22)); $g.DrawPolygon($pen, $p1); $p2 = [System.Drawing.Point[]]@([System.Drawing.Point]::new(32,26),[System.Drawing.Point]::new(52,36),[System.Drawing.Point]::new(32,46),[System.Drawing.Point]::new(12,36)); $g.DrawPolygon($pen, $p2) }
        "export"   { $g.DrawRectangle($pen, 16, 34, 32, 16); $g.FillPolygon($brush, @(([System.Drawing.Point]::new(32,8)), ([System.Drawing.Point]::new(44,22)), ([System.Drawing.Point]::new(20,22)))); $g.DrawLine($pen, 32, 22, 32, 36) }
        "settings" { $g.DrawEllipse($pen, 24, 24, 16, 16); for ($i = 0; $i -lt 8; $i++) { $a = $i * [Math]::PI / 4; $x = 32 + [Math]::Cos($a) * 21; $y = 32 + [Math]::Sin($a) * 21; $g.FillEllipse($brush, [single]($x - 3), [single]($y - 3), 6, 6) } }
        "power"    { $g.DrawArc($pen, 16, 18, 32, 32, 130, 280); $g.DrawLine($pen, 32, 12, 32, 32) }
        default    { $g.DrawEllipse($pen, 20, 20, 24, 24) }
    }
    $pen.Dispose(); $brush.Dispose(); $g.Dispose()
    Save-Png $bmp $path
}

function C([int]$r, [int]$g, [int]$b) { [System.Drawing.Color]::FromArgb($r, $g, $b) }

# Transparent-background system icons (Bluetooth / WiFi), 32x32.
function New-SystemIcon([string]$path, [int]$size, $color, [string]$type) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $pen = New-Object System.Drawing.Pen $color, 2
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    if ($type -eq "bt") {
        $g.DrawLine($pen, 16, 4, 16, 28)
        $g.DrawLine($pen, 16, 4, 24, 11)
        $g.DrawLine($pen, 24, 11, 8, 22)
        $g.DrawLine($pen, 16, 28, 24, 21)
        $g.DrawLine($pen, 24, 21, 8, 10)
    } else {
        $cx = 16; $cy = 24
        foreach ($r in @(5, 10, 15)) {
            $g.DrawArc($pen, ($cx - $r), ($cy - $r), (2 * $r), (2 * $r), 205, 130)
        }
        $brush = New-Object System.Drawing.SolidBrush $color
        $g.FillEllipse($brush, ($cx - 2), ($cy - 2), 4, 4)
        $brush.Dispose()
    }
    $pen.Dispose(); $g.Dispose()
    Save-Png $bmp $path
}

# ---------------------------------------------------------------------------
# Definitions
# ---------------------------------------------------------------------------
$gradients = @(
    # Blues
    @{n="gradient_ocean_blue";  s=@(52,152,219);  e=@(41,128,185);  d="vertical"},
    @{n="gradient_deep_blue";   s=@(52,73,94);    e=@(44,62,80);    d="vertical"},
    @{n="gradient_sky";         s=@(135,206,250); e=@(70,130,180);  d="vertical"},
    @{n="gradient_blue_purple"; s=@(52,152,219);  e=@(155,89,182);  d="diagonal"},
    @{n="gradient_navy";        s=@(44,62,80);    e=@(52,152,219);  d="vertical"},
    # Greens
    @{n="gradient_emerald";     s=@(46,204,113);  e=@(39,174,96);   d="vertical"},
    @{n="gradient_forest";      s=@(34,139,34);   e=@(0,100,0);     d="vertical"},
    @{n="gradient_mint";        s=@(152,251,152); e=@(60,179,113);  d="vertical"},
    # Reds / oranges
    @{n="gradient_sunset";      s=@(255,94,98);   e=@(255,195,113); d="vertical"},
    @{n="gradient_fire";        s=@(255,69,0);    e=@(255,140,0);   d="vertical"},
    @{n="gradient_coral";       s=@(255,127,80);  e=@(255,99,71);   d="vertical"},
    @{n="gradient_red_blue";    s=@(255,107,107); e=@(78,205,196);  d="diagonal"},
    # Purples / pinks
    @{n="gradient_purple";      s=@(142,45,226);  e=@(74,0,224);    d="vertical"},
    @{n="gradient_violet";      s=@(138,43,226);  e=@(75,0,130);    d="vertical"},
    @{n="gradient_pink";        s=@(255,105,180); e=@(255,20,147);  d="vertical"},
    @{n="gradient_purple_pink"; s=@(142,45,226);  e=@(255,105,180); d="diagonal"},
    # Dark / neutral
    @{n="gradient_dark";        s=@(44,62,80);    e=@(26,26,26);    d="vertical"},
    @{n="gradient_charcoal";    s=@(64,64,64);    e=@(32,32,32);    d="vertical"},
    @{n="gradient_light";       s=@(236,240,241); e=@(189,195,199); d="vertical"},
    @{n="gradient_silver";      s=@(192,192,192); e=@(128,128,128); d="vertical"},
    # Legacy aliases (kept so existing config.json keeps working)
    @{n="gradient_blue";        s=@(52,152,219);  e=@(41,128,185);  d="vertical"},
    @{n="gradient_green";       s=@(46,204,113);  e=@(39,174,96);   d="vertical"},
    @{n="gradient_red";         s=@(231,76,60);   e=@(192,57,43);   d="vertical"},
    @{n="gradient_orange";      s=@(243,156,18);  e=@(230,126,34);  d="vertical"}
)

$solids = @(
    @{n="solid_blue";   c=@(52,152,219)},
    @{n="solid_green";  c=@(46,204,113)},
    @{n="solid_red";    c=@(231,76,60)},
    @{n="solid_orange"; c=@(243,156,18)},
    @{n="solid_purple"; c=@(155,89,182)},
    @{n="solid_dark";   c=@(26,26,26)},
    @{n="solid_light";  c=@(236,240,241)},
    @{n="solid_teal";   c=@(26,188,156)}
)

$patterns = @("wood", "carbon", "metal", "concrete")

$buttonSizes = @(
    @{w=100; h=100},
    @{w=70;  h=70}
)

$templateGradW = 512; $templateGradH = 512
$templatePatternSize = 256
$templateSolidSize = 512

# ---------------------------------------------------------------------------
# Generate
# ---------------------------------------------------------------------------
Write-Host "Generating assets under $baseDir ..."

# Gradients (button cache only; no templates/ - page backgrounds live in pages/)
foreach ($gr in $gradients) {
    foreach ($sz in $buttonSizes) {
        Create-Gradient (Join-Path $baseDir "buttons/$($sz.w)x$($sz.h)/$($gr.n).png") $sz.w $sz.h (C $gr.s[0] $gr.s[1] $gr.s[2]) (C $gr.e[0] $gr.e[1] $gr.e[2]) $gr.d
    }
}

# Solids (button cache only)
foreach ($so in $solids) {
    foreach ($sz in $buttonSizes) {
        Create-Solid (Join-Path $baseDir "buttons/$($sz.w)x$($sz.h)/$($so.n).png") $sz.w $sz.h (C $so.c[0] $so.c[1] $so.c[2])
    }
}

# Patterns (button cache only)
foreach ($p in $patterns) {
    foreach ($sz in $buttonSizes) {
        $dst = Join-Path $baseDir "buttons/$($sz.w)x$($sz.h)/pattern_$p.png"
        switch ($p) {
            "wood"     { Create-Wood $dst $sz.w $sz.h }
            "carbon"   { Create-Carbon $dst $sz.w $sz.h }
            "metal"    { Create-Noise $dst $sz.w $sz.h 150 150 158 44 77 }
            "concrete" { Create-Noise $dst $sz.w $sz.h 185 185 185 30 99 }
        }
    }
}

# Page backgrounds (480x320)
$pages = Join-Path $baseDir "pages"
Create-Gradient (Join-Path $pages "age_bg_dark.png") 480 320 (C 26 26 26) (C 44 62 80) "vertical"
Create-Gradient (Join-Path $pages "page_light.png") 480 320 (C 236 240 241) (C 189 195 199) "vertical"
Create-Gradient (Join-Path $pages "page_blue.png") 480 320 (C 52 152 219) (C 155 89 182) "diagonal"
Create-Gradient (Join-Path $pages "page_purple.png") 480 320 (C 142 45 226) (C 74 0 224) "vertical"
Create-Gradient (Join-Path $pages "page_green.png") 480 320 (C 46 204 113) (C 39 174 96) "vertical"
Create-Wood (Join-Path $pages "page_wood.png") 480 320
Create-Carbon (Join-Path $pages "page_carbon.png") 480 320

# System backgrounds (480x320)
$sys = Join-Path $baseDir "system"
Create-Gradient (Join-Path $sys "splash_bg.png") 480 320 (C 26 26 46) (C 22 33 62) "vertical"
Create-Gradient (Join-Path $sys "settings_bg.png") 480 320 (C 26 26 26) (C 44 62 80) "vertical"
Create-Gradient (Join-Path $sys "menu_bg.png") 480 320 (C 26 26 26) (C 22 33 62) "vertical"

# Icons - pages (64px)
Create-Icon (Join-Path $baseDir "icons/pages/icon_photoshop.png") 64 (C 0 136 204) "Ps" ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/pages/icon_premiere.png")  64 (C 153 0 204) "Pr" ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/pages/icon_vsc.png")       64 (C 0 122 204) "VS" ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/pages/icon_browser.png")   64 (C 52 152 219) "W"  ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/pages/icon_settings.png")  64 (C 52 73 94)  "S"   ([System.Drawing.Color]::White)

# Icons - actions (32px)
Create-Icon (Join-Path $baseDir "icons/actions/icon_copy.png")  32 (C 46 204 113) "C"  ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/actions/icon_paste.png") 32 (C 241 196 15) "P"  ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/actions/icon_save.png")  32 (C 52 152 219) "S"  ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/actions/icon_play.png")  32 (C 46 204 113) ">"  ([System.Drawing.Color]::White)
Create-Icon (Join-Path $baseDir "icons/actions/icon_stop.png")  32 (C 231 76 60)  "X"  ([System.Drawing.Color]::White)

# System icons (BT / WiFi, on = white, off = grey)
$sysIcons = Join-Path $baseDir "icons/system"
New-SystemIcon (Join-Path $sysIcons "icon_bt.png")      32 ([System.Drawing.Color]::White)                 "bt"
New-SystemIcon (Join-Path $sysIcons "icon_bt_off.png")  32 ([System.Drawing.Color]::FromArgb(102,102,102)) "bt"
New-SystemIcon (Join-Path $sysIcons "icon_wifi.png")    32 ([System.Drawing.Color]::White)                 "wifi"
New-SystemIcon (Join-Path $sysIcons "icon_wifi_off.png") 32 ([System.Drawing.Color]::FromArgb(102,102,102)) "wifi"

# Button icons (white glyph, transparent, 64px) for hotkey buttons.
# Only generate placeholders for icons that do not exist yet, so real icons
# (e.g. the Material Icons set copied into icons/buttons) are never overwritten.
$btnIcons = @("copy","paste","save","play","stop","undo","redo","zoom_in","zoom_out","brush","text","layers","export","settings","power")
foreach ($ic in $btnIcons) {
    $dst = Join-Path $baseDir "icons/buttons/$ic.png"
    if (-not (Test-Path $dst)) {
        New-ButtonIcon $dst $ic
    }
}

# Fonts placeholder directory
$fontsDir = Join-Path $root "datadevice/fonts"
if (-not (Test-Path $fontsDir)) { New-Item -ItemType Directory -Path $fontsDir -Force | Out-Null }

# ---------------------------------------------------------------------------
# Stats
# ---------------------------------------------------------------------------
$files = Get-ChildItem -Path $baseDir -Recurse -File
$total = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ""
Write-Host "========================================"
Write-Host "  GENERATION COMPLETE"
Write-Host "========================================"
Write-Host ("Files: {0}" -f $files.Count)
Write-Host ("Size:  {0} KB" -f [math]::Round($total / 1KB, 2))
Write-Host ""
Write-Host "Run scripts\\optimize_images.bat to shrink the PNGs with OptiPNG."
Write-Host "========================================"

