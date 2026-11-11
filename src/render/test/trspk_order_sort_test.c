/* The radix order must be the order a stable comparison sort gives.
 *
 * trspk_order_sort replaced qsort of the depth renderers' draw records, and
 * those records decide which runs merge into one draw. A wrong order does not
 * crash: it splits a draw, or merges ranges that are not contiguous and draws
 * the wrong vertices. So every case here is checked against a stable insertion
 * sort of the same words performed in this file -- keys of every width the
 * renderers use (16 bits for D3D9's segments, 36 for the GL lanes' ranges),
 * keys that share whole digits (the skipped passes), ties (stability), and
 * the empty and one-word frames.
 *
 *   make test-trspk-order-sort
 */
#include "core/trspk_order_sort.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static uint32_t g_rng = 0x9E3779B9u;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* The oracle: stable, by key only. */
static void
reference_sort(uint64_t* words, uint32_t count, uint32_t index_bits)
{
    for( uint32_t i = 1u; i < count; i++ )
    {
        uint64_t const w = words[i];
        uint32_t j = i;
        while( j > 0u && (words[j - 1u] >> index_bits) > (w >> index_bits) )
        {
            words[j] = words[j - 1u];
            j--;
        }
        words[j] = w;
    }
}

static void
check(
    const char* name,
    uint32_t count,
    uint32_t index_bits,
    uint32_t key_bits,
    uint64_t key_mask,
    uint64_t key_fixed)
{
    struct TRSPK_OrderBuffer buffer;
    uint64_t* expect = malloc((size_t)(count ? count : 1u) * sizeof(uint64_t));
    const uint64_t* got;
    if( !expect )
        abort();
    memset(&buffer, 0, sizeof(buffer));
    trspk_order_buffer_reserve(&buffer, count ? count : 1u);
    for( uint32_t i = 0u; i < count; i++ )
    {
        uint64_t key = ((((uint64_t)rnd() << 32) | rnd()) & key_mask) | key_fixed;
        if( key_bits < 64u )
            key &= (1ull << key_bits) - 1ull;
        buffer.words[i] = trspk_order_word(key, i, index_bits);
        expect[i] = buffer.words[i];
    }
    reference_sort(expect, count, index_bits);
    got = trspk_order_sort(&buffer, count, index_bits, key_bits);
    for( uint32_t i = 0u; i < count; i++ )
    {
        if( got[i] != expect[i] )
        {
            printf("FAIL %s: position %u holds index %u, want %u\n", name, i,
                trspk_order_index(got[i], index_bits), trspk_order_index(expect[i], index_bits));
            g_failures++;
            break;
        }
    }
    trspk_order_buffer_free(&buffer);
    free(expect);
}

int
main(void)
{
    /* The GL lanes' array ranges: cutout (bit 35), binding (32..34), first. */
    check("ranges, random", 5000u, 28u, 36u, (0xFull << 32) | 0xFFFFFFFFull, 0u);
    check("ranges, one binding", 5000u, 28u, 36u, 0xFFFFFull, 2ull << 32);
    check("ranges, ties", 5000u, 28u, 36u, 0x7ull, 0u);
    /* D3D9's page segments: min_vertex. */
    check("segments", 3000u, 32u, 16u, 0xFFFFull, 0u);
    check("segments, one digit", 3000u, 32u, 16u, 0xFFull, 0x4200u);
    check("empty", 0u, 28u, 36u, 0xFFFFull, 0u);
    check("one", 1u, 28u, 36u, 0xFFFFull, 0u);
    check("two", 2u, 32u, 16u, 0xFFFFull, 0u);
    printf("trspk_order_sort_test: %s\n", g_failures ? "FAIL" : "ok");
    return g_failures ? 1 : 0;
}
