#ifndef RSCACHE_RSCACHESHARED_COMPRESSION_H
#define RSCACHE_RSCACHESHARED_COMPRESSION_H

#include <stdbool.h>
#include <stdint.h>

#define GZIP_NO_FOOTER 1

uint32_t
RSCacheShared_CompressionGzipDecompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length,
    int flags);

uint32_t
RSCacheShared_CompressionGzipUncompressedSize(
    uint8_t* compressed_data,
    int compressed_length);

uint32_t
RSCacheShared_CompressionBzipDecompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length);

#endif