#ifndef SHARED_TABLES_H
#define SHARED_TABLES_H

#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TORIDRAW_TABLES_PRECOMPUTED)
#define TORIDRAW_TABLE_QUAL const
#else
#define TORIDRAW_TABLE_QUAL
#endif

#ifndef TORIDRAW_PIXEL_T
#  ifdef TORIDRAW_PIXEL16
#    define TORIDRAW_PIXEL_T uint16_t
#  else
#    define TORIDRAW_PIXEL_T int
#  endif
#endif
typedef TORIDRAW_PIXEL_T toripixel_t;

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
#ifdef TORIDRAW_PIXEL16
extern TORIDRAW_TABLE_QUAL uint16_t g_hsl16_to_rgb_table[65536];
#else
extern TORIDRAW_TABLE_QUAL int g_hsl16_to_rgb_table[65536];
#endif

/*
 * Gouraud rasterizers carry packed HSL16 in 8.8 fixed point while walking a
 * span.  A triangle edge can land one subpixel past its last vertex after the
 * fixed-point nudge/rounding, so the interpolant is not guaranteed to remain
 * inside the palette's unsigned-16 range even though every vertex is.  Keep
 * that rounding from turning into an out-of-bounds palette read.  This is
 * deliberately only for interpolated values: callers with a real hsl16_t
 * already have a valid index and retain the branch-free direct lookup.
 */
static inline int
ToriDraw_Hsl16Ish8ToRgb(int hsl16_ish8)
{
    int hsl16 = hsl16_ish8 >> 8;
    if( (unsigned)hsl16 > 0xFFFFu )
        hsl16 = hsl16 < 0 ? 0 : 0xFFFF;
    return g_hsl16_to_rgb_table[hsl16];
}

extern const int* g_sin_table;
extern const int* g_cos_table;
extern const int* g_tan_table;

/*
 * Interleaved { cosine, sine } pairs for the projection hot path.  The table is
 * rebuilt whenever either selected source table changes, so a yaw lookup is one
 * 8-byte pair on one cache line instead of two lines from two 8KB tables.
 *
 * Unconditional: projection16_apple.S reads it with `ld2r`, and the x86
 * prepared kernel (projection16_prepared.sse2.h) reads both halves per call.
 * 16KB of BSS is not worth a conditional-compilation trap where the definition
 * exists on one target and silently does not on the others.
 */
extern int g_projection_model_yaw_table[2048][2];

/** Initialize and select ToriDraw's built-in 2,048-entry sine table. */
void
ToriDraw_InitSinTable(void);
/** Initialize and select ToriDraw's built-in 2,048-entry cosine table. */
void
ToriDraw_InitCosTable(void);
/** Initialize and select ToriDraw's built-in 2,048-entry tangent table. */
void
ToriDraw_InitTanTable(void);

/**
 * Select the 2,048-entry 16.16 sine table used by ToriDraw.
 *
 * Selected custom tables must remain alive and unchanged until they are
 * replaced. Reselecting the same pointer refreshes projection-derived data.
 */
void
ToriDraw_SetSinTable(const int* table);
/**
 * Select the 2,048-entry 16.16 cosine table used by ToriDraw.
 *
 * ToriDraw does not take ownership. Passing NULL restores its standalone
 * built-in table, so callers may optionally share a table owned by another
 * library without creating a dependency on that library. Selected custom
 * tables must remain alive and unchanged until they are replaced; reselecting
 * the same pointer refreshes projection-derived data.
 */
void
ToriDraw_SetCosTable(const int* table);
void
ToriDraw_SetTanTable(const int* table);

static inline const int*
ToriDraw_GetSinTable(void)
{
    return g_sin_table;
}

/** Return the cosine-table pointer currently used by ToriDraw. */
static inline const int*
ToriDraw_GetCosTable(void)
{
    return g_cos_table;
}

static inline const int*
ToriDraw_GetTanTable(void)
{
    return g_tan_table;
}

/** Hot-path indexed readers. Callers are responsible for a 0..2047 index. */
static inline int
ToriDraw_ReadSinTable(int index)
{
    return g_sin_table[index];
}

static inline int
ToriDraw_ReadCosTable(int index)
{
    return g_cos_table[index];
}

static inline int
ToriDraw_ReadTanTable(int index)
{
    return g_tan_table[index];
}

