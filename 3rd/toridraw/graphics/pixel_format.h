#ifndef TORIDRAW_PIXEL_FORMAT_H
#define TORIDRAW_PIXEL_FORMAT_H

/*
 * THE ONE OWNER OF THE PIXEL FORMAT.
 *
 * Everything that knows how a colour is laid out in a framebuffer word lives
 * here and nowhere else: the storage type, the pack from the ARGB8888 that
 * assets arrive in, and the three operations a span actually performs on a
 * pixel -- blend against a destination, scale by a shade, and test for the
 * colour key.
 *
 * ## Why this can be cheap
 *
 * No hot loop in ToriDraw interpolates in pixel space. The solid families walk
 * a packed HSL16 word and index a palette; the texture families sample a texel
 * and shade it. So the format is touched by exactly three operation classes --
 * pack, blend, test -- and pack is not one a span performs: it runs when the
 * palette is built, when a texture is registered, and when a sprite is
 * decoded. Conversion sits at the asset boundary, never in the loop.
 *
 * ## Naming
 *
 * Anything that writes a specific pixel format carries that format in its
 * name: `toripixel_rgb565_alpha_blend` blends RGB565 and nothing else. Every
 * implementation is defined on every build, so they can be tested against each
 * other in one translation unit, and a grep for a symbol answers exactly what
 * math runs at what pixel width.
 *
 * The format-NEUTRAL spellings -- `alpha_blend`, `shade_blend`,
 * `toripixel_pack_argb8888`, `toripixel_is_key`, `toripixel_texel_alpha` --
 * are aliases bound to exactly one implementation by the switch at the bottom.
 * Kernels spell only those. A name with no format token is a promise the code
 * works for all of them.
 *
 * ## The alpha lane
 *
 * ToriDraw's framebuffer alpha is UNDEFINED unless a target asks otherwise.
 * Nothing in the library reads it back, and the generalized blend would drift
 * an opaque lane downward anyway (0xFF blended with 0xFF truncates to 0xFE).
 * A platform whose compositor reads the lane defines
 * TORIDRAW_PIXEL_STORE_OPAQUE_FIXUP, which makes TORIPIXEL_OPAQUE_FIXUP(p) an
 * OR with the alpha mask; everywhere else it is the identity and costs
 * nothing. Texel alpha is a separate mechanism and is read through
 * toripixel_texel_alpha(), which is only meaningful where
 * TORIPIXEL_HAS_ALPHA_LANE says the format has somewhere to keep it.
 */

#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------- formats */
/*
 * Byte order is written high-to-low within the machine word, which is how the
 * packs below are spelled. A surface described by its BYTE order in memory is
 * the reverse of this on a little-endian target: an OpenGL GL_RGBA8 surface,
 * whose bytes run R,G,B,A, is TORIDRAW_PF_ABGR8888 here.
 */
#define TORIDRAW_PF_XRGB8888 1 /* .RGB -- the incumbent; top byte ignored     */
#define TORIDRAW_PF_ARGB8888 2 /* ARGB -- as XRGB, but the lane is real       */
#define TORIDRAW_PF_RGBA8888 3 /* RGBA -- alpha in the LOW byte               */
#define TORIDRAW_PF_ABGR8888 4 /* ABGR -- R and B swapped from ARGB           */
#define TORIDRAW_PF_BGRA8888 5 /* BGRA -- alpha in the LOW byte, R/B swapped  */
#define TORIDRAW_PF_RGB565 6   /* 16bpp, no alpha lane                        */
#define TORIDRAW_PF_ARGB1555 7 /* 16bpp, one alpha BIT                        */

/*
 * TORIDRAW_PIXEL16 was the historical 16-bit switch. It selected a uint16_t
 * framebuffer and a uint16_t palette but never said WHICH 16-bit format, so
 * the palette builder went on writing 24-bit packs into it and red was
 * truncated away; and it excluded the texture, depth and HD stages rather than
 * converting them. Both are fixed, and a format now has a name.
 *
 * It fails loudly rather than being ignored: silently resolving to XRGB8888
 * would hand an old 16-bit build a 32-bit framebuffer, which is the exact
 * class of quiet wrongness this series exists to remove.
 */
