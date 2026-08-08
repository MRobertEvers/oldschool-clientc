#include "compression.h"

#include "bzip.h"
#include "checksum.h"
#include "miniz.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    COMPRESSION_FORMAT_UNKNOWN = 0,
    COMPRESSION_FORMAT_GZIP = 1,
    COMPRESSION_FORMAT_ZLIB = 2
} compression_format_t;

static inline compression_format_t
detect_compression_format(
    const uint8_t* data,
    size_t size)
{
    if( size < 2 )
        return COMPRESSION_FORMAT_UNKNOWN;

    if( data[0] == 0x1f && data[1] == 0x8b )
        return COMPRESSION_FORMAT_GZIP;

    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    uint16_t header = (uint16_t)((cmf << 8) | flg);
    if( (header % 31) == 0 )
    {
        uint8_t cm = cmf & 0x0f;
        uint8_t cinfo = (cmf >> 4) & 0x0f;

        if( cm == 8 && cinfo <= 7 )
            return COMPRESSION_FORMAT_ZLIB;
    }

    return COMPRESSION_FORMAT_UNKNOWN;
}

static inline size_t
decompress_gzip_with_miniz(
    const uint8_t* compressed_data,
    size_t size,
    uint8_t* decompressed_data,
    size_t uncompressed_length)
{
    uint8_t flags = compressed_data[3];
    size_t header_size = 10;

    if( flags & 0x04 )
    {
        if( size < header_size + 2 )
            return 0;
        uint16_t extra_len = *(uint16_t*)(compressed_data + header_size);
        header_size += 2 + extra_len;
    }

    if( flags & 0x08 )
    {
        while( header_size < size && compressed_data[header_size] != 0 )
            header_size++;
        header_size++;
    }

    if( flags & 0x10 )
    {
        while( header_size < size && compressed_data[header_size] != 0 )
            header_size++;
        header_size++;
    }

    if( flags & 0x02 )
        header_size += 2;

    if( size < header_size + 8 )
        return 0;

    size_t deflate_size = size - header_size - 8;
    const uint8_t* deflate_data = compressed_data + header_size;

    size_t decompressed_size = tinfl_decompress_mem_to_mem(
        decompressed_data,
        uncompressed_length,
        deflate_data,
        deflate_size,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if( decompressed_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED )
        return 0;

    return decompressed_size;
}

uint32_t
RSCache_CompressionGzipUncompressedSize(
    uint8_t* compressed_data,
    int compressed_length)
{
    return *(uint32_t*)(compressed_data + compressed_length - 4);
}

uint32_t
RSCache_CompressionGzipDecompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length,
    int flags)
{
    z_stream strm;
    compression_format_t format =
        detect_compression_format(compressed_data, (size_t)compressed_length);
    if( format == COMPRESSION_FORMAT_UNKNOWN )
        return 0;

    if( format == COMPRESSION_FORMAT_GZIP )
    {
        if( (flags & RSCACHE_GZIP_NO_FOOTER) == 0 )
        {
            int uncompressed_length = *(int*)(compressed_data + compressed_length - 4);
            assert(out_length >= uncompressed_length);
        }

        return (uint32_t)decompress_gzip_with_miniz(
            compressed_data, (size_t)compressed_length, out, (size_t)out_length);
    }

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = (uInt)compressed_length;
    strm.next_in = (Bytef*)compressed_data;
    strm.avail_out = (uInt)out_length;
    strm.next_out = (Bytef*)out;

    int ret = inflateInit2(&strm, 15);
    if( ret != Z_OK )
        return 0;

    ret = inflate(&strm, Z_FINISH);
    if( ret != Z_STREAM_END )
    {
        inflateEnd(&strm);
        return 0;
    }

    int decompressed_size = (int)strm.total_out;
    inflateEnd(&strm);

    return (uint32_t)decompressed_size;
}

uint32_t
RSCache_CompressionBzipDecompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length)
{
    /* 0 means "failed", matching the gzip path above. A corrupt bzip container
     * used to take the process down inside bzip_decompress; it now reports and
     * returns, so a bad group is a failed read the caller can recover from. */
    if( !out || !compressed_data || out_length < 0 || compressed_length < 0 )
        return 0;
    if( bzip_decompress(
            (int8_t*)out, out_length, (int8_t*)compressed_data, compressed_length, 0) !=
        RETVAL_OK )
        return 0;
    return (uint32_t)out_length;
}

