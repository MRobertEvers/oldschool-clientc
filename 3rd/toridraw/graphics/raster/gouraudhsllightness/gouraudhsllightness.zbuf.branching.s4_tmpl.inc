/*
 * Gouraud (HSL lightness) triangle, `branching` walk, depth-tested. Included
 * once per variant. The includer defines:
 *
 *   GZB_FN       triangle entry point name
 *   GZB_ORDERED  y-sorted helper name
 *   GZB_SPAN     scanline name
 *   GZB_ALPHA    1 = blend by the face alpha, test depth but never write it
 *
 * The walk is raster.gouraudhsllightness.{opaque,alpha}...painter.branching.s4
 * verbatim, and so is the colour: one palette lookup per FOUR pixels, phased
 * from x_start, exactly as toridraw_gouraud_span_fill_ref does. That blocking
 * is visible output the parity test pins against the reference client, so the
 * depth twin keeps it; only the depth test runs per pixel.
 *
 * The depth plane and the store rules are flat.zbuf.branching.s4_tmpl.inc's:
 * strictly nearer wins, and only the opaque variant writes depth.
 */

#include "graphics/alpha.h"
#include "graphics/int_wrap.h"
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness_barycentric_steps.h"
#include "graphics/raster/zbuffer/zbuf_plane.h"
#include "graphics/shared_tables.h"
#include "graphics/zdepth.h"

#ifndef GZB_ALPHA
#define GZB_ALPHA 0
#endif

#if GZB_ALPHA
#define GZB_ALPHA_PARAM , int alpha
#define GZB_ALPHA_ARG , alpha
#define GZB_STORE(at, rgb)                                                                         \
    (pixel_buffer[(at)] = alpha_blend(alpha, pixel_buffer[(at)], (rgb)))
#else
#define GZB_ALPHA_PARAM
#define GZB_ALPHA_ARG
#define GZB_STORE(at, rgb) (pixel_buffer[(at)] = (rgb), zbuffer[(at)] = (torizdepth_t)key)
#endif

/* One pixel: test, then the variant's store. `key` is the caller's. */
#define GZB_PIXEL(at, rgb)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if( key > (float)zbuffer[(at)] )                                                           \
            GZB_STORE((at), (rgb));                                                                \
        key += key_step;                                                                           \
    } while( 0 )

static inline void
GZB_SPAN(
    toripixel_t* RESTRICT pixel_buffer,
    torizdepth_t* RESTRICT zbuffer,
    int offset,
    int screen_width,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8 GZB_ALPHA_PARAM,
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

    offset += x_start;
    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    color_hsl16_ish8 =
        toridraw_wrap_add(color_hsl16_ish8, toridraw_wrap_mul(x_start, color_step_hsl16_ish8));

    float key = key_row + key_step * (float)x_start;
    int const count = x_end - x_start;
    int const color_step4 = toridraw_wrap_shl(color_step_hsl16_ish8, 2);
    int rgb;

    for( int steps = count >> 2; steps > 0; steps-- )
    {
        rgb = ToriDraw_Hsl16Ish8ToPixel(color_hsl16_ish8);
        GZB_PIXEL(offset + 0, rgb);
        GZB_PIXEL(offset + 1, rgb);
        GZB_PIXEL(offset + 2, rgb);
        GZB_PIXEL(offset + 3, rgb);
        offset += 4;
        color_hsl16_ish8 = toridraw_wrap_add(color_hsl16_ish8, color_step4);
    }

    rgb = ToriDraw_Hsl16Ish8ToPixel(color_hsl16_ish8);
    for( int tail = count & 0x3; tail > 0; tail-- )
    {
        GZB_PIXEL(offset, rgb);
        offset += 1;
    }
}

