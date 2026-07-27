param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
$simRoot = Join-Path $repoRoot 'Simulator'
$buildDir = Join-Path $simRoot 'build'

# 先停掉运行中的模拟器：exe 被占用会导致 -Clean 删除失败或链接期写不进
# （与 VS Code 任务 sim: stop running 同口径，脚本内自带避免踩坑）
Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -like 'wegui_sim*' } |
    Stop-Process -Force -ErrorAction SilentlyContinue

if ($Clean -and (Test-Path $buildDir)) {
    # OneDrive 对目录节点的句柄释放较慢，整树 Remove-Item -Recurse 容易报
    # IOException。-Clean 只需要清空内容：先删文件、再自深向浅删子目录，
    # 目录节点本身删不掉也无妨（空目录不影响全新配置）。
    Get-ChildItem $buildDir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
    }
    $left = @(Get-ChildItem $buildDir -Recurse -File -ErrorAction SilentlyContinue)
    if ($left.Count -gt 0) {
        Write-Warning ("有 " + $left.Count + " 个文件仍被占用未删除（如模拟器/杀软占用），继续构建可能复用旧产物")
    }
    Get-ChildItem $buildDir -Recurse -Directory -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | ForEach-Object {
            Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
        }
    Remove-Item $buildDir -Force -ErrorAction SilentlyContinue
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    throw 'cmake not found in PATH.'
}

$ninja = Get-Command ninja -ErrorAction SilentlyContinue
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
$gxx = Get-Command g++ -ErrorAction SilentlyContinue
$mingwMake = Get-Command mingw32-make -ErrorAction SilentlyContinue

if ($cmake) { Write-Host ("cmake: " + $cmake.Source) }
if ($ninja) { Write-Host ("ninja: " + $ninja.Source) }
if ($gcc) { Write-Host ("gcc: " + $gcc.Source) }
if ($gxx) { Write-Host ("g++: " + $gxx.Source) }
if ($mingwMake) { Write-Host ("mingw32-make: " + $mingwMake.Source) }

# 把检测到的工具链 bin 目录前置到 PATH，确保 cc1.exe 等子进程优先加载
# 与自己配套的 DLL（libisl / libmpc / libwinpthread / libstdc++ 等），
# 而不是 PATH 上其它程序携带的不匹配旧版本。否则 gcc.exe 能启动，
# 但它拉起的 cc1.exe 会静默加载失败，CMake 报 “The C compiler is broken”。
$toolDirs = @()
foreach ($tool in @($gcc, $gxx, $ninja, $cmake, $mingwMake)) {
    if ($tool) {
        $dir = Split-Path $tool.Source -Parent
        if ($toolDirs -notcontains $dir) { $toolDirs += $dir }
    }
}
if ($toolDirs.Count -gt 0) {
    $env:PATH = ($toolDirs -join ';') + ';' + $env:PATH
}

$configureArgs = @(
    '-S', $simRoot,
    '-B', $buildDir
)

if ($ninja -and $gcc -and $gxx) {
    $configureArgs += @(
        '-G', 'Ninja',
        ('-DCMAKE_C_COMPILER=' + $gcc.Source),
        ('-DCMAKE_CXX_COMPILER=' + $gxx.Source)
    )
}
elseif ($mingwMake -and $gcc -and $gxx) {
    $configureArgs += @(
        '-G', 'MinGW Makefiles',
        ('-DCMAKE_C_COMPILER=' + $gcc.Source),
        ('-DCMAKE_CXX_COMPILER=' + $gxx.Source),
        ('-DCMAKE_MAKE_PROGRAM=' + $mingwMake.Source)
    )
}
else {
    throw 'No supported local build toolchain found in PATH. Require either ninja+gcc+g++ or mingw32-make+gcc+g++.'
}

# 生成器自愈：build 缓存记录的生成器与本次选择不一致时（工具链切换、
# OneDrive 回同步旧缓存等）自动清掉 CMakeCache/CMakeFiles 重新配置，
# 避免 "Does not match the generator used previously" 卡死。
$genIndex = [Array]::IndexOf($configureArgs, '-G')
if ($genIndex -ge 0) {
    $wantGen = $configureArgs[$genIndex + 1]
    $cacheFile = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path $cacheFile) {
        $m = Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' | Select-Object -First 1
        if ($m -and $m.Matches[0].Groups[1].Value -ne $wantGen) {
            Write-Host ("CMake generator changed: '" + $m.Matches[0].Groups[1].Value + "' -> '" + $wantGen + "', purging stale cache")
            Remove-Item $cacheFile -Force -ErrorAction SilentlyContinue
            $cmFilesDir = Join-Path $buildDir 'CMakeFiles'
            if (Test-Path $cmFilesDir) {
                Get-ChildItem $cmFilesDir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
                    Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
                }
                Get-ChildItem $cmFilesDir -Recurse -Directory -ErrorAction SilentlyContinue |
                    Sort-Object FullName -Descending | ForEach-Object {
                        Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
                    }
                Remove-Item $cmFilesDir -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

& $cmake.Source @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configure failed.'
}

& $cmake.Source --build $buildDir --target wegui_sim
if ($LASTEXITCODE -ne 0) {
    throw 'Simulator build failed.'
}
