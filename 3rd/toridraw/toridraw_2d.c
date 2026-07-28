#include "toridraw_2d.h"

#include "graphics/dash_restrict.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static int
toridraw2d_argb_alpha(uint32_t p)
{
    return (int)((p >> 24) & 0xFF);
}

static int
toridraw2d_lerp_channel(
    int a,
    int b,
    int t,
    int denom)
{
    if( denom <= 0 )
        return a;
    return a + (b - a) * t / denom;
}

/**
 * Exact replacement for `v / 255` over the range a channel blend can produce
 * (0 .. 255*255). Bit-identical to the division for every input, verified
 * exhaustively - so the blend paths below stay pixel-for-pixel what they were
 * while dropping three integer divides per pixel.
 */
static inline int
toridraw2d_div255(int v)
{
    return (v + (v >> 8) + 1) >> 8;
}

/**
 * Blend one ARGB source over one destination pixel with the alpha already
 * split into a/inv and the source channels pre-extracted, so the caller can
 * hoist all of that out of its inner loop.
 */
static inline int
toridraw2d_blend_channels(
    int dst,
    int sr,
    int sg,
    int sb,
    int a,
    int inv)
{
    int const dr = (dst >> 16) & 0xFF;
    int const dg = (dst >> 8) & 0xFF;
    int const db = dst & 0xFF;
    int const rr = toridraw2d_div255((sr * a) + (dr * inv));
    int const rg = toridraw2d_div255((sg * a) + (dg * inv));
    int const rb = toridraw2d_div255((sb * a) + (db * inv));
    return (int)0xFF000000 | (rr << 16) | (rg << 8) | rb;
}

void
ToriDraw2D_BlendArgbPixel(
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int argb,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);

    int const clip_left = view_port->clip_left;
    int const clip_top = view_port->clip_top;
    int const clip_right = view_port->clip_right;
    int const clip_bottom = view_port->clip_bottom;
    int const stride = view_port->stride;

    if( x < clip_left || y < clip_top || x >= clip_right || y >= clip_bottom )
        return;

    int a = (argb >> 24) & 0xFF;
    if( a == 0 )
        return;

    if( a == 255 )
    {
        pixel_buffer[y * stride + x] = (argb & 0x00FFFFFF) | 0xFF000000;
        return;
    }

    int* slot = &pixel_buffer[y * stride + x];
    *slot = toridraw2d_blend_channels(
        *slot, (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, a, 255 - a);
}

void
ToriDraw2D_FillRect(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);

    int const clip_left = view_port->clip_left;
    int const clip_top = view_port->clip_top;
    int const clip_right = view_port->clip_right;
    int const clip_bottom = view_port->clip_bottom;
    int const stride = view_port->stride;

    if( x0 < clip_left )
        x0 = clip_left;
    if( y0 < clip_top )
        y0 = clip_top;
    if( x1 > clip_right )
        x1 = clip_right;
    if( y1 > clip_bottom )
        y1 = clip_bottom;

    if( x0 >= x1 || y0 >= y1 )
        return;

    int const a = (argb >> 24) & 0xFF;
    if( a == 0 )
        return;

    /* The rect is already clipped; the old per-pixel ToriDraw2D_BlendArgbPixel
     * call re-tested all four clip edges and recomputed y*stride+x up to three
     * times per pixel. Row base and opacity are loop-invariant - hoist both. */
    if( a >= 255 )
    {
        for( int y = y0; y < y1; y++ )
        {
            int* RESTRICT row = pixel_buffer + (size_t)y * stride;
            for( int x = x0; x < x1; x++ )
                row[x] = argb;
        }
        return;
    }

    int const inv = 255 - a;
    int const sr = (argb >> 16) & 0xFF;
    int const sg = (argb >> 8) & 0xFF;
    int const sb = argb & 0xFF;

    for( int y = y0; y < y1; y++ )
    {
        int* RESTRICT row = pixel_buffer + (size_t)y * stride;
        for( int x = x0; x < x1; x++ )
            row[x] = toridraw2d_blend_channels(row[x], sr, sg, sb, a, inv);
    }
}

