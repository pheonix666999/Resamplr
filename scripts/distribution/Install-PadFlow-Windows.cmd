@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-PadFlow-Windows.ps1"
if errorlevel 1 (
  echo.
  echo Installation failed. See CLIENT_INSTALLATION.md for manual installation.
  pause
  exit /b 1
)
pause
