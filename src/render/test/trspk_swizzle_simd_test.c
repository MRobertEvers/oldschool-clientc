/* The SSE2 swizzle must reproduce the per-pixel rule exactly.
 *
 * Unlike the colour kernel, there is no tolerance here: this is a byte
 * shuffle, so "close" is meaningless -- every pixel must come out bit for bit
 * identical to trspk_swizzle_pixel, which is the definition.
 *
 * The cases that matter are the alpha rule's corners (a zero alpha byte with
 * and without colour) and the tail, because the vector loop runs four at a
 * time and hands the remainder back to the scalar rule; a length that is not a
 * multiple of four is where an off-by-one lives.
 *
 *   make test-trspk-swizzle-simd
 */
#include "core/trspk_swizzle_simd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PIXELS 1024

static int g_failures = 0;

static uint32_t g_rng = 0xC0FFEEu;

static uint32_t
next_rand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static void
check_buffer(uint32_t const* src, size_t count, const char* what)
{
    uint32_t got[MAX_PIXELS];
    size_t i;

    trspk_swizzle_argb_to_abgr(src, got, count);
    for( i = 0; i < count; i++ )
    {
        uint32_t const want = trspk_swizzle_pixel(src[i]);
        if( got[i] != want )
        {
            printf("  FAIL %s: [%u] 0x%08x -> 0x%08x want 0x%08x\n",
                what, (unsigned)i, (unsigned)src[i], (unsigned)got[i], (unsigned)want);
            g_failures++;
            return;
        }
    }
}

int
main(void)
{
    static uint32_t src[MAX_PIXELS];
    static uint32_t inplace[MAX_PIXELS];
    unsigned v;
    size_t len;
    size_t i;

    printf("trspk-swizzle-simd\n");

    /* Every byte value in every position, four lanes wide so each lane of the
     * vector sees a different pixel. */
    for( v = 0u; v < 256u; v++ )
    {
        src[0] = (uint32_t)v;
        src[1] = (uint32_t)v << 8;
        src[2] = (uint32_t)v << 16;
        src[3] = (uint32_t)v << 24;
        check_buffer(src, 4u, "byte sweep");
    }

    /* The alpha rule's two corners, interleaved so no lane is uniform:
     * alpha 0 with colour must become opaque, alpha 0 without colour must
     * stay transparent. */
    src[0] = 0x00000000u; /* no alpha, no colour -> stays 0 */
    src[1] = 0x00123456u; /* no alpha, has colour -> becomes opaque */
    src[2] = 0x7F000000u; /* alpha, no colour */
    src[3] = 0xFF654321u; /* alpha and colour */
    src[4] = 0x00FF0000u; /* red only, no alpha */
    src[5] = 0x000000FFu; /* blue only, no alpha */
    src[6] = 0x0000FF00u; /* green only, no alpha */
    src[7] = 0x01000001u;
    check_buffer(src, 8u, "alpha corners");

    /* Every tail length, so the remainder handoff is covered. */
    for( i = 0; i < MAX_PIXELS; i++ )
        src[i] = next_rand();
    for( len = 0u; len <= 64u; len++ )
        check_buffer(src, len, "tail length");

    check_buffer(src, MAX_PIXELS, "bulk random");

    /* In place, which is how the GL lanes actually call it. */
    memcpy(inplace, src, sizeof(inplace));
    trspk_swizzle_argb_to_abgr(inplace, inplace, MAX_PIXELS);
    for( i = 0; i < MAX_PIXELS; i++ )
    {
        uint32_t const want = trspk_swizzle_pixel(src[i]);
        if( inplace[i] != want )
        {
            printf("  FAIL in place: [%u] 0x%08x want 0x%08x\n",
                (unsigned)i, (unsigned)inplace[i], (unsigned)want);
            g_failures++;
            break;
        }
    }

    {
        /* Channel order, stated outright. */
        uint32_t one = 0xFF112233u;
        uint32_t out;
        trspk_swizzle_argb_to_abgr(&one, &out, 1u);
        if( out != 0xFF332211u )
        {
            printf("  FAIL channel order: 0x%08x\n", (unsigned)out);
            g_failures++;
        }
        else
            printf("  ok   R and B exchange, G and A stay\n");
    }

    if( g_failures )
    {
        printf("%d failures\n", g_failures);
        return 1;
    }
    printf("  ok   matches the scalar rule on every byte value\n");
    printf("  ok   alpha corners and every tail length\n");
    printf("  ok   correct in place\n");
    printf("All trspk swizzle SIMD tests passed.\n");
    return 0;
}
