/*
 * Unit test for the per-texel-alpha texture kernels.
 *
 * ToriDraw has three texture span kernels, and they differ only in what they do
 * with a texel: opaque writes it, transparent writes it unless it is pure black,
 * and this one — `raster_linear_alpha_blend_lerp8_v3` — composites it over the
 * framebuffer by the coverage in its own alpha byte. The third exists so an
 * imported material with a continuous alpha ramp draws as authored instead of
 * being thresholded into holes.
 *
 * That kernel is reached only by a texture whose `alpha_blended` flag is set,
 * which today only `rs2012_material_bake --alpha-textures` produces. This test
 * drives it directly so its arithmetic can be verified without a bake, a pack
 * and a client run — that loop is minutes long and answers a different question
 * (does the flag reach the raster) than this one (does the blend compute the
 * right pixel).
 *
 * What is asserted, against an independent per-channel reference rather than
 * against `alpha_blend` itself:
 *
 *   - alpha 0x00 leaves the destination byte-identical (skip, not blend-by-zero)
 *   - alpha 0xFF replaces it with the shaded texel (no blend rounding at all)
 *   - partial alpha matches ((dst*(255-a))>>8) + ((src*a)>>8) per channel, with
 *     no bleed between red, green and blue. The packed multiply carries red and
 *     blue in one word, so channel bleed is the failure this kernel is actually
 *     exposed to.
 *   - each pixel uses its own texel's alpha, not the block's first
 *   - the shade multiplier is applied to the texel before compositing
 *   - u masks and v wraps exactly as the opaque and transparent twins do
 *   - the per-pixel twin `tex_span_alpha_exact_block` and the full scanline
 *     agree with the same reference
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/alpha_blend 3rd/toridraw/toridraw_texture_alpha_blend_test.c -lm
 *   /tmp/alpha_blend
 *
 * With the overflow check the live client uses (clang or a gcc with libubsan;
 * the repository's pinned MinGW has no ubsan runtime and will fail to link):
 *   cc -std=c11 -O1 -g -Wall -Wextra -I3rd/toridraw \
 *      -fsanitize=signed-integer-overflow -fno-sanitize-recover=all \
 *      -o /tmp/alpha_blend_ubsan \
 *      3rd/toridraw/toridraw_texture_alpha_blend_test.c -lm
 *   /tmp/alpha_blend_ubsan
 *
 * Negative controls run against this file (each mutation was applied to the
 * engine, the test run, and the mutation reverted):
 *
 *   | mutation                                   | result |
 *   |--------------------------------------------|--------|
 *   | alpha hoisted out of the 8-pixel loop      | FAILED |
 *   | alpha 0 blended instead of skipped         | FAILED |
 *   | shade applied after the blend, not before  | FAILED |
 *   | v sampled one row off (wrap mask dropped)  | FAILED |
 *   | alpha_blend reverted to signed arithmetic  | passed |
 *
 * That last row is the honest limit of a value-level test: on x86 the wrapped
 * signed product has the same bit pattern, and the bits the arithmetic shift
 * would differ in are masked off before they reach a channel. The overflow is
 * undefined behaviour rather than a wrong pixel here, so only the UBSan build
 * above catches it — which is how it was caught the first time
 * (docs/qbd_toridraw_streaks_debug.md, α). The `faint over ...` cases below
 * exist to drive that arithmetic for it.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graphics/raster/texture/span/tex.span_uv.h"

#define TEX_W 128
#define TEX_SHIFT 7
#define TEX_LEN (TEX_W * TEX_W)
#define U_MASK (TEX_W - 1)
#define V_MASK 0x3F80

/* shade_blend(base, 256) == base, so a pixel decodes back to its texel. */
#define SHADE_IDENTITY 256

#define SCREEN_W 512

static uint32_t g_texels[TEX_LEN];
static int g_fail = 0;
static int g_checks = 0;

