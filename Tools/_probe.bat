@echo off
rem 在编辑器（-game 模式）里跑一次机制自检，日志直接落到 Saved\probe_armor.log。
rem 走计划任务启动，否则窗口会被沙箱的 Job 一起带走。
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 ^
  -fsautostart -fsprobe -fsundying -fsquit=140 -abslog="%ROOT%\Saved\probe_armor.log"
echo PROBE_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\probe_armor.log"
endlocal
