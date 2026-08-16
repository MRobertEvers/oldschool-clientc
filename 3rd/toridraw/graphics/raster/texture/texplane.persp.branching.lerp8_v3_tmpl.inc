/*
 * Perspective textured triangle with a shade plane, `branching` walk, over the
 * extended capability matrix. Included once per variant. The includer defines:
 *
 *   TSFA_FN       triangle entry point name
 *   TSFA_ORDERED  y-sorted helper name
 *   TSFA_SPAN     scanline kernel from span/tex.span.tex2.u.c
 *   TSFA_ZBUF     1 = depth-tested variant (optional; absent means 0)
 *
 * The walk, the uv basis and the shade plane are
 * texshadeblend.persp.{texopaque,textrans}.branching.lerp8_v3 verbatim. Every
 * capability that varies — the texel gate, per-face alpha, the face-colour
 * tint, per-axis addressing — reaches the span through one
 * `struct ToriDraw_TexSampler` instead of a growing argument list, so this
 * template is shared by all ten variants and the argument order never changes
 * when a capability is added.
 *
 * Keeping the walk identical to the plain kernels is deliberate: a variant is
 * meant to land on exactly the same pixels as its plain twin, so any divergence
 * here would show up as a seam between an alpha face and the opaque faces it
 * shares edges with. That invariant is what the coverage test pins.
 *
 * A TSFA_ZBUF variant takes three more corner values — the depth KEYS its caller
 * derived (graphics/zdepth.h) — and a z-buffer, and changes nothing else about
 * the walk. The keys permute with x/y/shade through the six-way dispatch, for
 * the same reason the shades do: they are per corner, unlike the uv basis, which
 * is anchored to the p/m/n vertices the projector named and must not move.
 */

#include "graphics/int_wrap.h"

#ifndef TSFA_ZBUF
#define TSFA_ZBUF 0
#endif

#if TSFA_ZBUF
#define TSFA_ZKEY_PARAMS , float key_a, float key_b, float key_c, torizdepth_t* RESTRICT zbuffer
#define TSFA_ZKEY_ARGS(ka, kb, kc) , (ka), (kb), (kc), zbuffer
#define TSFA_ZROW_ARGS , zbuffer, key_row, depth.step_dx
#define TSFA_ZADVANCE() (key_row += depth.step_dy)
#else
#define TSFA_ZKEY_PARAMS
#define TSFA_ZKEY_ARGS(ka, kb, kc)
#define TSFA_ZROW_ARGS
#define TSFA_ZADVANCE() ((void)0)
#endif

