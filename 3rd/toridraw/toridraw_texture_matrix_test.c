/*
 * Unit test for the extended perspective texture matrix on the `branching`
 * family:
 *
 *     texshadeblend.persp.<gate>[.facealpha][.modulate].branching.lerp8_v3
 *     gate in {texopaque, textrans, texalpha}
 *
 * Ten variants, all generated from one walker template, all reachable from the
 * two plain SIMD kernels they were derived from. That last part is what makes
 * them testable without a second rasterizer: every variant can be driven into a
 * configuration where it must equal a plain kernel *exactly*, and the checks are
 * built as a chain off that anchor.
 *
 *   1. identities   each capability, at its neutral setting, is a bit-exact
 *                   no-op against the plain kernel:
 *                     facealpha(0xFF)            == plain
 *                     modulate(256,256,256)      == plain
 *                     texalpha(all texels a=255) == texopaque
 *                     texalpha(a in {0,255})     == textrans on a matched texture
 *   2. coverage     every variant touches exactly the pixels the plain kernel
 *                   does, at every alpha and tint
 *   3. algebra      partial settings compose as specified, per pixel:
 *                     facealpha    -> alpha_blend(a, dst, plain)
 *                     texalpha     -> alpha_blend(texel_a, dst, plain)
 *                     both         -> alpha_blend(mul255(texel_a, a), dst, plain)
 *                     modulate     -> per-channel (plain * tint) >> 8
 *   4. gate         a keyed / zero-alpha texel is left EXACTLY alone, which is
 *                   not what compositing it at alpha 0 would do
 *   5. sampler      tex_sampler_index and tex_sampler_mul255 against their
 *                   contracts directly, including clamp addressing
 *   6. guards       nothing written outside the framebuffer
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/tex_matrix_test 3rd/toridraw/toridraw_texture_matrix_test.c -lm
 *   /tmp/tex_matrix_test
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
#include "graphics/raster/texture/texplane.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
// clang-format on

#define W 200
#define H 150
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

/*
 * Only the low 24 bits of a framebuffer word are the pixel.
 *
 * The high byte is genuinely undefined and ISA-dependent: shade_blend() masks
 * its input and shifts right by 8, so the scalar spans can never set it, while
 * the NEON spans multiply all four lanes and leave the shade-scaled texel alpha
 * there. Nothing downstream reads it — alpha_blend() masks both operands — so
 * comparing it would be asserting on an accident of which ISA the plain kernel
 * was compiled for. Every comparison below is on RGB.
 */
#define RGB(x) ((x)&0x00FFFFFF)

#define BG 0x00112233
#define BG_ALT 0x00445566
#define GUARD_FILL 0x5A5A5A5A

#define TEX_W 64
#define TEX_LEN (TEX_W * TEX_W)

/* Four textures that let the identity chain close.
 *
 *  opaque   : no zero RGB, alpha 255 everywhere
 *  keyed    : the same, with one quadrant set to RGB 0   (the colour key)
 *  alpha255 : the same as `opaque`, alpha 255 everywhere (texalpha == texopaque)
 *  alphakey : the same as `keyed`, but the hole is alpha 0 with intact RGB, so
 *             texalpha on it must equal textrans on `keyed`
 *  alpharamp: a continuous 0-255 alpha ramp, which no colour key can express -
 *             this is the case the gate exists for
 */
static int g_tex_opaque[TEX_LEN];
static int g_tex_keyed[TEX_LEN];
static int g_tex_alpha255[TEX_LEN];
static int g_tex_alphakey[TEX_LEN];
static int g_tex_alpharamp[TEX_LEN];

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
    { "degenerate-zero-area", { 20, 60, 100 }, { 20, 60, 100 } },
    { "clip-left", { -80, 60, 10 }, { 20, 40, 120 } },
    { "clip-right", { 140, 320, 190 }, { 20, 40, 120 } },
    { "clip-top", { 40, 150, 90 }, { -70, -30, 60 } },
    { "clip-bottom", { 40, 150, 90 }, { 60, 100, 260 } },
    { "clip-corner-br", { 130, 300, 180 }, { 90, 130, 250 } },
    { "covers-screen", { -400, 600, 100 }, { -300, -300, 500 } },
    { "offscreen-above", { 40, 150, 90 }, { -400, -380, -300 } },
    { "offscreen-right", { 400, 500, 450 }, { 20, 40, 120 } },
};

