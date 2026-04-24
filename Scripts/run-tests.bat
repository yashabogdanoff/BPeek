@echo off
REM ============================================================================
REM BPeek automation tests runner — standalone.
REM
REM Mirrors deploy-and-run.bat scaffolding (install plugin → patch .uproject →
REM UBT compile → run Automation tests → restore) but replaces the BPeekDump
REM commandlet step with `Automation RunTests BPeek.*`.
REM
REM Exits non-zero on test failure; keeps host project byte-for-byte clean.
REM
REM Usage:   scripts\run-tests.bat
REM Override host path:  set BPEEK_HOST=... & scripts\run-tests.bat
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0\.."

set HOST=%BPEEK_HOST%
set UE_ROOT=%BPEEK_UE_ROOT%
if "%UE_ROOT%"=="" set UE_ROOT=C:\Program Files\Epic Games\UE_5.4
set UE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set UBT_DOTNET=%UE_ROOT%\Engine\Binaries\ThirdParty\DotNet\6.0.302\windows\dotnet.exe
set UBT_DLL=%UE_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll

if not "%~1"=="" set HOST=%~1

if "%HOST%"=="" (
    echo [bpeek-tests] ERROR: host project path not specified.
    echo [bpeek-tests]   Pass it as the first argument:
    echo [bpeek-tests]     run-tests.bat "C:\Path\To\Project"
    echo [bpeek-tests]   Or set BPEEK_HOST in your environment.
    pause & exit /b 1
)

if not exist "%HOST%" (
    echo [bpeek-tests] ERROR: host path does not exist: %HOST%
    pause & exit /b 1
)

set UPROJECT=
for %%f in ("%HOST%\*.uproject") do set UPROJECT=%%~ff
if "%UPROJECT%"=="" (
    echo [bpeek-tests] ERROR: no *.uproject in %HOST%
    pause & exit /b 1
)
if not exist "%UE%" (echo ERROR: %UE% not found & pause & exit /b 1)
if not exist "%UBT_DOTNET%" (echo ERROR: %UBT_DOTNET% not found & pause & exit /b 1)
if not exist "%UBT_DLL%" (echo ERROR: %UBT_DLL% not found & pause & exit /b 1)

for %%f in ("%UPROJECT%") do set PROJNAME=%%~nf
set UBT_TARGET=%PROJNAME%Editor
set PLUGIN_TARGET=%HOST%\Plugins\BPeek
set UPROJECT_BACKUP=%UPROJECT%.bpeek-bak
set PLUGINS_DIR_EXISTED_BEFORE=0
if exist "%HOST%\Plugins" set PLUGINS_DIR_EXISTED_BEFORE=1

echo [bpeek-tests] ============================================================
echo [bpeek-tests] Host:     %HOST%
echo [bpeek-tests] Project:  %UPROJECT%
echo [bpeek-tests] Filter:   BPeek.*
echo [bpeek-tests] ============================================================

echo [bpeek-tests] 1/6 Backing up .uproject
copy /Y /B "%UPROJECT%" "%UPROJECT_BACKUP%" >nul || (echo ERROR: backup failed & exit /b 1)

echo [bpeek-tests] 2/6 Copying BPeek plugin into host
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
mkdir "%PLUGIN_TARGET%" 2>nul
copy /Y "BPeek.uplugin" "%PLUGIN_TARGET%\" >nul || goto cleanup
xcopy /E /I /Q /Y "Source" "%PLUGIN_TARGET%\Source\" >nul || goto cleanup
if exist "Config" xcopy /E /I /Q /Y "Config" "%PLUGIN_TARGET%\Config\" >nul
if exist "Resources" xcopy /E /I /Q /Y "Resources" "%PLUGIN_TARGET%\Resources\" >nul

echo [bpeek-tests] 3/6 Patching .uproject
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0patch-uproject.ps1" "%UPROJECT%" -PluginNames BPeek
if errorlevel 1 goto cleanup

echo [bpeek-tests] 4/6 Compiling plugin (UBT target: %UBT_TARGET%)
"%UBT_DOTNET%" "%UBT_DLL%" %UBT_TARGET% Win64 Development -Project="%UPROJECT%" -WaitMutex -FromMsBuild
set UBT_RC=%ERRORLEVEL%
if not "%UBT_RC%"=="0" (
    echo [bpeek-tests] ERROR: UBT compile failed ^(rc=%UBT_RC%^)
    goto cleanup
)

echo [bpeek-tests] 5/6 Running Automation tests ^(headless^)
REM -nullrhi           no D3D/Vulkan swapchain → no editor window even
REM                    though UnrealEditor-Cmd.exe is what we're invoking.
REM -unattended        non-interactive, no dialogs.
REM -nosplash -nopause no splash, no pause-on-exit.
REM -nosound           no audio init (pure waste for tests).
REM -nop4              no Perforce chatter.
REM -ExecCmds          chains "Automation RunTests <filter>; Quit" so the
REM                    editor runs tests then exits.
"%UE%" "%UPROJECT%" ^
    -ExecCmds="Automation RunTests BPeek.; Quit" ^
    -unattended -nullrhi -nosplash -nopause -nop4 -nosound -log
set TEST_RC=%ERRORLEVEL%

echo [bpeek-tests] 6/6 Test run finished, rc=%TEST_RC%

:cleanup
echo [bpeek-tests] Cleaning up — removing plugin
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
if "%PLUGINS_DIR_EXISTED_BEFORE%"=="0" (
    dir /b "%HOST%\Plugins" >nul 2>&1
    if errorlevel 1 rmdir "%HOST%\Plugins" 2>nul
)
if exist "%UPROJECT_BACKUP%" (
    copy /Y /B "%UPROJECT_BACKUP%" "%UPROJECT%" >nul
    del "%UPROJECT_BACKUP%"
)

echo.
echo [bpeek-tests] ============================================================
if "%TEST_RC%"=="0" (
    echo [bpeek-tests] SUCCESS — all tests passed
    set FINAL_RC=0
) else (
    echo [bpeek-tests] FAILURE ^(rc=%TEST_RC%^) — check log under %HOST%\Saved\Logs
    set FINAL_RC=1
)
echo [bpeek-tests] ============================================================

pause
endlocal & exit /b %FINAL_RC%
