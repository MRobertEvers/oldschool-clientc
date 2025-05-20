#include "sequence.h"
// #include "xtea.h"
#include "buffer.h"
#include "modeltype.c"

#include <assert.h>
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

/**
 * The Config table and Textures table use records of this form.
 *
 */
struct PackedArchive
{};

static void
read_packed_archive(struct PackedArchive* archive, char* data, int data_size, int archive_size)
{
    struct Buffer buffer = { .data = data, .position = 0, .data_size = data_size };

    // /* read the number of chunks at the end of the archive */
    // buffer.position(buffer.limit() - 1);
    // int chunks = buffer.get() & 0xFF;

    buffer.position = buffer.data_size - 1;
    int chunks = read_8(&buffer);
    buffer.position = 0;

    // int[][] chunkSizes = new int[chunks][size];
    // int[] sizes = new int[size];
    // buffer.position(buffer.limit() - 1 - chunks * size * 4);

    buffer.position = buffer.data_size - 1 - chunks * 4;
    for( int i = 0; i < chunks; i++ )
    {
        // int chunkSize = 0;
        // for (int id = 0; id < size; id++) {
        // 	/* read the delta-encoded chunk length */
        // 	int delta = buffer.getInt();
        // 	chunkSize += delta;

        // 	/* store the size of this chunk */
        // 	chunkSizes[chunk][id] = chunkSize;

        // 	/* and add it to the size of the whole file */
        // 	sizes[id] += chunkSize;
        // }
        int chunk_size = read_32(&buffer);
        int chunk_data = readto(NULL, chunk_size, chunk_size, &buffer);
    }
}

