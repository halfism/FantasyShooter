@echo off
rem Fixed-time awakening screenshot: announces Fury awakening at t=2s,
rem captures Awaken_Impact.png (+0.10s) and Awaken_After.png (+1.15s).
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 -fsautostart -fsawakenshot -fsgod -fsmap=0 -fswave=3 -fsquit=12 -abslog="%ROOT%\Saved\awaken.log"
echo AWAKEN_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\awaken.log"
endlocal
