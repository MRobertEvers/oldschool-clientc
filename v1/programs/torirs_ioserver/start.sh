#!/bin/bash

# ToriRS IO Server - Start Script with Default Values
# Auto-detects cache254/, cache/, and cache.kronos/ from the repo root.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PORT="${PORT:-8080}"
HOST="${HOST:-0.0.0.0}"
STATIC_DIR="${STATIC_DIR:-$SCRIPT_DIR/../browser/dist}"
LUA_DIR="${LUA_DIR:-$SCRIPT_DIR/../../revs/scripts}"

echo "Starting ToriRS IO Server..."
echo "  Host: $HOST:$PORT"
echo "  Lua Scripts: $LUA_DIR"
if [ -n "$STATIC_DIR" ]; then
    echo "  Static Files: $STATIC_DIR"
fi
if [ -n "$CACHE_DIR" ]; then
    echo "  Manual cache override: $CACHE_DIR (${CACHE_MODE:-dat1})"
else
    echo "  Caches: auto-detect from repo root (cache254/, cache/, cache.kronos/)"
fi
echo ""

node "$SCRIPT_DIR/server.js" \
    ${CACHE_DIR:+--cache="$CACHE_DIR"} \
    ${CACHE_MODE:+--mode="$CACHE_MODE"} \
    --port="$PORT" \
    --host="$HOST" \
    --lua="$LUA_DIR" \
    ${STATIC_DIR:+--static="$STATIC_DIR"}
