/*
 * The fixed-point setup of the C triangle kernels and texture spans, reached.
 *
 * Every kernel here starts a triangle in 16.16 or 8-bit fixed point:
 * `(dx << 16) / dy`, `x0 << 16`, a shade plane `(...) << 9`, a texture step
 * `(next_u - cur_u) << (texture_shift - 3)`. Half of all triangles have a
 * negative dx and every clipped one a negative x, and a left shift of a
 * negative int is undefined in C. Nothing else in the build calls several of
 * these kernels (the flat sort.s4 pair, the texshadeflat persp textrans
 * branching kernel) or calls them with such triangles, so no sanitizer run
 * could say so. This test does, under check-ubsan-units, on whichever texture
 * span the build selects (NEON, scalar with NEON_DISABLED, and SSE2, SSE4.1
 * and AVX2 in the x86_64 builds of test-toridraw-setup-shift-x86).
 *
 * It also prints one digest per kernel over the whole triangle set. The
 * digests are not pinned here, because the vector spans are not required to
 * be bit-identical to one another; a change to the setup arithmetic is proved
 * bit-for-bit by running the same build of this test before and after it and
 * comparing the lines (`digest <kernel> <hex>`).
 *
 * What it checks by itself: no kernel writes outside the framebuffer (guard
 * bands), and every kernel draws something for the triangles that cover
 * pixels on screen.
 *
 * Standalone TU, no cache or disk.
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

#include "impl/raster/flat/raster.flat.opaque.nofacealpha.nomodulate.painter.sort.s4.scalar.u.c"
#include "impl/raster/flat/raster.flat.alpha.nofacealpha.nomodulate.painter.sort.s4.scalar.u.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.sort.s1.scalar.u.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s1.scalar.c"
#include "impl/raster/gouraudrgb/raster.gouraudrgb.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudrgb/raster.gouraudrgb.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"

#include "impl/projection/projection.scalar_reference.u.c"
#include "impl/raster/span/span.tex.dispatch.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.texopaque.nofacealpha.nomodulate.painter.scanline.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.textrans.nofacealpha.nomodulate.painter.scanline.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeflat.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.affine.texopaque.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.affine.textrans.nofacealpha.nomodulate.painter.branching.lerp8.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.affine.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texshadeblend.affine.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
// clang-format on

#define W 240
#define H 160
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

static toripixel_t g_buf[BUF_LEN];
static int g_texels64[64 * 64];
static int g_texels128[128 * 128];
static int g_failures;
static int g_checks;

#define SENTINEL ((toripixel_t)0x5A)

struct Tri
{
    const char* name;
    int x[3];
    int y[3];
    int on_screen; /* covers at least one pixel centre on screen */
};

/* Both windings, every clip edge, slivers and the degenerate cases. Negative
 * dx and negative x are what the setup shifts used to meet. */
static const struct Tri g_tris[] = {
    { "interior", { 40, 190, 90 }, { 20, 35, 140 }, 1 },
    { "interior-cw", { 90, 190, 40 }, { 140, 35, 20 }, 1 },
    { "leaning-left", { 200, 20, 120 }, { 10, 60, 150 }, 1 },
    { "flat-top", { 30, 200, 95 }, { 30, 30, 130 }, 1 },
    { "flat-bottom", { 30, 200, 95 }, { 130, 130, 30 }, 1 },
    { "sliver-row", { 20, 220, 100 }, { 60, 60, 61 }, 0 },
    { "sliver-col", { 100, 101, 100 }, { 20, 70, 140 }, 0 },
    { "zero-area", { 20, 60, 100 }, { 20, 60, 100 }, 0 },
    { "point", { 50, 50, 50 }, { 50, 50, 50 }, 0 },
    { "clip-left", { -120, 60, 10 }, { 20, 40, 150 }, 1 },
    { "clip-right", { 160, 420, 210 }, { 20, 40, 150 }, 1 },
    { "clip-top", { 40, 190, 90 }, { -90, -30, 80 }, 1 },
    { "clip-bottom", { 40, 190, 90 }, { 60, 100, 300 }, 1 },
    { "clip-corner-tl", { -60, 90, -10 }, { -50, 30, 80 }, 1 },
    { "clip-corner-br", { 150, 400, 220 }, { 100, 150, 300 }, 1 },
    { "covers-screen", { -500, 700, 100 }, { -400, -400, 600 }, 1 },
    { "left-of-screen", { -500, -300, -400 }, { 20, 40, 140 }, 0 },
    { "right-of-screen", { 500, 700, 600 }, { 20, 40, 140 }, 0 },
    { "above-screen", { 40, 190, 90 }, { -500, -400, -300 }, 0 },
    { "below-screen", { 40, 190, 90 }, { 300, 400, 500 }, 0 },
};
#define TRI_COUNT ((int)(sizeof(g_tris) / sizeof(g_tris[0])))

