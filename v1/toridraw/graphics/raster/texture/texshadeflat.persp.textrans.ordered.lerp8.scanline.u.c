#ifndef TEXSHADEFLAT_PERSP_TEXTRANS_ORDERED_LERP8_SCANLINE_U_C
#define TEXSHADEFLAT_PERSP_TEXTRANS_ORDERED_LERP8_SCANLINE_U_C

#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"
#include "span/tex.span_peer_decl.h"

#include <assert.h>
#include <stdint.h>

static void
raster_texshadeflat_persp_textrans_ordered_lerp8_scanline(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
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
    int* RESTRICT texels,
    int texture_width)
{
    (void)stride;
    (void)screen_height;
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = (screen_x0_ish16 - 1) >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x0 < 0 )
        screen_x0 = 0;
    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    steps = screen_x1 - screen_x0;

    offset += screen_x0;

    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int curr_u = 0;
    int curr_v = 0;
    int next_u = 0;
    int next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;

    int shade = shade8bit_ish8 >> 8;

    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;

        int step_u = (next_u - curr_u) << (texture_shift - 3);
        int step_v = (next_v - curr_v) << (texture_shift - 3);

        int u_scan = curr_u << texture_shift;
        int v_scan = curr_v << texture_shift;

        raster_linear_transparent_texshadeflat_lerp8(
            (uint32_t*)pixel_buffer,
            offset,
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            shade);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;
    }

    if( lerp8_last_steps == 0 )
        return;

    int w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    curr_u = (au) / w;
    curr_u = clamp(curr_u, 0, texture_width - 1);
    curr_v = (bv) / w;

    au += step_au_dx;
    bv += step_bv_dx;
    cw += step_cw_dx;

    w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);
    next_v = (bv) / w;

    int step_u = (next_u - curr_u) << (texture_shift - 3);
    int step_v = (next_v - curr_v) << (texture_shift - 3);

    int u_scan = curr_u << texture_shift;
    int v_scan = curr_v << texture_shift;

    shade = shade8bit_ish8 >> 8;
    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan >> texture_shift;
        u &= texture_width - 1;
        v &= texture_width - 1;
        int texel = texels[u + v * texture_width];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

#endif
