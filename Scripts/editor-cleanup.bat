@echo off
REM ============================================================================
REM BPeek editor cleanup: remove plugin + restore .uproject.
REM
REM Pair with scripts\editor-deploy.bat. Run this AFTER closing the
REM UE editor to roll the host project back to its pre-deploy state.
REM
REM Usage:
REM   scripts\editor-cleanup.bat           (full cleanup, also wipes Saved\BPeek)
REM   scripts\editor-cleanup.bat --keep-md (keep generated Saved\BPeek contents)
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0\.."

set HOST=%BPEEK_HOST%
if "%HOST%"=="" (
    echo [bpeek-cleanup] ERROR: BPEEK_HOST not set.
    echo [bpeek-cleanup]   Set it to the same value used by editor-deploy.bat
    echo [bpeek-cleanup]   so the same host gets torn down.
    pause & exit /b 1
)

set KEEP_MD=0
if /I "%~1"=="--keep-md"  set KEEP_MD=1
if /I "%~1"=="-keep-md"   set KEEP_MD=1

if not exist "%HOST%" (echo [bpeek-cleanup] ERROR: host not found: %HOST% & pause & exit /b 1)

set UPROJECT=
for %%f in ("%HOST%\*.uproject") do set UPROJECT=%%~ff
if "%UPROJECT%"=="" (echo [bpeek-cleanup] ERROR: no *.uproject in %HOST% & pause & exit /b 1)

set PLUGIN_TARGET=%HOST%\Plugins\BPeek
set UPROJECT_BACKUP=%UPROJECT%.bpeek-bak
set HOST_OUT=%HOST%\Saved\BPeek

REM Leftover extension-plugin folders from the pre-merge layout (4 .uplugin
REM world). Kept in cleanup only to roll back hosts that still remember
REM the old layout — will be removed from the script once no known host
REM still has these directories.
set EI_LEGACY=%HOST%\Plugins\BPeekEnhancedInput
set GAS_LEGACY=%HOST%\Plugins\BPeekGAS
set FLOW_LEGACY=%HOST%\Plugins\BPeekFlow

REM Soft check — if editor is running, UBT-style DLL locks can block
REM rmdir. Don't kill it automatically; ask the user.
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | findstr UnrealEditor >nul
if not errorlevel 1 (
    echo [bpeek-cleanup] WARNING: UnrealEditor.exe is running.
    echo [bpeek-cleanup]   Close it before continuing — otherwise the plugin DLL
    echo [bpeek-cleanup]   stays loaded and rmdir will fail.
    echo [bpeek-cleanup]   Press any key once the editor is closed.
    pause >nul
)

echo [bpeek-cleanup] ============================================================
echo [bpeek-cleanup] Host:     %HOST%
echo [bpeek-cleanup] Project:  %UPROJECT%
if "%KEEP_MD%"=="1" (
    echo [bpeek-cleanup] MD output: KEEPING %HOST_OUT%
) else (
    echo [bpeek-cleanup] MD output: will wipe %HOST_OUT%
)
echo [bpeek-cleanup] ============================================================

echo [bpeek-cleanup] 1/3 Removing plugin from host
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
if exist "%EI_LEGACY%"     rmdir /s /q "%EI_LEGACY%"
if exist "%GAS_LEGACY%"    rmdir /s /q "%GAS_LEGACY%"
if exist "%FLOW_LEGACY%"   rmdir /s /q "%FLOW_LEGACY%"
REM Top-level Plugins\ only — we never touch GameFeatures\ because
REM the host project may have other plugins there we don't own.
dir /b "%HOST%\Plugins" >nul 2>&1
if errorlevel 1 rmdir "%HOST%\Plugins" 2>nul

echo [bpeek-cleanup] 2/3 Restoring .uproject from backup
if exist "%UPROJECT_BACKUP%" (
    copy /Y /B "%UPROJECT_BACKUP%" "%UPROJECT%" >nul
    del "%UPROJECT_BACKUP%"
) else (
    echo [bpeek-cleanup]   No backup found; .uproject left as-is.
    echo [bpeek-cleanup]   If it still lists BPeek in Plugins[], remove the entry
    echo [bpeek-cleanup]   manually or re-run scripts\editor-deploy.bat first.
)

echo [bpeek-cleanup] 3/3 Cleaning MD output
if "%KEEP_MD%"=="0" (
    if exist "%HOST_OUT%" rmdir /s /q "%HOST_OUT%"
) else (
    echo [bpeek-cleanup]   --keep-md: %HOST_OUT% preserved.
)

REM bpeek-metadata.json is an intermediate from the legacy pipeline —
REM should never appear, but clean up if it does.
if exist "%HOST%\bpeek-metadata.json" del /q "%HOST%\bpeek-metadata.json" 2>nul

echo.
echo [bpeek-cleanup] ============================================================
echo [bpeek-cleanup] DONE — host state restored.
echo [bpeek-cleanup]   Intermediate\Build\Win64\...\BPeek* artifacts left in
echo [bpeek-cleanup]   place (SCM-ignored by UE; UBT reclaims on next compile).
echo [bpeek-cleanup] ============================================================

endlocal & exit /b 0