void
ToriDraw2D_FillRectGradientVertical(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_top,
    int color_bot,
    int alpha,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);

    int h = y1 - y0;
    if( h <= 0 )
        return;

    for( int y = y0; y < y1; y++ )
    {
        int t = y - y0;
        int r = toridraw2d_lerp_channel((color_top >> 16) & 0xFF, (color_bot >> 16) & 0xFF, t, h);
        int g = toridraw2d_lerp_channel((color_top >> 8) & 0xFF, (color_bot >> 8) & 0xFF, t, h);
        int b = toridraw2d_lerp_channel(color_top & 0xFF, color_bot & 0xFF, t, h);
        int argb = (alpha << 24) | (r << 16) | (g << 8) | b;
        ToriDraw2D_FillRect(view_port, x0, y, x1, y + 1, argb, pixel_buffer);
    }
}

void
ToriDraw2D_FillRectGradientAlpha(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_top,
    int color_bot,
    int alpha_top,
    int alpha_bot,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);

    int h = y1 - y0;
    if( h <= 0 )
        return;

    for( int y = y0; y < y1; y++ )
    {
        int t = y - y0;
        int r = toridraw2d_lerp_channel((color_top >> 16) & 0xFF, (color_bot >> 16) & 0xFF, t, h);
        int g = toridraw2d_lerp_channel((color_top >> 8) & 0xFF, (color_bot >> 8) & 0xFF, t, h);
        int b = toridraw2d_lerp_channel(color_top & 0xFF, color_bot & 0xFF, t, h);
        int a = toridraw2d_lerp_channel(alpha_top, alpha_bot, t, h);
        int argb = (a << 24) | (r << 16) | (g << 8) | b;
        ToriDraw2D_FillRect(view_port, x0, y, x1, y + 1, argb, pixel_buffer);
    }
}

void
ToriDraw2D_DrawRectOutline(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb,
    int* pixel_buffer)
{
    ToriDraw2D_FillRect(view_port, x0, y0, x1, y0 + 1, argb, pixel_buffer);
    ToriDraw2D_FillRect(view_port, x0, y1 - 1, x1, y1, argb, pixel_buffer);
    ToriDraw2D_FillRect(view_port, x0, y0, x0 + 1, y1, argb, pixel_buffer);
    ToriDraw2D_FillRect(view_port, x1 - 1, y0, x1, y1, argb, pixel_buffer);
}

void
ToriDraw2D_DrawLine(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    int argb,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);

    if( thickness < 1 )
        thickness = 1;

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0;
    int y = y0;

    while( true )
    {
        int half = thickness / 2;
        ToriDraw2D_FillRect(
            view_port,
            x - half,
            y - half,
            x - half + thickness,
            y - half + thickness,
            argb,
            pixel_buffer);

        if( x == x1 && y == y1 )
            break;

        int e2 = err * 2;
        if( e2 > -dy )
        {
            err -= dy;
            x += sx;
        }
        if( e2 < dx )
        {
            err += dx;
            y += sy;
        }
    }
}

void
ToriDraw2D_BlitArgb(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    uint32_t const* src,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(src);
    if( src_w <= 0 || src_h <= 0 )
        return;

    for( int y = 0; y < src_h; y++ )
    {
        int sy = dst_y + y;
        for( int x = 0; x < src_w; x++ )
            ToriDraw2D_BlendArgbPixel(view_port, dst_x + x, sy, (int)src[y * src_w + x], pixel_buffer);
    }
}

void
ToriDraw2D_BlitArgbScaled(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(src);
    if( src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 )
        return;

    /* Exact incremental replacement for the per-pixel (x * src_w) / dst_w.
     * Carrying the remainder reproduces floor(x * src_w / dst_w) for every x
     * with adds and compares only - a 16.16 DDA would not, because the
     * truncated step drifts one unit low wherever dst_w divides x * src_w. */
    int sy = 0;
    int y_rem = 0;
    for( int y = 0; y < dst_h; y++ )
    {
        int sy_clamped = sy >= src_h ? src_h - 1 : sy;
        int dsty = dst_y + y;
        uint32_t const* srow = src + (size_t)sy_clamped * src_w;

        int sx = 0;
        int x_rem = 0;
        for( int x = 0; x < dst_w; x++ )
        {
            int sx_clamped = sx >= src_w ? src_w - 1 : sx;
            ToriDraw2D_BlendArgbPixel(
                view_port, dst_x + x, dsty, (int)srow[sx_clamped], pixel_buffer);

            x_rem += src_w;
            while( x_rem >= dst_w )
            {
                x_rem -= dst_w;
                sx++;
            }
        }

        y_rem += src_h;
        while( y_rem >= dst_h )
        {
            y_rem -= dst_h;
            sy++;
        }
    }
}

