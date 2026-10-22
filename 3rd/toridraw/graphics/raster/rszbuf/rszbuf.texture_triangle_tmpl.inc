/*
 * The rev-241 textured triangle [class162.method5170 / method5171], included
 * once per span. The includer defines:
 *
 *   RTX_FN             entry point name
 *   RTX_SPAN           the scanline (rszbuf_span_texture or ..._linear)
 *   RTX_X_STEP_SHIFT   3 when the span walks eight pixels per plane step, 0
 *                      when it walks one
 *
 * Over and above the walk it carries a lightness (0..127 in, 8.8 fixed point
 * down the rows, stepped as an int and so wrapping as the client's does) and
 * the three texture planes, which are anchored at the projection centre and so
 * can only be read once the top row has been clipped.
 */

#include "graphics/raster/rszbuf/rszbuf_texture_planes.h"

#define RZT_FN RTX_FN
#define RZT_PARAMS                                                                                 \
    , int shade_a, int shade_b, int shade_c,                                                       \
        const struct ToriDraw_RsZbufTextureFrame* frame, const int* RESTRICT texels,               \
        bool texture_opaque
#define RZT_SETUP                                                                                  \
    assert(texels);                                                                                \
    r->texture_opaque = texture_opaque;                                                            \
    float const area_px = dx_ab_px * dy_ac_px - dy_ab_px * dx_ac_px;                               \
    if( area_px == 0.0f )                                                                          \
        return;                                                                                    \
    int const shade_vertex[3] = { shade_a, shade_b, shade_c };                                     \
    float const dshade_ab = (float)(shade_b - shade_a);                                            \
    float const dshade_ac = (float)(shade_c - shade_a);                                            \
    int const shade_step_x =                                                                       \
        rszbuf_java_int((dy_ac_px * dshade_ab - dy_ab_px * dshade_ac) * 512.0f / area_px);         \
    int const shade_step_y =                                                                       \
        rszbuf_java_int((dx_ab_px * dshade_ac - dx_ac_px * dshade_ab) * 512.0f / area_px);         \
    struct ToriDraw_RsZbufTexturePlanes planes;                                                    \
    rszbuf_texture_planes_solve(&planes, frame, r->projection_scale, RTX_X_STEP_SHIFT);
#define RZT_ROW_DECLS                                                                              \
    int shade_row = 0;                                                                             \
    int u_row = 0;                                                                                 \
    int v_row = 0;                                                                                 \
    int w_row = 0;
#define RZT_ROW_START(vertex, x_top)                                                               \
    (shade_row = toridraw_wrap_add(                                                                \
         toridraw_wrap_sub(                                                                        \
             toridraw_wrap_shl(shade_vertex[(vertex)], 9),                                         \
             toridraw_wrap_mul(rszbuf_java_int(x_top), shade_step_x)),                             \
         shade_step_x))
#define RZT_ROW_PRESTEP(y_top)                                                                     \
    (shade_row = toridraw_wrap_sub(shade_row, toridraw_wrap_mul(rszbuf_java_int(y_top), shade_step_y)))
#define RZT_ROWS_BEGIN(y_top)                                                                      \
    do                                                                                             \
    {                                                                                              \
        int const centre_row = rszbuf_java_int(y_top) - r->center_y;                               \
        u_row = rszbuf_plane_at_row(&planes.u, centre_row);                                        \
        v_row = rszbuf_plane_at_row(&planes.v, centre_row);                                        \
        w_row = rszbuf_plane_at_row(&planes.w, centre_row);                                        \
    } while( 0 )
#define RZT_SPAN(row, left, right)                                                                 \
    RTX_SPAN(                                                                                      \
        r, texels, (row), (left), (right), shade_row, shade_step_x, key_row, key_step_x, u_row,    \
        v_row, w_row, planes.u.step_x, planes.v.step_x, planes.w.step_x)
#define RZT_ROW_ADVANCE()                                                                          \
    (shade_row = toridraw_wrap_add(shade_row, shade_step_y),                                       \
     u_row = toridraw_wrap_add(u_row, planes.u.step_y),                                            \
     v_row = toridraw_wrap_add(v_row, planes.v.step_y),                                            \
     w_row = toridraw_wrap_add(w_row, planes.w.step_y))
#include "graphics/raster/rszbuf/rszbuf.triangle_tmpl.inc"

#undef RTX_FN
#undef RTX_SPAN
#undef RTX_X_STEP_SHIFT
