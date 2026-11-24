@echo off
rem Uploads the finished Windows build to the game's GitHub releases page: double-click this file.
rem It does the work in Tools\windows\upload_release.ps1 (read that file for what happens).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\windows\upload_release.ps1"
echo.
pause
