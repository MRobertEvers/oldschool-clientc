#include "checksum.h"

/*
 * The two CRC-32 variants a cache uses. Both derive from the same polynomial but
 * in opposite bit orders, so they are built here side by side to keep the
 * distinction in view — see checksum.h.
 */

/*
 * The reflected table is four slices deep rather than one: slice k holds the
 * CRC contribution of a byte that still has k bytes behind it, which lets the
 * main loop fold four input bytes per round instead of one.
 *
 * Four and not eight. Slicing-by-8 is the usual choice and is roughly twice as
 * fast again on a desktop, but its tables are 8 KB, and the reason this kernel
 * is being touched at all is the i686/XP launch profile — 8 KB is half the L1
 * data cache of the CPUs that column describes, and evicting it to checksum a
 * cache group would cost the rest of the launch path more than the CRC saves.
 * 4 KB is the compromise that keeps the win without owning the cache.
 *
 * Slice 0 is the ordinary byte-at-a-time table, so the tail loop and the
 * incremental path are unchanged by any of this.
 */
#define CRC32_SLICES 4

static uint32_t crc32_reflected_table[CRC32_SLICES][256];
static uint32_t crc32_bzip2_table[256];
static int crc32_tables_ready = 0;

static void
crc32_build_tables(void)
{
    if( crc32_tables_ready )
        return;

    /* Reflected (LSB-first): poly 0xEDB88320, the bit-reverse of 0x04C11DB7. */
    for( uint32_t i = 0; i < 256; i++ )
    {
        uint32_t reg = i;
        for( int bit = 0; bit < 8; bit++ )
            reg = (reg & 1u) ? ((reg >> 1) ^ 0xEDB88320u) : (reg >> 1);
        crc32_reflected_table[0][i] = reg;
    }

    /* Slice k is slice k-1 advanced by one more byte position: shift the byte
     * out of the low end and fold it back through slice 0. */
    for( int slice = 1; slice < CRC32_SLICES; slice++ )
    {
        for( uint32_t i = 0; i < 256; i++ )
        {
            uint32_t prev = crc32_reflected_table[slice - 1][i];
            crc32_reflected_table[slice][i] =
                (prev >> 8) ^ crc32_reflected_table[0][prev & 0xffu];
        }
    }

    /* Unreflected (MSB-first): poly 0x04C11DB7, indexed by the top byte. */
    for( uint32_t i = 0; i < 256; i++ )
    {
        uint32_t reg = i << 24;
        for( int bit = 0; bit < 8; bit++ )
            reg = (reg & 0x80000000u) ? ((reg << 1) ^ 0x04C11DB7u) : (reg << 1);
        crc32_bzip2_table[i] = reg;
    }

    crc32_tables_ready = 1;
}

uint32_t
RSCache_Crc32(
    uint32_t crc,
    const uint8_t* data,
    size_t length)
{
    crc32_build_tables();

    /* Callers pass a plain running value (0 to start); the inversion that the
     * algorithm needs at both ends is kept internal. Because both inversions
     * live inside one call, feeding this a byte at a time still composes to the
     * same answer as one shot — test_compression.c pins that. */
    uint32_t reg = ~crc;
    size_t i = 0;

    /*
     * Short inputs take the plain loop, which is the case that actually shows up
     * here: a 300-frame embedded-server launch makes 20,165 calls totalling
     * 386 KB, a mean of 19 bytes each. At that size the wide path folds four or
     * five times — far too few rounds to pay back touching four tables instead
     * of one, and on the i686 target that extra 3 KB of resident table is taken
     * from a cache the rest of the launch path is competing for. The threshold
     * is what keeps this change from being a pessimisation on the workload it
     * was aimed at.
     *
     * 64 is not tuned; it is the point past which the slice tables are read
     * enough times to be worth their footprint under any plausible cache. The
     * bulk callers this exists for — whole-container validation — are orders of
     * magnitude above it, so nothing hinges on the exact value.
     */
    while( length >= 64u && length - i >= 4u )
    {
        /* Little-endian load, assembled from bytes rather than read through a
         * uint32_t: `data` carries no alignment guarantee, and the byte order
         * has to be fixed rather than the host's. Both compilers this tree uses
         * fold the pattern back into a single load on x86. */
        uint32_t v = ((uint32_t)data[i]) | ((uint32_t)data[i + 1] << 8) |
                     ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
        v ^= reg;
        reg = crc32_reflected_table[3][v & 0xffu] ^
              crc32_reflected_table[2][(v >> 8) & 0xffu] ^
              crc32_reflected_table[1][(v >> 16) & 0xffu] ^
              crc32_reflected_table[0][(v >> 24) & 0xffu];
        i += 4u;
    }

    for( ; i < length; i++ )
        reg = crc32_reflected_table[0][(reg ^ data[i]) & 0xffu] ^ (reg >> 8);
    return ~reg;
}

uint32_t
RSCache_Crc32Buffer(
    const uint8_t* data,
    size_t length)
{
    return RSCache_Crc32(0, data, length);
}

uint32_t
RSCache_Crc32Bzip2(
    uint32_t crc,
    const uint8_t* data,
    size_t length)
{
    crc32_build_tables();

    /* bzip2's own running form: the caller seeds 0xFFFFFFFF and complements the
     * result, because the format composes block CRCs into the stream CRC by
     * rotating and xoring these raw values. */
    for( size_t i = 0; i < length; i++ )
        crc = (crc << 8) ^ crc32_bzip2_table[((crc >> 24) ^ data[i]) & 0xffu];
    return crc;
}