#define GZIP_HEADER_SIZE 10
#define GZIP_FOOTER_SIZE 8

uint32_t
RSCache_CompressionGzipCompressBound(int data_length)
{
    if( data_length < 0 )
        return 0;
    /* miniz's bound plus our own framing. */
    return (uint32_t)(compressBound((mz_ulong)data_length) + GZIP_HEADER_SIZE + GZIP_FOOTER_SIZE);
}

uint32_t
RSCache_CompressionGzipCompress(
    uint8_t* out,
    int out_length,
    const uint8_t* data,
    int data_length)
{
    if( !out || !data || data_length < 0 )
        return 0;
    if( out_length < GZIP_HEADER_SIZE + GZIP_FOOTER_SIZE )
        return 0;

    /* Fixed 10-byte header: magic, deflate method, no flags, no mtime, no extra
     * fields. mtime is deliberately zero — a timestamp would make the same input
     * produce different bytes on each run, which would defeat the byte-exact
     * round-trip checks the encoders are verified with. */
    out[0] = 0x1f;
    out[1] = 0x8b;
    out[2] = 8; /* CM = deflate */
    out[3] = 0; /* FLG = none */
    out[4] = 0;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
    out[8] = 0;   /* XFL */
    out[9] = 0xff; /* OS = unknown */

    size_t body_capacity = (size_t)out_length - GZIP_HEADER_SIZE - GZIP_FOOTER_SIZE;

    /* Raw deflate: no zlib wrapper, since gzip supplies its own framing. */
    size_t body_size = tdefl_compress_mem_to_mem(
        out + GZIP_HEADER_SIZE,
        body_capacity,
        data,
        (size_t)data_length,
        TDEFL_DEFAULT_MAX_PROBES);
    if( body_size == 0 )
        return 0;

    uint8_t* footer = out + GZIP_HEADER_SIZE + body_size;
    uint32_t crc = RSCache_Crc32(0, data, (size_t)data_length);
    uint32_t isize = (uint32_t)data_length;

    /* Both footer fields are little-endian, unlike everything else in a cache. */
    footer[0] = (uint8_t)(crc & 0xff);
    footer[1] = (uint8_t)((crc >> 8) & 0xff);
    footer[2] = (uint8_t)((crc >> 16) & 0xff);
    footer[3] = (uint8_t)((crc >> 24) & 0xff);
    footer[4] = (uint8_t)(isize & 0xff);
    footer[5] = (uint8_t)((isize >> 8) & 0xff);
    footer[6] = (uint8_t)((isize >> 16) & 0xff);
    footer[7] = (uint8_t)((isize >> 24) & 0xff);

    return (uint32_t)(GZIP_HEADER_SIZE + body_size + GZIP_FOOTER_SIZE);
}

/* The cache stores bzip2 payloads with the 4-byte "BZh1" magic removed; the
 * client prepends it before decompressing (see bzip_decompress). */
#define BZIP_MAGIC_SIZE 4
/* Block size 1 (100k) — what every RuneScape cache uses. */
#define BZIP_CACHE_LEVEL 1

uint32_t
RSCache_CompressionBzipCompressBound(int data_length)
{
    if( data_length < 0 )
        return 0;
    return bzip_compress_bound((uint32_t)data_length);
}

uint32_t
RSCache_CompressionBzipCompress(
    uint8_t* out,
    int out_length,
    const uint8_t* data,
    int data_length)
{
    if( !out || !data || data_length < 0 )
        return 0;

    /* Compress into scratch so the magic can be dropped without shifting the
     * caller's buffer around. */
    uint32_t scratch_size = bzip_compress_bound((uint32_t)data_length);
    uint8_t* scratch = malloc(scratch_size);
    if( !scratch )
        return 0;

    uint32_t written =
        bzip_compress(scratch, scratch_size, data, (uint32_t)data_length, BZIP_CACHE_LEVEL);
    if( written <= BZIP_MAGIC_SIZE )
    {
        free(scratch);
        return 0;
    }

    uint32_t payload = written - BZIP_MAGIC_SIZE;
    if( (uint32_t)out_length < payload )
    {
        free(scratch);
        return 0;
    }

    memcpy(out, scratch + BZIP_MAGIC_SIZE, payload);
    free(scratch);
    return payload;
}
