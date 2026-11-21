@echo off
rem 独立编译 5.8 副本工程，走计划任务在沙箱外执行。
setlocal
set ENGINE=G:\Epic\EpicApps\UE_5.8
set PROJ=G:\Epic\UnrealProjects\FantasyShooter58\FantasyShooter.uproject
set OUT=G:\Epic\UnrealProjects\FantasyShooter58\Saved\build_dev.log
if not exist "%~dp0..\Saved" mkdir "%~dp0..\Saved"
call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" FantasyShooterEditor Win64 Development -Project="%PROJ%" -WaitMutex -NoUBA -MaxParallelActions=1 > "%OUT%" 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> "%OUT%"
endlocal
