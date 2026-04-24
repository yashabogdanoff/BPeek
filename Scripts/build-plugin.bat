@echo off
REM BPeek — pre-built plugin packaging for release distribution.
REM
REM Wraps `RunUAT.bat BuildPlugin` — the official UE workflow to build
REM a plugin without a host project. Produces:
REM   releases/BPeek-v<VERSION>-UE<UE_VERSION>-Win64/        (unzipped package)
REM   releases/BPeek-v<VERSION>-UE<UE_VERSION>-Win64.zip     (distributable)
REM
REM UE_VERSION is auto-derived from BPEEK_UE_ROOT folder name:
REM   G:\Epic Games\UE_5.4  →  5.4
REM   G:\Epic Games\UE_5.5  →  5.5
REM   ...etc.
REM
REM Prerequisites:
REM   - UE installed. Edit BPEEK_UE_ROOT below or set it as env var.
REM   - Optional 3P plugins installed in the build engine for their
REM     detection to succeed (e.g. Flow in Engine/Plugins/Marketplace/
REM     → resulting binary has BPEEK_WITH_FLOW=1).
REM   - ~5-10 minutes wall time, ~1 GB free disk for intermediates.

setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0\.."

REM UE install root — change here or set BPEEK_UE_ROOT env var.
set UE_ROOT=%BPEEK_UE_ROOT%
if "%UE_ROOT%"=="" set UE_ROOT=G:\Epic Games\UE_5.4

REM Derive UE version string from folder name (UE_5.4 → 5.4).
for %%f in ("%UE_ROOT%") do set UE_FOLDER=%%~nxf
set UE_VERSION=%UE_FOLDER:UE_=%
if "%UE_VERSION%"=="%UE_FOLDER%" (
    REM Folder didn't start with UE_, fallback to env override or "unknown".
    set UE_VERSION=%BPEEK_UE_VERSION%
    if "!UE_VERSION!"=="" set UE_VERSION=unknown
)

set UAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat
if not exist "%UAT%" (
    echo [build-plugin] RunUAT.bat not found at: %UAT%
    echo [build-plugin] Set BPEEK_UE_ROOT to point at your UE install.
    pause
    exit /b 1
)

REM Read plugin version from .uplugin VersionName.
set VERSION=dev
for /f "tokens=2 delims=:," %%v in ('findstr /c:"\"VersionName\"" BPeek.uplugin') do (
    set VERSION=%%~v
)
set VERSION=%VERSION: =%
set VERSION=%VERSION:"=%

set PLUGIN=%CD%\BPeek.uplugin
set PACKAGE_NAME=BPeek-v%VERSION%-UE%UE_VERSION%-Win64
set PACKAGE_DIR=%CD%\Releases\%PACKAGE_NAME%
set ZIP_PATH=%CD%\Releases\%PACKAGE_NAME%.zip

echo [build-plugin] ============================================================
echo [build-plugin] UE root:      %UE_ROOT%
echo [build-plugin] UE version:   %UE_VERSION%
echo [build-plugin] Plugin ver:   %VERSION%
echo [build-plugin] Package dir:  %PACKAGE_DIR%
echo [build-plugin] Zip:          %ZIP_PATH%
echo [build-plugin] ============================================================

if not exist "%CD%\Releases" mkdir "%CD%\Releases"
if exist "%PACKAGE_DIR%" (
    echo [build-plugin] Wiping old package dir
    rmdir /s /q "%PACKAGE_DIR%"
)
if exist "%ZIP_PATH%" del /q "%ZIP_PATH%"

call "%UAT%" BuildPlugin ^
    -Plugin="%PLUGIN%" ^
    -Package="%PACKAGE_DIR%" ^
    -TargetPlatforms=Win64 ^
    -Rocket
set RC=%ERRORLEVEL%

if not "%RC%"=="0" (
    echo.
    echo [build-plugin] ============================================================
    echo [build-plugin] FAILED: RunUAT rc=%RC%
    echo [build-plugin] ============================================================
    pause
    exit /b %RC%
)

REM Trim non-essential payload before zipping so the public release zip
REM stays close to the ~10 MB minimal-DLL size (88 MB → ~10 MB).
REM
REM   Intermediate/  — UBT build artifacts, never shipped anyway.
REM   Source/        — plugin source. Users who need to recompile (Flow
REM                    support, different engine version) clone the repo
REM                    directly; pre-built zip is for vanilla-engine use.
REM   *.pdb          — Win64 debug symbols, ~50-60 MB combined. Useful
REM                    for in-editor crash diagnostics, not for shipping.
echo.
echo [build-plugin] Trimming Intermediate/ Source/ and *.pdb before zip
if exist "%PACKAGE_DIR%\Intermediate" rmdir /s /q "%PACKAGE_DIR%\Intermediate"
if exist "%PACKAGE_DIR%\Source"       rmdir /s /q "%PACKAGE_DIR%\Source"
del /s /q "%PACKAGE_DIR%\*.pdb" >nul 2>&1

echo [build-plugin] Zipping package into %ZIP_PATH%
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -Path '%PACKAGE_DIR%\*' -DestinationPath '%ZIP_PATH%' -Force"
set ZIP_RC=%ERRORLEVEL%
if not "%ZIP_RC%"=="0" (
    echo [build-plugin] WARNING: zip step failed ^(rc=%ZIP_RC%^) — package dir intact
)

echo.
echo [build-plugin] ============================================================
echo [build-plugin] SUCCESS
echo [build-plugin]   Package:  %PACKAGE_DIR%\
if "%ZIP_RC%"=="0" (
    echo [build-plugin]   Zip:      %ZIP_PATH%
)
echo [build-plugin]   Next: scripts\deploy-prebuilt.bat ^<host^> to test on a project
echo [build-plugin]         or upload the zip to GitHub releases.
echo [build-plugin] ============================================================

pause
endlocal & exit /b 0
