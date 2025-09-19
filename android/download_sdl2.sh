#!/bin/bash
# Automatic SDL2 download script for Android Scene Tile Test
# This script downloads and extracts SDL2 for Android

echo "Scene Tile Test - SDL2 Download Script"
echo "======================================"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the android directory."
    exit 1
fi

echo ""
echo "This script will download SDL2 for Android and set it up automatically."
echo ""

read -p "Continue? (y/n): " continue
if [[ ! "$continue" =~ ^[Yy]$ ]]; then
    echo "Download cancelled."
    exit 0
fi

# Create temporary directory
TEMP_DIR=$(mktemp -d)
echo "Using temporary directory: $TEMP_DIR"

echo ""
echo "=========================================="
echo "Downloading SDL2..."
echo "=========================================="

# Try to download SDL2
echo "Downloading SDL2..."
if command -v wget &> /dev/null; then
    wget -O "$TEMP_DIR/SDL2.tar.gz" "https://www.libsdl.org/release/SDL2-2.28.5.tar.gz"
elif command -v curl &> /dev/null; then
    curl -L -o "$TEMP_DIR/SDL2.tar.gz" "https://www.libsdl.org/release/SDL2-2.28.5.tar.gz"
else
    echo "Error: Neither wget nor curl found. Please install one of them or download SDL2 manually."
    rm -rf "$TEMP_DIR"
    exit 1
fi

if [ $? -ne 0 ]; then
    echo "Download failed."
    rm -rf "$TEMP_DIR"
    exit 1
fi

echo "Download successful!"

echo ""
echo "=========================================="
echo "Extracting SDL2..."
echo "=========================================="

# Extract the tar.gz file
echo "Extracting SDL2..."
tar -xzf "$TEMP_DIR/SDL2.tar.gz" -C "$TEMP_DIR"

if [ $? -ne 0 ]; then
    echo "Extraction failed."
    rm -rf "$TEMP_DIR"
    exit 1
fi

echo "Extraction successful!"

echo ""
echo "=========================================="
echo "Setting up SDL2 files..."
echo "=========================================="

# Create SDL2 directory structure
echo "Creating SDL2 directory structure..."
mkdir -p "src/main/cpp/SDL2/include"
mkdir -p "src/main/cpp/SDL2/libs/android"

# Copy SDL2 headers
echo "Copying SDL2 headers..."
cp -r "$TEMP_DIR/SDL2-2.28.5/include/"* "src/main/cpp/SDL2/include/"
if [ $? -ne 0 ]; then
    echo "Failed to copy SDL2 headers."
    rm -rf "$TEMP_DIR"
    exit 1
fi

# Note: SDL2 source doesn't include pre-built Android libraries
# Users will need to build them or download them separately
echo "Note: SDL2 source package doesn't include pre-built Android libraries."
echo "You may need to build SDL2 for Android or download pre-built libraries separately."

echo ""
echo "=========================================="
echo "Verification..."
echo "=========================================="

# Verify installation
if [ -f "src/main/cpp/SDL2/include/SDL.h" ]; then
    echo "✓ SDL2 headers installed successfully."
else
    echo "✗ SDL2 headers not found."
    rm -rf "$TEMP_DIR"
    exit 1
fi

if [ -d "src/main/cpp/SDL2/libs/android" ]; then
    echo "✓ SDL2 Android libraries directory created."
else
    echo "✗ SDL2 Android libraries directory not found."
fi

echo ""
echo "SDL2 setup completed!"
echo "Note: You may need to build SDL2 for Android or download pre-built libraries."
echo "See setup_sdl2.md for more information."

# Clean up temporary files
rm -rf "$TEMP_DIR"

echo ""
echo "Setup completed successfully!"
