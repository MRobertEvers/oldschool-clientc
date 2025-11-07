# Emscripten WebSocket Cache Setup

This document describes how to set up and run the Emscripten build with WebSocket-based cache connectivity.

## Overview

The Emscripten build (`emscripten_sdl2_webgl1`) uses `cache_inet` to load game assets on-demand from a remote server. Since browsers cannot make direct TCP connections, we use WebSocket connections with a bridge to the TCP asset server.

## Architecture

```
┌─────────────────┐     WebSocket      ┌──────────────┐      TCP       ┌──────────────┐
│   Browser       │ ───────────────►   │  websockify  │ ────────────►  │ asset_server │
│ (Emscripten App)│  ws://localhost:   │   (bridge)   │  tcp://localhost│  (port 4949) │
│                 │      8080          │              │     :4949      │              │
└─────────────────┘                    └──────────────┘                └──────────────┘
                                             │
                                             │ HTTP
                                             ▼
                                       xteas.json
```

## Prerequisites

1. **Python with websockify**:
   ```bash
   pip install websockify
   # or
   pip3 install websockify
   ```

2. **Emscripten SDK** (emsdk) must be installed and activated

3. **Cache files** in `../cache/` directory with `xteas.json`

## Step-by-Step Setup

### 1. Build the Asset Server

```bash
# From project root
mkdir -p build
cd build
cmake ..
make asset_server
```

### 2. Start the Asset Server

The asset server is a TCP server that serves cache files on port 4949:

```bash
# From project root
./build/asset_server
```

You should see:
```
Asset Server starting...
Loaded X XTEA keys successfully
Server listening on port 4949
```

### 3. Start the WebSocket Bridge

In a **new terminal**, start websockify to bridge WebSocket to TCP:

```bash
# From project root
./scripts/start_websockify_bridge.sh
```

This script:
- Starts websockify on port 8080
- Bridges ws://localhost:8080 to tcp://localhost:4949 (WebSocket-to-TCP proxy only)

### 4. Build the Emscripten Application

```bash
# From project root
mkdir -p build.em
cd build.em

# Activate emsdk first
source /path/to/emsdk/emsdk_env.sh

# Configure and build
emcmake cmake ..
emmake make emscripten_sdl2_webgl1
```

### 5. Serve and Run the Application

```bash
# From project root, use the custom server script
./scripts/serve_emscripten.py
```

This script:
- Serves from `build.em/` directory automatically
- Also serves `xteas.json` from `../cache/` directory
- Runs on port 8000 by default (or specify: `./scripts/serve_emscripten.py 8080`)

Then open your browser to:
```
http://localhost:8000/emscripten_sdl2_webgl1.html
```

**Alternative**: You can also use Python's standard http.server:
```bash
cd build.em
python3 -m http.server 8000
```
But you'll need to manually copy `xteas.json` to the `build.em/` directory.

## How It Works

### XTEA Loading

1. On startup, the Emscripten app fetches `xteas.json` via HTTP from `/xteas.json` (same origin, port 8000)
2. The development server (`serve_emscripten.py`) serves it from `../cache/xteas.json`
3. The file is loaded into Emscripten's virtual filesystem at `../cache/xteas.json`
4. `game_new()` reads XTEA keys from the virtual filesystem

### Cache Connection

1. `cache_new_inet()` creates a POSIX socket connection to `127.0.0.1:4949`
2. With `-s PROXY_TO_PTHREAD=1` and `-s USE_PTHREADS=1`, Emscripten automatically proxies POSIX socket calls to WebSocket
3. Emscripten connects to `ws://localhost:8080/` (configured via `-s WEBSOCKET_URL`)
4. websockify receives the WebSocket connection and forwards it to `tcp://localhost:4949`
5. The asset_server handles the TCP connection and serves cache archives on-demand

### On-Demand Loading (Lazy Loading)

The `cache_inet` system does NOT load the entire cache at startup. The Emscripten build uses **lazy loading**:

**At Startup**:
- Connects to the server via WebSocket
- Validates/creates empty idx files
- Does NOT load reference tables (deferred until needed)

