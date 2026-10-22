/*
 * Flat triangle, `branching` walk, depth-tested. Included once per variant. The
 * includer defines:
 *
 *   FZB_FN       triangle entry point name
 *   FZB_ORDERED  y-sorted helper name
 *   FZB_SPAN     scanline name
 *   FZB_ALPHA    1 = blend by the face alpha, test depth but never write it
 *
 * The walk is raster.flat.{opaque,alpha}...painter.branching.s4 verbatim: the
 * same edge steps, the same span clamps, the same six-way y-sort and rejects.
 * A depth-tested face has to land on exactly the pixels its painter twin
 * lands on, or switching a model to the depth buffer would move its
 * silhouette; the depth test only decides which of those pixels survive.
 *
 * Two things are added and nothing else:
 *
 *   - the depth plane, solved once from the corners (graphics/raster/zbuffer/
 *     zbuf_plane.h) and stepped one add per pixel and one per row;
 *   - a compare before each store, and for the opaque variant a depth store
 *     after it. Strictly nearer wins: an equal key keeps what is there, which
 *     is what stops two faces sharing an edge from fighting over it.
 *
 * The painter's no-clip span split is not carried over. It is a proof that the
 * clamps cannot fire, so dropping it changes no pixel, and the depth span's
 * per-pixel compare is the cost that matters here.
 */

#include "graphics/alpha.h"
#include "graphics/raster/zbuffer/zbuf_plane.h"
#include "graphics/zdepth.h"

#ifndef FZB_ALPHA
#define FZB_ALPHA 0
#endif

#if FZB_ALPHA
#define FZB_ALPHA_PARAM , int alpha
#define FZB_ALPHA_ARG , alpha
#else
#define FZB_ALPHA_PARAM
#define FZB_ALPHA_ARG
#endif

static inline void
FZB_SPAN(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int offset,
    int screen_width,
    int x_start_ish16,
    int x_end_ish16,
    int rgb_color FZB_ALPHA_PARAM,
    float key_row,
    float key_step)
{
    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_end = x_end_ish16 >> 16;
    if( x_end >= screen_width )
        x_end = screen_width - 1;

    int x_start = x_start_ish16 >> 16;
    if( x_start < 0 )
        x_start = 0;

    if( x_start >= x_end )
        return;

    float key = key_row + key_step * (float)x_start;
    offset += x_start;

    for( int count = x_end - x_start; count > 0; count-- )
    {
        if( key > (float)zbuffer[offset] )
        {
#if FZB_ALPHA
            pixel_buffer[offset] = alpha_blend(alpha, pixel_buffer[offset], rgb_color);
#else
            pixel_buffer[offset] = rgb_color;
            zbuffer[offset] = (torizdepth_t)key;
#endif
        }
        offset += 1;
        key += key_step;
    }
}

static inline void
FZB_ORDERED(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16 FZB_ALPHA_PARAM,
    float key_a,
    float key_b,
    float key_c)
{
    if( y2 - y0 == 0 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int sarea = dx_AB * dy_AC - dx_AC * dy_AB;
    if( sarea == 0 )
        return;

    /* Solved from the corners as they arrived, before the y clip moves y0: the
     * plane is in absolute screen coordinates and each row reads its own key. */
    struct ToriDraw_ZbufPlane depth;
    if( !toridraw_zbuf_plane_solve(&depth, x0, y0, key_a, x1, y1, key_b, x2, y2, key_c) )
        return;

    int rgb_color = g_hsl16_to_pixel_table[color_hsl16];

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( y2 != y1 )
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        /* Only pre-step A->B if that edge is walked; see the painter twin. */
        if( y1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    int offset = y0 * stride;
    float key_row = toridraw_zbuf_plane_row(&depth, y0);

    if( y1 > screen_height )
    {
        y1 = screen_height;
        y2 = screen_height;
    }
    else if( y2 > screen_height )
    {
        y2 = screen_height;
    }

    int left_is_ab = (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
                     (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16);

    y2 -= y1;
    y1 -= y0;

    while( y1-- > 0 )
    {
        if( left_is_ab )
            FZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AB_ish16, edge_x_AC_ish16,
                rgb_color FZB_ALPHA_ARG, key_row, depth.step_dx);
        else
            FZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AC_ish16, edge_x_AB_ish16,
                rgb_color FZB_ALPHA_ARG, key_row, depth.step_dx);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
        offset += stride;
        key_row += depth.step_dy;
    }

    while( y2-- > 0 )
    {
        if( left_is_ab )
            FZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_BC_ish16, edge_x_AC_ish16,
                rgb_color FZB_ALPHA_ARG, key_row, depth.step_dx);
        else
            FZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AC_ish16, edge_x_BC_ish16,
                rgb_color FZB_ALPHA_ARG, key_row, depth.step_dx);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
        offset += stride;
        key_row += depth.step_dy;
    }
}

/* The painter twin's six-way y-sort, arm for arm: which corner becomes A when
 * two share a y decides the edges, so it is copied rather than re-derived. The
 * keys move with their corners. */
static inline void
FZB_FN(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16 FZB_ALPHA_PARAM,
    float key_a,
    float key_b,
    float key_c)
{
#define FZB_ORDERED_CALL(xa, xb, xc, ya, yb, yc, ka, kb, kc)                                      \
    FZB_ORDERED(                                                                                   \
        pixel_buffer, zbuffer, stride, screen_width, screen_height, xa, xb, xc, ya, yb, yc,        \
        color_hsl16 FZB_ALPHA_ARG, ka, kb, kc)

    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 > screen_height )
            return;
        if( y1 <= y2 )
        {
            if( y2 < 0 )
                return;
            FZB_ORDERED_CALL(x0, x1, x2, y0, y1, y2, key_a, key_b, key_c);
        }
        else
        {
            if( y1 < 0 )
                return;
            FZB_ORDERED_CALL(x0, x2, x1, y0, y2, y1, key_a, key_c, key_b);
        }
    }
    else if( y1 <= y2 )
    {
        if( y1 > screen_height )
            return;
        if( y2 <= y0 )
        {
            if( y0 < 0 )
                return;
            FZB_ORDERED_CALL(x1, x2, x0, y1, y2, y0, key_b, key_c, key_a);
        }
        else
        {
            if( y2 < 0 )
                return;
            FZB_ORDERED_CALL(x1, x0, x2, y1, y0, y2, key_b, key_a, key_c);
        }
    }
    else
    {
        if( y2 > screen_height )
            return;
        if( y0 <= y1 )
        {
            if( y1 < 0 )
                return;
            FZB_ORDERED_CALL(x2, x0, x1, y2, y0, y1, key_c, key_a, key_b);
        }
        else
        {
            if( y0 < 0 )
                return;
            FZB_ORDERED_CALL(x2, x1, x0, y2, y1, y0, key_c, key_b, key_a);
        }
    }

#undef FZB_ORDERED_CALL
}

#undef FZB_ALPHA_PARAM
#undef FZB_ALPHA_ARG
#undef FZB_FN
#undef FZB_ORDERED
#undef FZB_SPAN
#undef FZB_ALPHA
