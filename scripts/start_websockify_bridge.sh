#!/bin/bash

# WebSocket-to-TCP Bridge for Emscripten Cache Connection
# This script starts websockify to bridge WebSocket connections to the asset_server

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "================================================"
echo "Starting WebSocket-to-TCP Bridge for Cache"
echo "================================================"
echo ""

# Check if websockify is installed
if ! command -v websockify &> /dev/null; then
    echo "ERROR: websockify is not installed"
    echo "Install with: pip install websockify"
    echo "or: pip3 install websockify"
    exit 1
fi

# Check if asset_server is running
if ! nc -z localhost 4949 2>/dev/null; then
    echo "WARNING: asset_server (port 4949) is not running"
    echo "Start it with: ./build/asset_server"
    echo ""
    echo "Continuing anyway (websockify will wait for connections)..."
    echo ""
fi

# Check if xteas.json exists
if [ ! -f "$PROJECT_ROOT/cache/xteas.json" ]; then
    echo "ERROR: $PROJECT_ROOT/cache/xteas.json not found"
    echo "The cache directory should contain xteas.json"
    exit 1
fi

echo "Configuration:"
echo "  - WebSocket server: ws://localhost:8080"
echo "  - TCP target: tcp://localhost:4949"
echo "  - Web root: $PROJECT_ROOT/cache (serves xteas.json via HTTP)"
echo ""
echo "Emscripten will:"
echo "  1. Fetch xteas.json from http://localhost:8080/xteas.json"
echo "  2. Connect via WebSocket to ws://localhost:8080"
echo "  3. websockify forwards to tcp://localhost:4949 (asset_server)"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Start websockify - WebSocket-to-TCP bridge only (no web serving)
# The xteas.json will be served from the Emscripten dev server instead
websockify 8080 localhost:4949

