#include "toridraw_2d.h"

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

    int d = pixel_buffer[y * stride + x];
    int dr = (d >> 16) & 0xFF;
    int dg = (d >> 8) & 0xFF;
    int db = d & 0xFF;
    int sr = (argb >> 16) & 0xFF;
    int sg = (argb >> 8) & 0xFF;
    int sb = argb & 0xFF;
    int rr = (sr * a + dr * (255 - a)) / 255;
    int rg = (sg * a + dg * (255 - a)) / 255;
    int rb = (sb * a + db * (255 - a)) / 255;
    pixel_buffer[y * stride + x] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
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

    int a = (argb >> 24) & 0xFF;
    for( int y = y0; y < y1; y++ )
    {
        for( int x = x0; x < x1; x++ )
        {
            if( a >= 255 )
                pixel_buffer[y * stride + x] = argb;
            else
                ToriDraw2D_BlendArgbPixel(view_port, x, y, argb, pixel_buffer);
        }
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

    for( int y = 0; y < dst_h; y++ )
    {
        int sy = (y * src_h) / dst_h;
        if( sy >= src_h )
            sy = src_h - 1;
        int dsty = dst_y + y;
        for( int x = 0; x < dst_w; x++ )
        {
            int sx = (x * src_w) / dst_w;
            if( sx >= src_w )
                sx = src_w - 1;
            ToriDraw2D_BlendArgbPixel(
                view_port, dst_x + x, dsty, (int)src[sy * src_w + sx], pixel_buffer);
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

    for( int y = y0; y < y1; y++ )
    {
        int sy = y - origin_y;
        sy = ((sy % src_h) + src_h) % src_h;
        for( int x = x0; x < x1; x++ )
        {
            int sx = x - origin_x;
            sx = ((sx % src_w) + src_w) % src_w;
            ToriDraw2D_BlendArgbPixel(view_port, x, y, (int)src[sy * src_w + sx], pixel_buffer);
        }
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

    for( int y = 0; y < dst_h; y++ )
    {
        int dst_y_abs = dst_y + y;
        int my = (y * mask_h) / dst_h;
        if( my >= mask_h )
            my = mask_h - 1;
        for( int x = 0; x < dst_w; x++ )
        {
            int dst_x_abs = dst_x + x;
            int mx = (x * mask_w) / dst_w;
            if( mx >= mask_w )
                mx = mask_w - 1;

            uint32_t mask_px = mask[my * mask_w + mx];
            if( toridraw2d_argb_alpha(mask_px) == 0 )
                continue;

            int cx = (x * content_w) / dst_w;
            int cy = (y * content_h) / dst_h;
            if( cx >= content_w )
                cx = content_w - 1;
            if( cy >= content_h )
                cy = content_h - 1;

            uint32_t content_px = content[cy * content_w + cx];
            if( toridraw2d_argb_alpha(content_px) == 0 )
                continue;

            ToriDraw2D_BlendArgbPixel(view_port, dst_x_abs, dst_y_abs, (int)content_px, pixel_buffer);
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

    for( int y = 0; y < dst_h; y++ )
    {
        int dst_y_abs = dst_y + y;
        int my = (y * mask_h) / dst_h;
        if( my >= mask_h )
            my = mask_h - 1;
        for( int x = 0; x < dst_w; x++ )
        {
            int dst_x_abs = dst_x + x;
            int mx = (x * mask_w) / dst_w;
            if( mx >= mask_w )
                mx = mask_w - 1;

            uint32_t mask_px = mask[my * mask_w + mx];
            if( toridraw2d_argb_alpha(mask_px) > 127 )
                continue;

            int cx = (x * content_w) / dst_w;
            int cy = (y * content_h) / dst_h;
            if( cx >= content_w )
                cx = content_w - 1;
            if( cy >= content_h )
                cy = content_h - 1;

            uint32_t content_px = content[cy * content_w + cx];
            if( toridraw2d_argb_alpha(content_px) == 0 )
                continue;

            ToriDraw2D_BlendArgbPixel(view_port, dst_x_abs, dst_y_abs, (int)content_px, pixel_buffer);
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
