#ifndef RASTER_RSZBUF_GOURAUD_ZBUF_U_C
#define RASTER_RSZBUF_GOURAUD_ZBUF_U_C

/**
 * rszbuf.gouraud.zbuf
 *
 * The rev-241 depth-buffered gouraud triangle [class162.method5165] and its span
 * [method5375]. The three colours are hsl16; the span looks each up in the
 * palette, once per pixel or, at low detail, once per four.
 */

#include "graphics/raster/rszbuf/rszbuf_raster.h"

static inline int32_t
rszbuf_palette_color(const struct ToriDraw_RsZbufRaster* r, float hsl)
{
    return (int32_t)r->palette[rszbuf_java_int(hsl) & 0xFFFF];
}

static inline void
rszbuf_span_gouraud(
    struct ToriDraw_RsZbufRaster* r,
    int row,
    int x_start,
    int x_end,
    float color_at_x0,
    float color_step_x,
    float key_at_x0,
    float key_step_x)
{
    RSZBUF_NO_FMA

    if( r->clip_x )
    {
        if( x_end > r->clip_width )
            x_end = r->clip_width;
        if( x_start < 0 )
            x_start = 0;
    }
    if( x_start >= x_end )
        return;

    int at = row + x_start;
    float color = (float)x_start * color_step_x + color_at_x0;
    float key = (float)x_start * key_step_x + key_at_x0;
    int const dest_weight = r->face_alpha;
    int const source_weight = 256 - r->face_alpha;

    if( !r->gouraud_blocks4 )
    {
        int count = x_end - x_start;

        do
        {
            if( r->face_alpha == 0 )
                rszbuf_store(r, at, rszbuf_palette_color(r, color), key);
            else
                rszbuf_store_blend(
                    r, at, rszbuf_scale_rgb(rszbuf_palette_color(r, color), source_weight),
                    dest_weight, key);
            at++;
            key += key_step_x;
            color += color_step_x;
        } while( --count > 0 );
        return;
    }

    int blocks = (x_end - x_start) >> 2;
    int tail = (x_end - x_start) & 0x3;
    float const color_step4 = color_step_x * 4.0f;
    int32_t rgb;

    if( r->face_alpha == 0 )
    {
        if( blocks > 0 )
        {
            do
            {
                rgb = rszbuf_palette_color(r, color);
                color += color_step4;
                for( int i = 0; i < 4; i++, at++, key += key_step_x )
                    rszbuf_store(r, at, rgb, key);
            } while( --blocks > 0 );
        }
        if( tail > 0 )
        {
            rgb = rszbuf_palette_color(r, color);
            do
            {
                rszbuf_store(r, at, rgb, key);
                at++;
                key += key_step_x;
            } while( --tail > 0 );
        }
        return;
    }

    if( blocks > 0 )
    {
        do
        {
            rgb = rszbuf_scale_rgb(rszbuf_palette_color(r, color), source_weight);
            color += color_step4;
            for( int i = 0; i < 4; i++, at++, key += key_step_x )
                rszbuf_store_blend(r, at, rgb, dest_weight, key);
        } while( --blocks > 0 );
    }
    if( tail <= 0 )
        return;
    rgb = rszbuf_scale_rgb(rszbuf_palette_color(r, color), source_weight);
    do
    {
        rszbuf_store_blend(r, at, rgb, dest_weight, key);
        at++;
        key += key_step_x;
    } while( --tail > 0 );
}

#define RZT_FN rszbuf_triangle_gouraud
#define RZT_PARAMS , int color_a, int color_b, int color_c
/* A second degeneracy test, on the rounded corners, before the depth plane's
 * on the exact ones: the colour gradients are solved over whole pixels. */
#define RZT_SETUP                                                                                  \
    float const area_px = dx_ab_px * dy_ac_px - dy_ab_px * dx_ac_px;                               \
    if( area_px == 0.0f )                                                                          \
        return;                                                                                    \
    float const color_vertex[3] = { (float)color_a, (float)color_b, (float)color_c };             \
    float const dcolor_ab = color_vertex[1] - color_vertex[0];                                     \
    float const dcolor_ac = color_vertex[2] - color_vertex[0];                                     \
    float const color_step_x = (dy_ac_px * dcolor_ab - dy_ab_px * dcolor_ac) / area_px;            \
    float const color_step_y = (dx_ab_px * dcolor_ac - dx_ac_px * dcolor_ab) / area_px;
#define RZT_ROW_DECLS float color_row;
#define RZT_ROW_START(vertex, x_top)                                                               \
    (color_row = color_vertex[(vertex)] - (x_top) * color_step_x + color_step_x)
#define RZT_ROW_PRESTEP(y_top) (color_row -= (y_top) * color_step_y)
#define RZT_ROWS_BEGIN(y_top) ((void)0)
#define RZT_SPAN(row, left, right)                                                                 \
    rszbuf_span_gouraud(r, (row), (left), (right), color_row, color_step_x, key_row, key_step_x)
#define RZT_ROW_ADVANCE() (color_row += color_step_y)
#include "graphics/raster/rszbuf/rszbuf.triangle_tmpl.inc"

#endif
