#!/bin/bash
# Setup script for Android Scene Tile Test
# This script automates SDL2 installation and cache file copying

echo "Scene Tile Test - Android Setup Script"
echo "======================================"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the android directory."
    exit 1
fi

# Check if we're in the project root
if [ ! -d "../src" ]; then
    echo "Error: Project source directory not found. Please run this script from the android directory."
    exit 1
fi

echo ""
echo "This script will:"
echo "1. Download and setup SDL2 for Android"
echo "2. Copy cache files from the main project"
echo "3. Create necessary directories"
echo ""

read -p "Continue? (y/n): " continue
if [[ ! "$continue" =~ ^[Yy]$ ]]; then
    echo "Setup cancelled."
    exit 0
fi

echo ""
echo "=========================================="
echo "Step 1: Setting up SDL2 for Android"
echo "=========================================="

# Create SDL2 directory structure
echo "Creating SDL2 directory structure..."
mkdir -p "src/main/cpp/SDL2/include"
mkdir -p "src/main/cpp/SDL2/libs/android"

# Check if SDL2 is already downloaded
if [ -f "src/main/cpp/SDL2/include/SDL.h" ]; then
    echo "SDL2 headers already exist. Skipping download."
else
    echo ""
    echo "SDL2 not found. Please download SDL2 manually:"
    echo ""
    echo "1. Go to: https://www.libsdl.org/download-2.0.php"
    echo "2. Download 'SDL2-devel-2.x.x-mingw.tar.gz' or 'SDL2-devel-2.x.x-VC.zip'"
    echo "3. Extract the archive"
    echo "4. Copy the following files:"
    echo "   - SDL2/include/* to android/src/main/cpp/SDL2/include/"
    echo "   - SDL2/libs/android/* to android/src/main/cpp/SDL2/libs/android/"
    echo ""
    echo "After copying the files, run this script again."
    echo ""
    
    read -p "Have you copied the SDL2 files? (y/n): " sdl2_ready
    if [[ ! "$sdl2_ready" =~ ^[Yy]$ ]]; then
        echo "Please copy SDL2 files and run this script again."
        exit 1
    fi
fi

echo ""
echo "Verifying SDL2 installation..."
if [ ! -f "src/main/cpp/SDL2/include/SDL.h" ]; then
    echo "Error: SDL2 headers not found. Please copy SDL2/include/* to src/main/cpp/SDL2/include/"
    exit 1
fi

echo "SDL2 headers found."

# Check for SDL2 libraries
if [ ! -d "src/main/cpp/SDL2/libs/android" ] || [ -z "$(ls -A src/main/cpp/SDL2/libs/android 2>/dev/null)" ]; then
    echo "Warning: SDL2 Android libraries not found."
    echo "Please copy SDL2/libs/android/* to src/main/cpp/SDL2/libs/android/"
    echo "The app may not build without these libraries."
fi

echo ""
echo "=========================================="
echo "Step 2: Setting up cache files"
echo "=========================================="

# Create assets directory
echo "Creating assets directory structure..."
mkdir -p "src/main/assets/cache"

# Check if cache directory exists in main project
if [ ! -d "../cache" ]; then
    echo "Warning: Cache directory not found in main project."
    echo "Please ensure the cache directory exists at: ../cache"
    echo ""
    read -p "Continue without cache files? (y/n): " cache_continue
    if [[ ! "$cache_continue" =~ ^[Yy]$ ]]; then
        echo "Setup cancelled."
        exit 1
    fi
    
    # Create placeholder cache files
    echo "Creating placeholder cache structure..."
    echo "Creating placeholder files for testing..."
    
    # Create a minimal xteas.json
    cat > "src/main/assets/cache/xteas.json" << EOF
{
  "keys": []
}
EOF
    
    echo "Placeholder cache files created."
else
    echo "Found cache directory in main project."
    echo "Copying cache files..."
    
    # Copy cache files
    cp -r "../cache/"* "src/main/assets/cache/"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to copy cache files."
        exit 1
    fi
    
    echo "Cache files copied successfully."
    
    # Verify essential cache files
    echo "Verifying essential cache files..."
    if [ ! -f "src/main/assets/cache/main_file_cache.dat2" ]; then
        echo "Warning: main_file_cache.dat2 not found. This is required for the app to work."
    fi
    
    if [ ! -f "src/main/assets/cache/xteas.json" ]; then
        echo "Warning: xteas.json not found. The app will work without XTEA keys."
    fi
fi

echo ""
echo "=========================================="
echo "Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Open Android Studio"
echo "2. Open the 'android' directory as a project"
echo "3. Sync the project with Gradle files"
echo "4. Build and run on a device or emulator"
echo ""
echo "Or use the command line:"
echo "  ./gradlew assembleDebug"
echo ""
echo "Troubleshooting:"
echo "- If SDL2 is not found, ensure you copied the files correctly"
echo "- If cache files are missing, copy them from the main project"
echo "- Check device logs with: adb logcat | grep SceneTileTest"
echo ""

echo "Setup completed successfully!"
