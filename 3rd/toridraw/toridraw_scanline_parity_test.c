/*
 * Parity + coverage test for the `scanline` raster family.
 *
 * For every variant it renders the same triangles twice - once with the
 * default `branching` kernel, once with the new `scanline` kernel - into
 * separate framebuffers and reports how far apart they are. Triangles are
 * generated to hit every interesting case: fully on screen, clipped on each
 * edge, straddling corners, degenerate (zero area / zero height), inverted
 * winding, and single-row slivers.
 *
 * The two families are not required to be bit-identical: the `scanline`
 * kernels clamp a clipped right edge to screen_width rather than
 * screen_width-1, so a triangle clipped on the right can legitimately differ
 * in its final column. The test therefore asserts that
 *
 *   - unclipped triangles match exactly,
 *   - clipped triangles differ only in pixels adjacent to a clip boundary,
 *   - nothing is ever written outside the framebuffer (guard bands checked).
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/scanline_parity 3rd/toridraw/toridraw_scanline_parity_test.c -lm
 *   /tmp/scanline_parity
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"

/* The scanline family's runtime selector lives in toridraw.c; this TU stands
 * alone, so provide the definition here. */
int g_toridraw_raster_scanline = 0;

// clang-format off
#include "graphics/shared_tables.c"

#include "impl/raster/flat/raster.flat.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/flat/raster.flat.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"

#include "impl/projection/projection.scalar_reference.u.c"
#include "impl/raster/span/span.tex.dispatch.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.texopaque.nofacealpha.nomodulate.painter.scanline.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.textrans.nofacealpha.nomodulate.painter.scanline.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.affine.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.affine.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"

#include "impl/raster/scanline/scanline.dispatch.u.c"
// clang-format on

#define W 200
#define H 150
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

#define TEX_W 64
#define TEX_LEN (TEX_W * TEX_W)

static int g_texels[TEX_LEN];        /* checkerboard with a transparent quadrant */
static int g_texels_opaque[TEX_LEN]; /* same, but no zero texels */
static int g_texels_white[TEX_LEN];  /* uniform, isolates the shade plane */

struct Tri
{
    const char* name;
    int x[3];
    int y[3];
    int expect_clipped;
};

/* Every interesting geometric case. `expect_clipped` marks triangles whose
 * spans can touch a screen boundary, where the two families are allowed to
 * disagree on the boundary column. */
static const struct Tri g_tris[] = {
    { "interior", { 40, 150, 90 }, { 20, 35, 120 }, 0 },
    { "interior-flat-top", { 30, 160, 95 }, { 30, 30, 110 }, 0 },
    { "interior-flat-bottom", { 30, 160, 95 }, { 110, 110, 30 }, 0 },
    { "interior-cw", { 90, 150, 40 }, { 120, 35, 20 }, 0 },
    { "sliver-1row", { 20, 180, 100 }, { 60, 60, 61 }, 0 },
    { "sliver-1col", { 100, 101, 100 }, { 20, 70, 120 }, 0 },
    { "degenerate-zero-area", { 20, 60, 100 }, { 20, 60, 100 }, 0 },
    { "degenerate-zero-height", { 20, 60, 100 }, { 40, 40, 40 }, 0 },
    { "degenerate-point", { 50, 50, 50 }, { 50, 50, 50 }, 0 },
    { "clip-left", { -80, 60, 10 }, { 20, 40, 120 }, 1 },
    { "clip-right", { 140, 320, 190 }, { 20, 40, 120 }, 1 },
    { "clip-top", { 40, 150, 90 }, { -70, -30, 60 }, 1 },
    { "clip-bottom", { 40, 150, 90 }, { 60, 100, 260 }, 1 },
    { "clip-corner-tl", { -60, 70, -10 }, { -50, 20, 60 }, 1 },
    { "clip-corner-br", { 130, 300, 180 }, { 90, 130, 250 }, 1 },
    { "covers-screen", { -400, 600, 100 }, { -300, -300, 500 }, 1 },
    { "offscreen-above", { 40, 150, 90 }, { -400, -380, -300 }, 0 },
    { "offscreen-below", { 40, 150, 90 }, { 300, 380, 400 }, 0 },
    { "offscreen-left", { -400, -300, -350 }, { 20, 40, 120 }, 0 },
    { "offscreen-right", { 400, 500, 450 }, { 20, 40, 120 }, 0 },
};

