@echo off
REM ============================================================================
REM BPeek editor deploy: install plugin → patch .uproject → compile → stop.
REM
REM Use this when you want to test BPeek through the UE editor UI
REM (toolbar button, Content-Browser context menu, Project Settings
REM > Plugins > BPeek page). The script prepares the host project
REM so the editor loads BPeek, but does NOT run the commandlet and
REM does NOT remove the plugin on exit.
REM
REM After this batník finishes:
REM   1. Launch UnrealEditor.exe with the host .uproject (or pass the
REM      --launch flag below to have the script do it for you).
REM   2. Exercise BPeek through the UI.
REM   3. Close the editor.
REM   4. Run scripts\editor-cleanup.bat to remove the plugin and
REM      restore the .uproject.
REM
REM Usage:
REM   scripts\editor-deploy.bat            (just deploy + compile, exit)
REM   scripts\editor-deploy.bat --launch   (deploy + compile + launch editor)
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0\.."

REM Host project path comes from BPEEK_HOST. The only positional arg
REM this script accepts is --launch (see below).
set HOST=%BPEEK_HOST%
set UE_ROOT=%BPEEK_UE_ROOT%
if "%UE_ROOT%"=="" set UE_ROOT=C:\Program Files\Epic Games\UE_5.4
if "%HOST%"=="" (
    echo [bpeek-editor] ERROR: BPEEK_HOST not set.
    echo [bpeek-editor]   Set it in your environment, e.g.:
    echo [bpeek-editor]     set BPEEK_HOST=C:\Path\To\Project
    echo [bpeek-editor]     editor-deploy.bat --launch
    pause & exit /b 1
)
set UE_CMD=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set UE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe
set UBT_DOTNET=%UE_ROOT%\Engine\Binaries\ThirdParty\DotNet\6.0.302\windows\dotnet.exe
set UBT_DLL=%UE_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll

set LAUNCH=0
if /I "%~1"=="--launch" set LAUNCH=1
if /I "%~1"=="-launch"  set LAUNCH=1

if not "%BPEEK_HOST%"=="" set HOST=%BPEEK_HOST%

if not exist "%HOST%"    (echo [bpeek-editor] ERROR: host not found: %HOST% & pause & exit /b 1)
if not exist "%UE_CMD%"  (echo [bpeek-editor] ERROR: UnrealEditor-Cmd.exe missing & pause & exit /b 1)
if not exist "%UBT_DLL%" (echo [bpeek-editor] ERROR: UBT dll missing & pause & exit /b 1)

set UPROJECT=
for %%f in ("%HOST%\*.uproject") do set UPROJECT=%%~ff
if "%UPROJECT%"=="" (echo [bpeek-editor] ERROR: no *.uproject in %HOST% & pause & exit /b 1)

for %%f in ("%UPROJECT%") do set PROJNAME=%%~nf
set UBT_TARGET=%PROJNAME%Editor
set PLUGIN_TARGET=%HOST%\Plugins\BPeek
set UPROJECT_BACKUP=%UPROJECT%.bpeek-bak

REM ---- Guard: if a previous editor-deploy left state behind, fail loud.
REM ---- cleanup.bat is supposed to handle the teardown.
if exist "%PLUGIN_TARGET%" (
    echo [bpeek-editor] WARNING: %PLUGIN_TARGET% already present.
    echo [bpeek-editor]   A previous editor-deploy did not run cleanup.
    echo [bpeek-editor]   Re-running will overwrite it. Press any key to continue.
    pause >nul
)
if exist "%UPROJECT_BACKUP%" (
    echo [bpeek-editor] WARNING: %UPROJECT_BACKUP% exists from a prior run.
    echo [bpeek-editor]   Will be overwritten with the current .uproject state.
    pause >nul
)

echo [bpeek-editor] ============================================================
echo [bpeek-editor] Host:     %HOST%
echo [bpeek-editor] Project:  %UPROJECT%
echo [bpeek-editor] Mode:     deploy + compile ^(no commandlet run^)
if "%LAUNCH%"=="1" echo [bpeek-editor] Then:     launch UnrealEditor.exe
echo [bpeek-editor] ============================================================

echo [bpeek-editor] 1/4 Backing up .uproject
copy /Y /B "%UPROJECT%" "%UPROJECT_BACKUP%" >nul || goto :fail

echo [bpeek-editor] 2/4 Copying BPeek plugin into host
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
mkdir "%PLUGIN_TARGET%" 2>nul
copy /Y "BPeek.uplugin" "%PLUGIN_TARGET%\" >nul || goto :fail
xcopy /E /I /Q /Y "Source" "%PLUGIN_TARGET%\Source\" >nul || goto :fail
if exist "Config" xcopy /E /I /Q /Y "Config" "%PLUGIN_TARGET%\Config\" >nul
if exist "Resources" xcopy /E /I /Q /Y "Resources" "%PLUGIN_TARGET%\Resources\" >nul

echo [bpeek-editor] 3/4 Patching .uproject (tab-preserving)
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0patch-uproject.ps1" "%UPROJECT%" -PluginNames BPeek || goto :fail

echo [bpeek-editor] 4/4 Compiling plugin via UBT (target: %UBT_TARGET%)
echo [bpeek-editor]     first run ~30-40s, warm rebuild ~2-3s
"%UBT_DOTNET%" "%UBT_DLL%" %UBT_TARGET% Win64 Development -Project="%UPROJECT%" -WaitMutex -FromMsBuild
if not "%ERRORLEVEL%"=="0" (
    echo [bpeek-editor] ERROR: UBT compile failed ^(rc=%ERRORLEVEL%^)
    goto :fail
)

echo.
echo [bpeek-editor] ============================================================
echo [bpeek-editor] READY — plugin installed and compiled.
echo [bpeek-editor]
echo [bpeek-editor] Next steps:
if "%LAUNCH%"=="1" (
    echo [bpeek-editor]   Launching UnrealEditor.exe in the background...
) else (
    echo [bpeek-editor]   1. Open UE: "%UE%" "%UPROJECT%"
    echo [bpeek-editor]      ^(or re-run this script with --launch^)
)
echo [bpeek-editor]   2. Test BPeek via toolbar button / right-click asset /
echo [bpeek-editor]      Project Settings ^> Plugins ^> BPeek.
echo [bpeek-editor]   3. Close the editor when done.
echo [bpeek-editor]   4. Run scripts\editor-cleanup.bat to remove the plugin
echo [bpeek-editor]      and restore the host .uproject.
echo [bpeek-editor] ============================================================

if "%LAUNCH%"=="1" (
    start "" "%UE%" "%UPROJECT%"
)

endlocal & exit /b 0

:fail
echo [bpeek-editor] ERROR — deploy aborted. Run scripts\editor-cleanup.bat to roll back.
pause
endlocal & exit /b 1
