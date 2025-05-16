#include "osrs_cache.h"

#include <bzlib.h>
#include <stdbool.h>
#include <stdio.h>

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

// Index255 is sometimes known as ref table.

// xtea are keys used to encrypt map data.
void
load_index255(char* data, int data_size)
{
    int index_count = data_size / INDEX_255_ENTRY_SIZE;
}

struct IndexRecord
{
    int file_id;
    int id, sector, length;
};

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
{}
