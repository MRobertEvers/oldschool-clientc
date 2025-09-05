#include "texture.h"

#include "projection.h"
#include "shade.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SCREEN_HEIGHT 600
#define SCREEN_WIDTH 800

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static void
raster_texture_scanline(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int pixel_offset,
    long long au,
    long long bv,
    long long cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int* texels,
    long long texture_width,
    long long texture_opaque)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    while( steps-- > 0 )
    {
        if( (-cw) >> texture_shift == 0 )
            continue;

        // Instead of multiplying au,bv by texture width, shift the divisor down.
        int u = (au) / ((-cw) >> texture_shift);
        int v = (bv) / ((-cw) >> texture_shift);

        // This will wrap at the texture width.
        // The osrs rasterizer tiles textures implicitly by ignoring overflow when
        // stepping.
        u &= texture_width - 1;
        v &= texture_width - 1;

        // The osrs rasterizer clamps the u and v coordinates to the texture width.
        int c = -1;
        if( u < 0 )
        {
            u = 0;
            c = 0xFF0000;
        }
        if( v < 0 )
        {
            v = 0;
            c = 0x00FF00;
        }
        if( u >= texture_width )
        {
            c = 0xFF00FF;
            u = texture_width - 1;
        }
        if( v >= texture_width )
        {
            c = 0x00FFFF;
            v = texture_width - 1;
        }

        assert(u >= 0);
        assert(v >= 0);
        assert(u < texture_width);
        assert(v < texture_width);

        // if( u < 0 )
        //     u = 0;
        // if( v < 0 )
        //     v = 0;
        // if( u >= texture_width )
        //     u = texture_width - 1;
        // if( v >= texture_width )
        //     v = texture_width - 1;

        assert(u >= 0 && u < texture_width);
        assert(v >= 0 && v < texture_width);

        int texel = texels[u + v * texture_width];
        if( texture_opaque || texel != 0 )
        {
            if( c != -1 )
                pixel_buffer[offset] = c;
            else
                pixel_buffer[offset] = texel;
        }

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        offset += 1;
        assert(offset >= 0 && offset < screen_width * screen_height);
    }
}

static void
raster_texture_scanline_blend(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int pixel_offset,
    long long au,
    long long bv,
    long long cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* texels,
    long long texture_width,
    long long texture_opaque)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

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

    while( steps-- > 0 )
    {
        if( (-cw) >> texture_shift == 0 )
            continue;

        // Instead of multiplying au,bv by texture width, shift the divisor down.
        int u = (au) / ((-cw) >> texture_shift);
        int v = (bv) / ((-cw) >> texture_shift);

        // This will wrap at the texture width.
        // The osrs rasterizer tiles textures implicitly by ignoring overflow when
        // stepping.
        u &= texture_width - 1;
        v &= texture_width - 1;

        assert(u >= 0);
        assert(v >= 0);
        assert(u < texture_width);
        assert(v < texture_width);

        // if( u < 0 )
        //     u = 0;
        // if( v < 0 )
        //     v = 0;
        // if( u >= texture_width )
        //     u = texture_width - 1;
        // if( v >= texture_width )
        //     v = texture_width - 1;

        assert(u >= 0 && u < texture_width);
        assert(v >= 0 && v < texture_width);

        int texel = texels[u + v * texture_width];
        if( texture_opaque || texel != 0 )
        {
            pixel_buffer[offset] = shade_blend(texel, shade8bit_ish8 >> 8);
        }

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
        shade8bit_ish8 += step_shade8bit_dx_ish8;

        offset += 1;
        assert(offset >= 0 && offset < screen_width * screen_height);
    }
}

static inline int
clamp(int value, int min, int max)
{
    if( value < min )
        return min;
    if( value > max )
        return max;
    return value;
}

static void
raster_texture_scanline_transparent_blend_lerp8(
    int* pixel_buffer,
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
    int step_shade8bit_dx_ish8,
    int* texels,
    int texture_width)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

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
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;

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

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        w = (-cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;
        // next_v = clamp(next_v, 0x0, texture_width - 1);

        int step_u = (next_u - curr_u) << (texture_shift - 3);
        int step_v = (next_v - curr_v) << (texture_shift - 3);

        int u_scan = curr_u << texture_shift;
        int v_scan = curr_v << texture_shift;

        for( int i = 0; i < 8; i++ )
        {
            int u = u_scan >> texture_shift;
            int v = v_scan & 0x3f80;
            int texel = texels[u + (v)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade8bit_ish8 >> 8);

            u_scan += step_u;
            v_scan += step_v;

            offset += 1;
        }

        shade8bit_ish8 += lerp8_shade_step;

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

    au += step_au_dx;
    bv += step_bv_dx;
    cw += step_cw_dx;

    w = (-cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);

    next_v = (bv) / w;
    // next_v = clamp(next_v, 0x0, texture_width - 1);

    long long u_scan = curr_u << 3;
    long long v_scan = curr_v << 3;

    long long step_u = (next_u - curr_u);
    long long step_v = (next_v - curr_v);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> 3;
        int v = v_scan >> 3;
        u &= texture_width - 1;
        v &= texture_width - 1;
        int texel = texels[u + v * texture_width];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade8bit_ish8 >> 8);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static void
