#include "graphics/shared_tables.h"

#include <math.h>
#include <stdint.h>

#ifndef TORIDRAW_TABLES_PRECOMPUTED

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
int g_hsl16_to_rgb_table[65536];

static int g_sin_table_builtin[2048];
static int g_cos_table_builtin[2048];
static int g_tan_table_builtin[2048];

const int* g_sin_table = g_sin_table_builtin;
const int* RSCacheDat2A_NoiseCosTable = g_cos_table_builtin;
const int* g_tan_table = g_tan_table_builtin;

int g_reciprocal15[4096];
int g_reciprocal16[4096];

#ifndef TORIDRAW_DISABLE_SIMD_TABLES
uint16_t g_reciprocal16_simd[G_RECIPROCAL16_SIMD_LEN];
uint32_t g_reciprocal_norm30[G_RECIPROCAL_NORM_LEN];
#endif

#else /* TORIDRAW_TABLES_PRECOMPUTED */

const int* g_sin_table;
const int* RSCacheDat2A_NoiseCosTable;
const int* g_tan_table;

#endif /* !TORIDRAW_TABLES_PRECOMPUTED */

int
pix3d_set_gamma(
    int rgb,
    double gamma)
{
    double r = (double)(rgb >> 16) / 256.0;
    double g = (double)(rgb >> 8 & 0xff) / 256.0;
    double b = (double)(rgb & 0xff) / 256.0;
    double powR = pow(r, gamma);
    double powG = pow(g, gamma);
    double powB = pow(b, gamma);
    int intR = (int)(powR * 256.0);
    int intG = (int)(powG * 256.0);
    int intB = (int)(powB * 256.0);
    return (intR << 16) + (intG << 8) + intB;
}

static void
pix3d_init_palette(
    int* palette,
    double brightness)
{
    double random_brightness = brightness;
    int offset = 0;
    for( int y = 0; y < 512; y++ )
    {
        double hue = (double)(y / 8) / 64.0 + 0.0078125;
        double saturation = (double)(y & 0x7) / 8.0 + 0.0625;
        for( int x = 0; x < 128; x++ )
        {
            double lightness = (double)x / 128.0;
            double r = lightness;
            double g = lightness;
            double b = lightness;
            if( saturation != 0.0 )
            {
                double q;
                if( lightness < 0.5 )
                {
                    q = lightness * (saturation + 1.0);
                }
                else
                {
                    q = lightness + saturation - lightness * saturation;
                }
                double p = lightness * 2.0 - q;
                double t = hue + 0.3333333333333333;
                if( t > 1.0 )
                {
                    t--;
                }
                double d11 = hue - 0.3333333333333333;
                if( d11 < 0.0 )
                {
                    d11++;
                }
                if( t * 6.0 < 1.0 )
                {
                    r = p + (q - p) * 6.0 * t;
                }
                else if( t * 2.0 < 1.0 )
                {
                    r = q;
                }
                else if( t * 3.0 < 2.0 )
                {
                    r = p + (q - p) * (0.6666666666666666 - t) * 6.0;
                }
                else
                {
                    r = p;
                }
                if( hue * 6.0 < 1.0 )
                {
                    g = p + (q - p) * 6.0 * hue;
                }
                else if( hue * 2.0 < 1.0 )
                {
                    g = q;
                }
                else if( hue * 3.0 < 2.0 )
                {
                    g = p + (q - p) * (0.6666666666666666 - hue) * 6.0;
                }
                else
                {
                    g = p;
                }
                if( d11 * 6.0 < 1.0 )
                {
                    b = p + (q - p) * 6.0 * d11;
                }
                else if( d11 * 2.0 < 1.0 )
                {
                    b = q;
                }
                else if( d11 * 3.0 < 2.0 )
                {
                    b = p + (q - p) * (0.6666666666666666 - d11) * 6.0;
                }
                else
                {
                    b = p;
                }
            }
            int intR = (int)(r * 256.0);
            int intG = (int)(g * 256.0);
            int intB = (int)(b * 256.0);

            // int intR = 16 + (int)(r * 219.0 + 0.5);
            // int intG = 16 + (int)(g * 219.0 + 0.5);
            // int intB = 16 + (int)(b * 219.0 + 0.5);

            int rgb = (intR << 16) + (intG << 8) + intB;
            int rgbAdjusted = pix3d_set_gamma(rgb, random_brightness);
            palette[offset++] = rgbAdjusted;
        }
    }
}

#ifdef TORIDRAW_TABLES_PRECOMPUTED

void
init_hsl16_to_rgb_table(void)
{
}

void
init_sin_table(void)
{
}

void
init_cos_table(void)
{
}

void
init_tan_table(void)
{
}

void
init_reciprocal16(void)
{
}

#else /* !TORIDRAW_TABLES_PRECOMPUTED */

void
init_hsl16_to_rgb_table(void)
{
    // 0 and 128 are both black.
    pix3d_init_palette(g_hsl16_to_rgb_table, 0.8);
}

void
init_sin_table(void)
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(sin((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_sin_table_builtin[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
}

void
init_cos_table(void)
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(cos((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_cos_table_builtin[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
}

void
init_tan_table(void)
{
    for( int i = 0; i < 2048; i++ )
        g_tan_table_builtin[i] = (int)(tan((double)i * 0.0030679615) * (1 << 16));
}

void
init_reciprocal16(void)
{
    for( int i = 1; i < 4096; i++ )
        g_reciprocal16[i] = ((1 << 16) / i);

    for( int i = 1; i < 4096; i++ )
        g_reciprocal15[i] = ((1 << 15) / i);

#ifndef TORIDRAW_DISABLE_SIMD_TABLES
    for( int i = 1; i < G_RECIPROCAL16_SIMD_LEN; i++ )
        g_reciprocal16_simd[i] = ((1 << 16) / i);

    for( uint32_t i = 0; i < G_RECIPROCAL_NORM_LEN; i++ )
        g_reciprocal_norm30[i] = (uint32_t)((1u << G_RECIPROCAL_NORM_BITS) / (0x8000u + i));
#endif
}

#endif /* TORIDRAW_TABLES_PRECOMPUTED */

void
ToriDraw_SetSinTable(const int* table)
{
#ifndef TORIDRAW_TABLES_PRECOMPUTED
    g_sin_table = table ? table : g_sin_table_builtin;
#else
    g_sin_table = table;
#endif
}

void
ToriDraw_SetCosTable(const int* table)
{
#ifndef TORIDRAW_TABLES_PRECOMPUTED
    RSCacheDat2A_NoiseCosTable = table ? table : g_cos_table_builtin;
#else
    RSCacheDat2A_NoiseCosTable = table;
#endif
}

void
ToriDraw_SetTanTable(const int* table)
{
#ifndef TORIDRAW_TABLES_PRECOMPUTED
    g_tan_table = table ? table : g_tan_table_builtin;
#else
    g_tan_table = table;
#endif
}

const int*
ToriDraw_GetSinTable(void)
{
    return g_sin_table;
}

const int*
ToriDraw_GetCosTable(void)
{
    return RSCacheDat2A_NoiseCosTable;
}

const int*
ToriDraw_GetTanTable(void)
{
    return g_tan_table;
}
