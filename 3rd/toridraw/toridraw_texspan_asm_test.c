/*
 * Compare mode for the hand-written perspective texture span kernel.
 *
 *   make -C src test-texspan-asm
 *
 * The C kernel in tex.span.sse2.u.c is the reference. For every span replayed
 * here the hand-written twin must reproduce it BYTE FOR BYTE, and must not
 * touch a pixel outside the span it was handed. Both halves matter: a texture
 * kernel that is merely close produces the streaking documented in
 * docs/qbd_toridraw_streaks_debug.md, which reads as a wrong-looking surface
 * rather than a wrong pixel, and a kernel that runs one pixel long corrupts a
 * neighbouring span that was already correct.
 *
 * WHAT THE SPANS ARE
 *
 * Random within the ranges the rasterizer actually produces, plus the awkward
 * cases on purpose: w at and around zero (the block has no far endpoint), uv
 * quotients past the float reciprocal's exact range (the helper must fall back
 * to an integer divide), and v deltas wider than a texture tile (the linear fit
 * is refused and the block goes per pixel). Those three are exactly the paths a
 * hand-written fast path is tempted to drop, so they are generated deliberately
 * rather than hoped for.
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "toridraw_types.h"

int g_toridraw_raster_scanline = 0;

#if defined(TORIDRAW_ABLATE) && TORIDRAW_ABLATE
int g_toridraw_ablate = -1;
int g_toridraw_ablate_tex = -1;
#endif

/* Lives in projection.u.c, which the unity build pulls in ahead of the span
 * file. Only the affine ish16 kernels call it and this harness exercises the
 * perspective ones, but the translation unit still has to compile. */
static inline int
toridraw_add_mul32(int base, int step, int distance)
{
    unsigned const bits = (unsigned)base + (unsigned)step * (unsigned)distance;
    if( bits <= (unsigned)INT_MAX )
        return (int)bits;
    return -1 - (int)(UINT_MAX - bits);
}

// clang-format off
#include "graphics/shared_tables.c"
#include "graphics/raster/texture/span/tex.span_uv.h"
#include "impl/raster/span/span.tex.sse2.u.c"
#include "graphics/raster/texture/span/tex_span_asm.h"
// clang-format on

#define FB_WIDTH 765
#define FB_HEIGHT 503
#define FB_PIXELS (FB_WIDTH * FB_HEIGHT)
#define FB_GUARD 4096
#define FB_ALLOC (FB_PIXELS + 2 * FB_GUARD)

/* Deterministic; a compare failure has to be reproducible from the seed alone. */
static uint32_t g_rng = 0x9E3779B9u;

static uint32_t
rng_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rng_range(int lo, int hi)
{
    assert(hi >= lo);
    return lo + (int)(rng_next() % (uint32_t)(hi - lo + 1));
}

struct SpanCase
{
    int screen_x0_ish16;
    int screen_x1_ish16;
    int pixel_offset;
    int au;
    int bv;
    int cw;
    int step_au_dx;
    int step_bv_dx;
    int step_cw_dx;
    int shade8bit_ish8;
    int step_shade8bit_dx_ish8;
    int texture_width;
};

/* Four shapes, so the awkward paths are generated rather than hoped for. */
enum
{
    SHAPE_TYPICAL = 0,
    SHAPE_NEAR_ZERO_W,
    SHAPE_HUGE_UV,
    SHAPE_WIDE_V,
    SHAPE_COUNT
};