#define TRI_COUNT ((int)(sizeof(g_tris) / sizeof(g_tris[0])))

static int g_fail;

/*
 * The "nothing drew here" sentinel.
 *
 * It has to carry the framebuffer's type and not be a raw literal at each use:
 * on a 16-bit format 0x00112233 truncates on the way INTO the buffer, and a
 * comparison against the untruncated literal then reports every untouched
 * pixel as written. Naming it once makes the fill and the test agree on every
 * format.
 */
#define UNWRITTEN ((toripixel_t)0x00112233)

static void
buf_reset(toripixel_t* buf)
{
    for( int i = 0; i < BUF_LEN; i++ )
        buf[i] = UNWRITTEN;
}

static int
guard_intact(const toripixel_t* buf, const char* variant, const char* tri)
{
    for( int i = 0; i < GUARD; i++ )
    {
        if( buf[i] != UNWRITTEN || buf[BUF_LEN - 1 - i] != UNWRITTEN )
        {
            printf("  FAIL %-34s %-22s wrote outside the framebuffer\n", variant, tri);
            g_fail++;
            return 0;
        }
    }
    return 1;
}

/**
 * Compare two renders. Returns the number of differing pixels that are *not*
 * adjacent to a clip boundary (those are the sanctioned right-edge deviation).
 */
static int
compare(
    const toripixel_t* a,
    const toripixel_t* b,
    int allow_boundary,
    int* out_total_diff)
{
    int total = 0;
    int hard = 0;

    for( int y = 0; y < H; y++ )
    {
        for( int x = 0; x < W; x++ )
        {
            int i = GUARD + y * W + x;
            if( a[i] == b[i] )
                continue;
            total++;
            if( allow_boundary && (x >= W - 2 || x <= 1 || y >= H - 1) )
                continue;
            hard++;
        }
    }

    *out_total_diff = total;
    return hard;
}

static void
report(
    const char* variant,
    const struct Tri* tri,
    const toripixel_t* ref,
    const toripixel_t* got)
{
    if( !guard_intact(got, variant, tri->name) )
        return;

    int total = 0;
    int hard = compare(ref, got, tri->expect_clipped, &total);

    if( hard != 0 )
    {
        printf(
            "  FAIL %-34s %-22s %d differing pixels (%d away from any clip edge)\n",
            variant,
            tri->name,
            total,
            hard);
        g_fail++;
    }
    else if( total != 0 )
    {
        printf(
            "  ok   %-34s %-22s (%d boundary-column pixels differ, allowed)\n",
            variant,
            tri->name,
            total);
    }
}

/** Coverage-only comparison: which pixels were written, ignoring their value. */
static void
report_coverage(
    const char* variant,
    const struct Tri* tri,
    const toripixel_t* a,
    const toripixel_t* b)
{
    int bad = 0;
    for( int y = 0; y < H; y++ )
    {
        for( int x = 0; x < W; x++ )
        {
            int i = GUARD + y * W + x;
            int wrote_a = a[i] != UNWRITTEN;
            int wrote_b = b[i] != UNWRITTEN;
            if( wrote_a == wrote_b )
                continue;
            if( tri->expect_clipped && (x >= W - 2 || x <= 1 || y >= H - 1) )
                continue;
            bad++;
        }
    }

    if( bad )
    {
        printf("  FAIL %-34s %-22s %d pixels differ in coverage\n", variant, tri->name, bad);
        g_fail++;
    }
}

/* ------------------------------------------------------------------ flat */

static void
test_flat(toripixel_t* ref, toripixel_t* got)
{
    printf("flat.screen.opaque / alpha\n");

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(ref);
        buf_reset(got);
        raster_flat_screen_opaque_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31);
        raster_flat_screen_opaque_scanline_s8(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31);
        report("flat.screen.opaque.scanline.s8", t, ref, got);

        buf_reset(ref);
        buf_reset(got);
        raster_flat_screen_alpha_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31,
            0x80);
        raster_flat_screen_alpha_scanline_s8(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31,
            0x80);
        report("flat.screen.alpha.scanline.s8", t, ref, got);
    }
}

/* --------------------------------------------------- gouraudhsllightness */

