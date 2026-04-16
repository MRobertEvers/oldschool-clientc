@echo off
setlocal
if "%CI_OUT_ZIP%"=="" (
  echo CI_OUT_ZIP is required 1>&2
  exit /b 1
)
if "%CI_REPO_ROOT%"=="" (
  echo CI_REPO_ROOT is required 1>&2
  exit /b 1
)
python "%~dp0build_stub.py"
if errorlevel 1 exit /b 1
exit /b 0
