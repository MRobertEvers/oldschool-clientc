@echo off
REM Test Windows XP build outputs

setlocal

set "BUILD_DIR=build-winxp"

echo ========================================
echo Testing Windows XP Build Outputs
echo ========================================
echo.

if not exist "%BUILD_DIR%" (
    echo ERROR: Build directory not found: %BUILD_DIR%
    echo Please run build_winxp.bat first
    echo.
    pause
    exit /b 1
)

echo Checking executables...
echo.

set "ALL_OK=1"

if exist "%BUILD_DIR%\scene_tile_test.exe" (
    echo [OK] scene_tile_test.exe found
) else (
    echo [FAIL] scene_tile_test.exe NOT found
    set "ALL_OK=0"
)

if exist "%BUILD_DIR%\model_viewer.exe" (
    echo [OK] model_viewer.exe found
) else (
    echo [FAIL] model_viewer.exe NOT found
    set "ALL_OK=0"
)

echo.
echo Checking required DLLs...
echo.

if exist "%BUILD_DIR%\SDL2.dll" (
    echo [OK] SDL2.dll found
) else (
    echo [WARNING] SDL2.dll NOT found - application will not run without it
    set "ALL_OK=0"
)

if exist "%BUILD_DIR%\libwinpthread-1.dll" (
    echo [OK] libwinpthread-1.dll found
) else (
    echo [WARNING] libwinpthread-1.dll NOT found - may be needed
)

if exist "%BUILD_DIR%\libgcc_s_dw2-1.dll" (
    echo [INFO] libgcc_s_dw2-1.dll found (dynamically linked)
) else (
    echo [INFO] libgcc_s_dw2-1.dll not found (statically linked - OK)
)

if exist "%BUILD_DIR%\libstdc++-6.dll" (
    echo [INFO] libstdc++-6.dll found (dynamically linked)
) else (
    echo [INFO] libstdc++-6.dll not found (statically linked - OK)
)

echo.
echo ========================================

if "%ALL_OK%"=="1" (
    echo [SUCCESS] All critical files present!
    echo.
    echo You can deploy the %BUILD_DIR% directory to Windows XP.
    echo.
    echo Would you like to test scene_tile_test.exe now? (y/n^)
    set /p RUNTEST=
    if /i "!RUNTEST!"=="y" (
        echo.
        echo Running scene_tile_test.exe...
        echo (Press Ctrl+C if it hangs^)
        echo.
        cd "%BUILD_DIR%"
        scene_tile_test.exe
        cd ..
    )
) else (
    echo [FAIL] Some files are missing!
    echo Please check the build process.
)

echo ========================================
echo.
pause





