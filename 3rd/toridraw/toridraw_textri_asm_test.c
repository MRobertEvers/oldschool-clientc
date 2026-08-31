/*
 * tex_tri_i686.S against its C reference, framebuffer against framebuffer.
 *
 * The asm kernel is the WHOLE perspective-textured triangle -- vertex sort,
 * plane build, edge divides, both trapezoid walks and the span fill expanded
 * inline -- so, as with the gouraud kernel next door, there is no smaller unit
 * to check. The only honest test is to rasterize the same triangle twice and
 * compare every pixel.
 *
 * BIT-EXACT, not close. A textured triangle that lands one texel row off does
 * not read as a wrong pixel; it reads as a wrong-looking rock, which is the
 * streaking class of bug in docs/qbd_toridraw_streaks_debug.md. Both sides run
 * the same integer arithmetic -- the asm kept `idivl` rather than taking a
 * reciprocal precisely so this comparison could stay exact -- so any difference
 * at all is a bug in the walk, not a rounding difference.
 *
 * The generators matter more than the count. Uniform random triangles are
 * almost all interior, in front of the camera, and safely inside the plane's
 * 32-bit range; they would never reach the paths that actually break:
 *
 *   - the y pre-step and the y clamps, from vertices far off the viewport;
 *   - the flat-top branch, where y0 == y1 selects the other arm of the
 *     left-edge test that a random triangle hits with probability near zero;
 *   - the degenerate early return, where the C never divides and neither may
 *     the asm;
 *   - ToriDraw_TexturePlanePrepare32 rejecting the triangle outright, which is
 *     an early return in the middle of the prologue and easy to get wrong;
 *   - both texture widths, since the kernel dispatches on 64 vs 128 once per
 *     triangle rather than once per row.
 *
 * The framebuffer carries guard pages on both sides, filled with a distinctive
 * pattern and checked after every triangle: a walk that runs one row past the
 * bottom writes outside the picture, and comparing only the picture would call
 * that a pass.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

/* The support wrapper is gated on this; the test needs the symbol because the
 * kernel object it links against calls it. Defining it here also swaps
 * TORIDRAW_TEX_TRI_PERSP_OPAQUE over to the asm, which this file does not use:
 * it names both sides explicitly, on purpose, so the test cannot be fooled into
 * comparing one implementation against itself. */
#define TORIDRAW_TEXTRI_ASM 1

// clang-format off
#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "impl/projection/projection.scalar_reference.u.c"
#include "graphics/clamp.h"
#include "graphics/shade.h"
#include "impl/raster/span/span.tex.dispatch.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "graphics/raster/texture/tex_tri_asm.h"
#include "graphics/raster/texture/tex_tri_asm_support.u.c"
// clang-format on

#define W 137 /* deliberately not a multiple of 8: the span blocking is phased
               * from x_start, and a width that lined up with the block size
               * would hide a phase error at the right edge. */
#define H 91

/* STRIDE > W on purpose. The client's framebuffer row pitch is not always its
 * viewport width, and the kernel keeps the two in separate frame slots -- one
 * scales the row offset, the other clamps the span. A test that passed the same
 * number for both would score a kernel that had confused them as correct, and
 * the 12 columns between W and STRIDE are dead space the walk must never touch,
 * which the comparison below covers. */
#define STRIDE 149
#define PIXELS (STRIDE * H)
#define GUARD 2048
#define ALLOC (PIXELS + 2 * GUARD)

#define COT16 8192 /* what the client passes for a 512-wide viewport */

