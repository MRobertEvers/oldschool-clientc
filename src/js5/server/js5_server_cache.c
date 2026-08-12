#include "js5_server_cache.h"

#include <archive.h>
#include <checksum.h>
#include <dat2disk.h>
#include <reference_table.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define JS5_REFERENCE_INDEX 255
#define JS5_BLOCK_BYTES 512u
#define JS5_MASTER_ENTRY_BYTES 8u

/*
 * Enough of the cache's on-disk state to notice it changed.
 *
 * Everything this store answers with — the master index, the per-archive CRC
 * and version it validates against, the reference tables the disk decoded — is
 * a snapshot taken when the cache was opened. Repack the cache while the server
 * runs and that snapshot describes a file that no longer exists.
 *
 * The dat2 and idx255 are the right pair to watch. Every archive write appends
 * to the dat2, and every reference-table write touches idx255, so no change to
 * anything this store serves can miss both.
 */
struct Js5ServerCacheStamp
{
    int64_t dat2_size;
    int64_t dat2_mtime;
    int64_t idx255_size;
    int64_t idx255_mtime;
    bool valid;
};

struct Js5ServerCache
{
    struct RSCache_Dat2Disk* disk;
    uint8_t* master;
    size_t master_size;
    uint32_t reference_crc[RSCACHE_DAT2_DISK_TABLE_CAPACITY];
    uint32_t reference_version[RSCACHE_DAT2_DISK_TABLE_CAPACITY];
    bool reference_present[RSCACHE_DAT2_DISK_TABLE_CAPACITY];

    char directory[1024];
    struct Js5ServerCacheStamp stamp;
    /* Refresh attempts that failed. A cache being repacked looks corrupt while
     * the writer is mid-file, so a failed refresh keeps the old snapshot and is
     * retried; only the first is worth a message. */
    unsigned refresh_failures;
};

static uint32_t
js5_server_read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | (uint32_t)data[3];
}

static uint16_t
js5_server_read_u16(const uint8_t* data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | (uint16_t)data[1]);
}

static void
js5_server_stamp_file(
    const char* directory,
    const char* name,
    int64_t* size,
    int64_t* mtime)
{
    char path[1024];
    struct stat info;

    *size = -1;
    *mtime = -1;
    if( snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path) )
        return;
    if( stat(path, &info) != 0 )
        return;
    *size = (int64_t)info.st_size;
    *mtime = (int64_t)info.st_mtime;
}

static struct Js5ServerCacheStamp
js5_server_stamp(const char* directory)
{
    struct Js5ServerCacheStamp stamp;

    memset(&stamp, 0, sizeof(stamp));
    js5_server_stamp_file(
        directory, "main_file_cache.dat2", &stamp.dat2_size, &stamp.dat2_mtime);
    js5_server_stamp_file(
        directory, "main_file_cache.idx255", &stamp.idx255_size, &stamp.idx255_mtime);
    stamp.valid = stamp.dat2_size >= 0 && stamp.idx255_size >= 0;
    return stamp;
}

static bool
js5_server_stamp_equal(
    const struct Js5ServerCacheStamp* a,
    const struct Js5ServerCacheStamp* b)
{
    return a->valid && b->valid && a->dat2_size == b->dat2_size &&
           a->dat2_mtime == b->dat2_mtime && a->idx255_size == b->idx255_size &&
           a->idx255_mtime == b->idx255_mtime;
}

static bool
js5_server_index_present(
    const char* directory,
    int index)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, index);
    if( length < 0 || (size_t)length >= sizeof(path) )
        return false;
    FILE* file = fopen(path, "rb");
    if( !file )
        return false;
    fclose(file);
    return true;
}

static bool
js5_server_raw_container(
    struct RSCache_Dat2DiskArchive* archive,
    const uint8_t** data,
    size_t* size)
{
    size_t container_size;
    size_t trailer_size;

    if( !archive || !archive->data || archive->data_size <= 0 ||
        (uint8_t)archive->data[0] > 2u || !data || !size ||
        !RSCache_ArchiveRawContainerLength(
            (const uint8_t*)archive->data,
            (size_t)archive->data_size,
            &container_size) )
        return false;
    trailer_size = (size_t)archive->data_size - container_size;
    if( trailer_size != 0u && trailer_size != 2u && trailer_size != 4u )
        return false;
    *data = (const uint8_t*)archive->data;
    *size = container_size;
    return true;
}

