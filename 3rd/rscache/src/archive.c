#include "archive.h"

#include "compression.h"
#include "dat2disk.h"
#include "rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xteas.h>

#define NON_OSRS_PACKED_ARCHIVE_FORMAT 5

static int
hash_djb2(char* name)
{
    int hash = 0;
    for( int i = 0; name[i] != '\0'; i++ )
    {
        hash = (hash << 5) - hash + name[i];
        hash = hash & hash;
    }
    return hash;
}

static int
hash_rolling_polynomial_uppercase(const char* name)
{
    char c = 0;
    int hash = 0;
    for( int i = 0; name[i] != '\0'; i++ )
    {
        c = name[i];
        if( c >= 'a' && c <= 'z' )
            c = (char)(c - 'a' + 'A');
        hash = (hash * 61 + c - 32) | 0;
    }
    return hash;
}

int
RSCache_ArchiveNameHashDat2(char* name)
{
    return hash_djb2(name);
}

int
RSCache_ArchiveNameHashDat(const char* name)
{
    return hash_rolling_polynomial_uppercase(name);
}

bool
RSCache_ArchiveDecryptDecompress(
    struct RSCache_Dat2DiskArchive* archive,
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
    if( xtea_key_nullable && compression != NON_OSRS_PACKED_ARCHIVE_FORMAT )
        xteas_decrypt(archive->data + buffer.position, size + 4, (int32_t*)xtea_key_nullable);

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

static uint8_t decompress_buffer[65536];

bool
RSCache_ArchiveDecompressDat(
    struct RSCache_Dat2DiskArchive* archive,
    enum RSCache_ArchiveFormat format)
{
    assert(archive);

    switch( format )
    {
    case RSCACHE_ARCHIVE_FORMAT_DAT:
    {
        int uncompressed_length = RSCache_CompressionGzipDecompress(
            decompress_buffer,
            sizeof(decompress_buffer),
            (uint8_t*)archive->data,
            archive->data_size,
            RSCACHE_GZIP_NO_FOOTER);

        void* decompressed_data = malloc((size_t)uncompressed_length);
        if( !decompressed_data )
            return false;
        memcpy(decompressed_data, decompress_buffer, (size_t)uncompressed_length);

        free(archive->data);
        archive->data = decompressed_data;
        archive->data_size = uncompressed_length;
        return true;
    }
    case RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE:
        return true;
    default:
        assert(false && "Unknown archive format");
    }

    return false;
}
