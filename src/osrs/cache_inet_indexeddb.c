#ifdef __EMSCRIPTEN__

#include "cache_inet_indexeddb.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// JavaScript functions for IndexedDB operations
// Uses async polling (like recv) to avoid ASYNCIFY nesting issues
// clang-format off
EM_JS(void, js_indexeddb_init, (), {
    if (typeof Module.cacheDB !== 'undefined') {
        console.log("IndexedDB already initialized");
        return;
    }
    
    console.log("Initializing IndexedDB for cache archives...");
    
    Module.cacheDB = {
        db: null,
        ready: false,
        initPromise: null
    };
    
    // Request tracking for polling
    Module.idbRequests = {};
    Module.idbRequestCounter = 0;
    
    Module.cacheDB.initPromise = new Promise((resolve, reject) => {
        const request = indexedDB.open('OSRSCacheDB', 1);
        
        request.onerror = () => {
            console.error('IndexedDB failed to open');
            reject(request.error);
        };
        
        request.onsuccess = () => {
            Module.cacheDB.db = request.result;
            Module.cacheDB.ready = true;
            console.log('IndexedDB opened successfully');
            resolve();
        };
        
        request.onupgradeneeded = (event) => {
            const db = event.target.result;
            if (!db.objectStoreNames.contains('archives')) {
                const objectStore = db.createObjectStore('archives', { keyPath: 'key' });
                objectStore.createIndex('table_id', 'table_id', { unique: false });
                console.log('IndexedDB object store created');
            }
        };
    });
});

// Start an async IndexedDB read request
EM_JS(int, js_indexeddb_get_start, (int table_id, int archive_id), {
    if (!Module.cacheDB || !Module.cacheDB.ready || !Module.cacheDB.db) {
        return -1; // Not initialized
    }
    
    const key = table_id + '_' + archive_id;
    
    try {
        const transaction = Module.cacheDB.db.transaction(['archives'], 'readonly');
        const objectStore = transaction.objectStore('archives');
        const request = objectStore.get(key);
        
        // Generate unique request ID
        const requestId = (++Module.idbRequestCounter);
        
        Module.idbRequests[requestId] = {
            status: 'pending',
            result: null
        };
        
        request.onsuccess = () => {
            Module.idbRequests[requestId].status = 'success';
            Module.idbRequests[requestId].result = request.result;
        };
        
        request.onerror = () => {
            Module.idbRequests[requestId].status = 'error';
        };
        
        return requestId;
    } catch (e) {
        console.error('IndexedDB get_start error:', e);
        return -1;
    }
});

// Poll the request status: 0=pending, 1=success, -1=error
EM_JS(int, js_indexeddb_get_poll, (int request_id), {
    if (!Module.idbRequests || !Module.idbRequests[request_id]) {
        return -1; // Invalid request ID
    }
    
    const req = Module.idbRequests[request_id];
    
    if (req.status === 'pending') return 0; // Still waiting
    if (req.status === 'error') return -1; // Error occurred
    if (req.status === 'success') return 1; // Complete
    
    return -1;
});

// Get the result once poll returns success: 1=found, 0=not found, -1=error
EM_JS(int, js_indexeddb_get_result, (int request_id, char** data_ptr, int* size_ptr), {
    if (!Module.idbRequests || !Module.idbRequests[request_id]) {
        return -1; // Invalid request ID
    }
    
    const req = Module.idbRequests[request_id];
    
    // Clean up request tracking
    const result = req.result;
    delete Module.idbRequests[request_id];
    
    // Check if data exists
    if (!result || !result.data) {
        return 0; // Not found in IndexedDB
    }
    
    const data = result.data;
    const size = data.byteLength;
    
    // Allocate memory in C heap
    const ptr = _malloc(size);
    if (!ptr) {
        return -1; // Memory allocation failed
    }
    
    // Copy data to C heap
    HEAPU8.set(new Uint8Array(data), ptr);
    
    // Set output pointers
    setValue(data_ptr, ptr, '*');
    setValue(size_ptr, size, 'i32');
    
    return 1; // Found
});

// Store in IndexedDB (async, fire-and-forget)
EM_JS(int, js_indexeddb_put, (int table_id, int archive_id, const char* data, int size), {
    if (!Module.cacheDB || !Module.cacheDB.ready || !Module.cacheDB.db) {
        return -1; // Not initialized
    }
    
    const key = table_id + '_' + archive_id;
    
    try {
        // Copy data from C heap to JavaScript ArrayBuffer
        const dataArray = new Uint8Array(size);
        dataArray.set(HEAPU8.subarray(data, data + size));
        
        const record = {
            key: key,
            table_id: table_id,
            archive_id: archive_id,
            data: dataArray.buffer,
            timestamp: Date.now()
        };
        
        // Fire-and-forget: Store in IndexedDB asynchronously
        const transaction = Module.cacheDB.db.transaction(['archives'], 'readwrite');
        const objectStore = transaction.objectStore('archives');
        const request = objectStore.put(record);
        
        request.onsuccess = () => {
            console.log('Stored in IndexedDB: table=' + table_id + ', archive=' + archive_id + ', size=' + size);
        };
        
        request.onerror = () => {
            console.warn('IndexedDB store failed: table=' + table_id + ', archive=' + archive_id);
        };
        
        return 0; // Request started (success)
    } catch (e) {
        console.error('IndexedDB put error:', e);
        return -1; // Error
    }
});
// clang-format on

void
indexeddb_init(void)
{
    printf("Initializing IndexedDB for cache storage...\n");
    js_indexeddb_init();
    printf("IndexedDB initialization started (async)\n");
}

int
indexeddb_get_archive(int table_id, int archive_id, char** data_out, int* size_out)
{
    // Start the async IndexedDB read request
    int request_id = js_indexeddb_get_start(table_id, archive_id);
    if( request_id < 0 )
    {
        return -1; // Not initialized or error
    }

    // Poll until complete (same pattern as recv!)
    int poll_status;
    while( (poll_status = js_indexeddb_get_poll(request_id)) == 0 )
    {
        emscripten_sleep(5); // Wait 5ms and check again (IndexedDB is usually fast)
    }

    if( poll_status < 0 )
    {
        printf("IndexedDB read error: table=%d, archive=%d\n", table_id, archive_id);
        return -1; // Error occurred
    }

    // Get the result
    int result = js_indexeddb_get_result(request_id, data_out, size_out);
    if( result == 1 )
    {
        printf("IndexedDB cache HIT: table=%d, archive=%d\n", table_id, archive_id);
    }
    else if( result == 0 )
    {
        printf("IndexedDB cache MISS: table=%d, archive=%d\n", table_id, archive_id);
    }

    return result;
}

int
indexeddb_put_archive(int table_id, int archive_id, const char* data, int size)
{
    // Fire-and-forget background storage
    return js_indexeddb_put(table_id, archive_id, data, size);
}

#endif // __EMSCRIPTEN__
