@echo off
REM ============================================================================
REM BPeek — deploy pre-built package to host, run scan (persistent install).
REM
REM End-user workflow: pre-built DLL already compiled, this script copies
REM Binaries + Resources + .uplugin into host Plugins/, patches .uproject,
REM runs commandlet. No UBT compile step on the host — that happened once
REM inside build-plugin.bat.
REM
REM Persistent install (2026-04-24 pivot): plugin stays in host after the
REM run. Re-running overwrites the DLLs in place (iterative scan workflow).
REM .uproject patch is idempotent via patch-uproject.ps1 — re-runs don't
REM duplicate the plugin entry. Backup .uproject.bpeek-bak is left alone
REM as a safety net; delete it manually if you want to fully uninstall.
REM
REM Compare to deploy-and-run.bat (developer workflow — copies Source/
REM into host, UBT compiles it in-place, then runs commandlet, then tears
REM the whole install down). That path is for iterating on BPeek code.
REM
REM Usage:
REM   deploy-prebuilt.bat "C:\Path\To\Host"        (specify host)
REM   deploy-prebuilt.bat "C:\Host" "<package>"    (specify host + package dir)
REM   BPEEK_HOST=... deploy-prebuilt.bat           (host via env var)
REM
REM Host project path must come from either the positional arg or the
REM BPEEK_HOST env var — no hardcoded default. Package defaults to the
REM most recently built Releases/BPeek-v*-UE*-Win64/ directory; produce
REM one with Scripts\build-plugin.bat first.
REM
REM Package defaults to the most recently built Releases/BPeek-v*-UE*-Win64/
REM directory. Produce one via Scripts\build-plugin.bat first.
REM
REM Uninstall: manually remove <Host>/Plugins/BPeek/ and revert .uproject
REM (or restore from .bpeek-bak backup).
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0\.."

REM =========================================================================
REM Hardcoded defaults. Override via CLI arg #1 (host) or BPEEK_HOST env var.
REM UE is only needed here for UnrealEditor-Cmd.exe to run the commandlet —
REM no UBT compile step anymore since we're using a pre-built DLL.
REM =========================================================================
set HOST=%BPEEK_HOST%
set UE_ROOT=%BPEEK_UE_ROOT%
if "%UE_ROOT%"=="" set UE_ROOT=C:\Program Files\Epic Games\UE_5.4
REM =========================================================================

if not "%~1"=="" set HOST=%~1

if "%HOST%"=="" (
    echo [deploy-prebuilt] ERROR: host project path not specified.
    echo [deploy-prebuilt]   Pass it as the first argument:
    echo [deploy-prebuilt]     deploy-prebuilt.bat "C:\Path\To\Project"
    echo [deploy-prebuilt]   Or set BPEEK_HOST in your environment.
    pause & exit /b 1
)

REM Resolve package directory:
REM   1. CLI arg #2, if provided.
REM   2. Env BPEEK_PACKAGE, if set.
REM   3. Most recently modified releases/BPeek-v*-UE*-Win64/ dir.
set PACKAGE=
if not "%~2"=="" set PACKAGE=%~2
if "%PACKAGE%"=="" if not "%BPEEK_PACKAGE%"=="" set PACKAGE=%BPEEK_PACKAGE%
if "%PACKAGE%"=="" (
    for /f "delims=" %%d in ('dir /b /ad /o-d "Releases\BPeek-v*-UE*-Win64" 2^>nul') do (
        set PACKAGE=%CD%\Releases\%%d
        goto :found_package
    )
)
:found_package

if "%PACKAGE%"=="" (
    echo [deploy-prebuilt] ERROR: no pre-built package found in Releases\
    echo [deploy-prebuilt] Run Scripts\build-plugin.bat first.
    pause & exit /b 1
)
if not exist "%PACKAGE%" (
    echo [deploy-prebuilt] ERROR: package dir does not exist: %PACKAGE%
    pause & exit /b 1
)
if not exist "%PACKAGE%\Binaries" (
    echo [deploy-prebuilt] ERROR: %PACKAGE%\Binaries not found — not a valid BPeek package
    pause & exit /b 1
)
if not exist "%PACKAGE%\BPeek.uplugin" (
    echo [deploy-prebuilt] ERROR: %PACKAGE%\BPeek.uplugin not found
    pause & exit /b 1
)

