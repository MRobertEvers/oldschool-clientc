@echo off
REM MSYS2/MinGW Bash Shell Launcher
REM Tries multiple methods to launch a bash shell with MinGW

setlocal

set "MINGW_ROOT=C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2"
set "MINGW_BIN=%MINGW_ROOT%\mingw32\bin"

cls
echo ========================================
echo MinGW i686 Shell Launcher
echo ========================================
echo.

REM Method 1: Try MSYS2 shell launcher
if exist "%MINGW_ROOT%\msys2_shell.cmd" (
    echo Found MSYS2 shell launcher
    echo Starting MSYS2 MinGW32 environment...
    echo.
    "%MINGW_ROOT%\msys2_shell.cmd" -mingw32 -defterm -here -no-start
    exit /b 0
)

REM Method 2: Try direct bash from usr\bin
if exist "%MINGW_ROOT%\usr\bin\bash.exe" (
    echo Found MSYS2 bash in usr\bin
    echo Starting bash shell...
    echo.
    set "PATH=%MINGW_BIN%;%MINGW_ROOT%\usr\bin;%PATH%"
    set "MSYSTEM=MINGW32"
    set "MSYS2_PATH_TYPE=inherit"
    "%MINGW_ROOT%\usr\bin\bash.exe" --login -i
    exit /b 0
)

REM Method 3: Try bash from bin directory
if exist "%MINGW_BIN%\bash.exe" (
    echo Found bash in MinGW bin directory
    echo Starting bash shell...
    echo.
    set "PATH=%MINGW_BIN%;%PATH%"
    "%MINGW_BIN%\bash.exe" --login -i
    exit /b 0
)

REM Method 4: No bash found, offer alternatives
echo WARNING: No bash shell found in MinGW directory
echo.
echo Your MinGW distribution appears to be a standalone build without MSYS2.
echo.
echo Options:
echo   1. Use mingw_shell_simple.bat for a Windows CMD shell
echo   2. Install full MSYS2 from https://www.msys2.org/
echo   3. Use PowerShell: .\mingw_shell.ps1
echo   4. The build scripts work without a shell launcher
echo.
echo Falling back to CMD shell with MinGW in PATH...
echo.
pause

set "PATH=%MINGW_BIN%;%PATH%"
cmd /k "prompt [MinGW-i686] $P$G & echo Ready! Type 'exit' to close this shell."





