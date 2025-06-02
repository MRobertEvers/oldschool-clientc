#include "archive.h"
#include "buffer.h"
#include "xtea.h"

#include <assert.h>
#include <bzlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/**
 * @brief TODO: CRC and XTEA for archives that need it.
 *
 * @param archive
 */
bool
archive_decrypt_decompress(struct Dat2Archive* archive, int32_t* xtea_key_nullable)
{
    // TODO: CRC32

    struct Buffer buffer = { .data = archive->data,
                             .position = 0,
                             .data_size = archive->data_size };

    int compression = read_8(&buffer);
    // Uncompressed size
    int size = read_32(&buffer);
    if( xtea_key_nullable )
        xtea_decrypt(archive->data + buffer.position, size + 4, xtea_key_nullable);

    unsigned int crc = 0;
    for( int i = 0; i < 5; i++ )
    {
        crc = (crc << 8) ^ archive->data[i];
    }

    int bytes_read;

    switch( compression )
    {
    case 0:
    {
        // No compression
        char* data = malloc(size);
        bytes_read = readto(data, size, size, &buffer);
        if( bytes_read < size )
            return false;

        archive->data = data;
        archive->data_size = bytes_read;
        break;
    }
    case 1:
    {
        // BZip compression
        int uncompressed_length = read_32(&buffer);

        char* compressed_data = malloc(size + 4);
        bytes_read = readto(compressed_data + 4, size, size, &buffer);

        // Add BZIP2 magic header (BZh1)
        compressed_data[0] = 'B';
        compressed_data[1] = 'Z';
        compressed_data[2] = 'h';
        compressed_data[3] = '1';

        char* decompressed_data = malloc(uncompressed_length);
        int ret = BZ2_bzBuffToBuffDecompress(
            decompressed_data,
            &uncompressed_length,
            compressed_data,
            size + 4,
            0, // Small memory usage
            0  // Verbosity level
        );

        if( ret != BZ_OK )
        {
            printf("BZ2 decompression failed with error code: %d\n", ret);
            free(decompressed_data);
            free(compressed_data);
            return false;
        }

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;
        free(compressed_data);
    }
    break;
    case 2:
    {
        // GZ compression
        // 	int uncompressedLength = buffer.getInt();
        int uncompressed_length = read_32(&buffer) & 0xFFFFFFFF;
        char* compressed_data = malloc(size);
        bytes_read = readto(compressed_data, size, size, &buffer);
        if( bytes_read < size )
        {
            free(compressed_data);
            return false;
        }

        char* decompressed_data = malloc(uncompressed_length);
        if( !decompressed_data )
        {
            free(compressed_data);
            return false;
        }

        // Initialize zlib stream
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = size;
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
            return false;
        }

        // Decompress
        ret = inflate(&strm, Z_FINISH);
        if( ret != Z_STREAM_END )
        {
            printf("inflate failed with error code: %d\n", ret);
            inflateEnd(&strm);
            free(decompressed_data);
            free(compressed_data);
            return false;
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
        assert("Unknown compression method" && 0);
        return false;
    }

    return true;
}

bool
archive_decompress(struct Dat2Archive* archive)
{
    return archive_decrypt_decompress(archive, NULL);
}