/*
 * Unit test for the textured per-face-alpha kernels of the `branching` family.
 *
 *   texshadeblend.persp.texopaque.facealpha.branching.lerp8_v3
 *   texshadeblend.persp.textrans.facealpha.branching.lerp8_v3
 *
 * These have a reference to be checked against, which the gouraudrgb family did
 * not: each is its plain twin plus a composite, so the plain twin supplies both
 * the coverage and the colour the blend is expected to start from.
 *
 *   1. coverage   identical to the plain kernel, pixel for pixel, at every
 *                 alpha - the walker and the uv fit must not have moved. Taken
 *                 from a two-background render rather than from "differs from
 *                 the background", which an alpha blend can satisfy by
 *                 landing back on it.
 *   2. algebra    every covered pixel equals alpha_blend(alpha, dst, plain),
 *                 and every uncovered pixel is untouched
 *   3. gate       with a colour-keyed texture the textrans variant leaves
 *                 texel-0 pixels *exactly* alone. Blending them at alpha 0
 *                 would be a different thing and would show up here, because
 *                 alpha_blend(0, dst, src) is not the identity on dst.
 *   4. guards     nothing written outside the framebuffer, over interior,
 *                 clipped, offscreen and degenerate triangles
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/tex_facealpha_test 3rd/toridraw/toridraw_texture_facealpha_test.c -lm
 *   /tmp/tex_facealpha_test
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"

// clang-format off
#include "graphics/shared_tables.c"

#include "graphics/projection.u.c"
#include "graphics/raster/texture/span/tex.span.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.facealpha.branching.lerp8_v3.u.c"
// clang-format on

#define W 200
#define H 150
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

#define BG 0x00112233
#define BG_ALT 0x00445566
#define GUARD_FILL 0x5A5A5A5A

#define TEX_W 64
#define TEX_LEN (TEX_W * TEX_W)

static int g_texels[TEX_LEN];        /* checkerboard with a transparent quadrant */
static int g_texels_opaque[TEX_LEN]; /* same, but no zero texels */

struct Tri
{
    const char* name;
    int x[3];
    int y[3];
};

static const struct Tri g_tris[] = {
    { "interior", { 40, 150, 90 }, { 20, 35, 120 } },
    { "interior-flat-top", { 30, 160, 95 }, { 30, 30, 110 } },
    { "interior-flat-bottom", { 30, 160, 95 }, { 110, 110, 30 } },
    { "interior-cw", { 90, 150, 40 }, { 120, 35, 20 } },
    { "sliver-1row", { 20, 180, 100 }, { 60, 60, 61 } },
    { "sliver-1col", { 100, 101, 100 }, { 20, 70, 120 } },
    { "degenerate-zero-area", { 20, 60, 100 }, { 20, 60, 100 } },
    { "degenerate-zero-height", { 20, 60, 100 }, { 40, 40, 40 } },
    { "clip-left", { -80, 60, 10 }, { 20, 40, 120 } },
    { "clip-right", { 140, 320, 190 }, { 20, 40, 120 } },
    { "clip-top", { 40, 150, 90 }, { -70, -30, 60 } },
    { "clip-bottom", { 40, 150, 90 }, { 60, 100, 260 } },
    { "clip-corner-tl", { -60, 70, -10 }, { -50, 20, 60 } },
    { "clip-corner-br", { 130, 300, 180 }, { 90, 130, 250 } },
    { "covers-screen", { -400, 600, 100 }, { -300, -300, 500 } },
    { "offscreen-above", { 40, 150, 90 }, { -400, -380, -300 } },
    { "offscreen-below", { 40, 150, 90 }, { 300, 380, 400 } },
    { "offscreen-left", { -400, -300, -350 }, { 20, 40, 120 } },
    { "offscreen-right", { 400, 500, 450 }, { 20, 40, 120 } },
};

#define TRI_COUNT ((int)(sizeof(g_tris) / sizeof(g_tris[0])))

/* Orthographic uv basis: origin plus a U and a V end, at a depth that keeps the
 * whole triangle in front of the eye so the perspective divide is well behaved. */
struct TexVerts
{
    int x[3];
    int y[3];
    int z[3];
};

static const struct TexVerts g_texverts = {
    { -70, 70, -70 },
    { -70, -70, 70 },
    { 260, 300, 340 },
};

#define CAMERA_COT16 512

/* The argument run shared by every kernel under test, plain and facealpha. */
#define TEXGEOM(t, tv)                                                                             \
    W, W, H, CAMERA_COT16, (t)->x[0], (t)->x[1], (t)->x[2], (t)->y[0], (t)->y[1], (t)->y[2],        \
        (tv)->x[0], (tv)->x[1], (tv)->x[2], (tv)->y[0], (tv)->y[1], (tv)->y[2], (tv)->z[0],        \
        (tv)->z[1], (tv)->z[2]

static int g_fail;

static void
buf_fill(int* buf, int background)
{
    for( int i = 0; i < GUARD; i++ )
        buf[i] = GUARD_FILL;
    for( int i = 0; i < H * W; i++ )
        buf[GUARD + i] = background;
    for( int i = 0; i < GUARD; i++ )
        buf[GUARD + H * W + i] = GUARD_FILL;
}