static bool
js5_server_build_master(
    struct Js5ServerCache* cache,
    const char* directory)
{
    uint32_t crc[RSCACHE_DAT2_DISK_TABLE_CAPACITY] = { 0 };
    uint32_t version[RSCACHE_DAT2_DISK_TABLE_CAPACITY] = { 0 };
    int highest = -1;

    if( !js5_server_index_present(directory, JS5_REFERENCE_INDEX) )
    {
        fprintf(stderr, "js5_server: cache has no main_file_cache.idx255\n");
        return false;
    }

    for( int index = 0; index < RSCACHE_DAT2_DISK_TABLE_CAPACITY; index++ )
    {
        struct RSCache_Dat2DiskArchive* raw;
        const uint8_t* container;
        size_t container_size;

        if( !js5_server_index_present(directory, index) )
            continue;
        if( !cache->disk->tables[index] )
        {
            fprintf(stderr, "js5_server: idx%d has no valid reference table\n", index);
            return false;
        }
        raw = RSCache_Dat2DiskArchiveNewLoadRaw(cache->disk, JS5_REFERENCE_INDEX, index);
        if( !js5_server_raw_container(raw, &container, &container_size) )
        {
            fprintf(stderr, "js5_server: reference table %d is missing or malformed\n", index);
            RSCache_Dat2DiskArchiveFree(raw);
            return false;
        }
        crc[index] = RSCache_Crc32Buffer(container, container_size);
        version[index] = (uint32_t)cache->disk->tables[index]->version;
        cache->reference_crc[index] = crc[index];
        cache->reference_version[index] = version[index];
        cache->reference_present[index] = true;
        highest = index;
        RSCache_Dat2DiskArchiveFree(raw);
    }

    if( highest < 0 )
    {
        fprintf(stderr, "js5_server: cache contains no physical indices\n");
        return false;
    }

    size_t body_size = (size_t)(highest + 1) * JS5_MASTER_ENTRY_BYTES;
    cache->master_size = 5u + body_size;
    cache->master = (uint8_t*)malloc(cache->master_size);
    if( !cache->master )
        return false;

    uint8_t* out = cache->master;
    *out++ = 0u;
    *out++ = (uint8_t)(body_size >> 24u);
    *out++ = (uint8_t)(body_size >> 16u);
    *out++ = (uint8_t)(body_size >> 8u);
    *out++ = (uint8_t)body_size;
    for( int index = 0; index <= highest; index++ )
    {
        *out++ = (uint8_t)(crc[index] >> 24u);
        *out++ = (uint8_t)(crc[index] >> 16u);
        *out++ = (uint8_t)(crc[index] >> 8u);
        *out++ = (uint8_t)crc[index];
        *out++ = (uint8_t)(version[index] >> 24u);
        *out++ = (uint8_t)(version[index] >> 16u);
        *out++ = (uint8_t)(version[index] >> 8u);
        *out++ = (uint8_t)version[index];
    }
    return true;
}

static enum Js5ServerCacheResult
js5_server_validate_container(
    const struct Js5ServerCache* cache,
    uint8_t archive,
    uint16_t group,
    const struct RSCache_Dat2DiskArchive* raw,
    const uint8_t* container,
    size_t container_size)
{
    uint32_t expected_crc;
    uint32_t expected_version;
    size_t trailer_size;

    if( archive == JS5_REFERENCE_INDEX )
    {
        if( group >= RSCACHE_DAT2_DISK_TABLE_CAPACITY ||
            !cache->reference_present[group] )
            return JS5_SERVER_CACHE_NOT_FOUND;
        expected_crc = cache->reference_crc[group];
        expected_version = cache->reference_version[group];
    }
    else
    {
        const struct RSCache_ReferenceTable* table;
        const struct RSCache_ReferenceTableArchive* entry;

        if( archive >= RSCACHE_DAT2_DISK_TABLE_CAPACITY )
            return JS5_SERVER_CACHE_NOT_FOUND;
        table = cache->disk->tables[archive];
        if( !table || table->archive_count <= 0 ||
            group >= (uint32_t)table->archive_count )
            return JS5_SERVER_CACHE_NOT_FOUND;
        entry = &table->archives[group];
        if( entry->index < 0 )
            return JS5_SERVER_CACHE_NOT_FOUND;
        expected_crc = (uint32_t)entry->crc;
        expected_version = (uint32_t)entry->version;
    }

    if( RSCache_Crc32Buffer(container, container_size) != expected_crc )
        return JS5_SERVER_CACHE_ERROR;

    /* Some imported caches contain only the JS5 container, while native and
     * incrementally-built caches append a u16 or u32 local version. A present
     * trailer must agree with the authoritative reference metadata; an absent
     * trailer is still safe to serve because the container CRC is exact. */
    trailer_size = (size_t)raw->data_size - container_size;
    if( trailer_size == 4u &&
        js5_server_read_u32((const uint8_t*)raw->data + container_size) != expected_version )
        return JS5_SERVER_CACHE_ERROR;
    if( trailer_size == 2u &&
        js5_server_read_u16((const uint8_t*)raw->data + container_size) !=
            (uint16_t)expected_version )
        return JS5_SERVER_CACHE_ERROR;
    return JS5_SERVER_CACHE_OK;
}

