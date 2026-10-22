#ifndef TORIDRAW_RSZBUF_RASTER_H
#define TORIDRAW_RSZBUF_RASTER_H

/*
 * The official client's depth-buffered software rasterizer, rev 241.
 *
 * `::zbuf 1` swaps the client's rasterizer (class169.field2308) from the painter
 * (class160) to a depth-tested one (class162). This family is that one, ported
 * method for method: impl/raster/rszbuf/ holds the four triangle setups and
 * their spans, and this header the state they share and the pixel stores every
 * span goes through.
 *
 * It is NOT the SD painter with a depth test added -- that is the `zbuf` axis of
 * the SD families (impl/raster/dispatch/tri.zbuf.u.c). This one rasterizes from
 * float vertices (the 241 projection keeps sub-pixel positions), interpolates
 * depth as a float plane solved in float, tests it branch-free, and gates every
 * colour and depth store through two masks a caller sets per model. Each of
 * those changes pixels relative to the painter, which is why it is a family of
 * its own rather than a variant.
 *
 * Java semantics, kept exactly:
 *
 *   - float maths is IEEE single with NO fused multiply-add. The files that
 *     include this header turn FP_CONTRACT off; a compiler that fuses a*b+c
 *     rounds once where Java rounds twice.
 *   - (int) of a float saturates and maps NaN to 0 (rszbuf_java_int). A C cast
 *     of an out-of-range float is undefined, and x86 answers INT_MIN where Java
 *     answers INT_MAX.
 *   - int maths wraps. Every accumulator that can leave int range goes through
 *     graphics/int_wrap.h, and the colour arithmetic below works in uint32_t
 *     and shifts the signed result, as Java's `>>` does.
 *
 * Pixels are 32-bit 0x00RRGGBB words, as the client's are; the family is only
 * built for TORIPIXEL_IS_XRGB8888.
 */

#include "graphics/dash_restrict.h"
#include "graphics/int_wrap.h"
#include "graphics/pixel_format.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * The rasterizer's state: class175 (shared by both of the client's
 * rasterizers) plus the three fields class162 adds. Deob names in brackets.
 */
struct ToriDraw_RsZbufRaster
{
    /* The target. [Statics.field7152 / field7153 / field7156] */
    toripixel_t* RESTRICT pixels;
    float* RESTRICT zbuffer;
    int stride;

    /* Viewport. A row starts at y * stride [the client's row table,
     * field2462, holds exactly that]; the clip box is 0..clip_width x
     * 0..clip_height [field2456, field2457]; the projection centre
     * [field2454, field2455] anchors the texture planes. */
    int clip_width;
    int clip_height;
    int center_x;
    int center_y;

    /** A corner lies outside 0..clip_width, so spans clamp x [field2459]. */
    bool clip_x;
    /** Face transparency, 0 opaque. 254 is not a weight: it smears the pixel
     *  to the right one place left [field2446]. */
    int face_alpha;
    /** Gouraud colour stepped once per four pixels rather than per pixel
     *  [field2450, low detail]. */
    bool gouraud_blocks4;
    /** -1 lets a passing pixel write colour, 0 does not [field2465]. */
    int color_write_mask;
    /** -1 lets a passing pixel write depth, 0 does not [field2442]. */
    int depth_write_mask;
    /** Perspective numerator, 512 at the default zoom [field2452]. */
    int projection_scale;

    /** hsl16 -> pixel [class162.field2156]. */
    const toripixel_t* palette;
    /** The texture being drawn has no colour key [class162.field2155]. */
    bool texture_opaque;
};

/*
 * Java never fuses a*b+c. Clang does by default (-ffp-contract=on), so every
 * function here with float maths opens with this; at block scope it reaches
 * that function and nothing else in the unity build. GCC builds in ISO C mode,
 * where contraction is already off, and does not know the pragma.
 */
#if defined(__clang__)
#define RSZBUF_NO_FMA _Pragma("STDC FP_CONTRACT OFF")
#else
#define RSZBUF_NO_FMA
#endif

/* ------------------------------------------------------------ Java ints */

