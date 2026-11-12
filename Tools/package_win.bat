@echo off
rem Packages a Windows build of What Was Ours (Development) into Builds\Windows next to the project
rem (set WWO_OUT for another folder). Run it on a Windows PC: Unreal can't build for Windows from a Mac.
rem Needs Unreal Engine 5.8 (set UE_ROOT if it isn't in the default folder) and Visual Studio 2022
rem with the "Game development with C++" workload. The first run also builds the editor module and
rem every shader, so it takes a while.
rem The build starts from Config\ShippedSettings.json: the Settings values in git (saved on the Mac),
rem or this PC's own if "Save as defaults" has been used here.
rem The Fab character packs aren't in git (see HANDOFF.md, "Character models"): add them to the
rem project first, or bodies fall back to the mannequin.
setlocal
set "PROJECT_DIR=%~dp0.."
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
if "%WWO_OUT%"=="" set "WWO_OUT=%PROJECT_DIR%\Builds\Windows"
if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
  echo Unreal Engine 5.8 not found at "%UE_ROOT%". Set UE_ROOT to its folder and run this again.
  exit /b 1
)
if exist "%PROJECT_DIR%\Saved\WWO\SavedSettings.json" copy /Y "%PROJECT_DIR%\Saved\WWO\SavedSettings.json" "%PROJECT_DIR%\Config\ShippedSettings.json" >nul
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT_DIR%\WhatWasOurs.uproject" ^
  -platform=Win64 -clientconfig=Development -build -cook -stage -pak ^
  -archive -archivedirectory="%WWO_OUT%" -noP4 -utf8output
if errorlevel 1 (
  echo Packaging failed: the first error above is the one to read.
  exit /b 1
)
echo Packaged to "%WWO_OUT%". Copy that folder to another PC to play there; start WhatWasOurs.exe.
endlocal
