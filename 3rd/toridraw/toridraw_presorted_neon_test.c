/*
 * The eight AArch64/NEON presorted-run kernels against the C references they
 * replace, framebuffer against framebuffer, every pixel identical.
 *
 *   flat_tri_aarch64.S      opaque + alpha        vs raster_flat_screen_*_branching_s4
 *   gouraud_tri_aarch64.S   opaque + alpha        vs raster_gouraudhsllightness_screen_*_bary_branching_s4
 *   tex_tri_aarch64.S       opaque/trans x blend/flat
 *                                                 vs raster_texshadeblend_persp_tex{opaque,trans}_branching_lerp8_v3
 *
 * BIT-EXACT, not close. Unlike the i686 kernels these take exact `sdiv`
 * edge slopes and the same double-reciprocal colour steps the C computes, so
 * there is no rounding budget to spend: any differing pixel is a bug in the
 * walk. The textured pair mirrors the NEON C span (tex.span.neon.u.c), float
 * reciprocal, saturating narrow and all, because that C is what the macOS
 * build draws with.
 *
 * Only the RUN doors exist on this lane, so every triangle goes through a
 * batch -- runs of 1..64 rows -- and the C reference draws the same
 * triangles one at a time in the same order. That scores the walk, the
 * fill, the draw order across a run, and the per-row alpha/gate/shade lanes
 * in one comparison. Rows are staged already in y order, exactly as the
 * depth sort stages them, with the C's `<=` tie-breaks.
 *
 * The generators are the ones the i686 tests use, for the same reason:
 * uniform random triangles are almost all interior and never reach the
 * degenerate, flat-topped, sliver, wide, full-width and far-offscreen cases
 * where a hand-written walk actually breaks. The textured pass adds the
 * three orthographic bands that make ToriDraw_TexturePlanePrepare32
 * normalise, shift and reject.
 *
 * The framebuffers carry guard bands on both sides, checked with the picture:
 * a walk that ran one row past the bottom would write outside the viewport,
 * and a picture-only comparison would score that as a pass.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#define TORIDRAW_TEXTRI_NEON_ASM 1

// clang-format off
#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "impl/projection/projection.scalar_reference.u.c"
#include "graphics/clamp.h"
#include "graphics/shade.h"
#include "impl/raster/flat/raster.flat.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/flat/raster.flat.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/span/span.tex.dispatch.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "graphics/raster/texture/tex_tri_asm.h"
#include "graphics/raster/texture/tex_tri_asm_support.u.c"
#include "graphics/raster/flat/flat_tri_asm.h"
#include "graphics/raster/gouraudhsllightness/gouraud_tri_asm.h"
// clang-format on

#ifndef TEST_FLAT
#define TEST_FLAT 1
#endif
#ifndef TEST_GOURAUD
#define TEST_GOURAUD 1
#endif
#ifndef TEST_TEX
#define TEST_TEX 1
#endif

#define W 137 /* not a multiple of 4 or 8: the fills block from x_start, and a
               * width that lined up with the block would hide a phase error
               * at the right edge. */
#define H 91
/* STRIDE > W: the row pitch and the clip width are two different numbers in
 * every kernel, and a test that passed the same value for both would score a
 * kernel that confused them as correct. */
#define STRIDE 149
#define PIXELS (STRIDE * H)
#define GUARD 2048
#define ALLOC (PIXELS + 2 * GUARD)

#define BATCH_MAX 64
#define COT16 8192

struct tri
{
    int x[3];
    int y[3];
    int c[3];
    int alpha;
    /* textured */
    int ox[3];
    int oy[3];
    int oz[3];
    int shade[3];
    int texture_width;
};

static unsigned g_seed = 20260828u;

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

/* Eight screen-space generators, cycled. Each reaches something the others
 * cannot; see the i686 tests for the argument behind each. */
static void
generate_screen(struct tri* t, int i)
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
        case 2: /* slivers */
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
        case 6: /* wide */
            t->x[j] = (j == 0) ? 2 : ((j == 1) ? W - 3 : range(0, W - 1));
            t->y[j] = range(0, H - 1);
            break;
        default: /* full width, every lead-in phase */
            t->x[j] = (j == 0) ? -20 : ((j == 1) ? W + 20 : range(-20, W + 20));
            t->y[j] = range(-2, H + 2);
            break;
        }
        t->c[j] = range(0, 65535);
    }
    /* 0 and 255 are the ends the blend degenerates at; the rest is spread. */
    t->alpha = (i % 17 == 0) ? 0 : ((i % 19 == 0) ? 255 : range(1, 254));
}