struct tri
{
    int x[3];
    int y[3];
    int ox[3];
    int oy[3];
    int oz[3];
    int shade[3];
    int texture_width;
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
 * Six screen-space shapes, cycled, each reaching something the others cannot.
 */
static void
generate(struct tri* t, int kind)
{
    int i;

    switch( kind % 6 )
    {
    case 0: /* fully interior: the common case, and the only one that runs the
             * walk for its whole height without a clamp. */
        for( i = 0; i < 3; i++ )
        {
            t->x[i] = range(4, W - 5);
            t->y[i] = range(4, H - 5);
        }
        break;

    case 1: /* far outside on every axis: drives the y pre-step, the y clamps,
             * and the per-row left/right clamps. */
        for( i = 0; i < 3; i++ )
        {
            t->x[i] = range(-4000, W + 4000);
            t->y[i] = range(-4000, H + 4000);
        }
        break;

    case 2: /* flat top or flat bottom. */
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

    case 3: /* degenerate: zero area, collinear, or a repeated vertex. */
        t->x[0] = range(0, W);
        t->y[0] = range(0, H);
        t->x[1] = t->x[0];
        t->y[1] = t->y[0];
        t->x[2] = range(0, W);
        t->y[2] = ( next() & 1 ) ? t->y[0] : range(0, H);
        break;

    case 4: /* slivers: one or two pixels wide, where the under-eight tail of
             * the fill runs and the block loop never does. */
        t->x[0] = range(0, W - 1);
        t->y[0] = range(0, H - 1);
        t->x[1] = t->x[0] + range(0, 2);
        t->y[1] = t->y[0] + range(1, 30);
        t->x[2] = t->x[0] + range(-2, 2);
        t->y[2] = t->y[1] + range(0, 30);
        break;

    default: /* wide and flat: many block iterations per row, so a drifting
              * plane accumulator shows up. */
        t->x[0] = range(-30, 10);
        t->y[0] = range(0, H - 1);
        t->x[1] = range(W - 10, W + 30);
        t->y[1] = t->y[0] + range(0, 3);
        t->x[2] = range(-30, W + 30);
        t->y[2] = t->y[0] + range(1, 6);
        break;
    }

    /*
     * The orthographic uv frame, independently of the screen shape.
     *
     * These are camera-space, and their magnitude is what decides whether
     * ToriDraw_TexturePlanePrepare32 normalises, shifts, or rejects. Three
     * bands, so all three outcomes happen: ordinary geometry, geometry close
     * enough to the camera that the plane terms grow, and deliberately huge
     * values that push the cross products toward the rejection test.
     */
    switch( next() % 3 )
    {
    case 0:
        for( i = 0; i < 3; i++ )
        {
            t->ox[i] = range(-2000, 2000);
            t->oy[i] = range(-2000, 2000);
            t->oz[i] = range(200, 6000);
        }
        break;
    case 1:
        for( i = 0; i < 3; i++ )
        {
            t->ox[i] = range(-100, 100);
            t->oy[i] = range(-100, 100);
            t->oz[i] = range(1, 120);
        }
        break;
    default:
        for( i = 0; i < 3; i++ )
        {
            t->ox[i] = range(-200000, 200000);
            t->oy[i] = range(-200000, 200000);
            t->oz[i] = range(-200000, 200000);
        }
        break;
    }

    /* 7-bit shades, plus out-of-range ones: the interpolant is not guaranteed
     * inside the table even when the vertices are, so the wrap has to run. */
    for( i = 0; i < 3; i++ )
        t->shade[i] = ( next() % 4 == 0 ) ? range(-3000, 3000) : range(0, 127);

    t->texture_width = ( next() & 1 ) ? 64 : 128;
}

static int
compare(const int* a, const int* b, const struct tri* t, int idx,
        const char* door)
{
    int i;

    for( i = 0; i < ALLOC; i++ )
    {
        if( a[i] != b[i] )
        {
            int p = i - GUARD;
            if( p < 0 || p >= PIXELS )
                printf("MISMATCH [%s] tri %d OUTSIDE the framebuffer, %d pixels "
                       "%s the picture: c=0x%08X asm=0x%08X\n",
                       door, idx, ( p < 0 ) ? -p : p - PIXELS + 1,
                       ( p < 0 ) ? "before" : "past",
                       (unsigned)a[i], (unsigned)b[i]);
            else
                printf("MISMATCH [%s] tri %d at (%d,%d)%s: c=0x%08X asm=0x%08X\n",
                       door, idx, p % STRIDE, p / STRIDE,
                       ( p % STRIDE >= W )
                           ? " -- PAST THE VIEWPORT, in the stride padding"
                           : "",
                       (unsigned)a[i], (unsigned)b[i]);
            printf("  screen (%d,%d) (%d,%d) (%d,%d)  tw=%d\n",
                   t->x[0], t->y[0], t->x[1], t->y[1], t->x[2], t->y[2],
                   t->texture_width);
            printf("  ortho  (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)\n",
                   t->ox[0], t->oy[0], t->oz[0],
                   t->ox[1], t->oy[1], t->oz[1],
                   t->ox[2], t->oy[2], t->oz[2]);
            printf("  shade  %d %d %d\n",
                   t->shade[0], t->shade[1], t->shade[2]);
            return 1;
        }
    }
    return 0;
}

int
main(int argc, char** argv)
{
    int iters = ( argc > 1 ) ? atoi(argv[1]) : 200000;
    int bad = 0;
    int bad_flat = 0;
    int i;
    /* Coverage, not correctness. Two implementations that both draw nothing
     * agree on every pixel, so the comparison alone cannot tell a working
     * kernel from a pair of early returns; these count what was actually
     * rasterized and fail the run if the generators stopped producing work. */
    int drew = 0;
    int flat_drew = 0;
    long pixels_drawn = 0;
    int* fb_c = malloc(sizeof(*fb_c) * ALLOC);
    int* fb_a = malloc(sizeof(*fb_a) * ALLOC);
    int* texels = malloc(sizeof(*texels) * 128 * 128);
    int fb_hint;

    assert(fb_c);
    assert(fb_a);
    assert(texels);

    init_hsl16_to_rgb_table();

    memset(&fb_hint, 0x5A, sizeof(fb_hint));

    /* A texture no two texels of which are equal, so a fetch that lands on the
     * wrong texel cannot accidentally read the right colour. */
    for( i = 0; i < 128 * 128; i++ )
        texels[i] = (int)( 0x00010203u * (unsigned)i + 0x00A5C300u );

    for( i = 0; i < iters; i++ )
    {
        struct tri t;

        generate(&t, i);

        /* A distinctive fill, not zero: a kernel that wrote nothing at all
         * would match a zeroed pair of buffers. */
        memset(fb_c, 0x5A, sizeof(*fb_c) * ALLOC);
        memset(fb_a, 0x5A, sizeof(*fb_a) * ALLOC);

        raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
            fb_c + GUARD, STRIDE, W, H, COT16,
            t.x[0], t.x[1], t.x[2],
            t.y[0], t.y[1], t.y[2],
            t.ox[0], t.ox[1], t.ox[2],
            t.oy[0], t.oy[1], t.oy[2],
            t.oz[0], t.oz[1], t.oz[2],
            t.shade[0], t.shade[1], t.shade[2],
            texels, t.texture_width);

        toridraw_textri_opaque_lerp8_v3_sorting_asm(
            fb_a + GUARD, STRIDE, W, H, COT16,
            t.x[0], t.x[1], t.x[2],
            t.y[0], t.y[1], t.y[2],
            t.ox[0], t.ox[1], t.ox[2],
            t.oy[0], t.oy[1], t.oy[2],
            t.oz[0], t.oz[1], t.oz[2],
            t.shade[0], t.shade[1], t.shade[2],
            texels, t.texture_width);

        {
            int p;
            int n = 0;
            for( p = 0; p < PIXELS; p++ )
                if( fb_c[GUARD + p] != fb_hint )
                    n++;
            if( n )
            {
                drew++;
                pixels_drawn += n;
            }
        }

        /* Counted always, printed for the first ten. Neither pass breaks the
         * loop any more: each door carries its own tally, so a knowingly
         * imprecise one cannot starve the other of samples. */
        if( memcmp(fb_c, fb_a, sizeof(*fb_c) * ALLOC) != 0 )
        {
            bad++;
            if( bad <= 10 )
                compare(fb_c, fb_a, &t, i, "blend");
        }
    }


