@echo off
rem 真打一遍：机器人正面顶射君王，验证"甲是被真实火力一点点打穿的"，
rem 而不是只有 DebugBreakArmor 能弄破。
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 ^
  -fsautostart -fsboss=8 -fsgod -fsbot -fscam=2300,66 -fsquit=22 ^
  -abslog="%ROOT%\Saved\armor_fight.log"
echo ARMORFIGHT_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\armor_fight.log"
endlocal
