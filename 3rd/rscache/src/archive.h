#ifndef RSCACHE_ARCHIVE_H
#define RSCACHE_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct RSCache_Dat2DiskArchive;

enum RSCache_ArchiveFormat
{
    RSCACHE_ARCHIVE_FORMAT_DAT2 = 0,
    RSCACHE_ARCHIVE_FORMAT_DAT = 1,
    RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE = 2,
};

bool
RSCache_ArchiveDecryptDecompress(
    struct RSCache_Dat2DiskArchive* archive,
    uint32_t* xtea_key_nullable);

/**
 * Determine the exact length of a framed dat2 container at the start of `data`.
 *
 * The returned length includes the compression byte, length field(s), and
 * compressed payload. It deliberately excludes any u32/u16 revision trailer.
 * `data_size` may therefore be larger than the returned length. Unknown
 * compression modes, truncated headers/payloads, and overflowing lengths fail.
 */
bool
RSCache_ArchiveRawContainerLength(
    const uint8_t* data,
    size_t data_size,
    size_t* out_container_size);

/**
 * Validate an exact raw dat2 archive against reference-table metadata.
 *
 * CRC-32 covers only the framed container, never its revision trailer. Modern
 * caches append the revision as a big-endian u32; legacy caches append its low
 * 16 bits as a big-endian u16. No missing trailer or trailing garbage is
 * accepted. `out_container_size` is optional and is written only on success.
 */
bool
RSCache_ArchiveRawValidate(
    const uint8_t* data,
    size_t data_size,
    uint32_t expected_crc,
    uint32_t expected_version,
    size_t* out_container_size);

bool
RSCache_ArchiveDecompressDat(
    struct RSCache_Dat2DiskArchive* archive,
    enum RSCache_ArchiveFormat format);

int
RSCache_ArchiveNameHashDat(const char* name);

int
RSCache_ArchiveNameHashDat2(char* name);

/** dat2 container compression byte. */
#define RSCACHE_ARCHIVE_COMPRESSION_NONE 0
#define RSCACHE_ARCHIVE_COMPRESSION_BZIP2 1
#define RSCACHE_ARCHIVE_COMPRESSION_GZIP 2

/**
 * Build a dat2 archive container around `data`.
 *
 * Layout, mirroring RSCache_ArchiveDecryptDecompress:
 *   u8  compression
 *   i32 compressed length  (payload bytes that follow the length fields)
 *   i32 uncompressed length — present only for compression != NONE
 *   payload
 *   [optional] revision trailer — u32 if >=4 bytes remain, else u16 if >=2
 *              (read on decompress; not written by this encoder)
 *
 * When `xtea_key_nullable` is given, everything from the uncompressed-length
 * field onward is encrypted, matching the decoder's `size + 4` span. XTEA works
 * in 8-byte blocks and leaves any trailing partial block in the clear, which is
 * what the reference client does too.
 *
 * Returns bytes written, or 0 on failure. Ask RSCache_ArchiveEncodeBound for a
 * safe size.
 */
uint32_t
RSCache_ArchiveEncode(
    uint8_t* out,
    uint32_t out_capacity,
    const uint8_t* data,
    uint32_t data_size,
    int compression,
    uint32_t* xtea_key_nullable);

/** Safe `out_capacity` for RSCache_ArchiveEncode at the given compression. */
uint32_t
RSCache_ArchiveEncodeBound(
    uint32_t data_size,
    int compression);

#endif
