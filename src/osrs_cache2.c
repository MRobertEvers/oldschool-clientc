#include "osrs_cache.h"

#include <bzlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache"

// public byte[] data;
// public int fileCount;
// public int[] fileHash;
// public int[] fileSizeInflated;
// public int[] fileSizeDeflated;
// public int[] fileOffset;
// public boolean unpacked;
struct FileArchive
{
    char* data;
    int file_count;
    int* file_name_hash;
    int* file_size_inflated;
    int* file_size_deflated;
    int* file_offset;
    bool unpacked;
};

struct Buffer
{
    char* data;
    int position;
    int data_size;
};

static int
read_24(char* data, int data_size, struct Buffer* buffer)
{
    buffer->position += 3;
    return (data[buffer->position - 3] << 16) | (data[buffer->position - 2] << 8) |
           data[buffer->position - 1];
}

static int
read_16(char* data, int data_size, struct Buffer* buffer)
{
    buffer->position += 2;
    return (data[buffer->position - 2] << 8) | data[buffer->position - 1];
}

static int
read_32(char* data, int data_size, struct Buffer* buffer)
{
    buffer->position += 4;
    return (data[buffer->position - 4] << 24) | (data[buffer->position - 3] << 16) |
           (data[buffer->position - 2] << 8) | data[buffer->position - 1];
}

static int
readto(char* out, int out_size, int len, struct Buffer* buffer)
{
    int bytes_read = 0;
    while( len > 0 && buffer->position < buffer->data_size )
    {
        out[bytes_read] = buffer->data[buffer->position];
        len--;
        buffer->position++;
        bytes_read++;
    }

    return bytes_read;
}

static void
bump(int count, struct Buffer* buffer)
{
    buffer->position += count;
}

/**
 *
 *     int unpackedSize = buffer.read24();
        int packedSize = buffer.read24();

        if (packedSize != unpackedSize) {
            data = new byte[unpackedSize];
            BZip2.decompress(src, 6, packedSize, data);
            buffer = new Buffer(data);
            unpacked = true;
        } else {
            data = src;
            unpacked = false;
        }

        fileCount = buffer.readU16();
        fileHash = new int[fileCount];
        fileSizeInflated = new int[fileCount];
        fileSizeDeflated = new int[fileCount];
        fileOffset = new int[fileCount];

        int offset = buffer.position + (fileCount * 10);

        for (int file = 0; file < fileCount; file++) {
            fileHash[file] = buffer.read32();
            fileSizeInflated[file] = buffer.read24();
            fileSizeDeflated[file] = buffer.read24();
            fileOffset[file] = offset;
            offset += fileSizeDeflated[file];
        }
 *
 * @param data
 * @param data_size
 * @param file_archive
 */
void
parse_file_archive(char* data, int data_size, struct FileArchive* file_archive)
{
    struct Buffer buffer = { .data = data, .position = 0 };
    int unpacked_size = read_24(data, data_size, &buffer);
    int packed_size = read_24(data, data_size, &buffer);

    if( packed_size != unpacked_size )
    {
        file_archive->data = malloc(unpacked_size);
        int result = BZ2_bzBuffToBuffDecompress(
            file_archive->data, &unpacked_size, data + 6, packed_size, 0, 0);
        if( result != BZ_OK )
        {
            printf("Error decompressing file archive: %d\n", result);
            return;
        }
        buffer.data = file_archive->data;
        buffer.position = 0;

        file_archive->unpacked = true;
    }
    else
    {
        file_archive->data = malloc(data_size);
        memcpy(file_archive->data, data, data_size);

        file_archive->unpacked = false;
    }

    int file_count = read_16(file_archive->data, unpacked_size, &buffer);

    file_archive->file_count = file_count;
    file_archive->file_name_hash = malloc(file_archive->file_count * sizeof(int));
    file_archive->file_size_inflated = malloc(file_archive->file_count * sizeof(int));
    file_archive->file_size_deflated = malloc(file_archive->file_count * sizeof(int));
    file_archive->file_offset = malloc(file_archive->file_count * sizeof(int));

    int offset = read_24(file_archive->data, unpacked_size, &buffer);

    bump(file_count * 10, &buffer);

    for( int file = 0; file < file_count; file++ )
    {
        file_archive->file_name_hash[file] = read_32(file_archive->data, unpacked_size, &buffer);
        file_archive->file_size_inflated[file] =
            read_24(file_archive->data, unpacked_size, &buffer);
        file_archive->file_size_deflated[file] =
            read_24(file_archive->data, unpacked_size, &buffer);
        file_archive->file_offset[file] = offset;

        offset += file_archive->file_size_deflated[file];
    }
}

#define INDEX_255_ENTRY_SIZE 6
#define SECTOR_SIZE 520

// Index255 is sometimes known as ref table.

// xtea are keys used to encrypt map data.
void
load_index255(char* data, int data_size)
{
    int index_count = data_size / INDEX_255_ENTRY_SIZE;
}

/**
 * @brief "idx" files are indexes.
 */
struct IndexRecord
{
    int idx_file_id; // The file extension, e.g. 2 := idx2
    int record_id, sector, length;
};

