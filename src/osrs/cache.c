#include "cache.h"

#include "archive.h"
#include "archive_decompress.h"
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

static FILE*
fopen_dat2(char const* cache_directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", cache_directory);
    return fopen(path, "rb");
}

static char*
load_dat2_memory(char const* cache_directory, int* out_size)
{
    char* data = NULL;
    int data_size = 0;

    FILE* dat2_file = fopen_dat2(cache_directory);
    if( !dat2_file )
        return NULL;

    fseek(dat2_file, 0, SEEK_END);
    data_size = ftell(dat2_file);
    fseek(dat2_file, 0, SEEK_SET);

    data = malloc(data_size);
    fread(data, data_size, 1, dat2_file);
    *out_size = data_size;
    fclose(dat2_file);

    return data;
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

struct Cache*
cache_new_from_directory(char const* directory)
{
    struct Cache* cache = malloc(sizeof(struct Cache));
    memset(cache, 0, sizeof(struct Cache));

    cache->directory = strdup(directory);

    // cache->_dat2 = load_dat2_memory(cache->directory, &cache->_dat2_size);

    cache->_dat2_file = fopen_dat2(cache->directory);
    if( !cache->_dat2_file )
    {
        printf("Failed to open dat2 file\n");
        goto error;
    }

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
            goto error;
        }

        table = reference_table_new_decode(table_archive->data, table_archive->data_size);
        cache->tables[i] = table;
        table = NULL;

        cache_archive_free(table_archive);
        table_archive = NULL;
    }

    return cache;
error:
    if( table_archive )
        cache_archive_free(table_archive);
    if( table )
        reference_table_free(table);
    if( cache )
        free(cache);
    return NULL;
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

    if( cache->_dat2 )
        free(cache->_dat2);

    free(cache);
}

struct IndexRecord
{
    // This is a sanity check. I.e. if you run off the end of the index's dat2 file,
    // This should match the "table_id" to which the record belongs.
    // i.e. idx2 is table_id 2.
    int idx_file_id;
    int archive_idx;
    int sector;
    // length in bytes as it's stored on disk in the dat2 file.
    int length;
};

static FILE*
fopen_index(char const* directory, int table_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, table_id);
    return fopen(path, "rb");
}

static int
read_index(struct IndexRecord* record, char const* cache_directory, int table_id, int entry_idx)
{
    char data[INDEX_ENTRY_SIZE] = { 0 };

    FILE* index_file = fopen_index(cache_directory, table_id);
    if( !index_file )
        return -1;

    fseek(index_file, entry_idx * INDEX_ENTRY_SIZE, SEEK_SET);
    fread(data, INDEX_ENTRY_SIZE, 1, index_file);

    fclose(index_file);

    // 	int length = ((buffer[0] & 0xFF) << 16) | ((buffer[1] & 0xFF) << 8) | (buffer[2] & 0xFF);
    // int sector = ((buffer[3] & 0xFF) << 16) | ((buffer[4] & 0xFF) << 8) | (buffer[5] & 0xFF);

    // Convert Java:
    // int length = ((buffer[0] & 0xFF) << 16) | ((buffer[1] & 0xFF) << 8) | (buffer[2] & 0xFF);
    // int sector = ((buffer[3] & 0xFF) << 16) | ((buffer[4] & 0xFF) << 8) | (buffer[5] & 0xFF);

    // Read 3 bytes for length and 3 bytes for sector
    // Need to mask with 0xFF to handle sign extension when converting to int
    int length = ((data[0] & 0xFF) << 16) | ((data[1] & 0xFF) << 8) | (data[2] & 0xFF);

    int sector = ((data[3] & 0xFF) << 16) | ((data[4] & 0xFF) << 8) | (data[5] & 0xFF);

    if( length <= 0 || sector <= 0 )
        return -1;

    record->length = length;
    record->sector = sector;
    record->archive_idx = entry_idx;
    record->idx_file_id = table_id;

    return 0;
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
        printf("Failed to read index record for table %d\n", table_id);
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

    // 1. Consult the reference table for table_id.
    //  - Read the entry "table_id" in idx255
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Load the ReferenceTableEntry in the reference table specified by archive_id and file_id.
    //  - The "archive_id" is the "ArchiveReference" slot, and the "file_id" is the "FileReference"
    //    = This gives 1. the Number of files in the archive... etc.

    // The reference tables were loaded when the cache was created.
    struct ReferenceTable* table = cache->tables[table_id];

    assert(archive_id < table->archive_count);

    // int archive_slot = -1;
    // for( int i = 0; i < table->id_count; ++i )
    // {
    //     int id = table->ids[i];
    //     if( table->archives[id].index == archive_id )
    //     {
    //         archive_slot = id;
    //         break;
    //     }
    // }

    // assert(archive_slot == archive_id);

    struct ArchiveReference* archive_reference = &table->archives[archive_id];
    if( archive_reference->index != archive_id )
    {
        printf("Archive reference not found for table %d, archive %d\n", table_id, archive_id);
        goto error;
    }

    // 2. Consult the index for table_id. Table_id=2 is idx2
    //  - Read the entry "archive_id" in idx2. archive_id is the slot in the idx2 file..
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Use information from the reference table to load the files, etc.

    // TODO: Read archive_id or archive_slot?
    struct IndexRecord index_record = { 0 };
    if( read_index(&index_record, cache->directory, table_id, archive_id) != 0 )
    {
        printf("Failed to read index record for table %d\n", table_id);
        goto error;
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