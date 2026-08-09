#ifndef SHARED_TABLES_H
#define SHARED_TABLES_H

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

extern const int* g_sin_table;
extern const int* g_cos_table;
extern const int* g_tan_table;

/** Initialize and select ToriDraw's built-in 2,048-entry sine table. */
void
ToriDraw_InitSinTable(void);
/** Initialize and select ToriDraw's built-in 2,048-entry cosine table. */
void
ToriDraw_InitCosTable(void);
/** Initialize and select ToriDraw's built-in 2,048-entry tangent table. */
void
ToriDraw_InitTanTable(void);

void
ToriDraw_SetSinTable(const int* table);
/**
 * Select the 2,048-entry 16.16 cosine table used by ToriDraw.
 *
 * ToriDraw does not take ownership. Passing NULL restores its standalone
 * built-in table, so callers may optionally share a table owned by another
 * library without creating a dependency on that library.
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