/* Camera-space texture basis (origin, u end, v end) for the textured
 * kernels: one facing the camera, one turned so the u and v steps run
 * negative across the screen, one at constant depth (affine == perspective). */
struct TexBasis
{
    const char* name;
    int x[3];
    int y[3];
    int z[3];
};
static const struct TexBasis g_bases[] = {
    { "facing", { -128, 128, -128 }, { -128, -128, 128 }, { 900, 1100, 1000 } },
    { "turned", { 128, -128, 128 }, { 128, 128, -128 }, { 1100, 900, 1000 } },
    { "flat", { -128, 128, -128 }, { -128, -128, 128 }, { 1000, 1000, 1000 } },
};
#define BASIS_COUNT ((int)(sizeof(g_bases) / sizeof(g_bases[0])))

static void
check(
    int ok,
    const char* what,
    const char* kernel,
    const char* tri)
{
    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("  FAIL %s: %s (%s)\n", kernel, what, tri);
    }
}

static void
buf_reset(void)
{
    for( int i = 0; i < BUF_LEN; i++ )
        g_buf[i] = SENTINEL;
}

static uint64_t
fnv(
    uint64_t h,
    const void* data,
    size_t size)
{
    const unsigned char* p = data;
    for( size_t i = 0; i < size; i++ )
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* After one draw: the guard bands are untouched; returns how many on-screen
 * pixels were written, and folds the framebuffer into the kernel's digest. */
static int
after_draw(
    const char* kernel,
    const struct Tri* t,
    uint64_t* digest)
{
    int guard_ok = 1;
    int written = 0;
    for( int i = 0; i < GUARD; i++ )
    {
        if( g_buf[i] != SENTINEL || g_buf[GUARD + W * H + i] != SENTINEL )
            guard_ok = 0;
    }
    check(guard_ok, "nothing is written outside the framebuffer", kernel, t->name);
    for( int i = 0; i < W * H; i++ )
    {
        if( g_buf[GUARD + i] != SENTINEL )
            written++;
    }
    *digest = fnv(*digest, g_buf + GUARD, sizeof(toripixel_t) * W * H);
    return written;
}

#define FOR_EACH_TRI(KERNEL, DRAW)                                                                 \
    do                                                                                             \
    {                                                                                              \
        uint64_t digest_ = 1469598103934665603ull;                                                 \
        for( int i_ = 0; i_ < TRI_COUNT; i_++ )                                                    \
        {                                                                                          \
            const struct Tri* t = &g_tris[i_];                                                     \
            int written_;                                                                          \
            buf_reset();                                                                           \
            DRAW;                                                                                  \
            written_ = after_draw(KERNEL, t, &digest_);                                            \
            if( t->on_screen )                                                                     \
                check(written_ > 0, "a triangle on screen draws pixels", KERNEL, t->name);         \
        }                                                                                          \
        printf("digest %s %016llx\n", KERNEL, (unsigned long long)digest_);                         \
    } while( 0 )

#define GEOM W, W, H, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2]

static void
test_solid(void)
{
    FOR_EACH_TRI("flat.opaque.sort.s4", raster_flat_screen_opaque_sort_s4(g_buf + GUARD, GEOM, 0x5a37));
    FOR_EACH_TRI("flat.alpha.sort.s4", raster_flat_screen_alpha_sort_s4(g_buf + GUARD, GEOM, 0x5a37, 0x80));
    FOR_EACH_TRI(
        "gouraudhsllightness.opaque.sort.s1",
        raster_gouraudhsllightness_screen_opaque_bary_sort_s1(
            g_buf + GUARD, GEOM, 0x1240, 0x9a7f, 0x3c10));
    FOR_EACH_TRI(
        "gouraudhsllightness.opaque.branching.s4",
        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            g_buf + GUARD, GEOM, 0x1240, 0x9a7f, 0x3c10));
    FOR_EACH_TRI(
        "gouraudhsllightness.alpha.branching.s4",
        raster_gouraudhsllightness_screen_alpha_bary_branching_s4(
            g_buf + GUARD, GEOM, 0x1240, 0x9a7f, 0x3c10, 0x60));
    FOR_EACH_TRI(
        "gouraudhsllightness.alpha.branching.s1",
        raster_gouraudhsllightness_screen_alpha_bary_branching_s1(
            g_buf + GUARD, GEOM, 0x1240, 0x9a7f, 0x3c10, 0x60));
    FOR_EACH_TRI(
        "gouraudrgb.opaque.branching.s4",
        raster_gouraudrgb_screen_opaque_bary_branching_s4(
            g_buf + GUARD, GEOM, 0xff2010, 0x10ff20, 0x2010ff));
    FOR_EACH_TRI(
        "gouraudrgb.alpha.branching.s4",
        raster_gouraudrgb_screen_alpha_bary_branching_s4(
            g_buf + GUARD, GEOM, 0xff2010, 0x10ff20, 0x2010ff, 0x90));
}

