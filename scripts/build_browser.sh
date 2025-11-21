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

echo "Building emscripten_sdl2_webgl1..."
emmake make emscripten_sdl2_webgl1

# Copy the cache to the public/cache directory
echo "Copying cache to public/cache directory..."
mkdir -p ../public/cache
cp -r ../cache/* ../public/cache

# Copy files to public/build directory
echo "Copying files to public/build directory..."
mkdir -p ../public/build


# Copy all .js, .wasm, .data, .html files to public/build directory
cp *.js ../public/build/
cp *.wasm ../public/build/
cp *.data ../public/build/
cp *.html ../public/build/
# Copy debug files for DWARF debugging (if they exist)
cp *.dwp ../public/build/ 2>/dev/null || echo "No .dwp files found"
cp *.map ../public/build/ 2>/dev/null || echo "No .map files found"

echo "Build completed successfully!"
echo "Files copied to public/build/ directory"
echo "You can now serve the public/ directory with a web server"
echo ""
echo "To test locally, run:"
echo "  python3 -m http.server -d public 8080"
echo "  Then open http://localhost:8080 in your browser" 