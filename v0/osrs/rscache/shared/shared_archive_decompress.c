#include "shared_archive_decompress.h"

#include "shared_archive.h"
#include "shared_compression.h"
#include "shared_rs_buffer.h"
#include "shared_xtea.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Early development of the asset server used this format
 * to indicate no compression or encryption.
 */
#define NON_OSRS_PACKED_ARCHIVE_FORMAT 5

/**
 * @brief TODO: CRC and XTEA for archives that need it.
 *
 * @param archive
 */
bool
RSCacheShared_ArchiveDecryptDecompress(
    struct RSCacheShared_ArchiveBuffer* archive,
    uint32_t* xtea_key_nullable)
{
    // TODO: CRC32

    struct RSCacheShared_RSBuffer buffer = {
        .data = (uint8_t*)archive->data, .position = 0, .size = (uint32_t)archive->data_size };

    int compression = g1(&buffer);
    // Uncompressed size
    int size = g4(&buffer);
    if( xtea_key_nullable && compression != NON_OSRS_PACKED_ARCHIVE_FORMAT )
        RSCacheShared_XteaDecrypt(
            archive->data + buffer.position, size + 4, (int32_t*)xtea_key_nullable);

    unsigned int crc = 0;
    for( int i = 0; i < 5; i++ )
    {
        crc = (crc << 8) ^ archive->data[i];
    }

    int bytes_read;

    switch( compression )
    {
    // 5 is a hack for me.
    case NON_OSRS_PACKED_ARCHIVE_FORMAT:
    case 0:
    {
        // No compression
        char* data = malloc(size);
        bytes_read = greadto(&buffer, data, size, size);
        if( bytes_read < size )
            return false;

        free(archive->data);

        archive->data = data;
        archive->data_size = bytes_read;
        break;
    }
    case 1:
    {
        // BZip compression
        int uncompressed_length = g4(&buffer);

        char* compressed_data = malloc(size);
        bytes_read = greadto(&buffer, (char*)compressed_data, size, size);
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

        // bzip_decompress expects: (output_buffer, input_data, input_size, offset)
        RSCacheShared_CompressionBzipDecompress(
            (uint8_t*)decompressed_data,
            uncompressed_length,
            (uint8_t*)compressed_data,
            size);

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
        int uncompressed_length = g4(&buffer) & 0xFFFFFFFF;
        uint8_t* compressed_data = malloc(size);
        bytes_read = greadto(&buffer, (char*)compressed_data, size, size);
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

        RSCacheShared_CompressionGzipDecompress(
            (uint8_t*)decompressed_data, uncompressed_length, compressed_data, size, 0);

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;
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
RSCacheShared_ArchiveDecompress(struct RSCacheShared_ArchiveBuffer* archive)
{
    return RSCacheShared_ArchiveDecryptDecompress(archive, NULL);
}

static uint8_t decompress_buffer[65536];
bool
RSCacheShared_ArchiveDecompressDat(struct RSCacheShared_ArchiveBuffer* archive)
{
    switch( archive->format )
    {
    case RSCacheShared_ArchiveFormat_Dat:
    {
        int uncompressed_length = RSCacheShared_CompressionGzipDecompress(
            decompress_buffer,
            sizeof(decompress_buffer),
            (uint8_t*)archive->data,
            archive->data_size,
            GZIP_NO_FOOTER);

        void* decompressed_data = malloc(uncompressed_length);
        if( !decompressed_data )
            return false;
        memcpy(decompressed_data, decompress_buffer, uncompressed_length);

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;

        return true;
    }
    case RSCacheShared_ArchiveFormat_DatMultifile:
        // Archive is not a single compressed blob, so we don't need to decompress it.
        // The file archive format will handle the decompression.
        return true;
    default:
        assert(false && "Unknown archive format");
    }

    return false;
}