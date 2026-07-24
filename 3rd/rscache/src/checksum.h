#ifndef RSCACHE_CHECKSUM_H
#define RSCACHE_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Checksums the cache formats need when writing.
 *
 * Two *different* CRC-32s appear in a cache, and confusing them produces files
 * that read back fine locally but fail verification in a real client:
 *
 *  - `RSCache_Crc32` — the reflected, LSB-first CRC-32 (ISO-HDLC / zlib, poly
 *    0xEDB88320). This is what a JS5 reference table stores per archive, over
 *    the archive's *container* bytes (compression byte, lengths and payload as
 *    they sit on disk — not the decompressed data).
 *  - `RSCache_Crc32Bzip2` — the unreflected, MSB-first CRC-32 (poly 0x04C11DB7)
 *    used *inside* a bzip2 stream for its per-block and combined checksums.
 *    Same polynomial family, different bit order; they do not agree on any
 *    non-empty input.
 */

/** Reflected CRC-32 (ISO-HDLC / zlib). Start with 0 for a fresh sum. */
uint32_t
RSCache_Crc32(
    uint32_t crc,
    const uint8_t* data,
    size_t length);

/** Convenience: reflected CRC-32 over one buffer. */
uint32_t
RSCache_Crc32Buffer(
    const uint8_t* data,
    size_t length);

/**
 * Unreflected CRC-32 as used inside bzip2 streams.
 *
 * Maintains bzip2's running form, so pass 0xFFFFFFFF to begin and complement the
 * result yourself (`~crc`) to finish — matching how the format composes block
 * CRCs into the stream CRC.
 */
uint32_t
RSCache_Crc32Bzip2(
    uint32_t crc,
    const uint8_t* data,
    size_t length);

/**
 * Whirlpool (512-bit), the digest a JS5 reference table stores per archive when
 * its flags carry the 0x2 bit.
 *
 * Writes 64 bytes to `out`. Needed only for caches that actually set that flag;
 * OSRS reference tables generally do not, and the encoder omits the field
 * entirely when the flag is clear.
 */
#define RSCACHE_WHIRLPOOL_DIGEST_SIZE 64

void
RSCache_Whirlpool(
    const uint8_t* data,
    size_t length,
    uint8_t out[RSCACHE_WHIRLPOOL_DIGEST_SIZE]);

#endif