raster_texture_scanline_opaque_blend_lerp8(
    int* pixel_buffer,
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
    int step_shade8bit_dx_ish8,
    int* texels,
    int texture_width)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

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

    long long curr_u = 0;
    long long curr_v = 0;
    long long next_u = 0;
    long long next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;

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
            pixel_buffer[offset] = shade_blend(texel, shade8bit_ish8 >> 8);

            u_scan += step_u;
            v_scan += step_v;

            offset += 1;
        }

        shade8bit_ish8 += lerp8_shade_step;

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

    long long u_scan = curr_u << 3;
    long long v_scan = curr_v << 3;

    long long step_u = (next_u - curr_u);
    long long step_v = (next_v - curr_v);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> 3;
        int v = v_scan >> 3;
        u &= texture_width - 1;
        v &= texture_width - 1;
        int texel = texels[u + v * texture_width];
        pixel_buffer[offset] = shade_blend(texel, shade8bit_ish8 >> 8);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static void
raster_texture_scanline_transparent_lerp8(
    int* pixel_buffer,
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
    int* texels,
    int texture_width)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    long long curr_u = 0;
    long long curr_v = 0;
    long long next_u = 0;
    long long next_v = 0;

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

    long long u_scan = curr_u << 3;
    long long v_scan = curr_v << 3;

    long long step_u = (next_u - curr_u);
    long long step_v = (next_v - curr_v);

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

static void
raster_texture_scanline_opaque_lerp8(
    int* pixel_buffer,
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
    int* texels,
    int texture_width)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    long long curr_u = 0;
    long long curr_v = 0;
    long long next_u = 0;
    long long next_v = 0;

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

    long long u_scan = curr_u << 3;
    long long v_scan = curr_v << 3;

    long long step_u = (next_u - curr_u);
    long long step_v = (next_v - curr_v);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> 3;
        int v = v_scan >> 3;
        u &= texture_width - 1;
        v &= texture_width - 1;
        int texel = texels[u + v * texture_width];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static void
raster_texture_scanline_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int pixel_offset,
    long long au,
    long long bv,
    long long cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int* texels,
    long long texture_width,
    long long texture_opaque)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

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

    long long lerp8_steps = steps >> 3;
    long long lerp8_last_steps = steps & 0x7;
    long long curr_u = 0;
    long long curr_v = 0;

    long long u = au;
    long long v = bv;
    long long w = cw;

    long long curr_w_dsh7 = w >> 7;
    if( curr_w_dsh7 != 0 )
    {
        curr_u = u / (-curr_w_dsh7);
        curr_v = v / (-curr_w_dsh7);

        if( curr_u < 0 )
            curr_u = 0;
        else if( curr_u >= texture_width )
            curr_u = texture_width - 1;

        if( curr_v < 0 )
            curr_v = 0;
        else if( curr_v >= texture_width )
            curr_v = texture_width - 1;
    }

    long long next_u = 0;
    long long next_v = 0;

    u = u + (step_au_dx << 3);
    v = v + (step_bv_dx << 3);
    w = w + (step_cw_dx << 3);

    curr_w_dsh7 = w >> 7;
    if( curr_w_dsh7 != 0 )
    {
        next_u = u / (-curr_w_dsh7);
        next_v = v / (-curr_w_dsh7);

        if( next_u < 0x7 )
            next_u = 0x7;
        else if( next_u >= texture_width )
            next_u = texture_width - 1;

        if( next_v < 0x7 )
            next_v = 0x7;
        else if( next_v >= texture_width )
            next_v = texture_width - 1;
    }

    int step_u = (next_u - curr_u) >> 3;
    int step_v = (next_v - curr_v) >> 3;

    do
    {
        if( lerp8_steps == 0 )
            break;

        for( int i = 0; i < 8; i++ )
        {
            int texel = texels[curr_u + curr_v * texture_width];
            if( texture_opaque || texel != 0 )
                pixel_buffer[offset++] = texel;

            curr_u += step_u;
            curr_v += step_v;

            assert(curr_u >= 0 && curr_u < texture_width);
            assert(curr_v >= 0 && curr_v < texture_width);
        }

        curr_u = next_u;
        curr_v = next_v;

        u = u + (step_au_dx << 3);
        v = v + (step_bv_dx << 3);
        w = w + (step_cw_dx << 3);

        curr_w_dsh7 = w >> 7;
        if( curr_w_dsh7 != 0 )
        {
            next_u = u / (-curr_w_dsh7);
            next_v = v / (-curr_w_dsh7);

            if( next_u < 0x7 )
                next_u = 0x7;
            else if( next_u >= texture_width )
                next_u = texture_width - 1;

            if( next_v < 0x7 )
                next_v = 0x7;
            else if( next_v >= texture_width )
                next_v = texture_width - 1;
        }

        step_u = (next_u - curr_u) >> 3;
        step_v = (next_v - curr_v) >> 3;

        // if( (-cw) >> texture_shift == 0 )
        //     continue;

        // // Instead of multiplying au,bv by texture width, shift the divisor down.
        // int u = (au) / ((-cw) >> texture_shift);
        // int v = (bv) / ((-cw) >> texture_shift);

        // // This will wrap at the texture width.
        // // The osrs rasterizer tiles textures implicitly by ignoring overflow when
        // // stepping.
        // u &= texture_width - 1;
        // v &= texture_width - 1;

        // // The osrs rasterizer clamps the u and v coordinates to the texture width.
        // int c = -1;

        // assert(u >= 0);
        // assert(v >= 0);
        // assert(u < texture_width);
        // assert(v < texture_width);

        // // if( u < 0 )
        // //     u = 0;
        // // if( v < 0 )
        // //     v = 0;
        // // if( u >= texture_width )
        // //     u = texture_width - 1;
        // // if( v >= texture_width )
        // //     v = texture_width - 1;

        // assert(u >= 0 && u < texture_width);
        // assert(v >= 0 && v < texture_width);

        // int texel = texels[u + v * texture_width];
        // if( texture_opaque || texel != 0 )
        // {
        //     if( c != -1 )
        //         pixel_buffer[offset] = c;
        //     else
        //         pixel_buffer[offset] = texel;
        // }

        // au += step_au_dx;
        // bv += step_bv_dx;
        // cw += step_cw_dx;

        // offset += 1;
        // assert(offset >= 0 && offset < screen_width * screen_height);
    } while( lerp8_steps-- > 0 );

    while( lerp8_last_steps-- > 0 )
    {
        int texel = texels[curr_u + curr_v * texture_width];
        if( texture_opaque || texel != 0 )
            pixel_buffer[offset++] = texel;

        curr_u += step_u;
        curr_v += step_v;
    }
}