#ifdef TORIDRAW_PIXEL16
#error "TORIDRAW_PIXEL16 is retired -- build with -DTORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565"
#endif

#ifndef TORIDRAW_PIXEL_FORMAT
#define TORIDRAW_PIXEL_FORMAT TORIDRAW_PF_XRGB8888
#endif

/* Channel extraction from the ARGB8888 that assets arrive in. */
#define TORIPIXEL_ARGB_A(argb) (((uint32_t)(argb) >> 24) & 0xFFu)
#define TORIPIXEL_ARGB_R(argb) (((uint32_t)(argb) >> 16) & 0xFFu)
#define TORIPIXEL_ARGB_G(argb) (((uint32_t)(argb) >> 8) & 0xFFu)
#define TORIPIXEL_ARGB_B(argb) ((uint32_t)(argb) & 0xFFu)

/* ============================================================== XRGB8888 ==
 *
 * The incumbent format, and the one the hand-written assembly implements.
 * These three bodies are ToriDraw's originals, moved here unchanged -- the
 * blend's four-term sum and the shade's two-lane split are bit-for-bit what
 * graphics/alpha.h and graphics/shade.h have always computed, including the
 * blend's habit of leaving the top byte zero rather than carrying it.
 */

/*
 * The identity, and deliberately not a mask.
 *
 * `X` means the top byte is not INTERPRETED, not that it is cleared. Two
 * callers rely on the difference: the palette hands this a value that never
 * had an alpha byte, and the 2D layer hands it one carrying 0xFF because the
 * surface it composites onto wants an opaque lane. Masking would silently
 * take that away from the second to no benefit for the first.
 */
static inline int
toripixel_xrgb8888_pack_argb8888(uint32_t argb)
{
    return (int)argb;
}

/*
 * Red and blue are carried in one multiply: masking with 0xFF00FF leaves them
 * far enough apart that scaling by alpha cannot make one carry into the other,
 * so a single shift and mask recovers both. Green goes separately.
 *
 * The arithmetic must be UNSIGNED. The widest term is 0xFF00FF * 0xFF, which
 * is 4,261,543,425: that fits a uint32 with room to spare but overflows a
 * signed int, and the wrapped product shifts negative and yields a garbage
 * colour. It needs a high red channel and a high alpha to happen at all, which
 * is why it survived so long -- UBSan caught it as `15466510 * 175` on the
 * dark-red QBD. No widening to 64 bits is required, only the right signedness.
 */
static inline int
toripixel_xrgb8888_alpha_blend(int alpha, int base, int other)
{
    unsigned int const a = (unsigned int)alpha;
    unsigned int const a_inv = 0xFFu - a;
    unsigned int const b = (unsigned int)base;
    unsigned int const o = (unsigned int)other;

    return (int)(((((b & 0xFF00FFu) * a_inv) >> 8) & 0xFF00FFu) +
                 ((((o & 0xFF00FFu) * a) >> 8) & 0xFF00FFu) +
                 ((((o & 0xFF00u) * a) >> 8) & 0xFF00u) +
                 ((((b & 0xFF00u) * a_inv) >> 8) & 0xFF00u));
}

static inline int
toripixel_xrgb8888_shade_blend(int base, int shade)
{
    uint32_t rb = (uint32_t)base & 0x00ff00ff;
    uint32_t g = (uint32_t)base & 0x0000ff00;

    rb *= (uint32_t)shade;
    g *= (uint32_t)shade;

    rb &= 0xFF00FF00;
    g &= 0x00FF0000;

    return (int)((rb | g) >> 8);
}

static inline bool
toripixel_xrgb8888_is_key(int p)
{
    return ((uint32_t)p & 0x00FFFFFFu) == 0u;
}

