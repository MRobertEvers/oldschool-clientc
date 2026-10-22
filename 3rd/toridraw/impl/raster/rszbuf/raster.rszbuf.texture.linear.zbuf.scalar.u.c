#ifndef RASTER_RSZBUF_TEXTURE_LINEAR_ZBUF_U_C
#define RASTER_RSZBUF_TEXTURE_LINEAR_ZBUF_U_C

/**
 * rszbuf.texture.linear.zbuf
 *
 * The rev-241 depth-buffered textured triangle the client draws TERRAIN with
 * [class162.method5171] and its span [method5354]: one divide at each end of
 * the span and a straight line between -- no per-block refit, no u clamp, and
 * no face alpha, which terrain never has. Texels are 128 wide.
 */

#include "graphics/raster/rszbuf/rszbuf_raster.h"

static inline void
rszbuf_span_texture_linear(
    struct ToriDraw_RsZbufRaster* r,
    const int* RESTRICT texels,
    int row,
    int x_start,
    int x_end,
    int shade_at_x0,
    int shade_step_x,
    float key_at_x0,
    float key_step_x,
    int u_at_row,
    int v_at_row,
    int w_at_row,
    int u_step_x,
    int v_step_x,
    int w_step_x)
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
    int shade = toridraw_wrap_add(toridraw_wrap_mul(x_start, shade_step_x), shade_at_x0);
    float key = (float)x_start * key_step_x + key_at_x0;
    int const count = x_end - x_start;
    int const column = x_start - r->center_x;

    int const u_plane = toridraw_wrap_add(toridraw_wrap_mul(u_step_x, column), u_at_row);
    int const v_plane = toridraw_wrap_add(toridraw_wrap_mul(v_step_x, column), v_at_row);
    int const w_plane = toridraw_wrap_add(toridraw_wrap_mul(w_step_x, column), w_at_row);
    int const w_start = w_plane >> 14;
    int const u_start = w_start == 0 ? 0 : rszbuf_java_div(u_plane, w_start);
    int const v_start = w_start == 0 ? 0 : rszbuf_java_div(v_plane, w_start);

    int const u_plane_end = toridraw_wrap_add(toridraw_wrap_mul(u_step_x, count), u_plane);
    int const v_plane_end = toridraw_wrap_add(toridraw_wrap_mul(v_step_x, count), v_plane);
    int const w_plane_end = toridraw_wrap_add(toridraw_wrap_mul(w_step_x, count), w_plane);
    int const w_end = w_plane_end >> 14;
    int const u_end = w_end == 0 ? 0 : rszbuf_java_div(u_plane_end, w_end);
    int const v_end = w_end == 0 ? 0 : rszbuf_java_div(v_plane_end, w_end);

    int32_t uv = toridraw_wrap_add(toridraw_wrap_shl(u_start, 18), v_start);
    int32_t const uv_step = toridraw_wrap_add(
        toridraw_wrap_shl(rszbuf_java_div(toridraw_wrap_sub(u_end, u_start), count), 18),
        rszbuf_java_div(toridraw_wrap_sub(v_end, v_start), count));
    int blocks = count >> 3;
    int const shade_step8 = toridraw_wrap_shl(shade_step_x, 3);
    int block_shade = shade >> 8;
    bool const keyed = !r->texture_opaque;

    while( blocks-- > 0 )
    {
        for( int i = 0; i < 8; i++ )
        {
            if( keyed )
                rszbuf_store_texel_keyed(r, texels, at, block_shade, uv, key);
            else
                rszbuf_store_texel(r, texels, at, block_shade, uv, key);
            at++;
            uv = toridraw_wrap_add(uv, uv_step);
            key += key_step_x;
        }
        shade = toridraw_wrap_add(shade, shade_step8);
        block_shade = shade >> 8;
    }

    for( int tail = count & 0x7; tail-- > 0; key += key_step_x )
    {
        if( keyed )
            rszbuf_store_texel_keyed(r, texels, at, block_shade, uv, key);
        else
            rszbuf_store_texel(r, texels, at, block_shade, uv, key);
        at++;
        uv = toridraw_wrap_add(uv, uv_step);
    }
}

#define RTX_FN rszbuf_triangle_texture_linear
#define RTX_SPAN rszbuf_span_texture_linear
#define RTX_X_STEP_SHIFT 0
#include "graphics/raster/rszbuf/rszbuf.texture_triangle_tmpl.inc"

#endif
