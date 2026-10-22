/*
 * One AFFINE texture scanline, depth-tested. Included once per gate. The
 * includer defines:
 *
 *   TAZ_SPAN_FN   scanline entry point name
 *   TAZ_GATE      0 = every texel drawn (texopaque), 1 = RGB 0 is skipped (textrans)
 *
 * The span is draw_texture_scanline_{opaque,transparent}_blend_affine_
 * branching_lerp8_ish16_ordered (tex.span.scalar.u.c) pixel for pixel: the same
 * clamps and one-subpixel-early start, the same two end divides and linear uv
 * step, the same `u & u_mask` / `v & v_mask` addressing, and the shade held for
 * each eight-pixel block exactly as raster_linear_*_blend_lerp8_v3 holds it.
 * The painter writes those blocks through the per-ISA block kernels; here they
 * are a plain loop, because every pixel now has a compare in front of it.
 *
 * The signature is the sampler-shaped one texplane.persp.branching.lerp8_v3_
 * tmpl.inc calls, so the affine depth triangle is that template with this span
 * plugged in -- the affine and perspective triangle walks are the same code.
 * Only the texels, width, shift and masks are read off the sampler: the
 * painter's affine span does not clamp u, and neither does this.
 *
 * Both gates write depth when they draw. A colour-keyed texel that is skipped
 * writes nothing, because the store sits behind the same test that draws it.
 */

#include "graphics/int_wrap.h"

static inline void
TAZ_SPAN_FN(
    toripixel_t* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    const struct ToriDraw_TexSampler* RESTRICT sampler,
    torizdepth_t* RESTRICT zbuffer,
    float key_edge,
    float key_step_dx)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int const texture_width = sampler->width;
    int const texture_shift = sampler->shift;
    int const u_mask = sampler->u_mask;
    int const v_mask = sampler->v_mask;
    const int* RESTRICT texels = sampler->texels;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int offset = pixel_offset + x0;
    shade8bit_ish8 =
        toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_mul(step_shade8bit_dx_ish8, x0));

    int width = x1 - x0;

    int w_div = cw >> texture_shift;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div != 0 )
    {
        u_coord = au / w_div;
        v_coord = bv / w_div;
    }
    u_coord = clamp(u_coord, 0, texture_width - 1);

    int au_end = toridraw_add_mul32(au, step_au_dx, width);
    int bv_end = toridraw_add_mul32(bv, step_bv_dx, width);
    int cw_end = toridraw_add_mul32(cw, step_cw_dx, width);
    int w_div_end = cw_end >> texture_shift;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end != 0 )
    {
        u_coord_end = au_end / w_div_end;
        v_coord_end = bv_end / w_div_end;
    }
    u_coord_end = clamp(u_coord_end, 0, texture_width - 1);

    int u_scan = u_coord << texture_shift;
    int v_scan = v_coord << texture_shift;
    int step_u = ((u_coord_end - u_coord) << texture_shift) / width;
    int step_v = ((v_coord_end - v_coord) << texture_shift) / width;

    float key = key_edge + key_step_dx * (float)x0;

    for( int i = 0; i < width; i++ )
    {
        /* The painter's block kernels take one shade per eight pixels and the
         * remainder takes the shade after the last full block: both are the
         * shade of the block this pixel falls in. */
        if( i != 0 && (i & 7) == 0 )
            shade8bit_ish8 =
                toridraw_wrap_add(shade8bit_ish8, toridraw_wrap_shl(step_shade8bit_dx_ish8, 3));

        if( key > (float)zbuffer[offset] )
        {
            int const u = (u_scan >> texture_shift) & u_mask;
            int const v = v_scan & v_mask;
            int const texel = texels[u + v];

#if TAZ_GATE == 1
            if( texel != 0 )
#endif
            {
                pixel_buffer[offset] =
                    toritexel_to_pixel(toritexel_shade_blend(texel, shade8bit_ish8 >> 8));
                zbuffer[offset] = (torizdepth_t)key;
            }
        }

        u_scan += step_u;
        v_scan += step_v;
        offset += 1;
        key += key_step_dx;
    }
}

#undef TAZ_SPAN_FN
#undef TAZ_GATE
