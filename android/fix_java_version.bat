@echo off
REM Fix Java version compatibility for Android Scene Tile Test

echo Scene Tile Test - Java Version Fix Script
echo =========================================

echo.
echo Current Java version:
java -version

echo.
echo The error "Unsupported class file major version 68" indicates
echo that you're using Java 24, but Gradle 8.5 expects Java 17.
echo.

echo Solutions:
echo.
echo Option 1: Use Java 17 (Recommended)
echo 1. Download Java 17 from: https://adoptium.net/temurin/releases/?version=17
echo 2. Install Java 17
echo 3. Set JAVA_HOME to point to Java 17
echo.
echo Option 2: Update Gradle to support Java 24
echo 1. Update gradle-wrapper.properties to use Gradle 8.10+
echo 2. Update Android Gradle Plugin to 8.7.0+
echo.

set /p choice="Choose option (1 or 2): "

if "%choice%"=="1" (
    echo.
    echo Setting up Java 17 solution...
    goto :java17_setup
) else if "%choice%"=="2" (
    echo.
    echo Updating Gradle for Java 24 support...
    goto :gradle_update
) else (
    echo Invalid choice. Exiting.
    pause
    exit /b 1
)

:java17_setup
echo.
echo ==========================================
echo Java 17 Setup Instructions
echo ==========================================
echo.
echo 1. Download Java 17 from: https://adoptium.net/temurin/releases/?version=17
echo 2. Install Java 17
echo 3. Set JAVA_HOME environment variable:
echo    set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-17.0.9.9-hotspot
echo 4. Add Java 17 to PATH:
echo    set PATH=%%JAVA_HOME%%\bin;%%PATH%%
echo.
echo After installing Java 17, run this script again.
echo.

set /p java17_path="Enter Java 17 installation path (or press Enter to skip): "
if not "%java17_path%"=="" (
    set JAVA_HOME=%java17_path%
    set PATH=%JAVA_HOME%\bin;%PATH%
    echo Java 17 set to: %JAVA_HOME%
    echo Testing Java version...
    java -version
    goto :test_gradle
) else (
    echo Please install Java 17 and run this script again.
    pause
    exit /b 1
)

:gradle_update
echo.
echo ==========================================
echo Updating Gradle for Java 24 Support
echo ==========================================

echo Updating gradle-wrapper.properties...
echo distributionUrl=https\://services.gradle.org/distributions/gradle-8.10.2-bin.zip > gradle\wrapper\gradle-wrapper.properties
echo distributionBase=GRADLE_USER_HOME >> gradle\wrapper\gradle-wrapper.properties
echo distributionPath=wrapper/dists >> gradle\wrapper\gradle-wrapper.properties
echo zipStoreBase=GRADLE_USER_HOME >> gradle\wrapper\gradle-wrapper.properties
echo zipStorePath=wrapper/dists >> gradle\wrapper\gradle-wrapper.properties

echo Updating build.gradle...
powershell -Command "(Get-Content build.gradle) -replace 'com.android.application'' version ''8.0.2''', 'com.android.application'' version ''8.7.0''' | Set-Content build.gradle"

echo Gradle updated for Java 24 support!
goto :test_gradle

:test_gradle
echo.
echo ==========================================
echo Testing Gradle Build
echo ==========================================

echo Testing Gradle version...
.\gradlew.bat --version

if %errorlevel% equ 0 (
    echo.
    echo Testing clean build...
    .\gradlew.bat clean
    
    if %errorlevel% equ 0 (
        echo.
        echo ✓ Java version issue resolved!
        echo ✓ Gradle build working correctly!
        echo.
        echo You can now build the Android project.
    ) else (
        echo.
        echo ✗ Clean build failed. Check the error messages above.
    )
) else (
    echo.
    echo ✗ Gradle version check failed. Check the error messages above.
)

echo.
echo Java version fix completed!
echo.

pause
