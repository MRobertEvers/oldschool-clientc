@echo off
REM MSYS2 Shell Launcher for MinGW i686
REM Launches MSYS2 bash shell with MinGW toolchain

setlocal

set "MINGW_ROOT=C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2"
set "MSYS2_ROOT=%MINGW_ROOT%"

REM Check if MSYS2 exists
if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo ERROR: MSYS2 shell not found at: %MSYS2_ROOT%
    echo.
    echo This MinGW distribution may not include MSYS2.
    echo.
    echo Options:
    echo   1. Use mingw_shell_simple.bat for a regular CMD shell
    echo   2. Install full MSYS2 from https://www.msys2.org/
    echo   3. The build scripts work without a shell launcher
    echo.
    pause
    exit /b 1
)

cls
echo ========================================
echo Launching MSYS2 MinGW32 Shell
echo ========================================
echo.
echo MSYS2 Root: %MSYS2_ROOT%
echo Architecture: i686 (32-bit)
echo.
echo Starting bash shell...
echo.

REM Launch MSYS2 with MinGW32 environment
REM -mingw32 = Use MinGW32 (32-bit) environment
REM -defterm = Use default terminal
REM -here = Start in current directory
REM -no-start = Don't start a new window
"%MSYS2_ROOT%\msys2_shell.cmd" -mingw32 -defterm -here -no-start

