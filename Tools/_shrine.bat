@echo off
rem Shrine self-test + panel screenshot source.
rem -fsshrtest : assert cost curve / rank cap / bonuses in memory only (never writes the save)
rem -fscoins=N  : grant N spendable coins so the panel has something to show
rem -fsshrine   : open the panel at title so a capture can grab it
setlocal
set EDITOR=G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%EDITOR%" "%ROOT%\FantasyShooter.uproject" -game -windowed -resx=1600 -resy=900 ^
  -fsshrtest -fscoins=2000 -fsshrine -fscapture -fsquit=12 -abslog="%ROOT%\Saved\shrine.log"
echo SHRINE_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\shrine.log"
endlocal
