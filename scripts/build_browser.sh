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

# Build the browser targets
echo "Building scene_tile_test_imgui_browser..."
emmake make scene_tile_test_imgui_browser

echo "Building model_viewerfx_browser..."
emmake make model_viewerfx_browser

# Copy the cache to the public/cache directory
echo "Copying cache to public/cache directory..."
mkdir -p ../public/cache
cp -r ../cache/* ../public/cache

# Copy files to public/build directory
echo "Copying files to public/build directory..."
mkdir -p ../public/build

# Copy ImGui browser version
cp scene_tile_test_imgui_browser.js ../public/build/
cp scene_tile_test_imgui_browser.wasm ../public/build/
cp scene_tile_test_imgui_browser.data ../public/build/
cp scene_tile_test_imgui_browser.html ../public/build/

# Copy model viewer FX browser version
cp model_viewerfx_browser.js ../public/build/
cp model_viewerfx_browser.wasm ../public/build/
cp model_viewerfx_browser.data ../public/build/
cp model_viewerfx_browser.html ../public/build/

echo "Build completed successfully!"
echo "Files copied to public/build/ directory"
echo "You can now serve the public/ directory with a web server"
echo ""
echo "To test locally, run:"
echo "  python3 -m http.server -d public 8080"
echo "  Then open http://localhost:8080 in your browser" 