static inline void
GZB_ORDERED(
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
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16 GZB_ALPHA_PARAM,
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

    struct ToriDraw_ZbufPlane depth;
    if( !toridraw_zbuf_plane_solve(&depth, x0, y0, key_a, x1, y1, key_b, x2, y2, key_c) )
        return;

    int d_hsl_AB = color1_hsl16 - color0_hsl16;
    int d_hsl_AC = color2_hsl16 - color0_hsl16;

    gouraudhsllightness_recip_t recip_sarea = gouraudhsllightness_barycentric_recip(sarea);
    int step_x_hsl_ish8 = gouraudhsllightness_barycentric_hsl_step_ish8(
        d_hsl_AB * dy_AC - d_hsl_AC * dy_AB, recip_sarea);
    int step_y_hsl_ish8 = gouraudhsllightness_barycentric_hsl_step_ish8(
        d_hsl_AC * dx_AB - d_hsl_AB * dx_AC, recip_sarea);

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

    int hsl_ish8 = toridraw_wrap_sub(
        toridraw_wrap_add(step_x_hsl_ish8, color0_hsl16 << 8),
        toridraw_wrap_mul(x0, step_x_hsl_ish8));

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        /* Only pre-step A->B if that edge is walked; see the painter twin. */
        if( y1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        hsl_ish8 = toridraw_wrap_sub(hsl_ish8, toridraw_wrap_mul(step_y_hsl_ish8, y0));

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
            GZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AB_ish16, edge_x_AC_ish16,
                hsl_ish8, step_x_hsl_ish8 GZB_ALPHA_ARG, key_row, depth.step_dx);
        else
            GZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AC_ish16, edge_x_AB_ish16,
                hsl_ish8, step_x_hsl_ish8 GZB_ALPHA_ARG, key_row, depth.step_dx);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
        hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_hsl_ish8);
        offset += stride;
        key_row += depth.step_dy;
    }

    while( y2-- > 0 )
    {
        if( left_is_ab )
            GZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_BC_ish16, edge_x_AC_ish16,
                hsl_ish8, step_x_hsl_ish8 GZB_ALPHA_ARG, key_row, depth.step_dx);
        else
            GZB_SPAN(
                pixel_buffer, zbuffer, offset, screen_width, edge_x_AC_ish16, edge_x_BC_ish16,
                hsl_ish8, step_x_hsl_ish8 GZB_ALPHA_ARG, key_row, depth.step_dx);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
        hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_hsl_ish8);
        offset += stride;
        key_row += depth.step_dy;
    }
}

/* The painter twin's six-way y-sort, arm for arm; see the flat template. */
static inline void
GZB_FN(
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
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16 GZB_ALPHA_PARAM,
    float key_a,
    float key_b,
    float key_c)
{
#define GZB_ORDERED_CALL(xa, xb, xc, ya, yb, yc, ca, cb, cc, ka, kb, kc)                          \
    GZB_ORDERED(                                                                                   \
        pixel_buffer, zbuffer, stride, screen_width, screen_height, xa, xb, xc, ya, yb, yc, ca,    \
        cb, cc GZB_ALPHA_ARG, ka, kb, kc)

    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 > screen_height )
            return;
        if( y1 <= y2 )
        {
            if( y2 < 0 )
                return;
            GZB_ORDERED_CALL(
                x0, x1, x2, y0, y1, y2, color0_hsl16, color1_hsl16, color2_hsl16, key_a, key_b,
                key_c);
        }
        else
        {
            if( y1 < 0 )
                return;
            GZB_ORDERED_CALL(
                x0, x2, x1, y0, y2, y1, color0_hsl16, color2_hsl16, color1_hsl16, key_a, key_c,
                key_b);
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
            GZB_ORDERED_CALL(
                x1, x2, x0, y1, y2, y0, color1_hsl16, color2_hsl16, color0_hsl16, key_b, key_c,
                key_a);
        }
        else
        {
            if( y2 < 0 )
                return;
            GZB_ORDERED_CALL(
                x1, x0, x2, y1, y0, y2, color1_hsl16, color0_hsl16, color2_hsl16, key_b, key_a,
                key_c);
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
            GZB_ORDERED_CALL(
                x2, x0, x1, y2, y0, y1, color2_hsl16, color0_hsl16, color1_hsl16, key_c, key_a,
                key_b);
        }
        else
        {
            if( y0 < 0 )
                return;
            GZB_ORDERED_CALL(
                x2, x1, x0, y2, y1, y0, color2_hsl16, color1_hsl16, color0_hsl16, key_c, key_b,
                key_a);
        }
    }

#undef GZB_ORDERED_CALL
}

#undef GZB_PIXEL
#undef GZB_STORE
#undef GZB_ALPHA_PARAM
#undef GZB_ALPHA_ARG
#undef GZB_FN
#undef GZB_ORDERED
#undef GZB_SPAN
#undef GZB_ALPHA
