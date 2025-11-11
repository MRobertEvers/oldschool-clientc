# GameIO Emscripten Integration

This document explains how to use GameIO from Emscripten C/C++ code.

## Overview

The GameIO system allows C++ code running in Emscripten/WASM to request game archives from the asset server via WebSocket, with automatic IndexedDB caching. The system bridges JavaScript and C++ using Emscripten's EM_ASM macros.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    C++ / Emscripten                         │
│                                                             │
│  ┌────────────────────────────────────────────────────┐   │
│  │  Your C++ Code                                     │   │
│  │  #include "gameio_bridge.h"                       │   │
│  │                                                     │   │
│  │  int reqId = gameio_request_archive(5, 0);       │   │
│  └──────────────────────┬──────────────────────────────┘   │
│                         │                                   │
│                         ↓ EM_ASM                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  gameio_bridge.h                                     │  │
│  │  (Inline JavaScript via EM_ASM)                     │  │
│  └──────────────────────┬───────────────────────────────┘  │
└─────────────────────────┼───────────────────────────────────┘
                          │
                          ↓ window.GameIOAPI
┌─────────────────────────┼───────────────────────────────────┐
│                    JavaScript / Browser                     │
│  ┌──────────────────────┴───────────────────────────────┐  │
│  │  shell.html - GameIO API                            │  │
│  │  window.GameIOAPI.requestArchive(tableId, archiveId)│  │
│  └──────────────────────┬───────────────────────────────┘  │
│                         │                                   │
│                         ↓                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  GameIO.ts                                           │  │
│  │  - WebSocket communication                          │  │
│  │  - IndexedDB caching                                │  │
│  │  - Request queueing                                 │  │
│  └──────────────────────┬───────────────────────────────┘  │
└─────────────────────────┼───────────────────────────────────┘
                          │
                          ↓ WebSocket
                  ┌───────────────┐
                  │ Asset Server  │
                  │ (port 4949)   │
                  └───────────────┘
```

## Files

- **`GameIO.ts`**: TypeScript implementation of WebSocket + IndexedDB loader
- **`shell.html`**: HTML template with GameIO integration and JavaScript API
- **`gameio_bridge.h`**: C++ header with inline functions to call GameIO from C++
- **`gameio_example.cpp`**: Complete examples showing how to use the API

## Quick Start

### 1. Include the Header

```cpp
#include "src/platforms/browser/gameio_bridge.h"
```

### 2. Request an Archive

```cpp
// Request map archive (table 5, archive 0)
int requestId = gameio_request_archive(5, 0);

if (requestId < 0) {
    // Error
    printf("Failed to request archive\n");
} else if (requestId == 0) {
    // Cached! Data is immediately available
    int size = gameio_get_archive_data_size(5, 0);
    char* buffer = (char*)malloc(size);
    gameio_copy_archive_data_to_heap(5, 0, buffer, size);
    // Use buffer...
    free(buffer);
    gameio_clear_archive_data(5, 0);
} else {
    // Will download, requestId > 0
    // Poll status in game loop
}
```

### 3. Check Status (in game loop)

```cpp
const char* status = gameio_get_request_status(requestId);

