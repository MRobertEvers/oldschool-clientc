#ifndef RASTER_RSZBUF_FLAT_ZBUF_U_C
#define RASTER_RSZBUF_FLAT_ZBUF_U_C

/**
 * rszbuf.flat.zbuf
 *
 * The rev-241 depth-buffered flat triangle [class162.method5169] and its span
 * [method5350]. `rgb` is already a pixel -- the caller resolved the hsl16
 * colour through the palette, as the client's model draw does before it calls.
 */

#include "graphics/raster/rszbuf/rszbuf_raster.h"

/**
 * One flat scanline. The face alpha picks one of three stores: 0 is opaque;
 * 254 copies each pixel's right-hand neighbour onto it (the client's smear,
 * which is why it refuses a span touching either edge of the viewport); any
 * other value blends.
 */
static inline void
rszbuf_span_flat(
    struct ToriDraw_RsZbufRaster* r,
    int row,
    int32_t rgb,
    int x_start,
    int x_end,
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
    int count = x_end - x_start;
    float key = (float)x_start * key_step_x + key_at_x0;

    if( r->face_alpha == 0 )
    {
        for( ; count > 0; count--, at++, key += key_step_x )
            rszbuf_store(r, at, rgb, key);
        return;
    }

    if( r->face_alpha == 254 )
    {
        if( x_start == 0 || x_end > r->clip_width - 1 )
            return;
        for( ; count > 0; count--, at++, key += key_step_x )
            rszbuf_store_blend(r, at - 1, (int32_t)r->pixels[at], 0, key);
        return;
    }

    {
        int const dest_weight = r->face_alpha;
        int32_t const source = rszbuf_scale_rgb(rgb, 256 - r->face_alpha);

        for( ; count > 0; count--, at++, key += key_step_x )
            rszbuf_store_blend(r, at, source, dest_weight, key);
    }
}

#define RZT_FN rszbuf_triangle_flat
#define RZT_PARAMS , int32_t rgb
#define RZT_SETUP
#define RZT_ROW_START(vertex, x_top) ((void)0)
#define RZT_ROW_PRESTEP(y_top) ((void)0)
#define RZT_ROWS_BEGIN(y_top) ((void)0)
#define RZT_SPAN(row, left, right) \
    rszbuf_span_flat(r, (row), rgb, (left), (right), key_row, key_step_x)
#define RZT_ROW_ADVANCE() ((void)0)
#include "graphics/raster/rszbuf/rszbuf.triangle_tmpl.inc"

#endif
