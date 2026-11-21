@echo off
rem 通过计划任务在沙箱外编译（UBT 派生的 cl.exe 在 Agent 的 Job 里会瞬间失败）。
rem 用法：Start-Detached.ps1 -FilePath 本文件 -WaitSeconds 3
setlocal
set ENGINE=G:\Epic\EpicApps\UE_5.8
set PROJ=G:\Epic\UnrealProjects\FantasyShooter58\FantasyShooter.uproject
set OUT=G:\Epic\UnrealProjects\FantasyShooter58\Saved\build_dev.log

call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" FantasyShooterEditor Win64 Development -Project="%PROJ%" -WaitMutex -NoUBA -MaxParallelActions=1 > "%OUT%" 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> "%OUT%"
endlocal
