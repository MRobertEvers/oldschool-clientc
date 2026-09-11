#include "graphics/shared_tables.h"

#include <math.h>
#include <stdint.h>

_Alignas(64) int g_projection_model_yaw_table[2048][2];

#ifndef TORIDRAW_TABLES_PRECOMPUTED

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
toripixel_t g_hsl16_to_pixel_table[65536];

static int g_sin_table_builtin[2048];
static int g_cos_table_builtin[2048];
static int g_tan_table_builtin[2048];
int g_atan_turns16_table[TORIDRAW_ATAN_TABLE_LEN];

const int* g_sin_table = g_sin_table_builtin;
const int* g_cos_table = g_cos_table_builtin;
const int* g_tan_table = g_tan_table_builtin;

int g_reciprocal15[4096];
int g_reciprocal16[4096];

#ifdef TORIDRAW_SIMD_RECIPROCAL_TABLES
uint16_t g_reciprocal16_simd[G_RECIPROCAL16_SIMD_LEN];
uint32_t g_reciprocal_norm30[G_RECIPROCAL_NORM_LEN];
#endif

#else /* TORIDRAW_TABLES_PRECOMPUTED */

/*
 * The tables are `const` arrays in a GENERATED translation unit, which a
 * target with a linker script puts in ROM. That is the whole point: the
 * palette alone is 128 KB at 16bpp, and on a client with a quarter-megabyte
 * of RAM it has to come out of RAM or nothing else fits.
 *
 * Generate the unit with tools/toridraw_tables_gen.c, built with the SAME
 * -DTORIDRAW_PIXEL_FORMAT as the target -- the palette's element type is
 * toripixel_t, so a unit generated for one format is the wrong width for
 * another. The generator includes THIS file and calls the builders below, so
 * the emitted values are the ones the runtime path computes rather than a
 * second implementation of the same math.
 *
 * g_projection_model_yaw_table stays mutable: it is DERIVED from whichever
 * sine and cosine tables are selected, and ToriDraw_SetSinTable exists so a
 * host may select its own. 16 KB is what a precomputed build still pays.
 *
 * The generated unit DEFINES g_hsl16_to_pixel_table, g_atan_turns16_table,
 * g_reciprocal15 and g_reciprocal16 under their canonical names -- the header
 * already declares them `const` arrays here, so nothing downstream changes
 * spelling or pays an indirection. It also defines the three trigonometric
 * tables under their own names, because those are reached through a POINTER
 * a host may repoint (ToriDraw_SetSinTable).
 */
extern const int g_toridraw_sin_precomputed[2048];
extern const int g_toridraw_cos_precomputed[2048];
extern const int g_toridraw_tan_precomputed[2048];

const int* g_sin_table = g_toridraw_sin_precomputed;
const int* g_cos_table = g_toridraw_cos_precomputed;
const int* g_tan_table = g_toridraw_tan_precomputed;

#endif /* !TORIDRAW_TABLES_PRECOMPUTED */

static void
ToriDraw_RebuildProjectionModelYawTable(void)
{
    if( !g_cos_table || !g_sin_table )
        return;

    for( int i = 0; i < 2048; i++ )
    {
        g_projection_model_yaw_table[i][0] = g_cos_table[i];
        g_projection_model_yaw_table[i][1] = g_sin_table[i];
    }
}

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
    toripixel_t* palette,
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
            /* The one HSL -> framebuffer conversion in the library. Every
             * solid kernel is a lookup into what this writes, so the format
             * is decided here and nowhere downstream. */
            palette[offset++] = toripixel_pack_argb8888((uint32_t)rgbAdjusted);
        }
    }
}

#ifdef TORIDRAW_TABLES_PRECOMPUTED

void
init_hsl16_to_pixel_table(void)
{
}

/* The tables are already there; selecting them and refreshing the derived
 * yaw pairs is all an init can still do. Idempotent, like the builders. */
void
ToriDraw_InitSinTable(void)
{
    g_sin_table = g_toridraw_sin_precomputed;
    ToriDraw_RebuildProjectionModelYawTable();
}

void
ToriDraw_InitCosTable(void)
{
    g_cos_table = g_toridraw_cos_precomputed;
    ToriDraw_RebuildProjectionModelYawTable();
}

void
ToriDraw_InitTanTable(void)
{
    g_tan_table = g_toridraw_tan_precomputed;
}

void
ToriDraw_InitAtanTable(void)
{
}

void
init_reciprocal16(void)
{
}

#else /* !TORIDRAW_TABLES_PRECOMPUTED */

void
init_hsl16_to_pixel_table(void)
{
    // 0 and 128 are both black.
    pix3d_init_palette(g_hsl16_to_pixel_table, 0.8);
}

void
ToriDraw_InitSinTable(void)
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(sin((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_sin_table_builtin[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
    g_sin_table = g_sin_table_builtin;
    ToriDraw_RebuildProjectionModelYawTable();
}

void
ToriDraw_InitCosTable(void)
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(cos((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_cos_table_builtin[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
    g_cos_table = g_cos_table_builtin;
    ToriDraw_RebuildProjectionModelYawTable();
}

void
ToriDraw_InitTanTable(void)
{
    for( int i = 0; i < 2048; i++ )
        g_tan_table_builtin[i] = (int)(tan((double)i * 0.0030679615) * (1 << 16));
    g_tan_table = g_tan_table_builtin;
}

void
ToriDraw_InitAtanTable(void)
{
    /* atan(t) for t in [0, 1], expressed in 1/65536 turns: atan(1) is an eighth
     * of a turn, so the last entry is exactly 8192. Built in double and rounded
     * to nearest so the table itself contributes no bias. */
    for( int i = 0; i < TORIDRAW_ATAN_TABLE_LEN; i++ )
    {
        double t = (double)i / (double)(1 << TORIDRAW_ATAN_TABLE_BITS);
        double turns = atan(t) / (2.0 * 3.14159265358979323846);
        g_atan_turns16_table[i] = (int)(turns * 65536.0 + 0.5);
    }
}

void
init_reciprocal16(void)
{
    for( int i = 1; i < 4096; i++ )
        g_reciprocal16[i] = ((1 << 16) / i);

    for( int i = 1; i < 4096; i++ )
        g_reciprocal15[i] = ((1 << 15) / i);

#ifdef TORIDRAW_SIMD_RECIPROCAL_TABLES
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
    g_sin_table = table ? table : g_toridraw_sin_precomputed;
#endif
    ToriDraw_RebuildProjectionModelYawTable();
}

void
ToriDraw_SetCosTable(const int* table)
{
#ifndef TORIDRAW_TABLES_PRECOMPUTED
    g_cos_table = table ? table : g_cos_table_builtin;
#else
    g_cos_table = table ? table : g_toridraw_cos_precomputed;
#endif
    ToriDraw_RebuildProjectionModelYawTable();
}

void
ToriDraw_SetTanTable(const int* table)
{
#ifndef TORIDRAW_TABLES_PRECOMPUTED
    g_tan_table = table ? table : g_tan_table_builtin;
#else
    g_tan_table = table ? table : g_toridraw_tan_precomputed;
#endif
}