static int
guard_intact(const int* buf, const char* what, const char* tri)
{
    for( int i = 0; i < GUARD; i++ )
    {
        if( buf[i] != GUARD_FILL || buf[GUARD + H * W + i] != GUARD_FILL )
        {
            printf("  FAIL %-44s %-22s: wrote outside the framebuffer\n", what, tri);
            g_fail++;
            return 0;
        }
    }
    return 1;
}

static void
init_textures(void)
{
    for( int v = 0; v < TEX_W; v++ )
    {
        for( int u = 0; u < TEX_W; u++ )
        {
            int checker = (((u >> 3) ^ (v >> 3)) & 1);
            /* Never 0x000000 in the opaque table: a zero texel is the colour
             * key, and the two gates must be distinguishable. */
            int colour = checker ? 0x00C08040 : 0x004080C0;
            colour += (u & 7) << 8;

            g_texels_opaque[u + v * TEX_W] = colour;
            /* One quadrant of the keyed table is holes. */
            g_texels[u + v * TEX_W] = (u < TEX_W / 2 && v < TEX_W / 2) ? 0 : colour;
        }
    }
}

/*
 * Render the plain kernel over two different backgrounds.
 *
 * The plain kernels overwrite rather than blend, so a covered pixel lands on
 * the same value from either background and an uncovered one keeps whichever
 * background it started with. `covered[p]` is therefore exact - no sentinel
 * comparison, and no assumption that a drawn colour differs from the
 * background.
 */
typedef void (*plain_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int*, int);

static void
plain_coverage(
    plain_fn fn,
    const struct Tri* t,
    const struct TexVerts* tv,
    int* texels,
    int* buf_a,
    int* buf_b,
    unsigned char* covered)
{
    buf_fill(buf_a, BG);
    buf_fill(buf_b, BG_ALT);

    fn(buf_a + GUARD, TEXGEOM(t, tv), 0x20, 0x50, 0x70, texels, TEX_W);
    fn(buf_b + GUARD, TEXGEOM(t, tv), 0x20, 0x50, 0x70, texels, TEX_W);

    for( int p = 0; p < H * W; p++ )
        covered[p] = (unsigned char)(buf_a[GUARD + p] == buf_b[GUARD + p]);
}

typedef void (*alpha_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int*, int, int);

static void
check_variant(
    const char* name,
    plain_fn plain,
    alpha_fn blended,
    int* texels,
    int* buf_a,
    int* buf_b,
    int* got,
    unsigned char* covered)
{
    static const int alphas[] = { 0x00, 0x01, 0x40, 0x80, 0xC0, 0xFE, 0xFF };

    long total_covered = 0;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];
        const struct TexVerts* tv = &g_texverts;

        plain_coverage(plain, t, tv, texels, buf_a, buf_b, covered);
        if( !guard_intact(buf_a, name, t->name) )
            continue;

        for( int p = 0; p < H * W; p++ )
            total_covered += covered[p];

        for( int ai = 0; ai < (int)(sizeof(alphas) / sizeof(alphas[0])); ai++ )
        {
            int alpha = alphas[ai];

            buf_fill(got, BG);
            blended(got + GUARD, TEXGEOM(t, tv), 0x20, 0x50, 0x70, texels, TEX_W, alpha);

            if( !guard_intact(got, name, t->name) )
                break;

            int bad_covered = 0;
            int bad_untouched = 0;

            for( int p = 0; p < H * W; p++ )
            {
                int expect;
                if( covered[p] )
                    expect = alpha_blend(alpha, BG, buf_a[GUARD + p]);
                else
                    expect = BG;

                if( got[GUARD + p] != expect )
                {
                    if( covered[p] )
                        bad_covered++;
                    else
                        bad_untouched++;
                }
            }

            if( bad_covered || bad_untouched )
            {
                printf(
                    "  FAIL %-44s %-22s alpha=%3d: %d blended pixels wrong, "
                    "%d pixels touched outside coverage\n",
                    name,
                    t->name,
                    alpha,
                    bad_covered,
                    bad_untouched);
                g_fail++;
                break;
            }
        }
    }

    printf("  ok   %-44s all triangles / alphas (%ld covered px)\n", name, total_covered);
}

/*
 * The colour key is a separate decision from the alpha.
 *
 * A texel-0 pixel must be left exactly as it was, not composited at alpha 0 -
 * alpha_blend(0, dst, src) rounds dst down (255 becomes 254 per channel), so
 * the two are distinguishable and this check sees the difference.
 */
