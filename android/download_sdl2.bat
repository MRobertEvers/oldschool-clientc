@echo off
REM Automatic SDL2 download script for Android Scene Tile Test
REM This script downloads and extracts SDL2 for Android

echo Scene Tile Test - SDL2 Download Script
echo ======================================

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo Error: CMakeLists.txt not found. Please run this script from the android directory.
    pause
    exit /b 1
)

echo.
echo This script will download SDL2 for Android and set it up automatically.
echo.

set /p continue="Continue? (y/n): "
if /i not "%continue%"=="y" (
    echo Download cancelled.
    pause
    exit /b 0
)

REM Create temporary directory
set TEMP_DIR=%TEMP%\sdl2_download
if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"
mkdir "%TEMP_DIR%"

echo.
echo ==========================================
echo Downloading SDL2...
echo ==========================================

REM Try to download SDL2 using PowerShell (Windows 10+)
echo Attempting to download SDL2 using PowerShell...

powershell -Command "& {try {Invoke-WebRequest -Uri 'https://www.libsdl.org/release/SDL2-devel-2.28.5-VC.zip' -OutFile '%TEMP_DIR%\SDL2.zip' -UseBasicParsing; Write-Host 'Download successful'} catch {Write-Host 'PowerShell download failed: ' $_.Exception.Message; exit 1}}"

if %errorlevel% neq 0 (
    echo PowerShell download failed. Trying alternative method...
    
    REM Try using curl if available
    curl --version >nul 2>&1
    if %errorlevel% equ 0 (
        echo Using curl to download SDL2...
        curl -L -o "%TEMP_DIR%\SDL2.zip" "https://www.libsdl.org/release/SDL2-devel-2.28.5-VC.zip"
        if %errorlevel% neq 0 (
            echo curl download failed.
            goto :manual_download
        )
    ) else (
        echo curl not available.
        goto :manual_download
    )
)

REM Check if download was successful
if not exist "%TEMP_DIR%\SDL2.zip" (
    echo Download failed.
    goto :manual_download
)

echo Download successful!

echo.
echo ==========================================
echo Extracting SDL2...
echo ==========================================

REM Extract the zip file
echo Extracting SDL2...
powershell -Command "& {Expand-Archive -Path '%TEMP_DIR%\SDL2.zip' -DestinationPath '%TEMP_DIR%' -Force}"

if %errorlevel% neq 0 (
    echo Extraction failed.
    goto :manual_download
)

echo Extraction successful!

echo.
echo ==========================================
echo Setting up SDL2 files...
echo ==========================================

REM Create SDL2 directory structure
echo Creating SDL2 directory structure...
if not exist "src\main\cpp\SDL2" mkdir "src\main\cpp\SDL2"
if not exist "src\main\cpp\SDL2\include" mkdir "src\main\cpp\SDL2\include"
if not exist "src\main\cpp\SDL2\libs" mkdir "src\main\cpp\SDL2\libs"
if not exist "src\main\cpp\SDL2\libs\android" mkdir "src\main\cpp\SDL2\libs\android"

REM Copy SDL2 headers
echo Copying SDL2 headers...
xcopy "%TEMP_DIR%\SDL2-2.28.5\include\*" "src\main\cpp\SDL2\include\" /E /I /Y
if %errorlevel% neq 0 (
    echo Failed to copy SDL2 headers.
    goto :cleanup_and_exit
)

REM Copy SDL2 Android libraries (if they exist)
echo Copying SDL2 Android libraries...
if exist "%TEMP_DIR%\SDL2-2.28.5\lib\android" (
    xcopy "%TEMP_DIR%\SDL2-2.28.5\lib\android\*" "src\main\cpp\SDL2\libs\android\" /E /I /Y
    if %errorlevel% neq 0 (
        echo Warning: Failed to copy SDL2 Android libraries.
    ) else (
        echo SDL2 Android libraries copied successfully.
    )
) else (
    echo Warning: SDL2 Android libraries not found in the downloaded package.
    echo You may need to download them separately or build SDL2 from source.
)

echo.
echo ==========================================
echo Verification...
echo ==========================================

REM Verify installation
if exist "src\main\cpp\SDL2\include\SDL.h" (
    echo ✓ SDL2 headers installed successfully.
) else (
    echo ✗ SDL2 headers not found.
    goto :cleanup_and_exit
)

if exist "src\main\cpp\SDL2\libs\android" (
    echo ✓ SDL2 Android libraries directory created.
) else (
    echo ✗ SDL2 Android libraries directory not found.
)

echo.
echo SDL2 setup completed!
echo You can now run the main setup script or build the project.
goto :cleanup_and_exit

:manual_download
echo.
echo ==========================================
echo Manual Download Required
echo ==========================================
echo.
echo Automatic download failed. Please download SDL2 manually:
echo.
echo 1. Go to: https://www.libsdl.org/download-2.0.php
echo 2. Download "SDL2-devel-2.28.5-VC.zip" (Windows)
echo 3. Extract the archive
echo 4. Copy the following files:
echo    - SDL2-2.28.5/include/* to android/src/main/cpp/SDL2/include/
echo    - SDL2-2.28.5/lib/android/* to android/src/main/cpp/SDL2/libs/android/
echo.
echo After copying the files, run the setup script again.
echo.

:cleanup_and_exit
REM Clean up temporary files
if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"

echo.
pause
