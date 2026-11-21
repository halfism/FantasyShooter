@echo off
rem 打包版冒烟：确认 Dev 包里破甲逻辑与 HUD 都在（编辑器里已经不证明打包没问题）。
setlocal
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
"%ROOT%\Packaged\Windows\FantasyShooter.exe" -fsautostart -fsboss=8 -fsbreakarmor ^
  -fsgod -fsbot -fscam=2300,66 -fscapture -fsquit=12 ^
  -abslog="%ROOT%\Saved\pkg_armor.log"
echo PKGARMOR_EXIT=%ERRORLEVEL% >> "%ROOT%\Saved\pkg_armor.log"
endlocal