if (strcmp(status, "completed") == 0) {
    // Request completed, get the data
    int size = gameio_get_archive_data_size(tableId, archiveId);
    char* buffer = (char*)malloc(size);
    gameio_copy_archive_data_to_heap(tableId, archiveId, buffer, size);
    // Process data...
    free(buffer);
    gameio_clear_archive_data(tableId, archiveId);
}
```

## API Reference

### Request Functions

#### `gameio_request_archive(int tableId, int archiveId)`

Request an archive from the server or cache.

**Parameters:**
- `tableId`: Cache table ID (0-254 for normal tables, 255 for reference tables)
- `archiveId`: Archive ID within the table

**Returns:**
- `> 0`: Request ID (download in progress)
- `0`: Cached (data immediately available)
- `-1`: Error

**Example:**
```cpp
int requestId = gameio_request_archive(5, 0); // Request map 0
```

---

### Status Functions

#### `gameio_get_request_status(int requestId)`

Get the status of a request.

**Parameters:**
- `requestId`: Request ID from `gameio_request_archive`

**Returns:** Status string:
- `"pending"`: Request created but not yet sent
- `"queued"`: Waiting in queue
- `"sending"`: Sending request to server
- `"receiving"`: Receiving response
- `"completed"`: Successfully completed
- `"error"`: Failed
- `"cached"`: Was in cache (requestId was 0)
- `"unknown"`: Request not found

**Example:**
```cpp
const char* status = gameio_get_request_status(requestId);
if (strcmp(status, "completed") == 0) {
    // Ready to get data
}
```

---

#### `gameio_is_request_pending(int tableId, int archiveId)`

Check if a specific archive is currently being requested.

**Returns:** `true` if pending, `false` otherwise

---

#### `gameio_get_queue_length()`

Get the number of requests waiting in the queue.

**Returns:** Queue length

---

#### `gameio_is_connected()`

Check if GameIO is connected to the WebSocket server.

**Returns:** `true` if connected, `false` otherwise

---

### Data Functions

#### `gameio_get_archive_data_size(int tableId, int archiveId)`

Get the size of downloaded archive data.

**Returns:** Size in bytes, or 0 if not available

---

#### `gameio_copy_archive_data_to_heap(int tableId, int archiveId, void* heapPtr, int maxSize)`

Copy archive data from JavaScript memory to WASM heap.

**Parameters:**
- `tableId`: Cache table ID
- `archiveId`: Archive ID
- `heapPtr`: Pointer to pre-allocated buffer in WASM heap
- `maxSize`: Maximum bytes to copy

**Returns:** Number of bytes copied

**Example:**
```cpp
int size = gameio_get_archive_data_size(5, 0);
char* buffer = (char*)malloc(size);
int copied = gameio_copy_archive_data_to_heap(5, 0, buffer, size);
// Use buffer...
free(buffer);
```

---

#### `gameio_clear_archive_data(int tableId, int archiveId)`

Clear archive data from JavaScript memory after copying.  
**Call this after you're done to free memory!**

---

### Debug Functions

#### `gameio_print_current_request()`

Print current request info to browser console.

---

#### `gameio_print_active_requests()`

Print all active requests to browser console.

---

## Complete Example

```cpp
#include "src/platforms/browser/gameio_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structure to track pending downloads
struct PendingArchive {
    int requestId;
    int tableId;
    int archiveId;
};

PendingArchive pendingArchives[10];
int numPending = 0;

void request_map_async(int mapX, int mapY) {
    // Calculate map archive ID (example)
    int archiveId = (mapX << 8) | mapY;
    
    int requestId = gameio_request_archive(5, archiveId); // Table 5 = maps
    
    if (requestId == 0) {
        // Cached - process immediately
        process_map_data(archiveId);
    } else if (requestId > 0) {
        // Add to pending list
        pendingArchives[numPending].requestId = requestId;
        pendingArchives[numPending].tableId = 5;
        pendingArchives[numPending].archiveId = archiveId;
        numPending++;
    } else {
        printf("Failed to request map %d,%d\n", mapX, mapY);
    }
}

void process_map_data(int archiveId) {
    // Get data size
    int size = gameio_get_archive_data_size(5, archiveId);
    if (size == 0) return;
    
    // Allocate buffer
    unsigned char* buffer = (unsigned char*)malloc(size);
    
    // Copy from JS to C++
    gameio_copy_archive_data_to_heap(5, archiveId, buffer, size);
    
    // Parse map data...
    printf("Processing map archive %d (%d bytes)\n", archiveId, size);
    
    // Clean up
    free(buffer);
    gameio_clear_archive_data(5, archiveId);
}

void update_loop() {
    // Check all pending downloads
    for (int i = 0; i < numPending; i++) {
        const char* status = gameio_get_request_status(
            pendingArchives[i].requestId
        );
        
        if (strcmp(status, "completed") == 0) {
            // Download complete, process the data
            process_map_data(pendingArchives[i].archiveId);
            
            // Remove from pending list
            for (int j = i; j < numPending - 1; j++) {
                pendingArchives[j] = pendingArchives[j + 1];
            }
            numPending--;
            i--; // Adjust index since we removed an element
        } else if (strcmp(status, "error") == 0) {
            printf("Download failed for archive %d\n", 
                   pendingArchives[i].archiveId);
            // Remove from pending
            for (int j = i; j < numPending - 1; j++) {
                pendingArchives[j] = pendingArchives[j + 1];
            }
            numPending--;
            i--;
        }
    }
}

