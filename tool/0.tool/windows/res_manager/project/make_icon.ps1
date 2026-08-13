# 生成 1024x1024 应用图标源图 icon-src.png（与界面 Logo 同款像素网格设计）
Add-Type -AssemblyName System.Drawing

function New-RoundRectPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

$size = 1024
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

# 底板
$bgPath = New-RoundRectPath 32 32 960 960 224
$bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    (New-Object System.Drawing.Point(0, 0)),
    (New-Object System.Drawing.Point(0, $size)),
    [System.Drawing.Color]::FromArgb(255, 26, 34, 46),
    [System.Drawing.Color]::FromArgb(255, 15, 20, 28))
$g.FillPath($bgBrush, $bgPath)
$pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 52, 66, 86), 10)
$g.DrawPath($pen, $bgPath)

# 2x2 网格
$cell = 268; $gap = 72
$x0 = ($size - 2 * $cell - $gap) / 2
$y0 = $x0
$colors = @(
    [System.Drawing.Color]::FromArgb(255, 56, 189, 248),  # 左上 cyan
    [System.Drawing.Color]::FromArgb(255, 38, 51, 66),    # 右上 暗
    [System.Drawing.Color]::FromArgb(255, 38, 51, 66),    # 左下 暗
    [System.Drawing.Color]::FromArgb(255, 52, 211, 153)   # 右下 green
)
for ($i = 0; $i -lt 4; $i++) {
    $cx = $x0 + ($i % 2) * ($cell + $gap)
    $cy = $y0 + [math]::Floor($i / 2) * ($cell + $gap)
    $cp = New-RoundRectPath $cx $cy $cell $cell 56
    $b = New-Object System.Drawing.SolidBrush($colors[$i])
    $g.FillPath($b, $cp)
    $b.Dispose(); $cp.Dispose()
}

$g.Dispose()
$out = Join-Path $PSScriptRoot 'icon-src.png'
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved: $out"
