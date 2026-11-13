@echo off
rem One-step Windows build of What Was Ours: double-click this file, or run it from a Command Prompt.
rem It finds Unreal Engine 5.8, goes on without the Fab packs if they aren't here, and runs
rem Tools\package_win.bat. The game ends up in Builds\Windows inside this folder.
rem If the engine isn't found, set UE_ROOT to its folder first (the one that contains "Engine").
setlocal
set "HERE=%~dp0"
if defined UE_ROOT set "UE_ROOT=%UE_ROOT:"=%"
if defined UE_ROOT if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
  echo UE_ROOT is set to "%UE_ROOT%" but there is no engine there: looking in the usual places.
  set "UE_ROOT="
)
rem Where the Epic Games Launcher recorded the 5.8 install.
if not defined UE_ROOT for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul ^| findstr /i InstalledDirectory') do (
  if exist "%%B\Engine\Build\BatchFiles\RunUAT.bat" set "UE_ROOT=%%B"
)
if not defined UE_ROOT for %%D in (
  "%ProgramFiles%\Epic Games\UE_5.8"
  "%USERPROFILE%\Epic Games\UE_5.8"
  "C:\Epic Games\UE_5.8"
  "D:\Epic Games\UE_5.8"
  "E:\Epic Games\UE_5.8"
) do (
  if not defined UE_ROOT if exist "%%~D\Engine\Build\BatchFiles\RunUAT.bat" set "UE_ROOT=%%~D"
)
if not defined UE_ROOT (
  echo Unreal Engine 5.8 was not found. In the Epic Games Launcher, open the 5.8 install folder,
  echo copy its path, then in this window type:   set UE_ROOT=that path
  echo ^(no quotes^) and run build_windows.bat again.
  pause
  exit /b 1
)
echo Using Unreal Engine at "%UE_ROOT%"
if not exist "%HERE%Content\ParagonSteel" set "WWO_SKIP_PACKS=1"
if not exist "%HERE%Content\ParagonMinions" set "WWO_SKIP_PACKS=1"
if not exist "%HERE%Content\Robot_scout_R_21" set "WWO_SKIP_PACKS=1"
if "%WWO_SKIP_PACKS%"=="1" echo Building without the Fab enemy packs: enemies will show as the mannequin.
call "%HERE%Tools\package_win.bat"
set "RESULT=%ERRORLEVEL%"
echo.
if "%RESULT%"=="0" (echo Done. The game is in "%HERE%Builds\Windows".) else (echo The build failed. Scroll up to the first error.)
pause
exit /b %RESULT%
