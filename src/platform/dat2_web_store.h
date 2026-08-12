#ifndef SRC_PLATFORM_DAT2_WEB_STORE_H
#define SRC_PLATFORM_DAT2_WEB_STORE_H

/*
 * The browser's cache backing: a keyed record store, wearing a dat2 face.
 *
 * A dat2 cache is a sector-chained blob file plus a table of six-byte index
 * records, and every part of that exists to give a filesystem something it can
 * do — one file handle, append-only growth, offsets instead of names. A browser
 * has no such handle. What it has is IndexedDB, which is already a key-value
 * store, so laying a sector chain over it buys nothing and costs twice: the
 * 520-byte sector headers, and the orphaned sectors that a rewrite of an
 * existing archive leaves behind (RSCache_Dat2DiskWriteArchive appends and
 * re-points, exactly as the real client does).
 *
 * So the browser stores one record per archive, keyed by (cache, table,
 * archive), holding the same bytes an idx record would have addressed: the JS5
 * container followed by whatever local version trailer the writer appended.
 * That is the unit RSCache_Dat2DiskArchiveNewLoadRaw returns and
 * RSCache_Dat2DiskWriteArchiveTo accepts, so nothing above the store needs a
 * second encoding — and RSCache_Dat2Disk, reference tables, XTEA, table id
 * resolution and the whole decode path are the code the desktop build runs.
 *
 * ## Why the records live in the JS heap
 *
 * IndexedDB is asynchronous and this lane has no ASYNCIFY (see
 * src/platform/platform_check.mk), so a store->get that had to reach the
 * database could not answer the synchronous call it is standing in for. The
 * resolution is to keep the resident records on the JavaScript side, hydrated
 * in one cursor pass before main() runs, and let get() be a map lookup.
 *
 * Two things follow, both deliberate:
 *
 *   - The bytes do not sit in the wasm heap. Only the archive being decoded
 *     right now is copied in, and the caller frees it. A cache far larger than
 *     the 4GB wasm ceiling is therefore not itself a reason the module runs out
 *     of memory.
 *   - A record the hydration did not load reads as absent, not as an error. It
 *     is the same answer an empty cache gives, so JS5 downloads it again and
 *     re-writes it. Eviction and a partial hydrate cost bandwidth; they cannot
 *     produce a wrong archive.
 *
 * Nothing is evicted mid-session. JS5 remembers which groups it validated, and
 * a group it believes is ready must still be readable when a task asks for it —
 * dropping one would turn a completed download into a failed cache read.
 */

#include <stdint.h>

struct RSCache_Dat2Store;
struct Dat2WebStore;

/*
 * Attach to the record store the host hydrated for `cache_key`.
 *
 * `cache_key` is the manifest's cache directory name (cache.osrs239.summoning
 * and friends). It scopes the records, so one browser profile holds one store
 * per generation and switching manifest in the URL cannot mix them.
 *
 * Returns NULL when the host harness is absent or never opened the database,
 * which is the honest answer for a page that is not torirs_host.js.
 */
struct Dat2WebStore*
Dat2WebStore_New(const char* cache_key);

void
Dat2WebStore_Free(struct Dat2WebStore* store);

/** The dat2 backend vtable, for RSCache_Dat2DiskNewFromStore. */
struct RSCache_Dat2Store
Dat2WebStore_Ops(struct Dat2WebStore* store);

/** Records held resident, bytes held resident, records written this session. */
void
Dat2WebStore_Stats(
    const struct Dat2WebStore* store,
    int* out_records,
    int64_t* out_bytes,
    int* out_written);

/*
 * Read/write a client-owned file (the player's saved options) through the same
 * database.
 *
 * These are not cache archives and do not go in the archive store, but they
 * have the same problem: emscripten's MEMFS forgets them when the tab closes,
 * so without this the audio panel's volumes reset on every reload — which is
 * the very defect rs_prefs.c exists to fix on the desktop.
 *
 * Read returns 1 and a malloc'd buffer, 0 when there is no such file, -1 on
 * error. Write returns 0 on success.
 */
int
Dat2WebStore_FileRead(const char* path, uint8_t** data, int* size);

int
Dat2WebStore_FileWrite(const char* path, const uint8_t* data, int size);

#endif
