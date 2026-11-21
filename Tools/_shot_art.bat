@echo off
rem Fixed-time screenshot for the three path arts. arg 0=Pyre 1=SlowTime 2=Bulwark
setlocal
if "%~1"=="" goto usage
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fsart=%~1 -fsartshot -fsgod -fsmap=0 -fswave=3 -fsquit=12 -abslog="%ROOT%\Saved\artshot_%~1.log"
echo ARTSHOT_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\artshot_%~1.log"
goto done
:usage
echo usage: _shot_art.bat 0^|1^|2
:done
endlocal
