@echo off
setlocal
cd /d "%~dp0"
cmake --build --preset vs2022-release
if errorlevel 1 pause
