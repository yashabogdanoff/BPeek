@echo off
REM ============================================================================
REM BPeek end-to-end: install → compile → dump → cleanup plugin (output stays).
REM
REM One click from the BPeek repo deploys the plugin into a host UE project,
REM runs the BPeekDump commandlet, LEAVES the generated markdown at
REM <HOST>/bpeek/ so the host project can consume the output directly (for
REM AI agents pointed at the host repo, and so C++ Source/ cross-refs in MDs
REM resolve correctly — their relative paths are anchored to the host root).
REM
REM On re-run the previous <HOST>/bpeek/ is wiped before the commandlet runs,
REM so the output always reflects the current project state (no stale MDs
REM for assets that were deleted between runs).
REM
REM Plugin itself is installed before the run and removed after, so the host
REM tracked-file set (excluding bpeek/) stays byte-for-byte identical.
REM
REM Usage:
REM   scripts\deploy-and-run.bat "W:\Path\To\HostProject"
REM   or set BPEEK_HOST env var and run without arguments
REM
REM Optional env overrides:
REM   BPEEK_UE   — path to UnrealEditor-Cmd.exe (default: G:\Epic Games\UE_5.4\...)
REM
REM Host project gets during run:
REM   + Plugins/BPeek/               (copied from repo, then DELETED at end)
REM   + <project>.uproject           (patched to enable BPeek, then RESTORED)
REM   + Intermediate/Build/Win64/... (UHT/UBT artifacts — SCM-ignored; stay)
REM
REM Host project gets after run (persisted):
REM   + bpeek/                       (MD output — regenerated each run)
REM
REM Final state of host: same tracked-file snapshot as before the run, PLUS
REM the bpeek/ folder (add to SCM ignore if it should stay out of commits).
REM ============================================================================

setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0\.."

REM =========================================================================
REM Configuration:
REM   HOST     — host UE project root (must contain one *.uproject).
REM              Pass as CLI arg #1 or set BPEEK_HOST in the environment.
REM   UE_ROOT  — engine install root.
REM              Pass via BPEEK_UE_ROOT env var. Defaults to the standard
REM              Epic Games Launcher install path.
REM =========================================================================
set HOST=%BPEEK_HOST%
set UE_ROOT=%BPEEK_UE_ROOT%
if "%UE_ROOT%"=="" set UE_ROOT=C:\Program Files\Epic Games\UE_5.4

if not "%~1"=="" set HOST=%~1

if "%HOST%"=="" (
    echo [bpeek] ERROR: host project path not specified.
    echo [bpeek]   Pass it as the first argument:
    echo [bpeek]     deploy-and-run.bat "C:\Path\To\Project"
    echo [bpeek]   Or set BPEEK_HOST in your environment.
    pause & exit /b 1
)

REM Derive UE-dependent paths from UE_ROOT. DotNet layout shifted between
REM engine versions:
REM   UE 5.4: ThirdParty\DotNet\6.0.302\windows\dotnet.exe
REM   UE 5.7: ThirdParty\DotNet\8.0.412\win-x64\dotnet.exe
REM Try the two known layouts explicitly. Straight-line checks only —
REM a `for /d ... ( if ... )` block here caused batch to mis-parse the
REM rest of the script (cleanup section silently skipped).
set UE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set UBT_DLL=%UE_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll
set UBT_DOTNET=
if exist "%UE_ROOT%\Engine\Binaries\ThirdParty\DotNet\8.0.412\win-x64\dotnet.exe" set UBT_DOTNET=%UE_ROOT%\Engine\Binaries\ThirdParty\DotNet\8.0.412\win-x64\dotnet.exe
if exist "%UE_ROOT%\Engine\Binaries\ThirdParty\DotNet\6.0.302\windows\dotnet.exe" set UBT_DOTNET=%UE_ROOT%\Engine\Binaries\ThirdParty\DotNet\6.0.302\windows\dotnet.exe