static void
generate_texture(struct tri* t)
{
    int i;

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
    for( i = 0; i < 3; i++ )
        t->shade[i] = (next() % 4 == 0) ? range(-3000, 3000) : range(0, 127);
    t->texture_width = (next() & 1) ? 64 : 128;
}

/*
 * The y order the depth sort hands the batch entries, `<=` tie-breaks
 * included: triangles that tie differently stop tiling with each other, so
 * this is part of the contract.
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
compare(const int* a, const int* b, const struct tri* t, int idx, const char* door)
{
    int i;

    for( i = 0; i < ALLOC; i++ )
    {
        if( a[i] != b[i] )
        {
            int p = i - GUARD;
            if( p < 0 || p >= PIXELS )
                printf("MISMATCH [%s] tri %d OUTSIDE the framebuffer, %d pixels %s the "
                       "picture: c=0x%08X asm=0x%08X\n",
                       door, idx, (p < 0) ? -p : p - PIXELS + 1,
                       (p < 0) ? "before" : "past", (unsigned)a[i], (unsigned)b[i]);
            else
                printf("MISMATCH [%s] tri %d at (%d,%d)%s: c=0x%08X asm=0x%08X\n",
                       door, idx, p % STRIDE, p / STRIDE,
                       (p % STRIDE >= W) ? " -- PAST THE VIEWPORT, in the stride padding"
                                         : "",
                       (unsigned)a[i], (unsigned)b[i]);
            printf("  screen (%d,%d) (%d,%d) (%d,%d)  c %d %d %d  alpha %d\n",
                   t->x[0], t->y[0], t->x[1], t->y[1], t->x[2], t->y[2],
                   t->c[0], t->c[1], t->c[2], t->alpha);
            printf("  ortho  (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)  shade %d %d %d  tw=%d\n",
                   t->ox[0], t->oy[0], t->oz[0], t->ox[1], t->oy[1], t->oz[1],
                   t->ox[2], t->oy[2], t->oz[2], t->shade[0], t->shade[1],
                   t->shade[2], t->texture_width);
            return 1;
        }
    }
    return 0;
}

static long
count_drawn(const int* fb, int hint)
{
    long n = 0;
    int p;
    for( p = 0; p < PIXELS; p++ )
        if( fb[GUARD + p] != hint )
            n++;
    return n;
}

enum door
{
    DOOR_FLAT_OPAQUE,
    DOOR_FLAT_ALPHA,
    DOOR_GOURAUD_OPAQUE,
    DOOR_GOURAUD_ALPHA,
    DOOR_TEX_OPAQUE,
    DOOR_TEX_TRANS,
    DOOR_TEX_FLAT_OPAQUE,
    DOOR_TEX_FLAT_TRANS,
    DOOR_COUNT
};

static const char* const g_door_name[DOOR_COUNT] = {
    "flat opaque",     "flat alpha",      "gouraud opaque",   "gouraud alpha",
    "tex opaque blend", "tex trans blend", "tex opaque flat", "tex trans flat",
};

static void
stage_solid(int* rows, int i, const struct tri* t, enum door door)
{
    const unsigned char* pm = g_ysort[ysort_perm(t->y)];
    int* row = rows + i * TORIDRAW_GOURAUD_RUN_ROW_INTS;
    int k;

    for( k = 0; k < 3; k++ )
    {
        row[0 + k] = t->x[pm[k]];
        row[4 + k] = t->y[pm[k]];
    }
    if( door == DOOR_FLAT_OPAQUE || door == DOOR_FLAT_ALPHA )
    {
        row[8] = t->c[0];
        row[9] = t->alpha;
    }
    else
    {
        for( k = 0; k < 3; k++ )
            row[8 + k] = t->c[pm[k]];
        row[11] = t->alpha;
    }
}

static void
stage_tex(int* rows, int i, const struct tri* t, enum door door, int* texels)
{
    const unsigned char* pm = g_ysort[ysort_perm(t->y)];
    int* row = rows + i * TORIDRAW_RASTER_TEXBATCH_ROW_INTS;
    int flat = (door == DOOR_TEX_FLAT_OPAQUE || door == DOOR_TEX_FLAT_TRANS);
    int k;

    for( k = 0; k < 3; k++ )
    {
        row[TORIDRAW_TEXBATCH_LANE_X + k] = t->x[pm[k]];
        row[TORIDRAW_TEXBATCH_LANE_Y + k] = t->y[pm[k]];
        /* The texture frame is three ROLES, not the triangle's corners: it
         * is not permuted with them. */
        row[TORIDRAW_TEXBATCH_LANE_OX + k] = t->ox[k];
        row[TORIDRAW_TEXBATCH_LANE_OY + k] = t->oy[k];
        row[TORIDRAW_TEXBATCH_LANE_OZ + k] = t->oz[k];
        row[TORIDRAW_TEXBATCH_LANE_SHADE + k] = flat ? t->shade[0] : t->shade[pm[k]];
    }
    TORIDRAW_TEXBATCH_SET_TEXELS(row, texels);
    row[TORIDRAW_TEXBATCH_LANE_TW] = t->texture_width;
    row[TORIDRAW_TEXBATCH_LANE_GATE] =
        (door == DOOR_TEX_TRANS || door == DOOR_TEX_FLAT_TRANS) ? 1 : 0;
}

