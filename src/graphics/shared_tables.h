#ifndef SHARED_TABLES_H
#define SHARED_TABLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
extern int g_hsl16_to_rgb_table[65536];

extern int g_sin_table[2048];
extern int g_cos_table[2048];
extern int g_tan_table[2048];

extern int g_reciprocal16[4096];
extern int g_reciprocal15[4096];

/** 256 Ki entries; valid denominator indices are 1 .. G_RECIPROCAL16_SIMD_LEN-1. */
#define G_RECIPROCAL16_SIMD_LEN (256 * 1024)
extern uint16_t g_reciprocal16_simd[G_RECIPROCAL16_SIMD_LEN];

/** Normalized reciprocal for tex_vpentium4 perspective: w in [2^15, 2^16); entry i = 2^30 / (2^15 + i). */
#define G_RECIPROCAL_NORM_BITS 30
#define G_RECIPROCAL_NORM_LEN (1 << 15)
extern uint32_t g_reciprocal_norm30[G_RECIPROCAL_NORM_LEN];

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
