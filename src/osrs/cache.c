#include "cache.h"

#include "archive.h"
#include "archive_decompress.h"
#include "cache_inet.h"
#include "disk.h"
#include "rsbuf.h"
#include "xtea_config.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 520
#define INDEX_ENTRY_SIZE 6
#define CACHE_FILE_NAME_ROOT "main_file_cache"

static char g_sector_data[SECTOR_SIZE];

static FILE*
fopen_dat2(char const* cache_directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.dat2", cache_directory, CACHE_FILE_NAME_ROOT);
    return fopen(path, "rb+");
}

bool
cache_is_valid_table_id(int table_id)
{
    switch( table_id )
    {
    case CACHE_ANIMATIONS:
    case CACHE_SKELETONS:
    case CACHE_CONFIGS:
    case CACHE_INTERFACES:
    case CACHE_SOUNDEFFECTS:
    case CACHE_MAPS:
    case CACHE_MUSIC_TRACKS:
    case CACHE_MODELS:
    case CACHE_SPRITES:
    case CACHE_TEXTURES:
    case CACHE_BINARY:
    case CACHE_MUSIC_JINGLES:
    case CACHE_CLIENTSCRIPT:
    case CACHE_FONTS:
    case CACHE_MUSIC_SAMPLES:
    case CACHE_MUSIC_PATCHES:
    case CACHE_WORLDMAP_GEOGRAPHY:
    case CACHE_WORLDMAP:
    case CACHE_WORLDMAP_GROUND:
    case CACHE_DBTABLEINDEX:
    case CACHE_ANIMAYAS:
    case CACHE_GAMEVALS:
        return true;
    default:
        return false;
    }
}

static int const g_table_idx_files[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                         12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 24 };

static void
init_dat2(char const* directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        printf("Failed to open dat2 file. Creating new one.\n");
        file = fopen(path, "wb");
        if( !file )
        {
            printf("Failed to create dat2 file\n");
            return;
        }
        // 0 page
        fseek(file, 0, SEEK_SET);
        fwrite(g_sector_data, 1, SECTOR_SIZE, file);
    }

    fclose(file);
}

static void
init_files(char const* directory)
{
    char path[1024];
    FILE* file = NULL;

    for( int i = 0; i < sizeof(g_table_idx_files) / sizeof(g_table_idx_files[0]); i++ )
    {
        snprintf(
            path,
            sizeof(path),
            "%s/%s.idx%d",
            directory,
            CACHE_FILE_NAME_ROOT,
            g_table_idx_files[i]);
        file = fopen(path, "rb");
        if( !file )
        {
            file = fopen(path, "wb");
            assert(file);
        }
        else
            printf("File %s already exists, skipping\n", path);

        fclose(file);
    }
}

static void
init_reference_tables(struct Cache* cache)
{
    struct CacheArchive* table_archive = NULL;
    struct ReferenceTable* table = NULL;
    for( int i = 0; i < CACHE_TABLE_COUNT; ++i )
    {
        if( !cache_is_valid_table_id(i) )
            continue;

        table_archive = cache_archive_new_reference_table_load(cache, i);

        if( !table_archive )
        {
            printf("Failed to load referencetable %d\n", i);
            continue;
        }

        table = reference_table_new_decode(table_archive->data, table_archive->data_size);
        cache->tables[i] = table;
        table = NULL;

        cache_archive_free(table_archive);
        table_archive = NULL;
    }
}

// Load a single reference table on-demand (lazy loading)
static struct ReferenceTable*
cache_ensure_reference_table_loaded(struct Cache* cache, int table_id)
{
    // If already loaded, return it
    if( cache->tables[table_id] )
        return cache->tables[table_id];

    // Load it now
    printf("Lazy-loading reference table %d\n", table_id);
    struct CacheArchive* table_archive = cache_archive_new_reference_table_load(cache, table_id);
    if( !table_archive )
    {
        printf("Failed to load reference table %d\n", table_id);
        return NULL;
    }

    struct ReferenceTable* table =
        reference_table_new_decode(table_archive->data, table_archive->data_size);
    cache->tables[table_id] = table;

    cache_archive_free(table_archive);

    return table;
}

struct Cache*
cache_new_from_directory(char const* directory)
{
    struct Cache* cache = malloc(sizeof(struct Cache));
    memset(cache, 0, sizeof(struct Cache));

    cache->mode = CACHE_MODE_LOCAL_ONLY;
    cache->directory = strdup(directory);

    cache->_dat2_file = fopen_dat2(cache->directory);
    if( !cache->_dat2_file )
    {
        printf("Failed to open dat2 file\n");
        goto error;
    }

    init_reference_tables(cache);

    return cache;

error:
    if( cache )
        free(cache);
    return NULL;
}