struct Js5ServerCache*
Js5ServerCacheOpen(const char* cache_dir)
{
    struct Js5ServerCache* cache;

    if( !cache_dir || !cache_dir[0] )
        return NULL;
    cache = (struct Js5ServerCache*)calloc(1u, sizeof(*cache));
    if( !cache )
        return NULL;
    if( snprintf(cache->directory, sizeof(cache->directory), "%s", cache_dir) >=
        (int)sizeof(cache->directory) )
    {
        free(cache);
        return NULL;
    }
    /* Stamped before the read, so a cache rewritten *during* the open is
     * noticed by the next refresh rather than being mistaken for current. */
    cache->stamp = js5_server_stamp(cache_dir);
    cache->disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    if( !cache->disk || !js5_server_build_master(cache, cache_dir) )
    {
        Js5ServerCacheFree(cache);
        return NULL;
    }
    fprintf(
        stderr,
        "js5_server: validated %s, master=%zu bytes\n",
        cache_dir,
        cache->master_size);
    return cache;
}

int
Js5ServerCacheRefreshIfStale(struct Js5ServerCache* cache)
{
    struct Js5ServerCacheStamp stamp;
    struct Js5ServerCache probe;

    if( !cache )
        return 0;
    stamp = js5_server_stamp(cache->directory);
    if( js5_server_stamp_equal(&stamp, &cache->stamp) )
        return 0;

    /*
     * Rebuild into a scratch store first and swap only on success.
     *
     * A repack in progress genuinely looks corrupt — the writer is mid-file and
     * the reference tables do not yet describe what the dat2 holds — so a
     * failed rebuild must leave the working snapshot in place and be retried,
     * not take the server down. The stamp is deliberately NOT updated on
     * failure, which is what makes it retry.
     */
    memset(&probe, 0, sizeof(probe));
    snprintf(probe.directory, sizeof(probe.directory), "%s", cache->directory);
    probe.disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache->directory);
    if( !probe.disk || !js5_server_build_master(&probe, cache->directory) )
    {
        RSCache_Dat2DiskFree(probe.disk);
        free(probe.master);
        if( cache->refresh_failures++ == 0u )
            fprintf(
                stderr,
                "js5_server: %s changed but does not re-validate yet "
                "(a repack in progress?) — serving the previous contents\n",
                cache->directory);
        return -1;
    }

    /*
     * Swap in place: a Js5ServerSession holds this pointer for its lifetime,
     * so the store has to be updated rather than replaced. Blobs already handed
     * to a session are owned copies, so none of them dangle.
     */
    RSCache_Dat2DiskFree(cache->disk);
    free(cache->master);
    cache->disk = probe.disk;
    cache->master = probe.master;
    cache->master_size = probe.master_size;
    memcpy(cache->reference_crc, probe.reference_crc, sizeof(cache->reference_crc));
    memcpy(
        cache->reference_version, probe.reference_version, sizeof(cache->reference_version));
    memcpy(
        cache->reference_present, probe.reference_present, sizeof(cache->reference_present));
    cache->stamp = stamp;
    cache->refresh_failures = 0u;

    /*
     * Sessions mid-download are not told. They cannot be: JS5 has no message
     * for "the cache moved". A client holding the previous master will fail a
     * CRC on its next group, drop the connection and re-prime, which is exactly
     * the recovery path a corrupt local copy already takes.
     */
    fprintf(
        stderr,
        "js5_server: %s changed on disk — reloaded, master=%zu bytes\n",
        cache->directory,
        cache->master_size);
    return 1;
}

