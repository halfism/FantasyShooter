@echo off
rem Player-eye shot of the arena art props (no menagerie stands in the way).
rem usage: _shot_props.bat <0|1|2>
if "%~1"=="" (echo usage: _shot_props.bat ^<0^|1^|2^> & exit /b 1)
setlocal
set EDITOR=G:/Epic/EpicApps/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe
set ROOT=G:/Epic/UnrealProjects/FantasyShooter58
set THEME=%~1
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fscapture -fsgod -fsmap=%THEME% -fsquit=20 -abslog="%ROOT%\Saved\shot_props_%THEME%.log"
timeout /t 2 /nobreak >nul
if exist "%ROOT%\Saved\Cap_00.png" move /y "%ROOT%\Saved\Cap_00.png" "%ROOT%\Saved\Props_%THEME%.png"
endlocal
