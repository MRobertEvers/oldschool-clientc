#include "platform/io_wire.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Batch header is magic(4) + version(4) + count(4) + the cache descriptor; the
 * count is patched in place as records are appended, so its offset is fixed. */
#define IOWIRE_HEADER_SIZE 12
#define IOWIRE_COUNT_OFFSET 8

void
IOWireBuf_Init(struct IOWireBuf* buf)
{
    assert(buf);
    memset(buf, 0, sizeof(*buf));
}

void
IOWireBuf_Free(struct IOWireBuf* buf)
{
    if( !buf )
        return;
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

void
IOWireBuf_Reset(struct IOWireBuf* buf)
{
    assert(buf);
    buf->len = 0;
    buf->error = 0;
}

void
IOWireBuf_Append(
    struct IOWireBuf* buf,
    void const* bytes,
    int count)
{
    assert(buf);
    if( buf->error || count < 0 )
        return;
    if( count == 0 )
        return;
    if( buf->len + count > buf->cap )
    {
        int cap = buf->cap ? buf->cap : 4096;
        uint8_t* grown;
        while( cap < buf->len + count )
            cap *= 2;
        grown = realloc(buf->data, (size_t)cap);
        if( !grown )
        {
            buf->error = 1;
            return;
        }
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, bytes, (size_t)count);
    buf->len += count;
}

static void
put_u32(
    struct IOWireBuf* buf,
    uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    IOWireBuf_Append(buf, b, 4);
}

static void
put_i32(
    struct IOWireBuf* buf,
    int32_t v)
{
    put_u32(buf, (uint32_t)v);
}

/* Length-prefixed blob. A NULL pointer with a positive length would be a bug
 * in the caller, so it is normalized to empty rather than dereferenced. */
static void
put_blob(
    struct IOWireBuf* buf,
    void const* data,
    int len)
{
    if( !data || len < 0 )
        len = 0;
    put_u32(buf, (uint32_t)len);
    IOWireBuf_Append(buf, data, len);
}

void
IOWireReader_Init(
    struct IOWireReader* r,
    void const* data,
    int len)
{
    assert(r);
    r->data = (uint8_t const*)data;
    r->len = len < 0 ? 0 : len;
    r->off = 0;
    r->error = 0;
}

static uint32_t
get_u32(struct IOWireReader* r)
{
    uint32_t v;
    if( r->error || r->off + 4 > r->len )
    {
        r->error = 1;
        return 0;
    }
    v = (uint32_t)r->data[r->off] | ((uint32_t)r->data[r->off + 1] << 8) |
        ((uint32_t)r->data[r->off + 2] << 16) | ((uint32_t)r->data[r->off + 3] << 24);
    r->off += 4;
    return v;
}

static int32_t
get_i32(struct IOWireReader* r)
{
    return (int32_t)get_u32(r);
}

/* Returns a pointer into the reader's buffer (no copy) and its length. */
static uint8_t const*
get_blob(
    struct IOWireReader* r,
    int32_t* out_len)
{
    uint32_t len = get_u32(r);
    uint8_t const* p;
    *out_len = 0;
    if( r->error )
        return NULL;
    if( len > (uint32_t)(r->len - r->off) )
    {
        r->error = 1;
        return NULL;
    }
    p = r->data + r->off;
    r->off += (int)len;
    *out_len = (int32_t)len;
    return len ? p : NULL;
}

void
IOWire_BatchBegin(
    struct IOWireBuf* buf,
    struct IOWireCache const* cache)
{
    uint8_t magic[4] = { IOWIRE_MAGIC0, IOWIRE_MAGIC1, IOWIRE_MAGIC2, IOWIRE_MAGIC3 };
    struct IOWireCache none;

    IOWireBuf_Reset(buf);
    IOWireBuf_Append(buf, magic, 4);
    put_u32(buf, IOWIRE_VERSION);
    put_u32(buf, 0);

    if( !cache )
    {
        memset(&none, 0, sizeof(none));
        cache = &none;
    }
    put_i32(buf, cache->epoch);
    put_i32(buf, cache->game);
    put_i32(buf, cache->revision);
    put_u32(buf, cache->quirks);
    put_blob(buf, cache->dir, (int)strlen(cache->dir));
}

static void
batch_bump(struct IOWireBuf* buf)
{
    uint8_t* p;
    uint32_t count;
    if( buf->error || buf->len < IOWIRE_HEADER_SIZE )
        return;
    p = buf->data + IOWIRE_COUNT_OFFSET;
    count = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
            ((uint32_t)p[3] << 24);
    count++;
    p[0] = (uint8_t)(count & 0xFF);
    p[1] = (uint8_t)((count >> 8) & 0xFF);
    p[2] = (uint8_t)((count >> 16) & 0xFF);
    p[3] = (uint8_t)((count >> 24) & 0xFF);
}

int
IOWire_BatchRead(
    struct IOWireReader* r,
    struct IOWireCache* out_cache)
{
    uint32_t version;
    uint32_t count;
    struct IOWireCache cache;
    uint8_t const* dir;
    int32_t dir_len = 0;

    assert(r);
    if( out_cache )
        memset(out_cache, 0, sizeof(*out_cache));
    if( r->len < IOWIRE_HEADER_SIZE )
        return -1;
    if( r->data[0] != IOWIRE_MAGIC0 || r->data[1] != IOWIRE_MAGIC1 ||
        r->data[2] != IOWIRE_MAGIC2 || r->data[3] != IOWIRE_MAGIC3 )
        return -1;
    r->off = 4;
    version = get_u32(r);
    count = get_u32(r);
    if( r->error || version != IOWIRE_VERSION )
        return -1;

    memset(&cache, 0, sizeof(cache));
    cache.epoch = get_i32(r);
    cache.game = get_i32(r);
    cache.revision = get_i32(r);
    cache.quirks = get_u32(r);
    dir = get_blob(r, &dir_len);
    if( r->error || dir_len >= IOWIRE_CACHE_DIR_MAX )
        return -1;
    if( dir_len > 0 )
        memcpy(cache.dir, dir, (size_t)dir_len);
    cache.dir[dir_len] = '\0';
    if( out_cache )
        *out_cache = cache;
    /* A count larger than the buffer could hold is a truncated or hostile
     * batch; refuse before allocating anything per record. */
    if( count > (uint32_t)r->len )
        return -1;
    return (int)count;
}

void
IOWire_WriteRequest(
    struct IOWireBuf* buf,
    uint32_t req_id,
    struct ToriRS_IOItem const* item)
{
    char const* path = "";

    assert(buf);
    assert(item);

    if( item->kind == TORIRS_IOK_CONFIG_FILE )
        path = item->u.config_file.path;
    else if( item->kind == TORIRS_IOK_SCRIPT )
        path = item->u.script.path;

    put_u32(buf, req_id);
    put_i32(buf, (int32_t)item->kind);
    put_i32(buf, item->kind == TORIRS_IOK_CACHE ? item->u.cache.epoch : 0);
    put_i32(
        buf,
        item->kind == TORIRS_IOK_CACHE            ? item->u.cache.table_id
        : item->kind == TORIRS_IOK_REFERENCE_TABLE ? item->u.reference_table.table_id
                                                   : 0);
    put_i32(buf, item->kind == TORIRS_IOK_CACHE ? item->u.cache.archive_id : 0);
    put_i32(buf, item->kind == TORIRS_IOK_CACHE ? item->u.cache.flags : 0);
    put_blob(buf, path, (int)strlen(path));
    batch_bump(buf);
}

int
IOWire_ReadRequest(
    struct IOWireReader* r,
    struct IOWireRequest* out)
{
    uint8_t const* path;
    int32_t path_len = 0;

    assert(r);
    assert(out);
    memset(out, 0, sizeof(*out));

    out->req_id = get_u32(r);
    out->kind = get_i32(r);
    out->epoch = get_i32(r);
    out->table_id = get_i32(r);
    out->archive_id = get_i32(r);
    out->flags = get_i32(r);
    path = get_blob(r, &path_len);
    if( r->error )
        return -1;
    if( path_len >= TORIRS_IOITEM_MAX_PATH )
        return -1;
    if( path_len > 0 )
        memcpy(out->path, path, (size_t)path_len);
    out->path[path_len] = '\0';
    return 0;
}

void
IOWire_RequestToItem(
    struct IOWireRequest const* req,
    struct ToriRS_IOItem* out)
{
    assert(req);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->kind = (enum ToriRS_IOKind)req->kind;
    switch( out->kind )
    {
    case TORIRS_IOK_CACHE:
        out->u.cache.epoch = req->epoch;
        out->u.cache.table_id = req->table_id;
        out->u.cache.archive_id = req->archive_id;
        out->u.cache.flags = req->flags;
        break;
    case TORIRS_IOK_CONFIG_FILE:
        snprintf(out->u.config_file.path, sizeof(out->u.config_file.path), "%s", req->path);
        break;
    case TORIRS_IOK_SCRIPT:
        snprintf(out->u.script.path, sizeof(out->u.script.path), "%s", req->path);
        break;
    case TORIRS_IOK_REFERENCE_TABLE:
        out->u.reference_table.table_id = req->table_id;
        break;
    default:
        out->kind = TORIRS_IOK_NONE;
        break;
    }
}

/* A cache request carrying any of the dat1 flags reads a dat1 archive; every
 * other value is dat2. Same test load_cache_item makes. */
static int
cache_item_is_dat1(struct ToriRS_IOItem const* item)
{
    return item->u.cache.flags == TORIRS_IO_CACHE_DAT1 ||
           item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN ||
           item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY;
}

void
IOWire_WriteResponse(
    struct IOWireBuf* buf,
    uint32_t req_id,
    struct ToriRS_IOItem const* item)
{
    int32_t v[5] = { 0, 0, 0, 0, 0 };
    void const* blob = NULL;
    int blob_len = 0;
    int32_t const* aux = NULL;
    int aux_count = 0;

    assert(buf);
    assert(item);

    if( item->error_code == 0 && item->data )
    {
        switch( item->kind )
        {
        case TORIRS_IOK_CACHE:
            if( cache_item_is_dat1(item) )
            {
                struct RSCache_Dat1DiskArchive const* a = item->data;
                v[0] = a->table_id;
                v[1] = a->archive_id;
                v[2] = a->revision;
                v[3] = a->file_count;
                v[4] = (int32_t)a->format;
                blob = a->data;
                blob_len = a->data_size;
            }
            else
            {
                struct RSCache_Dat2DiskArchive const* a = item->data;
                v[0] = a->table_id;
                v[1] = a->archive_id;
                v[2] = a->revision;
                v[3] = a->file_count;
                blob = a->data;
                blob_len = a->data_size;
                aux = a->file_ids;
                aux_count = a->file_ids ? a->file_count : 0;
            }
            break;
        case TORIRS_IOK_REFERENCE_TABLE:
            /* Deliberately NOT the decoded table: the reference table is a
             * pointer graph, and the client already links the decoder. What
             * crosses is the archive bytes it decodes from. The server hands
             * those over through the same field (see io_server_load). */
            blob = item->data;
            blob_len = item->data_size;
            break;
        case TORIRS_IOK_CONFIG_FILE:
        case TORIRS_IOK_SCRIPT:
            blob = item->data;
            blob_len = item->data_size;
            break;
        default:
            break;
        }
    }

    put_u32(buf, req_id);
    put_i32(buf, (int32_t)item->kind);
    put_i32(buf, item->kind == TORIRS_IOK_CACHE ? item->u.cache.flags : 0);
    put_i32(buf, item->error_code);
    for( int i = 0; i < 5; i++ )
        put_i32(buf, v[i]);
    put_blob(buf, blob, blob_len);
    put_u32(buf, (uint32_t)aux_count);
    for( int i = 0; i < aux_count; i++ )
        put_i32(buf, aux[i]);
    batch_bump(buf);
}

int
IOWire_ReadResponse(
    struct IOWireReader* r,
    struct IOWireResponse* out)
{
    assert(r);
    assert(out);
    memset(out, 0, sizeof(*out));

    out->req_id = get_u32(r);
    out->kind = get_i32(r);
    out->flags = get_i32(r);
    out->error = get_i32(r);
    for( int i = 0; i < 5; i++ )
        out->v[i] = get_i32(r);
    out->blob = get_blob(r, &out->blob_len);
    {
        uint32_t aux_count = get_u32(r);
        if( r->error )
            return -1;
        if( aux_count > (uint32_t)((r->len - r->off) / 4) )
        {
            r->error = 1;
            return -1;
        }
        out->aux_count = (int32_t)aux_count;
        out->aux = aux_count ? r->data + r->off : NULL;
        r->off += (int)aux_count * 4;
    }
    return r->error ? -1 : 0;
}

/* The aux array is int32 little-endian at an arbitrary offset in the received
 * buffer, so it is read a byte at a time rather than cast. */
static int32_t
aux_at(
    uint8_t const* aux,
    int index)
{
    uint8_t const* p = aux + (size_t)index * 4;
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static int
apply_cache_dat2(
    struct IOWireResponse const* resp,
    struct ToriRS_IOItem* item)
{
    struct RSCache_Dat2DiskArchive* archive = calloc(1, sizeof(*archive));
    assert(archive);

    archive->table_id = resp->v[0];
    archive->archive_id = resp->v[1];
    archive->revision = resp->v[2];
    archive->file_count = resp->v[3];
    archive->data_size = resp->blob_len;
    if( resp->blob_len > 0 )
    {
        archive->data = malloc((size_t)resp->blob_len);
        assert(archive->data);
        memcpy(archive->data, resp->blob, (size_t)resp->blob_len);
    }
    if( resp->aux_count > 0 )
    {
        archive->file_ids = malloc((size_t)resp->aux_count * sizeof(int));
        assert(archive->file_ids);
        for( int i = 0; i < resp->aux_count; i++ )
            archive->file_ids[i] = aux_at(resp->aux, i);
    }

    item->data = archive;
    item->data_size = (int)sizeof(struct RSCache_Dat2DiskArchive);
    return 0;

oom:
    RSCache_Dat2DiskArchiveFree(archive);
    return -1;
}

static int
apply_cache_dat1(
    struct IOWireResponse const* resp,
    struct ToriRS_IOItem* item)
{
    struct RSCache_Dat1DiskArchive* archive = calloc(1, sizeof(*archive));
    assert(archive);

    archive->table_id = resp->v[0];
    archive->archive_id = resp->v[1];
    archive->revision = resp->v[2];
    archive->file_count = resp->v[3];
    archive->format = (enum RSCache_ArchiveFormat)resp->v[4];
    archive->data_size = resp->blob_len;
    if( resp->blob_len > 0 )
    {
        archive->data = malloc((size_t)resp->blob_len);
        assert(archive->data);
        memcpy(archive->data, resp->blob, (size_t)resp->blob_len);
    }

    item->data = archive;
    item->data_size = (int)sizeof(struct RSCache_Dat1DiskArchive);
    return 0;
}

static int
apply_bytes(
    struct IOWireResponse const* resp,
    struct ToriRS_IOItem* item)
{
    void* data = NULL;
    if( resp->blob_len > 0 )
    {
        data = malloc((size_t)resp->blob_len);
        assert(data);
        memcpy(data, resp->blob, (size_t)resp->blob_len);
    }
    item->data = data;
    item->data_size = resp->blob_len;
    return 0;
}

static int
apply_reference_table(
    struct IOWireResponse const* resp,
    struct ToriRS_IOItem* item)
{
    struct RSCache_ReferenceTable* table;

    if( resp->blob_len <= 0 )
        return -1;
    /* Decode locally rather than over the wire: the table is a pointer graph
     * and the archive bytes are one contiguous run. */
    table = RSCache_ReferenceTableNewDecode((char*)(uintptr_t)resp->blob, resp->blob_len);
    if( !table )
        return -1;
    item->data = table;
    item->data_size = (int)sizeof(struct RSCache_ReferenceTable);
    return 0;
}

int
IOWire_ApplyResponse(
    struct IOWireResponse const* resp,
    struct ToriRS_IOItem* item)
{
    int rc = -1;

    assert(resp);
    assert(item);

    item->data = NULL;
    item->data_size = 0;
    item->error_code = 0;

    if( resp->error != 0 )
    {
        item->error_code = resp->error;
        return -1;
    }

    switch( (enum ToriRS_IOKind)resp->kind )
    {
    case TORIRS_IOK_CACHE:
        rc = (resp->flags == TORIRS_IO_CACHE_DAT1 ||
              resp->flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN ||
              resp->flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY)
                 ? apply_cache_dat1(resp, item)
                 : apply_cache_dat2(resp, item);
        break;
    case TORIRS_IOK_REFERENCE_TABLE:
        rc = apply_reference_table(resp, item);
        break;
    case TORIRS_IOK_CONFIG_FILE:
    case TORIRS_IOK_SCRIPT:
        rc = apply_bytes(resp, item);
        break;
    default:
        rc = -1;
        break;
    }

    if( rc != 0 )
    {
        item->data = NULL;
        item->data_size = 0;
        item->error_code = -1;
    }
    return rc;
}

void
IOWire_FreeLoadedItem(struct ToriRS_IOItem* item)
{
    assert(item);
    if( !item->data )
        return;

    switch( item->kind )
    {
    case TORIRS_IOK_CACHE:
        if( cache_item_is_dat1(item) )
            RSCache_Dat1DiskArchiveFree(item->data);
        else
            RSCache_Dat2DiskArchiveFree(item->data);
        break;
    case TORIRS_IOK_REFERENCE_TABLE:
        /* The server never decodes one (it forwards the archive bytes), so
         * item->data here is a plain byte run. See io_server_load. */
        free(item->data);
        break;
    default:
        free(item->data);
        break;
    }
    item->data = NULL;
    item->data_size = 0;
}

void
IOWire_DescribeItem(
    struct ToriRS_IOItem const* item,
    char* out,
    int out_size)
{
    assert(item);
    assert(out);
    switch( item->kind )
    {
    case TORIRS_IOK_CACHE:
        snprintf(
            out,
            (size_t)out_size,
            "%s table=%d archive=%d flags=%d",
            cache_item_is_dat1(item) ? "dat1" : "dat2",
            item->u.cache.table_id,
            item->u.cache.archive_id,
            item->u.cache.flags);
        break;
    case TORIRS_IOK_REFERENCE_TABLE:
        snprintf(out, (size_t)out_size, "reftable table=%d", item->u.reference_table.table_id);
        break;
    case TORIRS_IOK_CONFIG_FILE:
        snprintf(out, (size_t)out_size, "config %s", item->u.config_file.path);
        break;
    case TORIRS_IOK_SCRIPT:
        snprintf(out, (size_t)out_size, "script %s", item->u.script.path);
        break;
    default:
        snprintf(out, (size_t)out_size, "kind=%d", (int)item->kind);
        break;
    }
}
