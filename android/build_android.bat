@echo off
REM Build script for Android Scene Tile Test
REM This script helps set up and build the Android project

echo Scene Tile Test - Android Build Script
echo =====================================

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo Error: CMakeLists.txt not found. Please run this script from the android directory.
    pause
    exit /b 1
)

REM Check for required tools
echo Checking for required tools...

REM Check for Java
java -version >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Java not found. Please install JDK 8 or higher.
    pause
    exit /b 1
)

REM Check for Android SDK
if not defined ANDROID_HOME (
    echo Warning: ANDROID_HOME not set. Please set it to your Android SDK path.
    echo Example: set ANDROID_HOME=C:\Users\%USERNAME%\AppData\Local\Android\Sdk
    pause
)

REM Check for Android NDK
if not defined ANDROID_NDK_HOME (
    echo Warning: ANDROID_NDK_HOME not set. Please set it to your Android NDK path.
    echo Example: set ANDROID_NDK_HOME=%ANDROID_HOME%\ndk\25.2.9519653
    pause
)

echo.
echo Building Android project...
echo.

REM Clean previous build
echo Cleaning previous build...
call gradlew clean

REM Build debug version
echo Building debug APK...
call gradlew assembleDebug

if %errorlevel% equ 0 (
    echo.
    echo Build successful!
    echo APK location: app\build\outputs\apk\debug\app-debug.apk
    echo.
    echo To install on device:
    echo adb install app\build\outputs\apk\debug\app-debug.apk
) else (
    echo.
    echo Build failed! Check the error messages above.
)

echo.
pause
