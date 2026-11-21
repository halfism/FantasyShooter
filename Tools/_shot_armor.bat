@echo off
rem 定点拍"破甲"：君王摆在正前方，2.2 秒后强制打穿甲片，
rem 脚本自己会在破甲瞬间 / 一秒后各留一张（Armor_Break.png / Armor_Broken.png）。
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 ^
  -fsautostart -fsboss=8 -fsbreakarmor -fscam=2300,66 -fsquit=9 ^
  -abslog="%ROOT%\Saved\armor_shot.log"
echo ARMORSHOT_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\armor_shot.log"
endlocal
