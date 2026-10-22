#ifndef RASTER_RSZBUF_TEXTURE_PERSPECTIVE_ZBUF_LERP8_U_C
#define RASTER_RSZBUF_TEXTURE_PERSPECTIVE_ZBUF_LERP8_U_C

/**
 * rszbuf.texture.perspective.zbuf.lerp8
 *
 * The rev-241 depth-buffered textured triangle the client draws MODELS with
 * [class162.method5170] and its span [method5352]: the texture coordinate is
 * re-derived from the planes every eight pixels and stepped linearly between,
 * u clamped into the texture, v left to wrap. Texels are 128 wide.
 */

#include "graphics/raster/rszbuf/rszbuf_raster.h"

/** u and v where the planes have reached, u clamped as the client clamps it. */
static inline void
rszbuf_texture_sample_point(int u_plane, int v_plane, int w_plane, int* u, int* v)
{
    int const w = w_plane >> 14;

    if( w == 0 )
    {
        *u = 0;
        *v = 0;
        return;
    }
    *u = rszbuf_java_div(u_plane, w);
    *v = rszbuf_java_div(v_plane, w);
    if( *u < 0 )
        *u = 0;
    else if( *u > 16256 )
        *u = 16256;
}

/** The packed (u << 18 | v) step across eight pixels from one point to the next. */
static inline int32_t
rszbuf_texture_uv_step8(int u_from, int v_from, int u_to, int v_to)
{
    return toridraw_wrap_add(
        toridraw_wrap_shl(toridraw_wrap_sub(u_to, u_from) >> 3, 18),
        toridraw_wrap_sub(v_to, v_from) >> 3);
}

/**
 * One textured scanline. The face alpha and the texture's colour key pick the
 * store; with any alpha the lightness is taken once for the whole span, which
 * is the client's behaviour, not a simplification.
 */
static inline void
rszbuf_span_texture(
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
    int u_step8_x,
    int v_step8_x,
    int w_step8_x)
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

    int u_plane = toridraw_wrap_add(toridraw_wrap_mul(u_step8_x >> 3, column), u_at_row);
    int v_plane = toridraw_wrap_add(toridraw_wrap_mul(v_step8_x >> 3, column), v_at_row);
    int w_plane = toridraw_wrap_add(toridraw_wrap_mul(w_step8_x >> 3, column), w_at_row);
    int u_start;
    int v_start;
    int u_end;
    int v_end;

    rszbuf_texture_sample_point(u_plane, v_plane, w_plane, &u_start, &v_start);
    u_plane = toridraw_wrap_add(u_step8_x, u_plane);
    v_plane = toridraw_wrap_add(v_step8_x, v_plane);
    w_plane = toridraw_wrap_add(w_step8_x, w_plane);
    rszbuf_texture_sample_point(u_plane, v_plane, w_plane, &u_end, &v_end);

    int32_t uv = toridraw_wrap_add(toridraw_wrap_shl(u_start, 18), v_start);
    int32_t uv_step = rszbuf_texture_uv_step8(u_start, v_start, u_end, v_end);
    int blocks = count >> 3;
    int const shade_step8 = toridraw_wrap_shl(shade_step_x, 3);
    int block_shade = shade >> 8;
    int const alpha = r->face_alpha;
    bool const keyed = !r->texture_opaque;
    /* With alpha the source is pre-weighted once, from the first block. */
    int const blend_shade = toridraw_wrap_mul(block_shade, 256 - alpha) >> 8;

#define RSZBUF_TEXEL_STORE()                                                                       \
    do                                                                                             \
    {                                                                                              \
        if( alpha != 0 )                                                                           \
            rszbuf_store_texel_blend(r, texels, at, alpha, blend_shade, uv, key, keyed);           \
        else if( keyed )                                                                           \
            rszbuf_store_texel_keyed(r, texels, at, block_shade, uv, key);                         \
        else                                                                                       \
            rszbuf_store_texel(r, texels, at, block_shade, uv, key);                               \
    } while( 0 )

    while( blocks-- > 0 )
    {
        for( int i = 0; i < 8; i++ )
        {
            RSZBUF_TEXEL_STORE();
            at++;
            uv = toridraw_wrap_add(uv, uv_step);
            key += key_step_x;
        }

        /* The next block starts where this one's end point was taken. */
        u_start = u_end;
        v_start = v_end;
        u_plane = toridraw_wrap_add(u_plane, u_step8_x);
        v_plane = toridraw_wrap_add(v_plane, v_step8_x);
        w_plane = toridraw_wrap_add(w_plane, w_step8_x);
        rszbuf_texture_sample_point(u_plane, v_plane, w_plane, &u_end, &v_end);
        uv = toridraw_wrap_add(toridraw_wrap_shl(u_start, 18), v_start);
        uv_step = rszbuf_texture_uv_step8(u_start, v_start, u_end, v_end);
        shade = toridraw_wrap_add(shade, shade_step8);
        block_shade = shade >> 8;
    }

    for( int tail = count & 0x7; tail-- > 0; key += key_step_x )
    {
        RSZBUF_TEXEL_STORE();
        at++;
        uv = toridraw_wrap_add(uv, uv_step);
    }

#undef RSZBUF_TEXEL_STORE
}

#define RTX_FN rszbuf_triangle_texture
#define RTX_SPAN rszbuf_span_texture
#define RTX_X_STEP_SHIFT 3
#include "graphics/raster/rszbuf/rszbuf.texture_triangle_tmpl.inc"

#endif
