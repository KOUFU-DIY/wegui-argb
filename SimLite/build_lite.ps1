# SimLite 轻量模拟器构建（Windows）
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File SimLite/build_lite.ps1            # 正式版（demo 由 main_lite.c 的 DEMO_ID 宏决定）
#   powershell ... -File SimLite/build_lite.ps1 -Demo 25                                  # 不改源码临时覆盖 DEMO_ID（-DDEMO_ID=25）
#   powershell ... -File SimLite/build_lite.ps1 -Run                                      # 构建后运行
#   powershell ... -File SimLite/build_lite.ps1 -Dev [-Run -Demo 25]                      # 开发者版 wegui_lite_dev（需本地 SimLite/debug/，不随仓库发布）
#   powershell ... -File SimLite/build_lite.ps1 -Compiler gcc                             # 强制 gcc
#
# 编译器：
#   tcc —— 把 TinyCC 解压到 SimLite/tcc/（tcc.exe 在该目录下），全套 ~2.5MB，秒级全量编译
#   gcc —— 已装 MinGW 时可用（带 -g 调试信息，配合 .vscode/launch.json 的 gdb 调试）
param(
    [string]$Compiler = "auto",
    [int]$Demo = 33,
    [switch]$Run,
    [switch]$Dev
)
$ErrorActionPreference = "Stop"
$Lite = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = Split-Path -Parent $Lite
$Build = Join-Path $Lite "build"
New-Item -ItemType Directory -Force -Path $Build | Out-Null

# ---------------- 源文件清单（Core/widgets/preview 全量 glob + demo 显式列出，含 showcase） ----------------
# 正式版：SimLite 顶层 .c（main_lite.c 编译期 DEMO_ID 选 demo，无调试设施）
# 开发版：换用 debug/ 下的入口与注册表（运行时选 demo + --shot 抓帧自检）
$sources = @()
if ($Dev) {
    if (-not (Test-Path "$Lite\debug\debug_main.c")) {
        throw "开发者版源码 SimLite/debug/ 仅本地维护、不随仓库发布；请去掉 -Dev 构建正式版"
    }
    $sources += Get-ChildItem "$Lite\*.c" | Where-Object Name -ne "main_lite.c"
    $sources += Get-ChildItem "$Lite\debug\*.c"
} else {
    $sources += Get-ChildItem "$Lite\*.c"
}
$sources += Get-ChildItem "$Repo\Core\*.c" | Where-Object Name -notlike "*_bckup.c"
$sources += Get-ChildItem "$Repo\Core\components\*.c"
$sources += Get-ChildItem "$Repo\Core\widgets\*\*.c" | Where-Object Name -notlike "*_bckup.c"
$sources += Get-ChildItem "$Repo\Core\widgets_preview\*\*.c"
# 稳定 demo 清单唯一事实源 Demo/demo_manifest.txt（新增 demo 只改清单一处；模拟器编译全部条目）
$DemoSources = @(Get-Content "$Repo\Demo\demo_manifest.txt" | ForEach-Object {
    ((($_ -split '#')[0].Trim()) -split '\s+')[0]
} | Where-Object { $_ -like "*.c" })
$sources += $DemoSources | ForEach-Object { Get-Item "$Repo\Demo\$_" }
$sources += Get-ChildItem "$Repo\Demo\preview\*.c"
$sources += @(
    "$Repo\tool\1.font2c\output\simli_16_2bpp.c",
    "$Repo\tool\1.font2c\output\simsun_12_2bpp.c",
    "$Repo\tool\1.font2c\output\msyh_16_4bpp_ime.c",
    "$Repo\tool\1.font2c\output\gbsn00lp_2_16_4bpp.c",
    "$Repo\tool\2.img2c\output\c\res_img.c",
    "$Repo\tool\3.bin2c\output\merged_bin.c"
) | ForEach-Object { Get-Item $_ }

$files = $sources | ForEach-Object { $_.FullName }
$inc = @(
    "-I$Lite", "-I$Repo", "-I$Repo\Core", "-I$Repo\Core\widgets",
    "-I$Repo\Demo", "-I$Repo\Demo\preview",
    "-I$Repo\tool\1.font2c\output", "-I$Repo\tool\2.img2c\output\c", "-I$Repo\tool\3.bin2c\output"
)

# ---------------- 编译器选择 ----------------
$tcc = Join-Path $Lite "tcc\tcc.exe"
if ($Compiler -eq "auto") {
    if (Test-Path $tcc) { $Compiler = "tcc" } else { $Compiler = "gcc" }
}

$exeName = if ($Dev) { "wegui_lite_dev.exe" } else { "wegui_lite.exe" }
$out = Join-Path $Build $exeName

# 正式版显式给了 -Demo 时经 -DDEMO_ID 覆盖源码默认值（main_lite.c 用 #ifndef 保护）
$defs = @("-DWE_SIMULATOR")
if (-not $Dev -and $PSBoundParameters.ContainsKey('Demo')) { $defs += "-DDEMO_ID=$Demo" }

$sw = [System.Diagnostics.Stopwatch]::StartNew()
if ($Compiler -eq "tcc") {
    if (-not (Test-Path $tcc)) { throw "tcc not found: $tcc" }
    & $tcc @defs -D__inline=inline @inc @files -luser32 -lgdi32 -o $out
} else {
    & gcc -O2 -g @defs @inc @files -luser32 -lgdi32 -o $out
}
if ($LASTEXITCODE -ne 0) { throw "build failed ($Compiler)" }
$sw.Stop()

# 正式版编完把 PE 头 subsystem 由 3(console) 补丁为 2(GUI)：启动不带黑色控制台窗。
# 采用字节补丁而非链接选项——编译器无关（tcc 0.9.27 的 -Wl,-subsystem 在脚本环境下
# 解析不可靠），入口仍是 console CRT 的 main，printf 静默丢弃。开发版保留控制台
# （--list/--shot/--autotest 需要 stdout）。
if (-not $Dev) {
    $bytes = [System.IO.File]::ReadAllBytes($out)
    $peOfs = [System.BitConverter]::ToInt32($bytes, 0x3C)
    $subOfs = $peOfs + 4 + 20 + 68
    if ($bytes[$subOfs] -eq 3) {
        $bytes[$subOfs] = 2
        [System.IO.File]::WriteAllBytes($out, $bytes)
    }
}

Copy-Item "$Repo\tool\3.bin2c\output\merged_bin.bin" $Build -Force -ErrorAction SilentlyContinue
$size = (Get-Item $out).Length
Write-Host ("built [{0}] {1}  ({2:N0} B, {3:N1} s)" -f $Compiler, $out, $size, $sw.Elapsed.TotalSeconds)

if ($Run) {
    if ($Dev) { Start-Process -FilePath $out -ArgumentList "$Demo" -WorkingDirectory $Build }
    else      { Start-Process -FilePath $out -WorkingDirectory $Build }
}
