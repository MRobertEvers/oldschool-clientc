/*
 * flat_tri_i686.S against its C references, framebuffer against framebuffer.
 *
 * Four entries to score, and each one can fail in a way the others cannot:
 *
 *   opaque single   the walk itself -- sort, edge slopes, vertical clip, the
 *                   two trapezoids -- plus a fill that aligns itself before it
 *                   widens, so the interesting counts are the ones either side
 *                   of the eight-pixel threshold and every phase of %edi
 *                   modulo four.
 *   alpha single    the same walk plus the blend. alpha_blend() is reproduced
 *                   four pixels at a time out of 16-bit lanes, with the source
 *                   term folded into a per-triangle constant; a channel that
 *                   carried where it should not, or did not carry where it
 *                   should, shows up here and nowhere else.
 *   opaque batch    order and carry across a run: nothing the body leaves in
 *                   the frame may reach the next triangle.
 *   alpha batch     the same, plus per-row alpha, since a run may mix them.
 *
 * BIT-EXACT, not close. These are the kernels the client actually draws with,
 * and the flat faces they cover sit against gouraud and textured ones that
 * have their own pinned kernels -- a flat face that landed one column off, or
 * one shade out, would tile visibly against its neighbours.
 *
 * The generators are the ones from toridraw_gouraud_tri_asm_test.c, for the
 * same reason: uniform random triangles are almost all interior and would
 * never reach the degenerate, flat-topped, sliver and fully-offscreen cases
 * where a hand-written walk actually breaks. Two are added here that the
 * gouraud test does not need -- wide triangles, which are the only ones that
 * reach the aligned block loop at all, and full-width ones, which put its
 * lead-in at every possible phase.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "graphics/raster/flat/flat.screen.opaque.branching.s4.c"
#include "graphics/raster/flat/flat.screen.alpha.branching.s4.c"

void toridraw_flat_tri_opaque_s4_asm(
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
    int color_hsl16);

void toridraw_flat_tri_alpha_s4_asm(
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
    int color_hsl16,
    int alpha);

void toridraw_flat_batch_opaque_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_flat_batch_alpha_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

/* Must match ROWBYTES / R_X / R_Y / R_C / R_A in flat_tri_i686.S. */
#define ROW_INTS  12
#define BATCH_MAX 64

#define W 137 /* deliberately not a multiple of 4: the fill blocks from the
               * destination pointer, and a width that lined up with the block
               * size would hide a phase error at the right edge. */
#define H 91

struct tri
{
    int x[3];
    int y[3];
    int color;
    int alpha;
};

static unsigned g_seed = 20260827u;

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
 * Eight generators, cycled. Each one reaches something the others cannot.
 */
