@echo off
rem Debug shot: place 3 tinted props right next to the player.
if "%~1"=="" (echo usage: _shot_proptest.bat ^<0^|1^|2^> & exit /b 1)
setlocal
set EDITOR=G:/Epic/EpicApps/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe
set ROOT=G:/Epic/UnrealProjects/FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fscapture -fsgod -fsmap=%~1 -fsproptest -fscam=950,-55 -fsquit=14 -abslog="%ROOT%\Saved\shot_proptest.log"
timeout /t 2 /nobreak >nul
move /y "%ROOT%\Saved\Cap_00.png" "%ROOT%\Saved\PropTest.png" >> "%ROOT%\Saved\shot_proptest.log" 2>&1
endlocal
