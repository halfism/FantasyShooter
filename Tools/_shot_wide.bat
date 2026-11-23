@echo off
rem Wide shot: pull the camera back to inspect the whole arena layout.
rem usage: _shot_wide.bat <0|1|2> [arm] [pitch]
if "%~1"=="" (echo usage: _shot_wide.bat ^<0^|1^|2^> [arm] [pitch] & exit /b 1)
setlocal
set EDITOR=G:/Epic/EpicApps/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe
set ROOT=G:/Epic/UnrealProjects/FantasyShooter58
set ARM=%~2
if "%ARM%"=="" set ARM=4200
set PITCH=%~3
if "%PITCH%"=="" set PITCH=-70
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fscapture -fsgod -fsmap=%~1 -fscam=%ARM%,%PITCH% -fsquit=14 -abslog="%ROOT%\Saved\shot_wide_%~1.log"
timeout /t 2 /nobreak >nul
move /y "%ROOT%\Saved\Cap_00.png" "%ROOT%\Saved\Wide_%~1.png" >> "%ROOT%\Saved\shot_wide_%~1.log" 2>&1
endlocal