REM Legacy explicit override still wins (BPEEK_UE for UnrealEditor-Cmd.exe).
if not "%BPEEK_UE%"=="" set UE=%BPEEK_UE%

if not exist "%HOST%" (
    echo [bpeek] ERROR: host path does not exist: %HOST%
    pause & exit /b 1
)

REM Find .uproject in host root.
set UPROJECT=
for %%f in ("%HOST%\*.uproject") do set UPROJECT=%%~ff
if "%UPROJECT%"=="" (
    echo [bpeek] ERROR: no *.uproject found in %HOST%
    pause & exit /b 1
)

if not exist "%UE%" (
    echo [bpeek] ERROR: UnrealEditor-Cmd.exe not found at: %UE%
    echo [bpeek] Edit the hardcoded UE path at the top of this batch file.
    pause & exit /b 1
)
if not exist "%UBT_DOTNET%" (
    echo [bpeek] ERROR: UBT dotnet.exe not found at: %UBT_DOTNET%
    pause & exit /b 1
)
if not exist "%UBT_DLL%" (
    echo [bpeek] ERROR: UnrealBuildTool.dll not found at: %UBT_DLL%
    pause & exit /b 1
)

REM Derive UBT target name: <ProjectBaseName>Editor, e.g. SIMULATOREditor.
for %%f in ("%UPROJECT%") do set PROJNAME=%%~nf
REM Default target name follows the convention "<ProjectName>Editor",
REM which is what UnrealBuildTool's default rules assembly expects.
REM Some projects (Lyra, Fortnite-style layouts) keep a shorter
REM editor-target name — override with BPEEK_UBT_TARGET.
if "%BPEEK_UBT_TARGET%"=="" (
    set UBT_TARGET=%PROJNAME%Editor
) else (
    set UBT_TARGET=%BPEEK_UBT_TARGET%
)

set PLUGIN_TARGET=%HOST%\Plugins\BPeek
set UPROJECT_BACKUP=%UPROJECT%.bpeek-bak
REM Saved/BPeek/ — default output dir. Matches commandlet default
REM (FPaths::ProjectSavedDir/BPeek). Saved/ is conventionally SCM-
REM ignored in UE projects so MD output stays out of commits without
REM touching the host's ignore config.
set HOST_OUT=%HOST%\Saved\BPeek
set HOST_METADATA=%HOST%\bpeek-metadata.json

REM Whether to remove the Plugins/ dir entirely if our plugin was the only one.
set PLUGINS_DIR_EXISTED_BEFORE=0
if exist "%HOST%\Plugins" set PLUGINS_DIR_EXISTED_BEFORE=1

echo [bpeek] ============================================================
echo [bpeek] Host:     %HOST%
echo [bpeek] Project:  %UPROJECT%
echo [bpeek] Editor:   %UE%
echo [bpeek] Output:   %HOST_OUT%
echo [bpeek] ============================================================
echo.

REM Close editor / UBT check: if running, UBT will fail on file lock. Warn only.
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | findstr UnrealEditor >nul
if not errorlevel 1 (
    echo [bpeek] WARNING: UnrealEditor.exe is running — UBT may fail on locked binaries.
    echo [bpeek] Please close the editor before continuing. Press any key to proceed anyway.
    pause >nul
)

REM ----- 1/8 Backup uproject ---------------------------------------------
REM /B = binary mode. Prevents any text-mode translation (rare but real at
REM ctrl-Z bytes); byte-for-byte identical copy.
echo [bpeek] 1/8 Backing up .uproject
copy /Y /B "%UPROJECT%" "%UPROJECT_BACKUP%" >nul
if errorlevel 1 (
    echo [bpeek] ERROR: failed to backup .uproject
    exit /b 1
)

