@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0生成解决方案.ps1"
if errorlevel 1 pause
