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

/*
 * Compressors. Each mirrors the framing its decompressor above expects, so a
 * compress/decompress round trip is byte-exact on the payload.
 */

/**
 * Deflate `data` into a **gzip** stream (0x1f 0x8b header, raw deflate body,
 * CRC-32 + ISIZE footer) — the framing RSCache_CompressionGzipDecompress
 * detects, and what dat1's whole-archive compression uses.
 *
 * Returns the byte count written to `out`, or 0 on failure (including `out`
 * being too small). Ask RSCache_CompressionGzipCompressBound for a safe size.
 */
uint32_t
RSCache_CompressionGzipCompress(
    uint8_t* out,
    int out_length,
    const uint8_t* data,
    int data_length);

/** Worst-case output size for RSCache_CompressionGzipCompress over
 *  `data_length` bytes, including header and footer. */
uint32_t
RSCache_CompressionGzipCompressBound(int data_length);

/**
 * Compress into the **headerless** bzip2 form a cache stores: a complete bzip2
 * stream at block size 1 (100k) with the leading 4-byte "BZh1" magic removed,
 * because the client prepends it on read (see bzip_decompress).
 *
 * Returns the byte count written to `out`, or 0 on failure.
 */
uint32_t
RSCache_CompressionBzipCompress(
    uint8_t* out,
    int out_length,
    const uint8_t* data,
    int data_length);

/** Worst-case output size for RSCache_CompressionBzipCompress. */
uint32_t
RSCache_CompressionBzipCompressBound(int data_length);

#endif