void
ToriDraw2D_BlitArgbTiled(
    struct ToriDraw_ViewPort* view_port,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int origin_x,
    int origin_y,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(src);
    if( src_w <= 0 || src_h <= 0 || rect_w <= 0 || rect_h <= 0 )
        return;

    int const clip_left = view_port->clip_left;
    int const clip_top = view_port->clip_top;
    int const clip_right = view_port->clip_right;
    int const clip_bottom = view_port->clip_bottom;

    int x0 = rect_x < clip_left ? clip_left : rect_x;
    int y0 = rect_y < clip_top ? clip_top : rect_y;
    int x1 = rect_x + rect_w;
    int y1 = rect_y + rect_h;
    if( x1 > clip_right )
        x1 = clip_right;
    if( y1 > clip_bottom )
        y1 = clip_bottom;

    /* One modulo at the top of the rect, then increment-and-wrap: the source
     * coordinate advances by exactly 1 per pixel, so the two per-pixel
     * divides the modulo pair compiled to are pure waste. */
    int sy = (((y0 - origin_y) % src_h) + src_h) % src_h;
    int const sx0 = (((x0 - origin_x) % src_w) + src_w) % src_w;

    for( int y = y0; y < y1; y++ )
    {
        uint32_t const* srow = src + (size_t)sy * src_w;
        int sx = sx0;
        for( int x = x0; x < x1; x++ )
        {
            ToriDraw2D_BlendArgbPixel(view_port, x, y, (int)srow[sx], pixel_buffer);
            if( ++sx == src_w )
                sx = 0;
        }
        if( ++sy == src_h )
            sy = 0;
    }
}

void
ToriDraw2D_BlitArgbMasked(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_w,
    int mask_h,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(content);
    assert(mask);
    if( dst_w <= 0 || dst_h <= 0 || mask_w <= 0 || mask_h <= 0 || content_w <= 0 ||
        content_h <= 0 )
        return;

    /* Exact incremental source stepping (see ToriDraw2D_BlitArgbScaled) in
     * place of three integer divides per pixel - and `cy` was loop-invariant
     * in x to begin with. */
    int my = 0;
    int my_rem = 0;
    int cy = 0;
    int cy_rem = 0;

    for( int y = 0; y < dst_h; y++ )
    {
        int dst_y_abs = dst_y + y;
        int my_c = my >= mask_h ? mask_h - 1 : my;
        int cy_c = cy >= content_h ? content_h - 1 : cy;
        uint32_t const* mask_row = mask + (size_t)my_c * mask_w;
        uint32_t const* content_row = content + (size_t)cy_c * content_w;

        int mx = 0;
        int mx_rem = 0;
        int cx = 0;
        int cx_rem = 0;

        for( int x = 0; x < dst_w; x++ )
        {
            int mx_c = mx >= mask_w ? mask_w - 1 : mx;
            int cx_c = cx >= content_w ? content_w - 1 : cx;

            mx_rem += mask_w;
            while( mx_rem >= dst_w )
            {
                mx_rem -= dst_w;
                mx++;
            }
            cx_rem += content_w;
            while( cx_rem >= dst_w )
            {
                cx_rem -= dst_w;
                cx++;
            }

            uint32_t mask_px = mask_row[mx_c];
            if( toridraw2d_argb_alpha(mask_px) == 0 )
                continue;

            uint32_t content_px = content_row[cx_c];
            if( toridraw2d_argb_alpha(content_px) == 0 )
                continue;

            ToriDraw2D_BlendArgbPixel(
                view_port, dst_x + x, dst_y_abs, (int)content_px, pixel_buffer);
        }

        my_rem += mask_h;
        while( my_rem >= dst_h )
        {
            my_rem -= dst_h;
            my++;
        }
        cy_rem += content_h;
        while( cy_rem >= dst_h )
        {
            cy_rem -= dst_h;
            cy++;
        }
    }
}