static void
test_gouraudhsllightness(toripixel_t* ref, toripixel_t* got)
{
    printf("gouraudhsllightness.screen.opaque / alpha (bary)\n");

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(ref);
        buf_reset(got);
        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000);
        raster_gouraudhsllightness_screen_opaque_bary_scanline_s4(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000);
        report("gouraudhsllightness.screen.opaque.bary.scanline.s4", t, ref, got);

        buf_reset(ref);
        buf_reset(got);
        raster_gouraudhsllightness_screen_alpha_bary_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000, 0x80);
        raster_gouraudhsllightness_screen_alpha_bary_scanline_s4(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000, 0x80);
        report("gouraudhsllightness.screen.alpha.bary.scanline.s4", t, ref, got);

        /* Constant colour exercises the flat-span degeneration path. */
        buf_reset(ref);
        buf_reset(got);
        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31,
            0x2A31, 0x2A31);
        raster_gouraudhsllightness_screen_opaque_bary_scanline_s4(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31,
            0x2A31, 0x2A31);
        report("gouraudhsllightness.scanline (constant colour)", t, ref, got);
    }
}

/*
 * Item-icon rendering exposed this at 36x32: a fixed-point Gouraud edge can
 * sample one HSL16 unit just outside the palette after its subpixel nudge. The
 * production crash was the no-clip s4 tail, so keep that exact three-pixel
 * shape here as a cache-independent regression. ASan turns the old lookup at
 * 65536 into a global-buffer-overflow; the saturated lookup must instead use
 * the last palette entry. Check the lower edge too because the same plane can
 * undershoot at the opposite side of a triangle.
 */
static void
test_gouraudhsllightness_palette_bounds(void)
{
    enum
    {
        ICON_W = 36,
        ICON_H = 32,
        ICON_ROW = 16,
        ICON_X = 12,
    };
    toripixel_t icon[ICON_W * ICON_H];
    int const high_ish8 = 0x10000 << 8;
    int const low_ish8 = -1;
    toripixel_t const high_rgb = g_hsl16_to_pixel_table[0xFFFF];
    toripixel_t const low_rgb = g_hsl16_to_pixel_table[0];

    printf("gouraudhsllightness HSL16 palette bounds (36x32 obj icon tail)\n");

    if( ToriDraw_Hsl16Ish8ToPixel(high_ish8) != high_rgb ||
        ToriDraw_Hsl16Ish8ToPixel(low_ish8) != low_rgb )
    {
        printf("  FAIL interpolated HSL16 palette clamp\n");
        g_fail++;
        return;
    }

    for( int i = 0; i < ICON_W * ICON_H; i++ )
        icon[i] = UNWRITTEN;

    draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered_noclip(
        icon,
        ICON_ROW * ICON_W,
        ICON_X << 16,
        (ICON_X + 3) << 16,
        high_ish8,
        0);

    for( int x = ICON_X; x < ICON_X + 3; x++ )
    {
        if( icon[ICON_ROW * ICON_W + x] != high_rgb )
        {
            printf("  FAIL no-clip s4 tail did not saturate HSL16\n");
            g_fail++;
            return;
        }
    }
}

/* --------------------------------------------------------------- texture */

/* Orthographic uv basis placed well in front of the near plane so the
 * perspective divide is well conditioned. */
struct TexVerts
{
    int ux[3];
    int uy[3];
    int uz[3];
};

static const struct TexVerts g_texverts = {
    { -128, 128, -128 },
    { -128, -128, 128 },
    { 900, 1100, 1000 },
};

/* Constant depth: perspective and affine must then agree exactly. */
static const struct TexVerts g_texverts_flat = {
    { -128, 128, -128 },
    { -128, -128, 128 },
    { 1000, 1000, 1000 },
};

#define TEXGEOM(TV)                                                                                \
    W, W, H, 512, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], (TV)->ux[0], (TV)->ux[1],   \
        (TV)->ux[2], (TV)->uy[0], (TV)->uy[1], (TV)->uy[2], (TV)->uz[0], (TV)->uz[1], (TV)->uz[2]