/**
 * @brief TODO: CRC and XTEA for archives that need it.
 *
 * @param archive
 */
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

    int bytes_read;

    switch( compression )
    {
    case 0:
        // No compression
        char* data = malloc(compressed_length);
        bytes_read = readto(data, compressed_length, compressed_length, &buffer);
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
            int uncompressed_length = read_32(&buffer);

            char* compressed_data = malloc(compressed_length + 4); // Add space for magic header
            // Add BZIP2 magic header (BZh1)
            compressed_data[0] = 'B';
            compressed_data[1] = 'Z';
            compressed_data[2] = 'h';
            compressed_data[3] = '1';

            bytes_read = readto(compressed_data + 4, compressed_length, compressed_length, &buffer);
            if( bytes_read < compressed_length )
            {
                printf(
                    "short read when reading file data for %d/%d\n",
                    archive->archive_record_id,
                    archive->archive_record_id);
                free(compressed_data);
                return;
            }

            char* decompressed_data = malloc(uncompressed_length);
            int ret = BZ2_bzBuffToBuffDecompress(
                decompressed_data,
                &uncompressed_length,
                compressed_data,
                compressed_length + 4, // Include magic header in length
                0,                     // Small memory usage
                0                      // Verbosity level
            );

            if( ret != BZ_OK )
            {
                printf("BZ2 decompression failed with error code: %d\n", ret);
                free(decompressed_data);
                free(compressed_data);
                return;
            }

            free(archive->data);
            archive->data = decompressed_data;
            archive->data_size = uncompressed_length;
            free(compressed_data);
        }
        break;
    case 2:
        // GZ compression
        {
            // 	int uncompressedLength = buffer.getInt();
            int uncompressed_length = read_32(&buffer);
            char* compressed_data = malloc(compressed_length);
            bytes_read = readto(compressed_data, compressed_length, compressed_length, &buffer);
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

struct Archive
{
    char** entries;
    int* entry_sizes;
    int entry_count;
};

/**
 * @brief
 *
 * Size is specified in the reference table.
 *
 * @param buffer
 * @param size
 * @return struct Archive*
 */
static struct Archive*
decode_archive(struct Buffer* buffer, int size)
{
    /* allocate a new archive object */
    struct Archive* archive = malloc(sizeof(struct Archive));
    if( !archive )
    {
        return NULL;
    }
    archive->entries = malloc(size * sizeof(char*));
    archive->entry_sizes = malloc(size * sizeof(int));
    if( !archive->entries || !archive->entry_sizes )
    {
        free(archive);
        return NULL;
    }
    archive->entry_count = size;

    /* read the number of chunks at the end of the archive */
    buffer->position = buffer->data_size - 1;
    int chunks = read_8(buffer);
    buffer->position = 0;

    /* read the sizes of the child entries and individual chunks */
    int** chunk_sizes = malloc(chunks * sizeof(int*));
    if( !chunk_sizes )
    {
        free(archive->entries);
        free(archive);
        return NULL;
    }
    for( int i = 0; i < chunks; i++ )
    {
        chunk_sizes[i] = malloc(size * sizeof(int));
        if( !chunk_sizes[i] )
        {
            for( int j = 0; j < i; j++ )
            {
                free(chunk_sizes[j]);
            }
            free(chunk_sizes);
            free(archive->entries);
            free(archive);
            return NULL;
        }
    }
    int* sizes = malloc(size * sizeof(int));
    if( !sizes )
    {
        for( int i = 0; i < chunks; i++ )
        {
            free(chunk_sizes[i]);
        }
        free(chunk_sizes);
        free(archive->entries);
        free(archive);
        return NULL;
    }
    memset(sizes, 0, size * sizeof(int));

    buffer->position = buffer->data_size - 1 - chunks * size * 4;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        int chunk_size = 0;
        for( int id = 0; id < size; id++ )
        {
            /* read the delta-encoded chunk length */
            int delta = read_32(buffer);
            chunk_size += delta;

            /* store the size of this chunk */
            chunk_sizes[chunk][id] = chunk_size;

            /* and add it to the size of the whole file */
            sizes[id] += chunk_size;
        }
    }

    /* allocate the buffers for the child entries */
    for( int id = 0; id < size; id++ )
    {
        archive->entries[id] = malloc(sizes[id]);
        if( !archive->entries[id] )
        {
            for( int j = 0; j < id; j++ )
            {
                free(archive->entries[j]);
            }
            free(archive->entries);
            free(sizes);
            for( int j = 0; j < chunks; j++ )
            {
                free(chunk_sizes[j]);
            }
            free(chunk_sizes);
            free(archive);
            return NULL;
        }
    }

    /* read the data into the buffers */
    buffer->position = 0;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        for( int id = 0; id < size; id++ )
        {
            /* get the length of this chunk */
            int chunk_size = chunk_sizes[chunk][id];

            /* copy this chunk into the file buffer */
            readto(archive->entries[id], chunk_size, chunk_size, buffer);
            archive->entry_sizes[id] = chunk_size;
        }
    }

    /* cleanup */
    for( int i = 0; i < chunks; i++ )
    {
        free(chunk_sizes[i]);
    }
    free(chunk_sizes);
    free(sizes);

    /* return the archive */
    return archive;
}

#define FLAG_IDENTIFIERS 0x1
#define FLAG_WHIRLPOOL 0x2
#define FLAG_SIZES 0x4
#define FLAG_HASH 0x8

struct ReferenceTableEntry
{
    int index;
    int identifier;
    int crc;
    int hash;
    unsigned char whirlpool[64];
    int compressed;
    int uncompressed;
    int version;
    struct
    {
        int* entries;
        int count;
    } children;
};

struct ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    int* ids; // entries is a sparse array. ids is the list of indices that are valid.
    struct ReferenceTableEntry* entries;
    int entry_count;
};

// public static int getSmartInt(ByteBuffer buffer) {
// 	if (buffer.get(buffer.position()) < 0)
// 		return buffer.getInt() & 0x7fffffff;
// 	return buffer.getShort() & 0xFFFF;
// }
static int
get_smart_int(struct Buffer* buffer)
{
    assert(buffer->position < buffer->data_size);
    if( (buffer->data[buffer->position]) < 0 )
        return read_32(buffer) & 0x7fffffff;
    return read_16(buffer) & 0xFFFF;
}