void
ToriDraw2D_BlitArgbMaskedInverted(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_w,
    int mask_h,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(content);
    assert(mask);
    if( dst_w <= 0 || dst_h <= 0 || mask_w <= 0 || mask_h <= 0 || content_w <= 0 ||
        content_h <= 0 )
        return;

    /* Exact incremental source stepping (see ToriDraw2D_BlitArgbScaled) in
     * place of three integer divides per pixel. */
    int my = 0;
    int my_rem = 0;
    int cy = 0;
    int cy_rem = 0;

    for( int y = 0; y < dst_h; y++ )
    {
        int dst_y_abs = dst_y + y;
        int my_c = my >= mask_h ? mask_h - 1 : my;
        int cy_c = cy >= content_h ? content_h - 1 : cy;
        uint32_t const* mask_row = mask + (size_t)my_c * mask_w;
        uint32_t const* content_row = content + (size_t)cy_c * content_w;

        int mx = 0;
        int mx_rem = 0;
        int cx = 0;
        int cx_rem = 0;

        for( int x = 0; x < dst_w; x++ )
        {
            int mx_c = mx >= mask_w ? mask_w - 1 : mx;
            int cx_c = cx >= content_w ? content_w - 1 : cx;

            mx_rem += mask_w;
            while( mx_rem >= dst_w )
            {
                mx_rem -= dst_w;
                mx++;
            }
            cx_rem += content_w;
            while( cx_rem >= dst_w )
            {
                cx_rem -= dst_w;
                cx++;
            }

            uint32_t mask_px = mask_row[mx_c];
            if( toridraw2d_argb_alpha(mask_px) > 127 )
                continue;

            uint32_t content_px = content_row[cx_c];
            if( toridraw2d_argb_alpha(content_px) == 0 )
                continue;

            ToriDraw2D_BlendArgbPixel(
                view_port, dst_x + x, dst_y_abs, (int)content_px, pixel_buffer);
        }

        my_rem += mask_h;
        while( my_rem >= dst_h )
        {
            my_rem -= dst_h;
            my++;
        }
        cy_rem += content_h;
        while( cy_rem >= dst_h )
        {
            cy_rem -= dst_h;
            cy++;
        }
    }
}

void
ToriDraw2D_BlitArgbRotatedMaskedInverted(
    struct ToriDraw_ViewPort* view_port,
    int mask_x,
    int mask_y,
    int mask_w,
    int mask_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_sw,
    int mask_sh,
    int angle,
    int angle_scale,
    int alpha,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(content);
    assert(mask);
    if( mask_w <= 0 || mask_h <= 0 || content_w <= 0 || content_h <= 0 || mask_sw <= 0 ||
        mask_sh <= 0 )
        return;

    double rad = 0.0;
    if( angle != 0 && angle_scale > 0 )
        rad = ((double)angle * 2.0 * 3.141592653589793) / (double)angle_scale;

    double cos_a = cos(rad);
    double sin_a = sin(rad);
    int cx = mask_x + mask_w / 2;
    int cy = mask_y + mask_h / 2;
    int content_cx = content_w / 2;
    int content_cy = content_h / 2;

    int const clip_left = view_port->clip_left;
    int const clip_top = view_port->clip_top;
    int const clip_right = view_port->clip_right;
    int const clip_bottom = view_port->clip_bottom;

    int x0 = mask_x < clip_left ? clip_left : mask_x;
    int y0 = mask_y < clip_top ? clip_top : mask_y;
    int x1 = mask_x + mask_w;
    int y1 = mask_y + mask_h;
    if( x1 > clip_right )
        x1 = clip_right;
    if( y1 > clip_bottom )
        y1 = clip_bottom;

    for( int py = y0; py < y1; py++ )
    {
        int my = ((py - mask_y) * mask_sh) / mask_h;
        if( my < 0 )
            my = 0;
        else if( my >= mask_sh )
            my = mask_sh - 1;

        for( int px = x0; px < x1; px++ )
        {
            int mx = ((px - mask_x) * mask_sw) / mask_w;
            if( mx < 0 )
                mx = 0;
            else if( mx >= mask_sw )
                mx = mask_sw - 1;

            uint32_t mask_px = mask[my * mask_sw + mx];
            if( toridraw2d_argb_alpha(mask_px) > 127 )
                continue;

            double lx = (double)(px - cx);
            double ly = (double)(py - cy);
            double ux = lx * cos_a + ly * sin_a;
            double uy = -lx * sin_a + ly * cos_a;
            int csx = (int)lround((double)content_cx + ux);
            int csy = (int)lround((double)content_cy + uy);
            if( csx < 0 || csy < 0 || csx >= content_w || csy >= content_h )
                continue;

            uint32_t content_px = content[csy * content_w + csx];
            if( toridraw2d_argb_alpha(content_px) == 0 )
                continue;

            if( alpha < 255 )
            {
                int a = toridraw2d_argb_alpha(content_px);
                a = (a * alpha) / 255;
                content_px = (content_px & 0x00FFFFFFu) | ((uint32_t)a << 24);
            }

            ToriDraw2D_BlendArgbPixel(view_port, px, py, (int)content_px, pixel_buffer);
        }
    }
}
