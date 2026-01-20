#include "compression.h"

#include "3rd/bzip/bzip.h"
#include "3rd/miniz/miniz.h"

// Format detection result
typedef enum
{
    COMPRESSION_FORMAT_UNKNOWN = 0,
    COMPRESSION_FORMAT_GZIP = 1,
    COMPRESSION_FORMAT_ZLIB = 2
} compression_format_t;

/**
 * @brief Detect compression format from data header
 * @param data Pointer to compressed data
 * @param size Size of compressed data
 * @return Detected compression format
 */
static inline compression_format_t
detect_compression_format(
    const uint8_t* data,
    size_t size)
{
    if( size < 2 )
        return COMPRESSION_FORMAT_UNKNOWN;

    // Check for GZIP magic number (0x1f, 0x8b)
    if( data[0] == 0x1f && data[1] == 0x8b )
    {
        return COMPRESSION_FORMAT_GZIP;
    }

    // Check for zlib header
    // zlib header: CMF (Compression Method and Flags) + FLG (FLaGs)
    // CMF: bits 0-3 = CM (compression method, 8 = deflate), bits 4-7 = CINFO (window size)
    // FLG: bits 0-4 = FCHECK (check bits), bits 5-6 = FLEVEL (compression level), bit 7 = FDICT
    // (preset dictionary)
    uint8_t cmf = data[0];
    uint8_t flg = data[1];

    // Check if CMF and FLG form a valid zlib header
    uint16_t header = (cmf << 8) | flg;
    if( (header % 31) == 0 ) // zlib header check
    {
        uint8_t cm = cmf & 0x0f;           // Compression method
        uint8_t cinfo = (cmf >> 4) & 0x0f; // Window size

        if( cm == 8 && cinfo <= 7 ) // Valid deflate with reasonable window size
        {
            printf("Detected zlib format (CMF=0x%02x, FLG=0x%02x)\n", cmf, flg);
            return COMPRESSION_FORMAT_ZLIB;
        }
    }

    printf(
        "Unknown compression format - first 2 bytes: 0x%02x 0x%02x\n",
        size > 0 ? data[0] : 0,
        size > 1 ? data[1] : 0);
    return COMPRESSION_FORMAT_UNKNOWN;
}

/**
 * @brief Decompress GZIP data using miniz by manually stripping headers
 * @param compressed_data Pointer to GZIP compressed data
 * @param size Size of compressed data
 * @param decompressed_data Output buffer for decompressed data
 * @param uncompressed_length Expected uncompressed length
 * @return Number of bytes decompressed, or 0 on failure
 */
static inline size_t
decompress_gzip_with_miniz(
    const uint8_t* compressed_data,
    size_t size,
    uint8_t* decompressed_data,
    size_t uncompressed_length)
{
    // GZIP header structure:
    // Bytes 0-1: Magic number (0x1f, 0x8b) - already verified
    // Byte 2: Compression method (8 = deflate) - already verified
    // Byte 3: Flags
    // Bytes 4-7: Modification time
    // Byte 8: Extra flags
    // Byte 9: Operating system
    // Optional: Extra field, filename, comment (if flags set)

    uint8_t flags = compressed_data[3];
    size_t header_size = 10; // Base header size

    // Skip optional fields based on flags
    if( flags & 0x04 ) // Extra field present
    {
        if( size < header_size + 2 )
        {
            printf("GZIP extra field length missing\n");
            return 0;
        }
        uint16_t extra_len = *(uint16_t*)(compressed_data + header_size);
        header_size += 2 + extra_len;
    }

    if( flags & 0x08 ) // Filename present
    {
        // Find null-terminated filename
        while( header_size < size && compressed_data[header_size] != 0 )
            header_size++;
        header_size++; // Skip null terminator
    }

    if( flags & 0x10 ) // Comment present
    {
        // Find null-terminated comment
        while( header_size < size && compressed_data[header_size] != 0 )
            header_size++;
        header_size++; // Skip null terminator
    }

    if( flags & 0x02 ) // CRC16 present
    {
        header_size += 2;
    }

    // Calculate deflate data size (subtract header and 8-byte footer)
    if( size < header_size + 8 )
    {
        printf("GZIP data too small for header + footer\n");
        return 0;
    }

    size_t deflate_size = size - header_size - 8;
    const uint8_t* deflate_data = compressed_data + header_size;

    // Use miniz raw deflate decompression
    size_t decompressed_size = tinfl_decompress_mem_to_mem(
        decompressed_data,
        uncompressed_length,
        deflate_data,
        deflate_size,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if( decompressed_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED )
    {
        printf("miniz raw deflate decompression failed\n");
        return 0;
    }

    return decompressed_size;
}

uint32_t
cache_gzip_uncompressed_size(
    uint8_t* compressed_data,
    int compressed_length)
{
    return *(int*)(compressed_data + compressed_length - 4);
}

uint32_t
cache_gzip_decompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length)
{
    // GZIP Stores the size of the uncompressed data in the last 4 bytes.
    // i.e. The footer.
    int uncompressed_length = *(int*)(compressed_data + compressed_length - 4);
    assert(out_length >= uncompressed_length);

    // Detect compression format
    compression_format_t format = detect_compression_format(compressed_data, compressed_length);
    if( format == COMPRESSION_FORMAT_UNKNOWN )
    {
        return 0;
    }

    z_stream strm;

    if( format == COMPRESSION_FORMAT_GZIP )
    {
        // Use miniz for GZIP decompression
        size_t decompressed_size =
            decompress_gzip_with_miniz(compressed_data, compressed_length, out, out_length);
        assert(decompressed_size == uncompressed_length);

        return decompressed_size;
    }
    else // format == COMPRESSION_FORMAT_ZLIB
    {
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = compressed_length;
        strm.next_in = (Bytef*)compressed_data;
        strm.avail_out = out_length;
        strm.next_out = (Bytef*)out;

        // Use zlib for zlib format
        int ret = inflateInit2(&strm, 15); // 15 for max window size, no GZIP wrapper
        if( ret != Z_OK )
        {
            printf("inflateInit2 failed with error code: %d\n", ret);
            return 0;
        }

        // Decompress
        ret = inflate(&strm, Z_FINISH);
        if( ret != Z_STREAM_END )
        {
            printf("inflate failed with error code: %d\n", ret);
            inflateEnd(&strm);
            return 0;
        }

        // Cleanup
        int decompressed_size = strm.total_out;
        inflateEnd(&strm);

        return decompressed_size;
    }
}

uint32_t
cache_bzip_decompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length)
{
    // bzip_decompress expects: (output_buffer, input_data, input_size, offset)
    bzip_decompress(out, compressed_data, compressed_length, 0);

    return out_length;
}
