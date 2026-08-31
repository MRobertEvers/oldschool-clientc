#ifndef TORIDRAW_RASTER_SCANLINE_FLAT_SCREEN_U_C
#define TORIDRAW_RASTER_SCANLINE_FLAT_SCREEN_U_C

/**
 * Flat triangles, `scanline` walk.
 *
 * Variant IDs: `flat.screen.opaque.scanline.s8`, `flat.screen.alpha.scanline.s8`.
 *
 * Drop-in replacements for `raster_flat_screen_{opaque,alpha}_branching_s4`.
 * One y-sort instead of a six-way permutation dispatch, y-clipping folded into
 * the edge accumulators, and the horizontal clip decided once per trapezoid
 * segment rather than once per pixel run.
 */

#include "graphics/dash_restrict.h"
#include "graphics/raster/scanline/scanline_common.h"
#include "impl/raster/span/span.solid.scanline.scalar.u.c"
#include "graphics/shared_tables.h"

static inline void
raster_flat_screen_opaque_scanline_s8(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16)
{
    struct ScanlineTri tri;
    struct ScanlineEdges edges;

    if( !scanline_tri_build(&tri, x0, x1, x2, y0, y1, y2) )
        return;
    if( !scanline_edges_build(&edges, &tri, screen_height) )
        return;

    toripixel_t rgb_color = (toripixel_t)g_hsl16_to_rgb_table[color_hsl16];

    for( int seg = 0; seg < 2; seg++ )
    {
        int rows = edges.rows[seg];
        if( rows <= 0 )
            continue;

        int l_ish16 = edges.l_ish16[seg];
        int l_step = edges.l_step[seg];
        int r_ish16 = edges.r_ish16[seg];
        int r_step = edges.r_step[seg];
        int offset = edges.y0[seg] * stride;

        if( scanline_segment_no_hclip(l_ish16, l_step, r_ish16, r_step, rows, screen_width) )
        {
            while( rows-- > 0 )
            {
                int x_start = l_ish16 >> 16;
                int count = (r_ish16 >> 16) - x_start;
                if( count > 0 )
                    scanline_span_flat_opaque(pixel_buffer, offset + x_start, count, rgb_color);

                l_ish16 += l_step;
                r_ish16 += r_step;
                offset += stride;
            }
        }
        else
        {
            while( rows-- > 0 )
            {
                int x_start;
                int count = scanline_row_clip(l_ish16, r_ish16, screen_width, &x_start);
                if( count > 0 )
                    scanline_span_flat_opaque(pixel_buffer, offset + x_start, count, rgb_color);

                l_ish16 += l_step;
                r_ish16 += r_step;
                offset += stride;
            }
        }
    }
}

static inline void
raster_flat_screen_alpha_scanline_s8(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16,
    int alpha)
{
    struct ScanlineTri tri;
    struct ScanlineEdges edges;

    if( !scanline_tri_build(&tri, x0, x1, x2, y0, y1, y2) )
        return;
    if( !scanline_edges_build(&edges, &tri, screen_height) )
        return;

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

    for( int seg = 0; seg < 2; seg++ )
    {
        int rows = edges.rows[seg];
        if( rows <= 0 )
            continue;

        int l_ish16 = edges.l_ish16[seg];
        int l_step = edges.l_step[seg];
        int r_ish16 = edges.r_ish16[seg];
        int r_step = edges.r_step[seg];
        int offset = edges.y0[seg] * stride;

        if( scanline_segment_no_hclip(l_ish16, l_step, r_ish16, r_step, rows, screen_width) )
        {
            while( rows-- > 0 )
            {
                int x_start = l_ish16 >> 16;
                int count = (r_ish16 >> 16) - x_start;
                if( count > 0 )
                    scanline_span_flat_alpha(
                        pixel_buffer, offset + x_start, count, rgb_color, alpha);

                l_ish16 += l_step;
                r_ish16 += r_step;
                offset += stride;
            }
        }
        else
        {
            while( rows-- > 0 )
            {
                int x_start;
                int count = scanline_row_clip(l_ish16, r_ish16, screen_width, &x_start);
                if( count > 0 )
                    scanline_span_flat_alpha(
                        pixel_buffer, offset + x_start, count, rgb_color, alpha);

                l_ish16 += l_step;
                r_ish16 += r_step;
                offset += stride;
            }
        }
    }
}

#endif