/*
 * Inverse trigonometry, as a table.
 *
 * ## The unit
 *
 * Angles come back in **1/65536 of a turn**, signed. A full turn is 65536, a
 * quarter turn 16384, and the range of Atan2 is (-32768, 32768]. That unit is
 * chosen because it is what the callers want: a cylindrical or spherical
 * texture mapping is `atan2(...) / 2pi + 0.5`, which in this unit is just
 * `(turns16 + 32768)`, with no pi anywhere. It is a finer unit than the
 * 2048-per-turn index the sin/cos tables take, and deliberately so — 2048 steps
 * across a 128-texel wrap is 16 texels of quantisation, which would be visible
 * banding; 65536 puts it at 1/512 of a texel.
 *
 * ## Why a table at all
 *
 * Not speed — these run per vertex, not per pixel. **Determinism.** libm's
 * atan2 is not bit-identical across platforms or versions, and uv generated
 * from it therefore is not either. This library has been bitten by exactly that
 * shape of bug before (see the p[256] Perlin note in
 * docs/rs2012_materials_backport/README.md §9: a bake that differed between
 * machines and hid from a re-run). A table plus a float divide is reproducible
 * everywhere.
 *
 * ## There is no separate arcsine table
 *
 * `ToriDraw_AsinTurns16` is real and callable, but it is backed by the same
 * atan2 table through `asin(x) == atan2(x, sqrt(1 - x*x))`. That is not a
 * shortcut, it is the better numerical choice: arcsine's derivative is
 * unbounded at +/-1, so a uniformly sampled arcsine table is at its worst
 * exactly where spherical mapping puts the poles. Routing through atan2 samples
 * a function whose derivative is bounded everywhere. `sqrtf` is an IEEE-exact
 * hardware instruction, not a libm approximation, so determinism is preserved.
 *
 * Spherical mapping does not even need the identity: `asin(y / |v|)` is exactly
 * `atan2(y, sqrt(x*x + z*z))`, which skips both the division and the sqrt of
 * the full length. The generator uses that form.
 */

/** Entries over t in [0, 1]; +1 so the t == 1 endpoint is a real entry and the
 *  interpolation below never indexes past the end. */
#define TORIDRAW_ATAN_TABLE_BITS 11
#define TORIDRAW_ATAN_TABLE_LEN ((1 << TORIDRAW_ATAN_TABLE_BITS) + 1)

/** atan(i / (1 << BITS)) in 1/65536 turns, so entries run 0 .. 8192. */
extern TORIDRAW_TABLE_QUAL int g_atan_turns16_table[TORIDRAW_ATAN_TABLE_LEN];

/** Build the arctangent table. Idempotent; required before Atan2/Asin. */
void
ToriDraw_InitAtanTable(void);

/**
 * atan2(y, x) in 1/65536 turns, range (-32768, 32768].
 *
 * Octant reduction keeps the table argument in [0, 1], where atan is smooth and
 * the linear interpolation below is worth well under one unit. atan2(0, 0) is 0
 * by convention, matching C.
 */
static inline int
ToriDraw_Atan2Turns16(float y, float x)
{
    float ay = y < 0.0f ? -y : y;
    float ax = x < 0.0f ? -x : x;

    if( ay == 0.0f && ax == 0.0f )
        return 0;

    /* Reduce to the first octant: the ratio is always the smaller over the
     * larger, so it lands in [0, 1] and the table is never extrapolated. */
    int swapped = ay > ax;
    float ratio = swapped ? (ax / ay) : (ay / ax);

    /* Guard the conversion rather than trusting the divide: a denormal or an
     * infinity in the inputs can put the ratio outside [0,1], and an
     * out-of-range float-to-int conversion is undefined (INT_MIN on x86). */
    if( !(ratio >= 0.0f) )
        ratio = 0.0f;
    else if( ratio > 1.0f )
        ratio = 1.0f;

    float scaled = ratio * (float)(1 << TORIDRAW_ATAN_TABLE_BITS);
    int index = (int)scaled;
    if( index >= (1 << TORIDRAW_ATAN_TABLE_BITS) )
        index = (1 << TORIDRAW_ATAN_TABLE_BITS) - 1;
    int frac = (int)((scaled - (float)index) * 256.0f);

    int lo = g_atan_turns16_table[index];
    int hi = g_atan_turns16_table[index + 1];
    int angle = lo + (((hi - lo) * frac) >> 8);

    /* First octant -> first quadrant. */
    if( swapped )
        angle = 16384 - angle;

    /* First quadrant -> the right one. Signs only; no further table work. */
    if( x < 0.0f )
        angle = 32768 - angle;
    if( y < 0.0f )
        angle = -angle;

    return angle;
}

/**
 * asin(x) in 1/65536 turns, range [-16384, 16384]. See the note above on why
 * this is atan2-backed rather than a table of its own. Out-of-range inputs
 * saturate instead of producing a NaN.
 */
static inline int
ToriDraw_AsinTurns16(float x)
{
    if( x >= 1.0f )
        return 16384;
    if( x <= -1.0f )
        return -16384;
    return ToriDraw_Atan2Turns16(x, sqrtf(1.0f - x * x));
}

extern TORIDRAW_TABLE_QUAL int g_reciprocal16[4096];
extern TORIDRAW_TABLE_QUAL int g_reciprocal15[4096];

#ifndef TORIDRAW_DISABLE_SIMD_TABLES
/** 256 Ki entries; valid denominator indices are 1 .. G_RECIPROCAL16_SIMD_LEN-1. */
#define G_RECIPROCAL16_SIMD_LEN (256 * 1024)
extern uint16_t g_reciprocal16_simd[G_RECIPROCAL16_SIMD_LEN];

/** Normalized reciprocal for tex_vpentium4 perspective: w in [2^15, 2^16); entry i = 2^30 / (2^15 + i). */
#define G_RECIPROCAL_NORM_BITS 30
#define G_RECIPROCAL_NORM_LEN (1 << 15)
extern uint32_t g_reciprocal_norm30[G_RECIPROCAL_NORM_LEN];
#endif

void
init_hsl16_to_rgb_table(void);
void
init_reciprocal16(void);

#ifdef __cplusplus
}
#endif

#endif // SHARED_TABLES_H