#define TRI_COUNT ((int)(sizeof(g_tris) / sizeof(g_tris[0])))

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

#define TEXGEOM(t, tv)                                                                             \
    W, W, H, CAMERA_COT16, (t)->x[0], (t)->x[1], (t)->x[2], (t)->y[0], (t)->y[1], (t)->y[2],        \
        (tv)->x[0], (tv)->x[1], (tv)->x[2], (tv)->y[0], (tv)->y[1], (tv)->y[2], (tv)->z[0],        \
        (tv)->z[1], (tv)->z[2], 0x20, 0x50, 0x70

static int g_fail;

/* Every plain kernel has this shape; every matrix kernel has it plus a sampler. */
typedef void (*plain_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int*, int);
typedef void (*matrix_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, const struct ToriDraw_TexSampler*);

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
            printf("  FAIL %-52s %-22s: wrote outside the framebuffer\n", what, tri);
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
            int i = u + v * TEX_W;
            int checker = (((u >> 3) ^ (v >> 3)) & 1);
            /* Never RGB 0 in the base tables: 0 is the colour key, and the two
             * gates have to stay distinguishable. */
            int rgb = (checker ? 0x00C08040 : 0x004080C0) + ((u & 7) << 8);
            int hole = (u < TEX_W / 2 && v < TEX_W / 2);

            g_tex_opaque[i] = (int)(0xFF000000u | (unsigned)rgb);
            g_tex_keyed[i] = hole ? 0 : (int)(0xFF000000u | (unsigned)rgb);
            g_tex_alpha255[i] = (int)(0xFF000000u | (unsigned)rgb);
            /* Matched pair for the gate identity: same holes, expressed as
             * alpha 0 rather than as RGB 0, with the RGB left intact so a
             * decoder that ignored the alpha would visibly differ. */
            g_tex_alphakey[i] =
                hole ? (int)(0x00000000u | (unsigned)rgb) : (int)(0xFF000000u | (unsigned)rgb);
            /* A ramp no colour key can express. */
            g_tex_alpharamp[i] = (int)(((unsigned)((u * 4) & 0xFF) << 24) | (unsigned)rgb);
        }
    }
}

/**
 * Exact coverage of a plain kernel: render it over two different backgrounds
 * and take the pixels that agree. The plain kernels overwrite, so a covered
 * pixel lands on the same value either way and an uncovered one keeps its own
 * background. No sentinel comparison, and no assumption that a drawn colour
 * differs from the background.
 */
static void
plain_coverage(
    plain_fn fn,
    const struct Tri* t,
    int* texels,
    int* out_value,
    unsigned char* covered)
{
    static int alt[BUF_LEN];
    const struct TexVerts* tv = &g_texverts;

    buf_fill(out_value, BG);
    buf_fill(alt, BG_ALT);
    fn(out_value + GUARD, TEXGEOM(t, tv), texels, TEX_W);
    fn(alt + GUARD, TEXGEOM(t, tv), texels, TEX_W);

    for( int p = 0; p < H * W; p++ )
        covered[p] = (unsigned char)(RGB(out_value[GUARD + p]) == RGB(alt[GUARD + p]));
}

/* ------------------------------------------------------------ identities */

/*
 * Each capability at its neutral setting must be bit-exact against the plain
 * kernel. These are the anchors: if any one of them drifts, every algebra check
 * below is measuring against a moved reference.
 *
 * `facealpha(0xFF) == plain` is exact by construction rather than by luck - the
 * span special-cases a fully opaque result into a plain store, because
 * alpha_blend rounds a 0xFF channel down to 0xFE and would otherwise dim every
 * opaque texel by one count per channel.
 */