#define TGEOM(B)                                                                                   \
    W, W, H, 512, t->x[0], t->x[1], t->x[2], t->y[0], t->y[1], t->y[2], (B)->x[0], (B)->x[1],      \
        (B)->x[2], (B)->y[0], (B)->y[1], (B)->y[2], (B)->z[0], (B)->z[1], (B)->z[2]

static void
test_textured_with(
    int* texels,
    int texture_width)
{
    char name[128];
    for( int b = 0; b < BASIS_COUNT; b++ )
    {
        const struct TexBasis* basis = &g_bases[b];

#define TEXNAME(K) (snprintf(name, sizeof(name), "%s.%s.tex%d", K, basis->name, texture_width), name)
        FOR_EACH_TRI(
            TEXNAME("texshadeflat.persp.texopaque.lerp8"),
            raster_texshadeflat_persp_texopaque_branching_lerp8(
                g_buf + GUARD, TGEOM(basis), 0x50, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeflat.persp.textrans.lerp8"),
            raster_texshadeflat_persp_textrans_branching_lerp8(
                g_buf + GUARD, TGEOM(basis), 0x50, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeblend.affine.texopaque.lerp8"),
            raster_texshadeblend_affine_texopaque_branching_lerp8(
                g_buf + GUARD, TGEOM(basis), 0x10, 0x7f, 0x40, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeblend.affine.textrans.lerp8"),
            raster_texshadeblend_affine_textrans_branching_lerp8(
                g_buf + GUARD, TGEOM(basis), 0x10, 0x7f, 0x40, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeblend.persp.texopaque.lerp8_v3"),
            raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
                g_buf + GUARD, TGEOM(basis), 0x10, 0x7f, 0x40, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeblend.persp.textrans.lerp8_v3"),
            raster_texshadeblend_persp_textrans_branching_lerp8_v3(
                g_buf + GUARD, TGEOM(basis), 0x10, 0x7f, 0x40, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeblend.affine.texopaque.lerp8_v3"),
            raster_texshadeblend_affine_texopaque_branching_lerp8_v3(
                g_buf + GUARD, TGEOM(basis), 0x10, 0x7f, 0x40, texels, texture_width));
        FOR_EACH_TRI(
            TEXNAME("texshadeblend.affine.textrans.lerp8_v3"),
            raster_texshadeblend_affine_textrans_branching_lerp8_v3(
                g_buf + GUARD, TGEOM(basis), 0x10, 0x7f, 0x40, texels, texture_width));
#undef TEXNAME
    }
}

static void
fill_texture(
    int* texels,
    int width)
{
    /* A checkerboard of two colours with a transparent quadrant, so the
     * textrans kernels skip some texels and draw others. */
    for( int v = 0; v < width; v++ )
    {
        for( int u = 0; u < width; u++ )
        {
            int texel = ((u >> 3) ^ (v >> 3)) & 1 ? 0x00c08040 : 0x003060a0;
            if( u >= width / 2 && v >= width / 2 )
                texel = 0;
            texels[u + v * width] = texel;
        }
    }
}

int
main(void)
{
    init_hsl16_to_pixel_table();
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    init_reciprocal16();
    fill_texture(g_texels64, 64);
    fill_texture(g_texels128, 128);

    test_solid();
    test_textured_with(g_texels64, 64);
    test_textured_with(g_texels128, 128);

    printf("setup_shift_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
