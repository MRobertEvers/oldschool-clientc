#!/bin/bash
# Quick setup script for Android Scene Tile Test
# This script performs all setup steps automatically

echo "Scene Tile Test - Quick Setup Script"
echo "===================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the android directory."
    exit 1
fi

echo ""
echo "This script will automatically:"
echo "1. Download and setup SDL2 for Android"
echo "2. Copy cache files from the main project"
echo "3. Create all necessary directories"
echo "4. Verify the setup"
echo ""

read -p "Continue with automatic setup? (y/n): " continue
if [[ ! "$continue" =~ ^[Yy]$ ]]; then
    echo "Setup cancelled."
    exit 0
fi

echo ""
echo "=========================================="
echo "Starting automatic setup..."
echo "=========================================="

# Step 1: Download SDL2
echo ""
echo "[1/4] Downloading SDL2..."
if [ -f "download_sdl2.sh" ]; then
    chmod +x download_sdl2.sh
    ./download_sdl2.sh
    if [ $? -ne 0 ]; then
        echo "SDL2 download failed. Please check the error messages above."
        exit 1
    fi
else
    echo "download_sdl2.sh not found. Skipping SDL2 download."
fi

# Step 2: Copy cache files
echo ""
echo "[2/4] Setting up cache files..."

# Create assets directory
mkdir -p "src/main/assets/cache"

# Check if cache directory exists in main project
if [ -d "../cache" ]; then
    echo "Copying cache files from main project..."
    cp -r "../cache/"* "src/main/assets/cache/"
    if [ $? -eq 0 ]; then
        echo "Cache files copied successfully."
    else
        echo "Warning: Failed to copy some cache files."
    fi
else
    echo "Warning: Cache directory not found in main project."
    echo "Creating placeholder cache files..."
    
    # Create a minimal xteas.json
    cat > "src/main/assets/cache/xteas.json" << EOF
{
  "keys": []
}
EOF
    
    echo "Placeholder cache files created."
fi

# Step 3: Create additional directories
echo ""
echo "[3/4] Creating additional directories..."

# Create gradle wrapper directory if it doesn't exist
mkdir -p "gradle/wrapper"

# Create build directory
mkdir -p "build"

echo "Additional directories created."

# Step 4: Verification
echo ""
echo "[4/4] Verifying setup..."

VERIFICATION_PASSED=1

# Check SDL2 headers
if [ -f "src/main/cpp/SDL2/include/SDL.h" ]; then
    echo "✓ SDL2 headers found"
else
    echo "✗ SDL2 headers missing"
    VERIFICATION_PASSED=0
fi

# Check SDL2 libraries directory
if [ -d "src/main/cpp/SDL2/libs/android" ]; then
    echo "✓ SDL2 Android libraries directory found"
else
    echo "✗ SDL2 Android libraries directory missing"
    VERIFICATION_PASSED=0
fi

# Check cache directory
if [ -d "src/main/assets/cache" ]; then
    echo "✓ Cache directory found"
else
    echo "✗ Cache directory missing"
    VERIFICATION_PASSED=0
fi

# Check essential files
if [ -f "CMakeLists.txt" ]; then
    echo "✓ CMakeLists.txt found"
else
    echo "✗ CMakeLists.txt missing"
    VERIFICATION_PASSED=0
fi

if [ -f "build.gradle" ]; then
    echo "✓ build.gradle found"
else
    echo "✗ build.gradle missing"
    VERIFICATION_PASSED=0
fi

if [ -f "src/main/AndroidManifest.xml" ]; then
    echo "✓ AndroidManifest.xml found"
else
    echo "✗ AndroidManifest.xml missing"
    VERIFICATION_PASSED=0
fi

echo ""
echo "=========================================="
echo "Setup Summary"
echo "=========================================="

if [ $VERIFICATION_PASSED -eq 1 ]; then
    echo "✓ Setup completed successfully!"
    echo ""
    echo "Next steps:"
    echo "1. Open Android Studio"
    echo "2. Open the 'android' directory as a project"
    echo "3. Sync the project with Gradle files"
    echo "4. Build and run on a device or emulator"
    echo ""
    echo "Or use the command line:"
    echo "  ./gradlew assembleDebug"
else
    echo "✗ Setup completed with warnings."
    echo "Please check the missing components above."
    echo "You may need to manually install SDL2 or copy cache files."
fi

echo ""
echo "Troubleshooting:"
echo "- If SDL2 is not found, run: ./download_sdl2.sh"
echo "- If cache files are missing, copy them from the main project"
echo "- Check device logs with: adb logcat | grep SceneTileTest"
echo ""

echo "Setup completed!"
