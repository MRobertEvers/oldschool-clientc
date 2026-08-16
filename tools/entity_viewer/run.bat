@echo off
setlocal EnableDelayedExpansion
rem
rem Run the entity viewer, refusing to serve a stale build.
rem
rem ## Why staleness is the thing this script is for
rem
rem The viewer compiles the SAME C twice: once into the native server and once
rem into web\ev_wasm.wasm through emcc. Edit ev_render.c, rebuild, and the
rem server is current while the browser silently keeps running the old
rem renderer - the page loads, the model draws, and nothing anywhere says the
rem two halves disagree. That failure has a particular flavour: you change a
rem kernel, see no difference, and conclude the change did nothing.
rem
rem So this checks both artefacts against their sources before serving, and by
rem default rebuilds what is stale rather than warning about it.
rem
rem Usage:
rem   tools\entity_viewer\run.bat [--cache DIR] [--rev NAME] [--catalog DIR]
rem                               [--names DIR] [--port N]
rem                               [--check-only] [--no-build] [--no-wasm]

set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
for %%I in ("%HERE%\..\..") do set "REPO=%%~fI"

set "CACHE=%REPO%\cache.osrs239"
set "REV=osrs239"
set "CATALOG=%REPO%\out\osrs239_anims"
set "NAMES=%REPO%\OSRS-Content\osrs239-content"
set "PORT=8099"
set "CHECK_ONLY=0"
set "DO_BUILD=1"
set "DO_WASM=1"
set "PASSTHRU="

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--cache"      ( set "CACHE=%~2" & shift & shift & goto parse )
if /i "%~1"=="--rev"        ( set "REV=%~2" & shift & shift & goto parse )
if /i "%~1"=="--catalog"    ( set "CATALOG=%~2" & shift & shift & goto parse )
if /i "%~1"=="--names"      ( set "NAMES=%~2" & shift & shift & goto parse )
if /i "%~1"=="--port"       ( set "PORT=%~2" & shift & shift & goto parse )
if /i "%~1"=="--check-only" ( set "CHECK_ONLY=1" & shift & goto parse )
if /i "%~1"=="--no-build"   ( set "DO_BUILD=0" & shift & goto parse )
if /i "%~1"=="--no-wasm"    ( set "DO_WASM=0" & shift & goto parse )
set "PASSTHRU=!PASSTHRU! %~1"
shift
goto parse
:parsed

rem The newest source timestamp across everything BOTH halves compile. A change
rem to any of these invalidates the server and the wasm together, which is the
rem case that goes unnoticed.
set "NEWEST=0"
call :newest "%HERE%\ev_render.c"
call :newest "%HERE%\ev_render.h"
call :newest "%HERE%\ev_wire.c"
call :newest "%HERE%\ev_wire.h"
call :newest "%HERE%\ev_server.c"
call :newest "%HERE%\ev_build.c"
for /r "%REPO%\3rd\toridraw" %%F in (*.c *.h *.inc) do call :newest "%%~fF"

call :stamp "%HERE%\ev_server.exe" SRV_T
if "%SRV_T%"=="0" call :stamp "%HERE%\ev_server" SRV_T
call :stamp "%HERE%\web\ev_wasm.wasm" WASM_T

set "SRV_STALE=0"
set "WASM_STALE=0"
if %SRV_T% LSS %NEWEST% set "SRV_STALE=1"
if %WASM_T% LSS %NEWEST% set "WASM_STALE=1"

echo entity viewer: build freshness
call :report %SRV_STALE% "ev_server" %SRV_T%
call :report %WASM_STALE% "web\ev_wasm.wasm" %WASM_T%

if "%CHECK_ONLY%"=="1" (
    if "%SRV_STALE%"=="1" exit /b 1
    if "%WASM_STALE%"=="1" exit /b 1
    exit /b 0
)

if "%SRV_STALE%"=="1" (
    if "%DO_BUILD%"=="1" (
        echo   rebuilding ev_server...
        make -C "%HERE%" ev_server || exit /b 1
    ) else (
        echo   WARNING: ev_server is stale and --no-build was given
    )
)

if "%WASM_STALE%"=="1" (
    if "%DO_WASM%"=="0" (
        echo   WARNING: the wasm is stale and --no-wasm was given -
        echo            the BROWSER will run the old renderer while the server is current.
    ) else (
        where emcc >nul 2>&1
        if errorlevel 1 (
            rem Refuse rather than serve a mismatch: this is the failure the
            rem script exists to prevent, and a warning scrolls past.
            echo.
            echo   ERROR: web\ev_wasm.wasm is stale and emcc is not on PATH.
            echo          The page would render with a build older than the server.
            echo          Install emsdk and re-run, or pass --no-wasm to accept it.
            exit /b 1
        ) else (
            echo   rebuilding web\ev_wasm.wasm...
            make -C "%HERE%" wasm || exit /b 1
        )
    )
)

set "ARGS=--rev %REV% "%CACHE%" --port %PORT% --web "%HERE%\web" --cache-root "%REPO%""
if exist "%CATALOG%" set "ARGS=%ARGS% --catalog "%CATALOG%""
if exist "%NAMES%"   set "ARGS=%ARGS% --names "%NAMES%""

echo.
echo   http://127.0.0.1:%PORT%/
echo.
"%HERE%\ev_server.exe" %ARGS%%PASSTHRU%
if errorlevel 9009 "%HERE%\ev_server" %ARGS%%PASSTHRU%
exit /b %errorlevel%

rem ---- helpers -------------------------------------------------------------

rem Fold one file's mtime into NEWEST. Uses the file's own timestamp rather
rem than a directory scan so a touched-but-unchanged tree does not read as new.
:newest
if not exist "%~1" goto :eof
set "T=%~t1"
set "T=%T:/=%"
set "T=%T::=%"
set "T=%T: =%"
if "%T%" GTR "%NEWEST%" set "NEWEST=%T%"
goto :eof

:stamp
if not exist "%~1" ( set "%~2=0" & goto :eof )
set "S=%~t1"
set "S=%S:/=%"
set "S=%S::=%"
set "S=%S: =%"
set "%~2=%S%"
goto :eof

:report
if "%~1"=="1" (
    echo   %~2 STALE
) else (
    if "%~3"=="0" ( echo   %~2 MISSING ) else ( echo   %~2 ok )
)
goto :eof
