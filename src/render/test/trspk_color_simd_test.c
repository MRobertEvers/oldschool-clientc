/* What the colour kernel actually has to guarantee.
 *
 * Not bit-equality with the scalar lane -- a reciprocal multiply is free to
 * land an ulp off a divide, and no consumer can tell. What every consumer DOES
 * rely on is that a colour survives the trip: unpack a byte to a float, pack
 * it back through (x * 255 + 0.5) truncation, and get the same byte. That is
 * the property that keeps a vertex colour identical after the bake stopped
 * dividing, and it is what this pins.
 *
 * The second thing it pins is channel order, because the SSE2 lane gets its
 * four lanes out of a byte interleave in B,G,R,A order and shuffles them back.
 * A shuffle that returns the right four values in the wrong order is the one
 * bug an all-channels-equal input cannot see.
 *
 *   make test-trspk-color-simd
 */
#include "core/trspk_color_simd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Comfortably inside half a byte step (1/510), so an implementation that is
 * merely a rounding apart passes and one that is a channel out does not. */
#define TOLERANCE (1.0f / 4096.0f)

static int g_failures = 0;

static float
absf(float v)
{
    return v < 0.0f ? -v : v;
}

static void
check_reference(uint32_t argb)
{
    float rgba[4];
    float want[4];
    int i;

    trspk_color_unpack_argb(argb, rgba);
    want[0] = (float)((argb >> 16) & 0xFFu) / 255.0f;
    want[1] = (float)((argb >> 8) & 0xFFu) / 255.0f;
    want[2] = (float)(argb & 0xFFu) / 255.0f;
    want[3] = (float)((argb >> 24) & 0xFFu) / 255.0f;

    for( i = 0; i < 4; i++ )
    {
        if( absf(rgba[i] - want[i]) > TOLERANCE )
        {
            printf("  FAIL 0x%08x lane %d: kernel %.9g want %.9g\n",
                (unsigned)argb, i, rgba[i], want[i]);
            g_failures++;
            return;
        }
    }
}

static void
check_round_trips(uint32_t argb)
{
    float rgba[4];
    uint32_t back;

    trspk_color_unpack_argb(argb, rgba);
    back = ((uint32_t)(rgba[3] * 255.0f + 0.5f) << 24) |
        ((uint32_t)(rgba[0] * 255.0f + 0.5f) << 16) |
        ((uint32_t)(rgba[1] * 255.0f + 0.5f) << 8) |
        (uint32_t)(rgba[2] * 255.0f + 0.5f);
    if( back != argb )
    {
        printf("  FAIL round trip 0x%08x -> 0x%08x\n", (unsigned)argb, (unsigned)back);
        g_failures++;
    }
}

int
main(void)
{
    unsigned v;
    unsigned w;

    printf("trspk-color-simd\n");

    /* Every byte value in every position, and every colour whose channels are
     * all equal -- then the round trip over the same sweep. */
    for( v = 0u; v < 256u; v++ )
    {
        check_reference((uint32_t)v);
        check_reference((uint32_t)v << 8);
        check_reference((uint32_t)v << 16);
        check_reference((uint32_t)v << 24);
        check_reference(v * 0x01010101u);

        check_round_trips(v * 0x01010101u);
        check_round_trips((uint32_t)v << 16);
        check_round_trips(0xFF000000u | (uint32_t)v);
    }

    /* Every (r,g) pair with distinct b and a, so a lane swap has nowhere to
     * hide across the whole byte range, not just at the corners. */
    for( v = 0u; v < 256u; v++ )
        for( w = 0u; w < 256u; w++ )
            check_round_trips(0x5A000000u | (v << 16) | (w << 8) | 0xC3u);

    check_reference(0x00000000u);
    check_reference(0xFFFFFFFFu);
    check_reference(0x12345678u);
    check_round_trips(0x12345678u);

    {
        /* Channel order, stated outright rather than inferred. */
        float rgba[4];
        trspk_color_unpack_argb(0x11223344u, rgba);
        if( (uint32_t)(rgba[0] * 255.0f + 0.5f) != 0x22u ||
            (uint32_t)(rgba[1] * 255.0f + 0.5f) != 0x33u ||
            (uint32_t)(rgba[2] * 255.0f + 0.5f) != 0x44u ||
            (uint32_t)(rgba[3] * 255.0f + 0.5f) != 0x11u )
        {
            printf("  FAIL channel order for 0x11223344\n");
            g_failures++;
        }
        else
            printf("  ok   channel order is r,g,b,a\n");
    }

    {
        float rgba[4];
        trspk_color_unpack_rgb_alpha(0x00223344u, 0x11u, rgba);
        if( (uint32_t)(rgba[3] * 255.0f + 0.5f) != 0x11u )
        {
            printf("  FAIL rgb+alpha form ignored its alpha\n");
            g_failures++;
        }
        else
            printf("  ok   rgb+alpha form takes the supplied alpha\n");
    }

    if( g_failures )
    {
        printf("%d failures\n", g_failures);
        return 1;
    }
    printf("  ok   within tolerance of the divide, every byte value\n");
    printf("  ok   every colour round trips back to its own bytes\n");
    printf("All trspk colour SIMD tests passed.\n");
    return 0;
}
