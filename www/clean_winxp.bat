@echo off
REM Clean Windows XP build artifacts

setlocal

set "BUILD_DIR=build-winxp"

echo ========================================
echo Clean Windows XP Build
echo ========================================
echo.

if exist "%BUILD_DIR%" (
    echo Removing build directory: %BUILD_DIR%
    rmdir /S /Q "%BUILD_DIR%"
    echo [OK] Build directory removed
) else (
    echo Build directory does not exist: %BUILD_DIR%
)

echo.
echo Clean complete!
echo.
pause