/**
 * @brief OpenRS calls this ReferenceTable. Runelite calls this "IndexData". See those classes
 * RuneLite/IndexData.java
 * OpenRS/ReferenceTable.java
 * @param buffer
 * @return struct ReferenceTable*
 */
struct ReferenceTable*
decode_reference_table(struct Buffer* buffer)
{
    // print the first 20 bytes of the buffer
    for( int i = 0; i < 20; i++ )
    {
        printf("%02X (%d) ", buffer->data[i], buffer->data[i]);
    }
    printf("\n");

    struct ReferenceTable* table = malloc(sizeof(struct ReferenceTable));
    if( !table )
    {
        return NULL;
    }
    memset(table, 0, sizeof(struct ReferenceTable));

    // Read header
    table->format = read_8(buffer) & 0xFF;
    if( table->format < 5 || table->format > 7 )
    {
        free(table);
        return NULL;
    }

    if( table->format >= 6 )
    {
        table->version = read_32(buffer);
    }

    table->flags = read_8(buffer) & 0xFF;

    // Read the IDs
    int id_count;
    if( table->format >= 7 )
        id_count = get_smart_int(buffer);
    else
        id_count = read_16(buffer) & 0xFFFF;
    int* ids = malloc(id_count * sizeof(int));
    if( !ids )
    {
        free(table);
        return NULL;
    }

    table->ids = ids;
    table->id_count = id_count;

    int accumulator = 0;
    int max_id = -1;
    for( int i = 0; i < id_count; i++ )
    {
        int delta = table->format >= 7 ? get_smart_int(buffer) : read_16(buffer) & 0xFFFF;
        ids[i] = accumulator += delta;
        if( ids[i] > max_id )
        {
            max_id = ids[i];
        }
    }
    max_id++;

    // Allocate entries array
    table->entries = malloc(max_id * sizeof(struct ReferenceTableEntry));
    if( !table->entries )
    {
        free(ids);
        free(table);
        return NULL;
    }
    memset(table->entries, 0, max_id * sizeof(struct ReferenceTableEntry));
    // Set index to -1 to indicate it is a sparse array
    for( int i = 0; i < max_id; i++ )
        table->entries[i].index = -1;
    table->entry_count = max_id;

    // Initialize entries
    for( int i = 0; i < id_count; i++ )
    {
        table->entries[ids[i]].index = ids[i];
    }

    // Read identifiers if present
    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            table->entries[id].identifier = read_32(buffer);
        }
    }

    // Read CRC32 checksums
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        table->entries[id].crc = read_32(buffer);
    }

    // Read sizes if present
    if( (table->flags & FLAG_SIZES) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            table->entries[id].compressed = read_32(buffer);
            table->entries[id].uncompressed = read_32(buffer);
        }
    }

    // Read version numbers
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        table->entries[id].version = read_32(buffer);
    }

    // Read child sizes
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        int child_count;
        if( table->format >= 7 )
            child_count = get_smart_int(buffer);
        else
            child_count = read_16(buffer) & 0xFFFF;
        table->entries[id].children.count = child_count;
        table->entries[id].children.entries = malloc(child_count * sizeof(int));
        if( !table->entries[id].children.entries )
        {
            // Cleanup on error
            for( int j = 0; j < i; j++ )
            {
                free(table->entries[ids[j]].children.entries);
            }
            free(ids);
            free(table->entries);
            free(table);
            return NULL;
        }
    }

    // Read child IDs
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        accumulator = 0;
        for( int j = 0; j < table->entries[id].children.count; j++ )
        {
            int delta = table->format >= 7 ? get_smart_int(buffer) : read_16(buffer) & 0xFFFF;
            table->entries[id].children.entries[j] = accumulator += delta;
        }
    }

    // Read identifiers if present
    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        // for (int index = 0; index < validArchivesCount; ++index)
        // 	{
        // 		ArchiveData ad = archives[index];
        // 		int num = numberOfFiles[index];

        // 		for (int i = 0; i < num; ++i)
        // 		{
        // 			FileData fd = ad.files[i];
        // 			int name = stream.readInt();
        // 			fd.nameHash = name;
        // 		}
        // 	}
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            for( int j = 0; j < table->entries[id].children.count; j++ )
            {
                table->entries[id].children.entries[j] = read_32(buffer);
            }
        }
    }

    return table;
}

