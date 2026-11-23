@echo off
rem A/B art pass: 1 = new lowpoly meshes (default), 0 = engine BasicShapes (-fsbasicshapes)
if "%~1"=="" (echo usage: _shot_artpass.bat ^<0^|1^> & exit /b 1)
setlocal
set EDITOR=G:/Epic/EpicApps/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe
set ROOT=G:/Epic/UnrealProjects/FantasyShooter58
set EXTRA=
set TAG=new
if "%~1"=="0" (set EXTRA=-fsbasicshapes& set TAG=old)
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fsmenagerie=0 %EXTRA% -fscapture -fsgod -fsmap=0 -fsquit=14 -abslog="%ROOT%\Saved\artpass_%TAG%.log"
timeout /t 2 /nobreak >nul
move /y "%ROOT%\Saved\Cap_00.png" "%ROOT%\Saved\ArtPass_%TAG%.png" >> "%ROOT%\Saved\artpass_%TAG%.log" 2>&1
endlocal