static void
generate(struct tri* t, int i)
{
    int k = i % 8;
    int j;

    for( j = 0; j < 3; j++ )
    {
        switch( k )
        {
        case 0: /* interior */
            t->x[j] = range(0, W - 1);
            t->y[j] = range(0, H - 1);
            break;
        case 1: /* straddling every edge */
            t->x[j] = range(-W, 2 * W);
            t->y[j] = range(-H, 2 * H);
            break;
        case 2: /* slivers: one-pixel spans, no block loop at all */
            t->x[j] = range(40, 44);
            t->y[j] = range(0, H - 1);
            break;
        case 3: /* flat-topped and flat-bottomed */
            t->x[j] = range(0, W - 1);
            t->y[j] = (j < 2) ? 20 : range(0, H - 1);
            break;
        case 4: /* far off-viewport */
            t->x[j] = range(-4000, 4000);
            t->y[j] = range(-4000, 4000);
            break;
        case 5: /* tiny */
            t->x[j] = range(60, 63);
            t->y[j] = range(40, 43);
            break;
        case 6: /* wide: long spans, so the aligned block loop actually runs */
            t->x[j] = (j == 0) ? 2 : ((j == 1) ? W - 3 : range(0, W - 1));
            t->y[j] = range(0, H - 1);
            break;
        default: /* full width, every lead-in phase */
            t->x[j] = (j == 0) ? -20 : ((j == 1) ? W + 20 : range(-20, W + 20));
            t->y[j] = range(-2, H + 2);
            break;
        }
    }
    t->color = range(0, 65535);
    /* 0 and 255 are the ends the blend degenerates at; the rest is spread. */
    t->alpha = (i % 17 == 0) ? 0 : ((i % 19 == 0) ? 255 : range(1, 254));
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
compare(const toripixel_t* a, const toripixel_t* b, const struct tri* t,
        const char* what, int idx)
{
    int i;

    for( i = 0; i < W * H; i++ )
    {
        if( a[i] != b[i] )
        {
            printf("MISMATCH %s %d at (%d,%d): c=0x%08X asm=0x%08X\n",
                   what, idx, i % W, i / W, (unsigned)a[i], (unsigned)b[i]);
            printf("  v0=(%d,%d) v1=(%d,%d) v2=(%d,%d) color=%d alpha=%d\n",
                   t->x[0], t->y[0], t->x[1], t->y[1], t->x[2], t->y[2],
                   t->color, t->alpha);
            return 1;
        }
    }
    return 0;
}

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

static void
stage(int* rows, int i, const struct tri* t)
{
    const unsigned char* pm = g_ysort[ysort_perm(t->y)];
    int k;

    for( k = 0; k < 3; k++ )
    {
        rows[i * ROW_INTS + 0 + k] = t->x[pm[k]];
        rows[i * ROW_INTS + 4 + k] = t->y[pm[k]];
    }
    /* A flat face has one colour, so nothing to permute. */
    rows[i * ROW_INTS + 8] = t->color;
    rows[i * ROW_INTS + 9] = t->alpha;
}

static int
single_pass(int iters, int alpha_lane)
{
    toripixel_t* fb_c = malloc(sizeof(*fb_c) * W * H);
    toripixel_t* fb_a = malloc(sizeof(*fb_a) * W * H);
    int bad = 0;
    int i;

    assert(fb_c);
    assert(fb_a);

    for( i = 0; i < iters; i++ )
    {
        struct tri t;

        generate(&t, i);

        /* A distinctive fill, not zero: a kernel that wrote nothing at all
         * would match a zeroed pair of buffers. It also gives the blend a
         * non-trivial destination to read. */
        memset(fb_c, 0x5A, sizeof(*fb_c) * W * H);
        memset(fb_a, 0x5A, sizeof(*fb_a) * W * H);

        if( alpha_lane )
        {
            raster_flat_screen_alpha_branching_s4(
                fb_c, W, W, H, t.x[0], t.x[1], t.x[2], t.y[0], t.y[1], t.y[2],
                t.color, t.alpha);
            toridraw_flat_tri_alpha_s4_asm(
                fb_a, W, W, H, t.x[0], t.x[1], t.x[2], t.y[0], t.y[1], t.y[2],
                t.color, t.alpha);
        }
        else
        {
            raster_flat_screen_opaque_branching_s4(
                fb_c, W, W, H, t.x[0], t.x[1], t.x[2], t.y[0], t.y[1], t.y[2],
                t.color);
            toridraw_flat_tri_opaque_s4_asm(
                fb_a, W, W, H, t.x[0], t.x[1], t.x[2], t.y[0], t.y[1], t.y[2],
                t.color);
        }

        if( compare(fb_c, fb_a, &t, alpha_lane ? "alpha tri" : "opaque tri", i) )
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

static int
batch_pass(int chunks, int alpha_lane)
{
    _Alignas(16) int rows[BATCH_MAX * ROW_INTS];
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
        /* Poison the padding lanes the shuffle carries through, so a kernel
         * that read one as data would produce something visibly wrong rather
         * than something accidentally right. */
        memset(rows, 0x7E, sizeof(rows));

        for( i = 0; i < count; i++ )
        {
            generate(&t[i], chunk * BATCH_MAX + i);
            stage(rows, i, &t[i]);

            if( alpha_lane )
                raster_flat_screen_alpha_branching_s4(
                    fb_c, W, W, H, t[i].x[0], t[i].x[1], t[i].x[2],
                    t[i].y[0], t[i].y[1], t[i].y[2], t[i].color, t[i].alpha);
            else
                raster_flat_screen_opaque_branching_s4(
                    fb_c, W, W, H, t[i].x[0], t[i].x[1], t[i].x[2],
                    t[i].y[0], t[i].y[1], t[i].y[2], t[i].color);
        }

        if( alpha_lane )
            toridraw_flat_batch_alpha_s4_asm(fb_a, W, W, H, rows, count);
        else
            toridraw_flat_batch_opaque_s4_asm(fb_a, W, W, H, rows, count);

        for( i = 0; i < W * H; i++ )
        {
            if( fb_c[i] != fb_a[i] )
            {
                printf("BATCH MISMATCH %s chunk %d (count %d) at (%d,%d):"
                       " c=0x%08X asm=0x%08X\n",
                       alpha_lane ? "alpha" : "opaque", chunk, count,
                       i % W, i / W, (unsigned)fb_c[i], (unsigned)fb_a[i]);
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

int
main(int argc, char** argv)
{
    int const iters = (argc > 1) ? atoi(argv[1]) : 200000;
    int const chunks = (iters / BATCH_MAX) > 0 ? (iters / BATCH_MAX) : 1;
    int lane;

    init_hsl16_to_rgb_table();

    for( lane = 0; lane < 2; lane++ )
    {
        char const* name = lane ? "alpha" : "opaque";

        if( single_pass(iters, lane) )
        {
            printf("FAIL: %s single\n", name);
            return 1;
        }
        printf("PASS: %s single, %d triangles, every pixel identical\n",
               name, iters);

        if( batch_pass(chunks, lane) )
        {
            printf("FAIL: %s batch\n", name);
            return 1;
        }
        printf("PASS: %s batch, %d chunks (counts 1..%d), every pixel identical\n",
               name, chunks, BATCH_MAX);
    }
    return 0;
}