void
raster_texture_transparent_blend_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
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
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
        SWAP(shade7bit_a, shade7bit_c);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
        SWAP(shade7bit_a, shade7bit_b);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
        SWAP(shade7bit_b, shade7bit_c);
    }

    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

    if( screen_y0 >= screen_height )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (screen_x0 < 0 || screen_x1 < 0 || screen_x2 < 0) &&
        (screen_x0 > screen_width || screen_x1 > screen_width || screen_x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    long long vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    long long vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    long long vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    long long vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    long long vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    long long vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    long long vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    long long vOVPlane_normal_xhat =
        orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    long long vOVPlane_normal_yhat =
        orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    long long vOVPlane_normal_zhat =
        orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    long long vUOPlane_normal_xhat =
        vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    long long vUOPlane_normal_yhat =
        vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    long long vUOPlane_normal_zhat =
        vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now polong long in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    long long dy_AC = screen_y2 - screen_y0;
    long long dy_AB = screen_y1 - screen_y0;
    long long dy_BC = screen_y2 - screen_y1;

    long long dx_AC = screen_x2 - screen_x0;
    long long dx_AB = screen_x1 - screen_x0;
    long long dx_BC = screen_x2 - screen_x1;

    int dblend7bit_ab = shade7bit_b - shade7bit_a;
    int dblend7bit_ac = shade7bit_c - shade7bit_a;

    long long step_edge_x_AC_ish16 = 0;
    long long step_edge_x_AB_ish16 = 0;
    long long step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    // Same idea here for color. Solve the system of equations.
    // Barycentric coordinates.

    // Shades are provided 0-127, shift up by 1, then up by 8 to get 0-255.
    // Again, kramer's rule.
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    // TODO: Why add 1 xhat?
    int shade8bit_edge_ish8 =
        (shade7bit_a << 9) - shade8bit_xhat_ish8 * screen_x0 + shade8bit_xhat_ish8;

    long long au = 0;
    long long bv = 0;
    long long cw = 0;

    long long edge_x_AC_ish16 = screen_x0 << 16;
    long long edge_x_AB_ish16 = screen_x0 << 16;
    long long edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        shade8bit_edge_ish8 -= shade8bit_yhat_ish8 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;

    long long dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    long long steps = screen_y1 - screen_y0;
    long long offset = screen_y0 * screen_width;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        long long x_start = edge_x_AC_ish16 >> 16;
        long long x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_transparent_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x_start,
            x_end,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_transparent_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16 >> 16,
            edge_x_BC_ish16 >> 16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }
}

