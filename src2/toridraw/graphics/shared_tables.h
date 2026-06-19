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
extern const int* RSCacheDat2A_NoiseCosTable;
extern const int* g_tan_table;

void
ToriDraw_SetSinTable(const int* table);
void
ToriDraw_SetCosTable(const int* table);
void
ToriDraw_SetTanTable(const int* table);
const int*
ToriDraw_GetSinTable(void);
const int*
ToriDraw_GetCosTable(void);
const int*
ToriDraw_GetTanTable(void);

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
init_sin_table(void);
void
init_cos_table(void);
void
init_tan_table(void);
void
init_reciprocal16(void);

#ifdef __cplusplus
}
#endif

#endif // SHARED_TABLES_H
