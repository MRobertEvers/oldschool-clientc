/*
 * Browser record store — C half. The design rationale is in the header; this
 * file is the glue and the JS that owns the resident map.
 *
 * Every EM_JS entry point below is a plain synchronous map operation. The
 * asynchrony (opening the database, the hydrate cursor, the write-back queue)
 * is entirely on the JS side and entirely outside the calls C makes, which is
 * what lets a synchronous dat2 read stand over an asynchronous database.
 */

#include "platform/dat2_web_store.h"
#include <assert.h>

#include <dat2disk.h>

#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
/* Compiling this natively is useful for a syntax check and for the store's own
 * unit test, which supplies its own vtable. Every call reports "no store". */
#define EM_JS(ret, name, args, body)                                                               \
    static ret name args                                                                           \
    {                                                                                              \
        return (ret)0;                                                                             \
    }
#define EM_JS_VOID(ret, name, args, body)                                                          \
    static ret name args                                                                           \
    {                                                                                              \
    }
#endif

/* clang-format off */

/*
 * Select the cache this store addresses. Returns 1 when the host has a
 * hydrated map for it, 0 when the harness is absent or never opened one.
 *
 * Not a no-op even when the key is the one already selected: it is also the
 * probe that tells C whether there is a host at all.
 */
EM_JS(int, torirs_web_store_open, (const char* key_ptr), {
    if( !Module.torirsStore || !Module.torirsStore.select ) { return 0; }
    return Module.torirsStore.select(UTF8ToString(key_ptr)) ? 1 : 0;
});

/*
 * Copy one record into the wasm heap. Returns 1 and fills the two out-params,
 * or 0 when the map does not hold it.
 *
 * The allocation happens here rather than in C because the size is only known
 * on this side; the caller frees it with the module's free, which is the same
 * allocator _malloc used.
 */
EM_JS(int, torirs_web_store_get, (int table_id, int archive_id, uint8_t** data_out, int* size_out), {
    if( !Module.torirsStore ) { return 0; }
    var bytes = Module.torirsStore.get(table_id, archive_id);
    if( !bytes ) { return 0; }
    var ptr = _malloc(bytes.length);
    if( !ptr ) { return 0; }
    /* HEAPU8 is re-read after the allocation: growing the heap detaches the
     * old ArrayBuffer and a view captured before it would write into memory
     * the runtime no longer owns. */
    HEAPU8.set(bytes, ptr);
    setValue(data_out, ptr, '*');
    setValue(size_out, bytes.length, 'i32');
    return 1;
});

EM_JS(int, torirs_web_store_put, (int table_id, int archive_id, const uint8_t* data, int size), {
    if( !Module.torirsStore ) { return -1; }
    return Module.torirsStore.put(table_id, archive_id,
                                  HEAPU8.slice(data, data + size)) ? 0 : -1;
});

EM_JS(int, torirs_web_store_has_table, (int table_id), {
    if( !Module.torirsStore ) { return 0; }
    return Module.torirsStore.hasTable(table_id) ? 1 : 0;
});

EM_JS(int, torirs_web_store_record_count, (void), {
    return Module.torirsStore ? Module.torirsStore.recordCount() : 0;
});

EM_JS(double, torirs_web_store_byte_count, (void), {
    return Module.torirsStore ? Module.torirsStore.byteCount() : 0;
});

EM_JS(int, torirs_web_file_get, (const char* path_ptr, uint8_t** data_out, int* size_out), {
    if( !Module.torirsStore || !Module.torirsStore.fileGet ) { return -1; }
    var bytes = Module.torirsStore.fileGet(UTF8ToString(path_ptr));
    if( bytes === null || bytes === undefined ) { return 0; }
    var ptr = _malloc(bytes.length ? bytes.length : 1);
    if( !ptr ) { return -1; }
    HEAPU8.set(bytes, ptr);
    setValue(data_out, ptr, '*');
    setValue(size_out, bytes.length, 'i32');
    return 1;
});

EM_JS(int, torirs_web_file_put, (const char* path_ptr, const uint8_t* data, int size), {
    if( !Module.torirsStore || !Module.torirsStore.filePut ) { return -1; }
    return Module.torirsStore.filePut(UTF8ToString(path_ptr),
                                      HEAPU8.slice(data, data + size)) ? 0 : -1;
});

/* 1 = bytes, 0 = the server has no such file, -1 = nothing answered,
 * -2 = this page cannot fetch at all. See Dat2WebStore_FileFetch. */
