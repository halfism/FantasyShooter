@echo off
rem Package outside the agent sandbox. Usage: _pack.bat Development Packaged
rem Sub-processes spawned by UAT are killed instantly inside the sandbox Job,
rem so this must be launched through the Task Scheduler wrapper.
rem 2026-11-21 evening: whole project moved to G:\Epic\UnrealProjects, so the
rem archive and staging directories live inside the project again.
setlocal
set ENGINE=G:\Epic\EpicApps\UE_5.8
set ROOT=G:\Epic\UnrealProjects\FantasyShooter58
set PROJ=%ROOT%\FantasyShooter.uproject
set CFG=%1
if "%CFG%"=="" set CFG=Development
set DIR=%2
if "%DIR%"=="" set DIR=Packaged
set OUT=%ROOT%\Saved\pack_%DIR%.log

call "%ENGINE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJ%" -noP4 -platform=Win64 -clientconfig=%CFG% -build -cook -stage -pak -package -archive -archivedirectory="%ROOT%\%DIR%" -stagingdirectory="%ROOT%\Saved\Stage_%DIR%" -utf8output -NoUBA > "%OUT%" 2>&1
echo PACK_EXIT=%ERRORLEVEL% >> "%OUT%"
endlocal