static void
span_case_make(struct SpanCase* c, int shape)
{
    assert(c);

    int x0 = rng_range(0, FB_WIDTH - 2);
    int x1 = rng_range(x0 + 1, FB_WIDTH - 1);
    c->screen_x0_ish16 = (x0 << 16) + 1;
    c->screen_x1_ish16 = x1 << 16;
    c->pixel_offset = rng_range(0, FB_HEIGHT - 1) * FB_WIDTH;
    c->texture_width = (rng_next() & 1) ? 128 : 64;
    c->shade8bit_ish8 = rng_range(0, 255) << 8;
    c->step_shade8bit_dx_ish8 = rng_range(-4096, 4096);

    switch( shape )
    {
    case SHAPE_NEAR_ZERO_W:
        /* cw crosses zero inside the span: blocks with no far endpoint, and
         * blocks whose w is 1, where the reciprocal is least helpful. */
        c->cw = rng_range(-4096, 4096) << 6;
        c->step_cw_dx = rng_range(-256, 256);
        c->au = rng_range(-(1 << 20), 1 << 20);
        c->bv = rng_range(-(1 << 20), 1 << 20);
        c->step_au_dx = rng_range(-(1 << 12), 1 << 12);
        c->step_bv_dx = rng_range(-(1 << 12), 1 << 12);
        break;

    case SHAPE_HUGE_UV:
        /* Quotients past TEX_SPAN_RECIPROCAL_EXACT_LIMIT, so the helpers must
         * take the integer-divide fallback rather than trust the float. */
        c->cw = rng_range(1, 64) << 6;
        c->step_cw_dx = rng_range(-8, 8);
        c->au = rng_range(-(1 << 28), 1 << 28);
        c->bv = rng_range(-(1 << 28), 1 << 28);
        c->step_au_dx = rng_range(-(1 << 16), 1 << 16);
        c->step_bv_dx = rng_range(-(1 << 16), 1 << 16);
        break;

    case SHAPE_WIDE_V:
        /* v sweeps more than a tile per block: tex_span_lerp8_fits refuses the
         * linear fit and the block must fall to the per-pixel path. */
        c->cw = rng_range(64, 8192) << 6;
        c->step_cw_dx = rng_range(-64, 64);
        c->au = rng_range(-(1 << 24), 1 << 24);
        c->bv = rng_range(-(1 << 26), 1 << 26);
        c->step_au_dx = rng_range(-(1 << 16), 1 << 16);
        c->step_bv_dx = rng_range(-(1 << 18), 1 << 18);
        break;

    case SHAPE_TYPICAL:
    default:
        /* What the rasterizer mostly issues: w comfortably positive, uv moving
         * a fraction of a tile per block, the linear fit passing. */
        c->cw = rng_range(256, 65536) << 6;
        c->step_cw_dx = rng_range(-1024, 1024);
        c->au = rng_range(-(1 << 22), 1 << 22);
        c->bv = rng_range(-(1 << 22), 1 << 22);
        c->step_au_dx = rng_range(-(1 << 14), 1 << 14);
        c->step_bv_dx = rng_range(-(1 << 14), 1 << 14);
        break;
    }
}

