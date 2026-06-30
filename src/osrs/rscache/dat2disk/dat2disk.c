#include "dat2disk.h"

#include "../shared/shared_archive.h"
#include "../shared/shared_archive_decompress.h"
// #include "dat2disk_inet.h"
#include "../disk/disk.h"
#include "../shared/shared_xtea_config.h"

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
RSCacheDat2Disk_IsValidTableId(int table_id)
{
    switch( table_id )
    {
    case RSCacheDat2Disk_Table_Animations:
    case RSCacheDat2Disk_Table_Skeletons:
    case RSCacheDat2Disk_Table_Configs:
    case RSCacheDat2Disk_Table_Interfaces:
    case RSCacheDat2Disk_Table_SoundEffects:
    case RSCacheDat2Disk_Table_Maps:
    case RSCacheDat2Disk_Table_MusicTracks:
    case RSCacheDat2Disk_Table_Models:
    case RSCacheDat2Disk_Table_Sprites:
    case RSCacheDat2Disk_Table_Textures:
    case RSCacheDat2Disk_Table_Binary:
    case RSCacheDat2Disk_Table_MusicJingles:
    case RSCacheDat2Disk_Table_Clientscript:
    case RSCacheDat2Disk_Table_Fonts:
    case RSCacheDat2Disk_Table_MusicSamples:
    case RSCacheDat2Disk_Table_MusicPatches:
    case RSCacheDat2Disk_Table_WorldmapGeography:
    case RSCacheDat2Disk_Table_Worldmap:
    case RSCacheDat2Disk_Table_WorldmapGround:
    case RSCacheDat2Disk_Table_DbtableIndex:
    case RSCacheDat2Disk_Table_Animayas:
    case RSCacheDat2Disk_Table_Gamevals:
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

    int const table_idx_count =
        (int)(sizeof(g_table_idx_files) / sizeof(g_table_idx_files[0]));
    for( int i = 0; i < table_idx_count; i++ )
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
init_reference_tables(struct RSCacheDat2Disk* cache)
{
    struct RSCacheDat2Disk_Archive* table_archive = NULL;
    struct RSCacheDat2Disk_ReferenceTable* table = NULL;
    for( int i = 0; i < RSCacheDat2Disk_Table_Count; ++i )
    {
        if( !RSCacheDat2Disk_IsValidTableId(i) )
            continue;

        table_archive = RSCacheDat2Disk_ArchiveNewReferenceTableLoad(cache, i);

        if( !table_archive )
        {
            printf("Failed to load referencetable %d\n", i);
            continue;
        }

        table = RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
        cache->tables[i] = table;
        table = NULL;

        RSCacheDat2Disk_ArchiveFree(table_archive);
        table_archive = NULL;
    }
}

// Load a single reference table on-demand (lazy loading)
static struct RSCacheDat2Disk_ReferenceTable*
cache_ensure_reference_table_loaded(
    struct RSCacheDat2Disk* cache,
    int table_id)
{
    // If already loaded, return it
    if( cache->tables[table_id] )
        return cache->tables[table_id];

    // Load it now
    printf("Lazy-loading reference table %d\n", table_id);
    struct RSCacheDat2Disk_Archive* table_archive = RSCacheDat2Disk_ArchiveNewReferenceTableLoad(cache, table_id);
    if( !table_archive )
    {
        printf("Failed to load reference table %d\n", table_id);
        return NULL;
    }

    struct RSCacheDat2Disk_ReferenceTable* table =
        RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    cache->tables[table_id] = table;

    RSCacheDat2Disk_ArchiveFree(table_archive);

    return table;
}

struct RSCacheDat2Disk*
RSCacheDat2Disk_NewFromDirectory(char const* directory)
{
    struct RSCacheDat2Disk* cache = malloc(sizeof(struct RSCacheDat2Disk));
    memset(cache, 0, sizeof(struct RSCacheDat2Disk));

    cache->mode = RSCacheDat2Disk_Mode_LocalOnly;
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

struct RSCacheDat2Disk*
RSCacheDat2Disk_NewInet(
    char const* directory,
    char const* ip,
    int port)
{
    (void)ip;
    (void)port;
    struct RSCacheDat2Disk* cache = malloc(sizeof(struct RSCacheDat2Disk));
    memset(cache, 0, sizeof(struct RSCacheDat2Disk));

    cache->mode = RSCacheDat2Disk_Mode_Inet;
    cache->directory = strdup(directory);

    init_dat2(cache->directory);
    init_files(cache->directory);

    cache->_dat2_file = fopen_dat2(cache->directory);
    if( !cache->_dat2_file )
    {
        printf("Failed to open dat2 file\n");
        goto error;
    }

    // cache->_inet_nullable = (void*)RSCacheDat2Disk_InetNewConnect(ip, port);
    // if( !cache->_inet_nullable )
    // {
    //     printf("Failed to connect to server\n");
    //     assert(false);
    //     return NULL;
    // }

    // For native builds: Load idx255 metadata upfront if needed
    (void)idx255_size_is_valid(cache->directory);

    // if( !is_valid )
    // {
    //     printf("idx255 file is not valid. Requesting from server.\n");
    //     struct RSCacheDat2Disk_InetPayload* payload = NULL;
    //     struct RSCacheDisk_IndexRecord index_record = { 0 };
    //     int sector_start;

    //     char path[1024];
    //     snprintf(path, sizeof(path), "%s/main_file_cache.idx255", cache->directory);
    //     FILE* idx255_file = fopen(path, "wb");
    //     if( !idx255_file )
    //     {
    //         printf("Failed to open idx255 file\n");
    //         goto error;
    //     }

    //     for( int i = 0; i < sizeof(g_table_idx_files) / sizeof(g_table_idx_files[0]); i++ )
    //     {
    //         printf("Requesting idx%d metadata from server\n", g_table_idx_files[i]);

    //         payload = RSCacheDat2Disk_InetPayloadNewArchiveRequest(
    //             (struct RSCacheDat2Disk_Inet*)cache->_inet_nullable, 255, g_table_idx_files[i]);

    //         if( !payload )
    //         {
    //             printf("Failed to request idx%d metadata from server\n", g_table_idx_files[i]);
    //             continue;
    //         }

    //         sector_start = RSCacheDisk_Dat2FileAppendArchive(
    //             cache->_dat2_file, 255, g_table_idx_files[i], payload->data, payload->data_size);

    //         index_record.idx_file_id = 255;
    //         index_record.archive_idx = g_table_idx_files[i];
    //         index_record.sector = sector_start;
    //         index_record.length = payload->data_size;

    //         RSCacheDisk_IndexFileWriteRecord(idx255_file, g_table_idx_files[i], &index_record);

    //         free(payload);
    //     }

    //     fclose(idx255_file);
    // }

    // For native builds, load all reference tables upfront
    init_reference_tables(cache);

    return cache;

error:;
    assert(false);
    return NULL;
}

struct RSCacheDat2Disk*
RSCacheDat2Disk_NewUninitialized(void)
{
    struct RSCacheDat2Disk* cache = malloc(sizeof(struct RSCacheDat2Disk));
    memset(cache, 0, sizeof(struct RSCacheDat2Disk));
    cache->mode = RSCacheDat2Disk_Mode_LocalOnly;
    return cache;
}

void
RSCacheDat2Disk_Free(struct RSCacheDat2Disk* cache)
{
    if( cache->_dat2_file )
        fclose(cache->_dat2_file);

    free((void*)cache->directory);
    for( int i = 0; i < RSCacheDat2Disk_Table_Count; ++i )
    {
        if( cache->tables[i] )
            RSCacheDat2Disk_ReferenceTableFree(cache->tables[i]);
    }

    free(cache);
}

static FILE*
dat2disk_fopen_index(
    char const* directory,
    int table_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, table_id);
    return fopen(path, "rb+");
}

static int
dat2disk_read_index(
    struct RSCacheDisk_IndexRecord* record,
    char const* cache_directory,
    int table_id,
    int entry_idx)
{
    FILE* index_file = dat2disk_fopen_index(cache_directory, table_id);
    if( !index_file )
        return -1;

    if( RSCacheDisk_IndexFileReadRecord(index_file, entry_idx, record) != 0 )
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

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewReferenceTableLoad(
    struct RSCacheDat2Disk* cache,
    int table_id)
{
    int res = 0;
    bool decompressed = false;
    char* dat2_data = NULL;
    struct RSCacheShared_ArchiveBuffer dat2_archive = { 0 };

    struct RSCacheDat2Disk_Archive* archive = malloc(sizeof(struct RSCacheDat2Disk_Archive));
    memset(archive, 0, sizeof(struct RSCacheDat2Disk_Archive));

    struct RSCacheDisk_IndexRecord index_record = { 0 };
    if( dat2disk_read_index(&index_record, cache->directory, 255, table_id) != 0 )
    {
        goto error;
    }

    res = RSCacheDisk_Dat2FileReadArchive(
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

    decompressed = RSCacheShared_ArchiveDecompress(&dat2_archive);
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

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewLoad(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id)
{
    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoadDecrypted(cache, table_id, archive_id, NULL);
    return archive;
}

void
RSCacheDat2Disk_ArchiveInitMetadataFromTable(
    struct RSCacheDat2Disk_ReferenceTable* table,
    struct RSCacheDat2Disk_Archive* archive)
{
    if( !table || !archive )
        return;

    assert(archive->archive_id < table->archive_count);
    struct RSCacheDat2Disk_ArchiveReference* archive_reference = &table->archives[archive->archive_id];
    archive->revision = archive_reference->version;
    archive->file_count = archive_reference->children.count;
}

void
RSCacheDat2Disk_ArchiveInitMetadata(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    struct RSCacheDat2Disk_ReferenceTable* table = cache_ensure_reference_table_loaded(cache, archive->table_id);
    if( !table )
    {
        printf("Failed to load reference table for table %d\n", archive->table_id);
        return;
    }

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(table, archive);
}

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewLoadDecrypted(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id,
    uint32_t* xtea_key_nullable)
{
    struct RSCacheShared_ArchiveBuffer dat2_archive = { 0 };
    struct RSCacheDat2Disk_Archive* archive = malloc(sizeof(struct RSCacheDat2Disk_Archive));
    memset(archive, 0, sizeof(struct RSCacheDat2Disk_Archive));

    (void)xtea_key_nullable;
    // 2. Consult the index for table_id. Table_id=2 is idx2
    //  - Read the entry "archive_id" in idx2. archive_id is the slot in the idx2 file..
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Use information from the reference table to load the files, etc.

    // TODO: Read archive_id or archive_slot?
    struct RSCacheDisk_IndexRecord index_record = { 0 };
    dat2disk_read_index(&index_record, cache->directory, table_id, archive_id);

    // // The archive is not loaded.
    // if( index_record.sector == 0 )
    // {
    //     if( cache->mode != RSCacheDat2Disk_Mode_Inet )
    //     {
    //         printf("Cache mode is not inet. Cannot request from server.\n");
    //         goto error;
    //     }

    //     payload = RSCacheDat2Disk_InetPayloadNewArchiveRequest(
    //         (struct RSCacheDat2Disk_Inet*)cache->_inet_nullable, table_id, archive_id);
    //     if( !payload )
    //     {
    //         printf("Failed to request archive from server\n");
    //         goto error;
    //     }

    //     int sector_start = RSCacheDisk_Dat2FileAppendArchive(
    //         cache->_dat2_file, table_id, archive_id, payload->data, payload->data_size);

    //     FILE* index_file = fopen_index(cache->directory, table_id);
    //     if( !index_file )
    //     {
    //         printf("Failed to open index file\n");
    //         goto error;
    //     }
    //     index_record.sector = sector_start;
    //     index_record.length = payload->data_size;
    //     index_record.idx_file_id = table_id;
    //     index_record.archive_idx = archive_id;
    //     RSCacheDisk_IndexFileWriteRecord(index_file, archive_id, &index_record);
    //     fclose(index_file);

    //     dat2disk_read_index(&index_record, cache->directory, table_id, archive_id);
    // }

    int res = RSCacheDisk_Dat2FileReadArchive(
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

    bool decompressed = RSCacheShared_ArchiveDecryptDecompress(&dat2_archive, xtea_key_nullable);
    if( !decompressed )
    {
        printf("Failed to decompress dat2 archive for table %d\n", table_id);
        goto error;
    }

    archive->data = dat2_archive.data;
    archive->data_size = dat2_archive.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;

    return archive;

error:
    if( dat2_archive.data )
        free(dat2_archive.data);
    free(archive);
    return NULL;
}

uint32_t*
RSCacheDat2Disk_ArchiveXteaKey(
    struct RSCacheDat2Disk* cache,
    int table_id,
    int archive_id)
{
    (void)cache;
    return (uint32_t*)RSCacheShared_XteaConfigFindKey(table_id, archive_id);
}

void
RSCacheDat2Disk_ArchiveFree(struct RSCacheDat2Disk_Archive* archive)
{
    if( archive->data )
        free(archive->data);
    free(archive);
}