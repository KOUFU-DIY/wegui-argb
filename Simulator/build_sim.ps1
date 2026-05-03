param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
$simRoot = Join-Path $repoRoot 'Simulator'
$buildDir = Join-Path $simRoot 'build'

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
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

& $cmake.Source @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configure failed.'
}

& $cmake.Source --build $buildDir --target wegui_sim
if ($LASTEXITCODE -ne 0) {
    throw 'Simulator build failed.'
}
