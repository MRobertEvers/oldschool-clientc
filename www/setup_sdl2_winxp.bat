@echo off
REM Helper script to download and setup SDL2 for MinGW i686 (Windows XP builds)
REM This script downloads SDL2 development files and installs them to the MinGW toolchain

setlocal EnableDelayedExpansion

echo ========================================
echo SDL2 Setup for Windows XP Builds
echo ========================================
echo.

REM Set the MinGW toolchain path (must match build_winxp.bat)
set "MINGW_ROOT=C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2"
set "MINGW_PREFIX=%MINGW_ROOT%\mingw32"

echo MinGW Installation: %MINGW_ROOT%
echo SDL2 will be installed to: %MINGW_PREFIX%
echo.

REM Check if MinGW exists
if not exist "%MINGW_PREFIX%" (
    echo ERROR: MinGW installation not found!
    echo Expected location: %MINGW_PREFIX%
    echo.
    echo Please update MINGW_ROOT in this script to match your MinGW installation.
    pause
    exit /b 1
)

REM Check if SDL2 is already installed
if exist "%MINGW_PREFIX%\include\SDL2\SDL.h" (
    echo [OK] SDL2 headers found at: %MINGW_PREFIX%\include\SDL2\
    set "SDL2_INSTALLED=1"
) else (
    echo [!] SDL2 headers not found
    set "SDL2_INSTALLED=0"
)

if exist "%MINGW_PREFIX%\lib\libSDL2.a" (
    echo [OK] SDL2 library found at: %MINGW_PREFIX%\lib\
    set "SDL2_LIB_INSTALLED=1"
) else (
    echo [!] SDL2 library not found
    set "SDL2_LIB_INSTALLED=0"
)

if exist "%MINGW_PREFIX%\bin\SDL2.dll" (
    echo [OK] SDL2.dll found at: %MINGW_PREFIX%\bin\
    set "SDL2_DLL_INSTALLED=1"
) else (
    echo [!] SDL2.dll not found
    set "SDL2_DLL_INSTALLED=0"
)

echo.

if "%SDL2_INSTALLED%"=="1" if "%SDL2_LIB_INSTALLED%"=="1" if "%SDL2_DLL_INSTALLED%"=="1" (
    echo ========================================
    echo SDL2 is already installed!
    echo ========================================
    echo.
    echo You can now run build_winxp.bat
    echo.
    pause
    exit /b 0
)

echo ========================================
echo SDL2 Installation Required
echo ========================================
echo.
echo To build for Windows XP, you need SDL2 development files.
echo.
echo Please follow these steps:
echo.
echo 1. Download SDL2 development libraries for MinGW:
echo    URL: https://github.com/libsdl-org/SDL/releases
echo    File: SDL2-devel-X.X.X-mingw.tar.gz (latest version)
echo.
echo 2. Extract the archive
echo.
echo 3. Copy files from i686-w64-mingw32 folder to your MinGW installation:
echo.
echo    From: SDL2-X.X.X\i686-w64-mingw32\include\SDL2\
echo    To:   %MINGW_PREFIX%\include\SDL2\
echo.
echo    From: SDL2-X.X.X\i686-w64-mingw32\lib\
echo    To:   %MINGW_PREFIX%\lib\
echo.
echo    From: SDL2-X.X.X\i686-w64-mingw32\bin\SDL2.dll
echo    To:   %MINGW_PREFIX%\bin\
echo.
echo Alternative: If you already have SDL2.dll, copy it to:
echo    %MINGW_PREFIX%\bin\SDL2.dll
echo.
echo ========================================
echo.

REM Check if user wants to open download page
echo Would you like to open the SDL2 releases page in your browser?
choice /C YN /M "Open SDL2 releases page"
if errorlevel 2 goto :skip_browser
if errorlevel 1 (
    start https://github.com/libsdl-org/SDL/releases
    echo.
    echo Browser opened. Download the MinGW development package.
)

:skip_browser
echo.
echo After installing SDL2, run this script again to verify the installation.
echo.
pause