/*
 * Anchor: the one textured variant whose reference counterpart uses the same
 * left-edge rule as the scanline family (x >> 16, no subpixel nudge). Every
 * other textured check below chains off this one.
 *
 * The `textrans` and `lerp8_v3` reference kernels instead start a span at
 * (x - 1) >> 16, which begins a row one pixel earlier whenever the left edge
 * lands exactly on a pixel boundary - and, because that shifts the 8-pixel
 * block alignment, changes uv for the whole row. The scanline family uses one
 * rule everywhere so textured faces do not seam against the flat and gouraudhsllightness
 * faces they share edges with, which means it cannot be bit-identical to both
 * reference conventions at once.
 */


static void
test_texture_anchor(toripixel_t* ref, toripixel_t* got)
{
    printf("texshadeflat.persp.texopaque.scanline vs branching (exact anchor)\n");

    const struct TexVerts* tv = &g_texverts;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(ref);
        buf_reset(got);
        raster_texshadeflat_persp_texopaque_branching_lerp8(
            ref + GUARD, TEXGEOM(tv), 0x40, g_texels, TEX_W);
        raster_texshadeflat_persp_texopaque_scanline_lerp8(
            got + GUARD, TEXGEOM(tv), 0x40, 0x40, 0x40, 0xFF, g_texels, TEX_W);
        report("texshadeflat.persp.texopaque.scanline", t, ref, got);
    }
}

/*
 * Chains off the anchor. Each check pins one axis of the variant matrix by
 * driving two variants into a configuration where they must agree exactly:
 *
 *   textrans   == texopaque        when no texel is 0
 *   texshadeblend == texshadeflat  when all three shades are equal
 *   affine     == persp            when the triangle is at constant depth
 *   facealpha(255) == plain        by construction
 */
static void
test_texture_chain(toripixel_t* a, toripixel_t* b)
{
    printf("scanline texture variant chain (gate / shade / space / alpha axes)\n");

    const struct TexVerts* tv = &g_texverts;
    const struct TexVerts* tvf = &g_texverts_flat;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        /* gate axis: with an all-opaque texture, textrans must equal texopaque */
        buf_reset(a);
        buf_reset(b);
        raster_texshadeflat_persp_texopaque_scanline_lerp8(
            a + GUARD, TEXGEOM(tv), 0x40, 0x40, 0x40, 0xFF, g_texels_opaque, TEX_W);
        raster_texshadeflat_persp_textrans_scanline_lerp8(
            b + GUARD, TEXGEOM(tv), 0x40, 0x40, 0x40, 0xFF, g_texels_opaque, TEX_W);
        report("textrans == texopaque (no zero texels)", t, a, b);

        /* shade axis: a constant shade plane must equal the flat-shade kernel */
        buf_reset(a);
        buf_reset(b);
        raster_texshadeflat_persp_texopaque_scanline_lerp8(
            a + GUARD, TEXGEOM(tv), 0x40, 0x40, 0x40, 0xFF, g_texels, TEX_W);
        raster_texshadeblend_persp_texopaque_scanline_lerp8(
            b + GUARD, TEXGEOM(tv), 0x40, 0x40, 0x40, 0xFF, g_texels, TEX_W);
        report("texshadeblend == texshadeflat (equal shades)", t, a, b);

        buf_reset(a);
        buf_reset(b);
        raster_texshadeflat_persp_textrans_scanline_lerp8(
            a + GUARD, TEXGEOM(tv), 0x33, 0x33, 0x33, 0xFF, g_texels, TEX_W);
        raster_texshadeblend_persp_textrans_scanline_lerp8(
            b + GUARD, TEXGEOM(tv), 0x33, 0x33, 0x33, 0xFF, g_texels, TEX_W);
        report("texshadeblend.textrans == texshadeflat.textrans", t, a, b);

        /*
         * space axis: at constant depth the affine and perspective walkers
         * must cover the same pixels and shade them the same way. A uniform
         * texture removes uv sampling from the comparison - the two spaces
         * legitimately round u/v differently (perspective snaps u to a whole
         * texel at each 8-pixel block boundary, affine only at the two span
         * ends), so their sampled texels are allowed to differ by a texel.
         */
        buf_reset(a);
        buf_reset(b);
        raster_texshadeblend_persp_texopaque_scanline_lerp8(
            a + GUARD, TEXGEOM(tvf), 0x20, 0x50, 0x70, 0xFF, g_texels_white, TEX_W);
        raster_texshadeblend_affine_texopaque_scanline_lerp8(
            b + GUARD, TEXGEOM(tvf), 0x20, 0x50, 0x70, 0xFF, g_texels_white, TEX_W);
        report("affine == persp coverage+shade (constant depth)", t, a, b);

        /* space axis, uv: with a real texture the two must still agree on
         * which pixels they touch, even where the sampled texel differs. */
        buf_reset(a);
        buf_reset(b);
        raster_texshadeblend_persp_texopaque_scanline_lerp8(
            a + GUARD, TEXGEOM(tvf), 0x20, 0x50, 0x70, 0xFF, g_texels_opaque, TEX_W);
        raster_texshadeblend_affine_texopaque_scanline_lerp8(
            b + GUARD, TEXGEOM(tvf), 0x20, 0x50, 0x70, 0xFF, g_texels_opaque, TEX_W);
        report_coverage("affine/persp identical coverage", t, a, b);
    }
}