**On-Demand**:
1. When an archive is first requested from a table, the reference table for that table is loaded
2. The reference table contains metadata about all archives in that table
3. Individual archives are then requested from the server as needed
4. Each request is a 6-byte message: `[request_type, table_id, archive_id (4 bytes)]`
5. The server responds with: `[status (4 bytes), size (4 bytes), data (size bytes)]`

**Example**: If the game loads map 50,50:
- First access → Loads reference table for CACHE_MAPS (table 5)
- Then requests the specific map archive from the server
- Subsequent map loads reuse the cached reference table

## Compile Flags

Key Emscripten flags for WebSocket support:

```cmake
-s ASYNCIFY=1               # Enable async/blocking call transformation
-s ASYNCIFY_IMPORTS=[...]   # Specify syscalls that can block (socket ops)
-s WEBSOCKET_SUBPROTOCOL='binary'  # Use binary protocol
-s WEBSOCKET_URL='ws://localhost:8080/'  # WebSocket bridge URL
```

**Why ASYNCIFY instead of pthreads?**
- WebGL requires running on the main browser thread
- `PROXY_TO_PTHREAD` moves execution to a worker, breaking WebGL
- ASYNCIFY allows blocking operations by unwinding/rewinding the call stack
- This has ~1-2% performance overhead and slight code size increase, but preserves WebGL compatibility

**ASYNCIFY_IMPORTS**: Specifies which low-level functions can trigger async behavior:
- `emscripten_sleep` - Sleep operations
- `__syscall_socket` - Socket creation
- `__syscall_connect` - Socket connection
- `__syscall_sendto` - Sending data
- `__syscall_recvfrom` - Receiving data

These cover all the blocking operations used by `cache_inet`'s POSIX socket calls.

## Troubleshooting

### WebSocket Connection Failed

**Symptoms**: Console shows "WebSocket connection failed" or timeout

**Solutions**:
1. Ensure websockify is running: `./scripts/start_websockify_bridge.sh`
2. Check if asset_server is running: `netstat -an | grep 4949`
3. Check websockify is listening: `netstat -an | grep 8080`

### xteas.json Not Found

**Symptoms**: "Failed to load xtea keys" or "Failed to fetch/setup xteas.json"

**Solutions**:
1. Ensure `cache/xteas.json` exists in the project root
2. Check if the development server can find it: Look for the message when starting `./scripts/serve_emscripten.py`
3. Test in browser: visit `http://localhost:8000/xteas.json` (should return the JSON file)
4. If file exists but not found, check the path in `serve_emscripten.py` script

### Asset Server Connection Refused

**Symptoms**: websockify shows "Connection refused" when forwarding

**Solutions**:
1. Start the asset_server first: `./build/asset_server`
2. Check if port 4949 is in use: `lsof -i :4949`

### WebGL Context Error / getParameter undefined

**Symptoms**: 
- "Cannot read properties of undefined (reading 'getParameter')"
- "worker sent an error" messages
- WebGL context creation fails

**Cause**: 
This happens if `PROXY_TO_PTHREAD` is enabled. WebGL contexts can only be created on the main browser thread, not in Web Workers.

**Solution**:
The build is configured to use `ASYNCIFY` instead of pthreads, which allows blocking socket operations while keeping WebGL on the main thread. Make sure you've rebuilt with the latest CMakeLists.txt configuration.

## Production Considerations

For production deployment:
1. Replace websockify with a proper WebSocket-to-TCP gateway (e.g., nginx with stream module)
2. Use HTTPS/WSS for secure connections
3. Implement authentication for the asset server
4. Consider CDN caching for frequently accessed assets
5. Implement retry logic and error handling

## See Also

- `test/emscripten_sdl2_webgl1.cpp` - Main Emscripten test application
- `test/asset_server.c` - TCP cache server implementation
- `CMakeLists.txt` - Build configuration with Emscripten flags
- `src/osrs/cache_inet.c` - POSIX socket-based cache client