REM ----- 2/8 Install plugin ----------------------------------------------
REM Flat repo layout (2026-04-24): plugin sits at repo root, so the copy
REM grabs BPeek.uplugin + Source/ + Config/ from %CD%. All extension
REM modules live under Source/ and are gated by BPEEK_WITH_* build-time
REM defines from filesystem detection in their .Build.cs files.
echo [bpeek] 2/8 Copying BPeek plugin into host
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
mkdir "%PLUGIN_TARGET%" 2>nul
copy /Y "BPeek.uplugin" "%PLUGIN_TARGET%\" >nul
xcopy /E /I /Q /Y "Source" "%PLUGIN_TARGET%\Source\" >nul
if errorlevel 1 (
    echo [bpeek] ERROR: failed to copy Source/
    goto cleanup
)
if exist "Config" xcopy /E /I /Q /Y "Config" "%PLUGIN_TARGET%\Config\" >nul
if exist "Resources" xcopy /E /I /Q /Y "Resources" "%PLUGIN_TARGET%\Resources\" >nul

REM Single plugin entry — extensions are modules inside BPeek.uplugin, not
REM separate plugins anymore.
set PATCH_PLUGINS=BPeek

REM ----- 3/8 Patch .uproject via tab-preserving script -------------------
REM scripts/patch-uproject.ps1 does a string-based insertion that preserves
REM existing tabs and line endings byte-for-byte (vs ConvertFrom-Json which
REM renormalises indentation and triggers false SCM diffs mid-run).
echo [bpeek] 3/8 Patching .uproject (adding plugin entry: %PATCH_PLUGINS%)
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0patch-uproject.ps1" "%UPROJECT%" -PluginNames %PATCH_PLUGINS%
if errorlevel 1 (
    echo [bpeek] ERROR: uproject patch failed
    goto cleanup
)

REM ----- 4/8 Stale-file cleanup ------------------------------------------
REM Commandlet itself wipes <HOST_OUT> contents (keeping the folder) on
REM full rebuild; skips the wipe under -only-changed. Batník only clears
REM the bpeek-metadata.json intermediate from the pre-rewrite pipeline if
REM it happens to be lying around.
if exist "%HOST_METADATA%" del /q "%HOST_METADATA%" 2>nul
if defined BPEEK_INCREMENTAL (
    echo [bpeek] 4/8 Incremental ^(BPEEK_INCREMENTAL=1^), commandlet will skip wipe
) else (
    echo [bpeek] 4/8 Full rebuild — commandlet will wipe %HOST_OUT% contents
)

REM ----- 5/8 Compile plugin via UBT --------------------------------------
REM UnrealEditor-Cmd.exe in -unattended mode does NOT auto-trigger UBT when
REM plugin DLLs are missing — it just logs 'module BPeek could not be found'
REM and exits. So we invoke UBT explicitly here. First run: ~30-40s. Repeat
REM runs with unchanged source: ~2-3s (UBT smart-make skips work).
echo [bpeek] 5/8 Compiling plugin via UBT (target: %UBT_TARGET%)
echo [bpeek]     first run is ~30-40s, later runs ~2-3s with warm caches
"%UBT_DOTNET%" "%UBT_DLL%" %UBT_TARGET% Win64 Development -Project="%UPROJECT%" -WaitMutex -FromMsBuild
set UBT_RC=%ERRORLEVEL%
if not "%UBT_RC%"=="0" (
    echo [bpeek] ERROR: UBT compile failed with code %UBT_RC%
    goto cleanup
)

