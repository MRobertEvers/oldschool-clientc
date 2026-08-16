/*
 * Whirlpool (512-bit hash), as specified by Barreto and Rijmen and used by JS5
 * reference tables when their flags carry the 0x2 bit.
 *
 * No cache in this repo actually sets that flag — a probe over cache, cache.643,
 * cache.jan2026, cache.kronos, cache.osrs184, cache.osrs230 and cache.osrs239
 * found zero tables with it — so this exists so the reference-table encoder can
 * round-trip a cache that *does*, rather than because anything here needs it.
 *
 * All tables are derived at first use from the specification's mini-box
 * construction, so there is no large literal table to mistranscribe. Verified
 * against the published test vectors for "", "abc" and the pangram (see
 * test_compression.c); those vectors fail loudly if any part of the derivation is
 * wrong, which is what makes a generated table safer here than a copied one.
 */

#include "checksum.h"

#include <string.h>

#define WHIRLPOOL_ROUNDS 10
#define WHIRLPOOL_BLOCK_BYTES 64
/* The message length is encoded in the trailing 256 bits of the final block. */
#define WHIRLPOOL_LENGTH_BYTES 32

static uint64_t whirlpool_circulant[8][256];
static uint64_t whirlpool_round_constant[WHIRLPOOL_ROUNDS + 1];
static int whirlpool_tables_ready = 0;

/** Multiply in GF(2^8) with Whirlpool's reduction polynomial 0x11D. */
static uint8_t
gf_multiply(
    uint8_t lhs,
    uint8_t rhs)
{
    unsigned product = 0;
    unsigned value = lhs;
    while( rhs )
    {
        if( rhs & 1u )
            product ^= value;
        rhs >>= 1;
        value <<= 1;
        if( value & 0x100u )
            value ^= 0x11Du;
    }
    return (uint8_t)product;
}

static void
whirlpool_build_tables(void)
{
    if( whirlpool_tables_ready )
        return;

    /* The 4-bit mini-boxes the S-box is built from. */
    static const uint8_t mini_e[16] = { 0x1, 0xB, 0x9, 0xC, 0xD, 0x6, 0xF, 0x3,
                                        0xE, 0x8, 0x7, 0x4, 0xA, 0x2, 0x5, 0x0 };
    static const uint8_t mini_r[16] = { 0x7, 0xC, 0xB, 0xD, 0xE, 0x4, 0x9, 0xF,
                                        0x6, 0x3, 0x8, 0xA, 0x2, 0x5, 0x1, 0x0 };
    uint8_t mini_e_inverse[16];
    for( int i = 0; i < 16; i++ )
        mini_e_inverse[mini_e[i]] = (uint8_t)i;

    uint8_t sbox[256];
    for( int high = 0; high < 16; high++ )
    {
        for( int low = 0; low < 16; low++ )
        {
            uint8_t y1 = mini_e[high];
            uint8_t y2 = mini_e_inverse[low];
            uint8_t mid = mini_r[y1 ^ y2];
            uint8_t z1 = mini_e[y1 ^ mid];
            uint8_t z2 = mini_e_inverse[y2 ^ mid];
            sbox[(high << 4) | low] = (uint8_t)((z1 << 4) | z2);
        }
    }

    /* The MDS layer is the circulant matrix cir(1, 1, 4, 1, 8, 5, 2, 9). Table 0
     * holds S[x] times each coefficient; tables 1..7 are rotations of it. */
    static const uint8_t circulant[8] = { 0x01, 0x01, 0x04, 0x01, 0x08, 0x05, 0x02, 0x09 };
    for( int x = 0; x < 256; x++ )
    {
        uint64_t word = 0;
        for( int k = 0; k < 8; k++ )
            word = (word << 8) | gf_multiply(sbox[x], circulant[k]);

        whirlpool_circulant[0][x] = word;
        for( int table = 1; table < 8; table++ )
            whirlpool_circulant[table][x] =
                (word >> (8 * table)) | (word << (64 - 8 * table));
    }

    /* Round constant r is the eight consecutive S-box entries starting at
     * 8*(r-1), packed big-endian. Zeroing all but the first byte here silently
     * produces a plausible-looking but wrong digest. */
    whirlpool_round_constant[0] = 0;
    for( int round = 1; round <= WHIRLPOOL_ROUNDS; round++ )
    {
        uint64_t value = 0;
        for( int k = 0; k < 8; k++ )
            value |= (uint64_t)sbox[8 * (round - 1) + k] << (56 - 8 * k);
        whirlpool_round_constant[round] = value;
    }

    whirlpool_tables_ready = 1;
}