set UE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
if not "%BPEEK_UE%"=="" set UE=%BPEEK_UE%
if not exist "%UE%" (
    echo [deploy-prebuilt] ERROR: UnrealEditor-Cmd.exe not found at: %UE%
    pause & exit /b 1
)
if not exist "%HOST%" (
    echo [deploy-prebuilt] ERROR: host path does not exist: %HOST%
    pause & exit /b 1
)

set UPROJECT=
for %%f in ("%HOST%\*.uproject") do set UPROJECT=%%~ff
if "%UPROJECT%"=="" (
    echo [deploy-prebuilt] ERROR: no *.uproject found in %HOST%
    pause & exit /b 1
)

set PLUGIN_TARGET=%HOST%\Plugins\BPeek
set UPROJECT_BACKUP=%UPROJECT%.bpeek-bak
set HOST_OUT=%HOST%\Saved\BPeek
set HOST_METADATA=%HOST%\bpeek-metadata.json

set PLUGINS_DIR_EXISTED_BEFORE=0
if exist "%HOST%\Plugins" set PLUGINS_DIR_EXISTED_BEFORE=1

echo [deploy-prebuilt] ============================================================
echo [deploy-prebuilt] Host:     %HOST%
echo [deploy-prebuilt] Package:  %PACKAGE%
echo [deploy-prebuilt] Editor:   %UE%
echo [deploy-prebuilt] Output:   %HOST_OUT%
echo [deploy-prebuilt] ============================================================
echo.

tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | findstr UnrealEditor >nul
if not errorlevel 1 (
    echo [deploy-prebuilt] WARNING: UnrealEditor.exe is running — rmdir may fail.
    echo [deploy-prebuilt] Close the editor before continuing. Press any key to proceed.
    pause >nul
)

REM ----- 1/6 Backup .uproject --------------------------------------------
echo [deploy-prebuilt] 1/6 Backing up .uproject
copy /Y /B "%UPROJECT%" "%UPROJECT_BACKUP%" >nul
if errorlevel 1 (
    echo [deploy-prebuilt] ERROR: failed to backup .uproject
    exit /b 1
)

REM ----- 2/6 Copy pre-built plugin (no Source/!) -------------------------
REM End-user shipping simulation: copy ONLY what a downloaded zip would
REM contain from the package dir — Binaries/, Resources/, .uplugin.
REM Skip Source/ (UE doesn't need it when Binaries are pre-compiled for
REM the matching engine version) and Intermediate/ (build artefacts).
echo [deploy-prebuilt] 2/6 Copying pre-built plugin to %PLUGIN_TARGET%
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
mkdir "%PLUGIN_TARGET%" 2>nul

xcopy /E /I /Q /Y "%PACKAGE%\Binaries"  "%PLUGIN_TARGET%\Binaries\"  >nul
if errorlevel 1 (
    echo [deploy-prebuilt] ERROR: failed to copy Binaries
    goto cleanup
)
if exist "%PACKAGE%\Resources" (
    xcopy /E /I /Q /Y "%PACKAGE%\Resources" "%PLUGIN_TARGET%\Resources\" >nul
)
copy /Y "%PACKAGE%\BPeek.uplugin" "%PLUGIN_TARGET%\" >nul
if errorlevel 1 (
    echo [deploy-prebuilt] ERROR: failed to copy .uplugin
    goto cleanup
)

