# WeGui SimLite headless 基准哈希回归
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File SimLite/autotest.ps1             # 比对全部 demo
#   powershell ... -File SimLite/autotest.ps1 -Update                                    # 重建基准哈希
#   powershell ... -File SimLite/autotest.ps1 -Ids 18,7,25                               # 只跑指定 demo
#   powershell ... -File SimLite/autotest.ps1 -SkipBuild                                 # 跳过构建直接跑现有 exe
#
# 原理：开发者版 wegui_lite_dev 运行时选 demo（注册表），整个回归只构建一次
# （tcc 秒级全量编译，gcc 兜底），再对每个 id 以 `<id> --autotest N` 跑
# headless（不开窗，固定 16ms 步进），对每帧 ARGB8888 帧缓冲做 FNV-1a 链式
# 哈希，与 autotest/golden.txt 逐行比对。帧缓冲布局与 565->8888 位扩展公式
# 与原 SDL 模拟器一致，基准哈希历史值直接沿用。
# 基准哈希只锁"渲染/动画"确定性；交互手感仍需人工验证。
param(
    [switch]$Update,
    [int[]]$Ids,
    [int]$Frames = 180,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$liteRoot = $PSScriptRoot
$buildDir = Join-Path $liteRoot 'build'
$goldenFile = Join-Path $liteRoot 'autotest\golden.txt'
$exe = Join-Path $buildDir 'wegui_lite_dev.exe'

# 构建一次开发者版（运行时选 demo，无需逐 id 重编）
if (-not $SkipBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $liteRoot 'build_lite.ps1') -Dev | Out-Host
    if ($LASTEXITCODE -ne 0) { throw '构建失败 (build_lite.ps1 -Dev)' }
}
if (-not (Test-Path $exe)) { throw "找不到 $exe（先跑 build_lite.ps1 -Dev）" }

if (-not $Ids) { $Ids = @(1..33) + @(101..116) + @(118..119) + @(121..126) } # 0=showcase 需 800x480 跳过；117/120 已毕业留洞（→33/32）

# 读入既有基准哈希
$golden = @{}
if (Test-Path $goldenFile) {
    foreach ($line in Get-Content $goldenFile) {
        if ($line -match '^id=(\d+) frames=\d+ (?:script=(\S+) )?crc=[0-9A-F]+$') {
            $k = if ($Matches[2]) { "$($Matches[1]).s" } else { "$($Matches[1])" }
            $golden[$k] = $line.Trim()
        }
    }
}

$results = [ordered]@{}
$fail = 0
foreach ($id in $Ids) {
    $out = Join-Path $buildDir 'autotest_crc.txt'
    Remove-Item $out -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $exe -ArgumentList @("$id", '--autotest', "$Frames", '--out', $out) `
        -WorkingDirectory $buildDir -PassThru -Wait -WindowStyle Hidden
    if ($p.ExitCode -ne 0) { throw "运行失败 (id=$id, exit=$($p.ExitCode))" }
    if (-not (Test-Path $out)) { throw "无结果文件 (id=$id)" }

    $line = (Get-Content $out -TotalCount 1).Trim()
    $results["$id"] = $line
    if ($Update) { Write-Host ("UPDATE " + $line) }
    elseif (-not $golden.ContainsKey("$id")) { Write-Host ("NEW    " + $line + "   (基准哈希缺失，用 -Update 登记)"); $fail++ }
    elseif ($golden["$id"] -ne $line) { Write-Host ("FAIL   " + $line + "   golden: " + $golden["$id"]); $fail++ }
    else { Write-Host ("PASS   " + $line) }

    # 交互轨迹基准哈希：scripts/<id>.evt 存在时追加一次脚本注入运行
    $evt = Join-Path $liteRoot ("autotest\scripts\" + $id + ".evt")
    if (Test-Path $evt) {
        # 脚本可用首行 "# frames=N" 覆盖本条轨迹的帧数（如需等待 demo 内容积累）
        $sframes = $Frames
        $fl = (Get-Content $evt -TotalCount 1)
        if ($fl -match '^#\s*frames=(\d+)') { $sframes = [int]$Matches[1] }
        Remove-Item $out -ErrorAction SilentlyContinue
        $p2 = Start-Process -FilePath $exe -ArgumentList @("$id", '--autotest', "$sframes", '--out', $out, '--script', $evt) `
            -WorkingDirectory $buildDir -PassThru -Wait -WindowStyle Hidden
        if ($p2.ExitCode -ne 0) { throw "脚本运行失败 (id=$id, exit=$($p2.ExitCode))" }
        if (-not (Test-Path $out)) { throw "脚本无结果文件 (id=$id)" }
        $sline = (Get-Content $out -TotalCount 1).Trim()
        $results["$id.s"] = $sline
        if ($Update) { Write-Host ("UPDATE " + $sline) }
        elseif (-not $golden.ContainsKey("$id.s")) { Write-Host ("NEW    " + $sline + "   (基准哈希缺失，用 -Update 登记)"); $fail++ }
        elseif ($golden["$id.s"] -ne $sline) { Write-Host ("FAIL   " + $sline + "   golden: " + $golden["$id.s"]); $fail++ }
        else { Write-Host ("PASS   " + $sline) }
    }
}

if ($Update) {
    New-Item -ItemType Directory -Force -Path (Split-Path $goldenFile -Parent) | Out-Null
    # 合并写回：本次运行的结果覆盖对应键，未运行的旧基准行保留（-Ids 局部更新安全）
    foreach ($k in $results.Keys) { $golden[$k] = $results[$k] }
    $ids = @($golden.Keys) | Where-Object { $_ -notmatch '\.s$' } | ForEach-Object { [int]$_ } | Sort-Object
    $lines = @()
    foreach ($i in $ids) {
        $lines += $golden["$i"]
        if ($golden.ContainsKey("$i.s")) { $lines += $golden["$i.s"] }
    }
    $lines | Set-Content -Encoding ascii $goldenFile
    Write-Host ("基准哈希已写入: " + $goldenFile + " (" + $lines.Count + " 条)")
}
else {
    if ($fail -gt 0) { Write-Host ("FAILED: $fail 项不一致"); exit 1 }
    Write-Host 'ALL PASS'
}