/*
 * The texture encodes its own coordinates: green is v, blue is u, red is a
 * constant high value so the red channel is loud enough for a bleed into it (or
 * out of it) to show. Alpha ramps with u so one 8-pixel block can cover empty,
 * partial and full coverage at once, with 0x00 and 0xFF landing on exact
 * columns rather than being approached.
 */
static int
texel_alpha_for_u(int u)
{
    if( u == 0 )
        return 0x00;
    if( u == 1 )
        return 0xFF;
    /* 2..127 -> 2..254, so every other case is a genuine partial. */
    return u * 2;
}

static void
texels_init(void)
{
    for( int v = 0; v < TEX_W; v++ )
    {
        for( int u = 0; u < TEX_W; u++ )
        {
            uint32_t const rgb = 0x00FF0000u | ((uint32_t)v << 8) | (uint32_t)u;
            g_texels[u + v * TEX_W] = ((uint32_t)texel_alpha_for_u(u) << 24) | rgb;
        }
    }
}

/* --- independent reference ------------------------------------------------ */

/*
 * Per-channel, written out longhand. `alpha_blend` does the same arithmetic
 * with red and blue packed into one multiply; comparing against a second copy
 * of that trick would only prove it equals itself, so this unpacks first. The
 * >>8 (rather than /255) is the kernel's definition of the weight, not an
 * approximation this reference is entitled to fix.
 */
static uint32_t
ref_blend_channels(int alpha, uint32_t dst, uint32_t src)
{
    int const a_inv = 0xFF - alpha;
    uint32_t out = 0;

    for( int shift = 0; shift <= 16; shift += 8 )
    {
        uint32_t const d = (dst >> shift) & 0xFFu;
        uint32_t const s = (src >> shift) & 0xFFu;
        uint32_t const c = (((d * (uint32_t)a_inv) >> 8) + ((s * (uint32_t)alpha) >> 8)) & 0xFFu;
        out |= c << shift;
    }
    return out;
}

/* shade_blend, unpacked the same way. */
static uint32_t
ref_shade(uint32_t rgb, int shade)
{
    uint32_t out = 0;

    for( int shift = 0; shift <= 16; shift += 8 )
    {
        uint32_t const c = (((rgb >> shift) & 0xFFu) * (uint32_t)shade) >> 8;
        out |= (c & 0xFFu) << shift;
    }
    return out;
}

/* Per-channel tint, unpacked longhand like the blend above. `tint` is three
 * 0..256 channels, so 256 is the identity. */
static uint32_t
ref_tint(uint32_t rgb, const struct TexSpanTint* tint)
{
    uint32_t out = 0;

    for( int i = 0; i < 3; i++ )
    {
        int const shift = 16 - 8 * i;
        uint32_t c = (((rgb >> shift) & 0xFFu) * (uint32_t)tint->channel[i]) >> 8;
        if( c > 0xFFu )
            c = 0xFFu;
        out |= c << shift;
    }
    return out;
}

/* What one pixel must become. `dst` is what was already in the framebuffer;
 * `tint` is NULL for the plain alpha kernel. */
static uint32_t
ref_pixel_tinted(uint32_t dst, uint32_t texel, int shade, const struct TexSpanTint* tint)
{
    int const alpha = (int)(texel >> 24);
    uint32_t lit = ref_shade(texel & 0x00FFFFFFu, shade);

    if( tint )
        lit = ref_tint(lit, tint);
    if( alpha == 0 )
        return dst;
    if( alpha == 0xFF )
        return lit;
    return ref_blend_channels(alpha, dst, lit);
}

static uint32_t
ref_pixel(uint32_t dst, uint32_t texel, int shade)
{
    return ref_pixel_tinted(dst, texel, shade, NULL);
}

/* --- harness -------------------------------------------------------------- */