static void
reference(int* fb, const struct tri* t, enum door door, int* texels)
{
    int* pix = fb + GUARD;
    switch( door )
    {
    case DOOR_FLAT_OPAQUE:
        raster_flat_screen_opaque_branching_s4(
            pix, STRIDE, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2],
            t->c[0]);
        break;
    case DOOR_FLAT_ALPHA:
        raster_flat_screen_alpha_branching_s4(
            pix, STRIDE, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2],
            t->c[0], t->alpha);
        break;
    case DOOR_GOURAUD_OPAQUE:
        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            pix, STRIDE, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2],
            t->c[0], t->c[1], t->c[2]);
        break;
    case DOOR_GOURAUD_ALPHA:
        raster_gouraudhsllightness_screen_alpha_bary_branching_s4(
            pix, STRIDE, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2],
            t->c[0], t->c[1], t->c[2], t->alpha);
        break;
    case DOOR_TEX_OPAQUE:
        raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
            pix, STRIDE, W, H, COT16, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1],
            t->y[2], t->ox[0], t->ox[1], t->ox[2], t->oy[0], t->oy[1], t->oy[2],
            t->oz[0], t->oz[1], t->oz[2], t->shade[0], t->shade[1], t->shade[2],
            texels, t->texture_width);
        break;
    case DOOR_TEX_TRANS:
        raster_texshadeblend_persp_textrans_branching_lerp8_v3(
            pix, STRIDE, W, H, COT16, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1],
            t->y[2], t->ox[0], t->ox[1], t->ox[2], t->oy[0], t->oy[1], t->oy[2],
            t->oz[0], t->oz[1], t->oz[2], t->shade[0], t->shade[1], t->shade[2],
            texels, t->texture_width);
        break;
    case DOOR_TEX_FLAT_OPAQUE:
        /* The flat kernels' definition: the blend kernel at equal shades,
         * which is also what the per-face C path calls. */
        raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
            pix, STRIDE, W, H, COT16, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1],
            t->y[2], t->ox[0], t->ox[1], t->ox[2], t->oy[0], t->oy[1], t->oy[2],
            t->oz[0], t->oz[1], t->oz[2], t->shade[0], t->shade[0], t->shade[0],
            texels, t->texture_width);
        break;
    case DOOR_TEX_FLAT_TRANS:
        raster_texshadeblend_persp_textrans_branching_lerp8_v3(
            pix, STRIDE, W, H, COT16, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1],
            t->y[2], t->ox[0], t->ox[1], t->ox[2], t->oy[0], t->oy[1], t->oy[2],
            t->oz[0], t->oz[1], t->oz[2], t->shade[0], t->shade[0], t->shade[0],
            texels, t->texture_width);
        break;
    default:
        assert(0);
    }
}

static void
run_door(int* fb, const int* rows, int count, enum door door)
{
    int* pix = fb + GUARD;
    switch( door )
    {
    case DOOR_FLAT_OPAQUE:
        toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm(pix, STRIDE, W, H, rows, count);
        break;
    case DOOR_FLAT_ALPHA:
        toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm(pix, STRIDE, W, H, rows, count);
        break;
#if TEST_GOURAUD
    case DOOR_GOURAUD_OPAQUE:
        toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(pix, STRIDE, W, H, rows, count);
        break;
    case DOOR_GOURAUD_ALPHA:
        toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(pix, STRIDE, W, H, rows, count);
        break;
#endif
#if TEST_TEX
    case DOOR_TEX_OPAQUE:
        toridraw_textri_opaque_lerp8_v3_presorted_run_xrgb8888_asm(
            pix, STRIDE, W, H, COT16, rows, count);
        break;
    case DOOR_TEX_TRANS:
        toridraw_textri_trans_lerp8_v3_presorted_run_xrgb8888_asm(
            pix, STRIDE, W, H, COT16, rows, count);
        break;
    case DOOR_TEX_FLAT_OPAQUE:
        toridraw_textri_flat_opaque_lerp8_v3_presorted_run_xrgb8888_asm(
            pix, STRIDE, W, H, COT16, rows, count);
        break;
    case DOOR_TEX_FLAT_TRANS:
        toridraw_textri_flat_trans_lerp8_v3_presorted_run_xrgb8888_asm(
            pix, STRIDE, W, H, COT16, rows, count);
        break;
#endif
    default:
        assert(0);
    }
}