static bool
idx255_size_is_valid(char const* directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.idx255", directory);
    FILE* file = fopen(path, "rb");
    if( !file )
        return false;
    fseek(file, 0, SEEK_END);
    bool is_valid = ftell(file) > INDEX_ENTRY_SIZE;
    fclose(file);
    return is_valid;
}

struct Cache*
cache_new_inet(char const* directory, char const* ip, int port)
{
    struct Cache* cache = malloc(sizeof(struct Cache));
    memset(cache, 0, sizeof(struct Cache));

    cache->mode = CACHE_MODE_INET;
    cache->directory = strdup(directory);

    init_dat2(cache->directory);
    init_files(cache->directory);

    cache->_dat2_file = fopen_dat2(cache->directory);
    if( !cache->_dat2_file )
    {
        printf("Failed to open dat2 file\n");
        goto error;
    }

    cache->_inet_nullable = (void*)cache_inet_new_connect(ip, port);
    if( !cache->_inet_nullable )
    {
        printf("Failed to connect to server\n");
        assert(false);
        return NULL;
    }

    // For native builds: Load idx255 metadata upfront if needed
    int is_valid = idx255_size_is_valid(cache->directory);

    if( !is_valid )
    {
        printf("idx255 file is not valid. Requesting from server.\n");
        struct CacheInetPayload* payload = NULL;
        struct IndexRecord index_record = { 0 };
        int sector_start;

        char path[1024];
        snprintf(path, sizeof(path), "%s/main_file_cache.idx255", cache->directory);
        FILE* idx255_file = fopen(path, "wb");
        if( !idx255_file )
        {
            printf("Failed to open idx255 file\n");
            goto error;
        }

        for( int i = 0; i < sizeof(g_table_idx_files) / sizeof(g_table_idx_files[0]); i++ )
        {
            printf("Requesting idx%d metadata from server\n", g_table_idx_files[i]);

            payload = cache_inet_payload_new_archive_request(
                (struct CacheInet*)cache->_inet_nullable, 255, g_table_idx_files[i]);

            if( !payload )
            {
                printf("Failed to request idx%d metadata from server\n", g_table_idx_files[i]);
                continue;
            }

            sector_start = disk_dat2file_append_archive(
                cache->_dat2_file, 255, g_table_idx_files[i], payload->data, payload->data_size);

            index_record.idx_file_id = 255;
            index_record.archive_idx = g_table_idx_files[i];
            index_record.sector = sector_start;
            index_record.length = payload->data_size;

            disk_indexfile_write_record(idx255_file, g_table_idx_files[i], &index_record);

            free(payload);
        }

        fclose(idx255_file);
    }

    // For native builds, load all reference tables upfront
    init_reference_tables(cache);

    return cache;

error:;
    assert(false);
    return NULL;
}

struct Cache*
cache_new_uninitialized(void)
{
    struct Cache* cache = malloc(sizeof(struct Cache));
    memset(cache, 0, sizeof(struct Cache));
    cache->mode = CACHE_MODE_LOCAL_ONLY;
    return cache;
}

void
cache_free(struct Cache* cache)
{
    if( cache->_dat2_file )
        fclose(cache->_dat2_file);

    free(cache->directory);
    for( int i = 0; i < CACHE_TABLE_COUNT; ++i )
    {
        if( cache->tables[i] )
            reference_table_free(cache->tables[i]);
    }

    free(cache);
}

static FILE*
fopen_index(char const* directory, int table_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, table_id);
    return fopen(path, "rb+");
}

static int
read_index(struct IndexRecord* record, char const* cache_directory, int table_id, int entry_idx)
{
    FILE* index_file = fopen_index(cache_directory, table_id);
    if( !index_file )
        return -1;

    if( disk_indexfile_read_record(index_file, entry_idx, record) != 0 )
    {
        goto error;
    }

    record->idx_file_id = table_id;

    fclose(index_file);
    return 0;

error:;
    fclose(index_file);
    return -1;
}

enum CachePreloadKind
cache_archive_preload_check(struct Cache* cache, int table_id, int archive_id)
{
    struct IndexRecord index_record = { 0 };
    if( read_index(&index_record, cache->directory, table_id, archive_id) != 0 )
    {
        return CACHE_PRELOAD_FAILED;
    }

    if( index_record.sector == 0 )
        return CACHE_PRELOAD_NEEDLOAD;
    else
        return CACHE_PRELOAD_READY;
}

