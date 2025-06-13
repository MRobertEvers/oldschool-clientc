#ifndef _PALETTE_H
#define _PALETTE_H

#include <stdint.h>

// 16-bit HSL values are packed as follows:
// 6 bits for hue
// 3 bits for saturation
// 7 bits for lightness

#define HSL_HUE(x) ((x) >> 10)
#define HSL_SAT(x) (((x) >> 7) & 0x7)
#define HSL_LIGHT(x) ((x) & 0x7F)

int palette_rgb_to_hsl16(int rgb);

struct HSL
{
    int hue;
    int sat;
    int light;

    int luminance;

    int chroma;
};

struct HSL palette_rgb_to_hsl24(int rgb);

int palette_hsl24_to_hsl16(int hue, int saturation, int lightness);
int palette_pack_hsl24(int hue, int saturation, int lightness);
#endif