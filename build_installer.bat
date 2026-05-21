@echo off
REM --- StayPutVR build + NSIS installer ---
REM Builds the project, stages files, and creates the .exe installer.
REM Usage: build_installer.bat [Debug|Release]
REM Default: Release

setlocal

REM --- Build first ---
call "%~dp0build_win.bat" %1
if errorlevel 1 (
    echo.
    echo Build failed, aborting installer.
    exit /b 1
)

REM --- Re-enter VS environment (build_win.bat runs in its own setlocal) ---
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
)

cd /d %~dp0

REM --- Locate NSIS ---
set "NSIS_EXE="

REM Check portable copy in installer/NSIS/
if exist "installer\NSIS\makensis.exe" (
    set "NSIS_EXE=installer\NSIS\makensis.exe"
    set "NSISDIR=installer\NSIS"
    goto :found_nsis
)

REM Check system PATH
where makensis >nul 2>&1
if not errorlevel 1 (
    for /f "delims=" %%p in ('where makensis') do (
        set "NSIS_EXE=%%p"
        for %%d in ("%%~dpp..") do set "NSISDIR=%%~fd"
    )
    goto :found_nsis
)

REM Check common install locations
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    set "NSIS_EXE=C:\Program Files (x86)\NSIS\makensis.exe"
    set "NSISDIR=C:\Program Files (x86)\NSIS"
    goto :found_nsis
)
if exist "C:\Program Files\NSIS\makensis.exe" (
    set "NSIS_EXE=C:\Program Files\NSIS\makensis.exe"
    set "NSISDIR=C:\Program Files\NSIS"
    goto :found_nsis
)

echo.
echo ERROR: NSIS (makensis.exe) not found.
echo Install NSIS from https://nsis.sourceforge.io/Download
echo   - or place a portable copy in installer\NSIS\
echo   - or add makensis.exe to your PATH
exit /b 1

:found_nsis
echo NSIS: %NSIS_EXE%

REM --- Stage files for installer ---
echo.
echo Staging installer files...
cmake --build build --target prepare_installer
if errorlevel 1 (
    echo.
    echo Failed to stage installer files.
    exit /b 1
)

REM --- Build the NSIS installer ---
echo.
echo NSISDIR: %NSISDIR%
echo Building installer...
"%NSIS_EXE%" installer\app_installer.nsi
if errorlevel 1 (
    echo.
    echo INSTALLER BUILD FAILED
    exit /b 1
)

echo.
echo ============================================
echo  Installer built successfully!
echo  Output: installer\StayPutVR v1.3.2 Setup.exe
echo ============================================