static inline int
toripixel_xrgb8888_texel_alpha(int p)
{
    return (int)(((uint32_t)p >> 24) & 0xFFu);
}

/* ================================================= the other 32bpp packs ==
 *
 * One pack per byte order. Each is a permutation of the same four bytes, so
 * the compiler folds every one of these to two or three instructions.
 */

static inline uint32_t
toripixel_argb8888_pack_argb8888(uint32_t argb)
{
    return argb;
}

static inline uint32_t
toripixel_rgba8888_pack_argb8888(uint32_t argb)
{
    return (argb << 8) | TORIPIXEL_ARGB_A(argb);
}

static inline uint32_t
toripixel_abgr8888_pack_argb8888(uint32_t argb)
{
    return (TORIPIXEL_ARGB_A(argb) << 24) | (TORIPIXEL_ARGB_B(argb) << 16) |
           (TORIPIXEL_ARGB_G(argb) << 8) | TORIPIXEL_ARGB_R(argb);
}

static inline uint32_t
toripixel_bgra8888_pack_argb8888(uint32_t argb)
{
    return (TORIPIXEL_ARGB_B(argb) << 24) | (TORIPIXEL_ARGB_G(argb) << 16) |
           (TORIPIXEL_ARGB_R(argb) << 8) | TORIPIXEL_ARGB_A(argb);
}

/*
 * The generalized 32bpp blend and shade.
 *
 * The 0xFF00FF trick is not an RGB trick, it is an EVEN-BYTE-LANES trick:
 * bytes 0 and 2 are far enough apart to scale in one multiply, and bytes 1
 * and 3 become the same problem after a shift. So one body serves every byte
 * order, and channel order stops being a term in the arithmetic entirely.
 *
 * Unlike the XRGB twin this carries all four bytes rather than dropping the
 * top one -- these formats may have a real lane there, and a target that needs
 * it opaque says so with TORIPIXEL_OPAQUE_FIXUP at the store.
 */
static inline uint32_t
toripixel_lanes8_alpha_blend(int alpha, uint32_t base, uint32_t other)
{
    uint32_t const a = (uint32_t)alpha;
    uint32_t const a_inv = 0xFFu - a;

    uint32_t const even = ((((base & 0x00FF00FFu) * a_inv) >> 8) & 0x00FF00FFu) +
                          ((((other & 0x00FF00FFu) * a) >> 8) & 0x00FF00FFu);
    uint32_t const odd = ((((base >> 8) & 0x00FF00FFu) * a_inv) & 0xFF00FF00u) +
                         ((((other >> 8) & 0x00FF00FFu) * a) & 0xFF00FF00u);

    return even | odd;
}

static inline uint32_t
toripixel_lanes8_shade_blend(uint32_t base, int shade)
{
    uint32_t const s = (uint32_t)shade;
    uint32_t const even = ((base & 0x00FF00FFu) * s >> 8) & 0x00FF00FFu;
    uint32_t const odd = (((base >> 8) & 0x00FF00FFu) * s) & 0xFF00FF00u;

    return even | odd;
}

#define TORIPIXEL_DEFINE_LANES8_OPS(fmt, key_mask, alpha_shift)                                    \
    static inline uint32_t toripixel_##fmt##_alpha_blend(                                          \
        int alpha, uint32_t base, uint32_t other)                                                  \
    {                                                                                              \
        return toripixel_lanes8_alpha_blend(alpha, base, other);                                   \
    }                                                                                              \
    static inline uint32_t toripixel_##fmt##_shade_blend(uint32_t base, int shade)                 \
    {                                                                                              \
        return toripixel_lanes8_shade_blend(base, shade);                                          \
    }                                                                                              \
    static inline bool toripixel_##fmt##_is_key(uint32_t p)                                        \
    {                                                                                              \
        return (p & (key_mask)) == 0u;                                                             \
    }                                                                                              \
    static inline int toripixel_##fmt##_texel_alpha(uint32_t p)                                    \
    {                                                                                              \
        return (int)((p >> (alpha_shift)) & 0xFFu);                                                \
    }