void
Js5ServerCacheFree(struct Js5ServerCache* cache)
{
    if( !cache )
        return;
    RSCache_Dat2DiskFree(cache->disk);
    free(cache->master);
    free(cache);
}

enum Js5ServerCacheResult
Js5ServerCacheLoad(
    struct Js5ServerCache* cache,
    uint8_t archive,
    uint16_t group,
    struct Js5ServerBlob* blob)
{
    struct RSCache_Dat2DiskArchive* raw;
    const uint8_t* container;
    size_t container_size;

    if( !cache || !blob )
        return JS5_SERVER_CACHE_ERROR;
    memset(blob, 0, sizeof(*blob));

    if( archive == JS5_REFERENCE_INDEX && group == JS5_REFERENCE_INDEX )
    {
        blob->data = (uint8_t*)malloc(cache->master_size);
        if( !blob->data )
            return JS5_SERVER_CACHE_ERROR;
        memcpy(blob->data, cache->master, cache->master_size);
        blob->size = cache->master_size;
        return JS5_SERVER_CACHE_OK;
    }

    raw = RSCache_Dat2DiskArchiveNewLoadRaw(cache->disk, archive, group);
    if( !raw )
        return JS5_SERVER_CACHE_NOT_FOUND;
    if( !js5_server_raw_container(raw, &container, &container_size) )
    {
        RSCache_Dat2DiskArchiveFree(raw);
        return JS5_SERVER_CACHE_ERROR;
    }
    enum Js5ServerCacheResult validation = js5_server_validate_container(
        cache, archive, group, raw, container, container_size);
    if( validation != JS5_SERVER_CACHE_OK )
    {
        RSCache_Dat2DiskArchiveFree(raw);
        return validation;
    }
    blob->data = (uint8_t*)malloc(container_size);
    if( !blob->data )
    {
        RSCache_Dat2DiskArchiveFree(raw);
        return JS5_SERVER_CACHE_ERROR;
    }
    memcpy(blob->data, container, container_size);
    blob->size = container_size;
    RSCache_Dat2DiskArchiveFree(raw);
    return JS5_SERVER_CACHE_OK;
}

void
Js5ServerBlobFree(struct Js5ServerBlob* blob)
{
    if( !blob )
        return;
    free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

enum Js5ServerCacheResult
Js5ServerCacheBuildResponse(
    struct Js5ServerCache* cache,
    uint8_t archive,
    uint16_t group,
    uint8_t** out,
    size_t* out_size)
{
    struct Js5ServerBlob blob;
    enum Js5ServerCacheResult result;
    size_t markers;
    size_t capacity;
    size_t position = 0u;
    size_t offset = 0u;

    if( !out || !out_size )
        return JS5_SERVER_CACHE_ERROR;
    *out = NULL;
    *out_size = 0u;
    result = Js5ServerCacheLoad(cache, archive, group, &blob);
    if( result != JS5_SERVER_CACHE_OK )
        return result;

    markers = blob.size > 509u ? (blob.size - 509u + 510u) / 511u : 0u;
    if( blob.size > SIZE_MAX - 3u - markers )
    {
        Js5ServerBlobFree(&blob);
        return JS5_SERVER_CACHE_ERROR;
    }
    capacity = 3u + blob.size + markers;
    *out = (uint8_t*)malloc(capacity);
    if( !*out )
    {
        Js5ServerBlobFree(&blob);
        return JS5_SERVER_CACHE_ERROR;
    }

    (*out)[position++] = archive;
    (*out)[position++] = (uint8_t)(group >> 8u);
    (*out)[position++] = (uint8_t)group;
    while( offset < blob.size )
    {
        size_t block_remaining = JS5_BLOCK_BYTES - (position % JS5_BLOCK_BYTES);
        if( block_remaining == JS5_BLOCK_BYTES && position > 0u )
        {
            (*out)[position++] = 0xffu;
            block_remaining--;
        }
        size_t take = blob.size - offset;
        if( take > block_remaining )
            take = block_remaining;
        memcpy(*out + position, blob.data + offset, take);
        position += take;
        offset += take;
    }
    Js5ServerBlobFree(&blob);
    *out_size = position;
    return JS5_SERVER_CACHE_OK;
}

size_t
Js5ServerCacheMasterSize(const struct Js5ServerCache* cache)
{
    return cache ? cache->master_size : 0u;
}