int main() {
    printf("Requesting maps...\n");
    
    // Request some maps
    request_map_async(50, 50); // Lumbridge
    request_map_async(51, 50);
    request_map_async(50, 51);
    
    // In real code, call update_loop() in your game loop
    printf("Call update_loop() in your game loop\n");
    
    return 0;
}
```

## Request Status Flow

```
gameio_request_archive(5, 0)
         ↓
    ┌────┴────┐
    │ Cached? │
    └────┬────┘
         │
    ┌────┴────┐
    │ Yes│ No │
    └────┼────┘
         │    │
         │    └──→ requestId > 0
         │         status = "pending"
         │              ↓
         │         status = "queued"
         │              ↓
         │         status = "sending"
         │              ↓
         │         status = "receiving"
         │              ↓
         │         status = "completed"
         │              ↓
         ↓         [Data ready]
    requestId = 0
    [Data ready immediately]
```

## Common Patterns

### Pattern 1: Synchronous (Cached Only)

```cpp
int requestId = gameio_request_archive(5, 0);
if (requestId == 0) {
    // Process immediately
    int size = gameio_get_archive_data_size(5, 0);
    // ... copy and process
} else {
    printf("Not cached yet\n");
}
```

### Pattern 2: Asynchronous (With Polling)

```cpp
// In initialization/when needed
int requestId = gameio_request_archive(5, 0);
if (requestId > 0) {
    save_pending_request(requestId, 5, 0);
}

// In game loop
void update() {
    for (auto& req : pendingRequests) {
        if (strcmp(gameio_get_request_status(req.id), "completed") == 0) {
            process_archive(req.tableId, req.archiveId);
            mark_processed(req);
        }
    }
}
```

### Pattern 3: Batch Loading

```cpp
void load_map_region(int centerX, int centerY, int radius) {
    for (int dx = -radius; dx <= radius; dx++) {
        for (int dy = -radius; dy <= radius; dy++) {
            int mapId = ((centerX + dx) << 8) | (centerY + dy);
            int reqId = gameio_request_archive(5, mapId);
            
            if (reqId == 0) {
                process_map_immediate(mapId);
            } else if (reqId > 0) {
                queue_for_processing(reqId, mapId);
            }
        }
    }
}
```

## Cache Table IDs

| Table | Content |
|-------|---------|
| 0 | Underlays |
| 1 | Identikit |
| 2 | Configs |
| 3 | Interfaces |
| 4 | Sound Effects |
| **5** | **Maps** |
| 6 | Music |
| **7** | **Models** |
| 8 | Sprites |
| 9 | Textures |
| 10 | Binary |
| 12 | Scripts |
| 13 | Font Metrics |
| **255** | **Reference Tables** |

## Troubleshooting

### "GameIO API not available"

**Problem:** C++ code runs before JavaScript GameIO is initialized.

**Solution:** GameIO auto-initializes on page load. Ensure your C++ code runs after:
```javascript
window.addEventListener('load', async () => {
    await initGameIO();
    // Now safe to call C++ code that uses GameIO
    Module._your_cpp_function();
});
```

### Data size is 0

**Problem:** Calling `gameio_get_archive_data_size()` returns 0.

**Reasons:**
1. Request not completed yet (check status)
2. Request failed (status = "error")
3. Archive doesn't exist

**Solution:**
```cpp
if (strcmp(gameio_get_request_status(reqId), "completed") == 0) {
    int size = gameio_get_archive_data_size(tableId, archiveId);
    if (size > 0) {
        // OK to proceed
    }
}
```

### Memory leaks

**Problem:** JavaScript memory fills up with archive data.

**Solution:** Always call `gameio_clear_archive_data()` after copying:
```cpp
gameio_copy_archive_data_to_heap(5, 0, buffer, size);
// Use buffer...
gameio_clear_archive_data(5, 0); // Important!
```

## Performance Tips

1. **Request Early**: Request archives before you need them
2. **Batch Requests**: Request multiple archives at once (they queue automatically)
3. **Clean Up**: Always call `gameio_clear_archive_data()` after copying
4. **Check Cache First**: Cached archives (requestId=0) are instant
5. **Monitor Queue**: Use `gameio_get_queue_length()` to avoid overwhelming the system

## See Also

- [GameIO.ts README](README.md) - Full GameIO documentation
- [QUICKSTART.md](QUICKSTART.md) - Setup guide
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture
- [gameio_example.cpp](gameio_example.cpp) - Complete examples

---

**Integration Version**: 1.0  
**Date**: November 2025

