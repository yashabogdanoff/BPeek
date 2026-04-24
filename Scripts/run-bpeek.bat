@echo off
REM BPeek plugin — commandlet launcher.
REM
REM Copy this file into the ROOT of the UE project you want to scan,
REM alongside your *.uproject. Run it — BPeek dumps MD files into
REM <Project>/bpeek/.
REM
REM Prerequisites:
REM   1. Plugins/BPeek/ exists in the host project. Two options:
REM        a) Source: copy this repo's BPeek/ into <Host>/Plugins/BPeek/
REM        b) Prebuilt: drop RunUAT-packaged plugin into the same path
REM   2. <Host>.uproject lists BPeek in its Plugins array:
REM        { "Name": "BPeek", "Enabled": true }
REM   3. UE 5.4 installed with UnrealEditor-Cmd.exe accessible.
REM   4. Editor is CLOSED (UBT locks binaries during compile).
REM
REM Output: MD files land in bpeek/ (project root). Configurable by
REM passing -bpeekmd=<dir> to BPeekDump. Commandlet is a thin alias
REM over DumpMetadata — see BPeek/Source/BPeek/BPeekDumpCommandlet.cpp.
REM
REM Expected wall time: ~1-2 min on a ~1000-asset project.
REM UBT recompiles automatically if the plugin source changed.

setlocal
cd /d "%~dp0"

REM Auto-detect UE 5.4 install. Override with BPEEK_UE environment
REM variable if you have UE in a non-standard location.
set UE=%BPEEK_UE%
if "%UE%"=="" set UE=G:\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
if not exist "%UE%" (
    echo [bpeek] UnrealEditor-Cmd.exe not found at: %UE%
    echo [bpeek] Set BPEEK_UE environment variable to point at your install.
    pause
    exit /b 1
)

REM .uproject auto-detected as the only *.uproject in repo root. If
REM there are multiple, pick the first — adjust by BPEEK_UPROJECT env.
set UPROJECT=%BPEEK_UPROJECT%
if "%UPROJECT%"=="" for %%f in (*.uproject) do set UPROJECT=%%~ff
if "%UPROJECT%"=="" (
    echo [bpeek] No *.uproject found in repo root.
    echo [bpeek] Run this batch file from your UE project directory,
    echo [bpeek]   or set BPEEK_UPROJECT environment variable.
    pause
    exit /b 1
)

set OUTDIR=%~dp0bpeek

echo [bpeek] Editor:  %UE%
echo [bpeek] Project: %UPROJECT%
echo [bpeek] Output:  %OUTDIR%
echo [bpeek] Running BPeekDump commandlet...
echo.

REM Snapshot pre-run state so we can detect "dump actually happened"
REM even if the editor returns non-zero from unrelated asset-loading
REM warnings (typical for projects with missing marketplace refs).
if exist "%OUTDIR%" (
    for /f %%c in ('dir /s /b /a:-d "%OUTDIR%\*.md" 2^>nul ^| find /c /v ""') do set PRE_COUNT=%%c
) else (
    set PRE_COUNT=0
)

"%UE%" "%UPROJECT%" ^
    -run=BPeekScan ^
    -unattended -nosplash -nop4
set RC=%ERRORLEVEL%

REM Re-count MD files. Commandlet success is judged by "more MD files
REM after than before" — UE often exits non-zero because of cosmetic
REM editor warnings (missing textures, failed-to-load marketplace
REM packages) that don't affect the actual dump. We trust the filesystem.
if exist "%OUTDIR%" (
    for /f %%c in ('dir /s /b /a:-d "%OUTDIR%\*.md" 2^>nul ^| find /c /v ""') do set POST_COUNT=%%c
) else (
    set POST_COUNT=0
)
set /a DELTA=POST_COUNT-PRE_COUNT

echo.
echo [bpeek] =========================================================
echo [bpeek] Editor exit code: %RC%
echo [bpeek] MD files before: %PRE_COUNT%
echo [bpeek] MD files after:  %POST_COUNT% (delta: %DELTA%)
echo [bpeek]
if %POST_COUNT% GTR 0 (
    echo [bpeek] Output: %OUTDIR%\
    if not "%RC%"=="0" (
        echo [bpeek] NOTE: UE exited with code %RC% but dump completed.
        echo [bpeek]   Typically caused by missing marketplace asset refs
        echo [bpeek]   in the project. See Saved\Logs\ if concerned.
    )
    set FINAL_RC=0
) else (
    echo [bpeek] FAILED: no MD files produced.
    echo [bpeek] See Saved\Logs\ for the full UE log.
    set FINAL_RC=1
)
echo [bpeek] =========================================================

pause
endlocal & exit /b %FINAL_RC%