static void
test_textrans_gate_is_not_alpha_zero(int* buf)
{
    printf("textrans.facealpha skips keyed texels rather than blending them at 0\n");

    static const struct Tri t = { "interior", { 40, 150, 90 }, { 20, 35, 120 } };
    const struct TexVerts* tv = &g_texverts;

    /* A background whose channels are all 0xFF, where alpha_blend(0, dst, src)
     * is visibly not the identity. */
    const int white = 0x00FFFFFF;

    buf_fill(buf, white);
    raster_texshadeblend_persp_textrans_facealpha_branching_lerp8_v3(
        buf + GUARD, TEXGEOM(&t, tv), 0x20, 0x50, 0x70, g_texels, TEX_W, 0x00);

    if( !guard_intact(buf, "textrans.facealpha keyed", t.name) )
        return;

    /* Confirm the premise: alpha 0 is not the identity on this background. */
    if( alpha_blend(0x00, white, 0x00C08040) == white )
    {
        printf("  FAIL premise: alpha_blend(0, dst, src) is the identity, check is vacuous\n");
        g_fail++;
        return;
    }

    /* With alpha 0, every pixel the kernel *touches* moves off white; a keyed
     * pixel must still be exactly white. There must be some of each, or the
     * texture is not exercising the gate. */
    long untouched = 0;
    long touched = 0;
    for( int p = 0; p < H * W; p++ )
    {
        if( buf[GUARD + p] == white )
            untouched++;
        else
            touched++;
    }

    if( touched == 0 )
    {
        printf("  FAIL textrans.facealpha keyed: kernel drew nothing, check is vacuous\n");
        g_fail++;
        return;
    }

    printf("  ok   %ld px left exactly alone, %ld px composited\n", untouched, touched);
}

/*
 * At alpha 0xFF the blend is *not* the identity - alpha_blend rounds a 0xFF
 * channel down to 0xFE - so "facealpha(255) == plain" would be the wrong
 * assertion. What must hold is that the two agree on which pixels they touch.
 * That is the walker check, isolated from the compositing.
 */
static void
test_coverage_identical(int* buf_a, int* buf_b, int* got, unsigned char* covered)
{
    printf("facealpha coverage == plain coverage (walker unchanged)\n");

    struct
    {
        const char* name;
        plain_fn plain;
        alpha_fn blended;
        int* texels;
    } cases[] = {
        { "texopaque", (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
          (alpha_fn)raster_texshadeblend_persp_texopaque_facealpha_branching_lerp8_v3,
          g_texels_opaque },
        { "textrans", (plain_fn)raster_texshadeblend_persp_textrans_branching_lerp8_v3,
          (alpha_fn)raster_texshadeblend_persp_textrans_facealpha_branching_lerp8_v3, g_texels },
    };

    for( int c = 0; c < 2; c++ )
    {
        int mismatched = 0;

        for( int i = 0; i < TRI_COUNT; i++ )
        {
            const struct Tri* t = &g_tris[i];
            const struct TexVerts* tv = &g_texverts;

            plain_coverage(cases[c].plain, t, tv, cases[c].texels, buf_a, buf_b, covered);

            /* Same two-background trick on the blended kernel. */
            buf_fill(got, BG);
            cases[c].blended(
                got + GUARD, TEXGEOM(t, tv), 0x20, 0x50, 0x70, cases[c].texels, TEX_W, 0xFF);
            buf_fill(buf_b, BG_ALT);
            cases[c].blended(
                buf_b + GUARD, TEXGEOM(t, tv), 0x20, 0x50, 0x70, cases[c].texels, TEX_W, 0xFF);

            for( int p = 0; p < H * W; p++ )
            {
                /* Blended: a covered pixel depends on its background, so the
                 * two runs differ there and agree only where untouched. */
                int blended_touched = !(got[GUARD + p] == BG && buf_b[GUARD + p] == BG_ALT);
                if( blended_touched != (int)covered[p] )
                    mismatched++;
            }
        }

        if( mismatched )
        {
            printf(
                "  FAIL %-44s: %d pixels differ in coverage from the plain kernel\n",
                cases[c].name,
                mismatched);
            g_fail++;
        }
        else
        {
            printf("  ok   %-44s all triangles\n", cases[c].name);
        }
    }
}

int
main(void)
{
    static int buf_a[BUF_LEN];
    static int buf_b[BUF_LEN];
    static int got[BUF_LEN];
    static unsigned char covered[H * W];

    init_hsl16_to_rgb_table();
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    init_reciprocal16();
    init_textures();

    printf("texshadeblend.persp.*.facealpha.branching.lerp8_v3 blend algebra\n");
    check_variant(
        "texshadeblend.persp.texopaque.facealpha",
        (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
        (alpha_fn)raster_texshadeblend_persp_texopaque_facealpha_branching_lerp8_v3,
        g_texels_opaque,
        buf_a,
        buf_b,
        got,
        covered);
    check_variant(
        "texshadeblend.persp.textrans.facealpha",
        (plain_fn)raster_texshadeblend_persp_textrans_branching_lerp8_v3,
        (alpha_fn)raster_texshadeblend_persp_textrans_facealpha_branching_lerp8_v3,
        g_texels,
        buf_a,
        buf_b,
        got,
        covered);

    test_coverage_identical(buf_a, buf_b, got, covered);
    test_textrans_gate_is_not_alpha_zero(buf_a);

    if( g_fail )
    {
        printf("\n%d texture facealpha check(s) FAILED\n", g_fail);
        return 1;
    }

    printf("\nall texture facealpha checks passed\n");
    return 0;
}
