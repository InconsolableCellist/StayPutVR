@echo off
REM --- StayPutVR Windows build script ---
REM Usage: build_win.bat [Debug|Release]
REM Default: Release

setlocal

set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

REM --- VS environment ---
if defined VSCMD_VER goto :skip_vcvars
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
:skip_vcvars

REM --- OpenVR SDK auto-detect ---
if defined OPENVR_SDK_PATH goto :found_openvr
for /f "delims=" %%d in ('dir /b /ad /o-n "C:\apps\programming\openvr-*" 2^>nul') do (
    set "OPENVR_SDK_PATH=C:\apps\programming\%%d"
    goto :found_openvr
)
echo ERROR: OpenVR SDK not found in C:\apps\programming\openvr-*
echo Set OPENVR_SDK_PATH environment variable and retry.
exit /b 1
:found_openvr
echo OpenVR SDK: %OPENVR_SDK_PATH%

REM --- Steam auto-detect ---
if defined STEAM_PATH goto :found_steam
if exist "C:\games\Steam" (
    set "STEAM_PATH=C:\games\Steam"
    goto :found_steam
)
if exist "C:\Program Files (x86)\Steam" (
    set "STEAM_PATH=C:\Program Files (x86)\Steam"
    goto :found_steam
)
echo WARNING: Steam not found. Install target will not work.
set "STEAM_PATH="
:found_steam
if defined STEAM_PATH echo Steam: %STEAM_PATH%

cd /d %~dp0

REM --- Configure ---
cmake -B build -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DOPENVR_SDK_PATH="%OPENVR_SDK_PATH%" ^
    -DSTEAM_PATH="%STEAM_PATH%"
if errorlevel 1 (
    echo.
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)

REM --- Build ---
cmake --build build
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD SUCCEEDED (%BUILD_TYPE%)
echo Output: %~dp0build\