struct CacheArchive*
cache_archive_new_reference_table_load(struct Cache* cache, int table_id)
{
    int res = 0;
    bool decompressed = false;
    char* dat2_data = NULL;
    struct Dat2Archive dat2_archive = { 0 };

    struct CacheArchive* archive = malloc(sizeof(struct CacheArchive));
    memset(archive, 0, sizeof(struct CacheArchive));

    struct IndexRecord index_record = { 0 };
    if( read_index(&index_record, cache->directory, 255, table_id) != 0 )
    {
        goto error;
    }

    res = disk_dat2file_read_archive(
        cache->_dat2_file,
        index_record.idx_file_id,
        index_record.archive_idx,
        index_record.sector,
        index_record.length,
        &dat2_archive);

    if( res != 0 )
    {
        printf("Failed to read dat2 archive for table %d\n", table_id);
        goto error;
    }

    decompressed = archive_decompress(&dat2_archive);
    if( !decompressed )
    {
        printf("Failed to decompress dat2 archive for table %d\n", table_id);
        goto error;
    }

    archive->data = dat2_archive.data;
    archive->data_size = dat2_archive.data_size;
    archive->archive_id = index_record.archive_idx;
    archive->table_id = table_id;
    archive->file_count = -1;

    return archive;

error:
    if( dat2_archive.data )
        free(dat2_archive.data);
    if( dat2_data )
        free(dat2_data);
    free(archive);
    return NULL;
}

struct CacheArchive*
cache_archive_new_load(struct Cache* cache, int table_id, int archive_id)
{
    return cache_archive_new_load_decrypted(cache, table_id, archive_id, NULL);
}

struct CacheArchive*
cache_archive_new_load_decrypted(
    struct Cache* cache, int table_id, int archive_id, int32_t* xtea_key_nullable)
{
    struct CacheArchive* archive = malloc(sizeof(struct CacheArchive));
    memset(archive, 0, sizeof(struct CacheArchive));

    // 2. Consult the index for table_id. Table_id=2 is idx2
    //  - Read the entry "archive_id" in idx2. archive_id is the slot in the idx2 file..
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Use information from the reference table to load the files, etc.

    // TODO: Read archive_id or archive_slot?
    struct IndexRecord index_record = { 0 };
    read_index(&index_record, cache->directory, table_id, archive_id);

    // The archive is not loaded.
    if( index_record.sector == 0 )
    {
        if( cache->mode != CACHE_MODE_INET )
        {
            printf("Cache mode is not inet. Cannot request from server.\n");
            goto error;
        }

        struct CacheInetPayload* payload = cache_inet_payload_new_archive_request(
            (struct CacheInet*)cache->_inet_nullable, table_id, archive_id);
        if( !payload )
        {
            printf("Failed to request archive from server\n");
            goto error;
        }

        int sector_start = disk_dat2file_append_archive(
            cache->_dat2_file, table_id, archive_id, payload->data, payload->data_size);

        FILE* index_file = fopen_index(cache->directory, table_id);
        if( !index_file )
        {
            printf("Failed to open index file\n");
            goto error;
        }
        index_record.sector = sector_start;
        index_record.length = payload->data_size;
        index_record.idx_file_id = table_id;
        index_record.archive_idx = archive_id;
        disk_indexfile_write_record(index_file, archive_id, &index_record);
        fclose(index_file);

        read_index(&index_record, cache->directory, table_id, archive_id);
    }

    struct Dat2Archive dat2_archive = { 0 };
    int res = disk_dat2file_read_archive(
        cache->_dat2_file,
        index_record.idx_file_id,
        index_record.archive_idx,
        index_record.sector,
        index_record.length,
        &dat2_archive);
    if( res != 0 )
    {
        printf("Failed to read dat2 archive for table %d\n", table_id);
        goto error;
    }

    bool decompressed = archive_decrypt_decompress(&dat2_archive, xtea_key_nullable);
    if( !decompressed )
    {
        printf("Failed to decompress dat2 archive for table %d\n", table_id);
        goto error;
    }

    archive->data = dat2_archive.data;
    archive->data_size = dat2_archive.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;

    // 1. Consult the reference table for table_id.
    //  - Read the entry "table_id" in idx255
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Load the ReferenceTableEntry in the reference table specified by archive_id and file_id.
    //  - The "archive_id" is the "ArchiveReference" slot, and the "file_id" is the "FileReference"
    //    = This gives 1. the Number of files in the archive... etc.

    // Get reference table (load on-demand if not yet loaded)
    struct ReferenceTable* table = cache_ensure_reference_table_loaded(cache, table_id);
    if( !table )
    {
        printf("Failed to load reference table for table %d\n", table_id);
        goto error;
    }

    assert(archive_id < table->archive_count);

    struct ArchiveReference* archive_reference = &table->archives[archive_id];
    if( archive_reference->index != archive_id )
    {
        printf("Archive reference not found for table %d, archive %d\n", table_id, archive_id);
        goto error;
    }

    archive->revision = archive_reference->version;
    archive->file_count = archive_reference->children.count;

    return archive;

error:
    if( dat2_archive.data )
        free(dat2_archive.data);
    free(archive);
    return NULL;
}

uint32_t*
cache_archive_xtea_key(struct Cache* cache, int table_id, int archive_id)
{
    return xtea_config_find_key(table_id, archive_id);
}

void
cache_archive_free(struct CacheArchive* archive)
{
    if( archive->data )
        free(archive->data);
    free(archive);
}