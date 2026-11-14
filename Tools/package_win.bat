@echo off
rem Packages a Windows build of What Was Ours (Development) into Builds\Windows next to the project
rem (set WWO_OUT for another folder). Run it on a Windows PC: Unreal can't build for Windows from a Mac.
rem Needs Unreal Engine 5.8 (set UE_ROOT if it isn't in the default folder) and Visual Studio 2022
rem with the "Game development with C++" workload. The first run also builds the editor module and
rem every shader, so it takes a while.
rem The build starts from Config\ShippedSettings.json: the Settings values in git (saved on the Mac),
rem or this PC's own if "Save as defaults" has been used here.
setlocal
set "PROJECT_DIR=%~dp0.."
rem Quotes typed around a folder (set UE_ROOT="C:\...") would break the checks below: drop them.
if defined UE_ROOT set "UE_ROOT=%UE_ROOT:"=%"
if defined WWO_OUT set "WWO_OUT=%WWO_OUT:"=%"
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
if "%WWO_OUT%"=="" set "WWO_OUT=%PROJECT_DIR%\Builds\Windows"
if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
  echo Unreal Engine 5.8 not found at "%UE_ROOT%". Set UE_ROOT to its folder and run this again.
  exit /b 1
)
rem The enemy models come from three Fab packs that are not in git. Without them the cook has
rem retargeters and config pointing at meshes that aren't there: stop here unless told to go on.
set "MISSING="
if not exist "%PROJECT_DIR%\Content\ParagonSteel" set "MISSING=%MISSING% ParagonSteel"
if not exist "%PROJECT_DIR%\Content\ParagonMinions" set "MISSING=%MISSING% ParagonMinions"
if not exist "%PROJECT_DIR%\Content\Robot_scout_R_21" set "MISSING=%MISSING% Robot_scout_R_21"
if not "%MISSING%"=="" if not "%WWO_SKIP_PACKS%"=="1" (
  echo Missing Fab packs in Content:%MISSING%
  echo Add them to the project from your Fab library ^(HANDOFF.md, "Character models"^), or set WWO_SKIP_PACKS=1
  echo to try without them ^(enemies fall back to the mannequin; the cook may log errors^).
  exit /b 1
)
rem Without the packs the cook reports the assets that point at them as errors: keep going past those.
set "COOK_EXTRA="
if not "%MISSING%"=="" set "COOK_EXTRA=-IgnoreCookErrors"
if exist "%PROJECT_DIR%\Saved\WWO\SavedSettings.json" copy /Y "%PROJECT_DIR%\Saved\WWO\SavedSettings.json" "%PROJECT_DIR%\Config\ShippedSettings.json" >nul
if exist "%PROJECT_DIR%\Saved\WWO\Keybinds.json" copy /Y "%PROJECT_DIR%\Saved\WWO\Keybinds.json" "%PROJECT_DIR%\Config\ShippedKeybinds.json" >nul
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT_DIR%\WhatWasOurs.uproject" ^
  -platform=Win64 -clientconfig=Development -build -cook -stage -pak ^
  -archive -archivedirectory="%WWO_OUT%" -noP4 -utf8output %COOK_EXTRA%
if errorlevel 1 (
  echo Packaging failed: the first error above is the one to read.
  exit /b 1
)
echo Packaged under "%WWO_OUT%" ^(UAT adds a Windows folder inside it^). Copy that folder to another PC to play
echo there; start WhatWasOurs.exe in it.
endlocal
