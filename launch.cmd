@echo off
rem Windows shim for ./launch -- and, where Python cannot run it, a standalone
rem implementation of the one thing that matters.
rem
rem   launch.cmd list
rem   launch.cmd run <profile> [client args...]
rem
rem TWO MACHINES, ONE COMMAND.
rem
rem On a development host this delegates to the Python launcher, which resolves
rem profiles, composes manifests, bakes derived artifacts and supervises
rem services. Nothing about that behaviour changes.
rem
rem On the Windows XP box it cannot. That machine has Python 3.2 (2011), and
rem tools/launcher uses subprocess.run, which arrived in 3.5 -- so the launcher
rem does not merely misbehave there, it raises. The last CPython that installs
rem on XP at all is 3.4, so this is not fixable by upgrading the box, and
rem porting the launcher back to 3.2 would make three files carry a 2011
rem dialect forever to serve one machine that only ever needs to do one thing:
rem name an already-composed manifest and start the client.
rem
rem So when no usable Python is found, that one thing is done here in batch.
rem The manifests are already composed -- generated on the dev host from
rem profiles/ and copied across -- so the fallback reads build\manifests\ and
rem never needs to understand a profile at all.

setlocal EnableExtensions
cd /d "%~dp0"

rem ---------------------------------------------------------------- python --
rem `py` is the python.org launcher and is not always present; a Store or conda
rem install gives you `python` and no `py`. Require 3.5+, because that is what
rem tools/launcher actually needs -- a 3.2 that answers `python --version`
rem happily would otherwise be picked and then fail deep inside a run.
rem %%~$PATH:P searches PATH and expands to a full path or to nothing. Used
rem rather than `where`, which does not exist on Windows XP -- there the probe
rem printed "'where' is not recognized" twice and then fell through, which
rem worked by accident and read like a broken script.
set "PYEXE="
for %%P in (py.exe python.exe) do (
    if not defined PYEXE if not "%%~$PATH:P"=="" call :try_python "%%~$PATH:P"
)

if defined PYEXE (
    "%PYEXE%" "%~dp0launch" %*
    endlocal
    exit /b %errorlevel%
)

rem ------------------------------------------------------- batch fallback --
if "%~1"=="" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage
if /I "%~1"=="list" goto :do_list
if /I "%~1"=="run" goto :do_run

echo launch: no usable Python here, and '%~1' is not one of the commands this>&2
echo         fallback implements ^(list, run^). See the header.>&2
endlocal
exit /b 2

:do_list
echo Composed manifests in build\manifests ^(no Python here, so these are what
echo there is -- regenerate on the dev host with launch.cmd show ^<profile^>^):
echo.
if not exist "build\manifests\*.ini" goto :no_manifests
for %%F in (build\manifests\*.ini) do echo   %%~nF
endlocal
exit /b 0

:no_manifests
echo   ^(none^) -- copy build\manifests\ across from the dev host.>&2
endlocal
exit /b 1

:do_run
shift
set "PROFILE=%~1"
if not defined PROFILE goto :usage
shift

set "MANIFEST=build\manifests\%PROFILE%.ini"
if not exist "%MANIFEST%" goto :no_such_profile

rem The client, wherever it is. A pushed build sits beside this script; a repo
rem checkout keeps it in src\. Checking both means the same command works in
rem either layout instead of one of them being a special case.
set "EXE="
if exist "torirs.exe"     set "EXE=torirs.exe"
if not defined EXE if exist "src\torirs.exe" set "EXE=src\torirs.exe"
if not defined EXE goto :no_exe

rem Everything after the profile goes to the client untouched, so --soft3d,
rem --d3d9, --user and the rest work exactly as they do on the dev host. No
rem --user/--pass are added: the manifest states its own, and inventing a
rem default here is how the two Windows launchers used to disagree about which
rem account a self-contained manifest logs in as.
set "EXTRA="
:collect
if "%~1"=="" goto :collected
set "EXTRA=%EXTRA% %1"
shift
goto :collect
:collected

echo launch: %EXE% --manifest %MANIFEST%%EXTRA%
"%EXE%" --manifest "%MANIFEST%"%EXTRA%
endlocal
exit /b %errorlevel%

:no_such_profile
echo launch: no composed manifest for '%PROFILE%' at %MANIFEST%>&2
echo         launch.cmd list   shows what is here.>&2
endlocal
exit /b 1

:no_exe
echo launch: torirs.exe is not here or in src\. Push a build from the dev host.>&2
endlocal
exit /b 1

:try_python
rem Accept only 3.5 or newer -- that is what tools/launcher needs
rem (subprocess.run). The XP box's Python 3.2 answers `--version` perfectly
rem happily, so a probe that only checked for A python would pick it and then
rem fail deep inside a run, which is the worse failure of the two.
for /f "tokens=2" %%V in ('"%~1" --version 2^>^&1') do call :check_ver "%~1" "%%V"
goto :eof

:check_ver
if defined PYEXE goto :eof
for /f "tokens=1,2 delims=." %%A in ("%~2") do (
    if "%%A"=="3" if not "%%B"=="" (
        if %%B GEQ 5 set "PYEXE=%~1"
    )
)
goto :eof

:usage
echo usage: launch.cmd ^<command^> [args...]
echo.
echo   launch.cmd list                     profiles ^(or composed manifests^)
echo   launch.cmd run ^<profile^> [args...]  build if possible, then run
echo.
echo   launch.cmd run rs289lc-xp --soft3d
echo.
echo With Python 3.5+ this is the full launcher ^(show, bench, status, ...^).
echo Without it -- the XP box -- only list and run, straight from
echo build\manifests\.
endlocal
exit /b 1
