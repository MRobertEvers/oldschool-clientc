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

/** Optional container trailer: revision after the compressed payload (RuneLite
 *  Container.decompress). Prefer 4 bytes when enough remain (OSRS 235+), else 2. */
static void
archive_read_container_revision(
    struct RSCache_Buffer* buffer,
    struct RSCache_Dat2DiskArchive* archive)
{
    uint32_t remaining = RSCache_BufferRemaining(buffer);
    if( remaining >= 4u )
        archive->revision = RSCache_BufferG4(buffer);
    else if( remaining >= 2u )
        archive->revision = (int)RSCache_BufferG2(buffer);
}

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
        archive_read_container_revision(&buffer, archive);
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

        archive_read_container_revision(&buffer, archive);

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

        archive_read_container_revision(&buffer, archive);

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

uint32_t
RSCache_ArchiveEncodeBound(
    uint32_t data_size,
    int compression)
{
    /* 1 compression byte + 2 length fields, plus room for the payload to grow
     * slightly when a compressor cannot win. */
    uint32_t framing = 1u + 4u + 4u + 16u;
    switch( compression )
    {
    case RSCACHE_ARCHIVE_COMPRESSION_NONE:
        return framing + data_size;
    case RSCACHE_ARCHIVE_COMPRESSION_BZIP2:
        return framing + RSCache_CompressionBzipCompressBound((int)data_size);
    case RSCACHE_ARCHIVE_COMPRESSION_GZIP:
        return framing + RSCache_CompressionGzipCompressBound((int)data_size);
    default:
        return 0;
    }
}

uint32_t
RSCache_ArchiveEncode(
    uint8_t* out,
    uint32_t out_capacity,
    const uint8_t* data,
    uint32_t data_size,
    int compression,
    uint32_t* xtea_key_nullable)
{
    if( !out || (!data && data_size != 0) )
        return 0;

    uint8_t* payload = NULL;
    uint32_t payload_size = 0;

    switch( compression )
    {
    case RSCACHE_ARCHIVE_COMPRESSION_NONE:
        payload_size = data_size;
        break;

    case RSCACHE_ARCHIVE_COMPRESSION_BZIP2:
    {
        uint32_t bound = RSCache_CompressionBzipCompressBound((int)data_size);
        payload = malloc(bound ? bound : 1);
        if( !payload )
            return 0;
        payload_size =
            RSCache_CompressionBzipCompress(payload, (int)bound, data, (int)data_size);
        if( payload_size == 0 )
        {
            free(payload);
            return 0;
        }
        break;
    }

    case RSCACHE_ARCHIVE_COMPRESSION_GZIP:
    {
        uint32_t bound = RSCache_CompressionGzipCompressBound((int)data_size);
        payload = malloc(bound ? bound : 1);
        if( !payload )
            return 0;
        payload_size =
            RSCache_CompressionGzipCompress(payload, (int)bound, data, (int)data_size);
        if( payload_size == 0 )
        {
            free(payload);
            return 0;
        }
        break;
    }

    default:
        return 0;
    }

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    bool uncompressed_field = compression != RSCACHE_ARCHIVE_COMPRESSION_NONE;
    uint32_t needed = 1u + 4u + (uncompressed_field ? 4u : 0u) + payload_size;
    if( out_capacity < needed )
    {
        free(payload);
        return 0;
    }

    p1(&buffer, compression);
    /* The stored length counts the payload only, not the uncompressed-length
     * field that precedes it — the decoder reads that field separately and then
     * consumes `size` payload bytes. */
    p4(&buffer, (int)payload_size);

    /* Everything from here is what XTEA covers. */
    uint32_t encrypted_start = buffer.position;

    if( uncompressed_field )
        p4(&buffer, (int)data_size);

    pbuf(&buffer, payload ? payload : data, (int)payload_size);
    free(payload);

    if( xtea_key_nullable )
    {
        /* The decoder decrypts `size + 4` bytes starting right after the length
         * field, which is the uncompressed-length field plus the payload. For an
         * uncompressed archive there is no such field, so the span is just the
         * payload. */
        uint32_t span = buffer.position - encrypted_start;
        xteas_encrypt((char*)out + encrypted_start, (int)span, (int32_t*)xtea_key_nullable);
    }

    return buffer.position;
}

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
        /* Dat1 model/anim archives are whole-file gzip. Prefer the gzip ISIZE
         * footer (mod 2^32) so archives larger than the old 64 KiB scratch
         * buffer still decompress. When the footer is missing or unusable,
         * fall back to a growing scratch allocation. */
        uint32_t expect = 0;
        if( archive->data_size >= 8 )
            expect = RSCache_CompressionGzipUncompressedSize(
                (uint8_t*)archive->data, archive->data_size);

        uint32_t capacity = expect > 0 ? expect : 65536u;
        uint8_t* out = NULL;
        uint32_t uncompressed_length = 0;
        for( ;; )
        {
            free(out);
            out = malloc(capacity ? capacity : 1);
            if( !out )
                return false;
            uncompressed_length = RSCache_CompressionGzipDecompress(
                out,
                (int)capacity,
                (uint8_t*)archive->data,
                archive->data_size,
                RSCACHE_GZIP_NO_FOOTER);
            if( uncompressed_length > 0 )
                break;
            if( expect > 0 || capacity >= (1u << 26) )
            {
                free(out);
                return false;
            }
            capacity *= 2;
        }

        free(archive->data);
        archive->data = out;
        archive->data_size = (int)uncompressed_length;
        return true;
    }
    case RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE:
        return true;
    default:
        assert(false && "Unknown archive format");
    }

    return false;
}
