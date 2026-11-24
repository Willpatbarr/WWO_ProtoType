@echo off
rem Builds the Windows release of What Was Ours: double-click this file.
rem It does the work in Tools\windows\build_release.ps1 (read that file for what happens).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\windows\build_release.ps1"
echo.
pause
