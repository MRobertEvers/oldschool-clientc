#!/bin/bash
# Build script for Android Scene Tile Test
# This script helps set up and build the Android project

echo "Scene Tile Test - Android Build Script"
echo "====================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the android directory."
    exit 1
fi

# Check for required tools
echo "Checking for required tools..."

# Check for Java
if ! command -v java &> /dev/null; then
    echo "Error: Java not found. Please install JDK 8 or higher."
    exit 1
fi

# Check for Android SDK
if [ -z "$ANDROID_HOME" ]; then
    echo "Warning: ANDROID_HOME not set. Please set it to your Android SDK path."
    echo "Example: export ANDROID_HOME=\$HOME/Android/Sdk"
    read -p "Press Enter to continue anyway..."
fi

# Check for Android NDK
if [ -z "$ANDROID_NDK_HOME" ]; then
    echo "Warning: ANDROID_NDK_HOME not set. Please set it to your Android NDK path."
    echo "Example: export ANDROID_NDK_HOME=\$ANDROID_HOME/ndk/25.2.9519653"
    read -p "Press Enter to continue anyway..."
fi

echo ""
echo "Building Android project..."
echo ""

# Make gradlew executable
chmod +x gradlew

# Clean previous build
echo "Cleaning previous build..."
./gradlew clean

# Build debug version
echo "Building debug APK..."
./gradlew assembleDebug

if [ $? -eq 0 ]; then
    echo ""
    echo "Build successful!"
    echo "APK location: app/build/outputs/apk/debug/app-debug.apk"
    echo ""
    echo "To install on device:"
    echo "adb install app/build/outputs/apk/debug/app-debug.apk"
else
    echo ""
    echo "Build failed! Check the error messages above."
    exit 1
fi

echo ""
echo "Build completed."
