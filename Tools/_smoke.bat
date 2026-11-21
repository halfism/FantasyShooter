@echo off
rem Smoke test: bot plays naturally for 45s (waves + elites + art cast), then quits by itself.
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsauto -fsbot -fsgod -fsmap=0 -fscapture -fsquit=45 -abslog="%ROOT%\Saved\smoke.log"
echo SMOKE_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\smoke.log"
endlocal