static inline int32_t
rszbuf_float_bits(float value)
{
    int32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline float
rszbuf_bits_float(int32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/** Java's (int) of a float: truncate, saturate, NaN -> 0. */
static inline int
rszbuf_java_int(float value)
{
    if( value != value )
        return 0;
    if( value >= 2147483648.0f )
        return INT32_MAX;
    if( value <= -2147483648.0f )
        return INT32_MIN;
    return (int)value;
}

/** Java's int `/`: truncating, and INT_MIN / -1 wraps rather than traps. */
static inline int
rszbuf_java_div(int dividend, int divisor)
{
    if( divisor == -1 )
        return (int)(0u - (uint32_t)dividend);
    return dividend / divisor;
}

/** Java's `>>` on an int: arithmetic. */
static inline int32_t
rszbuf_sar(uint32_t bits, int shift)
{
    return (int32_t)bits >> shift;
}

/* ------------------------------------------------------------ the test */

/**
 * All ones when `key` is nearer than `stored`, else zero [class162.method5387].
 *
 * The sign bit of their difference, spread. The key grows as depth shrinks
 * (rszbuf_depth_key), so a negative `stored - key` is a nearer pixel; equal
 * keys give +0 and keep what is there.
 */
static inline int32_t
rszbuf_nearer_mask(float stored, float key)
{
    return rszbuf_float_bits(stored - key) >> 31;
}

/**
 * The depth key of a camera-space depth [Statics.method1874]: a reciprocal,
 * larger is nearer, 0 is farther than any surface (the cleared buffer).
 */
static inline float
rszbuf_depth_key(float depth)
{
    RSZBUF_NO_FMA
    float const scaled = (depth - 75.0f) * 1.0100503f;
    return 150.75377f / scaled + 1.0100503f;
}

/* ------------------------------------------------------- pixel stores */

/**
 * Write `rgb` and `key` where `nearer` passes, each behind its own mask
 * [class162.method5342].
 */
static inline void
rszbuf_store_masked(
    struct ToriDraw_RsZbufRaster* r,
    int at,
    int32_t rgb,
    float key,
    int32_t nearer)
{
    int32_t const color = r->color_write_mask & nearer;
    int32_t const depth = r->depth_write_mask & nearer;

    r->pixels[at] = (toripixel_t)((color & rgb) | (~color & (int32_t)r->pixels[at]));
    r->zbuffer[at] = rszbuf_bits_float(
        (depth & rszbuf_float_bits(key)) | (~depth & rszbuf_float_bits(r->zbuffer[at])));
}

/** An opaque pixel [class162.method5346]. */
static inline void
rszbuf_store(
    struct ToriDraw_RsZbufRaster* r,
    int at,
    int32_t rgb,
    float key)
{
    rszbuf_store_masked(r, at, rgb, key, rszbuf_nearer_mask(r->zbuffer[at], key));
}

/**
 * A blended pixel: the destination scaled by `dest_weight`/256 plus a source
 * already scaled by the caller. Never writes depth [class162.method5355].
 */
static inline void
rszbuf_store_blend(
    struct ToriDraw_RsZbufRaster* r,
    int at,
    int32_t source_scaled,
    int dest_weight,
    float key)
{
    uint32_t const dest = (uint32_t)r->pixels[at];
    uint32_t const weight = (uint32_t)dest_weight;
    int32_t const mixed =
        (int32_t)((rszbuf_sar((dest & 0xFF00u) * weight, 8) & 0xFF00) +
                  (rszbuf_sar((dest & 0xFF00FFu) * weight, 8) & 0xFF00FF) +
                  (uint32_t)source_scaled);
    int32_t const color = r->color_write_mask & rszbuf_nearer_mask(r->zbuffer[at], key);

    r->pixels[at] = (toripixel_t)((~color & (int32_t)dest) + (color & mixed));
}

/** `rgb` scaled by `weight`/256 per channel, as the blends pre-scale a source. */
static inline int32_t
rszbuf_scale_rgb(int32_t rgb, int weight)
{
    uint32_t const c = (uint32_t)rgb;
    uint32_t const w = (uint32_t)weight;

    return (int32_t)((rszbuf_sar((c & 0xFF00FFu) * w, 8) & 0xFF00FF) +
                     (rszbuf_sar((c & 0xFF00u) * w, 8) & 0xFF00));
}

/* ------------------------------------------------------ texel stores */

/** The texel at a packed (u << 18 | v) coordinate of a 128-wide texture. */
static inline int32_t
rszbuf_texel(const int* RESTRICT texels, int32_t uv)
{
    return texels[(int)((uint32_t)uv >> 25) + (uv & 0x3F80)];
}

/** A texel lit by `shade` (0..255) [the shading in class162.method5379]. */
static inline int32_t
rszbuf_shade_texel(int32_t texel, int shade)
{
    uint32_t const t = (uint32_t)texel;
    uint32_t const s = (uint32_t)shade;

    return rszbuf_sar(((t & 0xFF00FFu) * s & 0xFF00FF00u) + ((t & 0xFF00u) * s & 0xFF0000u), 8);
}

/** The colour key: RGB 0 is not drawn. Our texels carry alpha in the high
 *  byte where the client's carry none, so only RGB is compared. */
static inline bool
rszbuf_texel_keyed_out(int32_t texel)
{
    return (texel & 0xFFFFFF) == 0;
}

/** An opaque texel [class162.method5379]. */
static inline void
rszbuf_store_texel(
    struct ToriDraw_RsZbufRaster* r,
    const int* RESTRICT texels,
    int at,
    int shade,
    int32_t uv,
    float key)
{
    rszbuf_store(r, at, rszbuf_shade_texel(rszbuf_texel(texels, uv), shade), key);
}

/** A colour-keyed texel [class162.method5377]. */
static inline void
rszbuf_store_texel_keyed(
    struct ToriDraw_RsZbufRaster* r,
    const int* RESTRICT texels,
    int at,
    int shade,
    int32_t uv,
    float key)
{
    int32_t const texel = rszbuf_texel(texels, uv);

    if( !rszbuf_texel_keyed_out(texel) )
        rszbuf_store(r, at, rszbuf_shade_texel(texel, shade), key);
}

/**
 * A texel blended over the destination [class162.method5343, and with the
 * colour key method5351]. `source_shade` already carries the source weight
 * (shade * (256 - alpha) >> 8); `dest_weight` is the face alpha. Never writes
 * depth.
 */
static inline void
rszbuf_store_texel_blend(
    struct ToriDraw_RsZbufRaster* r,
    const int* RESTRICT texels,
    int at,
    int dest_weight,
    int source_shade,
    int32_t uv,
    float key,
    bool keyed)
{
    int32_t const texel = rszbuf_texel(texels, uv);
    uint32_t const t = (uint32_t)texel;
    uint32_t const s = (uint32_t)source_shade;
    uint32_t const dest = (uint32_t)r->pixels[at];
    uint32_t const w = (uint32_t)dest_weight;

    if( keyed && rszbuf_texel_keyed_out(texel) )
        return;

    int32_t const lit =
        rszbuf_sar(((t & 0xFF00FFu) * s & 0xFF00FF00u) | ((t & 0xFF00u) * s & 0xFF0000u), 8);
    int32_t const kept =
        rszbuf_sar(((dest & 0xFF00FFu) * w & 0xFF00FF00u) | ((dest & 0xFF00u) * w & 0xFF0000u), 8);
    int32_t const mixed = (int32_t)((uint32_t)kept + (uint32_t)lit);
    int32_t const color = r->color_write_mask & rszbuf_nearer_mask(r->zbuffer[at], key);

    r->pixels[at] = (toripixel_t)((int32_t)((uint32_t)(~color & (int32_t)dest) +
                                            (uint32_t)(color & mixed)));
}

/**
 * The depth at the start of a triangle's first row, at x == 0
 * [class162.method5347]: the top corner's key walked back to column 0, and down
 * to row 0 when the top is above the viewport. The last term moves it from the
 * corner's exact y to the row the walk rounds that y to.
 */
static inline float
rszbuf_row_start_depth(
    float x_top,
    float y_top,
    float key_top,
    float key_step_x,
    float key_step_y)
{
    RSZBUF_NO_FMA
    float key = key_top - x_top * key_step_x + key_step_x;

    if( y_top < 0.0f )
        key -= (float)rszbuf_java_int(y_top) * key_step_y;
    return key - (y_top - (float)rszbuf_java_int(y_top + 0.5f)) * key_step_y;
}

#endif
