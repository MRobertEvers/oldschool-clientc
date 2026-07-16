#ifndef RSCACHE_COMPRESSION_H
#define RSCACHE_COMPRESSION_H

#include <stdint.h>

#define RSCACHE_GZIP_NO_FOOTER 1

uint32_t
RSCache_CompressionGzipDecompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length,
    int flags);

uint32_t
RSCache_CompressionGzipUncompressedSize(
    uint8_t* compressed_data,
    int compressed_length);

uint32_t
RSCache_CompressionBzipDecompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length);

#endif