void
raster_texture_opaque_blend_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
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
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
        SWAP(shade7bit_a, shade7bit_c);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
        SWAP(shade7bit_a, shade7bit_b);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
        SWAP(shade7bit_b, shade7bit_c);
    }

    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

    if( screen_y0 >= screen_height )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (screen_x0 < 0 || screen_x1 < 0 || screen_x2 < 0) &&
        (screen_x0 > screen_width || screen_x1 > screen_width || screen_x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    long long vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    long long vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    long long vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    long long vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    long long vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    long long vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    long long vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    long long vOVPlane_normal_xhat =
        orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    long long vOVPlane_normal_yhat =
        orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    long long vOVPlane_normal_zhat =
        orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    long long vUOPlane_normal_xhat =
        vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    long long vUOPlane_normal_yhat =
        vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    long long vUOPlane_normal_zhat =
        vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now polong long in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    long long dy_AC = screen_y2 - screen_y0;
    long long dy_AB = screen_y1 - screen_y0;
    long long dy_BC = screen_y2 - screen_y1;

    long long dx_AC = screen_x2 - screen_x0;
    long long dx_AB = screen_x1 - screen_x0;
    long long dx_BC = screen_x2 - screen_x1;

    int dblend7bit_ab = shade7bit_b - shade7bit_a;
    int dblend7bit_ac = shade7bit_c - shade7bit_a;

    long long step_edge_x_AC_ish16 = 0;
    long long step_edge_x_AB_ish16 = 0;
    long long step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    // Same idea here for color. Solve the system of equations.
    // Barycentric coordinates.

    // Shades are provided 0-127, shift up by 1, then up by 8 to get 0-255.
    // Again, kramer's rule.
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    // TODO: Why add 1 xhat?
    int shade8bit_edge_ish8 =
        (shade7bit_a << 9) - shade8bit_xhat_ish8 * screen_x0 + shade8bit_xhat_ish8;

    long long au = 0;
    long long bv = 0;
    long long cw = 0;

    long long edge_x_AC_ish16 = screen_x0 << 16;
    long long edge_x_AB_ish16 = screen_x0 << 16;
    long long edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        shade8bit_edge_ish8 -= shade8bit_yhat_ish8 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;

    long long dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    long long steps = screen_y1 - screen_y0;
    long long offset = screen_y0 * screen_width;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        long long x_start = edge_x_AC_ish16 >> 16;
        long long x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_opaque_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x_start,
            x_end,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_opaque_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16 >> 16,
            edge_x_BC_ish16 >> 16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }
}

void
raster_texture_transparent_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit,
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
    }

    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

    if( screen_y0 >= screen_height )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (screen_x0 < 0 || screen_x1 < 0 || screen_x2 < 0) &&
        (screen_x0 > screen_width || screen_x1 > screen_width || screen_x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    long long vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    long long vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    long long vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    long long vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    long long vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    long long vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    long long vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    long long vOVPlane_normal_xhat =
        orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    long long vOVPlane_normal_yhat =
        orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    long long vOVPlane_normal_zhat =
        orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    long long vUOPlane_normal_xhat =
        vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    long long vUOPlane_normal_yhat =
        vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    long long vUOPlane_normal_zhat =
        vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now polong long in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    long long dy_AC = screen_y2 - screen_y0;
    long long dy_AB = screen_y1 - screen_y0;
    long long dy_BC = screen_y2 - screen_y1;

    long long dx_AC = screen_x2 - screen_x0;
    long long dx_AB = screen_x1 - screen_x0;
    long long dx_BC = screen_x2 - screen_x1;

    long long step_edge_x_AC_ish16 = 0;
    long long step_edge_x_AB_ish16 = 0;
    long long step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    long long au = 0;
    long long bv = 0;
    long long cw = 0;

    long long edge_x_AC_ish16 = screen_x0 << 16;
    long long edge_x_AB_ish16 = screen_x0 << 16;
    long long edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;

    long long dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    long long steps = screen_y1 - screen_y0;
    long long offset = screen_y0 * screen_width;

    int shade = shade7bit << 9;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        long long x_start = edge_x_AC_ish16 >> 16;
        long long x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_transparent_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x_start,
            x_end,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_transparent_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16 >> 16,
            edge_x_BC_ish16 >> 16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }
}

void
raster_texture_opaque_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit,
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
    }

    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

    if( screen_y0 >= screen_height )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (screen_x0 < 0 || screen_x1 < 0 || screen_x2 < 0) &&
        (screen_x0 > screen_width || screen_x1 > screen_width || screen_x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    long long vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    long long vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    long long vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    long long vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    long long vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    long long vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    long long vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    long long vOVPlane_normal_xhat =
        orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    long long vOVPlane_normal_yhat =
        orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    long long vOVPlane_normal_zhat =
        orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    long long vUOPlane_normal_xhat =
        vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    long long vUOPlane_normal_yhat =
        vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    long long vUOPlane_normal_zhat =
        vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now polong long in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    long long dy_AC = screen_y2 - screen_y0;
    long long dy_AB = screen_y1 - screen_y0;
    long long dy_BC = screen_y2 - screen_y1;

    long long dx_AC = screen_x2 - screen_x0;
    long long dx_AB = screen_x1 - screen_x0;
    long long dx_BC = screen_x2 - screen_x1;

    long long step_edge_x_AC_ish16 = 0;
    long long step_edge_x_AB_ish16 = 0;
    long long step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    long long au = 0;
    long long bv = 0;
    long long cw = 0;

    long long edge_x_AC_ish16 = screen_x0 << 16;
    long long edge_x_AB_ish16 = screen_x0 << 16;
    long long edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;

    long long dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    long long steps = screen_y1 - screen_y0;
    long long offset = screen_y0 * screen_width;

    int shade = shade7bit << 9;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        long long x_start = edge_x_AC_ish16 >> 16;
        long long x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_opaque_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x_start,
            x_end,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT;
    cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT;
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_opaque_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16 >> 16,
            edge_x_BC_ish16 >> 16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }
}