static void
check_pixel(
    const char* case_name,
    int index,
    uint32_t got,
    uint32_t want,
    uint32_t dst,
    uint32_t texel,
    int shade)
{
    g_checks++;
    if( got == want )
        return;
    g_fail = 1;
    printf(
        "  FAIL %-26s i=%3d  dst=%06X texel=%08X shade=%3d  got=%06X want=%06X\n",
        case_name,
        index,
        dst & 0xFFFFFFu,
        texel,
        shade,
        got & 0xFFFFFFu,
        want & 0xFFFFFFu);
}

/*
 * Drive one 8-pixel kernel call and check every pixel against the reference.
 * u_scan/v_scan are in the kernel's units: u pre-shifted by texture_shift, v
 * pre-shifted and masked by v_mask (so it indexes a row directly).
 */
static void
run_block(
    const char* case_name,
    uint32_t fill,
    int u0,
    int v0,
    int du,
    int dv,
    int shade)
{
    uint32_t frame[8];
    uint32_t before[8];
    int u_scan = u0 << TEX_SHIFT;
    int v_scan = (v0 & (TEX_W - 1)) << TEX_SHIFT;
    int const step_u = du << TEX_SHIFT;
    int const step_v = dv << TEX_SHIFT;

    for( int i = 0; i < 8; i++ )
        frame[i] = before[i] = fill + (uint32_t)i * 0x000101u;

    raster_linear_alpha_blend_lerp8_v3(
        frame, g_texels, u_scan, v_scan, step_u, step_v, TEX_SHIFT, U_MASK, V_MASK, shade);

    for( int i = 0; i < 8; i++ )
    {
        int const u = (u_scan >> TEX_SHIFT) & U_MASK;
        int const v = (v_scan & V_MASK) >> TEX_SHIFT;
        uint32_t const texel = g_texels[u + v * TEX_W];

        check_pixel(
            case_name, i, frame[i], ref_pixel(before[i], texel, shade), before[i], texel, shade);

        u_scan += step_u;
        v_scan += step_v;
    }
}

/* --- cases ---------------------------------------------------------------- */

static void
case_fully_opaque_texels(void)
{
    /* Column 1 is alpha 0xFF for every row: the destination must be gone. */
    uint32_t frame[8];
    int const v0 = 9;

    for( int i = 0; i < 8; i++ )
        frame[i] = 0x00123456u;

    raster_linear_alpha_blend_lerp8_v3(
        frame,
        g_texels,
        1 << TEX_SHIFT,
        v0 << TEX_SHIFT,
        0,
        0,
        TEX_SHIFT,
        U_MASK,
        V_MASK,
        SHADE_IDENTITY);

    for( int i = 0; i < 8; i++ )
    {
        uint32_t const texel = g_texels[1 + v0 * TEX_W];
        check_pixel(
            "alpha=0xFF replaces", i, frame[i], texel & 0x00FFFFFFu, 0x00123456u, texel,
            SHADE_IDENTITY);
    }
}

static void
case_fully_transparent_texels(void)
{
    /* Column 0 is alpha 0: every destination byte must survive untouched. A
     * blend-by-zero would round-trip most values and quietly damage the rest. */
    uint32_t frame[8];
    uint32_t const before[8] = { 0x00FFFFFFu, 0x00000001u, 0x00808080u, 0x000000FFu,
                                 0x00FF0000u, 0x0000FF00u, 0x00010101u, 0x007F7F7Fu };
    int const v0 = 3;

    memcpy(frame, before, sizeof(frame));

    raster_linear_alpha_blend_lerp8_v3(
        frame,
        g_texels,
        0,
        v0 << TEX_SHIFT,
        0,
        0,
        TEX_SHIFT,
        U_MASK,
        V_MASK,
        SHADE_IDENTITY);

    for( int i = 0; i < 8; i++ )
        check_pixel(
            "alpha=0x00 skips", i, frame[i], before[i], before[i], g_texels[v0 * TEX_W],
            SHADE_IDENTITY);
}

