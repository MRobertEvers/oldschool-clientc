#include "osrs_cache.h"
// #include "xtea.h"

#include <bzlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

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
read_8(struct Buffer* buffer)
{
    buffer->position += 1;
    return buffer->data[buffer->position - 1] & 0xFF;
}

static int
read_24(struct Buffer* buffer)
{
    buffer->position += 3;
    return ((buffer->data[buffer->position - 3] & 0xFF) << 16) |
           ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

static int
read_16(struct Buffer* buffer)
{
    buffer->position += 2;
    return ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

static int
read_32(struct Buffer* buffer)
{
    buffer->position += 4;
    // print the 4 bytes at the current position

    return ((buffer->data[buffer->position - 4] & 0xFF) << 24) |
           ((buffer->data[buffer->position - 3] & 0xFF) << 16) |
           ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
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

struct Dat2Archive
{
    char* data;
    int data_size;
    int archive_record_id;
};

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
    int record_id,
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
                printf("short read when reading file data for %d/%d\n", record_id, current_index);
                return -1;
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
    archive->archive_record_id = record_id;

    return 0;
}

static void
decompress_dat2archive(struct Dat2Archive* archive)
{
    // TODO: CRC32

    struct Buffer buffer = { .data = archive->data,
                             .position = 0,
                             .data_size = archive->data_size };

    int compression = read_8(&buffer);
    int compressed_length = read_32(&buffer);

    if( compressed_length < 0 || compressed_length > 1000000 )
    {
        printf("Invalid data");
        return;
    }

    unsigned int crc = 0;
    for( int i = 0; i < 5; i++ )
    {
        crc = (crc << 8) ^ archive->data[i];
    }

    switch( compression )
    {
    case 0:
        // No compression
        char* data = malloc(compressed_length);
        int bytes_read = readto(data, compressed_length, compressed_length, &buffer);
        if( bytes_read < compressed_length )
        {
            printf(
                "short read when reading file data for %d/%d\n",
                archive->archive_record_id,
                archive->archive_record_id);
        }

        archive->data = data;
        archive->data_size = bytes_read;
        break;
    case 1:
        // BZip compression
        {
            // byte[] encryptedData = new byte[compressedLength + 4];
            char* encrypted_data = malloc(compressed_length + 4);
            int bytes_read =
                readto(encrypted_data, compressed_length + 4, buffer.data_size, &buffer);
            if( bytes_read < compressed_length + 4 )
            {
                printf(
                    "short read when reading file data for %d/%d\n",
                    archive->archive_record_id,
                    archive->archive_record_id);
            }

            // Allocate buffer for decompressed data
            char* decompressed_data = malloc(compressed_length * 2); // Start with 2x size
            unsigned int decompressed_size = compressed_length * 2;

            // Decompress using bzlib
            int ret = BZ2_bzBuffToBuffDecompress(
                decompressed_data,
                &decompressed_size,
                encrypted_data,
                compressed_length,
                0, // Small memory usage
                0  // Verbosity level
            );

            if( ret != BZ_OK )
            {
                printf("BZ2 decompression failed with error code: %d\n", ret);
                free(decompressed_data);
                free(encrypted_data);
                return;
            }

            // Update archive with decompressed data
            free(archive->data);
            archive->data = decompressed_data;
            archive->data_size = decompressed_size;
            free(encrypted_data);
        }
        break;
    case 2:
        // GZ compression
        {
            // 	int uncompressedLength = buffer.getInt();
            int uncompressed_length = read_32(&buffer);
            char* compressed_data = malloc(compressed_length);
            int bytes_read = readto(compressed_data, compressed_length, compressed_length, &buffer);
            if( bytes_read < compressed_length )
            {
                printf(
                    "short read when reading file data for %d/%d\n",
                    archive->archive_record_id,
                    archive->archive_record_id);
                free(compressed_data);
                return;
            }

            // Allocate buffer for decompressed data
            char* decompressed_data = malloc(uncompressed_length); // Start with 2x size

            // Initialize zlib stream
            z_stream strm;
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = compressed_length;
            strm.next_in = (Bytef*)compressed_data;
            strm.avail_out = uncompressed_length;
            strm.next_out = (Bytef*)decompressed_data;

            // Initialize for GZIP format
            int ret = inflateInit2(&strm, 15 + 32); // 15 for max window size, 32 for GZIP format
            if( ret != Z_OK )
            {
                printf("inflateInit2 failed with error code: %d\n", ret);
                free(decompressed_data);
                free(compressed_data);
                return;
            }

            // Decompress
            ret = inflate(&strm, Z_FINISH);
            if( ret != Z_STREAM_END )
            {
                printf("inflate failed with error code: %d\n", ret);
                inflateEnd(&strm);
                free(decompressed_data);
                free(compressed_data);
                return;
            }

            // Update archive with decompressed data
            free(archive->data);
            archive->data = decompressed_data;
            archive->data_size = strm.total_out;

            // Cleanup
            inflateEnd(&strm);
            free(compressed_data);
        }
        break;
    default:
        printf("Unknown compression method: %d\n", compression);
        return;
    }

    // cleanup_xtea_keys();
}

/**
 * Load .dat2 file. The .dat2 file contains tables for each of the idx files.
 *  - The dat2 file may contain compressed data.
 * index255 has IndexRecords that point to metadata for the other tables stored in the .dat2 file.
 *  - It's entries contain table information such as number of entries, crc, compressed size,
 * uncompressed size, entry ids, etc.
 * The other idx files contain IndexRecords that point to data such as model data, object data, etc.
 *
 * Each dat2 record may be uncompressed, compressed with bzip, or compressed with gzip.
 * This includes the meta table and all other tables.
 *
 * All IndexRecords reference some location in the .dat2 file.
 *
 * Table 7 (indexed by idx7) is the model table.
 *
 * |--------------------------------|
 * |  Meta Table                    |
 * |    |- Table 7 Record           |
 * |    |- ...                      |
 * |--------------------------------|
 * |  Model Table                   |
 * |    |- Model Record             |
 * |    |- ...                      |
 * |--------------------------------|
 *
 * The .dat2 file itself is written in sectors. Each sector is 520 bytes.
 * Each record in the .dat2 resides in some number of sectors.
 * All records are aligned to sector boundaries.
 *
 *
 * Index files are all the same, and are arrays of IndexRecords. These a FIXED SIZE!
 * They are 6 bytes each.
 * struct IndexRecord
 * {
 *     int idx_file_id; // The file extension, e.g. 2 := idx2
 *     int record_id, sector, length;
 * };
 *
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

    int model_index_size;
    char* model_index_data;
    FILE* model_index = fopen(CACHE_PATH "/main_file_cache.idx7", "rb");
    if( model_index == NULL )
    {
        printf("Failed to open model index file");
        return NULL;
    }
    fseek(model_index, 0, SEEK_END);
    model_index_size = ftell(model_index);
    fseek(model_index, 0, SEEK_SET);
    model_index_data = malloc(model_index_size);
    fread(model_index_data, 1, model_index_size, model_index);
    fclose(model_index);

    int master_index_record_count = master_index_size / INDEX_255_ENTRY_SIZE;

    struct Dat2Archive* archives = malloc(master_index_record_count * sizeof(struct Dat2Archive));
    memset(archives, 0, master_index_record_count * sizeof(struct Dat2Archive));
    for( int i = 0; i < master_index_record_count; i++ )
    {
        struct IndexRecord record;
        read_index_entry(255, master_index_data, master_index_size, i, &record);

        // record now contains a region in the .dat2 file that contains the data indexed by
        // idx<i>.

        read_dat2(
            &archives[i],
            dat2_data,
            dat2_size,
            255,
            record.record_id,
            record.sector,
            record.length);
    }

    struct Dat2Archive* model_archive = &archives[7];

    struct IndexRecord record;
    read_index_entry(7, model_index_data, model_index_size, 0, &record);

    // char* record_buffer = malloc(record.length);
    // readto(record_buffer, record.length, record.length, model_archive);

    struct Dat2Archive archive;
    read_dat2(&archive, dat2_data, dat2_size, 7, record.record_id, record.sector, record.length);

    decompress_dat2archive(&archive);
}