TORIPIXEL_DEFINE_LANES8_OPS(argb8888, 0x00FFFFFFu, 24)
TORIPIXEL_DEFINE_LANES8_OPS(rgba8888, 0xFFFFFF00u, 0)
TORIPIXEL_DEFINE_LANES8_OPS(abgr8888, 0x00FFFFFFu, 24)
TORIPIXEL_DEFINE_LANES8_OPS(bgra8888, 0xFFFFFF00u, 0)

/* ================================================================ RGB565 ==
 *
 * The three channels are spread into one 32-bit word as G | R....B, which
 * leaves each of them six or more spare bits above it -- enough that all three
 * survive a multiply by a 5-bit weight without carrying into each other. So a
 * blend is ONE multiply per operand for the whole pixel rather than two, and
 * the alpha is quantized to 32 steps to keep it that way.
 *
 * There is no alpha lane. texel_alpha reports fully opaque; a kernel that
 * needs a real per-texel ramp reads the side plane the texture carries
 * instead, which is what TORIPIXEL_HAS_ALPHA_LANE == 0 tells it to do.
 */

#define TORIPIXEL_RGB565_SPREAD_MASK 0x07E0F81Fu

static inline uint16_t
toripixel_rgb565_pack_argb8888(uint32_t argb)
{
    return (uint16_t)(((TORIPIXEL_ARGB_R(argb) >> 3) << 11) |
                      ((TORIPIXEL_ARGB_G(argb) >> 2) << 5) | (TORIPIXEL_ARGB_B(argb) >> 3));
}

static inline uint16_t
toripixel_rgb565_alpha_blend(int alpha, uint16_t base, uint16_t other)
{
    uint32_t const a = (uint32_t)alpha >> 3;
    uint32_t const b = ((uint32_t)base | ((uint32_t)base << 16)) & TORIPIXEL_RGB565_SPREAD_MASK;
    uint32_t const o = ((uint32_t)other | ((uint32_t)other << 16)) & TORIPIXEL_RGB565_SPREAD_MASK;
    uint32_t const r = ((b * (32u - a) + o * a) >> 5) & TORIPIXEL_RGB565_SPREAD_MASK;

    return (uint16_t)(r | (r >> 16));
}

static inline uint16_t
toripixel_rgb565_shade_blend(uint16_t base, int shade)
{
    uint32_t const b = ((uint32_t)base | ((uint32_t)base << 16)) & TORIPIXEL_RGB565_SPREAD_MASK;
    uint32_t const r = ((b * (uint32_t)shade) >> 8) & TORIPIXEL_RGB565_SPREAD_MASK;

    return (uint16_t)(r | (r >> 16));
}

static inline bool
toripixel_rgb565_is_key(uint16_t p)
{
    return p == 0u;
}

static inline int
toripixel_rgb565_texel_alpha(uint16_t p)
{
    (void)p;
    return 0xFF;
}

/* ============================================================== ARGB1555 ==
 *
 * The same spread trick over a 5/5/5 layout, with the top bit carried through
 * untouched: it is one bit and cannot be interpolated, so the blend preserves
 * the destination's and the pack thresholds the source's at half.
 */

#define TORIPIXEL_ARGB1555_SPREAD_MASK 0x03E07C1Fu
#define TORIPIXEL_ARGB1555_ALPHA_BIT 0x8000u

static inline uint16_t
toripixel_argb1555_pack_argb8888(uint32_t argb)
{
    uint32_t const a = TORIPIXEL_ARGB_A(argb) >= 0x80u ? TORIPIXEL_ARGB1555_ALPHA_BIT : 0u;

    return (uint16_t)(a | ((TORIPIXEL_ARGB_R(argb) >> 3) << 10) |
                      ((TORIPIXEL_ARGB_G(argb) >> 3) << 5) | (TORIPIXEL_ARGB_B(argb) >> 3));
}

