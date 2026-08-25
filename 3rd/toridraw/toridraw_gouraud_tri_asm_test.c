/*
 * gouraud_tri_i686.S against its C reference, framebuffer against framebuffer.
 *
 * The asm kernel is the WHOLE triangle -- vertex sort, prologue, both trapezoid
 * walks and the span fill -- so there is no smaller unit to check and no way to
 * check it other than rasterizing the same triangle twice and comparing every
 * pixel. Anything less would leave the interesting failures (a mis-sorted
 * vertex, an edge stepped in the wrong order, a colour accumulator advanced on
 * the wrong rows) invisible.
 *
 * BIT-EXACT, not close. The 4-pixel colour quantization in
 * toridraw_gouraud_span_fill_short is visible output, already pinned by
 * toridraw_scanline_parity_test; a variant that re-blocked or shifted its phase
 * would pass a "looks the same" check and change what the client draws.
 *
 * The generators matter more than the count. Uniform random triangles are
 * almost all interior and would never reach the paths that actually break:
 * degenerate and flat-topped triangles, vertices far off every edge of the
 * viewport, single-pixel spans (37.3% of real spans are one pixel), and colours
 * chosen to drive the interpolant outside the palette so the clamp is exercised
 * in both directions.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.bary.branching.s4.c"

void toridraw_gouraud_tri_opaque_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16);

#define W 137 /* deliberately not a multiple of 4: the span blocking is phased
               * from x_start, and a width that lined up with the block size
               * would hide a phase error at the right edge. */
#define H 91

struct tri
{
    int x[3];
    int y[3];
    int c[3];
};

static unsigned g_seed = 20260824u;

static unsigned
next(void)
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

static int
range(int lo, int hi)
{
    return lo + (int)(next() % (unsigned)(hi - lo + 1));
}

/*
 * Six generators, cycled. Each one reaches something the others cannot.
 */
static void
generate(struct tri* t, int kind)
{
    int i;

    switch( kind % 6 )
    {
    case 0: /* fully interior: the common case, and the only one that runs the
             * proved-no-clip loop for its whole height. */
        for( i = 0; i < 3; i++ )
        {
            t->x[i] = range(4, W - 5);
            t->y[i] = range(4, H - 5);
        }
        break;

    case 1: /* far outside on every axis: drives the y pre-step, the y clamps,
             * and the left/right clamps, including edges whose 16.16 slope
             * product overflows. */
        for( i = 0; i < 3; i++ )
        {
            t->x[i] = range(-4000, W + 4000);
            t->y[i] = range(-4000, H + 4000);
        }
        break;

    case 2: /* flat top or flat bottom: y0 == y1 selects the other half of the
             * left-edge test, which a random triangle hits with probability
             * near zero. */
        t->x[0] = range(-20, W + 20);
        t->x[1] = range(-20, W + 20);
        t->x[2] = range(-20, W + 20);
        t->y[0] = range(-10, H + 10);
        t->y[1] = t->y[0];
        t->y[2] = range(-10, H + 10);
        if( next() & 1 )
        {
            int s = t->y[0];
            t->y[0] = t->y[2];
            t->y[2] = s;
            t->y[1] = t->y[2];
        }
        break;

    case 3: /* degenerate: zero area, collinear, or a repeated vertex. The
             * kernel must take the same early return the C does, not divide. */
        t->x[0] = range(0, W);
        t->y[0] = range(0, H);
        t->x[1] = t->x[0];
        t->y[1] = t->y[0];
        t->x[2] = range(0, W);
        t->y[2] = (next() & 1) ? t->y[0] : range(0, H);
        break;

    case 4: /* slivers: one or two pixels wide, which is where most real spans
             * live and where the under-four tail runs. */
        t->x[0] = range(0, W - 1);
        t->y[0] = range(0, H - 1);
        t->x[1] = t->x[0] + range(0, 2);
        t->y[1] = t->y[0] + range(1, 30);
        t->x[2] = t->x[0] + range(-2, 2);
        t->y[2] = t->y[1] + range(0, 30);
        break;

    default: /* tall and thin the other way: wide flat spans that run the block
              * loop many times, so a drifting colour accumulator shows up. */
        t->x[0] = range(-30, 10);
        t->y[0] = range(0, H - 1);
        t->x[1] = range(W - 10, W + 30);
        t->y[1] = t->y[0] + range(0, 3);
        t->x[2] = range(-30, W + 30);
        t->y[2] = t->y[0] + range(1, 6);
        break;
    }

    for( i = 0; i < 3; i++ )
    {
        switch( next() % 4 )
        {
        case 0:
            t->c[i] = range(0, 0xFFFF); /* in range */
            break;
        case 1:
            t->c[i] = range(0, 200); /* near the bottom of the palette */
            break;
        case 2:
            t->c[i] = range(0xFF00, 0xFFFF); /* near the top */
            break;
        default:
            /* Out of range on purpose. The interpolant is not guaranteed inside
             * the palette even when the vertices are, so the clamp has to work;
             * feeding it out-of-range vertices makes sure both arms of it run. */
            t->c[i] = range(-40000, 105000);
            break;
        }
    }
}

static int
compare(const toripixel_t* a, const toripixel_t* b, const struct tri* t, int idx)
{
    int i;

    for( i = 0; i < W * H; i++ )
    {
        if( a[i] != b[i] )
        {
            printf("MISMATCH tri %d at (%d,%d): c=0x%08X asm=0x%08X\n",
                   idx, i % W, i / W, (unsigned)a[i], (unsigned)b[i]);
            printf("  v0=(%d,%d,%d) v1=(%d,%d,%d) v2=(%d,%d,%d)\n",
                   t->x[0], t->y[0], t->c[0],
                   t->x[1], t->y[1], t->c[1],
                   t->x[2], t->y[2], t->c[2]);
            return 1;
        }
    }
    return 0;
}

int
main(int argc, char** argv)
{
    int iters = (argc > 1) ? atoi(argv[1]) : 200000;
    int bad = 0;
    int i;
    toripixel_t* fb_c = malloc(sizeof(*fb_c) * W * H);
    toripixel_t* fb_a = malloc(sizeof(*fb_a) * W * H);

    assert(fb_c);
    assert(fb_a);

    init_hsl16_to_rgb_table();

    for( i = 0; i < iters; i++ )
    {
        struct tri t;

        generate(&t, i);

        /* A distinctive fill, not zero: a kernel that wrote nothing at all
         * would match a zeroed pair of buffers. */
        memset(fb_c, 0x5A, sizeof(*fb_c) * W * H);
        memset(fb_a, 0x5A, sizeof(*fb_a) * W * H);

        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            fb_c, W, W, H,
            t.x[0], t.x[1], t.x[2],
            t.y[0], t.y[1], t.y[2],
            t.c[0], t.c[1], t.c[2]);

        toridraw_gouraud_tri_opaque_s4_asm(
            fb_a, W, W, H,
            t.x[0], t.x[1], t.x[2],
            t.y[0], t.y[1], t.y[2],
            t.c[0], t.c[1], t.c[2]);

        if( compare(fb_c, fb_a, &t, i) )
        {
            bad++;
            if( bad >= 10 )
                break;
        }
    }

    free(fb_c);
    free(fb_a);

    if( bad )
    {
        printf("FAIL: %d mismatching triangles\n", bad);
        return 1;
    }
    printf("PASS: %d triangles, every pixel identical\n", iters);
    return 0;
}
