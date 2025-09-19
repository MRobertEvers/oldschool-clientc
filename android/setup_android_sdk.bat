@echo off
REM Setup Android SDK and NDK for Scene Tile Test

echo Scene Tile Test - Android SDK Setup Script
echo ==========================================

echo.
echo This script will help you set up the Android SDK and NDK.
echo.

REM Check if Android SDK is already installed
if defined ANDROID_HOME (
    echo Android SDK found at: %ANDROID_HOME%
    goto :check_ndk
)

echo Android SDK not found. Please install it:
echo.
echo Option 1: Install Android Studio (Recommended)
echo 1. Go to: https://developer.android.com/studio
echo 2. Download and install Android Studio
echo 3. Open Android Studio and install the SDK
echo.
echo Option 2: Install SDK Command Line Tools
echo 1. Go to: https://developer.android.com/studio#command-tools
echo 2. Download "Command line tools only"
echo 3. Extract to a directory like C:\Android\Sdk
echo.

set /p sdk_path="Enter the path to your Android SDK (or press Enter to skip): "
if not "%sdk_path%"=="" (
    set ANDROID_HOME=%sdk_path%
    echo Android SDK set to: %ANDROID_HOME%
) else (
    echo Please install Android SDK and run this script again.
    pause
    exit /b 1
)

:check_ndk
REM Check if Android NDK is installed
if defined ANDROID_NDK_HOME (
    echo Android NDK found at: %ANDROID_NDK_HOME%
    goto :set_environment
)

echo Android NDK not found. Please install it:
echo.
echo Option 1: Install via Android Studio
echo 1. Open Android Studio
echo 2. Go to Tools ^> SDK Manager
echo 3. Click on "SDK Tools" tab
echo 4. Check "NDK (Side by side)" and install
echo.
echo Option 2: Download manually
echo 1. Go to: https://developer.android.com/ndk/downloads
echo 2. Download the NDK
echo 3. Extract to %ANDROID_HOME%\ndk\25.2.9519653
echo.

set /p ndk_path="Enter the path to your Android NDK (or press Enter to skip): "
if not "%ndk_path%"=="" (
    set ANDROID_NDK_HOME=%ndk_path%
    echo Android NDK set to: %ANDROID_NDK_HOME%
) else (
    echo Please install Android NDK and run this script again.
    pause
    exit /b 1
)

:set_environment
echo.
echo ==========================================
echo Setting Environment Variables
echo ==========================================

echo Setting ANDROID_HOME=%ANDROID_HOME%
echo Setting ANDROID_NDK_HOME=%ANDROID_NDK_HOME%

REM Set environment variables for current session
set ANDROID_HOME=%ANDROID_HOME%
set ANDROID_NDK_HOME=%ANDROID_NDK_HOME%

REM Add to PATH
set PATH=%PATH%;%ANDROID_HOME%\tools;%ANDROID_HOME%\platform-tools;%ANDROID_NDK_HOME%

echo.
echo Environment variables set for current session.
echo.
echo To make these permanent, add them to your system environment variables:
echo ANDROID_HOME=%ANDROID_HOME%
echo ANDROID_NDK_HOME=%ANDROID_NDK_HOME%
echo.
echo And add these to your PATH:
echo %ANDROID_HOME%\tools
echo %ANDROID_HOME%\platform-tools
echo %ANDROID_NDK_HOME%
echo.

echo ==========================================
echo Verification
echo ==========================================

REM Check if adb is available
adb version >nul 2>&1
if %errorlevel% equ 0 (
    echo ✓ ADB (Android Debug Bridge) found
) else (
    echo ✗ ADB not found - check your PATH
)

REM Check if ndk-build is available
ndk-build --version >nul 2>&1
if %errorlevel% equ 0 (
    echo ✓ NDK build tools found
) else (
    echo ✗ NDK build tools not found - check your NDK installation
)

echo.
echo Android SDK setup completed!
echo You can now try building the project.
echo.

pause