/*
 * WHAT "PASSES" MEANS HERE.
 *
 * The three edge slopes come off a packed NEON reciprocal, the WinXP kernels'
 * EDGESLOPES3 ladder, so the kernels are NOT bit-identical to the C and this
 * test scores them the way toridraw_flat_tri_asm_test.c scores the i686
 * ladder: a triangle and pixel budget an order of magnitude above what was
 * measured and several orders below anything visible. It is not a licence to
 * drift. Assemble the kernels with -DTORIDRAW_EDGE_IDIV and build this test
 * with -DTORIDRAW_PRESORTED_EXACT, and both numbers must be exactly zero --
 * which is what separates a regression in the walk from the approximation
 * in the slope. The textured kernels carry two more approximations the C
 * does not (the i686 kernel's rcpss reciprocal of w and its shared
 * reciprocal of sarea), so they never reach zero and are held to a budget of
 * their own.
 */
#ifdef TORIDRAW_PRESORTED_EXACT
#define TOL_TRIANGLES_PPM 0
#define TOL_PIXELS_PPM 0
#define TOL_TEX_TRIANGLES_PPM 0
#define TOL_TEX_PIXELS_PPM 0
#else
#define TOL_TRIANGLES_PPM 2000    /* 0.2% of triangles may differ; the
                                   * reciprocal arm measures up to 650  */
#define TOL_PIXELS_PPM 200        /* 0.02% of drawn pixels; measured 72 */
#define TOL_TEX_TRIANGLES_PPM 20000
#define TOL_TEX_PIXELS_PPM 2000
#endif

struct deviation
{
    long triangles;   /* triangles whose standalone raster differs        */
    long total;       /* triangles scored                                 */
    long pixels;      /* differing pixels, over those triangles           */
    long drawn;       /* pixels the reference drew                        */
    long worst;       /* most differing pixels in one triangle            */
    long runs;        /* runs that differed                               */
};

