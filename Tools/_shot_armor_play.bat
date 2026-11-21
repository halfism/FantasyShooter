@echo off
rem 实战镜头（默认相机）拍破甲：机器人顶着打，2.2 秒后强制打穿甲片，
rem 让 Cap_00/Cap_01 落在"贴脸 + 甲片飞出去 + HUD 甲条消失"的画面。
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 ^
  -fsautostart -fsboss=8 -fsbreakarmor -fsgod -fsbot -fscapture -fsquit=12 ^
  -abslog="%ROOT%\Saved\armor_play.log"
echo ARMORPLAY_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\armor_play.log"
endlocal
