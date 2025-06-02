#include "cache.h"

#include "archive.h"
#include "archive_decompress.h"
#include "buffer.h"
#include "xtea_config.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 520
#define INDEX_ENTRY_SIZE 6

static char*
load_dat2_memory(char const* cache_directory, int* out_size)
{
    char* data = NULL;
    int data_size = 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", cache_directory);
    FILE* dat2_file = fopen(path, "rb");
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

/**
 * @brief In the dat2 file, the records are archives.
 *
 * @param data
 * @param data_size
 * @param idx_file_id The idx file that contains the record. I.e. The block of data knows what index
 * file indexes it. This should be the idx file id of the index file.
 * @param record_id
 * @param sector
 * @param length
 */
static int
read_dat2(
    struct Dat2Archive* archive,
    char* data,
    int data_size,
    int idx_file_id,
    int archive_id,
    int sector,
    int length)
{
    struct Buffer data_buffer = { .data = data, .position = 0, .data_size = data_size };

    char read_buffer[SECTOR_SIZE];
    int read_buffer_len = 0;

    int header_size;
    int data_block_size;
    int current_archive;
    int current_part;
    int next_sector;
    int current_index = 0;

    if( sector <= 0L || data_size / SECTOR_SIZE < (long)sector )
    {
        printf("bad read, dat length %d, requested sector %d", data_size, sector);
        return -1;
    }

    int out_len = 0;
    char* out = malloc(length);
    memset(out, 0, length);

    for( int part = 0, read_bytes_count = 0; length > read_bytes_count; sector = next_sector )
    {
        if( sector == 0 )
        {
            printf("Unexpected end of file\n");
            return -1;
        }
        data_buffer.position = sector * SECTOR_SIZE;

        data_block_size = length - read_bytes_count;
        if( archive_id > 0xFFFF )
        {
            header_size = 10;
            if( data_block_size > SECTOR_SIZE - header_size )
                data_block_size = SECTOR_SIZE - header_size;

            int bytes_read = readto(
                read_buffer, sizeof(read_buffer), header_size + data_block_size, &data_buffer);
            if( bytes_read < header_size + data_block_size )
            {
                printf("short read when reading file data for %d/%d\n", archive_id, current_index);
                return -1;
            }

            read_buffer_len = bytes_read;

            current_archive = ((read_buffer[0] & 0xFF) << 24) | ((read_buffer[1] & 0xFF) << 16) |
                              ((read_buffer[2] & 0xFF) << 8) | (read_buffer[3] & 0xFF);
            current_part = ((read_buffer[4] & 0xFF) << 8) + (read_buffer[5] & 0xFF);
            next_sector = ((read_buffer[6] & 0xFF) << 16) | ((read_buffer[7] & 0xFF) << 8) |
                          (read_buffer[8] & 0xFF);
            current_index = read_buffer[9] & 0xFF;
        }
        else
        {
            header_size = 8;
            if( data_block_size > SECTOR_SIZE - header_size )
                data_block_size = SECTOR_SIZE - header_size;

            int bytes_read = readto(
                read_buffer, sizeof(read_buffer), header_size + data_block_size, &data_buffer);
            if( bytes_read < header_size + data_block_size )
            {
                printf("short read when reading file data for %d/%d\n", archive_id, current_index);
                return -1;
            }

            read_buffer_len = bytes_read;

            current_archive = ((read_buffer[0] & 0xFF) << 8) | (read_buffer[1] & 0xFF);
            current_part = ((read_buffer[2] & 0xFF) << 8) | (read_buffer[3] & 0xFF);
            next_sector = ((read_buffer[4] & 0xFF) << 16) | ((read_buffer[5] & 0xFF) << 8) |
                          (read_buffer[6] & 0xFF);
            current_index = read_buffer[7] & 0xFF;
        }

        if( archive_id != current_archive || current_part != part || idx_file_id != current_index )
        {
            printf(
                "data mismatch %d != %d, %d != %d, %d != %d\n",
                archive_id,
                current_archive,
                part,
                current_part,
                idx_file_id,
                current_index);
            return -1;
        }

        if( next_sector < 0 || data_size / SECTOR_SIZE < (long)next_sector )
        {
            printf("invalid next sector");
            return -1;
        }

        memcpy(out + out_len, read_buffer + header_size, data_block_size);
        out_len += data_block_size;

        read_bytes_count += data_block_size;

        ++part;
    }

    archive->data = out;
    archive->data_size = out_len;
    archive->archive_id = archive_id;

    return 0;
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

    cache->_dat2 = load_dat2_memory(cache->directory, &cache->_dat2_size);

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
    free(cache->directory);
    for( int i = 0; i < CACHE_TABLE_COUNT; ++i )
    {
        if( cache->tables[i] )
            reference_table_free(cache->tables[i]);
    }
    free(cache);
}

struct IndexRecord
{
    int idx_file_id;
    int archive_idx;
    int sector;
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

    res = read_dat2(
        &dat2_archive,
        cache->_dat2,
        cache->_dat2_size,
        index_record.idx_file_id,
        index_record.archive_idx,
        index_record.sector,
        index_record.length);

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

    int archive_slot = -1;
    for( int i = 0; i < table->id_count; ++i )
    {
        int id = table->ids[i];
        if( table->archives[id].index == archive_id )
        {
            archive_slot = id;
            break;
        }
    }

    struct ArchiveReference* archive_reference = &table->archives[archive_slot];
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
    int res = read_dat2(
        &dat2_archive,
        cache->_dat2,
        cache->_dat2_size,
        index_record.idx_file_id,
        index_record.archive_idx,
        index_record.sector,
        index_record.length);
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