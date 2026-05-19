#!/bin/bash

# ToriRS IO Server - Start Script with Default Values
# This script runs the server with reasonable default configuration

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default configuration values
CACHE_DIR="${CACHE_DIR:-/Users/matthewevers/Documents/git_repos/3draster/cache254}"
CACHE_MODE="${CACHE_MODE:-dat1}"
PORT="${PORT:-8080}"
HOST="${HOST:-0.0.0.0}"
STATIC_DIR="${STATIC_DIR:-$SCRIPT_DIR/../browser/dist}"
LUA_DIR="${LUA_DIR:-$SCRIPT_DIR/../../revs/scripts}"

# Validate cache directory
if [ ! -d "$CACHE_DIR" ]; then
    echo "Error: Cache directory '$CACHE_DIR' does not exist."
    echo ""
    echo "Please set CACHE_DIR environment variable to your OSRS cache location."
    echo "Common locations:"
    echo "  - macOS: ~/.jagex_cache_32 or ~/.jagex_cache_33"
    echo "  - Windows: %USERPROFILE%/.jagex_cache_32"
    echo "  - Linux: ~/.jagex_cache_32"
    echo ""
    echo "Usage: CACHE_DIR=/path/to/cache $0"
    exit 1
fi

echo "Starting ToriRS IO Server..."
echo "  Cache: $CACHE_DIR ($CACHE_MODE)"
echo "  Host: $HOST:$PORT"
echo "  Lua Scripts: $LUA_DIR"
if [ -n "$STATIC_DIR" ]; then
    echo "  Static Files: $STATIC_DIR"
fi
echo ""

# Run the server
node "$SCRIPT_DIR/server.js" \
    --cache="$CACHE_DIR" \
    --mode="$CACHE_MODE" \
    --port="$PORT" \
    --host="$HOST" \
    --lua="$LUA_DIR" \
    ${STATIC_DIR:+--static="$STATIC_DIR"}
