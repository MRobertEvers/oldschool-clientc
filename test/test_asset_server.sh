#!/bin/bash

# Test script for asset_server with cache

cd "$(dirname "$0")/.."

# Check if cache directory exists
CACHE_DIR="../cache"
if [ ! -d "$CACHE_DIR" ]; then
    echo "Error: Cache directory not found at $CACHE_DIR"
    exit 1
fi

echo "Starting asset_server with cache..."
./build/asset_server $CACHE_DIR &
SERVER_PID=$!

# Wait for server to start
sleep 2

echo ""
echo "Running test client..."
echo ""

# Test 1: Request from MODELS table (table 7)
echo "Test 1: Requesting Model (Table=7, Archive=0)"
./build/test_asset_client 7 0
echo ""

# Test 2: Request from TEXTURES table (table 9)
echo "Test 2: Requesting Texture (Table=9, Archive=0)"
./build/test_asset_client 9 0
echo ""

# Test 3: Request from SPRITES table (table 8)
echo "Test 3: Requesting Sprite (Table=8, Archive=0)"
./build/test_asset_client 8 0
echo ""

# Kill server
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "Test complete!"
