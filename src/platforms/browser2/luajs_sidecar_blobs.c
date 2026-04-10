#include "osrs/lua_sidecar/luac_sidecar_command_dispatch.h"

#include <emscripten.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

// clang-format off
EM_JS(int, js_gameblob_is_ready, (), {
    return (Module.gameBlobDB && Module.gameBlobDB.ready && Module.gameBlobDB.db) ? 1 : 0;
});

EM_JS(void, js_gameblob_init, (), {
    if (typeof Module.gameBlobDB !== 'undefined') {
        return;
    }
    Module.gameBlobDB = {
        db: null,
        ready: false,
        initPromise: null
    };
    Module.gameBlobRequests = {};
    Module.gameBlobRequestCounter = 0;

    Module.gameBlobDB.initPromise = new Promise((resolve, reject) => {
        const request = indexedDB.open('GameBlobs', 1);
        request.onerror = () => reject(request.error);
        request.onsuccess = () => {
            Module.gameBlobDB.db = request.result;
            Module.gameBlobDB.ready = true;
            resolve();
        };
        request.onupgradeneeded = (event) => {
            const db = event.target.result;
            if (!db.objectStoreNames.contains('blobs')) {
                db.createObjectStore('blobs', { keyPath: 'name' });
            }
        };
    });
});

EM_JS(int, js_gameblob_put, (const char* name_utf8, const char* data, int size), {
    if (!Module.gameBlobDB || !Module.gameBlobDB.ready || !Module.gameBlobDB.db) {
        return -1;
    }
    let nameLen = 0;
    while (HEAPU8[name_utf8 + nameLen] !== 0) nameLen++;
    const name = new TextDecoder().decode(HEAPU8.subarray(name_utf8, name_utf8 + nameLen));
    const dataArray = new Uint8Array(size);
    dataArray.set(HEAPU8.subarray(data, data + size));
    try {
        const transaction = Module.gameBlobDB.db.transaction(['blobs'], 'readwrite');
        const store = transaction.objectStore('blobs');
        store.put({ name: name, data: dataArray.buffer });
        return 0;
    } catch (e) {
        console.error('GameBlobs put error:', e);
        return -1;
    }
});

EM_JS(int, js_gameblob_get_start, (const char* name_utf8), {
    if (!Module.gameBlobDB || !Module.gameBlobDB.ready || !Module.gameBlobDB.db) {
        return -1;
    }
    let nameLen = 0;
    while (HEAPU8[name_utf8 + nameLen] !== 0) nameLen++;
    const name = new TextDecoder().decode(HEAPU8.subarray(name_utf8, name_utf8 + nameLen));

    const requestId = (++Module.gameBlobRequestCounter);
    Module.gameBlobRequests[requestId] = { status: 'pending', result: null };

    try {
        const transaction = Module.gameBlobDB.db.transaction(['blobs'], 'readonly');
        const store = transaction.objectStore('blobs');
        const request = store.get(name);
        request.onsuccess = () => {
            Module.gameBlobRequests[requestId].status = 'success';
            Module.gameBlobRequests[requestId].result = request.result;
        };
        request.onerror = () => {
            Module.gameBlobRequests[requestId].status = 'error';
        };
        return requestId;
    } catch (e) {
        delete Module.gameBlobRequests[requestId];
        return -1;
    }
});

EM_JS(int, js_gameblob_get_poll, (int request_id), {
    const req = Module.gameBlobRequests && Module.gameBlobRequests[request_id];
    if (!req) return -1;
    if (req.status === 'pending') return 0;
    if (req.status === 'error') return -1;
    if (req.status === 'success') return 1;
    return -1;
});

EM_JS(int, js_gameblob_get_result, (int request_id, char** data_ptr, int* size_ptr), {
    const req = Module.gameBlobRequests[request_id];
    if (!req) return -1;
    const result = req.result;
    delete Module.gameBlobRequests[request_id];

    if (!result || !result.data) {
        return 0;
    }
    const data = result.data;
    const size = data.byteLength;
    const ptr = _malloc(size);
    if (!ptr) return -1;
    HEAPU8.set(new Uint8Array(data), ptr);
    setValue(data_ptr, ptr, '*');
    setValue(size_ptr, size, 'i32');
    return 1;
});
// clang-format on

static void
gameblob_ensure_init(void)
{
    static int inited = 0;
    if( !inited )
    {
        js_gameblob_init();
        inited = 1;
    }
    for( int i = 0; i < 2000; i++ )
    {
        if( js_gameblob_is_ready() == 1 )
            return;
        emscripten_sleep(5);
    }
}

int
LuaCSidecar_BlobStore(
    const char* name_utf8,
    const void* data,
    size_t size)
{
    if( !name_utf8 || size > (size_t)INT_MAX )
        return -1;
    gameblob_ensure_init();
    return js_gameblob_put(name_utf8, (const char*)data, (int)size) == 0 ? 0 : -1;
}

int
LuaCSidecar_BlobRead(
    const char* name_utf8,
    void** data_out,
    int* size_out)
{
    *data_out = NULL;
    *size_out = 0;
    if( !name_utf8 )
        return -1;

    gameblob_ensure_init();

    int rid = js_gameblob_get_start(name_utf8);
    if( rid < 0 )
        return -1;

    int poll_status;
    while( (poll_status = js_gameblob_get_poll(rid)) == 0 )
    {
        emscripten_sleep(5);
    }

    if( poll_status < 0 )
        return -1;

    char* data = NULL;
    int sz = 0;
    int gr = js_gameblob_get_result(rid, &data, &sz);
    if( gr < 0 )
        return -1;
    if( gr == 0 || sz == 0 )
    {
        return -1;
    }

    *data_out = data;
    *size_out = sz;
    return 0;
}