    /*
     * Kernels 7 and 8 -- the flat doors -- in a loop of their own.
     *
     * WHAT IS BEING COMPARED. The reference is the blend ASM at equal shades,
     * which tex_tri_asm.h names as these kernels' reference, and not the C.
     * The two asm doors share every approximation in the prologue -- the
     * packed rcpps edge slopes above all -- so at s0 == s1 == s2 they owe
     * each other every pixel, and one differing pixel is a difference in the
     * walk. Held against the C, this pass would inherit the blend door's own
     * slop, which the comparison above already spends its error budget on.
     *
     * WHY A SEPARATE LOOP, AND WHY THE FLAT DOOR GOES FIRST IN IT. Both
     * doors build their frame at the same esp, so whichever runs second reads
     * back whatever the first left there. Run the blend door on triangle i
     * first and it leaves exactly the slopes and texture plane that a flat
     * face is supposed to compute for itself -- so a flat door that skipped
     * its entire prologue would inherit the right answer and pass. That is
     * not hypothetical: it is what the first two drafts of this pass did, and
     * both scored a broken kernel as correct. Replaying the same shapes from
     * the same seed, with the flat door leading, puts triangle i-1 in that
     * slot instead, which is what a run of faces hands it in the client.
     *
     * The bug this was written for: the flat doors set P_FLAT and branched
     * around the shade gradients, but the branch landed past the edge
     * divides, the nine cross products of the uv plane and the prepare call
     * as well. A flat face walked the previous triangle's edges into the
     * previous triangle's texture frame.
     */
    g_seed = 20260824u;
    for( i = 0; i < iters; i++ )
    {
        struct tri t;

        generate(&t, i);

        memset(fb_c, 0x5A, sizeof(*fb_c) * ALLOC);
        memset(fb_a, 0x5A, sizeof(*fb_a) * ALLOC);

        toridraw_textri_flat_opaque_lerp8_v3_sorting_asm(
            fb_a + GUARD, STRIDE, W, H, COT16,
            t.x[0], t.x[1], t.x[2],
            t.y[0], t.y[1], t.y[2],
            t.ox[0], t.ox[1], t.ox[2],
            t.oy[0], t.oy[1], t.oy[2],
            t.oz[0], t.oz[1], t.oz[2],
            t.shade[0], t.shade[0], t.shade[0],
            texels, t.texture_width);

        toridraw_textri_opaque_lerp8_v3_sorting_asm(
            fb_c + GUARD, STRIDE, W, H, COT16,
            t.x[0], t.x[1], t.x[2],
            t.y[0], t.y[1], t.y[2],
            t.ox[0], t.ox[1], t.ox[2],
            t.oy[0], t.oy[1], t.oy[2],
            t.oz[0], t.oz[1], t.oz[2],
            t.shade[0], t.shade[0], t.shade[0],
            texels, t.texture_width);

        {
            int p;
            int n = 0;
            for( p = 0; p < PIXELS; p++ )
                if( fb_c[GUARD + p] != fb_hint )
                    n++;
            if( n )
            {
                flat_drew++;
                pixels_drawn += n;
            }
        }

        if( memcmp(fb_c, fb_a, sizeof(*fb_c) * ALLOC) != 0 )
        {
            bad_flat++;
            if( bad_flat <= 10 )
                compare(fb_c, fb_a, &t, i, "flat");
        }
    }

    free(fb_c);
    free(fb_a);
    free(texels);

    if( bad_flat )
    {
        printf("FAIL: %d triangles where the flat door disagreed with the "
               "blend door at equal shades -- the two are the same walk, "
               "so they owe each other every pixel\n", bad_flat);
        return 1;
    }
    if( bad )
    {
        printf("FAIL: %d mismatching triangles\n", bad);
        return 1;
    }
    if( drew * 4 < iters )
    {
        printf("FAIL: only %d of %d triangles drew anything -- the generators "
               "or the plane setup stopped producing work, and a comparison of "
               "two early returns proves nothing\n", drew, iters);
        return 1;
    }
    if( flat_drew * 4 < iters )
    {
        printf("FAIL: only %d of %d flat triangles drew anything -- the flat "
               "door is being compared against an early return\n",
               flat_drew, iters);
        return 1;
    }
    printf("PASS: %d triangles x 2 doors, every pixel identical "
           "(%d blend, %d flat, %ld pixels)\n",
           iters, drew, flat_drew, pixels_drawn);
    return 0;
}