REM ----- 3/6 Patch .uproject --------------------------------------------
echo [deploy-prebuilt] 3/6 Patching .uproject (adding plugin entry: BPeek)
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0patch-uproject.ps1" "%UPROJECT%" -PluginNames BPeek
if errorlevel 1 (
    echo [deploy-prebuilt] ERROR: uproject patch failed
    goto cleanup
)

REM ----- 4/6 Stale-file cleanup -----------------------------------------
if exist "%HOST_METADATA%" del /q "%HOST_METADATA%" 2>nul

REM ----- 5/6 Run commandlet (no UBT compile — pre-built DLL loads directly)
set BPEEK_EXTRA=
if defined BPEEK_RECOMPILE   set BPEEK_EXTRA=!BPEEK_EXTRA! -recompile
if defined BPEEK_INCREMENTAL set BPEEK_EXTRA=!BPEEK_EXTRA! -only-changed
if defined BPEEK_VERBOSE     set BPEEK_EXTRA=!BPEEK_EXTRA! -verbose

echo [deploy-prebuilt] 4/6 Running BPeekScan commandlet%BPEEK_EXTRA%
echo [deploy-prebuilt]     (no UBT step — DLL already compiled inside package)
echo.
call "%UE%" "%UPROJECT%" -run=BPeekScan%BPEEK_EXTRA% -unattended -nosplash -nop4
set UE_RC=%ERRORLEVEL%
echo.

REM ----- 6/6 Count output -----------------------------------------------
set POST_COUNT=0
if exist "%HOST_OUT%" (
    for /f %%c in ('dir /s /b /a:-d "%HOST_OUT%\*.md" 2^>nul ^| find /c /v ""') do set POST_COUNT=%%c
)

echo [deploy-prebuilt] 5/6 UE exit code: %UE_RC%, MD files in %HOST_OUT%: %POST_COUNT%

set DUMP_OK=0
if %POST_COUNT% GTR 0 set DUMP_OK=1

REM ============================================================
REM Persistent install mode (2026-04-24 pivot): plugin stays in host's
REM Plugins/BPeek/ after the run. Re-deploying the same package to the
REM same host overwrites the DLLs in place. Intended for iterative
REM scanning workflow — user opens the project, runs a scan whenever
REM they need fresh MD output, without re-installing the plugin.
REM
REM The .uproject patching is idempotent (patch-uproject.ps1 checks if
REM BPeek is already listed and skips re-adding), so re-runs don't
REM duplicate the plugin entry. The backup .uproject.bpeek-bak stays
REM in place as a safety net — delete manually if you want to uninstall.
REM
REM To uninstall: manually remove <Host>/Plugins/BPeek/ and revert the
REM .uproject change (or restore from the .bpeek-bak backup).
REM ============================================================
:cleanup

if exist "%HOST_METADATA%" del /q "%HOST_METADATA%" 2>nul

if exist "%HOST_OUT%\_bpeek_coverage.txt" (
    echo.
    echo [deploy-prebuilt] ============================================================
    echo [deploy-prebuilt] Coverage report:
    echo [deploy-prebuilt] ------------------------------------------------------------
    type "%HOST_OUT%\_bpeek_coverage.txt"
)

echo.
echo [deploy-prebuilt] ============================================================
if "%DUMP_OK%"=="1" (
    echo [deploy-prebuilt] SUCCESS: %POST_COUNT% MD files
    echo [deploy-prebuilt]   Output:     %HOST_OUT%\
    echo [deploy-prebuilt]   Plugin:     %PLUGIN_TARGET%\ ^(persistent^)
    echo [deploy-prebuilt]   .uproject:  BPeek entry added ^(backup at %UPROJECT_BACKUP%^)
    set FINAL_RC=0
) else (
    echo [deploy-prebuilt] FAILURE: no MD files produced
    echo [deploy-prebuilt]   UE exit code: %UE_RC%
    echo [deploy-prebuilt]   Log:          %HOST%\Saved\Logs\*.log
    set FINAL_RC=1
)
echo [deploy-prebuilt] ============================================================

pause
endlocal & exit /b %FINAL_RC%
