/*
 * Regression test for the perspective texture span uv mapping.
 *
 * Background: docs/qbd_toridraw_streaks_debug.md, hypothesis J. When a face's
 * texture plane passes close to the eye - which is what the camera sitting
 * inside the Queen Black Dragon's bounding sphere does - the homogeneous
 * divisor `w` goes small and the uv quotients blow up. The span kernels walk 8
 * pixels at a time and fit a straight line between the exact uv at the block's
 * endpoints, and three things went wrong there at once:
 *
 *   - `cur_v << texture_shift` and `v_scan += step_v` overflowed int (the
 *     UBSan abort at tex.span.scalar.u.c in the fix2 capture),
 *   - the float reciprocal lost the low bits of v, which are exactly the bits
 *     `v_mask` keeps, and
 *   - the linear fit kept being used across blocks that sweep the whole
 *     texture, smearing rows into a streak.
 *
 * The test drives each perspective span kernel directly and compares the texel
 * every pixel actually sampled against an exact per-pixel divide - the mapping
 * the reference rasterizer uses for every pixel
 * (docs/raster_scanlines_thedaneeffect.txt).
 *
 * The texture encodes its own coordinates and the shade is the identity
 * multiplier (256), so a framebuffer word decodes straight back to the (u, v)
 * the kernel sampled. lerp8 is an approximation, so benign spans are allowed a
 * texel or two; what this catches is an error of tens of texel rows.
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/span_uv 3rd/toridraw/toridraw_texture_span_uv_test.c -lm
 *   /tmp/span_uv
 *
 * Scalar kernels (what TORIDRAW_NO_SIMD=1 builds):
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -DNEON_DISABLED=1 -DAVX2_DISABLED=1 -DSSE41_DISABLED=1 -DSSE2_DISABLED=1 \
 *      -o /tmp/span_uv_scalar 3rd/toridraw/toridraw_texture_span_uv_test.c -lm
 *   /tmp/span_uv_scalar
 *
 * With the overflow check the live client uses:
 *   cc -std=c11 -O1 -g -Wall -Wextra -I3rd/toridraw \
 *      -DNEON_DISABLED=1 -DAVX2_DISABLED=1 -DSSE41_DISABLED=1 -DSSE2_DISABLED=1 \
 *      -fsanitize=signed-integer-overflow -fno-sanitize-recover=all \
 *      -o /tmp/span_uv_ubsan 3rd/toridraw/toridraw_texture_span_uv_test.c -lm
 *   /tmp/span_uv_ubsan
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
// clang-format on

#define SCREEN_W 512
#define TEX_W 128
#define TEX_LEN (TEX_W * TEX_W)

/* shade_blend(base, 256) == base, so a pixel decodes back to its texel. */
#define SHADE_IDENTITY_ISH8 (256 << 8)
#define TEXEL_BIAS 0x010101
#define UNTOUCHED 0

/* An 8-pixel linear fit of a hyperbola is allowed to land a texel or two off
 * inside the block. Anything past this is not interpolation error. */
#define TOLERATED_TEXEL_ERROR 4

static int g_texels[TEX_LEN];
static int g_ref[SCREEN_W];
static int g_got[SCREEN_W];
static int g_fail;

static void
texels_init(void)
{
    for( int v = 0; v < TEX_W; v++ )
        for( int u = 0; u < TEX_W; u++ )
            g_texels[u + v * TEX_W] = TEXEL_BIAS + (v << 8) + u;
}

/* The texture tiles, so row 127 and row 0 are neighbours, not 127 apart. */
static int
wrap_dist(int a, int b)
{
    int d = (a - b) & (TEX_W - 1);
    return d > TEX_W / 2 ? TEX_W - d : d;
}

/*
 * Exact per-pixel span in ToriDraw's units, where cur_u/cur_v are texel indices
 * rather than the reference client's pre-shifted pair. Everything that could
 * overflow is done unsigned, so the reference is itself free of UB and equals
 * the infinite-precision answer masked to the texture.
 */
static void
reference_span(
    int* dst,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    const int* texels,
    int texture_width)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;

    for( int x = x0; x < x1; x++ )
    {
        int w = cw >> shift;
        if( w != 0 )
        {
            int u = clamp(au / w, 0, texture_width - 1);
            int v = (int)(((uint32_t)(bv / w) << shift) & (uint32_t)v_mask);
            dst[x] = texels[u + v];
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
    }
}

struct Case
{
    const char* name;
    int au, bv, cw;
    int step_au, step_bv, step_cw;
};

/*
 * au/bv/cw are the values at screen centre, matching the kernels' convention.
 * ToriDraw_TexturePlanePrepare32 guarantees they stay inside int across the
 * viewport, and these cases respect that.
 *
 * The pathological cases put w in the ones-to-hundreds range against a large
 * bv, which is what a texture plane passing near the eye produces, and which
 * matches the |quotient| ~ 1e7 the fix2 NDJSON capture recorded.
 */