static void
case_partial_over_saturated_red(void)
{
    /*
     * The α regression's shape: a high red destination and a high alpha. The
     * widest term in the packed blend is 0xFF00FF * 0xFF = 4,261,543,425, which
     * fits uint32 and overflows int. A signed regression here wraps negative
     * and the pixel comes back garbage rather than merely rounded.
     */
    run_block("partial over 0xFF0000", 0x00FF0000u, 87, 40, 0, 0, SHADE_IDENTITY);
    run_block("partial over 0xFFFFFF", 0x00FFFFFFu, 87, 41, 0, 0, SHADE_IDENTITY);
    run_block("partial over 0x000000", 0x00000000u, 87, 42, 0, 0, SHADE_IDENTITY);

    /*
     * The widest term is the *destination's* red-blue pair times (255 - alpha),
     * so the overflow needs a bright destination and a LOW texel alpha — a faint
     * texel over a bright wall, not a strong one. u=2 is alpha 0x04, giving
     * 0xFF00FF * 251 = 4,194,695,685; u=40 is alpha 0x50 over the doc's
     * `15466510 * 175`. Both exceed INT_MAX and are the cases a signed
     * regression fails on.
     */
    run_block("faint over 0xFFFFFF", 0x00FFFFFFu, 2, 43, 0, 0, SHADE_IDENTITY);
    run_block("faint over 0xFF00FF", 0x00FF00FFu, 2, 44, 0, 0, SHADE_IDENTITY);
    run_block("alpha 0x50 over 0xEC00CE", 0x00EC00CEu, 40, 45, 0, 0, SHADE_IDENTITY);
}

static void
case_per_pixel_alpha(void)
{
    /*
     * Stepping u by one walks the alpha ramp across the block, starting on the
     * two exact columns: pixel 0 is alpha 0x00, pixel 1 is 0xFF, the rest are
     * partials. A kernel that hoisted one alpha out of the loop passes every
     * case above and fails this one.
     */
    run_block("per-pixel alpha ramp", 0x00204060u, 0, 17, 1, 0, SHADE_IDENTITY);
    run_block("per-pixel alpha ramp hi", 0x00E0D0C0u, 120, 18, 1, 0, SHADE_IDENTITY);
}

static void
case_shade_applied_before_blend(void)
{
    /* Half shade must darken the texel and then composite the darkened colour;
     * shading after the blend would also darken the destination's share. */
    run_block("shade 128", 0x0000FF00u, 60, 21, 1, 0, 128);
    run_block("shade 64", 0x0000FF00u, 60, 22, 1, 0, 64);
    run_block("shade 384 (over-bright)", 0x00102030u, 60, 23, 1, 0, 384);
}

static void
case_uv_walk_and_wrap(void)
{
    /* u wraps through u_mask and v through v_mask; both are the twins' rules,
     * and run_block recomputes the expected texel with the same masking, so a
     * kernel that dropped either lands on a different texel and fails. */
    run_block("u wraps at 128", 0x00304050u, 124, 5, 1, 0, SHADE_IDENTITY);
    run_block("v wraps at 128", 0x00304050u, 30, 125, 0, 1, SHADE_IDENTITY);
    run_block("u and v step", 0x00304050u, 10, 60, 3, 5, SHADE_IDENTITY);
    run_block("v steps backwards", 0x00304050u, 40, 4, 0, -1, SHADE_IDENTITY);
    run_block("diagonal wrap", 0x00304050u, 126, 126, 1, 1, SHADE_IDENTITY);
}

/* --- the modulate kernel -------------------------------------------------- */

/*
 * Same walk as run_block, through the modulated kernel. The mask this stands in
 * for is greyscale, so the tint is the only thing that can put colour on the
 * surface - a kernel that dropped it would still pass every case above.
 */