/*
 * Partial face alpha has no reference to compare against, so verify the
 * blend algebraically: every written pixel must equal
 * alpha_blend(alpha, background, opaque_result), and nothing outside the
 * opaque coverage may be touched.
 */
static void
check_facealpha(
    const char* variant,
    const struct Tri* t,
    const toripixel_t* opaque_buf,
    const toripixel_t* got,
    int alpha)
{
    int bad = 0;
    for( int p = 0; p < H * W; p++ )
    {
        toripixel_t opaque = opaque_buf[GUARD + p];
        toripixel_t expect =
            (opaque == UNWRITTEN) ? UNWRITTEN : alpha_blend(alpha, UNWRITTEN, opaque);
        if( got[GUARD + p] != expect )
            bad++;
    }

    if( bad )
    {
        printf(
            "  FAIL %-34s %-22s alpha=%3d: %d pixels are not alpha_blend(dst, opaque)\n",
            variant,
            t->name,
            alpha,
            bad);
        g_fail++;
    }
}

static void
test_texture_facealpha_blend(toripixel_t* opaque_buf, toripixel_t* got)
{
    printf("texshade*.facealpha.scanline blend algebra (all gates / spaces)\n");

    const struct TexVerts* tv = &g_texverts;
    static const int alphas[] = { 0x00, 0x01, 0x60, 0xC0, 0xFF };

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        for( int ai = 0; ai < (int)(sizeof(alphas) / sizeof(alphas[0])); ai++ )
        {
            int alpha = alphas[ai];

#define FACEALPHA_CASE(NAME, PLAIN_FN, ALPHA_FN, TVP, TEX)                                         \
    do                                                                                             \
    {                                                                                              \
        buf_reset(opaque_buf);                                                                     \
        buf_reset(got);                                                                            \
        PLAIN_FN(opaque_buf + GUARD, TEXGEOM(TVP), 0x20, 0x50, 0x70, 0xFF, TEX, TEX_W);            \
        ALPHA_FN(got + GUARD, TEXGEOM(TVP), 0x20, 0x50, 0x70, alpha, TEX, TEX_W);                  \
        if( guard_intact(got, NAME, t->name) )                                                     \
            check_facealpha(NAME, t, opaque_buf, got, alpha);                                      \
    } while( 0 )

            FACEALPHA_CASE(
                "texshadeblend.persp.texopaque.facealpha",
                raster_texshadeblend_persp_texopaque_scanline_lerp8,
                raster_texshadeblend_persp_texopaque_facealpha_scanline_lerp8,
                tv,
                g_texels);

            FACEALPHA_CASE(
                "texshadeblend.persp.textrans.facealpha",
                raster_texshadeblend_persp_textrans_scanline_lerp8,
                raster_texshadeblend_persp_textrans_facealpha_scanline_lerp8,
                tv,
                g_texels);

            FACEALPHA_CASE(
                "texshadeblend.affine.texopaque.facealpha",
                raster_texshadeblend_affine_texopaque_scanline_lerp8,
                raster_texshadeblend_affine_texopaque_facealpha_scanline_lerp8,
                tv,
                g_texels);

            FACEALPHA_CASE(
                "texshadeblend.affine.textrans.facealpha",
                raster_texshadeblend_affine_textrans_scanline_lerp8,
                raster_texshadeblend_affine_textrans_facealpha_scanline_lerp8,
                tv,
                g_texels);

            FACEALPHA_CASE(
                "texshadeflat.persp.textrans.facealpha",
                raster_texshadeflat_persp_textrans_scanline_lerp8,
                raster_texshadeflat_persp_textrans_facealpha_scanline_lerp8,
                tv,
                g_texels);

            FACEALPHA_CASE(
                "texshadeflat.affine.texopaque.facealpha",
                raster_texshadeflat_affine_texopaque_scanline_lerp8,
                raster_texshadeflat_affine_texopaque_facealpha_scanline_lerp8,
                tv,
                g_texels);

#undef FACEALPHA_CASE
        }
    }
}

