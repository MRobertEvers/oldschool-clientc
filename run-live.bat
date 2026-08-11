@echo off
rem Run torirs from a boot manifest, on Windows. The cmd.exe half of
rem run-live.sh -- same arguments, same environment variables, same result.
rem
rem   run-live.bat <manifest.ini> [user] [pass] [client args...]
rem
rem The manifest (manifest_osrs239_rs2012.ini, manifest_osrs230.ini, ...) names
rem the cache, rev, transport, host/port and RSA keys. user/pass default to
rem asdf/a, and anything after them is handed to the client verbatim, so
rem `run-live.bat manifest_osrs239_rs2012.ini testc test --soft3d` works.
rem
rem For osrs230 / osrs239 without --offline this always runs the in-process
rem server: it builds with EMBED_SERVER=1, sets TORIRS_TRANSPORT=embed, and
rem exports MOCK230_REV from the manifest so the embedded world writes the same
rem wire the client speaks.
rem
rem The server script pack is a SEPARATE build from the binary, and an embedded
rem server loads whatever script.dat was compiled last -- not what the tree says
rem today. Building the binary and not the pack is how a session ends up running
rem content nobody has written for weeks, with nothing anywhere reporting the
rem mismatch, so the pack is rebuilt here for every embedded run.
rem
rem Knobs:
rem   TORIRS_NO_BUILD=1   run the existing exe, skip both builds
rem   TORIRS_PRINT_ONLY=1 print what would run (rev, embed decision, argv) and
rem                       exit -- how you check a manifest resolves as expected
rem                       without booting a client
rem   TORIRS_TOOLCHAIN    MinGW bin directory (default toolchain\mingw64\bin)
rem   plus every TORIRS_* the client itself reads (TORIRS_NET_DEBUG=1,
rem   TORIRS_MAX_FRAMES, TORIRS_EXIT_BMP, TORIRS_ZBUFFER_NPCS=25000, ...)
rem
rem `run-live.sh web` has no equivalent here: the web lane needs emscripten and
rem a POSIX shell throughout. Use Git Bash and run-live.sh for that.

setlocal EnableExtensions EnableDelayedExpansion
pushd "%~dp0"

set "EXE=src\torirs_win64.exe"

if "%~1"=="" goto :usage
if /I "%~1"=="web" goto :web_note
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage

set "MANIFEST=%~1"
shift
set "USER_NAME=%~1"
if not defined USER_NAME set "USER_NAME=asdf"
if defined USER_NAME shift
set "PASS=%~1"
if not defined PASS set "PASS=a"
if defined PASS shift

rem Whatever is left goes to the client untouched. %1 rather than %~1 so a
rem quoted argument (TORIRS_NET_CHEAT style, with spaces) stays one argument.
set "EXTRA="
:collect
if "%~1"=="" goto :collected
set "EXTRA=!EXTRA! %1"
shift
goto :collect
:collected

if not exist "%MANIFEST%" (
    echo run-live.bat: manifest "%MANIFEST%" not found 1>&2
    popd & exit /b 1
)

rem rev comes from the manifest's [net:boot] section.
set "REV="
for /f "usebackq tokens=2 delims==" %%R in (`findstr /r /c:"^[ 	]*rev[ 	]*=" "%MANIFEST%"`) do (
    if not defined REV set "REV=%%R"
)
for /f "tokens=* delims= " %%T in ("!REV!") do set "REV=%%T"
if not defined REV (
    echo run-live.bat: no rev= in [net:boot] of "%MANIFEST%" 1>&2
    popd & exit /b 1
)

rem --offline never logs in, so it never wants the embedded server.
set "OFFLINE=0"
echo !EXTRA! | findstr /c:"--offline" >nul 2>&1 && set "OFFLINE=1"

set "EMBED=0"
if "!OFFLINE!"=="0" (
    if /I "!REV!"=="osrs230" set "EMBED=1"
    if /I "!REV!"=="osrs239" set "EMBED=1"
)

if "!EMBED!"=="1" (
    set "TORIRS_TRANSPORT=embed"
    if not defined MOCK230_REV set "MOCK230_REV=!REV!"
)

