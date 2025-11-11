# Quick Start Guide

Get up and running with GameIO in 5 minutes.

## Prerequisites

Before you begin, ensure you have:

1. **Asset Server Built**
   ```bash
   cd /path/to/3d-raster
   mkdir -p build
   cd build
   cmake ..
   make asset_server
   ```

2. **Python with websockify**
   ```bash
   pip install websockify
   ```

3. **Cache Files** in `../cache/` directory with `xteas.json`

## Step 1: Start the Asset Server

From the project root:

```bash
./build/asset_server
```

You should see:
```
Asset Server starting...
Loaded X XTEA keys successfully
Server listening on port 4949
```

## Step 2: Start the WebSocket Bridge

In a **new terminal**:

```bash
./scripts/start_websockify_bridge.sh
```

You should see:
```
WebSocket server settings:
  - listening on port 8080
  - proxying to tcp://localhost:4949
```

## Step 3: Build TypeScript (Optional)

If you want to compile TypeScript to JavaScript:

```bash
cd src/platforms/browser
npm install
npm run build
```

This creates `dist/GameIO.js` with type definitions.

## Step 4: Serve the Application

From the project root:

```bash
./scripts/serve_emscripten.py
```

Or use any HTTP server:

```bash
python3 -m http.server 8000
```

## Step 5: Test in Browser

Open your browser to one of these pages:

### Interactive Example
```
http://localhost:8000/src/platforms/browser/example.html
```

Features:
- Connect to asset server
- Request archives by table/ID
- View cache statistics
- Clear cache

### Automated Test Suite
```
http://localhost:8000/src/platforms/browser/test-gameio.html
```

Features:
- Automated test cases
- Validates functionality
- Shows console output
- Performance metrics

## Usage in Your Code

### Basic Example

```typescript
import { GameIO } from './GameIO.js';

// Initialize
const gameIO = new GameIO("ws://localhost:8080/");
await gameIO.initialize();

// Request an archive
const data = await gameIO.requestArchive(5, 0);
if (data) {
    console.log(`Received ${data.byteLength} bytes`);
}
```

### In HTML

```html
<!DOCTYPE html>
<html>
<head>
    <title>My Game</title>
</head>
<body>
    <script type="module">
        import { GameIO } from './src/GameIO.js';
        
        async function loadAssets() {
            const gameIO = new GameIO();
            await gameIO.initialize();
            
            // Load map data
            const mapData = await gameIO.requestArchive(5, 0);
            console.log('Map loaded:', mapData);
        }
        
        loadAssets();
    </script>
</body>
</html>
```

## Troubleshooting

### "WebSocket connection failed"

**Check:**
1. Is websockify running? (`ps aux | grep websockify`)
2. Is it on the correct port? (should be 8080)
3. Check browser console for exact error

**Fix:**
```bash
./scripts/start_websockify_bridge.sh
```

### "Failed to open IndexedDB"

**Check:**
1. Are you using a modern browser? (Chrome 24+, Firefox 16+, Safari 10+)
2. Is private/incognito mode enabled? (may block IndexedDB)
3. Check browser console for storage quota errors

**Fix:**
- Try regular (non-incognito) mode
- Check available disk space
- Clear browser storage

### "Asset Server connection refused"

**Check:**
1. Is asset_server running? (`ps aux | grep asset_server`)
2. Is it listening on port 4949? (`lsof -i :4949`)

**Fix:**
```bash
cd build
./asset_server
```

### "Archive not found" (null response)

**Reasons:**
1. Archive doesn't exist in cache
2. Invalid table/archive ID
3. Missing XTEA key (for encrypted archives)

**Fix:**
- Verify the table/archive ID is correct
- Check `../cache/xteas.json` exists
- Check asset_server console for errors

### Browser shows "CORS error"

**Fix:**
Serve from the same origin as the WebSocket, or configure CORS headers.

## Common Cache Table IDs

| Table ID | Content Type |
|----------|-------------|
| 0 | Underlay |
| 1 | Identkit |
| 2 | Configs |
| 3 | Interfaces |
| 4 | Sound Effects |
| 5 | **Maps** |
| 6 | Music |
| 7 | **Models** |
| 8 | Sprites |
| 9 | Textures |
| 10 | Binary |
| 12 | Scripts |
| 13 | Font Metrics |
| 14 | Sound Effects (alt) |
| 15 | Vorbis Music |
| 255 | **Reference Tables** |

## Example: Loading a Map

```typescript
// Initialize GameIO
const gameIO = new GameIO("ws://localhost:8080/");
await gameIO.initialize();

// Map coordinates (e.g., Lumbridge = 50, 50)
const chunkX = 50;
const chunkY = 50;

// Calculate map archive ID (simplified - check your game's formula)
const mapId = (chunkX << 8) | chunkY;

// Request map terrain
const terrainData = await gameIO.requestArchive(5, mapId);
if (terrainData) {
    console.log(`Terrain loaded: ${terrainData.byteLength} bytes`);
    // Parse terrain data...
}

// Request map locations (objects)
const locData = await gameIO.requestArchive(5, mapId + 1);
if (locData) {
    console.log(`Locations loaded: ${locData.byteLength} bytes`);
    // Parse location data...
}
```

## Performance Tips

1. **Batch Requests**: Request multiple archives concurrently
   ```typescript
   const promises = [
       gameIO.requestArchive(5, 0),
       gameIO.requestArchive(5, 1),
       gameIO.requestArchive(5, 2)
   ];
   const results = await Promise.all(promises);
   ```

2. **Preload Reference Tables**: Load reference tables early
   ```typescript
   await gameIO.requestReferenceTable(5); // Maps
   await gameIO.requestReferenceTable(7); // Models
   ```

3. **Monitor Cache**: Check cache size periodically
   ```typescript
   const stats = await gameIO.getCacheStats();
   console.log(`Cache size: ${stats.count} archives`);
   ```

4. **Clear Selectively**: Don't clear entire cache unnecessarily
   ```typescript
   // Clear only when needed (e.g., update available)
   await gameIO.clearCache();
   ```

## Next Steps

- Read the full [README.md](README.md) for detailed API documentation
- Check out [example.html](example.html) for interactive examples
- Run [test-gameio.html](test-gameio.html) to verify your setup
- See the main [EMSCRIPTEN_WEBSOCKET_SETUP.md](../../../EMSCRIPTEN_WEBSOCKET_SETUP.md) for architecture details

## Getting Help

If you encounter issues:

1. Check browser console for errors
2. Check asset_server console output
3. Verify all services are running (asset_server, websockify, http server)
4. Try the test suite to isolate the problem
5. Review the troubleshooting section above

Happy coding! 🚀