static inline void
TSFA_ORDERED(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    const struct ToriDraw_TexSampler* RESTRICT sampler TSFA_ZKEY_PARAMS)
{
    if( y0 > screen_height )
        return;

    int dy_AC = y2 - y0;
    int dy_AB = y1 - y0;

    int dx_AC = x2 - x0;
    int dx_AB = x1 - x0;

    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

#if TSFA_ZBUF
    /* Solved from the corners as they arrived, before the y clip moves y0: the
     * plane is stated in absolute screen coordinates and the row key is read out
     * of it at whichever row the walk actually starts on. */
    struct ToriDraw_ZbufPlane depth;
    if( !toridraw_zbuf_plane_solve(
            &depth, x0, y0, key_a, x1, y1, key_b, x2, y2, key_c) )
        return;
#endif

    int dy_BC = y2 - y1;
    int dx_BC = x2 - x1;

    int dblend7bit_ab = shade7bit_b - shade7bit_a;
    int dblend7bit_ac = shade7bit_c - shade7bit_a;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    /* Assumes the world coordinates differ from uv coordinates only by scale. */
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    int vUVPlane_normal_xhat = vU_z * vV_y - vU_y * vV_z;
    int vUVPlane_normal_yhat = vU_x * vV_z - vU_z * vV_x;
    int vUVPlane_normal_zhat = vU_y * vV_x - vU_x * vV_y;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;
    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    struct ToriDraw_TexturePlane32 texture_plane = {
        .term = {
            { vOVPlane_normal_xhat, vOVPlane_normal_yhat, vOVPlane_normal_zhat, 0 },
            { vUOPlane_normal_xhat, vUOPlane_normal_yhat, vUOPlane_normal_zhat, 0 },
            { vUVPlane_normal_xhat, vUVPlane_normal_yhat, vUVPlane_normal_zhat, 0 },
        },
    };
    if( !ToriDraw_TexturePlanePrepare32(
            &texture_plane, screen_width, screen_height, camera_cot16) )
        return;
    vOVPlane_normal_xhat = texture_plane.term[0].x;
    vOVPlane_normal_yhat = texture_plane.term[0].y;
    vUOPlane_normal_xhat = texture_plane.term[1].x;
    vUOPlane_normal_yhat = texture_plane.term[1].y;
    vUVPlane_normal_xhat = texture_plane.term[2].x;
    vUVPlane_normal_yhat = texture_plane.term[2].y;

    /* Shades arrive 0-127; shift up by 1, then by 8, so the accumulator is an
     * 8.8 value over 0-255. Cramer's rule, as for the uv planes. */
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    int shade8bit_edge_ish8 = toridraw_wrap_add(
        toridraw_wrap_sub(shade7bit_a << 9, toridraw_wrap_mul(shade8bit_xhat_ish8, x0)),
        shade8bit_xhat_ish8);

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        /* Only pre-step A->B if that edge is walked - see the identical note in
         * the plain v3 kernel and the full argument in zbuf.screen.u.c. */
        if( y1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;
        shade8bit_edge_ish8 = toridraw_wrap_sub(
            shade8bit_edge_ish8, toridraw_wrap_mul(shade8bit_yhat_ish8, y0));

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    au = texture_plane.term[0].base;
    bv = texture_plane.term[1].base;
    cw = texture_plane.term[2].base;

    int dy = y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int offset = y0 * stride;

#if TSFA_ZBUF
    float key_row = toridraw_zbuf_plane_row(&depth, y0);
#endif

    if( y1 > screen_height )
    {
        y1 = screen_height;
        y2 = screen_height;
    }
    else if( y2 > screen_height )
    {
        y2 = screen_height;
    }

#define TSFA_ROW(left, right)                                                                      \
    do                                                                                             \
    {                                                                                              \
        TSFA_SPAN(                                                                                 \
            pixel_buffer,                                                                          \
            screen_width,                                                                          \
            (left),                                                                                \
            (right),                                                                               \
            offset,                                                                                \
            au,                                                                                    \
            bv,                                                                                    \
            cw,                                                                                    \
            vOVPlane_normal_xhat,                                                                  \
            vUOPlane_normal_xhat,                                                                  \
            vUVPlane_normal_xhat,                                                                  \
            shade8bit_edge_ish8,                                                                   \
            shade8bit_xhat_ish8,                                                                   \
            sampler TSFA_ZROW_ARGS);                                                               \
    } while( 0 )

#define TSFA_ADVANCE()                                                                             \
    do                                                                                             \
    {                                                                                              \
        au += vOVPlane_normal_yhat;                                                                \
        bv += vUOPlane_normal_yhat;                                                                \
        cw += vUVPlane_normal_yhat;                                                                \
        shade8bit_edge_ish8 = toridraw_wrap_add(shade8bit_edge_ish8, shade8bit_yhat_ish8);         \
        offset += stride;                                                                          \
        TSFA_ZADVANCE();                                                                           \
    } while( 0 )

    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            TSFA_ROW(edge_x_AB_ish16, edge_x_AC_ish16);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            TSFA_ADVANCE();
        }

        while( y2-- > 0 )
        {
            TSFA_ROW(edge_x_BC_ish16, edge_x_AC_ish16);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            TSFA_ADVANCE();
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            TSFA_ROW(edge_x_AC_ish16, edge_x_AB_ish16);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            TSFA_ADVANCE();
        }

        while( y2-- > 0 )
        {
            TSFA_ROW(edge_x_AC_ish16, edge_x_BC_ish16);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            TSFA_ADVANCE();
        }
    }