REM ----- 6/8 Run commandlet ----------------------------------------------
REM BPEEK_RECOMPILE=1 opts into per-BP FKismetEditorUtilities::
REM CompileBlueprint capture (adds compiler messages to ## Issues).
REM Adds ~10-30 min on a ~700-BP project, so OFF by default; use
REM for audit passes.
REM BPEEK_INCREMENTAL=1 opts into -only-changed: hash-diff against
REM _bpeek_hashes.json, regen only changed/new assets. Massive speedup
REM for small edits.
set BPEEK_EXTRA=
if defined BPEEK_RECOMPILE   set BPEEK_EXTRA=!BPEEK_EXTRA! -recompile
if defined BPEEK_INCREMENTAL set BPEEK_EXTRA=!BPEEK_EXTRA! -only-changed
REM BPEEK_VERBOSE=1 opts OUT of the default AI-optimised layout back
REM to expanded markdown tables / single-file Blueprint MDs / un-
REM shortened asset paths. The default (no flag) is compact output.
if defined BPEEK_VERBOSE     set BPEEK_EXTRA=!BPEEK_EXTRA! -verbose

echo [bpeek] 6/8 Running BPeekDump commandlet%BPEEK_EXTRA%
echo.
REM UE 5.7's commandlet shutdown leaves cmd.exe's parser in a broken
REM state (subsequent `%VAR%` substitutions silently strip chars) when
REM UE-Cmd is invoked directly. `call` forces a clean sub-context; cmd
REM resumes normally after the child exits.
call "%UE%" "%UPROJECT%" -run=BPeekScan%BPEEK_EXTRA% -unattended -nosplash -nop4
set UE_RC=%ERRORLEVEL%
echo.

REM ----- 7/8 Count output ------------------------------------------------
set POST_COUNT=0
if exist "%HOST_OUT%" (
    for /f %%c in ('dir /s /b /a:-d "%HOST_OUT%\*.md" 2^>nul ^| find /c /v ""') do set POST_COUNT=%%c
)

echo [bpeek] 7/8 UE exit code: %UE_RC%, MD files in %HOST_OUT%: %POST_COUNT%

set DUMP_OK=0
if %POST_COUNT% GTR 0 set DUMP_OK=1

REM ============================================================
:cleanup
echo [bpeek] 8/8 Removing plugin from host, restoring .uproject
if exist "%PLUGIN_TARGET%" rmdir /s /q "%PLUGIN_TARGET%"
if "%PLUGINS_DIR_EXISTED_BEFORE%"=="0" (
    REM We created Plugins/ ourselves — remove if now empty.
    dir /b "%HOST%\Plugins" >nul 2>&1
    if errorlevel 1 rmdir "%HOST%\Plugins" 2>nul
)

REM Intermediate artifacts from plugin compilation live in
REM <host>\Intermediate\Build\Win64\UnrealEditor\Development\BPeek*
REM These are SCM-ignored (UBT owns Intermediate/). Not deleting — they
REM stay until UBT regenerates them for the host's native target next
REM compile, which will simply skip BPeek since it's no longer referenced.

if exist "%HOST_METADATA%" (
    echo [bpeek]     also deleting %HOST_METADATA%
    del /q "%HOST_METADATA%" 2>nul
)

REM Restore .uproject (/B = binary mode, see step 1/8 rationale).
if exist "%UPROJECT_BACKUP%" (
    copy /Y /B "%UPROJECT_BACKUP%" "%UPROJECT%" >nul
    del "%UPROJECT_BACKUP%"
)

REM ----- Coverage report (emitted by commandlet as _bpeek_coverage.txt) -
if exist "%HOST_OUT%\_bpeek_coverage.txt" (
    echo.
    echo [bpeek] ============================================================
    echo [bpeek] Coverage report:
    echo [bpeek] ------------------------------------------------------------
    type "%HOST_OUT%\_bpeek_coverage.txt"
)

echo.
echo [bpeek] ============================================================
if "%DUMP_OK%"=="1" (
    echo [bpeek] SUCCESS: %POST_COUNT% MD files
    echo [bpeek]   Output:     %HOST_OUT%\
    echo [bpeek]   Host state: plugin removed, .uproject restored
    echo [bpeek]               bpeek/ folder left in place — add to SCM ignore if desired
    set FINAL_RC=0
) else (
    echo [bpeek] FAILURE: no MD files produced
    echo [bpeek]   UE exit code: %UE_RC%
    echo [bpeek]   Log:          %HOST%\Saved\Logs\*.log
    echo [bpeek]   Host state:   plugin removed, .uproject restored
    set FINAL_RC=1
)
echo [bpeek] ============================================================

pause
endlocal & exit /b %FINAL_RC%
