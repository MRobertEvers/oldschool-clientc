@echo off
REM Quick setup script for Android Scene Tile Test
REM This script performs all setup steps automatically

echo Scene Tile Test - Quick Setup Script
echo ====================================

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo Error: CMakeLists.txt not found. Please run this script from the android directory.
    pause
    exit /b 1
)

echo.
echo This script will automatically:
echo 1. Download and setup SDL2 for Android
echo 2. Copy cache files from the main project
echo 3. Create all necessary directories
echo 4. Verify the setup
echo.

set /p continue="Continue with automatic setup? (y/n): "
if /i not "%continue%"=="y" (
    echo Setup cancelled.
    pause
    exit /b 0
)

echo.
echo ==========================================
echo Starting automatic setup...
echo ==========================================

REM Step 1: Download SDL2
echo.
echo [1/4] Downloading SDL2...
call download_sdl2.bat
if %errorlevel% neq 0 (
    echo SDL2 download failed. Please check the error messages above.
    pause
    exit /b 1
)

REM Step 2: Copy cache files
echo.
echo [2/4] Setting up cache files...

REM Create assets directory
if not exist "src\main\assets" mkdir "src\main\assets"
if not exist "src\main\assets\cache" mkdir "src\main\assets\cache"

REM Check if cache directory exists in main project
if exist "..\cache" (
    echo Copying cache files from main project...
    xcopy "..\cache\*" "src\main\assets\cache\" /E /I /Y
    if %errorlevel% equ 0 (
        echo Cache files copied successfully.
    ) else (
        echo Warning: Failed to copy some cache files.
    )
) else (
    echo Warning: Cache directory not found in main project.
    echo Creating placeholder cache files...
    
    REM Create a minimal xteas.json
    echo {^} > "src\main\assets\cache\xteas.json"
    echo   "keys": [] >> "src\main\assets\cache\xteas.json"
    echo {^} >> "src\main\assets\cache\xteas.json"
    
    echo Placeholder cache files created.
)

REM Step 3: Create additional directories
echo.
echo [3/4] Creating additional directories...

REM Create gradle wrapper directory if it doesn't exist
if not exist "gradle\wrapper" mkdir "gradle\wrapper"

REM Create build directory
if not exist "build" mkdir "build"

echo Additional directories created.

REM Step 4: Verification
echo.
echo [4/4] Verifying setup...

set VERIFICATION_PASSED=1

REM Check SDL2 headers
if exist "src\main\cpp\SDL2\include\SDL.h" (
    echo ✓ SDL2 headers found
) else (
    echo ✗ SDL2 headers missing
    set VERIFICATION_PASSED=0
)

REM Check SDL2 libraries directory
if exist "src\main\cpp\SDL2\libs\android" (
    echo ✓ SDL2 Android libraries directory found
) else (
    echo ✗ SDL2 Android libraries directory missing
    set VERIFICATION_PASSED=0
)

REM Check cache directory
if exist "src\main\assets\cache" (
    echo ✓ Cache directory found
) else (
    echo ✗ Cache directory missing
    set VERIFICATION_PASSED=0
)

REM Check essential files
if exist "CMakeLists.txt" (
    echo ✓ CMakeLists.txt found
) else (
    echo ✗ CMakeLists.txt missing
    set VERIFICATION_PASSED=0
)

if exist "build.gradle" (
    echo ✓ build.gradle found
) else (
    echo ✗ build.gradle missing
    set VERIFICATION_PASSED=0
)

if exist "src\main\AndroidManifest.xml" (
    echo ✓ AndroidManifest.xml found
) else (
    echo ✗ AndroidManifest.xml missing
    set VERIFICATION_PASSED=0
)

echo.
echo ==========================================
echo Setup Summary
echo ==========================================

if %VERIFICATION_PASSED% equ 1 (
    echo ✓ Setup completed successfully!
    echo.
    echo Next steps:
    echo 1. Open Android Studio
    echo 2. Open the 'android' directory as a project
    echo 3. Sync the project with Gradle files
    echo 4. Build and run on a device or emulator
    echo.
    echo Or use the command line:
    echo   gradlew assembleDebug
) else (
    echo ✗ Setup completed with warnings.
    echo Please check the missing components above.
    echo You may need to manually install SDL2 or copy cache files.
)

echo.
echo Troubleshooting:
echo - If SDL2 is not found, run: download_sdl2.bat
echo - If cache files are missing, copy them from the main project
echo - Check device logs with: adb logcat ^| findstr SceneTileTest
echo.

pause
