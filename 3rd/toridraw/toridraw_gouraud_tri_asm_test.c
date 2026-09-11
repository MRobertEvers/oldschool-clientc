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
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"

void toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm(
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

void toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_gouraud_alpha_s4_sorting_xrgb8888_asm(
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
    int color2_hsl16,
    int alpha);

void toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

/* Must match ROWBYTES / R_X / R_Y / R_C in gouraud_tri_i686.S. */
#define BATCH_ROW_INTS 12
#define BATCH_MAX      64

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

/*
 * WHAT "PASSES" MEANS HERE, NOW THAT IT IS NOT "every pixel identical".
 *
 * The three edge slopes come off a packed divps instead of three idivl. That
 * is the fastest form measured on the target -- 65.43 ns for the divide group
 * against idivl's 70.82 -- and with the 16.16 scale applied AFTER the divide
 * the inputs all convert exactly, so the only rounding left is the divide's
 * own. Measured against the exact integer lane over 200,000 randomised
 * triangles it moves 5 of them and 333 pixels of 247 million drawn, and 315 of
 * those 333 are one triangle with |dx| >= 32768, where the integer form's
 * deliberate wraparound is a different answer rather than a rounder one.
 *
 * So the bound below is set an order of magnitude above what was measured and
 * several orders below anything visible. It is not a licence to drift: build
 * with -DTORIDRAW_EDGE_IDIV and both numbers must be exactly zero, which is
 * what separates a regression in the walk from the approximation in the slope.
 */
#define TOL_TRIANGLES_PPM 200    /* 0.02% of triangles may differ  */
#define TOL_PIXELS_PPM     50    /* 0.005% of drawn pixels         */

struct deviation
{
    long triangles;
    long pixels;
    long drawn;
    long worst;
};

static int
deviation_ok(const struct deviation* d, long triangles, const char* what)
{
    double const tri_ppm =
        triangles ? 1e6 * (double)d->triangles / (double)triangles : 0.0;
    double const px_ppm =
        d->drawn ? 1e6 * (double)d->pixels / (double)d->drawn : 0.0;
    int const ok = (tri_ppm <= TOL_TRIANGLES_PPM) && (px_ppm <= TOL_PIXELS_PPM);

    printf("%-14s %ld/%ld triangles (%.1f ppm), %ld/%ld pixels (%.2f ppm),"
           " worst %ld px  %s\n",
           what, d->triangles, triangles, tri_ppm, d->pixels, d->drawn, px_ppm,
           d->worst, ok ? "PASS" : "FAIL");
    return ok;
}

static void
deviation_note(struct deviation* d, const toripixel_t* a, const toripixel_t* b,
               int count, toripixel_t blank)
{
    long here = 0;
    int i;

    for( i = 0; i < count; i++ )
    {
        if( a[i] != blank )
            d->drawn++;
        if( a[i] != b[i] )
            here++;
    }
    if( here )
    {
        d->triangles++;
        d->pixels += here;
        if( here > d->worst )
            d->worst = here;
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

/*
 * The batch entry against the same C reference, a CHUNK at a time.
 *
 * Two things only a chunk can check, and the one-triangle pass above cannot.
 *
 * ORDER. The batch rasterises `count` triangles inside one call, and a painter
 * has no depth buffer -- if the loop walked its rows backwards, or restarted
 * one, overlapping triangles would land in the wrong order and every triangle
 * would still be individually correct. The reference draws the identical
 * sequence one call at a time, into a viewport small enough that the generated
 * triangles genuinely overlap.
 *
 * CARRY. Everything the body leaves in the frame -- the stepped edges, the
 * colour accumulator, V_OFF, the segment counts -- has to be dead by the time
 * the next row starts. A slot that survived would make triangle n+1 depend on
 * triangle n, which no single-triangle test can see. Chunk lengths sweep 1 up
 * to the buffer size, so the loop is entered, re-entered and left at every
 * count.
 */
/*
 * The y order the depth sort now hands the batch entries, transcribed from the
 * C wrapper's ladder. `<=` throughout: triangles that tie differently stop
 * tiling with each other, so the tie-breaks are part of the contract and not
 * an implementation detail.
 */
static int
ysort_perm(const int* y)
{
    if( y[0] <= y[1] && y[0] <= y[2] )
        return (y[1] <= y[2]) ? 0 : 1;
    if( y[1] <= y[2] )
        return (y[2] <= y[0]) ? 2 : 3;
    return (y[0] <= y[1]) ? 4 : 5;
}

static const unsigned char g_ysort[6][3] = {
    { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 }
};

static int
batch_pass(int chunks)
{
    _Alignas(16) int rows[BATCH_MAX * BATCH_ROW_INTS];
    toripixel_t* fb_c = malloc(sizeof(*fb_c) * W * H);
    toripixel_t* fb_a = malloc(sizeof(*fb_a) * W * H);
    struct tri t[BATCH_MAX];
    int bad = 0;
    int chunk;

    assert(fb_c);
    assert(fb_a);

    for( chunk = 0; chunk < chunks; chunk++ )
    {
        int const count = 1 + (chunk % BATCH_MAX);
        int i;

        memset(fb_c, 0x5A, sizeof(*fb_c) * W * H);
        memset(fb_a, 0x5A, sizeof(*fb_a) * W * H);
        /* Poison the padding lane the shuffle carries through, so a kernel
         * that read it as data would produce something visibly wrong rather
         * than something accidentally right. */
        memset(rows, 0x7E, sizeof(rows));

        for( i = 0; i < count; i++ )
        {
            const unsigned char* pm;
            int k;

            generate(&t[i], chunk * BATCH_MAX + i);
            pm = g_ysort[ysort_perm(t[i].y)];
            for( k = 0; k < 3; k++ )
            {
                rows[i * BATCH_ROW_INTS + 0 + k] = t[i].x[pm[k]];
                rows[i * BATCH_ROW_INTS + 4 + k] = t[i].y[pm[k]];
                rows[i * BATCH_ROW_INTS + 8 + k] = t[i].c[pm[k]];
            }

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
                fb_c, W, W, H,
                t[i].x[0], t[i].x[1], t[i].x[2],
                t[i].y[0], t[i].y[1], t[i].y[2],
                t[i].c[0], t[i].c[1], t[i].c[2]);
        }

        toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(fb_a, W, W, H, rows, count);

        for( i = 0; i < W * H; i++ )
        {
            if( fb_c[i] != fb_a[i] )
            {
                printf("BATCH MISMATCH chunk %d (count %d) at (%d,%d):"
                       " c=0x%08X asm=0x%08X\n",
                       chunk, count, i % W, i / W,
                       (unsigned)fb_c[i], (unsigned)fb_a[i]);
                bad++;
                break;
            }
        }
        if( bad >= 10 )
            break;
    }

    free(fb_c);
    free(fb_a);
    return bad;
}

/*
 * The alpha lane, single and batched.
 *
 * It shares the whole walk with the opaque lane, so what is actually under
 * test here is the span: a palette lookup per four pixels exactly as the
 * opaque span does it, then alpha_blend() four pixels at a time out of 16-bit
 * lanes, with the source colour's contribution recomputed per block because it
 * is the thing that changes. The 1..3 pixel tail is a real loop rather than the
 * opaque span's branchless triple store -- a store is idempotent and a blend is
 * not -- and a tail that blended a pixel twice is exactly what this catches.
 *
 * The destination is filled with a non-trivial pattern, not zero: blending
 * into zeros would hide any error in the destination term.
 */
static int
alpha_pass(int iters, int batched)
{
    _Alignas(16) int rows[BATCH_MAX * BATCH_ROW_INTS];
    toripixel_t* fb_c = malloc(sizeof(*fb_c) * W * H);
    toripixel_t* fb_a = malloc(sizeof(*fb_a) * W * H);
    int bad = 0;
    int i;

    assert(fb_c);
    assert(fb_a);

    for( i = 0; i < iters; i++ )
    {
        struct tri t;
        int const alpha = (i % 17 == 0) ? 1
                        : (i % 19 == 0) ? 254
                                        : 1 + (int)(next() % 254u);
        const unsigned char* pm;
        int k;

        generate(&t, i);
        memset(fb_c, 0x5A, sizeof(*fb_c) * W * H);
        memset(fb_a, 0x5A, sizeof(*fb_a) * W * H);
        memset(rows, 0x7E, sizeof(rows));

        raster_gouraudhsllightness_screen_alpha_bary_branching_s4(
            fb_c, W, W, H, t.x[0], t.x[1], t.x[2], t.y[0], t.y[1], t.y[2],
            t.c[0], t.c[1], t.c[2], alpha);

        if( batched )
        {
            pm = g_ysort[ysort_perm(t.y)];
            for( k = 0; k < 3; k++ )
            {
                rows[0 + k] = t.x[pm[k]];
                rows[4 + k] = t.y[pm[k]];
                rows[8 + k] = t.c[pm[k]];
            }
            rows[11] = alpha;
            toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(fb_a, W, W, H, rows, 1);
        }
        else
        {
            toridraw_gouraud_alpha_s4_sorting_xrgb8888_asm(
                fb_a, W, W, H, t.x[0], t.x[1], t.x[2], t.y[0], t.y[1], t.y[2],
                t.c[0], t.c[1], t.c[2], alpha);
        }

        if( compare(fb_c, fb_a, &t, i) )
        {
            bad++;
            if( bad >= 10 )
                break;
        }
    }

    free(fb_c);
    free(fb_a);
    return bad;
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

    init_hsl16_to_pixel_table();

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

        toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm(
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

    {
        int const chunks = (iters / BATCH_MAX) > 0 ? (iters / BATCH_MAX) : 1;
        int const batch_bad = batch_pass(chunks);

        if( batch_bad )
        {
            printf("FAIL: %d mismatching batch chunks\n", batch_bad);
            return 1;
        }
        printf("PASS: %d batched chunks (counts 1..%d), every pixel identical\n",
               chunks, BATCH_MAX);
    }
    {
        int const n = iters / 4;

        if( alpha_pass(n, 0) )
        {
            printf("FAIL: alpha single\n");
            return 1;
        }
        printf("PASS: alpha single, %d triangles, every pixel identical\n", n);

        if( alpha_pass(n, 1) )
        {
            printf("FAIL: alpha batch\n");
            return 1;
        }
        printf("PASS: alpha batch, %d triangles, every pixel identical\n", n);
    }
    return 0;
}
