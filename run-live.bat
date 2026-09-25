@echo off
rem Run torirs from a boot manifest, on Windows -- for a cmd.exe prompt, or a
rem double-click. This is a shim: run-live.ps1 is the implementation.
rem
rem   run-live.bat <manifest.ini> [user] [pass] [client args...]
rem
rem There were two Windows launchers for a while and they disagreed about
rem credentials -- the batch one defaulted to asdf/a and never read the
rem manifest's own user=/pass=, so a self-contained manifest silently logged in
rem as the wrong account. One implementation, in PowerShell, is why that cannot
rem happen again. See run-live.ps1 for the manifest keys it reads, the composed
rem cache bakes it triggers, and the TORIRS_* knobs.
rem
rem `run-live.sh web` has no equivalent here: the web lane needs emscripten and
rem a POSIX shell throughout. Use Git Bash and run-live.sh for that.

setlocal EnableExtensions
set "PS1=%~dp0run-live.ps1"

if "%~1"=="" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage

rem Positional user/pass, as this script has always taken them. An empty value
rem is passed as "" so run-live.ps1 falls back to the manifest, then asdf/a.
set "MANIFEST=%~1"
shift
set "USER_NAME=%~1"
if defined USER_NAME shift
set "PASS=%~1"
if defined PASS shift

rem %1 rather than %~1 so a quoted client argument (TORIRS_NET_CHEAT style, with
rem spaces) survives as one argument.
set "EXTRA="
:collect
if "%~1"=="" goto :collected
set "EXTRA=%EXTRA% %1"
shift
goto :collect
:collected

rem No `--` separator before %EXTRA%: PowerShell rejects a bare `--` as an
rem ambiguous parameter name. Client arguments are long-form (--soft3d,
rem --opengl3), so they never match a parameter and land in ClientArgs anyway.
rem
rem Manifest/user/pass are now positional on the PowerShell side too (it takes
rem one raw argument array, exactly like run-live.sh's $1/shift), so they are
rem simply forwarded in order rather than converted to -User/-Pass.
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" "%MANIFEST%" "%USER_NAME%" "%PASS%"%EXTRA%
exit /b %ERRORLEVEL%

:usage
echo usage: run-live.bat ^<manifest.ini^> [user] [pass] [client args...]
echo.
echo   run-live.bat manifests/manifest_osrs239_rs2012.ini
echo   run-live.bat manifests/manifest_osrs239_rs2012.ini testc test --soft3d
echo.
echo   PowerShell prompt:  .\run-live.ps1 manifests/manifest_osrs239_rs2012.ini
exit /b 1
