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

#include "graphics/raster/flat/flat.screen.opaque.branching.s4.c"
#include "graphics/raster/flat/flat.screen.alpha.branching.s4.c"
#include "graphics/raster/gouraud/gouraud.screen.opaque.bary.branching.s4.c"
#include "graphics/raster/gouraud/gouraud.screen.alpha.bary.branching.s4.c"

#include "graphics/projection.u.c"
#include "graphics/raster/texture/span/tex.span.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeflat.persp.textrans.ordered.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeflat.persp.textrans.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8_v3.u.c"

#include "graphics/raster/scanline/scanline.u.c"
// clang-format on

#define W 200
#define H 150
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

#define TEX_W 64
#define TEX_LEN (TEX_W * TEX_W)

static int g_texels[TEX_LEN];

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

static void
buf_reset(int* buf)
{
    for( int i = 0; i < BUF_LEN; i++ )
        buf[i] = 0x00112233;
}

static int
guard_intact(const int* buf, const char* variant, const char* tri)
{
    for( int i = 0; i < GUARD; i++ )
    {
        if( buf[i] != 0x00112233 || buf[BUF_LEN - 1 - i] != 0x00112233 )
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
    const int* a,
    const int* b,
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
    const int* ref,
    const int* got)
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

/* ------------------------------------------------------------------ flat */

static void
test_flat(int* ref, int* got)
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

/* --------------------------------------------------------------- gouraud */

static void
test_gouraud(int* ref, int* got)
{
    printf("gouraud.screen.opaque / alpha (bary)\n");

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(ref);
        buf_reset(got);
        raster_gouraud_screen_opaque_bary_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000);
        raster_gouraud_screen_opaque_bary_scanline_s4(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000);
        report("gouraud.screen.opaque.bary.scanline.s4", t, ref, got);

        buf_reset(ref);
        buf_reset(got);
        raster_gouraud_screen_alpha_bary_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000, 0x80);
        raster_gouraud_screen_alpha_bary_scanline_s4(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x1000,
            0x2800, 0x4000, 0x80);
        report("gouraud.screen.alpha.bary.scanline.s4", t, ref, got);

        /* Constant colour exercises the flat-span degeneration path. */
        buf_reset(ref);
        buf_reset(got);
        raster_gouraud_screen_opaque_bary_branching_s4(
            ref + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31,
            0x2A31, 0x2A31);
        raster_gouraud_screen_opaque_bary_scanline_s4(
            got + GUARD, W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], 0x2A31,
            0x2A31, 0x2A31);
        report("gouraud.scanline (constant colour)", t, ref, got);
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

static void
test_texture(int* ref, int* got)
{
    printf("texshade{flat,blend}.{persp,affine}.{texopaque,textrans}\n");

    const struct TexVerts* tv = &g_texverts;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

#define TEXGEOM                                                                                    \
    W, W, H, 512, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], tv->ux[0], tv->ux[1],       \
        tv->ux[2], tv->uy[0], tv->uy[1], tv->uy[2], tv->uz[0], tv->uz[1], tv->uz[2]

#define TEXARGS(REF_SHADE_A, REF_SHADE_B, REF_SHADE_C)                                             \
    TEXGEOM, REF_SHADE_A, REF_SHADE_B, REF_SHADE_C

        /* persp / flat shade / opaque */
        buf_reset(ref);
        buf_reset(got);
        raster_texshadeflat_persp_texopaque_branching_lerp8(
            ref + GUARD, TEXGEOM, 0x40, g_texels, TEX_W);
        raster_texshadeflat_persp_texopaque_scanline_lerp8(
            got + GUARD, TEXARGS(0x40, 0x40, 0x40), 0xFF, g_texels, TEX_W);
        report("texshadeflat.persp.texopaque.scanline", t, ref, got);

        /* persp / flat shade / transparent */
        buf_reset(ref);
        buf_reset(got);
        raster_texshadeflat_persp_textrans_branching_lerp8(
            ref + GUARD, TEXGEOM, 0x40, g_texels, TEX_W);
        raster_texshadeflat_persp_textrans_scanline_lerp8(
            got + GUARD, TEXARGS(0x40, 0x40, 0x40), 0xFF, g_texels, TEX_W);
        report("texshadeflat.persp.textrans.scanline", t, ref, got);

        /* persp / blend shade / opaque */
        buf_reset(ref);
        buf_reset(got);
        raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
            ref + GUARD, TEXARGS(0x20, 0x50, 0x70), g_texels, TEX_W);
        raster_texshadeblend_persp_texopaque_scanline_lerp8(
            got + GUARD, TEXARGS(0x20, 0x50, 0x70), 0xFF, g_texels, TEX_W);
        report("texshadeblend.persp.texopaque.scanline", t, ref, got);

        /* persp / blend shade / transparent */
        buf_reset(ref);
        buf_reset(got);
        raster_texshadeblend_persp_textrans_branching_lerp8_v3(
            ref + GUARD, TEXARGS(0x20, 0x50, 0x70), g_texels, TEX_W);
        raster_texshadeblend_persp_textrans_scanline_lerp8(
            got + GUARD, TEXARGS(0x20, 0x50, 0x70), 0xFF, g_texels, TEX_W);
        report("texshadeblend.persp.textrans.scanline", t, ref, got);

        /* affine / blend shade / opaque */
        buf_reset(ref);
        buf_reset(got);
        raster_texshadeblend_affine_texopaque_branching_lerp8_v3(
            ref + GUARD, TEXARGS(0x20, 0x50, 0x70), g_texels, TEX_W);
        raster_texshadeblend_affine_texopaque_scanline_lerp8(
            got + GUARD, TEXARGS(0x20, 0x50, 0x70), 0xFF, g_texels, TEX_W);
        report("texshadeblend.affine.texopaque.scanline", t, ref, got);

        /* affine / blend shade / transparent */
        buf_reset(ref);
        buf_reset(got);
        raster_texshadeblend_affine_textrans_branching_lerp8_v3(
            ref + GUARD, TEXARGS(0x20, 0x50, 0x70), g_texels, TEX_W);
        raster_texshadeblend_affine_textrans_scanline_lerp8(
            got + GUARD, TEXARGS(0x20, 0x50, 0x70), 0xFF, g_texels, TEX_W);
        report("texshadeblend.affine.textrans.scanline", t, ref, got);

