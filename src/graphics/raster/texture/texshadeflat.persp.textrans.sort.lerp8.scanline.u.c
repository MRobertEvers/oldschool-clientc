#ifndef TEXSHADEFLAT_PERSP_TEXTRANS_SORT_LERP8_SCANLINE_U_C
#define TEXSHADEFLAT_PERSP_TEXTRANS_SORT_LERP8_SCANLINE_U_C

#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"
#include "tex_shard_swap.h"

#include <assert.h>

static void
raster_texshadeflat_persp_textrans_sort_lerp8_scanline(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
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
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0 < 0 )
        screen_x0 = 0;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    // If texture width is 128 or 64.
    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int curr_u = 0;
    int curr_v = 0;
    int next_u = 0;
    int next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;

    int shade = shade8bit_ish8 >> 8;

    do
    {
        if( lerp8_steps == 0 )
            break;

        int w = (-cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;
        // curr_v = clamp(curr_v, 0, texture_width - 1);

        au += (step_au_dx << 3);
        bv += (step_bv_dx << 3);
        cw += (step_cw_dx << 3);

        w = (-cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;
        // next_v = clamp(next_v, 0x0, texture_width - 1);

        int step_u = (next_u - curr_u);
        int step_v = (next_v - curr_v);

        int u_scan = curr_u << 3;
        int v_scan = curr_v << 3;

        for( int i = 0; i < 8; i++ )
        {
            int u = u_scan >> 3;
            int v = v_scan >> 3;
            u &= texture_width - 1;
            v &= texture_width - 1;
            int texel = texels[u + v * texture_width];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);

            u_scan += step_u;
            v_scan += step_v;

            offset += 1;
        }

    } while( lerp8_steps-- > 0 );

    if( lerp8_last_steps == 0 )
        return;

    int w = (-cw) >> texture_shift;
    if( w == 0 )
        return;

    curr_u = (au) / w;
    curr_u = clamp(curr_u, 0, texture_width - 1);
    curr_v = (bv) / w;
    // curr_v = clamp(curr_v, 0, texture_width - 1);

    au += (step_au_dx << 3);
    bv += (step_bv_dx << 3);
    cw += (step_cw_dx << 3);

    w = (-cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);

    next_v = (bv) / w;
    // next_v = clamp(next_v, 0x0, texture_width - 1);

    int u_scan = curr_u << 3;
    int v_scan = curr_v << 3;

    int step_u = (next_u - curr_u);
    int step_v = (next_v - curr_v);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> 3;
        int v = v_scan >> 3;
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
