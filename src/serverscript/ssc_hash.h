#ifndef SRC_SERVERSCRIPT_SSC_HASH_H
#define SRC_SERVERSCRIPT_SSC_HASH_H

/*
 * One 64-bit content hash for everything the incremental pack build compares:
 * source files, symbol answers, script bodies. Not cryptographic — it only has
 * to make an accidental collision between two versions of one unit absurd —
 * but it has to be fast enough that hashing the whole script tree (~45 MB) is
 * not the cost of a build. Eight bytes at a time through a multiply-xorshift
 * mix, then a finaliser.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SSC_HASH_SEED 0x9e3779b97f4a7c15ull

static inline uint64_t
ssc_hash_mix(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

static inline uint64_t
ssc_hash_bytes(const void* data, size_t length, uint64_t seed)
{
    const unsigned char* bytes = (const unsigned char*)data;
    uint64_t h = seed ^ (length * 0x9ddfea08eb382d69ull);
    size_t i = 0;

    for( ; i + 8 <= length; i += 8 )
    {
        uint64_t word;

        memcpy(&word, bytes + i, 8);
        h = (h ^ ssc_hash_mix(word)) * 0x9e3779b97f4a7c15ull;
    }
    if( i < length )
    {
        uint64_t word = 0;

        memcpy(&word, bytes + i, length - i);
        h = (h ^ ssc_hash_mix(word ^ 0x5bd1e995ull)) * 0x9e3779b97f4a7c15ull;
    }
    return ssc_hash_mix(h);
}

static inline uint64_t
ssc_hash_u64(uint64_t h, uint64_t value)
{
    return ssc_hash_mix((h ^ ssc_hash_mix(value + 0x632be59bd9b4e019ull)) * 0x9e3779b97f4a7c15ull);
}

static inline uint64_t
ssc_hash_str(uint64_t h, const char* text)
{
    size_t length = text ? strlen(text) : 0;

    return ssc_hash_u64(h, ssc_hash_bytes(text ? text : "", length, SSC_HASH_SEED));
}

#endif
