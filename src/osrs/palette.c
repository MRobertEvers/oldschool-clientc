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