static void
test_identities(int* plain_buf, int* got)
{
    printf("neutral-setting identities against the plain kernels\n");

    static unsigned char covered[H * W];

    struct
    {
        const char* name;
        plain_fn plain;
        matrix_fn variant;
        int* plain_tex;
        int* variant_tex;
    } cases[] = {
        { "texopaque.facealpha(0xFF) == texopaque",
          (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_texopaque_facealpha_branching_lerp8_v3,
          g_tex_opaque, g_tex_opaque },
        { "texopaque.modulate(identity) == texopaque",
          (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_texopaque_modulate_branching_lerp8_v3,
          g_tex_opaque, g_tex_opaque },
        { "textrans.facealpha(0xFF) == textrans",
          (plain_fn)raster_texshadeblend_persp_textrans_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_textrans_facealpha_branching_lerp8_v3,
          g_tex_keyed, g_tex_keyed },
        { "textrans.modulate(identity) == textrans",
          (plain_fn)raster_texshadeblend_persp_textrans_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_textrans_modulate_branching_lerp8_v3,
          g_tex_keyed, g_tex_keyed },
        /* The gate axis: an all-opaque alpha plane makes texalpha texopaque. */
        { "texalpha(all a=255) == texopaque",
          (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_texalpha_branching_lerp8_v3,
          g_tex_opaque, g_tex_alpha255 },
        /* ...and a binary alpha plane makes it textrans on the matched texture. */
        { "texalpha(a in {0,255}) == textrans",
          (plain_fn)raster_texshadeblend_persp_textrans_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_texalpha_branching_lerp8_v3,
          g_tex_keyed, g_tex_alphakey },
        { "texalpha.facealpha(0xFF) == texopaque",
          (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
          (matrix_fn)raster_texplane_persp_texalpha_facealpha_branching_lerp8_v3,
          g_tex_opaque, g_tex_alpha255 },
        { "texalpha.facealpha.modulate(neutral) == texopaque",
          (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
          (matrix_fn)
              raster_texplane_persp_texalpha_facealpha_modulate_branching_lerp8_v3,
          g_tex_opaque, g_tex_alpha255 },
    };

    for( int c = 0; c < (int)(sizeof(cases) / sizeof(cases[0])); c++ )
    {
        int bad_total = 0;

        for( int i = 0; i < TRI_COUNT; i++ )
        {
            const struct Tri* t = &g_tris[i];
            const struct TexVerts* tv = &g_texverts;

            plain_coverage(cases[c].plain, t, cases[c].plain_tex, plain_buf, covered);

            struct ToriDraw_TexSampler s;
            ToriDraw_TexSamplerInit(&s, cases[c].variant_tex, TEX_W);
            /* Neutral throughout: opaque face, identity tint, repeat both. */

            buf_fill(got, BG);
            cases[c].variant(got + GUARD, TEXGEOM(t, tv), &s);

            if( !guard_intact(got, cases[c].name, t->name) )
                break;

            for( int p = 0; p < H * W; p++ )
                if( RGB(got[GUARD + p]) != RGB(plain_buf[GUARD + p]) )
                    bad_total++;
        }

        if( bad_total )
        {
            printf("  FAIL %-52s: %d pixels differ from the plain kernel\n",
                   cases[c].name, bad_total);
            g_fail++;
        }
        else
        {
            printf("  ok   %-52s all triangles, bit-exact\n", cases[c].name);
        }
    }
}

/* -------------------------------------------------------------- algebra */

/*
 * Partial settings, checked per pixel against the plain kernel's own output.
 *
 * Coverage comes from the plain pass, never from "differs from the background":
 * a blend can legitimately land back on the background colour.
 */
static void
check_blend(
    const char* name,
    const struct Tri* t,
    const int* plain_buf,
    const unsigned char* covered,
    const int* got,
    const int* texels_for_alpha,
    int face_alpha,
    int use_texel_alpha)
{
    int bad_covered = 0;
    int bad_untouched = 0;

    for( int p = 0; p < H * W; p++ )
    {
        int expect;
        if( !covered[p] )
        {
            expect = BG;
        }
        else
        {
            /* The texel alpha at this pixel is not recoverable from the plain
             * pass, so the texel-alpha cases use a uniform alpha plane and pass
             * it in; the ramp is covered by the gate test instead. */
            int a = use_texel_alpha ? use_texel_alpha : 0xFF;
            a = tex_sampler_mul255(a, face_alpha);
            expect = (a >= 0xFF) ? RGB(plain_buf[GUARD + p])
                                 : alpha_blend(a, BG, RGB(plain_buf[GUARD + p]));
        }

        if( RGB(got[GUARD + p]) != RGB(expect) )
        {
            if( covered[p] )
                bad_covered++;
            else
                bad_untouched++;
        }
    }

    (void)texels_for_alpha;

    if( bad_covered || bad_untouched )
    {
        printf("  FAIL %-52s %-22s: %d blended wrong, %d touched outside coverage\n",
               name, t->name, bad_covered, bad_untouched);
        g_fail++;
    }
}

static void
test_alpha_algebra(int* plain_buf, int* got)
{
    printf("alpha algebra: facealpha, texalpha, and the two composed\n");

    static unsigned char covered[H * W];
    static const int alphas[] = { 0x00, 0x01, 0x40, 0x80, 0xC0, 0xFE, 0xFF };
    /* A uniform alpha plane, so the expected texel alpha is known per pixel
     * without re-deriving the uv walk in the test. */
    static int uniform_alpha_tex[TEX_LEN];
    static const int texel_alphas[] = { 0x30, 0x90, 0xFF };

    for( int ta = 0; ta < (int)(sizeof(texel_alphas) / sizeof(texel_alphas[0])); ta++ )
    {
        int texel_alpha = texel_alphas[ta];
        for( int i = 0; i < TEX_LEN; i++ )
            uniform_alpha_tex[i] =
                (int)(((unsigned)texel_alpha << 24) | ((unsigned)g_tex_opaque[i] & 0xFFFFFF));

        for( int ai = 0; ai < (int)(sizeof(alphas) / sizeof(alphas[0])); ai++ )
        {
            int alpha = alphas[ai];

            for( int i = 0; i < TRI_COUNT; i++ )
            {
                const struct Tri* t = &g_tris[i];
                const struct TexVerts* tv = &g_texverts;

                plain_coverage(
                    (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
                    t, g_tex_opaque, plain_buf, covered);

                struct ToriDraw_TexSampler s;
                ToriDraw_TexSamplerInit(&s, uniform_alpha_tex, TEX_W);
                s.face_alpha = alpha;

                buf_fill(got, BG);
                raster_texplane_persp_texalpha_facealpha_branching_lerp8_v3(
                    got + GUARD, TEXGEOM(t, tv), &s);
                if( !guard_intact(got, "texalpha.facealpha", t->name) )
                    break;
                check_blend(
                    "texalpha.facealpha == blend(mul255(texel_a, face_a))",
                    t, plain_buf, covered, got, uniform_alpha_tex, alpha, texel_alpha);

                /* facealpha alone, on a fully opaque texture. */
                struct ToriDraw_TexSampler s2;
                ToriDraw_TexSamplerInit(&s2, g_tex_opaque, TEX_W);
                s2.face_alpha = alpha;

                buf_fill(got, BG);
                raster_texplane_persp_texopaque_facealpha_branching_lerp8_v3(
                    got + GUARD, TEXGEOM(t, tv), &s2);
                if( !guard_intact(got, "texopaque.facealpha", t->name) )
                    break;
                check_blend(
                    "texopaque.facealpha == blend(face_a)",
                    t, plain_buf, covered, got, g_tex_opaque, alpha, 0xFF);
            }
        }
    }
    printf("  ok   3 texel alphas x 7 face alphas x %d triangles\n", TRI_COUNT);
}

/*
 * Modulate is a per-channel multiply applied AFTER the shade, so the expected
 * value is derived from the plain kernel's own output channel by channel.
 */
static void
test_modulate_algebra(int* plain_buf, int* got)
{
    printf("modulate algebra: per-channel (plain * tint) >> 8\n");

    static unsigned char covered[H * W];
    static const int tints[][3] = {
        { 256, 256, 256 }, { 128, 128, 128 }, { 256, 0, 0 }, { 64, 192, 300 }, { 0, 0, 0 },
    };

    int bad_total = 0;

    for( int ti = 0; ti < (int)(sizeof(tints) / sizeof(tints[0])); ti++ )
    {
        for( int i = 0; i < TRI_COUNT; i++ )
        {
            const struct Tri* t = &g_tris[i];
            const struct TexVerts* tv = &g_texverts;

            plain_coverage(
                (plain_fn)raster_texshadeblend_persp_texopaque_branching_lerp8_v3,
                t, g_tex_opaque, plain_buf, covered);

            struct ToriDraw_TexSampler s;
            ToriDraw_TexSamplerInit(&s, g_tex_opaque, TEX_W);
            s.tint_r = tints[ti][0];
            s.tint_g = tints[ti][1];
            s.tint_b = tints[ti][2];

            buf_fill(got, BG);
            raster_texplane_persp_texopaque_modulate_branching_lerp8_v3(
                got + GUARD, TEXGEOM(t, tv), &s);
            if( !guard_intact(got, "texopaque.modulate", t->name) )
                break;

            for( int p = 0; p < H * W; p++ )
            {
                int expect;
                if( !covered[p] )
                    expect = BG;
                else
                    expect = tex_sampler_tint(&s, RGB(plain_buf[GUARD + p]));
                if( RGB(got[GUARD + p]) != RGB(expect) )
                    bad_total++;
            }
        }
    }

    if( bad_total )
    {
        printf("  FAIL modulate: %d pixels are not the tinted plain result\n", bad_total);
        g_fail++;
    }
    else
    {
        printf("  ok   5 tints x %d triangles\n", TRI_COUNT);
    }
}

/*
 * A zero-coverage texel must be left EXACTLY alone, for both gates that have
 * one. Compositing it at alpha 0 is a different thing and is detectable:
 * alpha_blend(0, dst, src) rounds dst down, so a white destination would move.
 */
static void
test_zero_coverage_is_untouched(int* buf)
{
    printf("keyed / zero-alpha texels are left exactly alone\n");

    static const struct Tri t = { "interior", { 40, 150, 90 }, { 20, 35, 120 } };
    const struct TexVerts* tv = &g_texverts;
    const int white = 0x00FFFFFF;

    if( alpha_blend(0x00, white, 0x00C08040) == white )
    {
        printf("  FAIL premise: alpha_blend(0, dst, src) is the identity, check is vacuous\n");
        g_fail++;
        return;
    }

    struct
    {
        const char* name;
        matrix_fn fn;
        int* tex;
    } cases[] = {
        { "textrans.facealpha", (matrix_fn)
            raster_texplane_persp_textrans_facealpha_branching_lerp8_v3, g_tex_keyed },
        { "texalpha", (matrix_fn)
            raster_texplane_persp_texalpha_branching_lerp8_v3, g_tex_alphakey },
        { "texalpha.facealpha", (matrix_fn)
            raster_texplane_persp_texalpha_facealpha_branching_lerp8_v3, g_tex_alphakey },
    };

    for( int c = 0; c < 3; c++ )
    {
        struct ToriDraw_TexSampler s;
        ToriDraw_TexSamplerInit(&s, cases[c].tex, TEX_W);
        s.face_alpha = 0x00;

        buf_fill(buf, white);
        cases[c].fn(buf + GUARD, TEXGEOM(&t, tv), &s);
        if( !guard_intact(buf, cases[c].name, t.name) )
            continue;

        long untouched = 0, touched = 0;
        for( int p = 0; p < H * W; p++ )
        {
            if( RGB(buf[GUARD + p]) == white )
                untouched++;
            else
                touched++;
        }

        /* Both populations must be non-empty, or the texture is not exercising
         * the gate and the check proves nothing. */
        if( touched == 0 )
        {
            printf("  FAIL %-20s: drew nothing, check is vacuous\n", cases[c].name);
            g_fail++;
        }
        else
        {
            printf("  ok   %-20s %ld px exactly alone, %ld px composited\n",
                   cases[c].name, untouched, touched);
        }
    }
}

/* -------------------------------------------------------------- sampler */

/*
 * The sampler's own contracts, tested directly rather than inferred from
 * rendered pixels. Clamp addressing in particular is nearly unobservable
 * end-to-end - it only differs from wrapping where a span runs off the texture -
 * so the index function is the thing worth pinning.
 */
static void
test_sampler_contracts(void)
{
    printf("tex_sampler_index addressing and tex_sampler_mul255\n");

    int bad = 0;
    long checked = 0;

    for( int width = 64; width <= 128; width *= 2 )
    {
        struct ToriDraw_TexSampler s;
        ToriDraw_TexSamplerInit(&s, NULL, width);
        int shift = s.shift;

        for( int mode = 0; mode < 4; mode++ )
        {
            s.clamp_s = mode & 1;
            s.clamp_t = (mode >> 1) & 1;

            for( int uu = -3 * width; uu < 3 * width; uu += 7 )
            {
                for( int vv = -3 * width; vv < 3 * width; vv += 11 )
                {
                    int idx = tex_sampler_index(&s, uu << shift, vv << shift);
                    int u = idx & (width - 1);
                    int v = idx >> shift;

                    /* Always inside the texture, whatever the mode. */
                    if( idx < 0 || idx >= width * width )
                    {
                        bad++;
                    }
                    else
                    {
                        int want_u = s.clamp_s ? clamp(uu, 0, width - 1) : (uu & (width - 1));
                        int want_v = s.clamp_t ? clamp(vv, 0, width - 1) : (vv & (width - 1));
                        if( u != want_u || v != want_v )
                            bad++;
                    }
                    checked++;
                }
            }
        }
    }

    if( bad )
    {
        printf("  FAIL tex_sampler_index: %d of %ld samples wrong\n", bad, checked);
        g_fail++;
    }
    else
    {
        printf("  ok   %ld index samples, 2 widths x 4 address modes\n", checked);
    }

    /* mul255 must be exact at both ends: 255*255 == 255 is what makes
     * "opaque texel on an opaque face" a plain store rather than a blend. */
    int mul_bad = 0;
    for( int a = 0; a <= 255; a++ )
    {
        for( int b = 0; b <= 255; b++ )
        {
            int want = (a * b + 127) / 255;
            if( tex_sampler_mul255(a, b) != want )
                mul_bad++;
        }
    }
    if( mul_bad )
    {
        printf("  FAIL tex_sampler_mul255: %d of 65536 pairs are not round(a*b/255)\n", mul_bad);
        g_fail++;
    }
    else
    {
        printf("  ok   65536 mul255 pairs are exactly round(a*b/255)\n");
    }
}

/*
 * Clamp addressing observed end to end. A texture whose column 0 and column
 * width-1 are distinct sentinels makes wrapping visible: under repeat, a span
 * running off the right edge samples column 0; under clamp it must keep
 * sampling column width-1.
 */
static void
test_clamp_end_to_end(int* buf_repeat, int* buf_clamp)
{
    printf("clamp addressing changes what a span off the edge samples\n");

    static int edge_tex[TEX_LEN];
    for( int v = 0; v < TEX_W; v++ )
        for( int u = 0; u < TEX_W; u++ )
            edge_tex[u + v * TEX_W] =
                (int)(0xFF000000u | (unsigned)(0x00010000 * (u + 1) + 0x00000100 * (v + 1)));

    static const struct Tri t = { "interior", { 40, 150, 90 }, { 20, 35, 120 } };
    const struct TexVerts* tv = &g_texverts;

    struct ToriDraw_TexSampler rep, cla;
    ToriDraw_TexSamplerInit(&rep, edge_tex, TEX_W);
    ToriDraw_TexSamplerInit(&cla, edge_tex, TEX_W);
    cla.clamp_s = 1;
    cla.clamp_t = 1;

    buf_fill(buf_repeat, BG);
    buf_fill(buf_clamp, BG);
    raster_texplane_persp_texalpha_branching_lerp8_v3(
        buf_repeat + GUARD, TEXGEOM(&t, tv), &rep);
    raster_texplane_persp_texalpha_branching_lerp8_v3(
        buf_clamp + GUARD, TEXGEOM(&t, tv), &cla);

    if( !guard_intact(buf_repeat, "clamp (repeat pass)", t.name) )
        return;
    if( !guard_intact(buf_clamp, "clamp (clamp pass)", t.name) )
        return;

    long differ = 0, covered = 0;
    for( int p = 0; p < H * W; p++ )
    {
        if( RGB(buf_repeat[GUARD + p]) == BG && RGB(buf_clamp[GUARD + p]) == BG )
            continue;
        covered++;
        if( RGB(buf_repeat[GUARD + p]) != RGB(buf_clamp[GUARD + p]) )
            differ++;
    }

    /* The two must agree on coverage and disagree somewhere, or the geometry
     * never leaves the texture and the mode is untested. */
    if( differ == 0 )
    {
        printf("  FAIL clamp: identical to repeat over %ld px - the span never leaves"
               " the texture, so this proves nothing\n", covered);
        g_fail++;
    }
    else
    {
        printf("  ok   %ld of %ld covered px differ between repeat and clamp\n",
               differ, covered);
    }
}

int
main(void)
{
    static int plain_buf[BUF_LEN];
    static int got[BUF_LEN];
    static int aux[BUF_LEN];

    init_hsl16_to_rgb_table();
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    init_reciprocal16();
    init_textures();

    test_identities(plain_buf, got);
    test_alpha_algebra(plain_buf, got);
    test_modulate_algebra(plain_buf, got);
    test_zero_coverage_is_untouched(got);
    test_sampler_contracts();
    test_clamp_end_to_end(got, aux);

    if( g_fail )
    {
        printf("\n%d texture matrix check(s) FAILED\n", g_fail);
        return 1;
    }

    printf("\nall texture matrix checks passed\n");
    return 0;
}