static inline uint16_t
toripixel_argb1555_alpha_blend(int alpha, uint16_t base, uint16_t other)
{
    uint32_t const a = (uint32_t)alpha >> 3;
    uint32_t const b = ((uint32_t)base | ((uint32_t)base << 16)) & TORIPIXEL_ARGB1555_SPREAD_MASK;
    uint32_t const o = ((uint32_t)other | ((uint32_t)other << 16)) & TORIPIXEL_ARGB1555_SPREAD_MASK;
    uint32_t const r = ((b * (32u - a) + o * a) >> 5) & TORIPIXEL_ARGB1555_SPREAD_MASK;

    return (uint16_t)((r | (r >> 16)) | ((uint32_t)base & TORIPIXEL_ARGB1555_ALPHA_BIT));
}

static inline uint16_t
toripixel_argb1555_shade_blend(uint16_t base, int shade)
{
    uint32_t const b = ((uint32_t)base | ((uint32_t)base << 16)) & TORIPIXEL_ARGB1555_SPREAD_MASK;
    uint32_t const r = ((b * (uint32_t)shade) >> 8) & TORIPIXEL_ARGB1555_SPREAD_MASK;

    return (uint16_t)((r | (r >> 16)) | ((uint32_t)base & TORIPIXEL_ARGB1555_ALPHA_BIT));
}

static inline bool
toripixel_argb1555_is_key(uint16_t p)
{
    return ((uint32_t)p & ~TORIPIXEL_ARGB1555_ALPHA_BIT) == 0u;
}

static inline int
toripixel_argb1555_texel_alpha(uint16_t p)
{
    return ((uint32_t)p & TORIPIXEL_ARGB1555_ALPHA_BIT) ? 0xFF : 0x00;
}


/* ============================================== back to ARGB8888 ==
 *
 * The inverse of pack, for the code that has to look at a finished pixel
 * rather than produce one: parity tests recovering a channel, a screenshot
 * writer, a colour picker. Not a hot path on any target -- nothing in the
 * raster stage reads a pixel back except to blend it, and blending stays in
 * the native format.
 *
 * The 16-bit unpacks REPLICATE the high bits into the low ones (0x1F -> 0xFF,
 * not 0xF8) so a fully saturated channel comes back fully saturated. They are
 * still lossy: five bits cannot carry eight, and a caller measuring precision
 * has to know that -- TORIPIXEL_LANES_8BIT is what says whether they do.
 */

static inline uint32_t
toripixel_xrgb8888_to_argb8888(int p)
{
    return (uint32_t)p & 0x00FFFFFFu;
}

static inline uint32_t
toripixel_argb8888_to_argb8888(uint32_t p)
{
    return p;
}

static inline uint32_t
toripixel_rgba8888_to_argb8888(uint32_t p)
{
    return (p >> 8) | (p << 24);
}

static inline uint32_t
toripixel_abgr8888_to_argb8888(uint32_t p)
{
    return (p & 0xFF00FF00u) | ((p & 0x000000FFu) << 16) | ((p >> 16) & 0x000000FFu);
}

static inline uint32_t
toripixel_bgra8888_to_argb8888(uint32_t p)
{
    return ((p & 0x000000FFu) << 24) | ((p >> 8) & 0x000000FFu) << 16 |
           (((p >> 16) & 0x000000FFu) << 8) | ((p >> 24) & 0x000000FFu);
}

/** 5/6/5 widened by bit replication, so 0x1F becomes 0xFF and not 0xF8. */
static inline uint32_t
toripixel_rgb565_to_argb8888(uint16_t p)
{
    uint32_t r5 = ((uint32_t)p >> 11) & 0x1Fu;
    uint32_t g6 = ((uint32_t)p >> 5) & 0x3Fu;
    uint32_t b5 = (uint32_t)p & 0x1Fu;
    uint32_t r8 = (r5 << 3) | (r5 >> 2);
    uint32_t g8 = (g6 << 2) | (g6 >> 4);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);

    return (r8 << 16) | (g8 << 8) | b8;
}

