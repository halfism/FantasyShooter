@echo off
rem Fixed-time awakening screenshot: arg 0=Fury 1=Swift 2=Ward
rem Announces the awakening at t=2s, captures Impact (+0.10s), Land (+0.30s), After (+1.15s).
if "%~1"=="" (echo usage: _shot_awaken.bat ^<0^|1^|2^> & exit /b 1)
setlocal
set EDITOR=G:/Epic/EpicApps/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe
set ROOT=G:/Epic/UnrealProjects/FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fsawakenshot -fsawkpath=%~1 -fsgod -fsmap=0 -fswave=3 -fsquit=14 -abslog="%ROOT%\Saved\awaken_%~1.log"
echo AWAKEN_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\awaken_%~1.log"
endlocal
