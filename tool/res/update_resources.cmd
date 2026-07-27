@echo off
rem WeGui resource one-click update (hookable as Keil Before-Build user command)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update_resources.ps1"
if errorlevel 1 (
  echo [update_resources] FAILED
  exit /b 1
)
