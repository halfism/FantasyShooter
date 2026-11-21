@echo off
rem 近景拍"甲片还在"的君王：只看一眼正面甲和 HUD 甲条，不破甲。
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 ^
  -fsautostart -fsboss=8 -fscapture -fscam=2300,66 -fsquit=6 ^
  -abslog="%ROOT%\Saved\armor_intact.log"
echo ARMORINTACT_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\armor_intact.log"
endlocal