EM_JS(int, torirs_web_file_fetch, (const char* path_ptr, uint8_t** data_out, int* size_out), {
    if( !Module.torirsStore || !Module.torirsStore.fileFetch ) { return -2; }
    var r = Module.torirsStore.fileFetch(UTF8ToString(path_ptr));
    if( !r || r.status === 0 ) { return -1; }
    if( !r.bytes ) { return 0; }
    var ptr = _malloc(r.bytes.length ? r.bytes.length : 1);
    if( !ptr ) { return -1; }
    HEAPU8.set(r.bytes, ptr);
    setValue(data_out, ptr, '*');
    setValue(size_out, r.bytes.length, 'i32');
    return 1;
});

/* clang-format on */

struct Dat2WebStore
{
    char* cache_key;
    int written;
};

/*
 * One store per process.
 *
 * The JS map is a single object on Module, selected by cache key, so two C
 * handles addressing different keys would silently share it. App opens one
 * cache, so a second handle is already a bug; this makes it a visible one.
 */
static struct Dat2WebStore* g_web_store = NULL;

static int
web_store_get(
    void* user,
    int table_id,
    int archive_id,
    uint8_t** data,
    int* size)
{
    (void)user;
    assert(data);
    assert(size);
    *data = NULL;
    *size = 0;
    return torirs_web_store_get(table_id, archive_id, data, size);
}

static int
web_store_put(
    void* user,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int size)
{
    struct Dat2WebStore* store = (struct Dat2WebStore*)user;

    if( size <= 0 )
        return -1;
    assert(data);
    if( torirs_web_store_put(table_id, archive_id, data, size) != 0 )
        return -1;
    if( store )
        store->written++;
    return 0;
}

static int
web_store_has_table(
    void* user,
    int table_id)
{
    (void)user;
    return torirs_web_store_has_table(table_id);
}

struct Dat2WebStore*
Dat2WebStore_New(const char* cache_key)
{
    struct Dat2WebStore* store;

    assert(cache_key);
    if( !cache_key[0] || g_web_store )
        return NULL;
    if( !torirs_web_store_open(cache_key) )
        return NULL;

    store = calloc(1u, sizeof(*store));
    assert(store);
    store->cache_key = strdup(cache_key);
    assert(store->cache_key);
    g_web_store = store;
    return store;
}

void
Dat2WebStore_Free(struct Dat2WebStore* store)
{
    if( !store )
        return;
    if( g_web_store == store )
        g_web_store = NULL;
    free(store->cache_key);
    free(store);
}

struct RSCache_Dat2Store
Dat2WebStore_Ops(struct Dat2WebStore* store)
{
    struct RSCache_Dat2Store ops;

    memset(&ops, 0, sizeof(ops));
    ops.user = store;
    ops.get = web_store_get;
    ops.put = web_store_put;
    ops.has_table = web_store_has_table;
    return ops;
}

void
Dat2WebStore_Stats(
    const struct Dat2WebStore* store,
    int* out_records,
    int64_t* out_bytes,
    int* out_written)
{
    if( out_records )
        *out_records = store ? torirs_web_store_record_count() : 0;
    if( out_bytes )
        *out_bytes = store ? (int64_t)torirs_web_store_byte_count() : 0;
    if( out_written )
        *out_written = store ? store->written : 0;
}

int
Dat2WebStore_FileRead(
    const char* path,
    uint8_t** data,
    int* size)
{
    assert(path);
    assert(data);
    assert(size);
    *data = NULL;
    *size = 0;
    return torirs_web_file_get(path, data, size);
}

int
Dat2WebStore_FileWrite(
    const char* path,
    const uint8_t* data,
    int size)
{
    if( !path || (!data && size > 0) || size < 0 )
        return -1;
    return torirs_web_file_put(path, data, size);
}

int
Dat2WebStore_FileFetch(
    const char* path,
    uint8_t** data,
    int* size)
{
    int status;

    assert(path);
    assert(data);
    assert(size);
    *data = NULL;
    *size = 0;

    status = torirs_web_file_fetch(path, data, size);
    /*
     * A page with no fetch facility is one that cannot ask, not one whose
     * server is down -- an older torirs_host.js, or a harness of some other
     * kind. Reported as "absent" so it behaves exactly as this lane did before
     * the second leg existed, instead of putting the client into an outage it
     * has no way out of.
     */
    if( status == -2 )
        return 0;
    return status;
}