int
main(void)
{
    int* texels128 = malloc(128 * 128 * sizeof(int));
    assert(texels128);
    int* texels64 = malloc(64 * 64 * sizeof(int));
    assert(texels64);
    for( int i = 0; i < 128 * 128; i++ )
        texels128[i] = (int)(rng_next() & 0x00FFFFFFu);
    for( int i = 0; i < 64 * 64; i++ )
        texels64[i] = (int)(rng_next() & 0x00FFFFFFu);
    /* The transparency sentinel is present, so a transparent twin added later
     * to this harness sees it without the texture having to change. */
    for( int i = 0; i < 128 * 128; i += 7 )
        texels128[i] = 0;
    for( int i = 0; i < 64 * 64; i += 5 )
        texels64[i] = 0;

    int* ref_alloc = malloc(FB_ALLOC * sizeof(int));
    int* asm_alloc = malloc(FB_ALLOC * sizeof(int));
    assert(ref_alloc);
    assert(asm_alloc);

    int* ref_fb = ref_alloc + FB_GUARD;
    int* asm_fb = asm_alloc + FB_GUARD;

    const int kSpansPerShape = 2000;
    int mismatches = 0;
    int drawn = 0;
    long long pixels = 0;

    for( int shape = 0; shape < SHAPE_COUNT; shape++ )
    {
        for( int n = 0; n < kSpansPerShape; n++ )
        {
            struct SpanCase c;
            span_case_make(&c, shape);
            int* texels = (c.texture_width == 128) ? texels128 : texels64;

            /* Identical starting contents, so a difference is the kernel's. */
            for( int i = 0; i < FB_ALLOC; i++ )
            {
                int v = (int)(rng_next() & 0x00FFFFFFu);
                ref_alloc[i] = v;
                asm_alloc[i] = v;
            }

            draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
                ref_fb,
                FB_WIDTH,
                c.screen_x0_ish16,
                c.screen_x1_ish16,
                c.pixel_offset,
                c.au,
                c.bv,
                c.cw,
                c.step_au_dx,
                c.step_bv_dx,
                c.step_cw_dx,
                c.shade8bit_ish8,
                c.step_shade8bit_dx_ish8,
                texels,
                c.texture_width);

            toridraw_texspan_opaque_lerp8_v3_xrgb8888_asm(
                asm_fb,
                FB_WIDTH,
                c.screen_x0_ish16,
                c.screen_x1_ish16,
                c.pixel_offset,
                c.au,
                c.bv,
                c.cw,
                c.step_au_dx,
                c.step_bv_dx,
                c.step_cw_dx,
                c.shade8bit_ish8,
                c.step_shade8bit_dx_ish8,
                texels,
                c.texture_width);

            if( memcmp(ref_alloc, asm_alloc, (size_t)FB_ALLOC * sizeof(int)) != 0 )
            {
                int first = -1;
                for( int i = 0; i < FB_ALLOC; i++ )
                    if( ref_alloc[i] != asm_alloc[i] )
                    {
                        first = i;
                        break;
                    }
                if( mismatches < 8 )
                    printf(
                        "MISMATCH shape=%d span=%d at pixel %d (span offset %d): "
                        "ref=%08X asm=%08X\n"
                        "  x0i=%d x1i=%d off=%d au=%d bv=%d cw=%d sau=%d sbv=%d "
                        "scw=%d sh=%d ssh=%d tw=%d\n",
                        shape,
                        n,
                        first - FB_GUARD,
                        c.pixel_offset,
                        (unsigned)ref_alloc[first],
                        (unsigned)asm_alloc[first],
                        c.screen_x0_ish16,
                        c.screen_x1_ish16,
                        c.pixel_offset,
                        c.au,
                        c.bv,
                        c.cw,
                        c.step_au_dx,
                        c.step_bv_dx,
                        c.step_cw_dx,
                        c.shade8bit_ish8,
                        c.step_shade8bit_dx_ish8,
                        c.texture_width);
                mismatches++;
            }

            int x0 = (c.screen_x0_ish16 - 1) >> 16;
            if( x0 < 0 )
                x0 = 0;
            int x1 = c.screen_x1_ish16 >> 16;
            if( x1 >= FB_WIDTH )
                x1 = FB_WIDTH - 1;
            if( x0 < x1 )
            {
                drawn++;
                pixels += x1 - x0;
            }
        }
    }

    /*
     * Timing.
     *
     * The scene bench reports render p50 to about +/-1-3% and the whole texture
     * fill is a quarter of it, so a kernel that wins a fifth of the fill moves
     * render by about the harness's own spread. Replaying spans with nothing
     * else in the loop separates the two kernels; the scene bench afterwards
     * says whether the win survives contact with a real frame.
     *
     * TYPICAL only. The other three shapes exist to prove correctness on paths
     * the rasterizer takes rarely, and timing against them would rank the
     * kernels on work real scenes barely do. The two alternate every repeat and
     * the best pass wins, because this machine drifts under both.
     */
    if( mismatches == 0 || getenv("TEXSPAN_TIME_ANYWAY") )
    {
        const int kCases = 512;
        const int kRepeats = 80;
        struct SpanCase* cases = malloc((size_t)kCases * sizeof(*cases));
        assert(cases);
        long long timed_pixels = 0;
        for( int i = 0; i < kCases; i++ )
        {
            span_case_make(&cases[i], SHAPE_TYPICAL);
            int x0 = (cases[i].screen_x0_ish16 - 1) >> 16;
            if( x0 < 0 )
                x0 = 0;
            int x1 = cases[i].screen_x1_ish16 >> 16;
            if( x1 >= FB_WIDTH )
                x1 = FB_WIDTH - 1;
            if( x0 < x1 )
                timed_pixels += x1 - x0;
        }

        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        double best[2] = { 1e30, 1e30 };

        for( int r = 0; r < kRepeats; r++ )
        {
            for( int which = 0; which < 2; which++ )
            {
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);
                for( int i = 0; i < kCases; i++ )
                {
                    struct SpanCase* c = &cases[i];
                    int* tx = (c->texture_width == 128) ? texels128 : texels64;
                    if( which == 0 )
                        draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
                            ref_fb, FB_WIDTH, c->screen_x0_ish16,
                            c->screen_x1_ish16, c->pixel_offset, c->au, c->bv,
                            c->cw, c->step_au_dx, c->step_bv_dx, c->step_cw_dx,
                            c->shade8bit_ish8, c->step_shade8bit_dx_ish8, tx,
                            c->texture_width);
                    else
                        toridraw_texspan_opaque_lerp8_v3_xrgb8888_asm(
                            asm_fb, FB_WIDTH, c->screen_x0_ish16,
                            c->screen_x1_ish16, c->pixel_offset, c->au, c->bv,
                            c->cw, c->step_au_dx, c->step_bv_dx, c->step_cw_dx,
                            c->shade8bit_ish8, c->step_shade8bit_dx_ish8, tx,
                            c->texture_width);
                }
                QueryPerformanceCounter(&t1);
                double ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 /
                            (double)freq.QuadPart;
                if( ns < best[which] )
                    best[which] = ns;
            }
        }

        printf(
            "timing: %d spans, %lld px, best of %d interleaved passes\n"
            "  C    %8.1f us   %6.3f ns/px\n"
            "  asm  %8.1f us   %6.3f ns/px    %+.1f%%\n",
            kCases,
            timed_pixels,
            kRepeats,
            best[0] / 1000.0,
            best[0] / (double)timed_pixels,
            best[1] / 1000.0,
            best[1] / (double)timed_pixels,
            100.0 * (best[1] - best[0]) / best[0]);
        free(cases);
    }

    printf(
        "texspan asm compare: %d spans, %d non-empty, %lld pixels, %d mismatches\n",
        SHAPE_COUNT * kSpansPerShape,
        drawn,
        pixels,
        mismatches);

    free(texels128);
    free(texels64);
    free(ref_alloc);
    free(asm_alloc);

    if( mismatches )
    {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
