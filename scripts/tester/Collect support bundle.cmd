@echo off
rem One double-click: gathers the logs and system facts into a zip on the Desktop. Nothing is uploaded.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect-support.ps1"
echo.
pause
