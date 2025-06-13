#include "palette.h"

#include <assert.h>

int
palette_rgb_to_hsl16(int rgb)
{
    double r = (double)((rgb >> 16) & 255) / 256.0;
    double g = (double)((rgb >> 8) & 255) / 256.0;
    double b = (double)(rgb & 255) / 256.0;

    double min = r;
    if( g < min )
        min = g;
    if( b < min )
        min = b;

    double max = r;
    if( g > max )
        max = g;
    if( b > max )
        max = b;

    double hue = 0.0;
    double saturation = 0.0;
    double lightness = (max + min) / 2.0;

    if( min != max )
    {
        if( lightness < 0.5 )
        {
            saturation = (max - min) / (max + min);
        }
        else
        {
            saturation = (max - min) / (2.0 - max - min);
        }

        if( r == max )
        {
            hue = (g - b) / (max - min);
        }
        else if( max == g )
        {
            hue = 2.0 + (b - r) / (max - min);
        }
        else if( max == b )
        {
            hue = 4.0 + (r - g) / (max - min);
        }
    }

    hue /= 6.0;
    if( hue < 0.0 )
        hue += 1.0;

    // Convert to 16-bit format:
    // - 6 bits for hue (0-63)
    // - 3 bits for saturation (0-7)
    // - 7 bits for lightness (0-127)
    int hue_bits = (int)(hue * 64.0);
    int sat_bits = (int)((saturation - 0.0625) * 8.0);
    int light_bits = (int)(lightness * 128.0);

    // Clamp values
    if( hue_bits < 0 )
        hue_bits = 0;
    else if( hue_bits > 63 )
        hue_bits = 63;

    if( sat_bits < 0 )
        sat_bits = 0;
    else if( sat_bits > 7 )
        sat_bits = 7;

    if( light_bits < 0 )
        light_bits = 0;
    else if( light_bits > 127 )
        light_bits = 127;

    // Pack into 16-bit value
    int hsl16 = (hue_bits << 10) | (sat_bits << 7) | light_bits;
    assert(hsl16 >= 0 && hsl16 < 65536);
    return hsl16;
}

//  const r = ((rgb >> 16) & 0xff) / 256.0;
//         const g = ((rgb >> 8) & 0xff) / 256.0;
//         const b = (rgb & 0xff) / 256.0;

//         let minRgb = r;
//         if (g < minRgb) {
//             minRgb = g;
//         }
//         if (b < minRgb) {
//             minRgb = b;
//         }

//         let maxRgb = r;
//         if (g > maxRgb) {
//             maxRgb = g;
//         }
//         if (b > maxRgb) {
//             maxRgb = b;
//         }

//         let hue = 0.0;
//         let sat = 0.0;
//         const light = (maxRgb + minRgb) / 2.0;

//         if (maxRgb !== minRgb) {
//             if (light < 0.5) {
//                 sat = (maxRgb - minRgb) / (maxRgb + minRgb);
//             }

//             if (light >= 0.5) {
//                 sat = (maxRgb - minRgb) / (2.0 - maxRgb - minRgb);
//             }

//             if (maxRgb === r) {
//                 hue = (g - b) / (maxRgb - minRgb);
//             } else if (maxRgb === g) {
//                 hue = 2.0 + (b - r) / (maxRgb - minRgb);
//             } else if (maxRgb === b) {
//                 hue = 4.0 + (r - g) / (maxRgb - minRgb);
//             }
//         }
//         hue /= 6.0;
//         this.saturation = (sat * 256.0) | 0;
//         this.lightness = (light * 256.0) | 0;
//         if (this.saturation < 0) {
//             this.saturation = 0;
//         } else if (this.saturation > 255) {
//             this.saturation = 255;
//         }
//         if (this.lightness < 0) {
//             this.lightness = 0;
//         } else if (this.lightness > 255) {
//             this.lightness = 255;
//         }
//         if (light > 0.5) {
//             this.hueMultiplier = (512.0 * (sat * (1.0 - light))) | 0;
//         } else {
//             this.hueMultiplier = (512.0 * (sat * light)) | 0;
//         }
//         if (this.hueMultiplier < 1) {
//             this.hueMultiplier = 1;
//         }
//         this.hue = (this.hueMultiplier * hue) | 0;

struct HSL
palette_rgb_to_hsl24(int rgb)
{
    double r = (double)((rgb >> 16) & 255) / (double)256.0;
    double g = (double)((rgb >> 8) & 255) / (double)256.0;
    double b = (double)(rgb & 255) / (double)256.0;

    double min = r;
    if( g < min )
        min = g;
    if( b < min )
        min = b;

    double max = r;
    if( g > max )
        max = g;
    if( b > max )
        max = b;

    double hue = 0.0;
    double saturation = 0.0;
    double lightness = (max + min) / 2.0;
    double mul = 1.0;

    if( min != max )
    {
        if( lightness < 0.5 )
        {
            saturation = (max - min) / (max + min);
        }
        else
        {
            saturation = (max - min) / (2.0 - max - min);
        }

        if( r == max )
        {
            hue = (g - b) / (max - min);
        }
        else if( max == g )
        {
            hue = 2.0 + (b - r) / (max - min);
        }
        else if( max == b )
        {
            hue = 4.0 + (r - g) / (max - min);
        }
    }

    hue /= 6.0;

    int hue_int = (int)(hue * 256.0);
    int sat_int = (int)(saturation * 256.0);
    int light_int = (int)(lightness * 256.0);

    if( sat_int < 0 )
        sat_int = 0;
    else if( sat_int > 255 )
        sat_int = 255;

    if( light_int < 0 )
        light_int = 0;
    else if( light_int > 255 )
        light_int = 255;

    int luminance;
    if( lightness > 0.5 )
        luminance = (int)(saturation * (1.0 - lightness) * (double)512.0);
    else
        luminance = (int)(saturation * lightness * (double)512.0);

    if( luminance < 1 )
        luminance = 1;

    struct HSL hsl;
    hsl.hue = hue_int;
    hsl.sat = sat_int;
    hsl.light = light_int;
    hsl.luminance = luminance;
    hsl.chroma = (int)(hue * ((double)luminance));
    return hsl;
}

int
palette_hsl24_to_hsl16(int hue, int saturation, int lightness)
{
    if( lightness > 179 )
        saturation >>= 1;

    if( lightness > 192 )
        saturation >>= 1;

    if( lightness > 217 )
        saturation >>= 1;

    if( lightness > 243 )
        saturation >>= 1;

    return ((hue / 4) << 10) + ((saturation / 32) << 7) + ((lightness / 2));
}