static void
run_block_modulated(
    const char* case_name,
    uint32_t fill,
    int u0,
    int v0,
    int du,
    int dv,
    int shade,
    int tint_rgb)
{
    uint32_t frame[8];
    uint32_t before[8];
    struct TexSpanTint tint;
    int u_scan = u0 << TEX_SHIFT;
    int v_scan = (v0 & (TEX_W - 1)) << TEX_SHIFT;
    int const step_u = du << TEX_SHIFT;
    int const step_v = dv << TEX_SHIFT;

    tex_span_tint_pack(tint_rgb, &tint);

    for( int i = 0; i < 8; i++ )
        frame[i] = before[i] = fill + (uint32_t)i * 0x000101u;

    raster_linear_alpha_modulate_lerp8_v3(
        frame, g_texels, u_scan, v_scan, step_u, step_v, TEX_SHIFT, U_MASK, V_MASK,
        shade, &tint);

    for( int i = 0; i < 8; i++ )
    {
        int const u = (u_scan >> TEX_SHIFT) & U_MASK;
        int const v = (v_scan & V_MASK) >> TEX_SHIFT;
        uint32_t const texel = g_texels[u + v * TEX_W];

        check_pixel(
            case_name, i, frame[i], ref_pixel_tinted(before[i], texel, shade, &tint),
            before[i], texel, shade);

        u_scan += step_u;
        v_scan += step_v;
    }
}

static void
case_modulate(void)
{
    /* A white tint must be the exact identity. tex_span_tint_pack maps 255 to
     * 256, not 255, precisely so this holds: a >>8 of a 255 scale would darken
     * every channel by one part in 256 and the modulated path would not be
     * substitutable for the plain one. */
    {
        uint32_t plain[8];
        uint32_t modulated[8];
        struct TexSpanTint tint;
        int const u0 = 40, v0 = 11;

        tex_span_tint_pack(0x00FFFFFF, &tint);
        for( int i = 0; i < 8; i++ )
            plain[i] = modulated[i] = 0x00304050u + (uint32_t)i;

        raster_linear_alpha_blend_lerp8_v3(
            plain, g_texels, u0 << TEX_SHIFT, v0 << TEX_SHIFT, 1 << TEX_SHIFT, 0,
            TEX_SHIFT, U_MASK, V_MASK, SHADE_IDENTITY);
        raster_linear_alpha_modulate_lerp8_v3(
            modulated, g_texels, u0 << TEX_SHIFT, v0 << TEX_SHIFT, 1 << TEX_SHIFT, 0,
            TEX_SHIFT, U_MASK, V_MASK, SHADE_IDENTITY, &tint);

        for( int i = 0; i < 8; i++ )
            check_pixel(
                "white tint == identity", i, modulated[i], plain[i], 0, g_texels[u0 + i],
                SHADE_IDENTITY);
    }

    /* A tint with a dead channel must remove that channel entirely: the texture
     * is grey-ish, so this is what proves the surface colour comes from the
     * face and not from the mask. */
    run_block_modulated("tint pure red", 0x00202020u, 40, 12, 1, 0, SHADE_IDENTITY, 0x00FF0000);
    run_block_modulated("tint pure green", 0x00202020u, 40, 13, 1, 0, SHADE_IDENTITY, 0x0000FF00);
    run_block_modulated("tint pure blue", 0x00202020u, 40, 14, 1, 0, SHADE_IDENTITY, 0x000000FF);
    run_block_modulated("tint black", 0x00806040u, 40, 15, 1, 0, SHADE_IDENTITY, 0x00000000);

    /* Realistic cases: a dark purple fringe and a red patch over a lit surface,
     * at partial shade, walking the alpha ramp and wrapping v. */
    run_block_modulated("tint purple, shade 128", 0x00303840u, 0, 16, 1, 0, 128, 0x00604878);
    run_block_modulated("tint red, v wraps", 0x00181818u, 30, 126, 0, 1, 200, 0x00A03028);
    run_block_modulated("tint olive, diagonal", 0x00404040u, 100, 100, 1, 1, 96, 0x00808040);

    /* Tint and shade must compose in the documented order - shade first, then
     * tint - and both must reach every pixel. */
    run_block_modulated("tint+shade over bright", 0x00FFFFFFu, 2, 17, 0, 0, 64, 0x0040C080);
}