static inline uint32_t
toripixel_argb1555_to_argb8888(uint16_t p)
{
    uint32_t r5 = ((uint32_t)p >> 10) & 0x1Fu;
    uint32_t g5 = ((uint32_t)p >> 5) & 0x1Fu;
    uint32_t b5 = (uint32_t)p & 0x1Fu;
    uint32_t a8 = ((uint32_t)p & TORIPIXEL_ARGB1555_ALPHA_BIT) ? 0xFFu : 0x00u;

    return (a8 << 24) | ((((r5 << 3) | (r5 >> 2))) << 16) | ((((g5 << 3) | (g5 >> 2))) << 8) |
           ((b5 << 3) | (b5 >> 2));
}

/* ================================================ the texel shading space ==
 *
 * A TEXEL is not a pixel, and does not become one until it is stored.
 *
 * Texture data arrives from the cache as ARGB8888 and STAYS ARGB8888 in
 * memory, on every target. The whole texture composite -- the colour key, the
 * texel's own alpha, the shade, the tint -- then runs in 8-bit channels
 * exactly as it always has, and only the final store converts.
 *
 * That is a better trade than packing texels to the framebuffer format at
 * registration, which was the obvious move and the wrong one:
 *
 *   - it would shade and tint in 5-bit channels on a 16-bit target, which is
 *     where the precision is least affordable (tex_sampler_shade_tint exists
 *     precisely because rounding twice in 8 bits already streaks);
 *   - it would put the texel's alpha somewhere a 16-bit pixel has no room for,
 *     forcing a parallel alpha plane through every span signature;
 *   - and it would buy nothing, because a texel is sampled once per pixel
 *     DRAWN either way.
 *
 * Keeping the shading space at ARGB8888 costs one pack per drawn pixel on a
 * format that differs from it, and exactly nothing on the two that do not --
 * where toritexel_to_pixel is the identity by definition rather than by luck,
 * because there the shading space IS the framebuffer format.
 */

/** What a texture holds, and what the composite computes in: ARGB8888. */
typedef uint32_t toritexel_t;

#define TORITEXEL_COLOR_MASK 0x00FFFFFFu
#define toritexel_shade_blend toripixel_xrgb8888_shade_blend
#define toritexel_is_key toripixel_xrgb8888_is_key
#define toritexel_alpha toripixel_xrgb8888_texel_alpha

/* ========================================================= the selection ==
 *
 * One format is bound to the neutral spellings. Everything above stays defined
 * and testable; only this block decides what a kernel gets when it writes
 * `alpha_blend`.
 *
 * TORIPIXEL_IS_XRGB8888 is what the hand-written assembly gates on -- it is a
 * positive claim about what those kernels implement, so a door that has no
 * twin for the selected format falls through to its C twin instead of
 * assembling something that would scribble.
 */

#if TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_XRGB8888

typedef int toripixel_t;
#define TORIPIXEL_FORMAT_NAME "xrgb8888"
#define TORIPIXEL_IS_XRGB8888 1
#define TORIPIXEL_LANES_8BIT 1
#define TORIPIXEL_HAS_ALPHA_LANE 0
#define TORIPIXEL_COLOR_MASK 0x00FFFFFF
#define TORIPIXEL_ALPHA_MASK 0x00000000
#define toripixel_pack_argb8888 toripixel_xrgb8888_pack_argb8888
#define alpha_blend toripixel_xrgb8888_alpha_blend
#define shade_blend toripixel_xrgb8888_shade_blend
#define toripixel_is_key toripixel_xrgb8888_is_key
#define toripixel_texel_alpha toripixel_xrgb8888_texel_alpha
#define toripixel_to_argb8888 toripixel_xrgb8888_to_argb8888
/* The shading space IS this format, so the conversion is the identity by
 * definition -- not a mask that happens to be a no-op. */
#define toritexel_to_pixel(t) ((toripixel_t)(t))

#elif TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_ARGB8888