static void
read_index_entry(
    int index_file_id, char* data, int data_size, int entry_slot, struct IndexRecord* record)
{
    struct Buffer buffer = { .data = data, .position = entry_slot * INDEX_255_ENTRY_SIZE };

    int length = (data[buffer.position] << 16) | (data[buffer.position + 1] << 8) |
                 (data[buffer.position + 2] & 0xFF);
    int sector = (data[buffer.position + 3] << 16) | (data[buffer.position + 4] << 8) |
                 (data[buffer.position + 5] & 0xFF);

    if( length <= 0 || sector <= 0 )
    {
        return;
    }

    record->length = length;
    record->sector = sector;
    record->record_id = entry_slot;
    record->idx_file_id = index_file_id;
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
static char*
read_dat2(char* data, int data_size, int idx_file_id, int record_id, int sector, int length)
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
        return NULL;
    }

    int out_len = 0;
    char* out = malloc(length);
    memset(out, 0, length);

    for( int part = 0, read_bytes_count = 0; length > read_bytes_count; sector = next_sector )
    {
        if( sector == 0 )
        {
            printf("Unexpected end of file\n");
            return NULL;
        }
        data_buffer.position = sector * SECTOR_SIZE;

        data_block_size = length - read_bytes_count;
        if( record_id > 0xFFFF )
        {
            header_size = 10;
            if( data_block_size > SECTOR_SIZE - header_size )
                data_block_size = SECTOR_SIZE - header_size;

            int bytes_read = readto(
                read_buffer, sizeof(read_buffer), header_size + data_block_size, &data_buffer);
            if( bytes_read < header_size + data_block_size )
            {
                printf("short read when reading file data for %d/%d\n", record_id, current_index);
                return NULL;
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
                printf("short read when reading file data for %d/%d\n", record_id, current_index);
                return NULL;
            }

            read_buffer_len = bytes_read;

            current_archive = ((read_buffer[0] & 0xFF) << 8) | (read_buffer[1] & 0xFF);
            current_part = ((read_buffer[2] & 0xFF) << 8) | (read_buffer[3] & 0xFF);
            next_sector = ((read_buffer[4] & 0xFF) << 16) | ((read_buffer[5] & 0xFF) << 8) |
                          (read_buffer[6] & 0xFF);
            current_index = read_buffer[7] & 0xFF;
        }

        if( record_id != current_archive || current_part != part || idx_file_id != current_index )
        {
            printf(
                "data mismatch %d != %d, %d != %d, %d != %d\n",
                record_id,
                current_archive,
                part,
                current_part,
                idx_file_id,
                current_index);
            return NULL;
        }

        if( next_sector < 0 || data_size / SECTOR_SIZE < (long)next_sector )
        {
            printf("invalid next sector");
            return NULL;
        }

        memcpy(out + out_len, read_buffer + header_size, data_block_size);
        out_len += data_block_size;

        read_bytes_count += data_block_size;

        ++part;
    }

    return out;
}

/**
 * Load .dat2 file
 *  - The dat2 file contains the compressed data.
 *  - The idx files
 * Load index255 which has indexes for the other idx files
 *  - It's entries point to where in the .dat2 file that, e.g. idx2 can be found.
 *  - For example, the second entry in index255 points to the data indexed by idx2 in the .dat2
 * file.
 *
 * The other indexes contain archives inside the chunks of from the .dat2 file
 *
 * Each of the indexes from 0-25 are compressed with bzip2
 *
 * For each index entry in index 255,
 *
 * @return struct Model*
 */
struct Model*
load_models()
{
    char* dat2_data;
    int dat2_size;
    FILE* dat2 = fopen(CACHE_PATH "/main_file_cache.dat2", "rb");
    if( dat2 == NULL )
    {
        printf("Failed to open dat2 file");
        return NULL;
    }
    fseek(dat2, 0, SEEK_END);
    dat2_size = ftell(dat2);
    fseek(dat2, 0, SEEK_SET);
    dat2_data = malloc(dat2_size);
    fread(dat2_data, 1, dat2_size, dat2);
    fclose(dat2);

    int master_index_size;
    char* master_index_data;
    FILE* master_index = fopen(CACHE_PATH "/main_file_cache.idx255", "rb");
    if( master_index == NULL )
    {
        printf("Failed to open master index file");
        return NULL;
    }
    fseek(master_index, 0, SEEK_END);
    master_index_size = ftell(master_index);
    fseek(master_index, 0, SEEK_SET);
    master_index_data = malloc(master_index_size);
    fread(master_index_data, 1, master_index_size, master_index);
    fclose(master_index);

    int master_index_record_count = master_index_size / INDEX_255_ENTRY_SIZE;

    for( int i = 0; i < master_index_record_count; i++ )
    {
        struct IndexRecord record;
        read_index_entry(255, master_index_data, master_index_size, i, &record);

        // record now contains a region in the .dat2 file that contains the data indexed by
        // idx<i>.

        read_dat2(dat2_data, dat2_size, 255, record.record_id, record.sector, record.length);
    }
}
