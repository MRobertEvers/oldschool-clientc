#include "archive_decompress.h"

#include "archive.h"
#include "rsbuf.h"
#include "xtea.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3rd/miniz/miniz.h"
#include "3rd/bzip/bzip.h"

// Format detection result
typedef enum {
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
static inline compression_format_t detect_compression_format(const uint8_t* data, size_t size)
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
    // FLG: bits 0-4 = FCHECK (check bits), bits 5-6 = FLEVEL (compression level), bit 7 = FDICT (preset dictionary)
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    
    // Check if CMF and FLG form a valid zlib header
    uint16_t header = (cmf << 8) | flg;
    if( (header % 31) == 0 ) // zlib header check
    {
        uint8_t cm = cmf & 0x0f;  // Compression method
        uint8_t cinfo = (cmf >> 4) & 0x0f;  // Window size
        
        if( cm == 8 && cinfo <= 7 ) // Valid deflate with reasonable window size
        {
            printf("Detected zlib format (CMF=0x%02x, FLG=0x%02x)\n", cmf, flg);
            return COMPRESSION_FORMAT_ZLIB;
        }
    }
    
    printf("Unknown compression format - first 2 bytes: 0x%02x 0x%02x\n", 
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
static inline size_t decompress_gzip_with_miniz(const uint8_t* compressed_data, size_t size, 
                                               uint8_t* decompressed_data, size_t uncompressed_length)
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
        decompressed_data, uncompressed_length,
        deflate_data, deflate_size,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
    );
    
    if( decompressed_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED )
    {
        printf("miniz raw deflate decompression failed\n");
        return 0;
    }
    
    return decompressed_size;
}


/**
 * @brief TODO: CRC and XTEA for archives that need it.
 *
 * @param archive
 */
bool
archive_decrypt_decompress(struct Dat2Archive* archive, uint32_t* xtea_key_nullable)
{
    // TODO: CRC32

    struct RSBuffer buffer = { .data = archive->data, .position = 0, .size = archive->data_size };

    int compression = g1(&buffer);
    // Uncompressed size
    int size = g4(&buffer);
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
        bzip_decompress((int8_t*)decompressed_data, (int8_t*)compressed_data, size, 0);

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


        // Detect compression format
        compression_format_t format = detect_compression_format(compressed_data, size);
        if( format == COMPRESSION_FORMAT_UNKNOWN )
        {
            free(decompressed_data);
            free(compressed_data);
            return false;
        }

        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = size;
        strm.next_in = (Bytef*)compressed_data;
        strm.avail_out = uncompressed_length;
        strm.next_out = (Bytef*)decompressed_data;

        if( format == COMPRESSION_FORMAT_GZIP )
        {
            // Use miniz for GZIP decompression
            size_t decompressed_size = decompress_gzip_with_miniz(
                compressed_data, size, decompressed_data, uncompressed_length
            );
            
            if( decompressed_size == 0 )
            {
                free(decompressed_data);
                free(compressed_data);
                return false;
            }
            
            // Update archive with decompressed data
            free(archive->data);
            archive->data = (char*)decompressed_data;
            archive->data_size = (int)decompressed_size;
        }
        else // format == COMPRESSION_FORMAT_ZLIB
        {
            // Use zlib for zlib format
            int ret = inflateInit2(&strm, 15); // 15 for max window size, no GZIP wrapper
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
            archive->data = (char*)decompressed_data;
            archive->data_size = strm.total_out;

            // Cleanup
            inflateEnd(&strm);
        }
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