void
raster_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int* texels,
    int texture_width,
    int texture_opaque)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
    }

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    int vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    int vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    int vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.
    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

    int dy_AC = screen_y2 - screen_y0;
    int dy_AB = screen_y1 - screen_y0;
    int dy_BC = screen_y2 - screen_y1;

    int dx_AC = screen_x2 - screen_x0;
    int dx_AB = screen_x1 - screen_x0;
    int dx_BC = screen_x2 - screen_x1;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    int x_start, x_end, x, u, v;

    int y = screen_y0;
    for( ; y < screen_y1 && y < screen_height; y++ )
    {
        if( y < 0 || y >= screen_height )
            continue;

        x_start = edge_x_AC_ish16 >> 16;
        x_end = edge_x_AB_ish16 >> 16;

        if( x_start > x_end )
        {
            SWAP(x_start, x_end);
        }

        for( x = x_start; x < x_end; x++ )
        {
            if( x < 0 || x >= screen_width )
                continue;

            au = vOVPlane_normal_zhat << UNIT_SCALE_SHIFT + vOVPlane_normal_xhat * (x - 400) +
                                             vOVPlane_normal_yhat * (y - 300);
            bv = vUOPlane_normal_zhat << UNIT_SCALE_SHIFT + vUOPlane_normal_xhat * (x - 400) +
                                             vUOPlane_normal_yhat * (y - 300);
            cw = vUVPlane_normal_zhat << UNIT_SCALE_SHIFT + vUVPlane_normal_xhat * (x - 400) +
                                             vUVPlane_normal_yhat * (y - 300);

            assert(cw != 0);

            u = (au * texture_width) / (-cw);
            v = (bv * texture_width) / (-cw);

            if( u < 0 )
                u = 0;
            if( u >= texture_width )
                u = texture_width - 1;
            if( v < 0 )
                v = 0;
            if( v >= texture_width )
                v = texture_width - 1;

            assert(u >= 0);
            assert(u < texture_width);
            assert(v >= 0);
            assert(v < texture_width);
            int texel = texels[u + v * texture_width];

            int offset = y * screen_width + x;
            assert(offset >= 0 && offset < screen_width * screen_height);
            pixel_buffer[offset] = texel;
        }

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
    }

    if( screen_y1 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y1;
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;
        screen_y1 = 0;
    }

    y = screen_y1;

    for( ; y < screen_y2 && y < screen_height; y++ )
    {
        if( y < 0 || y >= screen_height )
            continue;

        x_start = edge_x_AC_ish16 >> 16;
        x_end = edge_x_BC_ish16 >> 16;

        if( x_start > x_end )
        {
            SWAP(x_start, x_end);
        }

        for( x = x_start; x < x_end; x++ )
        {
            if( x < 0 || x >= screen_width )
                continue;

            au = (vOVPlane_normal_zhat << UNIT_SCALE_SHIFT) + vOVPlane_normal_xhat * (x - 400) +
                                             vOVPlane_normal_yhat * (y - 300);
            bv = (vUOPlane_normal_zhat << UNIT_SCALE_SHIFT) + vUOPlane_normal_xhat * (x - 400) +
                                             vUOPlane_normal_yhat * (y - 300);
            cw = (vUVPlane_normal_zhat << UNIT_SCALE_SHIFT) + vUVPlane_normal_xhat * (x - 400) +
                                             vUVPlane_normal_yhat * (y - 300);

            assert(cw != 0);

            u = (au * texture_width) / (-cw);
            v = (bv * texture_width) / (-cw);

            if( u < 0 )
                u = 0;
            if( u >= texture_width )
                u = texture_width - 1;
            if( v < 0 )
                v = 0;
            if( v >= texture_width )
                v = texture_width - 1;

            assert(u >= 0 && u < texture_width);
            assert(v >= 0 && v < texture_width);
            int texel = texels[u + v * texture_width];

            int offset = y * screen_width + x;
            assert(offset >= 0 && offset < screen_width * screen_height);
            pixel_buffer[offset] = texel;
        }

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
    }
}

