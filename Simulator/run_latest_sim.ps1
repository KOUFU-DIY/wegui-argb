$exePath = Join-Path $PSScriptRoot "build\wegui_sim.exe"

if (-not (Test-Path $exePath)) {
    throw "No simulator executable found. Please build the simulator first."
}

# Close any previously launched simulator instances before opening the latest one.
Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -like "wegui_sim*" } |
    Stop-Process -Force

Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath -Parent)