if "%TORIRS_PRINT_ONLY%"=="1" (
    echo manifest        : %MANIFEST%
    echo rev             : !REV!
    echo offline         : !OFFLINE!
    echo embedded server : !EMBED!
    if "!EMBED!"=="1" echo TORIRS_TRANSPORT: !TORIRS_TRANSPORT!  MOCK230_REV: !MOCK230_REV!
    echo argv            : "%EXE%" --manifest "%MANIFEST%" --user "%USER_NAME%" --pass "%PASS%"!EXTRA!
    popd
    exit /b 0
)

if "%TORIRS_NO_BUILD%"=="1" goto :run

rem --- toolchain -----------------------------------------------------------
rem The repository owns its compiler (lib\mingw64-win64-toolchain.zip, extracted
rem to toolchain\mingw64 by build_windows.ps1). src/makefile's recipes are POSIX,
rem so a real sh.exe has to be on PATH as well -- Git for Windows ships one.
set "TC=%TORIRS_TOOLCHAIN%"
if not defined TC set "TC=%CD%\toolchain\mingw64\bin"
if not exist "!TC!\mingw32-make.exe" (
    where mingw32-make.exe >nul 2>&1
    if errorlevel 1 (
        echo run-live.bat: no mingw32-make. Run .\build_windows.ps1 once to 1>&2
        echo   unpack toolchain\mingw64, or set TORIRS_TOOLCHAIN to a MinGW bin dir. 1>&2
        popd
        exit /b 1
    )
    set "TC="
)

set "SHBIN="
for %%D in ("C:\Program Files\Git\usr\bin" "C:\Program Files (x86)\Git\usr\bin" "%LOCALAPPDATA%\Programs\Git\usr\bin") do (
    if not defined SHBIN if exist "%%~D\sh.exe" set "SHBIN=%%~D"
)
if not defined SHBIN (
    where sh.exe >nul 2>&1
    if errorlevel 1 (
        echo run-live.bat: no sh.exe found. src\makefile uses POSIX recipes -- 1>&2
        echo   install Git for Windows, or put sh.exe on PATH. 1>&2
        popd
        exit /b 1
    )
)
if defined TC set "PATH=!TC!;!PATH!"
if defined SHBIN set "PATH=!SHBIN!;!PATH!"

set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"

rem --- build ---------------------------------------------------------------
if "!EMBED!"=="1" (
    echo run-live.bat: !REV! -- building with EMBED_SERVER=1 ^(in-process server, MOCK230_REV=!MOCK230_REV!^) 1>&2
    echo run-live.bat: building the server script pack... 1>&2
    mingw32-make -C src PLATFORM=win64 CC=gcc mock230-scripts
    if errorlevel 1 (
        echo run-live.bat: script pack build failed 1>&2
        popd
        exit /b 1
    )
    mingw32-make -C src EMBED_SERVER=1 CC=gcc -j!JOBS! win64
    if errorlevel 1 (
        echo run-live.bat: client build failed 1>&2
        popd
        exit /b 1
    )
) else (
    if not exist "%EXE%" (
        echo run-live.bat: building %EXE%... 1>&2
        mingw32-make -C src CC=gcc -j!JOBS! win64
        if errorlevel 1 (
            echo run-live.bat: client build failed 1>&2
            popd
            exit /b 1
        )
    )
)

:run
if not exist "%EXE%" (
    echo run-live.bat: %EXE% not found -- build it with .\build_windows.ps1 -Opt 1>&2
    popd & exit /b 1
)

"%EXE%" --manifest "%MANIFEST%" --user "%USER_NAME%" --pass "%PASS%"%EXTRA%
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%

:web_note
echo run-live.bat: the web lane needs emscripten and a POSIX shell throughout. 1>&2
echo   Use Git Bash:  ./run-live.sh web ^<manifest.ini^> [user] [pass] 1>&2
popd & exit /b 1

:usage
echo usage: run-live.bat ^<manifest.ini^> [user] [pass] [client args...]
echo.
echo   run-live.bat manifest_osrs239_rs2012.ini testc test
echo   run-live.bat manifest_osrs239_rs2012.ini testc test --soft3d
echo   set TORIRS_NO_BUILD=1 ^&^& run-live.bat manifest_osrs239_rs2012.ini testc test
popd & exit /b 1