void
textureTriangle(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int shadeA,
    int shadeB,
    int shadeC,
    int originX,
    int originY,
    int originZ,
    int txB,
    int txC,
    int tyB,
    int tyC,
    int tzB,
    int tzC,
    int* pixel_buffer,
    int* texels,
    int texture_width)
{
    // _Pix3D.opaque = !_Pix3D.textureHasTransparency[texture];

    int verticalX = originX - txB;
    int verticalY = originY - tyB;
    int verticalZ = originZ - tzB;

    int horizontalX = txC - originX;
    int horizontalY = tyC - originY;
    int horizontalZ = tzC - originZ;

    int shift_offset = 0;
    int zshift = 14 - shift_offset;
    int xshift = 8 - shift_offset;
    int yshift = 5 - shift_offset;

    // Shift up by 14, the top 7 bits are the texture coord.
    // 9 of the bit shift come from the (d * z) that all model vertexes are multiplied by.
    // So really this is upshifted by 5.
    // Since the zhat component is really
    // U = (dudz << 5) * SCALE  + (dudx << 5 * x) + (dudy << 5 * y)
    // Since SCALE is << 9, then the upshift is really by 5.
    // the xshift of 8, is pre-multiplied by 8 (<< 3 and << 5).

    // For U and V, we want the top 7 bits to represent the texture coordinate.
    // Since we are only shifting up by 5, shifting down by 7, or masking the top 7 bits,
    // will give the result divided by 4.
    // Perhaps pnm coords are 4 times longer than textures?
    // so a U of 4 is actuall u 1?

    // Another trick used in later deobs is shifting the U value up by 18 (which is already assumed
    // to be shifted up by 7) so that the top 7 bits and anything else is truncated.

    int u = ((horizontalX * originY) - (horizontalY * originX)) << zshift;
    int uStride = ((horizontalY * originZ) - (horizontalZ * originY)) << (xshift);
    int uStepVertical = ((horizontalZ * originX) - (horizontalX * originZ)) << yshift;

    int v = ((verticalX * originY) - (verticalY * originX)) << zshift;
    int vStride = ((verticalY * originZ) - (verticalZ * originY)) << xshift;
    int vStepVertical = ((verticalZ * originX) - (verticalX * originZ)) << yshift;

    int w = ((verticalY * horizontalX) - (verticalX * horizontalY)) << zshift;
    int wStride = ((verticalZ * horizontalY) - (verticalY * horizontalZ)) << xshift;
    int wStepVertical = ((verticalX * horizontalZ) - (verticalZ * horizontalX)) << yshift;

    int dxAB = xB - xA;
    int dyAB = yB - yA;
    int dxAC = xC - xA;
    int dyAC = yC - yA;

    /**
     * I'm not sure how this works, but later versions use barycentric coordinates
     * to interpolate colors.
     */
    int xStepAB = 0;
    int shadeStepAB = 0;
    if( yB != yA )
    {
        xStepAB = (dxAB << 16) / dyAB;
        shadeStepAB = ((shadeB - shadeA) << 16) / dyAB;
    }

    int xStepBC = 0;
    int shadeStepBC = 0;
    if( yC != yB )
    {
        xStepBC = ((xC - xB) << 16) / (yC - yB);
        shadeStepBC = ((shadeC - shadeB) << 16) / (yC - yB);
    }

    int xStepAC = 0;
    int shadeStepAC = 0;
    if( yC != yA )
    {
        xStepAC = ((xA - xC) << 16) / (yA - yC);
        shadeStepAC = ((shadeA - shadeC) << 16) / (yA - yC);
    }

    // this won't change any rendering, saves not wasting time "drawing" an invalid triangle
    int triangleArea = (dxAB * dyAC) - (dyAB * dxAC);
    if( triangleArea == 0 )
    {
        return;
    }

    if( yA <= yB && yA <= yC )
    {
        if( yA < SCREEN_HEIGHT )
        {
            if( yB > SCREEN_HEIGHT )
            {
                yB = SCREEN_HEIGHT;
            }

            if( yC > SCREEN_HEIGHT )
            {
                yC = SCREEN_HEIGHT;
            }

            if( yB < yC )
            {
                xC = xA <<= 16;
                shadeC = shadeA <<= 16;
                if( yA < 0 )
                {
                    xC -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    shadeC -= shadeStepAC * yA;
                    shadeA -= shadeStepAB * yA;
                    yA = 0;
                }

                xB <<= 16;
                shadeB <<= 16;
                if( yB < 0 )
                {
                    xB -= xStepBC * yB;
                    shadeB -= shadeStepBC * yB;
                    yB = 0;
                }

                int dy = yA - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )
                {
                    yC -= yB;
                    yB -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeA >> 8);
                        xC += xStepAC;
                        xA += xStepAB;
                        shadeC += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeB >> 8);
                        xC += xStepAC;
                        xB += xStepBC;
                        shadeC += shadeStepAC;
                        shadeB += shadeStepBC;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yC -= yB;
                    yB -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeC >> 8);
                        xC += xStepAC;
                        xA += xStepAB;
                        shadeC += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeC >> 8);
                        xC += xStepAC;
                        xB += xStepBC;
                        shadeC += shadeStepAC;
                        shadeB += shadeStepBC;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
            else
            {
                xB = xA <<= 16;
                shadeB = shadeA <<= 16;
                if( yA < 0 )
                {
                    xB -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    shadeB -= shadeStepAC * yA;
                    shadeA -= shadeStepAB * yA;
                    yA = 0;
                }

                xC <<= 16;
                shadeC <<= 16;
                if( yC < 0 )
                {
                    xC -= xStepBC * yC;
                    shadeC -= shadeStepBC * yC;
                    yC = 0;
                }

                int dy = yA - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( (yA == yC || xStepAC >= xStepAB) && (yA != yC || xStepBC <= xStepAB) )
                {
                    yB -= yC;
                    yC -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeB >> 8);
                        xB += xStepAC;
                        xA += xStepAB;
                        shadeB += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeC >> 8);
                        xC += xStepBC;
                        xA += xStepAB;
                        shadeC += shadeStepBC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yB -= yC;
                    yC -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeA >> 8);
                        xB += xStepAC;
                        xA += xStepAB;
                        shadeB += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeA >> 8);
                        xC += xStepBC;
                        xA += xStepAB;
                        shadeC += shadeStepBC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
        }
    }
    else if( yB <= yC )
    {
        if( yB < SCREEN_HEIGHT )
        {
            if( yC > SCREEN_HEIGHT )
            {
                yC = SCREEN_HEIGHT;
            }

            if( yA > SCREEN_HEIGHT )
            {
                yA = SCREEN_HEIGHT;
            }

            if( yC < yA )
            {
                xA = xB <<= 16;
                shadeA = shadeB <<= 16;
                if( yB < 0 )
                {
                    xA -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    shadeA -= shadeStepAB * yB;
                    shadeB -= shadeStepBC * yB;
                    yB = 0;
                }

                xC <<= 16;
                shadeC <<= 16;
                if( yC < 0 )
                {
                    xC -= xStepAC * yC;
                    shadeC -= shadeStepAC * yC;
                    yC = 0;
                }

                int dy = yB - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( (yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC) )
                {
                    yA -= yC;
                    yC -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeB >> 8);
                        xA += xStepAB;
                        xB += xStepBC;
                        shadeA += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeC >> 8);
                        xA += xStepAB;
                        xC += xStepAC;
                        shadeA += shadeStepAB;
                        shadeC += shadeStepAC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yA -= yC;
                    yC -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeA >> 8);
                        xA += xStepAB;
                        xB += xStepBC;
                        shadeA += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeA >> 8);
                        xA += xStepAB;
                        xC += xStepAC;
                        shadeA += shadeStepAB;
                        shadeC += shadeStepAC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
            else
            {
                xC = xB <<= 16;
                shadeC = shadeB <<= 16;
                if( yB < 0 )
                {
                    xC -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    shadeC -= shadeStepAB * yB;
                    shadeB -= shadeStepBC * yB;
                    yB = 0;
                }

                xA <<= 16;
                shadeA <<= 16;
                if( yA < 0 )
                {
                    xA -= xStepAC * yA;
                    shadeA -= shadeStepAC * yA;
                    yA = 0;
                }

                int dy = yB - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( xStepAB < xStepBC )
                {
                    yC -= yA;
                    yA -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeB >> 8);
                        xC += xStepAB;
                        xB += xStepBC;
                        shadeC += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeB >> 8);
                        xA += xStepAC;
                        xB += xStepBC;
                        shadeA += shadeStepAC;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yC -= yA;
                    yA -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeC >> 8);
                        xC += xStepAB;
                        xB += xStepBC;
                        shadeC += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeA >> 8);
                        xA += xStepAC;
                        xB += xStepBC;
                        shadeA += shadeStepAC;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
        }
    }
    else if( yC < SCREEN_HEIGHT )
    {
        if( yA > SCREEN_HEIGHT )
        {
            yA = SCREEN_HEIGHT;
        }

        if( yB > SCREEN_HEIGHT )
        {
            yB = SCREEN_HEIGHT;
        }

        if( yA < yB )
        {
            xB = xC <<= 16;
            shadeB = shadeC <<= 16;
            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                shadeB -= shadeStepBC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            xA <<= 16;
            shadeA <<= 16;
            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                shadeA -= shadeStepAB * yA;
                yA = 0;
            }

            int dy = yC - (SCREEN_HEIGHT / 2);
            u += uStepVertical * dy;
            v += vStepVertical * dy;
            w += wStepVertical * dy;

            if( xStepBC < xStepAC )
            {
                yB -= yA;
                yA -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yA >= 0 )
                {
                    textureRaster(
                        xB >> 16,
                        xC >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeC >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yB >= 0 )
                {
                    textureRaster(
                        xB >> 16,
                        xA >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeA >> 8);
                    xB += xStepBC;
                    xA += xStepAB;
                    shadeB += shadeStepBC;
                    shadeA += shadeStepAB;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
            else
            {
                yB -= yA;
                yA -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yA >= 0 )
                {
                    textureRaster(
                        xC >> 16,
                        xB >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeB >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yB >= 0 )
                {
                    textureRaster(
                        xA >> 16,
                        xB >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeB >> 8);
                    xB += xStepBC;
                    xA += xStepAB;
                    shadeB += shadeStepBC;
                    shadeA += shadeStepAB;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
        }
        else
        {
            xA = xC <<= 16;
            shadeA = shadeC <<= 16;
            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                shadeA -= shadeStepBC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            xB <<= 16;
            shadeB <<= 16;
            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                shadeB -= shadeStepAB * yB;
                yB = 0;
            }

            int dy = yC - (SCREEN_HEIGHT / 2);
            u += uStepVertical * dy;
            v += vStepVertical * dy;
            w += wStepVertical * dy;

            if( xStepBC < xStepAC )
            {
                yA -= yB;
                yB -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yB >= 0 )
                {
                    textureRaster(
                        xA >> 16,
                        xC >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeC >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yA >= 0 )
                {
                    textureRaster(
                        xB >> 16,
                        xC >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeC >> 8);
                    xB += xStepAB;
                    xC += xStepAC;
                    shadeB += shadeStepAB;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
            else
            {
                yA -= yB;
                yB -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yB >= 0 )
                {
                    textureRaster(
                        xC >> 16,
                        xA >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeA >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yA >= 0 )
                {
                    textureRaster(
                        xC >> 16,
                        xB >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeB >> 8);
                    xB += xStepAB;
                    xC += xStepAC;
                    shadeB += shadeStepAB;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
        }
    }
}

void
textureRaster(
    int xA,
    int xB,
    int* dst,
    int offset,
    int* texels,
    int curU,
    int curV,
    int u,
    int v,
    int w,
    int uStride,
    int vStride,
    int wStride,
    int shadeA,
    int shadeB)
{
    if( xA >= xB )
    {
        return;
    }

    int opaque = 0;
    int shadeStrides;
    int strides;
    // Alpha true
    if( xB != xA )
    {
        shadeStrides = (shadeB - shadeA) / (xB - xA);

        if( xB > SCREEN_WIDTH )
        {
            xB = SCREEN_WIDTH;
        }

        if( xA < 0 )
        {
            shadeA -= xA * shadeStrides;
            xA = 0;
        }

        if( xA >= xB )
        {
            return;
        }

        strides = (xB - xA) >> 3;
        shadeStrides <<= 12;
        shadeA <<= 9;
    }
    else
    {
        if( xB - xA > 7 )
        {
            strides = (xB - xA) >> 3;
            // shadeStrides = (shadeB - shadeA) * _Pix3D.reciprical15[strides] >> 6;
        }
        else
        {
            strides = 0;
            shadeStrides = 0;
        }

        shadeA <<= 9;
    }

    offset += xA;

    // if lowdetail
    if( false )
    {
        int nextU = 0;
        int nextV = 0;
        int dx = xA - (SCREEN_WIDTH / 2);

        u = u + (uStride >> 3) * dx;
        v = v + (vStride >> 3) * dx;
        w = w + (wStride >> 3) * dx;

        int curW = w >> 12;
        if( curW != 0 )
        {
            curU = u / curW;
            curV = v / curW;
            if( curU < 0 )
            {
                curU = 0;
            }
            else if( curU > 0xfc0 )
            {
                curU = 0xfc0;
            }
        }

        u = u + uStride;
        v = v + vStride;
        w = w + wStride;

        curW = w >> 12;
        if( curW != 0 )
        {
            nextU = u / curW;
            nextV = v / curW;
            if( nextU < 0x7 )
            {
                nextU = 0x7;
            }
            else if( nextU > 0xfc0 )
            {
                nextU = 0xfc0;
            }
        }

        int stepU = (nextU - curU) >> 3;
        int stepV = (nextV - curV) >> 3;
        curU += (shadeA >> 3) & 0xc0000;
        int shadeShift = shadeA >> 23;

        if( true )
        {
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 12;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 0x7 )
                    {
                        nextU = 0x7;
                    }
                    else if( nextU > 0xfc0 )
                    {
                        nextU = 0xfc0;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += (shadeA >> 3) & 0xc0000;
                shadeShift = shadeA >> 23;
            }

            strides = xB - xA & 0x7;
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;
            }
        }
        else
        {
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 12;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 7 )
                    {
                        nextU = 7;
                    }
                    else if( nextU > 0xfc0 )
                    {
                        nextU = 0xfc0;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += (shadeA >> 3) & 0xc0000;
                shadeShift = shadeA >> 23;
            }

            strides = (xB - xA) & 0x7;
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }

                offset++;
                curU += stepU;
                curV += stepV;
            }
        }
    }
    else
    {
        int nextU = 0;
        int nextV = 0;
        int dx = xA - (SCREEN_WIDTH / 2);

        // This should really be (dx >> 3)
        // Since we are making dx>>3 strides (linear interpolate between every 8 pixels)
        u = u + (uStride >> 3) * dx;
        v = v + (vStride >> 3) * dx;
        w = w + (wStride >> 3) * dx;

        // The math gives u, 0-1, but we need 0 - 128. So the result should auto be upshifted by 7
        // And we want the top 7 bits of each to be the texture coord.
        // is this (7 for the texture size) + (7 the top 7 bits)
        int curW = w >> 14;
        if( curW != 0 )
        {
            curU = u / curW;
            curV = v / curW;
            if( curU < 0 )
            {
                curU = 0;
            }
            else if( curU > 0x3f80 )
            {
                curU = 0x3f80;
            }
        }

        // It appears that the uStrides are pre-multiplied by eight (shifted up by << 8 rather than
        // << 5)
        //
        u = u + uStride;
        v = v + vStride;
        w = w + wStride;

        curW = w >> 14;
        if( curW != 0 )
        {
            nextU = u / curW;
            nextV = v / curW;
            if( nextU < 0x7 )
            {
                nextU = 0x7;
            }
            // 0x3f80 top 7 bits of a 14 bit number.
            // 16256
            else if( nextU > 0x3f80 )
            {
                nextU = 0x3f80;
            }
        }

        int stepU = (nextU - curU) >> 3;
        int stepV = (nextV - curV) >> 3;
        curU += shadeA & 0x600000;
        int shadeShift = shadeA >> 23;

        if( opaque )
        {
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 14;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 0x7 )
                    {
                        nextU = 0x7;
                    }
                    else if( nextU > 0x3f80 )
                    {
                        nextU = 0x3f80;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += shadeA & 0x600000;
                shadeShift = shadeA >> 23;
            }

            strides = xB - xA & 0x7;
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;
            }
        }
        else
        {
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 14;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 0x7 )
                    {
                        nextU = 0x7;
                    }
                    else if( nextU > 0x3f80 )
                    {
                        nextU = 0x3f80;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += shadeA & 0x600000;
                shadeShift = shadeA >> 23;
            }

            strides = xB - xA & 0x7;
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }

                offset++;
                curU += stepU;
                curV += stepV;
            }
        }
    }
}
