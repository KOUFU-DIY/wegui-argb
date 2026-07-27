# make_demo_assets.ps1 — 从素材库生成 demo 角色图（可重跑，输出覆盖）
#
# 来源记录 v2（2026-07-26 用户精选；换图改这里再重跑即可）：
#   demo_raw    <- G:\gif\128x64 测试\RGB1 (2854).png                          128x64 原样复制
#   demo_cat    <- G:\gif\128x160动画素材包\a (50).png                          128x160 原样复制
#   demo_sprite <- G:\gif\4\403.png（128x160 恰为 4:5）                         整幅缩放 64x80
#   demo_qoi    <- G:\gif\240x320动画素材包\一键三连-略nd\1211.png              裁 240x135(y=80) 缩 96x54
#   demo_alpha   <- E:\OneDrive\Project\MS\Program\toy\疯狂动物城\开心29.png      80x80 原生透明，原样复制
#   demo_overlay <- E:\OneDrive\Project\MS\Program\toy\疯狂动物城\害羞_00045.png  80x80 原生透明，原样复制
#   备用未使用：G:\gif\5\532.png
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$res = Split-Path $PSScriptRoot -Parent

foreach ($d in @("$res\input2raw", "$res\input2indexqoi", "$res\alpha_indexqoi")) {
    New-Item -ItemType Directory -Force $d | Out-Null
}

# 裁切 + 缩放 + 模式：
#   none   裁切后缩放
#   circle 圆形透明蒙版；round 圆角透明蒙版（12px 圆角）
#   fit    不裁切：等比缩放至完整放入 OutW x OutH，居中，四周透明（保留原生 alpha）
function Convert-Asset {
    param(
        [string]$Src, [int]$CropW, [int]$CropH, [int]$CropY,  # CropY = -1 表示垂直居中
        [int]$OutW, [int]$OutH, [string]$Mask, [string]$Dst
    )
    $img = [System.Drawing.Image]::FromFile($Src)
    try {
        if ($Mask -eq 'fit') {
            $scale = [Math]::Min($OutW / $img.Width, $OutH / $img.Height)
            $w = [int][Math]::Round($img.Width * $scale)
            $h = [int][Math]::Round($img.Height * $scale)
            $bmp = New-Object System.Drawing.Bitmap($OutW, $OutH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.DrawImage($img, [int](($OutW - $w) / 2), [int](($OutH - $h) / 2), $w, $h)
            $g.Dispose()
            $bmp.Save($Dst, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Dispose()
            Write-Output ("OK  {0}  {1}x{2} (fit)" -f (Split-Path $Dst -Leaf), $OutW, $OutH)
            return
        }

        $cx = [int](($img.Width - $CropW) / 2)
        $cy = if ($CropY -lt 0) { [int](($img.Height - $CropH) / 2) } else { $CropY }

        # 第一步：裁切区域缩放到目标尺寸的临时位图
        $tmp = New-Object System.Drawing.Bitmap($OutW, $OutH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $tg = [System.Drawing.Graphics]::FromImage($tmp)
        $tg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $dstRect = New-Object System.Drawing.Rectangle(0, 0, $OutW, $OutH)
        $srcRect = New-Object System.Drawing.Rectangle($cx, $cy, $CropW, $CropH)
        $tg.DrawImage($img, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
        $tg.Dispose()

        if ($Mask -eq 'none') {
            $tmp.Save($Dst, [System.Drawing.Imaging.ImageFormat]::Png)
            $tmp.Dispose()
        }
        else {
            # 第二步：用路径填充纹理画刷，得到抗锯齿的透明边缘
            $bmp = New-Object System.Drawing.Bitmap($OutW, $OutH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $path = New-Object System.Drawing.Drawing2D.GraphicsPath
            if ($Mask -eq 'circle') {
                $path.AddEllipse(0, 0, $OutW - 1, $OutH - 1)
            }
            else {
                $r = 12; $d = 2 * $r
                $path.AddArc(0, 0, $d, $d, 180, 90)
                $path.AddArc($OutW - 1 - $d, 0, $d, $d, 270, 90)
                $path.AddArc($OutW - 1 - $d, $OutH - 1 - $d, $d, $d, 0, 90)
                $path.AddArc(0, $OutH - 1 - $d, $d, $d, 90, 90)
                $path.CloseFigure()
            }
            $brush = New-Object System.Drawing.TextureBrush($tmp)
            $g.FillPath($brush, $path)
            $brush.Dispose(); $path.Dispose(); $g.Dispose()
            $bmp.Save($Dst, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Dispose(); $tmp.Dispose()
        }
        Write-Output ("OK  {0}  {1}x{2}" -f (Split-Path $Dst -Leaf), $OutW, $OutH)
    }
    finally { $img.Dispose() }
}

# 原样复制的三张
Copy-Item -LiteralPath 'G:\gif\128x64 测试\RGB1 (2854).png' "$res\input2raw\demo_raw.png" -Force
Write-Output 'OK  demo_raw.png  128x64 (copy)'
Copy-Item -LiteralPath 'G:\gif\128x160动画素材包\a (50).png' "$res\input2indexqoi\demo_cat.png" -Force
Write-Output 'OK  demo_cat.png  128x160 (copy)'
Copy-Item -LiteralPath 'E:\OneDrive\Project\MS\Program\toy\疯狂动物城\开心29.png' "$res\alpha_indexqoi\demo_alpha.png" -Force
Write-Output 'OK  demo_alpha.png  80x80 (copy, native alpha)'
Copy-Item -LiteralPath 'E:\OneDrive\Project\MS\Program\toy\疯狂动物城\害羞_00045.png' "$res\alpha_indexqoi\demo_overlay.png" -Force
Write-Output 'OK  demo_overlay.png  80x80 (copy, native alpha)'

# 加工的两张
Convert-Asset -Src 'G:\gif\4\403.png' -CropW 128 -CropH 160 -CropY 0 `
              -OutW 64 -OutH 80 -Mask 'none' -Dst "$res\input2raw\demo_sprite.png"
Convert-Asset -Src 'G:\gif\240x320动画素材包\一键三连-略nd\1211.png' -CropW 240 -CropH 135 -CropY 80 `
              -OutW 96 -OutH 54 -Mask 'none' -Dst "$res\input2indexqoi\demo_qoi.png"

Write-Output 'DONE.'