/* --- the per-pixel twin and the whole scanline ---------------------------- */

/*
 * tex_span_alpha_exact_block draws the partial tail and any block whose uv
 * gradient the linear fit cannot represent. It must composite identically to
 * the 8-pixel kernel — the pixels it draws sit next to the ones the kernel drew.
 */
static void
case_exact_block_matches_reference(void)
{
    int frame[8];
    int before[8];
    /* w = cw >> shift; keep it constant so the sampled texel is predictable. */
    int const cw = 4 << TEX_SHIFT;
    int const w = cw >> TEX_SHIFT;
    int const au = 33 * w;
    int const bv = 71 * w;
    int const step_au = 1 * w;
    int const step_bv = 2 * w;

    for( int i = 0; i < 8; i++ )
        frame[i] = before[i] = (int)(0x00405060u + (uint32_t)i);

    tex_span_alpha_exact_block(
        frame, 0, (const int*)g_texels, 8, au, bv, cw, step_au, step_bv, 0,
        SHADE_IDENTITY, TEX_W, TEX_SHIFT, NULL);

    for( int i = 0; i < 8; i++ )
    {
        int const u = 33 + i;
        int const v = (71 + 2 * i) & (TEX_W - 1);
        uint32_t const texel = g_texels[u + v * TEX_W];

        check_pixel(
            "exact block",
            i,
            (uint32_t)frame[i],
            ref_pixel((uint32_t)before[i], texel, SHADE_IDENTITY),
            (uint32_t)before[i],
            texel,
            SHADE_IDENTITY);
    }
}

/*
 * The whole scanline: block walk, per-pixel fallback and tail together, against
 * a reference that composites every pixel with an exact divide. This is the
 * mapping check the opaque and transparent kernels already have in
 * toridraw_texture_span_uv_test.c, extended to say the alpha path samples the
 * same texel *and* composites it correctly.
 */
/* Where the exact per-pixel mapping lands, and whether the pixel is drawn. */
struct RefSample
{
    int drawn;
    int u;
    int v;
};

