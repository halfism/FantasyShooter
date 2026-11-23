@echo off
rem Screenshot a single arena theme with the new art pass.
rem usage: _shot_map.bat <0|1|2>   (0=blue 1=frost 2=lava)
if "%~1"=="" (echo usage: _shot_map.bat ^<0^|1^|2^> & exit /b 1)
setlocal
set EDITOR=G:/Epic/EpicApps/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe
set ROOT=G:/Epic/UnrealProjects/FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fsmenagerie=0 -fscapture -fsgod -fsmap=%~1 -fsquit=14 -abslog="%ROOT%\Saved\shot_map_%~1.log"
timeout /t 2 /nobreak >nul
move /y "%ROOT%\Saved\Cap_00.png" "%ROOT%\Saved\Shot_Map%~1.png" >> "%ROOT%\Saved\shot_map_%~1.log" 2>&1
endlocal