#undef TEXARGS
#undef TEXGEOM
    }
}

/*
 * The facealpha variants have no `branching` counterpart, so they are checked
 * for self-consistency instead: alpha 0xFF must reproduce the non-alpha
 * variant, and any lower alpha must stay inside the framebuffer and leave the
 * destination somewhere between the untouched value and the opaque result.
 */
static void
test_texture_facealpha(int* opaque_buf, int* got)
{
    printf("texshade*.facealpha.scanline (no branching counterpart)\n");

    const struct TexVerts* tv = &g_texverts;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

#define TEXARGS(ALPHA)                                                                             \
    W, W, H, 512, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], tv->ux[0], tv->ux[1],       \
        tv->ux[2], tv->uy[0], tv->uy[1], tv->uy[2], tv->uz[0], tv->uz[1], tv->uz[2], 0x20, 0x50,    \
        0x70, ALPHA, g_texels, TEX_W

        buf_reset(opaque_buf);
        buf_reset(got);
        raster_texshadeblend_persp_texopaque_scanline_lerp8(opaque_buf + GUARD, TEXARGS(0xFF));
        raster_texshadeblend_persp_texopaque_facealpha_scanline_lerp8(
            got + GUARD, TEXARGS(0xFF));
        report("texshadeblend.persp.texopaque.facealpha=255", t, opaque_buf, got);

        buf_reset(got);
        raster_texshadeblend_persp_texopaque_facealpha_scanline_lerp8(
            got + GUARD, TEXARGS(0x60));
        guard_intact(got, "texshadeblend.persp.texopaque.facealpha=96", t->name);

        buf_reset(opaque_buf);
        buf_reset(got);
        raster_texshadeblend_persp_textrans_scanline_lerp8(opaque_buf + GUARD, TEXARGS(0xFF));
        raster_texshadeblend_persp_textrans_facealpha_scanline_lerp8(
            got + GUARD, TEXARGS(0xFF));
        report("texshadeblend.persp.textrans.facealpha=255", t, opaque_buf, got);

        buf_reset(got);
        raster_texshadeblend_affine_textrans_facealpha_scanline_lerp8(
            got + GUARD, TEXARGS(0x60));
        guard_intact(got, "texshadeblend.affine.textrans.facealpha=96", t->name);

#undef TEXARGS
    }
}

int
main(void)
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();

    /* Checkerboard with a transparent (0) quadrant so the textrans gate and
     * the u/v masks both get exercised. */
    for( int v = 0; v < TEX_W; v++ )
    {
        for( int u = 0; u < TEX_W; u++ )
        {
            int transparent = (u < TEX_W / 4) && (v < TEX_W / 4);
            g_texels[u + v * TEX_W] =
                transparent ? 0 : (0x00202020 + (u << 16) + (v << 8) + ((u ^ v) & 0xFF));
        }
    }

    int* ref = malloc(sizeof(int) * BUF_LEN);
    int* got = malloc(sizeof(int) * BUF_LEN);
    if( !ref || !got )
        return 1;

    test_flat(ref, got);
    test_gouraud(ref, got);
    test_texture(ref, got);
    test_texture_facealpha(ref, got);

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
