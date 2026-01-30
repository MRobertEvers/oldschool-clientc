@echo off
REM Quick build script for Windows XP (minimal output)

setlocal

set "MINGW_BIN=C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2\mingw32\bin"
set "PATH=%MINGW_BIN%;%PATH%"
set "BUILD_DIR=build-winxp"

REM Convert to forward slashes for CMake
set "MINGW_BIN_FORWARD=%MINGW_BIN:\=/%"

echo Building for Windows XP...
echo.

REM Configure
cmake -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%MINGW_BIN_FORWARD%/gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW_BIN_FORWARD%/g++.exe" -DCMAKE_RC_COMPILER="%MINGW_BIN_FORWARD%/windres.exe" -DCMAKE_C_FLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -O3 -march=pentium4 -msse2" -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -O3 -march=pentium4 -msse2" -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" > nul

if errorlevel 1 (
    echo Configuration failed! Run build_winxp.bat for detailed output.
    pause
    exit /b 1
)

REM Build
cmake --build "%BUILD_DIR%" --config Release > nul

if errorlevel 1 (
    echo Build failed! Run build_winxp.bat for detailed output.
    pause
    exit /b 1
)

REM Copy DLLs
if exist "%MINGW_BIN%\libwinpthread-1.dll" copy /Y "%MINGW_BIN%\libwinpthread-1.dll" "%BUILD_DIR%\" > nul
if exist "%MINGW_BIN%\SDL2.dll" copy /Y "%MINGW_BIN%\SDL2.dll" "%BUILD_DIR%\" > nul
if exist "lib\SDL2.dll" copy /Y "lib\SDL2.dll" "%BUILD_DIR%\" > nul

echo.
echo Build complete: %BUILD_DIR%\
echo.

if exist "%BUILD_DIR%\scene_tile_test.exe" (
    echo   [OK] scene_tile_test.exe
)

if exist "%BUILD_DIR%\model_viewer.exe" (
    echo   [OK] model_viewer.exe
)

echo.
pause