#include "npctype.c"

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

    int config_index_size;
    char* config_index_data;
    FILE* config_index = fopen(CACHE_PATH "/main_file_cache.idx2", "rb");
    if( config_index == NULL )
    {
        printf("Failed to open config index file");
        return NULL;
    }
    fseek(config_index, 0, SEEK_END);
    config_index_size = ftell(config_index);
    fseek(config_index, 0, SEEK_SET);
    config_index_data = malloc(config_index_size);
    fread(config_index_data, 1, config_index_size, config_index);
    fclose(config_index);

    int master_index_record_count = master_index_size / INDEX_255_ENTRY_SIZE;

    struct Dat2Archive* archives = malloc(master_index_record_count * sizeof(struct Dat2Archive));
    memset(archives, 0, master_index_record_count * sizeof(struct Dat2Archive));
    for( int i = 0; i < master_index_record_count; i++ )
    {
        // cache.read(CacheIndex.META = 255, table: 0)
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

        decompress_dat2archive(&archives[i]);
    }

    // From source
    // OpenRS2
    // cache.read(CacheIndex.MODELS = 7, modelId: 0)
    // Runelite
    // Index index = store.getIndex(IndexType.MODELS);

    int tztok_jad_id = 9319;
    struct IndexRecord record;
    read_index_entry(7, model_index_data, model_index_size, tztok_jad_id, &record);

    struct Dat2Archive archive;
    read_dat2(&archive, dat2_data, dat2_size, 7, record.record_id, record.sector, record.length);

    decompress_dat2archive(&archive);

    struct Model* model = decodeModel(archive.data, archive.data_size);

    write_model_separate(model, "model2");

    // End cache.read

    /***
     * Read the NPC Type Table
     */

    int enum_config_table_index = 8;
    int npcs_config_table_index = 9;
    int config_reference_table_index = 2;
    struct Dat2Archive* config_reference_table = &archives[config_reference_table_index];
    struct Buffer config_reference_table_buffer = { .data = config_reference_table->data,
                                                    .data_size = config_reference_table->data_size,
                                                    .position = 0 };
    struct ReferenceTable* reference_table = decode_reference_table(&config_reference_table_buffer);

    for( int i = 0; i < reference_table->id_count; i++ )
    {
        // The reference table entries is a sparse array.
        // The "index" is the index of the entry in the reference table.
        // not necessarily the index of the entry in the entry array.
        // look for the entry with index i

        int index = reference_table->ids[i];
        printf("Entry %d: %d\n", i, reference_table->entries[index].index);
        // print the size
        printf("Size: %d\n", reference_table->entries[index].uncompressed);
        printf("Compressed: %d\n", reference_table->entries[index].compressed);
        printf("Children: %d\n", reference_table->entries[index].children.count);
    }

    int npc_config_table_index;
    int sequence_config_table_index;
    for( int i = 0; i < reference_table->id_count; i++ )
    {
        if( reference_table->entries[i].index == npcs_config_table_index )
        {
            npc_config_table_index = i;
        }
        else if( reference_table->entries[i].index == sequence_config_table_index )
        {
            sequence_config_table_index = i;
        }
    }
    int npc_config_table_size = reference_table->entries[npc_config_table_index].children.count;
    int sequence_config_table_size =
        reference_table->entries[sequence_config_table_index].children.count;
    // cache.read(CacheIndex.CONFIGS, ConfigArchive.NPC)

    read_index_entry(2, config_index_data, config_index_size, 9, &record);

    // struct Dat2Archive archive;
    memset(&archive, 0, sizeof(struct Dat2Archive));
    read_dat2(&archive, dat2_data, dat2_size, 2, record.record_id, record.sector, record.length);

    decompress_dat2archive(&archive);

    // End cache.read   struct IndexRecord record;

    struct Buffer config_archive_buffer = { .data = archive.data,
                                            .data_size = archive.data_size,
                                            .position = 0 };

    struct Archive* packed_archive = decode_archive(&config_archive_buffer, npc_config_table_size);

    for( int i = 0; i < packed_archive->entry_count; i++ )
    {
        struct Buffer buffer = { .data = packed_archive->entries[i],
                                 .data_size = packed_archive->entry_sizes[i],
                                 .position = 0 };
        struct NPCType npc = { 0 };
        decode_npc_type(&npc, &buffer);

        // Print TzTok-Jad
        if( npc.name && strcmp(npc.name, "TzTok-Jad") == 0 )
        {
            print_npc_type(&npc);
        }
    }

    // Read animations from the osrs cache
    int animations_index_size;
    char* animations_index_data;
    FILE* animations_index = fopen(CACHE_PATH "/main_file_cache.idx0", "rb");
    if( animations_index == NULL )
    {
        printf("Failed to open animations index file");
        return NULL;
    }
    fseek(animations_index, 0, SEEK_END);
    animations_index_size = ftell(animations_index);
    fseek(animations_index, 0, SEEK_SET);
    animations_index_data = malloc(animations_index_size);
    fread(animations_index_data, 1, animations_index_size, animations_index);
    fclose(animations_index);

    // Load skeletons from index 1
    int skeletons_index_size;
    char* skeletons_index_data;
    FILE* skeletons_index = fopen(CACHE_PATH "/main_file_cache.idx1", "rb");
    if( skeletons_index == NULL )
    {
        printf("Failed to open skeletons index file");
        return NULL;
    }
    fseek(skeletons_index, 0, SEEK_END);
    skeletons_index_size = ftell(skeletons_index);
    fseek(skeletons_index, 0, SEEK_SET);
    skeletons_index_data = malloc(skeletons_index_size);
    fread(skeletons_index_data, 1, skeletons_index_size, skeletons_index);
    fclose(skeletons_index);

    // Read sequence
    // Runelite
    // Index index = store.getIndex(IndexType.CONFIGS);
    // Archive archive = index.getArchive(ConfigType.SEQUENCE.getId());

    int config_type_sequence = 12;
    read_index_entry(2, config_index_data, config_index_size, config_type_sequence, &record);

    // struct Dat2Archive archive;
    memset(&archive, 0, sizeof(struct Dat2Archive));
    read_dat2(&archive, dat2_data, dat2_size, 2, record.record_id, record.sector, record.length);

    // byte[] archiveData = storage.loadArchive(archive);
    decompress_dat2archive(&archive);

    // ArchiveFiles files = archive.getFiles(archiveData);
    // Read packed archive
    struct Buffer archive_buffer = { .data = archive.data,
                                     .data_size = archive.data_size,
                                     .position = 0 };
    // ArchiveFiles files = archive.getFiles(archiveData);
    packed_archive = decode_archive(&archive_buffer, sequence_config_table_size);

    for( int i = 0; i < packed_archive->entry_count; i++ )
    {
        struct Buffer buffer = { .data = packed_archive->entries[i],
                                 .data_size = packed_archive->entry_sizes[i],
                                 .position = 0 };
        struct SequenceDefinition sequence;
        decode_sequence(&sequence, &buffer);
        if( sequence.id == 2650 )
        {
            print_sequence(&sequence);
        }
        free_sequence(&sequence);
    }
}