static const struct Case g_cases[] = {
    { "benign-floor", 1 << 20, 1 << 21, 1 << 22, 64, 512, 256 },
    { "benign-wall", 1 << 18, 1 << 19, 1 << 20, 32, 64, 128 },
    { "w-small-bigv", 1 << 24, 2000000000, 200 << 7, 1024, 4096, 64 },
    { "w-crosses-zero", 1 << 22, 1500000000, 0, 512, 8192, 4096 },
    { "w-crosses-zero-neg", -(1 << 22), -1500000000, 0, -512, -8192, 4096 },
    { "w-one", 1 << 20, 2000000000, 1 << 7, 16, 1024, 16 },
    { "huge-bv-w-one", 2000000000, 2100000000, 1 << 7, 4096, 65536, 8 },
};

#define CASE_COUNT ((int)(sizeof(g_cases) / sizeof(g_cases[0])))

static void
run_case(const struct Case* c, int transparent, int v3)
{
    const int x0_ish16 = 0;
    const int x1_ish16 = (SCREEN_W - 1) << 16;

    for( int i = 0; i < SCREEN_W; i++ )
        g_ref[i] = g_got[i] = UNTOUCHED;

    reference_span(
        g_ref,
        SCREEN_W,
        x0_ish16,
        x1_ish16,
        c->au,
        c->bv,
        c->cw,
        c->step_au,
        c->step_bv,
        c->step_cw,
        g_texels,
        TEX_W);

    if( v3 && transparent )
        draw_texture_scanline_transparent_blend_branching_lerp8_v3_ordered(
            g_got, SCREEN_W, x0_ish16, x1_ish16, 0,
            c->au, c->bv, c->cw, c->step_au, c->step_bv, c->step_cw,
            SHADE_IDENTITY_ISH8, 0, g_texels, TEX_W);
    else if( v3 )
        draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
            g_got, SCREEN_W, x0_ish16, x1_ish16, 0,
            c->au, c->bv, c->cw, c->step_au, c->step_bv, c->step_cw,
            SHADE_IDENTITY_ISH8, 0, g_texels, TEX_W);
    else if( transparent )
        draw_texture_scanline_transparent_blend_branching_lerp8_ordered(
            g_got, SCREEN_W, SCREEN_W, 1, x0_ish16, x1_ish16, 0,
            c->au, c->bv, c->cw, c->step_au, c->step_bv, c->step_cw,
            SHADE_IDENTITY_ISH8, 0, g_texels, TEX_W);
    else
        draw_texture_scanline_opaque_blend_branching_lerp8_ordered(
            g_got, SCREEN_W, SCREEN_W, 1, x0_ish16, x1_ish16, 0,
            c->au, c->bv, c->cw, c->step_au, c->step_bv, c->step_cw,
            SHADE_IDENTITY_ISH8, 0, g_texels, TEX_W);

    int max_du = 0;
    int max_dv = 0;
    int bad = 0;
    int first_bad = -1;
    int compared = 0;

    for( int i = 0; i < SCREEN_W; i++ )
    {
        /* w == 0 on one side is a skipped pixel, not a uv disagreement. */
        if( g_ref[i] == UNTOUCHED || g_got[i] == UNTOUCHED )
            continue;
        compared++;

        int r = g_ref[i] - TEXEL_BIAS;
        int g = g_got[i] - TEXEL_BIAS;
        int du = wrap_dist(g & 0xFF, r & 0xFF);
        int dv = wrap_dist((g >> 8) & 0xFF, (r >> 8) & 0xFF);

        if( du > max_du )
            max_du = du;
        if( dv > max_dv )
            max_dv = dv;
        if( du > TOLERATED_TEXEL_ERROR || dv > TOLERATED_TEXEL_ERROR )
        {
            if( first_bad < 0 )
                first_bad = i;
            bad++;
        }
    }

    printf(
        "  %-20s %-3s %-11s cmp=%3d  max_du=%3d  max_dv=%3d  over_tol=%3d",
        c->name,
        v3 ? "v3" : "-",
        transparent ? "transparent" : "opaque",
        compared,
        max_du,
        max_dv,
        bad);
    if( bad )
    {
        printf("  FAIL (first bad pixel x=%d)\n", first_bad);
        g_fail = 1;
    }
    else
    {
        printf("\n");
    }
}

int
main(void)
{
    texels_init();

    printf("perspective texture span uv vs exact per-pixel divide\n");
    for( int i = 0; i < CASE_COUNT; i++ )
    {
        run_case(&g_cases[i], 1, 1);
        run_case(&g_cases[i], 0, 1);
        run_case(&g_cases[i], 1, 0);
        run_case(&g_cases[i], 0, 0);
    }

    printf(
        "\n%s\n",
        g_fail ? "texture span uv: FAILED"
               : "texture span uv: all spans within interpolation tolerance");
    return g_fail;
}
