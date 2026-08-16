#ifndef SRC_PLATFORM_IO_WIRE_H
#define SRC_PLATFORM_IO_WIRE_H

/*
 * Wire form of a struct ToriRS_IOItem.
 *
 * The web host has no disk, so the one place the native build touches files —
 * PlatformX_IO_LoadItem — is moved across a socket. This file is the codec for
 * that move, and it is deliberately the *only* thing both halves share: the IO
 * server runs the real PlatformX_IO_LoadItem against a real cache directory and
 * encodes what it produced; the browser decodes it back into an item that is
 * indistinguishable from one the native backend filled in. Every task above the
 * platform layer stays unaware that anything moved.
 *
 * Encoding it at the *item* seam rather than at the file seam is what keeps the
 * cache's semantics on one side of the wire. Resolving a logical table to this
 * cache's table id, deciding whether a map archive is XTEA-encrypted, and
 * mapping a dat1 map square through the versionlist all need the open cache to
 * answer, so they stay with the cache. What crosses is a decompressed archive
 * and its metadata, which is just bytes and ints.
 *
 * Little-endian on both sides (wasm32 and every host we build for), but encoded
 * byte by byte anyway so a big-endian server is not a silent corruption.
 *
 * Batch framing (both directions):
 *
 *   'T' 'R' 'I' 'O' | u32 version | u32 count | cache | record * count
 *
 * A batch is how the browser pays for one round trip per frame instead of one
 * per archive: everything a task queued before it yielded goes out together.
 *
 * The `cache` header says which cache the batch is about — its identity and its
 * directory, exactly what the native client resolves from its boot manifest.
 * A request is only meaningful against a particular cache: the same table id
 * means different things on different branches, and a dat2 read against a dat1
 * cache is not a smaller answer but a different question. Naming it per batch
 * (rather than per record, which would repeat it, or once at connect, which
 * would make the server stateful) is what lets one server answer any client:
 * it opens what it is asked for and keeps it open.
 */

#include "asyncio.h"

#include <stdint.h>

#define IOWIRE_MAGIC0 'T'
#define IOWIRE_MAGIC1 'R'
#define IOWIRE_MAGIC2 'I'
#define IOWIRE_MAGIC3 'O'
/* 2: the batch header gained the cache descriptor. */
#define IOWIRE_VERSION 2u

#define IOWIRE_CACHE_DIR_MAX 256

/**
 * Which cache a batch is about.
 *
 * The four identity fields are the ones a boot manifest states ([cache:boot]
 * epoch/game/revision/quirks); `dir` is where it lives, relative to the
 * server's cache root. Together they are everything RSCache needs to open it
 * and answer in the same terms the native client would.
 */
struct IOWireCache
{
    int32_t epoch;    /* enum RSCache_Epoch */
    int32_t game;     /* enum RSCache_Game */
    int32_t revision;
    uint32_t quirks;
    char dir[IOWIRE_CACHE_DIR_MAX];
};

/** Growable output buffer. Zero-initialize (or IOWireBuf_Init) before use. */
struct IOWireBuf
{
    uint8_t* data;
    int len;
    int cap;
    /** Sticky: set when an append could not allocate. Callers check once at
     * the end rather than after every field. */
    int error;
};

void
IOWireBuf_Init(struct IOWireBuf* buf);

void
IOWireBuf_Free(struct IOWireBuf* buf);

/** Keep the allocation, drop the contents. */
void
IOWireBuf_Reset(struct IOWireBuf* buf);

void
IOWireBuf_Append(
    struct IOWireBuf* buf,
    void const* bytes,
    int count);

/** Bounds-checked reader over a received batch. */
struct IOWireReader
{
    uint8_t const* data;
    int len;
    int off;
    /** Sticky: set by the first read that ran past the end or failed a check. */
    int error;
};

void
IOWireReader_Init(
    struct IOWireReader* r,
    void const* data,
    int len);

/* --- Batch framing ------------------------------------------------------ */

/**
 * Write the batch header with count 0; every Write*Request/Response below
 * bumps the count in place, so the caller never tracks it.
 *
 * `cache` may be NULL for a response batch, which is about whatever cache the
 * request batch already named.
 */
void
IOWire_BatchBegin(
    struct IOWireBuf* buf,
    struct IOWireCache const* cache);

/**
 * Read and validate the header. Returns the record count, or -1.
 * `out_cache` may be NULL when the caller does not care which cache it names.
 */
int
IOWire_BatchRead(
    struct IOWireReader* r,
    struct IOWireCache* out_cache);

/* --- Requests ----------------------------------------------------------- */

struct IOWireRequest
{
    uint32_t req_id;
    int32_t kind; /* enum ToriRS_IOKind */
    int32_t epoch;
    int32_t table_id;
    int32_t archive_id;
    int32_t flags;
    char path[TORIRS_IOITEM_MAX_PATH];
};

/** Append `item`'s request half (everything PlatformX_IO_LoadItem reads). */
void
IOWire_WriteRequest(
    struct IOWireBuf* buf,
    uint32_t req_id,
    struct ToriRS_IOItem const* item);

/** Read one request record. Returns 0 on success, -1 on a malformed batch. */
int
IOWire_ReadRequest(
    struct IOWireReader* r,
    struct IOWireRequest* out);

/** Rebuild the item a request describes, ready for PlatformX_IO_LoadItem. */
void
IOWire_RequestToItem(
    struct IOWireRequest const* req,
    struct ToriRS_IOItem* out);

/* --- Responses ---------------------------------------------------------- */

/**
 * Append the result half of `item` — what PlatformX_IO_LoadItem left in
 * data/data_size/error_code, flattened. `item` must still carry the request
 * fields (kind and, for a cache read, u.cache.flags) because they select the
 * payload shape.
 */
void
IOWire_WriteResponse(
    struct IOWireBuf* buf,
    uint32_t req_id,
    struct ToriRS_IOItem const* item);

struct IOWireResponse
{
    uint32_t req_id;
    int32_t kind;
    int32_t flags;
    int32_t error;
    int32_t v[5];
    uint8_t const* blob; /* points into the reader's buffer */
    int32_t blob_len;
    uint8_t const* aux; /* int32 array, unaligned; points into the buffer */
    int32_t aux_count;
};

/** Read one response record. Returns 0 on success, -1 on a malformed batch. */
int
IOWire_ReadResponse(
    struct IOWireReader* r,
    struct IOWireResponse* out);

/**
 * Materialize a response into `item` exactly as the native backend would have:
 * freshly allocated structs that the item's normal owner frees. Returns 0 on
 * success; on failure the item is left with error_code set and no data, which
 * is the same thing a failed local read produces.
 */
int
IOWire_ApplyResponse(
    struct IOWireResponse const* resp,
    struct ToriRS_IOItem* item);

/**
 * Release what PlatformX_IO_LoadItem allocated into `item`.
 *
 * ToriRS_IO_ClearItem free()s item->data flat, which is right for a file read
 * and a leak for an archive (its `data` and `file_ids` hang off the struct).
 * The server loads and drops thousands of archives per session, so it needs the
 * kind-aware free.
 */
void
IOWire_FreeLoadedItem(struct ToriRS_IOItem* item);

/** Human-readable item description for server logs ("dat2 table=8 archive=3"). */
void
IOWire_DescribeItem(
    struct ToriRS_IOItem const* item,
    char* out,
    int out_size);

#endif