/** One application of the block cipher W, folded into Miyaguchi-Preneel. */
static void
whirlpool_compress(
    uint64_t state_hash[8],
    const uint8_t block[WHIRLPOOL_BLOCK_BYTES])
{
    uint64_t plain[8];
    for( int i = 0; i < 8; i++ )
    {
        uint64_t word = 0;
        for( int b = 0; b < 8; b++ )
            word = (word << 8) | block[i * 8 + b];
        plain[i] = word;
    }

    uint64_t key[8];
    uint64_t state[8];
    for( int i = 0; i < 8; i++ )
    {
        key[i] = state_hash[i];
        state[i] = plain[i] ^ key[i];
    }

    for( int round = 1; round <= WHIRLPOOL_ROUNDS; round++ )
    {
        uint64_t next[8];

        /* Key schedule: the cipher applied to the key itself. */
        for( int i = 0; i < 8; i++ )
        {
            uint64_t word = 0;
            for( int k = 0; k < 8; k++ )
                word ^= whirlpool_circulant[k][(key[(i - k) & 7] >> (56 - 8 * k)) & 0xffu];
            next[i] = word;
        }
        next[0] ^= whirlpool_round_constant[round];
        for( int i = 0; i < 8; i++ )
            key[i] = next[i];

        /* Round transformation of the state under the new key. */
        for( int i = 0; i < 8; i++ )
        {
            uint64_t word = key[i];
            for( int k = 0; k < 8; k++ )
                word ^= whirlpool_circulant[k][(state[(i - k) & 7] >> (56 - 8 * k)) & 0xffu];
            next[i] = word;
        }
        for( int i = 0; i < 8; i++ )
            state[i] = next[i];
    }

    /* Miyaguchi-Preneel: H = W_H(m) ^ m ^ H. */
    for( int i = 0; i < 8; i++ )
        state_hash[i] ^= state[i] ^ plain[i];
}

void
RSCache_Whirlpool(
    const uint8_t* data,
    size_t length,
    uint8_t out[RSCACHE_WHIRLPOOL_DIGEST_SIZE])
{
    whirlpool_build_tables();

    uint64_t state_hash[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    size_t offset = 0;
    while( length - offset >= WHIRLPOOL_BLOCK_BYTES )
    {
        whirlpool_compress(state_hash, data + offset);
        offset += WHIRLPOOL_BLOCK_BYTES;
    }

    /* Final block(s): 0x80, zero padding, then the bit length in the last 32
     * bytes. When the remainder leaves no room for the length field, this spills
     * into a second block. */
    uint8_t tail[WHIRLPOOL_BLOCK_BYTES * 2];
    memset(tail, 0, sizeof(tail));

    size_t remainder = length - offset;
    if( remainder > 0 )
        memcpy(tail, data + offset, remainder);
    tail[remainder] = 0x80;

    size_t tail_blocks =
        (remainder + 1 + WHIRLPOOL_LENGTH_BYTES > WHIRLPOOL_BLOCK_BYTES) ? 2 : 1;
    size_t tail_size = tail_blocks * WHIRLPOOL_BLOCK_BYTES;

    /* Bit length, big-endian, right-aligned in the final block. Only the low 64
     * bits can be non-zero for any in-memory buffer. */
    uint64_t bit_length = (uint64_t)length * 8u;
    for( int i = 0; i < 8; i++ )
        tail[tail_size - 1 - (size_t)i] = (uint8_t)((bit_length >> (8 * i)) & 0xffu);

    for( size_t i = 0; i < tail_blocks; i++ )
        whirlpool_compress(state_hash, tail + i * WHIRLPOOL_BLOCK_BYTES);

    for( int i = 0; i < 8; i++ )
    {
        for( int b = 0; b < 8; b++ )
            out[i * 8 + b] = (uint8_t)((state_hash[i] >> (56 - 8 * b)) & 0xffu);
    }
}