typedef uint32_t toripixel_t;
#define TORIPIXEL_FORMAT_NAME "argb8888"
#define TORIPIXEL_IS_XRGB8888 0
#define TORIPIXEL_LANES_8BIT 1
#define TORIPIXEL_HAS_ALPHA_LANE 1
#define TORIPIXEL_COLOR_MASK 0x00FFFFFFu
#define TORIPIXEL_ALPHA_MASK 0xFF000000u
#define toripixel_pack_argb8888 toripixel_argb8888_pack_argb8888
#define alpha_blend toripixel_argb8888_alpha_blend
#define shade_blend toripixel_argb8888_shade_blend
#define toripixel_is_key toripixel_argb8888_is_key
#define toripixel_texel_alpha toripixel_argb8888_texel_alpha
#define toripixel_to_argb8888 toripixel_argb8888_to_argb8888
/* The shading space IS this format, so the conversion is the identity by
 * definition -- not a mask that happens to be a no-op. */
#define toritexel_to_pixel(t) ((toripixel_t)(t))

#elif TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGBA8888

typedef uint32_t toripixel_t;
#define TORIPIXEL_FORMAT_NAME "rgba8888"
#define TORIPIXEL_IS_XRGB8888 0
#define TORIPIXEL_LANES_8BIT 1
#define TORIPIXEL_HAS_ALPHA_LANE 1
#define TORIPIXEL_COLOR_MASK 0xFFFFFF00u
#define TORIPIXEL_ALPHA_MASK 0x000000FFu
#define toripixel_pack_argb8888 toripixel_rgba8888_pack_argb8888
#define alpha_blend toripixel_rgba8888_alpha_blend
#define shade_blend toripixel_rgba8888_shade_blend
#define toripixel_is_key toripixel_rgba8888_is_key
#define toripixel_texel_alpha toripixel_rgba8888_texel_alpha
#define toripixel_to_argb8888 toripixel_rgba8888_to_argb8888
#define toritexel_to_pixel(t) toripixel_pack_argb8888(t)

#elif TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_ABGR8888

typedef uint32_t toripixel_t;
#define TORIPIXEL_FORMAT_NAME "abgr8888"
#define TORIPIXEL_IS_XRGB8888 0
#define TORIPIXEL_LANES_8BIT 1
#define TORIPIXEL_HAS_ALPHA_LANE 1
#define TORIPIXEL_COLOR_MASK 0x00FFFFFFu
#define TORIPIXEL_ALPHA_MASK 0xFF000000u
#define toripixel_pack_argb8888 toripixel_abgr8888_pack_argb8888
#define alpha_blend toripixel_abgr8888_alpha_blend
#define shade_blend toripixel_abgr8888_shade_blend
#define toripixel_is_key toripixel_abgr8888_is_key
#define toripixel_texel_alpha toripixel_abgr8888_texel_alpha
#define toripixel_to_argb8888 toripixel_abgr8888_to_argb8888
#define toritexel_to_pixel(t) toripixel_pack_argb8888(t)

#elif TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_BGRA8888

typedef uint32_t toripixel_t;
#define TORIPIXEL_FORMAT_NAME "bgra8888"
#define TORIPIXEL_IS_XRGB8888 0
#define TORIPIXEL_LANES_8BIT 1
#define TORIPIXEL_HAS_ALPHA_LANE 1
#define TORIPIXEL_COLOR_MASK 0xFFFFFF00u
#define TORIPIXEL_ALPHA_MASK 0x000000FFu
#define toripixel_pack_argb8888 toripixel_bgra8888_pack_argb8888
#define alpha_blend toripixel_bgra8888_alpha_blend
#define shade_blend toripixel_bgra8888_shade_blend
#define toripixel_is_key toripixel_bgra8888_is_key
#define toripixel_texel_alpha toripixel_bgra8888_texel_alpha
#define toripixel_to_argb8888 toripixel_bgra8888_to_argb8888
#define toritexel_to_pixel(t) toripixel_pack_argb8888(t)

#elif TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565

