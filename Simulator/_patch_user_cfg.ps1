$path = Join-Path $PSScriptRoot '..\we_user_config.h'
$text = [System.IO.File]::ReadAllText($path)

$anchor = "#define WE_CFG_DEBUG_DIRTY_RECT (0)"
$insert = "`r`n`r`n/* 控件性能压测开关`r`n * 0: 关闭，正常按需重绘`r`n * 1: 打开，每帧强制标脏所有控件，持续全量重绘以压测控件渲染性能 */`r`n#define WE_CFG_DEBUG_PERF_STRESS (0)"

if ($text -match [regex]::Escape("WE_CFG_DEBUG_PERF_STRESS")) {
    Write-Output "ALREADY_PRESENT"
    return
}

$idx = $text.IndexOf($anchor)
if ($idx -lt 0) {
    Write-Output "ANCHOR_NOT_FOUND"
    return
}

$pos = $idx + $anchor.Length
$new = $text.Substring(0, $pos) + $insert + $text.Substring($pos)

$enc = New-Object System.Text.UTF8Encoding($false)  # UTF-8 no BOM
[System.IO.File]::WriteAllText($path, $new, $enc)
Write-Output "PATCHED"
