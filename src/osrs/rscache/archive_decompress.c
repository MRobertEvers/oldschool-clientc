#include "archive_decompress.h"

#include "archive.h"
#include "compression.h"
#include "rsbuf.h"
#include "xtea.h"

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
archive_decrypt_decompress(
    struct ArchiveBuffer* archive,
    uint32_t* xtea_key_nullable)
{
    // TODO: CRC32

    struct RSBuffer buffer = { .data = archive->data, .position = 0, .size = archive->data_size };

    int compression = g1(&buffer);
    // Uncompressed size
    int size = g4(&buffer);
    if( xtea_key_nullable && compression != NON_OSRS_PACKED_ARCHIVE_FORMAT )
        xtea_decrypt(archive->data + buffer.position, size + 4, xtea_key_nullable);

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
        bytes_read = greadto(&buffer, compressed_data, size, size);
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
        cache_bzip_decompress(decompressed_data, uncompressed_length, compressed_data, size);

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
        bytes_read = greadto(&buffer, compressed_data, size, size);
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

        cache_gzip_decompress(decompressed_data, uncompressed_length, compressed_data, size);

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
archive_decompress(struct ArchiveBuffer* archive)
{
    return archive_decrypt_decompress(archive, NULL);
}

bool
archive_decompress_dat(struct ArchiveBuffer* archive)
{
    switch( archive->format )
    {
    case ARCHIVE_FORMAT_DAT:
    {
        int uncompressed_length = cache_gzip_uncompressed_size(archive->data, archive->data_size);
        if( uncompressed_length == 0 )
            return false;

        uint8_t* decompressed_data = malloc(uncompressed_length);
        if( !decompressed_data )
            return false;

        cache_gzip_decompress(
            decompressed_data, uncompressed_length, archive->data, archive->data_size);

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;

        return true;
    }
    case ARCHIVE_FORMAT_DAT_MULTIFILE:
        // Archive is not a single compressed blob, so we don't need to decompress it.
        // The file archive format will handle the decompression.
        return true;
    default:
        assert(false && "Unknown archive format");
    }

    return false;
}