/*
 * Verify the interpolated shade plane analytically rather than against
 * another rasterizer.
 *
 * With a uniform white texture the output of a texshadeblend span is exactly
 * shade_blend(0x00FFFFFF, shade8), so each channel is floor(255 * shade8/256)
 * and shade8 can be read straight back out of the framebuffer.
 *
 * The lerp8 kernels hold shade constant across each 8-pixel block, anchored at
 * the span's first pixel - so the *first written pixel of every row* is the
 * one place where the sampled shade must equal the plane evaluated at that
 * exact pixel. That is what this checks, against a double-precision plane
 * solved from the three input vertices.
 */
static void
test_texture_shade_plane(toripixel_t* got)
{
    printf("texshadeblend shade plane vs analytic plane (white texture)\n");

    const struct TexVerts* tv = &g_texverts;
    const int shade_a = 0x20;
    const int shade_b = 0x50;
    const int shade_c = 0x70;

    int worst = 0;
    int samples = 0;
    double err_sum = 0.0;
    double signed_sum = 0.0;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        /* Solve shade8(x, y) = k0 + kx*x + ky*y through the three vertices. */
        double x0 = t->x[0], x1 = t->x[1], x2 = t->x[2];
        double y0 = t->y[0], y1 = t->y[1], y2 = t->y[2];
        double s0 = shade_a * 2.0, s1 = shade_b * 2.0, s2 = shade_c * 2.0;

        double det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if( det == 0.0 )
            continue;

        double kx = ((s1 - s0) * (y2 - y0) - (s2 - s0) * (y1 - y0)) / det;
        double ky = ((s2 - s0) * (x1 - x0) - (s1 - s0) * (x2 - x0)) / det;
        double k0 = s0 - kx * x0 - ky * y0;

        buf_reset(got);
        raster_texshadeblend_persp_texopaque_scanline_lerp8(
            got + GUARD, TEXGEOM(tv), shade_a, shade_b, shade_c, 0xFF, g_texels_white, TEX_W);

        for( int y = 0; y < H; y++ )
        {
            for( int x = 0; x < W; x++ )
            {
                toripixel_t px = got[GUARD + y * W + x];
                if( px == UNWRITTEN )
                    continue;

                /* First written pixel of this row: shade is sampled here. */
                /* shade_blend stores floor(255 * shade8 / 256) per channel, so
                 * the inverse is a ceiling, not a round. The channel has to be
                 * read back through the format -- `px & 0xFF` is blue only in
                 * an ARGB layout, and is the alpha lane in RGBA. */
                int channel = (int)TORIPIXEL_ARGB_B(toripixel_to_argb8888(px));
                int shade8 = (channel * 256 + 254) / 255;

                /* The kernel applies a one-step nudge (+kx) to the plane base,
                 * matching the reference kernels. */
                double expect = k0 + kx * (x + 1) + ky * y;
                double err = shade8 - expect;
                int d = (int)(err < 0 ? -err : err);
                if( d > worst )
                    worst = d;
                err_sum += (err < 0 ? -err : err);
                signed_sum += err;
                samples++;
                break;
            }
        }
    }

    double mean_abs = samples ? err_sum / samples : 0.0;
    double bias = samples ? signed_sum / samples : 0.0;

    printf(
        "  checked %d row-start samples: worst |err| %d, mean |err| %.2f, bias %+.2f\n",
        samples,
        worst,
        mean_abs,
        bias);

    /*
     * The residual is fixed-point noise, not a plane error: shade_xhat/yhat are
     * truncated to 1/256 of a shade step and accumulate over a span, and
     * recovering shade8 from floor(255*shade8/256) is itself lossy by one. A
     * wrong plane shows up as a large worst-case *and* a large bias, so both
     * are bounded here.
     */
    /*
     * The tolerance is an 8-bit-channel tolerance, so the assertion only runs
     * where the framebuffer has 8-bit channels. On a 16-bit format the shade
     * comes back through five bits and the residual is dominated by that
     * quantisation, which would be measuring the FORMAT rather than the
     * kernel's plane -- the interpolation this checks is identical either way.
     */
    if( !TORIPIXEL_LANES_8BIT )
    {
        printf("  (skipped: 16-bit channels cannot resolve an 8-bit shade)\n");
    }
    else if( worst > 4 || mean_abs > 1.5 || bias < -1.0 || bias > 1.0 )
    {
        printf("  FAIL interpolated shade does not match the analytic plane\n");
        g_fail++;
    }
}

