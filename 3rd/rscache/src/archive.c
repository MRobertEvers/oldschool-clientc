#include "archive.h"

#include "compression.h"
#include "disk.h"
#include "rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NON_OSRS_PACKED_ARCHIVE_FORMAT 5

bool
RSCache_ArchiveDecryptDecompress(
    struct RSCache_DiskArchive* archive,
    uint32_t* xtea_key_nullable)
{
    assert(archive);

    struct RSCache_Buffer buffer = {
        .data = (uint8_t*)archive->data,
        .position = 0,
        .size = (uint32_t)archive->data_size,
    };

    int compression = g1(&buffer);
    int size = g4(&buffer);
    (void)xtea_key_nullable;

    int bytes_read;

    switch( compression )
    {
    case NON_OSRS_PACKED_ARCHIVE_FORMAT:
    case 0:
    {
        char* data = malloc((size_t)size);
        if( !data )
            return false;

        bytes_read = greadto(&buffer, data, size, size);
        if( bytes_read < size )
        {
            free(data);
            return false;
        }

        free(archive->data);
        archive->data = data;
        archive->data_size = bytes_read;
        break;
    }
    case 1:
    {
        int uncompressed_length = g4(&buffer);

        char* compressed_data = malloc((size_t)size);
        if( !compressed_data )
            return false;

        bytes_read = greadto(&buffer, compressed_data, size, size);
        if( bytes_read < size )
        {
            free(compressed_data);
            return false;
        }

        char* decompressed_data = malloc((size_t)uncompressed_length);
        if( !decompressed_data )
        {
            free(compressed_data);
            return false;
        }

        RSCache_CompressionBzipDecompress(
            (uint8_t*)decompressed_data, uncompressed_length, (uint8_t*)compressed_data, size);

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;
        free(compressed_data);
        break;
    }
    case 2:
    {
        int uncompressed_length = g4(&buffer) & 0xFFFFFFFF;
        uint8_t* compressed_data = malloc((size_t)size);
        if( !compressed_data )
            return false;

        bytes_read = greadto(&buffer, (char*)compressed_data, size, size);
        if( bytes_read < size )
        {
            free(compressed_data);
            return false;
        }

        char* decompressed_data = malloc((size_t)uncompressed_length);
        if( !decompressed_data )
        {
            free(compressed_data);
            return false;
        }

        RSCache_CompressionGzipDecompress(
            (uint8_t*)decompressed_data, uncompressed_length, compressed_data, size, 0);

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;
        free(compressed_data);
        break;
    }
    default:
        printf("Unknown compression method: %d\n", compression);
        assert("Unknown compression method" && 0);
        return false;
    }

    return true;
}