static int
door_pass(enum door door, int chunks, int* texels)
{
    static _Alignas(16) int rows[BATCH_MAX * TORIDRAW_RASTER_TEXBATCH_ROW_INTS];
    int* fb_c = malloc(sizeof(*fb_c) * ALLOC);
    int* fb_a = malloc(sizeof(*fb_a) * ALLOC);
    struct tri t[BATCH_MAX];
    int tex = door >= DOOR_TEX_OPAQUE;
    int row_ints = tex ? TORIDRAW_RASTER_TEXBATCH_ROW_INTS : TORIDRAW_GOURAUD_RUN_ROW_INTS;
    struct deviation d;
    int printed = 0;
    int chunk;
    long drew = 0;
    int fb_hint;
    double tri_ppm;
    double px_ppm;
    int tol_tri = tex ? TOL_TEX_TRIANGLES_PPM : TOL_TRIANGLES_PPM;
    int tol_px = tex ? TOL_TEX_PIXELS_PPM : TOL_PIXELS_PPM;
    int ok;

    assert(fb_c);
    assert(fb_a);
    memset(&fb_hint, 0x5A, sizeof(fb_hint));
    memset(&d, 0, sizeof(d));

    for( chunk = 0; chunk < chunks; chunk++ )
    {
        int const count = 1 + (chunk % BATCH_MAX);
        int i;

        /* A distinctive fill, not zero: a kernel that wrote nothing at all
         * would match a zeroed pair of buffers, and the blends need a
         * non-trivial destination. */
        memset(fb_c, 0x5A, sizeof(*fb_c) * ALLOC);
        memset(fb_a, 0x5A, sizeof(*fb_a) * ALLOC);
        /* Poison the padding lanes, so a kernel that read one as data would
         * produce something visibly wrong rather than accidentally right. */
        memset(rows, 0x7E, sizeof(rows));

        for( i = 0; i < count; i++ )
        {
            generate_screen(&t[i], chunk * BATCH_MAX + i);
            if( tex )
            {
                generate_texture(&t[i]);
                stage_tex(rows, i, &t[i], door, texels);
            }
            else
                stage_solid(rows, i, &t[i], door);
            reference(fb_c, &t[i], door, texels);
        }

        run_door(fb_a, rows, count, door);

        d.total += count;
        d.drawn += count_drawn(fb_c, fb_hint);
        if( d.drawn )
            drew++;

        if( memcmp(fb_c, fb_a, sizeof(*fb_c) * ALLOC) == 0 )
            continue;
        d.runs++;

        /* The run differed. Replay it one row at a time to attribute the
         * difference to triangles and pixels: the batch is what is scored,
         * but the budget is per triangle and the offending geometry is what
         * a fix needs. A row that writes OUTSIDE the picture is never
         * tolerated, whatever the budget. */
        for( i = 0; i < count; i++ )
        {
            int p;
            long here = 0;
            int outside = 0;

            memset(fb_c, 0x5A, sizeof(*fb_c) * ALLOC);
            memset(fb_a, 0x5A, sizeof(*fb_a) * ALLOC);
            reference(fb_c, &t[i], door, texels);
            run_door(fb_a, rows + i * row_ints, 1, door);
            for( p = 0; p < ALLOC; p++ )
            {
                if( fb_c[p] == fb_a[p] )
                    continue;
                here++;
                if( p < GUARD || p >= GUARD + PIXELS || (p - GUARD) % STRIDE >= W )
                    outside = 1;
            }
            if( !here )
                continue;
            d.triangles++;
            d.pixels += here;
            if( here > d.worst )
                d.worst = here;
            if( outside || printed < 3 )
            {
                compare(fb_c, fb_a, &t[i], chunk * BATCH_MAX + i, g_door_name[door]);
                printed++;
            }
            if( outside )
            {
                printf("FAIL: %-17s wrote outside the viewport\n", g_door_name[door]);
                free(fb_c);
                free(fb_a);
                return 1;
            }
        }
    }

    free(fb_c);
    free(fb_a);

    tri_ppm = d.total ? 1e6 * (double)d.triangles / (double)d.total : 0.0;
    px_ppm = d.drawn ? 1e6 * (double)d.pixels / (double)d.drawn : 0.0;
    ok = tri_ppm <= (double)tol_tri && px_ppm <= (double)tol_px;

    if( drew * 4 < chunks )
    {
        printf("FAIL: %-17s only %ld of %d runs drew anything -- a comparison of two "
               "early returns proves nothing\n",
               g_door_name[door], drew, chunks);
        return 1;
    }
    printf("%s: %-17s %ld/%ld triangles (%.1f ppm), %ld/%ld pixels (%.2f ppm), "
           "worst %ld px, %ld/%d runs  [budget %d / %d ppm]\n",
           ok ? "PASS" : "FAIL", g_door_name[door], d.triangles, d.total, tri_ppm,
           d.pixels, d.drawn, px_ppm, d.worst, d.runs, chunks, tol_tri, tol_px);
    return ok ? 0 : 1;
}

int
main(int argc, char** argv)
{
    int const iters = (argc > 1) ? atoi(argv[1]) : 100000;
    int const chunks = (iters / BATCH_MAX) > 0 ? (iters / BATCH_MAX) : 1;
    int* texels = malloc(sizeof(*texels) * 128 * 128);
    int failed = 0;
    int i;

    assert(texels);
    init_hsl16_to_pixel_table();

    /* A texture no two texels of which are equal, so a fetch that lands on
     * the wrong texel cannot accidentally read the right colour. Every
     * sixteenth texel is zero, so the colour key has something to key on. */
    for( i = 0; i < 128 * 128; i++ )
        texels[i] = (i % 16 == 5) ? 0 : (int)(0x00010203u * (unsigned)i + 0x00A5C300u);

    for( i = 0; i < DOOR_COUNT; i++ )
    {
        int tex = i >= DOOR_TEX_OPAQUE;
        int gouraud = i == DOOR_GOURAUD_OPAQUE || i == DOOR_GOURAUD_ALPHA;
        int flat = i == DOOR_FLAT_OPAQUE || i == DOOR_FLAT_ALPHA;

        if( (flat && !TEST_FLAT) || (gouraud && !TEST_GOURAUD) || (tex && !TEST_TEX) )
            continue;
        g_seed = 20260828u + (unsigned)i;
        failed |= door_pass((enum door)i, chunks, texels);
    }

    free(texels);
    return failed ? 1 : 0;
}
