#ifndef SHARED_TABLES_H
#define SHARED_TABLES_H

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

void init_sin_table(void);
void init_cos_table(void);
void init_tan_table(void);

#ifdef __cplusplus
}
#endif

#endif // SHARED_TABLES_H
