@echo off
REM Switch to Java 17 for Android Scene Tile Test

echo Scene Tile Test - Java 17 Switch Script
echo =======================================

echo.
echo Current Java version:
java -version

echo.
echo ==========================================
echo Java 17 Installation Guide
echo ==========================================
echo.
echo To fix the Java version compatibility issue:
echo.
echo 1. Download Java 17 from: https://adoptium.net/temurin/releases/?version=17
echo 2. Install Java 17 (recommended location: C:\Program Files\Eclipse Adoptium\jdk-17.x.x.x-hotspot)
echo 3. Run this script again to set JAVA_HOME
echo.

set /p java17_path="Enter Java 17 installation path (or press Enter to skip): "

if "%java17_path%"=="" (
    echo.
    echo No path entered. Please install Java 17 and run this script again.
    echo.
    echo Download link: https://adoptium.net/temurin/releases/?version=17
    pause
    exit /b 1
)

REM Check if the path exists
if not exist "%java17_path%" (
    echo.
    echo Error: Path does not exist: %java17_path%
    echo Please check the path and try again.
    pause
    exit /b 1
)

REM Check if it's a valid Java installation
if not exist "%java17_path%\bin\java.exe" (
    echo.
    echo Error: Java installation not found at: %java17_path%
    echo Please check the path and try again.
    pause
    exit /b 1
)

echo.
echo Setting JAVA_HOME to: %java17_path%
set JAVA_HOME=%java17_path%
set PATH=%JAVA_HOME%\bin;%PATH%

echo.
echo Testing Java version...
java -version

if %errorlevel% equ 0 (
    echo.
    echo ✓ Java 17 set successfully!
    echo.
    echo Testing Gradle build...
    .\gradlew.bat clean
    
    if %errorlevel% equ 0 (
        echo.
        echo ✓ Java version issue resolved!
        echo ✓ Gradle build working correctly!
        echo.
        echo You can now build the Android project with:
        echo   .\gradlew.bat assembleDebug
    ) else (
        echo.
        echo ✗ Gradle build still failing. Check the error messages above.
        echo You may need to install Android SDK/NDK first.
    )
) else (
    echo.
    echo ✗ Java version check failed. Please check your Java 17 installation.
)

echo.
echo ==========================================
echo Environment Variables Set
echo ==========================================
echo JAVA_HOME=%JAVA_HOME%
echo PATH updated to include Java 17
echo.
echo Note: These changes are only for the current session.
echo To make them permanent, add them to your system environment variables.
echo.

pause