/* Exact plane captured from the full-client QBD frame that used to produce
 * purple/green bands.  The old projection multiply overflowed before its
 * right shift, then the U/O x plane overflowed again while walking the 1158px
 * viewport.  The shared homogeneous shift must make it fit without putting
 * 64-bit values in the renderer. */
static void
test_texture_plane_32bit_normalization(void)
{
    struct ToriDraw_TexturePlane32 plane = {
        .term = {
            { 650897, 1251497, 559873, 0 },
            { -9219892, -131868, -1290348, 0 },
            { -1216909, 32019, -817613, 0 },
        },
    };
    int projected;

    printf("texture plane 32-bit normalization (full-client QBD capture)\n");

    if( !project_scale_unit_try(-1290348, 156928, &projected) ||
        projected != -1581966848 )
    {
        printf("  FAIL split projection multiply: got %d\n", projected);
        g_fail++;
        return;
    }

    if( !ToriDraw_TexturePlanePrepare32(&plane, 1158, 800, 156928) )
    {
        printf("  FAIL plane normalization rejected the QBD triangle\n");
        g_fail++;
        return;
    }

    if( plane.shift != 2 || plane.term[0].x != 162724 ||
        plane.term[0].y != 312874 || plane.term[0].base != 171601024 ||
        plane.term[1].x != -2304973 || plane.term[1].y != -32967 ||
        plane.term[1].base != -395491712 || plane.term[2].x != -304227 ||
        plane.term[2].y != 8004 || plane.term[2].base != -250598400 )
    {
        printf(
            "  FAIL shift=%d A={%d,%d,%d} B={%d,%d,%d} C={%d,%d,%d}\n",
            plane.shift,
            plane.term[0].x,
            plane.term[0].y,
            plane.term[0].base,
            plane.term[1].x,
            plane.term[1].y,
            plane.term[1].base,
            plane.term[2].x,
            plane.term[2].y,
            plane.term[2].base);
        g_fail++;
    }
}

#undef TEXGEOM

int
main(void)
{
    init_hsl16_to_pixel_table();
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    init_reciprocal16();

    /* Checkerboard with a transparent (0) quadrant so the textrans gate and
     * the u/v masks both get exercised. */
    for( int v = 0; v < TEX_W; v++ )
    {
        for( int u = 0; u < TEX_W; u++ )
        {
            int transparent = (u < TEX_W / 4) && (v < TEX_W / 4);
            int rgb = 0x00202020 + (u << 16) + (v << 8) + ((u ^ v) & 0xFF);
            g_texels[u + v * TEX_W] = transparent ? 0 : rgb;
            g_texels_opaque[u + v * TEX_W] = rgb;
            g_texels_white[u + v * TEX_W] = 0x00FFFFFF;
        }
    }

    toripixel_t* ref = malloc(sizeof(*ref) * BUF_LEN);
    toripixel_t* got = malloc(sizeof(*got) * BUF_LEN);
    assert(ref);
    assert(got);

    test_flat(ref, got);
    test_gouraudhsllightness(ref, got);
    test_gouraudhsllightness_palette_bounds();
    test_texture_anchor(ref, got);
    test_texture_chain(ref, got);
    test_texture_facealpha_blend(ref, got);
    test_texture_shade_plane(got);
    test_texture_plane_32bit_normalization();

    free(ref);
    free(got);

    if( g_fail )
    {
        printf("\n%d failure(s)\n", g_fail);
        return 1;
    }

    printf("\nall scanline variants agree with their branching counterparts\n");
    return 0;
}
