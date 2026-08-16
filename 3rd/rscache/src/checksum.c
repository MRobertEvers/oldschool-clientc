#include "checksum.h"

/*
 * The two CRC-32 variants a cache uses. Both derive from the same polynomial but
 * in opposite bit orders, so they are built here side by side to keep the
 * distinction in view — see checksum.h.
 */

static uint32_t crc32_reflected_table[256];
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
        crc32_reflected_table[i] = reg;
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
     * algorithm needs at both ends is kept internal. */
    uint32_t reg = ~crc;
    for( size_t i = 0; i < length; i++ )
        reg = crc32_reflected_table[(reg ^ data[i]) & 0xffu] ^ (reg >> 8);
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
