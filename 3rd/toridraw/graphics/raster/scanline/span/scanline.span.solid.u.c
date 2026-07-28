#ifndef TORIDRAW_RASTER_SCANLINE_SPAN_SOLID_U_C
#define TORIDRAW_RASTER_SCANLINE_SPAN_SOLID_U_C

/**
 * Solid-colour span kernels for the `scanline` family (flat + gouraud).
 *
 * Every kernel takes an already-clipped pixel run: a base offset and a count.
 * All bounds work happened once per segment in the walker, so these loops are
 * straight-line and vectorise cleanly.
 */

#include "graphics/alpha.h"
#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"

#ifndef TORIDRAW_PIXEL16
#include "graphics/raster/gouraud/span/gouraud.screen.alpha.span.u.c"
#endif

/* ------------------------------------------------------------------ flat */

static inline void
scanline_span_flat_opaque(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int count,
    toripixel_t rgb_color)
{
    int i = 0;

    for( ; i + 8 <= count; i += 8 )
    {
        pixel_buffer[offset + 0] = rgb_color;
        pixel_buffer[offset + 1] = rgb_color;
        pixel_buffer[offset + 2] = rgb_color;
        pixel_buffer[offset + 3] = rgb_color;
        pixel_buffer[offset + 4] = rgb_color;
        pixel_buffer[offset + 5] = rgb_color;
        pixel_buffer[offset + 6] = rgb_color;
        pixel_buffer[offset + 7] = rgb_color;
        offset += 8;
    }

    for( ; i < count; i++ )
        pixel_buffer[offset++] = rgb_color;
}

static inline void
scanline_span_flat_alpha(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int count,
    int rgb_color,
    int alpha)
{
    int i = 0;

#ifndef TORIDRAW_PIXEL16
    for( ; i + 4 <= count; i += 4 )
    {
        raster_linear_alpha_s4((uint32_t*)pixel_buffer, offset, rgb_color, alpha);
        offset += 4;
    }
#endif

    for( ; i < count; i++ )
    {
        pixel_buffer[offset] = (toripixel_t)alpha_blend(alpha, pixel_buffer[offset], rgb_color);
        offset += 1;
    }
}

/* --------------------------------------------------------------- gouraud */

/**
 * One palette lookup per four pixels, matching the `s4` kernels: the HSL16
 * accumulator advances by four steps at a time and the resulting RGB is reused
 * across the quad.
 *
 * A zero colour step means the whole run is one colour, which happens
 * constantly on terrain and untextured walls - that case degenerates to the
 * flat fill instead of walking an accumulator.
 */
static inline void
scanline_span_gouraud_opaque(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int count,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    if( color_step_hsl16_ish8 == 0 )
    {
        scanline_span_flat_opaque(
            pixel_buffer,
            offset,
            count,
            (toripixel_t)g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8]);
        return;
    }

    int quads = count >> 2;
    int step4 = color_step_hsl16_ish8 << 2;

    while( quads-- > 0 )
    {
        toripixel_t rgb_color = (toripixel_t)g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];

        pixel_buffer[offset + 0] = rgb_color;
        pixel_buffer[offset + 1] = rgb_color;
        pixel_buffer[offset + 2] = rgb_color;
        pixel_buffer[offset + 3] = rgb_color;
        offset += 4;

        color_hsl16_ish8 += step4;
    }

    int tail = count & 0x3;
    if( tail )
    {
        toripixel_t rgb_color = (toripixel_t)g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
        do
        {
            pixel_buffer[offset++] = rgb_color;
        } while( --tail );
    }
}

static inline void
scanline_span_gouraud_alpha(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int count,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8,
    int alpha)
{
    if( color_step_hsl16_ish8 == 0 )
    {
        scanline_span_flat_alpha(
            pixel_buffer, offset, count, g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8], alpha);
        return;
    }

    int quads = count >> 2;
    int step4 = color_step_hsl16_ish8 << 2;

    while( quads-- > 0 )
    {
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];

#ifndef TORIDRAW_PIXEL16
        raster_linear_alpha_s4((uint32_t*)pixel_buffer, offset, rgb_color, alpha);
        offset += 4;
#else
        for( int i = 0; i < 4; i++ )
        {
            pixel_buffer[offset] = (toripixel_t)alpha_blend(alpha, pixel_buffer[offset], rgb_color);
            offset += 1;
        }
#endif

        color_hsl16_ish8 += step4;
    }

    int tail = count & 0x3;
    if( tail )
    {
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
        do
        {
            pixel_buffer[offset] = (toripixel_t)alpha_blend(alpha, pixel_buffer[offset], rgb_color);
            offset += 1;
        } while( --tail );
    }
}

#endif
