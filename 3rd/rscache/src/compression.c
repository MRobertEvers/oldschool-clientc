#include "compression.h"

#include "bzip.h"
#include "miniz.h"

#include <assert.h>
#include <stdio.h>

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
    bzip_decompress((int8_t*)out, (int8_t*)compressed_data, compressed_length, 0);
    return (uint32_t)out_length;
}