static void
reference_alpha_span(
    uint32_t* dst,
    struct RefSample* sample,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= SCREEN_W )
        x1 = SCREEN_W - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (SCREEN_W >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    for( int x = x0; x < x1; x++ )
    {
        int const w = cw >> TEX_SHIFT;
        if( w != 0 )
        {
            int u = au / w;
            u = u < 0 ? 0 : (u > TEX_W - 1 ? TEX_W - 1 : u);
            int const v = (bv / w) & (TEX_W - 1);
            dst[x] = ref_pixel(dst[x], g_texels[u + v * TEX_W], shade);
            sample[x].drawn = 1;
            sample[x].u = u;
            sample[x].v = v;
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
    }
}

/* The 8-pixel linear fit may land a texel or two from the exact per-pixel
 * divide. That is interpolation error, measured by toridraw_texture_span_uv_test.c;
 * accepting a small neighbourhood here keeps this test about compositing. */
#define SCANLINE_TEXEL_TOLERANCE 2

/* True when `got` is the correct composite of *some* texel within the tolerance
 * of where the exact mapping landed. A wrong blend cannot pass this: no
 * neighbouring texel reproduces a channel-bled or unweighted result. */
static int
composite_of_nearby_texel(uint32_t got, uint32_t dst, int u_ref, int v_ref, int shade)
{
    for( int dv = -SCANLINE_TEXEL_TOLERANCE; dv <= SCANLINE_TEXEL_TOLERANCE; dv++ )
    {
        for( int du = -SCANLINE_TEXEL_TOLERANCE; du <= SCANLINE_TEXEL_TOLERANCE; du++ )
        {
            int const u = (u_ref + du) & (TEX_W - 1);
            int const v = (v_ref + dv) & (TEX_W - 1);
            if( got == ref_pixel(dst, g_texels[u + v * TEX_W], shade) )
                return 1;
        }
    }
    return 0;
}

/*
 * A benign span only: the linear fit is an approximation, so on a near-eye span
 * the kernel may legitimately sample a neighbouring texel and produce a
 * different — not wrong — colour. Sampling accuracy under those conditions is
 * what toridraw_texture_span_uv_test.c measures; this case is here to prove the
 * alpha wrapper composites what it samples across a real span, including the
 * unaligned tail.
 */
static void
case_full_scanline_benign(void)
{
    static uint32_t got[SCREEN_W];
    static uint32_t want[SCREEN_W];
    static uint32_t initial[SCREEN_W];
    static struct RefSample sample[SCREEN_W];
    int const x0_ish16 = 0;
    int const x1_ish16 = (SCREEN_W - 3) << 16; /* not a multiple of 8: exercise the tail */
    int const au = 1 << 20;
    int const bv = 1 << 21;
    int const cw = 1 << 22;
    int const step_au = 64;
    int const step_bv = 512;
    int const step_cw = 256;
    int mismatches = 0;
    int approximated = 0;

    for( int i = 0; i < SCREEN_W; i++ )
    {
        got[i] = want[i] = initial[i] = 0x00203040u + (uint32_t)(i & 0x1F);
        sample[i].drawn = 0;
        sample[i].u = 0;
        sample[i].v = 0;
    }

    reference_alpha_span(
        want, sample, x0_ish16, x1_ish16, au, bv, cw, step_au, step_bv, step_cw,
        SHADE_IDENTITY);

    draw_texture_scanline_alpha_blend_branching_lerp8_v3_ordered(
        (int*)got,
        SCREEN_W,
        x0_ish16,
        x1_ish16,
        0,
        au,
        bv,
        cw,
        step_au,
        step_bv,
        step_cw,
        SHADE_IDENTITY << 8,
        0,
        (int*)g_texels,
        TEX_W,
        NULL);

    for( int i = 0; i < SCREEN_W; i++ )
    {
        g_checks++;
        if( got[i] == want[i] )
            continue;

        /* An undrawn pixel must be untouched: the alpha kernel never writes
         * where w == 0, exactly like its twins. */
        if( !sample[i].drawn )
        {
            mismatches++;
            if( mismatches <= 4 )
                printf(
                    "  FAIL full scanline (wrote undrawn pixel) x=%3d got=%06X want=%06X\n",
                    i,
                    got[i] & 0xFFFFFFu,
                    want[i] & 0xFFFFFFu);
            continue;
        }

        if( composite_of_nearby_texel(
                got[i], initial[i], sample[i].u, sample[i].v, SHADE_IDENTITY) )
        {
            approximated++;
            continue;
        }

        mismatches++;
        if( mismatches <= 4 )
            printf(
                "  FAIL full scanline           x=%3d  ref texel=(%d,%d)  got=%06X want=%06X\n",
                i,
                sample[i].u,
                sample[i].v,
                got[i] & 0xFFFFFFu,
                want[i] & 0xFFFFFFu);
    }

    printf(
        "  full scanline: %d/%d pixels blended a texel within +/-%d of exact\n",
        approximated,
        SCREEN_W,
        SCANLINE_TEXEL_TOLERANCE);

    if( mismatches )
    {
        g_fail = 1;
        printf("  full scanline: %d/%d pixels not a correct composite\n", mismatches, SCREEN_W);
    }
}

int
main(void)
{
    texels_init();

    printf("texture alpha-blend kernel vs per-channel reference\n");

    case_fully_transparent_texels();
    case_fully_opaque_texels();
    case_partial_over_saturated_red();
    case_per_pixel_alpha();
    case_shade_applied_before_blend();
    case_uv_walk_and_wrap();
    case_modulate();
    case_exact_block_matches_reference();
    case_full_scanline_benign();

    printf(
        "\n%d pixel checks: %s\n",
        g_checks,
        g_fail ? "texture alpha blend: FAILED" : "texture alpha blend: all pixels exact");
    return g_fail;
}
