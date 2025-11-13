#!/bin/bash

# Build script for GameIO.ts
# Compiles TypeScript and copies output to public/build

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../../.." && pwd )"

echo -e "${YELLOW}Building GameIO.ts...${NC}"

# Navigate to browser platform directory
cd "$SCRIPT_DIR"

# Check if node_modules exists, install if not
if [ ! -d "node_modules" ]; then
    echo -e "${YELLOW}node_modules not found. Installing dependencies...${NC}"
    npm install
fi

# Clean previous build
echo -e "${YELLOW}Cleaning previous build...${NC}"
npm run clean 2>/dev/null || rm -rf dist

# Build TypeScript
if [ "$BUILD_MODE" = "debug" ]; then
    echo -e "${YELLOW}Compiling TypeScript with DEBUG info...${NC}"
    npm run build -- --inlineSourceMap --inlineSources
else
    echo -e "${YELLOW}Compiling TypeScript...${NC}"
    npm run build
fi

# Check if build succeeded
if [ ! -f "dist/GameIO.js" ]; then
    echo -e "${RED}Build failed: dist/GameIO.js not found${NC}"
    exit 1
fi

# Create public/build directory if it doesn't exist
PUBLIC_BUILD_DIR="$PROJECT_ROOT/public/build"
mkdir -p "$PUBLIC_BUILD_DIR"

# Copy compiled files to public/build
echo -e "${YELLOW}Copying files to public/build...${NC}"
cp dist/GameIO.js "$PUBLIC_BUILD_DIR/GameIO.js"
cp dist/GameIO.js.map "$PUBLIC_BUILD_DIR/GameIO.js.map" 2>/dev/null || echo "Warning: Source map not found"
cp dist/GameIO.d.ts "$PUBLIC_BUILD_DIR/GameIO.d.ts" 2>/dev/null || echo "Warning: Type declarations not found"

echo -e "${GREEN}✓ Build complete!${NC}"
echo -e "${GREEN}  Output: $PUBLIC_BUILD_DIR/GameIO.js${NC}"

# Show file sizes
echo ""
echo "File sizes:"
ls -lh "$PUBLIC_BUILD_DIR/GameIO.js" | awk '{print "  GameIO.js: " $5}'
if [ -f "$PUBLIC_BUILD_DIR/GameIO.js.map" ]; then
    ls -lh "$PUBLIC_BUILD_DIR/GameIO.js.map" | awk '{print "  GameIO.js.map: " $5}'
fi
if [ -f "$PUBLIC_BUILD_DIR/GameIO.d.ts" ]; then
    ls -lh "$PUBLIC_BUILD_DIR/GameIO.d.ts" | awk '{print "  GameIO.d.ts: " $5}'
fi