#undef TSFA_ROW
#undef TSFA_ADVANCE
}

static inline void
TSFA_FN(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    const struct ToriDraw_TexSampler* RESTRICT sampler TSFA_ZKEY_PARAMS)
{
#define TSFA_ORDERED_CALL(xa, xb, xc, ya, yb, yc, sa, sb, sc, ka, kb, kc)                          \
    TSFA_ORDERED(                                                                                  \
        pixel_buffer,                                                                              \
        stride,                                                                                    \
        screen_width,                                                                              \
        screen_height,                                                                             \
        camera_cot16,                                                                              \
        (xa),                                                                                      \
        (xb),                                                                                      \
        (xc),                                                                                      \
        (ya),                                                                                      \
        (yb),                                                                                      \
        (yc),                                                                                      \
        orthographic_uvorigin_x0,                                                                  \
        orthographic_uend_x1,                                                                      \
        orthographic_vend_x2,                                                                      \
        orthographic_uvorigin_y0,                                                                  \
        orthographic_uend_y1,                                                                      \
        orthographic_vend_y2,                                                                      \
        orthographic_uvorigin_z0,                                                                  \
        orthographic_uend_z1,                                                                      \
        orthographic_vend_z2,                                                                      \
        (sa),                                                                                      \
        (sb),                                                                                      \
        (sc),                                                                                      \
        sampler TSFA_ZKEY_ARGS((ka), (kb), (kc)))

    /* `key_a/b/c` below are macro arguments that only the depth variants'
     * TSFA_ZKEY_ARGS keeps; in a plain variant they are discarded with the rest
     * of the expansion and never reach the compiler. */

    /* The uv basis is anchored to vertices 0/1/2 as the projector named them,
     * so only the screen positions, shades and depth keys permute here. */
    if( y0 <= y1 && y0 <= y2 )
    {
        if( y1 <= y2 )
        {
            if( y2 < 0 || y0 > screen_height )
                return;
            TSFA_ORDERED_CALL(
                x0, x1, x2, y0, y1, y2, shade7bit_a, shade7bit_b, shade7bit_c, key_a, key_b,
                key_c);
        }
        else
        {
            if( y1 < 0 || y0 > screen_height )
                return;
            TSFA_ORDERED_CALL(
                x0, x2, x1, y0, y2, y1, shade7bit_a, shade7bit_c, shade7bit_b, key_a, key_c,
                key_b);
        }
    }
    else if( y1 <= y2 )
    {
        if( y2 <= y0 )
        {
            if( y0 < 0 || y1 > screen_height )
                return;
            TSFA_ORDERED_CALL(
                x1, x2, x0, y1, y2, y0, shade7bit_b, shade7bit_c, shade7bit_a, key_b, key_c,
                key_a);
        }
        else
        {
            if( y2 < 0 || y1 > screen_height )
                return;
            TSFA_ORDERED_CALL(
                x1, x0, x2, y1, y0, y2, shade7bit_b, shade7bit_a, shade7bit_c, key_b, key_a,
                key_c);
        }
    }
    else
    {
        if( y0 <= y1 )
        {
            if( y1 < 0 || y2 > screen_height )
                return;
            TSFA_ORDERED_CALL(
                x2, x0, x1, y2, y0, y1, shade7bit_c, shade7bit_a, shade7bit_b, key_c, key_a,
                key_b);
        }
        else
        {
            if( y0 < 0 || y2 > screen_height )
                return;
            TSFA_ORDERED_CALL(
                x2, x1, x0, y2, y1, y0, shade7bit_c, shade7bit_b, shade7bit_a, key_c, key_b,
                key_a);
        }
    }

#undef TSFA_ORDERED_CALL
}

#undef TSFA_ZKEY_PARAMS
#undef TSFA_ZKEY_ARGS
#undef TSFA_ZROW_ARGS
#undef TSFA_ZADVANCE

#undef TSFA_FN
#undef TSFA_ORDERED
#undef TSFA_SPAN
#undef TSFA_ZBUF
