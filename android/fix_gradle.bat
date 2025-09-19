@echo off
REM Fix Gradle wrapper issues for Android Scene Tile Test

echo Scene Tile Test - Gradle Fix Script
echo ===================================

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo Error: CMakeLists.txt not found. Please run this script from the android directory.
    pause
    exit /b 1
)

echo.
echo This script will fix the Gradle wrapper issues.
echo.

REM Create gradle wrapper directory if it doesn't exist
if not exist "gradle\wrapper" mkdir "gradle\wrapper"

echo Downloading Gradle wrapper JAR...

REM Try to download gradle-wrapper.jar using PowerShell
powershell -Command "& {try {Invoke-WebRequest -Uri 'https://github.com/gradle/gradle/raw/v8.0.0/gradle/wrapper/gradle-wrapper.jar' -OutFile 'gradle\wrapper\gradle-wrapper.jar' -UseBasicParsing; Write-Host 'Download successful'} catch {Write-Host 'Download failed: ' $_.Exception.Message; exit 1}}"

if %errorlevel% neq 0 (
    echo PowerShell download failed. Trying alternative method...
    
    REM Try using curl if available
    curl --version >nul 2>&1
    if %errorlevel% equ 0 (
        echo Using curl to download Gradle wrapper...
        curl -L -o "gradle\wrapper\gradle-wrapper.jar" "https://github.com/gradle/gradle/raw/v8.0.0/gradle/wrapper/gradle-wrapper.jar"
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
if exist "gradle\wrapper\gradle-wrapper.jar" (
    echo Gradle wrapper JAR downloaded successfully!
    goto :verify_setup
) else (
    echo Download failed.
    goto :manual_download
)

:manual_download
echo.
echo ==========================================
echo Manual Download Required
echo ==========================================
echo.
echo Automatic download failed. Please download the Gradle wrapper manually:
echo.
echo 1. Go to: https://github.com/gradle/gradle/raw/v8.0.0/gradle/wrapper/gradle-wrapper.jar
echo 2. Download the file
echo 3. Save it as: android\gradle\wrapper\gradle-wrapper.jar
echo.
echo After downloading, run this script again.
echo.

:verify_setup
echo.
echo ==========================================
echo Verifying Setup
echo ==========================================

if exist "gradle\wrapper\gradle-wrapper.jar" (
    echo ✓ Gradle wrapper JAR found
) else (
    echo ✗ Gradle wrapper JAR missing
    echo Please download it manually from the link above.
)

if exist "gradle\wrapper\gradle-wrapper.properties" (
    echo ✓ Gradle wrapper properties found
) else (
    echo ✗ Gradle wrapper properties missing
)

if exist "gradlew.bat" (
    echo ✓ Gradle wrapper script found
) else (
    echo ✗ Gradle wrapper script missing
)

echo.
echo Gradle fix completed!
echo You can now try building the project again.
echo.

pause