typedef uint16_t toripixel_t;
#define TORIPIXEL_FORMAT_NAME "rgb565"
#define TORIPIXEL_IS_XRGB8888 0
#define TORIPIXEL_LANES_8BIT 0
#define TORIPIXEL_HAS_ALPHA_LANE 0
#define TORIPIXEL_COLOR_MASK 0xFFFFu
#define TORIPIXEL_ALPHA_MASK 0x0000u
#define toripixel_pack_argb8888 toripixel_rgb565_pack_argb8888
#define alpha_blend toripixel_rgb565_alpha_blend
#define shade_blend toripixel_rgb565_shade_blend
#define toripixel_is_key toripixel_rgb565_is_key
#define toripixel_texel_alpha toripixel_rgb565_texel_alpha
#define toripixel_to_argb8888 toripixel_rgb565_to_argb8888
#define toritexel_to_pixel(t) toripixel_pack_argb8888(t)

#elif TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_ARGB1555

typedef uint16_t toripixel_t;
#define TORIPIXEL_FORMAT_NAME "argb1555"
#define TORIPIXEL_IS_XRGB8888 0
#define TORIPIXEL_LANES_8BIT 0
#define TORIPIXEL_HAS_ALPHA_LANE 1
#define TORIPIXEL_COLOR_MASK 0x7FFFu
#define TORIPIXEL_ALPHA_MASK 0x8000u
#define toripixel_pack_argb8888 toripixel_argb1555_pack_argb8888
#define alpha_blend toripixel_argb1555_alpha_blend
#define shade_blend toripixel_argb1555_shade_blend
#define toripixel_is_key toripixel_argb1555_is_key
#define toripixel_texel_alpha toripixel_argb1555_texel_alpha
#define toripixel_to_argb8888 toripixel_argb1555_to_argb8888
#define toritexel_to_pixel(t) toripixel_pack_argb8888(t)

#else
#error "TORIDRAW_PIXEL_FORMAT names no known format"
#endif


/*
 * Whether the texel shading space and the framebuffer format are the SAME
 * thing -- that is, whether toritexel_to_pixel is the identity.
 *
 * The vector texture spans compose in 8-bit lanes (which every format's texels
 * are) and then store a whole vector of native words. That last step is only
 * free where no conversion stands between the two, so those lanes claim this
 * and every other format takes the scalar span, which converts per pixel
 * through toritexel_to_pixel and is correct everywhere. A vector lane for a
 * converting format is a real kernel to write, not a cast to add.
 */
#if TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_XRGB8888 ||                                               \
    TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_ARGB8888
#define TORIPIXEL_TEXEL_SPACE_IS_NATIVE 1
#else
#define TORIPIXEL_TEXEL_SPACE_IS_NATIVE 0
#endif

/*
 * The store-time opaque fixup. Identity unless a target asked for it, so the
 * expression disappears everywhere else rather than costing an OR per pixel.
 */
#ifdef TORIDRAW_PIXEL_STORE_OPAQUE_FIXUP
#define TORIPIXEL_OPAQUE_FIXUP(p) ((toripixel_t)((p) | TORIPIXEL_ALPHA_MASK))
#else
#define TORIPIXEL_OPAQUE_FIXUP(p) (p)
#endif

/* The framebuffer word size, for code that reasons about strides in bytes. */
#define TORIPIXEL_BYTES ((int)sizeof(toripixel_t))

/*
 * The old spelling. TORIDRAW_PIXEL_T was overridable from outside and a few
 * build lanes may still set it; honour it as a hard error rather than letting
 * two authorities disagree about the width of a framebuffer word.
 */
#ifdef TORIDRAW_PIXEL_T
_Static_assert(
    sizeof(TORIDRAW_PIXEL_T) == sizeof(toripixel_t),
    "TORIDRAW_PIXEL_T disagrees with TORIDRAW_PIXEL_FORMAT about the pixel width");
#endif

#endif /* TORIDRAW_PIXEL_FORMAT_H */
