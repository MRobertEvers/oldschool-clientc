#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <stdbool.h>
#include <stdint.h>

uint32_t
cache_gzip_decompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length);

uint32_t
cache_gzip_uncompressed_size(
    uint8_t* compressed_data,
    int compressed_length);

uint32_t
cache_bzip_decompress(
    uint8_t* out,
    int out_length,
    uint8_t* compressed_data,
    int compressed_length);

#endif