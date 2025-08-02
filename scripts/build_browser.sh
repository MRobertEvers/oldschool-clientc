#!/bin/bash

# Build script for browser version using Emscripten
set -e

echo "Building 3D Raster for browser with Emscripten..."

# Check if emcmake is available
if ! command -v emcmake &> /dev/null; then
    echo "Error: emcmake not found. Please install Emscripten SDK."
    echo "Visit: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

# Create build directory if it doesn't exist
mkdir -p build.em

# Navigate to build directory
cd build.em

# Configure with CMake
echo "Configuring with CMake..."
emcmake cmake ..

# Build the browser target
echo "Building scene_tile_test_browser..."
emmake make scene_tile_test_browser

# Copy the cache to the public/cache directory
echo "Copying cache to public/cache directory..."
cp -r ../cache/* ../public/cache

# Copy files to public/build directory
echo "Copying files to public/build directory..."
cp scene_tile_test_browser.js ../public/build/
cp scene_tile_test_browser.wasm ../public/build/
cp scene_tile_test_browser.data ../public/build/

echo "Build completed successfully!"
echo "Files copied to public/build/ directory"
echo "You can now serve the public/ directory with a web server"
echo ""
echo "To test locally, run:"
echo "  python3 -m http.server -d public 8080"
echo "  Then open http://localhost:8080 in your browser" 