@echo off
REM Setup script for Android Scene Tile Test
REM This script automates SDL2 installation and cache file copying

echo Scene Tile Test - Android Setup Script
echo ======================================

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo Error: CMakeLists.txt not found. Please run this script from the android directory.
    pause
    exit /b 1
)

REM Check if we're in the project root
if not exist "..\src" (
    echo Error: Project source directory not found. Please run this script from the android directory.
    pause
    exit /b 1
)

echo.
echo This script will:
echo 1. Download and setup SDL2 for Android
echo 2. Copy cache files from the main project
echo 3. Create necessary directories
echo.

set /p continue="Continue? (y/n): "
if /i not "%continue%"=="y" (
    echo Setup cancelled.
    pause
    exit /b 0
)

echo.
echo ==========================================
echo Step 1: Setting up SDL2 for Android
echo ==========================================

REM Create SDL2 directory structure
echo Creating SDL2 directory structure...
if not exist "src\main\cpp\SDL2" mkdir "src\main\cpp\SDL2"
if not exist "src\main\cpp\SDL2\include" mkdir "src\main\cpp\SDL2\include"
if not exist "src\main\cpp\SDL2\libs" mkdir "src\main\cpp\SDL2\libs"
if not exist "src\main\cpp\SDL2\libs\android" mkdir "src\main\cpp\SDL2\libs\android"

REM Check if SDL2 is already downloaded
if exist "src\main\cpp\SDL2\include\SDL.h" (
    echo SDL2 headers already exist. Skipping download.
    goto :copy_sdl2_libs
)

echo.
echo SDL2 not found. Please download SDL2 manually:
echo.
echo 1. Go to: https://www.libsdl.org/download-2.0.php
echo 2. Download "SDL2-devel-2.x.x-VC.zip" (Windows) or "SDL2-devel-2.x.x-mingw.tar.gz"
echo 3. Extract the archive
echo 4. Copy the following files:
echo    - SDL2/include/* to android/src/main/cpp/SDL2/include/
echo    - SDL2/libs/android/* to android/src/main/cpp/SDL2/libs/android/
echo.
echo After copying the files, run this script again.
echo.

set /p sdl2_ready="Have you copied the SDL2 files? (y/n): "
if /i not "%sdl2_ready%"=="y" (
    echo Please copy SDL2 files and run this script again.
    pause
    exit /b 1
)

:copy_sdl2_libs
echo.
echo Verifying SDL2 installation...
if not exist "src\main\cpp\SDL2\include\SDL.h" (
    echo Error: SDL2 headers not found. Please copy SDL2/include/* to src/main/cpp/SDL2/include/
    pause
    exit /b 1
)

echo SDL2 headers found.

REM Check for SDL2 libraries
if not exist "src\main\cpp\SDL2\libs\android" (
    echo Warning: SDL2 Android libraries not found.
    echo Please copy SDL2/libs/android/* to src/main/cpp/SDL2/libs/android/
    echo The app may not build without these libraries.
)

echo.
echo ==========================================
echo Step 2: Setting up cache files
echo ==========================================

REM Create assets directory
echo Creating assets directory structure...
if not exist "src\main\assets" mkdir "src\main\assets"
if not exist "src\main\assets\cache" mkdir "src\main\assets\cache"

REM Check if cache directory exists in main project
if not exist "..\cache" (
    echo Warning: Cache directory not found in main project.
    echo Please ensure the cache directory exists at: ..\cache
    echo.
    set /p cache_continue="Continue without cache files? (y/n): "
    if /i not "%cache_continue%"=="y" (
        echo Setup cancelled.
        pause
        exit /b 1
    )
    goto :create_placeholder_cache
)

echo Found cache directory in main project.
echo Copying cache files...

REM Copy cache files
xcopy "..\cache\*" "src\main\assets\cache\" /E /I /Y
if %errorlevel% neq 0 (
    echo Error: Failed to copy cache files.
    pause
    exit /b 1
)

echo Cache files copied successfully.

REM Verify essential cache files
echo Verifying essential cache files...
if not exist "src\main\assets\cache\main_file_cache.dat2" (
    echo Warning: main_file_cache.dat2 not found. This is required for the app to work.
)

if not exist "src\main\assets\cache\xteas.json" (
    echo Warning: xteas.json not found. The app will work without XTEA keys.
)

goto :setup_complete

:create_placeholder_cache
echo Creating placeholder cache structure...
echo Creating placeholder files for testing...

REM Create a minimal xteas.json
echo {^} > "src\main\assets\cache\xteas.json"
echo   "keys": [] >> "src\main\assets\cache\xteas.json"
echo {^} >> "src\main\assets\cache\xteas.json"

echo Placeholder cache files created.

:setup_complete
echo.
echo ==========================================
echo Setup Complete!
echo ==========================================
echo.
echo Next steps:
echo 1. Open Android Studio
echo 2. Open the 'android' directory as a project
echo 3. Sync the project with Gradle files
echo 4. Build and run on a device or emulator
echo.
echo Or use the command line:
echo   gradlew assembleDebug
echo.
echo Troubleshooting:
echo - If SDL2 is not found, ensure you copied the files correctly
echo - If cache files are missing, copy them from the main project
echo - Check device logs with: adb logcat ^| findstr SceneTileTest
echo.

pause
