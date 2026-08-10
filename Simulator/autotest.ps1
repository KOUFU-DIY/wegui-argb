# WeGui 模拟器 headless 基准哈希回归
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File Simulator/autotest.ps1            # 比对全部 demo
#   powershell ... -File Simulator/autotest.ps1 -Update                                   # 重建基准哈希
#   powershell ... -File Simulator/autotest.ps1 -Ids 18,7,25                              # 只跑指定 demo
#
# 原理：DEMO_ID 是编译期宏（main_sim.c 用 #ifndef 守卫），脚本对每个 ID 用
# -DWE_DEMO_ID 重新配置+增量构建（只重编 main_sim.c + 链接），再以
# --autotest N 跑 headless（SDL dummy 驱动，固定 16ms 步进），对每帧屏幕
# 缓冲做 FNV-1a 链式哈希，与 autotest/golden.txt 逐行比对。
# 基准哈希只锁"渲染/动画"确定性；交互手感仍需人工验证。
param(
    [switch]$Update,
    [int[]]$Ids,
    [int]$Frames = 180
)

$ErrorActionPreference = 'Stop'

$simRoot = $PSScriptRoot
$buildDir = Join-Path $simRoot 'build_autotest'
$goldenFile = Join-Path $simRoot 'autotest\golden.txt'

# 工具链探测与 PATH 前置（与 build_sim.ps1 同口径，仅支持 ninja 路径）
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
$gxx = Get-Command g++ -ErrorAction SilentlyContinue
if (-not ($cmake -and $ninja -and $gcc -and $gxx)) {
    throw 'autotest 需要 cmake + ninja + gcc + g++ 均在 PATH 上'
}
$toolDirs = @()
foreach ($t in @($gcc, $gxx, $ninja, $cmake)) {
    $d = Split-Path $t.Source -Parent
    if ($toolDirs -notcontains $d) { $toolDirs += $d }
}
$env:PATH = ($toolDirs -join ';') + ';' + $env:PATH

if (-not $Ids) { $Ids = @(1..31) + @(101..126) } # 0=showcase 需 800x480，跳过

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
    & $cmake.Source -S $simRoot -B $buildDir -G Ninja `
        "-DCMAKE_C_COMPILER=$($gcc.Source)" "-DCMAKE_CXX_COMPILER=$($gxx.Source)" `
        "-DWE_DEMO_ID=$id" "-DWE_SIM_AUTOTEST=1" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "CMake configure 失败 (id=$id)" }
    & $cmake.Source --build $buildDir --target wegui_sim | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "构建失败 (id=$id)" }

    $exe = Join-Path $buildDir 'wegui_sim.exe'
    $out = Join-Path $buildDir 'autotest_crc.txt'
    Remove-Item $out -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $exe -ArgumentList @('--autotest', "$Frames", '--out', $out) `
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
    $evt = Join-Path $simRoot ("autotest\scripts\" + $id + ".evt")
    if (Test-Path $evt) {
        # 脚本可用首行 "# frames=N" 覆盖本条轨迹的帧数（如需等待 demo 内容积累）
        $sframes = $Frames
        $fl = (Get-Content $evt -TotalCount 1)
        if ($fl -match '^#\s*frames=(\d+)') { $sframes = [int]$Matches[1] }
        Remove-Item $out -ErrorAction SilentlyContinue
        $p2 = Start-Process -FilePath $exe -ArgumentList @('--autotest', "$sframes", '--out', $out, '--script', $evt) `
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

