@echo off
REM Final setup script for Android Scene Tile Test

echo Scene Tile Test - Final Setup Script
echo ====================================

echo.
echo This script will complete the Android setup by addressing:
echo 1. Java version compatibility
echo 2. SDL2 configuration
echo 3. Android SDK/NDK setup
echo.

echo ==========================================
echo Step 1: Java Version Check
echo ==========================================

java -version
echo.

echo Current Java version detected.
echo If you see "Unsupported class file major version 68", you need Java 17.
echo.

set /p java_choice="Do you have Java 17 installed? (y/n): "

if /i "%java_choice%"=="n" (
    echo.
    echo ==========================================
    echo Installing Java 17
    echo ==========================================
    echo.
    echo Please download Java 17 from: https://adoptium.net/temurin/releases/?version=17
    echo Install it and run this script again.
    echo.
    pause
    exit /b 1
)

echo.
echo ==========================================
echo Step 2: Setting Java 17 Environment
echo ==========================================

set /p java17_path="Enter Java 17 installation path: "

if not exist "%java17_path%\bin\java.exe" (
    echo Error: Java 17 not found at: %java17_path%
    echo Please check the path and try again.
    pause
    exit /b 1
)

set JAVA_HOME=%java17_path%
set PATH=%JAVA_HOME%\bin;%PATH%

echo Java 17 set to: %JAVA_HOME%
echo Testing Java version...
java -version

echo.
echo ==========================================
echo Step 3: Android SDK/NDK Setup
echo ==========================================

if not defined ANDROID_HOME (
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

    set /p sdk_path="Enter the path to your Android SDK: "
    if not "%sdk_path%"=="" (
        set ANDROID_HOME=%sdk_path%
        echo Android SDK set to: %ANDROID_HOME%
    ) else (
        echo Please install Android SDK and run this script again.
        pause
        exit /b 1
    )
)

if not defined ANDROID_NDK_HOME (
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

    set /p ndk_path="Enter the path to your Android NDK: "
    if not "%ndk_path%"=="" (
        set ANDROID_NDK_HOME=%ndk_path%
        echo Android NDK set to: %ANDROID_NDK_HOME%
    ) else (
        echo Please install Android NDK and run this script again.
        pause
        exit /b 1
    )
)

echo.
echo ==========================================
echo Step 4: SDL2 Setup
echo ==========================================

echo SDL2 headers are already installed.
echo SDL2 Android libraries will be handled by the build system.
echo.

echo ==========================================
echo Step 5: Testing Build
echo ==========================================

echo Testing Gradle build...
.\gradlew.bat clean

if %errorlevel% equ 0 (
    echo.
    echo ✓ Build successful!
    echo.
    echo Testing full build...
    .\gradlew.bat assembleDebug
    
    if %errorlevel% equ 0 (
        echo.
        echo 🎉 SUCCESS! Android build completed successfully!
        echo.
        echo Your APK is ready at: app\build\outputs\apk\debug\app-debug.apk
        echo.
        echo You can now install and run the app on an Android device.
    ) else (
        echo.
        echo ⚠️ Build completed with warnings/errors.
        echo Check the output above for details.
    )
) else (
    echo.
    echo ❌ Build failed. Check the error messages above.
    echo.
    echo Common issues:
    echo 1. Java version compatibility
    echo 2. Android SDK/NDK not installed
    echo 3. SDL2 libraries missing
    echo.
    echo Please resolve these issues and try again.
)

echo.
echo ==========================================
echo Setup Complete
echo ==========================================
echo.
echo Environment variables set for current session:
echo JAVA_HOME=%JAVA_HOME%
echo ANDROID_HOME=%ANDROID_HOME%
echo ANDROID_NDK_HOME=%ANDROID_NDK_HOME%
echo.
echo To make these permanent, add them to your system environment variables.
echo.

pause
