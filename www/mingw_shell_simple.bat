@echo off
REM Simple MinGW Shell - No fancy output, just sets PATH and launches

set "MINGW_BIN=C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2\mingw32\bin"
set "PATH=%MINGW_BIN%;%PATH%"

echo MinGW i686 shell ready
echo.

cmd /k "prompt [MinGW] $P$G"





