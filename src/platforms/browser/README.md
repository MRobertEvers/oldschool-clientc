# GameIO - WebSocket Archive Loader with IndexedDB Caching

A TypeScript implementation for requesting game archives from the asset server via WebSocket and caching them in IndexedDB.

## Quick Start

### Building

```bash
# From this directory
./build.sh

# Or from project root
./scripts/build-gameio.sh

# Or using npm
npm run build:deploy
```

This compiles the TypeScript and copies the output to `public/build/` for serving.

See [BUILD.md](BUILD.md) for detailed build instructions.

## Features

- **WebSocket Communication**: Connects to the asset server using the binary protocol
- **IndexedDB Caching**: Automatically caches downloaded archives for offline use
- **Request Queueing**: Handles multiple concurrent requests efficiently
- **Protocol Compliance**: Implements the exact protocol expected by `asset_server.c`

## Protocol

The implementation follows the binary protocol used by `asset_server.c`:

### Request Format (6 bytes)

```
[request_code(1)] [table_id(1)] [archive_id(4)]
```

- `request_code`: Always `1` for archive requests
- `table_id`: Cache table ID (0-254 for normal tables, 255 for reference tables)
- `archive_id`: Archive ID within the table (big-endian uint32)

### Response Format

```
[status(4)] [size(4)] [data(size bytes)]
```

- `status`: `1` for success, `0` for error (big-endian uint32)
- `size`: Size of archive data in bytes (big-endian uint32)
- `data`: Raw archive data (if size > 0)

## Usage

### Basic Example

```typescript
import { GameIO } from "./src/GameIO.js";

// Create and initialize
const gameIO = new GameIO("ws://localhost:8080/");
await gameIO.initialize();

// Request an archive (checks cache first, then requests from server)
const data = await gameIO.requestArchive(5, 12345);
if (data) {
  console.log(`Received ${data.byteLength} bytes`);
  // Process the archive data...
}

// Request a reference table
const refTable = await gameIO.requestReferenceTable(5);

// Get cache statistics
const stats = await gameIO.getCacheStats();
console.log(`Cached ${stats.count} archives`);

// Clear the cache
await gameIO.clearCache();

// Close connections when done
await gameIO.close();
```

### Setting Up the Server

Before using GameIO, you need to have the asset server and WebSocket bridge running:

#### 1. Start the Asset Server

```bash
# From project root
./build/asset_server
```

The asset server listens on TCP port 4949.

#### 2. Start the WebSocket Bridge

```bash
# From project root
./scripts/start_websockify_bridge.sh
```

This bridges WebSocket connections (port 8080) to the asset server (port 4949).

#### 3. Serve the Application

```bash
# From project root
./scripts/serve_emscripten.py
```

Then open `http://localhost:8000/src/platforms/browser/example.html` to test.

## API Reference

### Constructor

```typescript
constructor(wsUrl: string = "ws://localhost:8080/")
```

Creates a new GameIO instance.

**Parameters:**

- `wsUrl`: WebSocket URL to connect to (default: "ws://localhost:8080/")

### Methods

#### `initialize(): Promise<void>`

Initializes IndexedDB and establishes WebSocket connection.

**Throws:** Error if initialization fails

---

#### `requestArchive(tableId: number, archiveId: number): Promise<ArrayBuffer | null>`

Requests an archive. Checks IndexedDB cache first, then requests from server if not found.

**Parameters:**

- `tableId`: Cache table ID (0-254)
- `archiveId`: Archive ID within the table

**Returns:** ArrayBuffer containing the archive data, or null if not found

**Throws:** Error if WebSocket is not connected

---

#### `requestReferenceTable(tableId: number): Promise<ArrayBuffer | null>`

Requests a reference table (table 255).

**Parameters:**

- `tableId`: The table ID to get the reference table for

**Returns:** ArrayBuffer containing the reference table data, or null if not found

---

#### `getCacheStats(): Promise<{ count: number; tables: Map<number, number> }>`

Gets statistics about cached archives.

**Returns:** Object containing:

- `count`: Total number of cached archives
- `tables`: Map of table IDs to archive counts

---

#### `clearCache(): Promise<void>`

Clears all cached archives from IndexedDB.

---

#### `close(): Promise<void>`

Closes WebSocket connection and IndexedDB database.

## Implementation Details

### Request Queue

The implementation uses a request queue to handle multiple archive requests:

- Requests are processed sequentially (one at a time)
- Responses are matched to their corresponding requests
- Failed requests are rejected with appropriate errors

### State Machine

The response parser uses a state machine with three states:

1. **ReadingStatus**: Reading 4-byte status code
2. **ReadingSize**: Reading 4-byte data size
3. **ReadingData**: Reading archive data

This ensures correct parsing even if responses arrive in multiple chunks.

### Caching Strategy

- **Cache-first**: Always checks IndexedDB before making network requests
- **Auto-save**: Downloaded archives are automatically saved to IndexedDB
- **Timestamped**: Each cache entry includes a timestamp for future expiration logic

### IndexedDB Schema

**Database:** `GameArchiveCache`

**Object Store:** `archives`

- **Key Path:** `["tableId", "archiveId"]` (compound key)
- **Indexes:** `tableId` (for querying archives by table)

**Record Format:**

```typescript
{
    tableId: number,
    archiveId: number,
    data: ArrayBuffer,
    timestamp: number
}
```

## Error Handling

The implementation handles various error scenarios:

- **Connection Errors**: Rejects pending requests if WebSocket closes
- **Protocol Errors**: Validates request codes and status responses
- **Database Errors**: Falls back to network requests if IndexedDB fails
- **Invalid Data**: Returns null for archives that don't exist

## Testing

Use the included `example.html` to test the implementation:

1. Open the example page in your browser
2. Click "Initialize" to connect
3. Enter table and archive IDs
4. Click "Request Archive" to fetch data
5. Use "Get Cache Stats" to see what's cached
6. Use "Clear Cache" to reset the cache

## Cache Table IDs

Common cache table IDs:

- `5`: Maps
- `7`: Models
- `10`: Interfaces
- `255`: Reference tables (metadata)

See the main cache documentation for a complete list of table IDs.

## Performance Considerations

- **Bandwidth**: Only downloads archives once; subsequent requests use IndexedDB
- **Memory**: Archives are stored as ArrayBuffers (binary data)
- **Concurrency**: One request at a time to maintain protocol integrity
- **Storage**: IndexedDB can store hundreds of MB of cache data

## Future Enhancements

Possible improvements:

- Parallel requests (if server supports it)
- Cache expiration policies
- Progress callbacks for large downloads
- Automatic reconnection on disconnect
- Compression support
- Cache size limits and LRU eviction

## License

Same as the main project.
