#include "toridraw_sprite.h"

#include "bmp.h"
#include "graphics/dash_restrict.h"
#include "toridraw_math.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool
toridraw_sprite_pixel_opaque(uint32_t p)
{
    return ((p >> 24) & 0xFF) != 0;
}

static uint32_t*
ToriDraw_Pix8ToArgb(
    struct ToriDraw_Pix8* pix8,
    struct ToriDraw_PixPalette* palette)
{
    uint32_t* pixels_argb = malloc((size_t)pix8->width * (size_t)pix8->height * sizeof(uint32_t));
    if( !pixels_argb )
        return NULL;
    memset(pixels_argb, 0, (size_t)pix8->width * (size_t)pix8->height * sizeof(uint32_t));

    for( int i = 0; i < pix8->width * pix8->height; i++ )
    {
        int palette_index = pix8->pixels[i];
        assert(palette_index >= 0 && palette_index < palette->palette_count);
        pixels_argb[i] = (uint32_t)palette->palette[palette_index];
    }

    return pixels_argb;
}

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromPix8(
    struct ToriDraw_Pix8* pix8,
    struct ToriDraw_PixPalette* palette)
{
    assert(pix8);
    assert(palette);
    struct ToriDraw_Sprite* sprite = (struct ToriDraw_Sprite*)malloc(sizeof(struct ToriDraw_Sprite));
    if( !sprite )
        return NULL;
    memset(sprite, 0, sizeof(struct ToriDraw_Sprite));
    sprite->pixels_argb = ToriDraw_Pix8ToArgb(pix8, palette);
    if( !sprite->pixels_argb )
    {
        free(sprite);
        return NULL;
    }
    sprite->width = pix8->width;
    sprite->height = pix8->height;
    return sprite;
}

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromPix32(struct ToriDraw_Pix32* pix32)
{
    assert(pix32);
    assert(pix32->pixels);
    struct ToriDraw_Sprite* sprite = (struct ToriDraw_Sprite*)malloc(sizeof(struct ToriDraw_Sprite));
    if( !sprite )
        return NULL;
    memset(sprite, 0, sizeof(struct ToriDraw_Sprite));

    int y = 0;
    int width = pix32->stride_x;
    int height = pix32->stride_y;

    uint32_t* pixels =
        malloc((size_t)pix32->draw_width * (size_t)pix32->draw_height * sizeof(uint32_t));
    if( !pixels )
    {
        free(sprite);
        return NULL;
    }
    memset(pixels, 0, (size_t)pix32->draw_width * (size_t)pix32->draw_height * sizeof(uint32_t));

    if( pix32->stride_y > pix32->draw_height )
        height = pix32->draw_height;

    if( pix32->stride_x > pix32->draw_width )
        width = pix32->draw_width;

    int write_x = 0;
    int write_y = pix32->crop_y;
    for( ; y < height; y++ )
    {
        write_x = pix32->crop_x;
        for( int x = 0; x < width; x++ )
        {
            int pixel_index = x + y * pix32->stride_x;
            int write_index = write_x + write_y * pix32->draw_width;
            if( write_index < 0 || write_index >= pix32->draw_width * pix32->draw_height )
                continue;
            assert(write_index < pix32->draw_width * pix32->draw_height);
            pixels[write_index] = (uint32_t)pix32->pixels[pixel_index];
            write_x++;
        }
        write_y++;
    }

    sprite->pixels_argb = pixels;
    sprite->width = pix32->draw_width;
    sprite->height = pix32->draw_height;
    return sprite;
}

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromArgbOwned(
    uint32_t* pixels_argb,
    int width,
    int height)
{
    assert(pixels_argb);
    if( width <= 0 || height <= 0 )
        return NULL;
    struct ToriDraw_Sprite* sprite = (struct ToriDraw_Sprite*)malloc(sizeof(struct ToriDraw_Sprite));
    if( !sprite )
        return NULL;
    memset(sprite, 0, sizeof(struct ToriDraw_Sprite));
    sprite->pixels_argb = pixels_argb;
    sprite->width = width;
    sprite->height = height;
    sprite->crop_width = width;
    sprite->crop_height = height;
    return sprite;
}

void
ToriDraw_Pix8Free(struct ToriDraw_Pix8* pix8)
{
    if( !pix8 )
        return;
    free(pix8->pixels);
    free(pix8);
}

void
ToriDraw_PixpaletteFree(struct ToriDraw_PixPalette* palette)
{
    if( !palette )
        return;
    free(palette->palette);
    free(palette);
}

void
ToriDraw2D_BlitSprite(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x_offset,
    int y_offset,
    int* pixel_buffer)
{
    assert(sprite);
    assert(view_port);
    assert(pixel_buffer);
    ToriDraw2D_BlitSprite_subrect(
        sprite,
        view_port,
        x_offset,
        y_offset,
        0,
        0,
        sprite->width,
        sprite->height,
        pixel_buffer);
}

static void
sprite_blend_pixel(
    int* dst,
    uint32_t src,
    int alpha)
{
    if( alpha >= 255 )
    {
        *dst = (int)src;
        return;
    }
    if( alpha <= 0 )
        return;

    int const dst_px = *dst;
    int const src_a = (int)(src >> 24) & 0xFF;
    if( src_a == 0 && (src & 0xFFFFFF) == 0 )
        return;

    int const src_r = (int)(src >> 16) & 0xFF;
    int const src_g = (int)(src >> 8) & 0xFF;
    int const src_b = (int)src & 0xFF;
    int const dst_r = (dst_px >> 16) & 0xFF;
    int const dst_g = (dst_px >> 8) & 0xFF;
    int const dst_b = dst_px & 0xFF;

    int const blend = alpha;
    int const inv = 255 - blend;
    /* (v + (v>>8) + 1) >> 8 is exactly v/255 over 0..255*255 - same pixels as
     * the division it replaces, without three idiv per blended pixel. */
    int const rn = (src_r * blend) + (dst_r * inv);
    int const gn = (src_g * blend) + (dst_g * inv);
    int const bn = (src_b * blend) + (dst_b * inv);
    int const r = (rn + (rn >> 8) + 1) >> 8;
    int const g = (gn + (gn >> 8) + 1) >> 8;
    int const b = (bn + (bn >> 8) + 1) >> 8;
    *dst = (int)(0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

void
ToriDraw2D_BlitSpriteAlpha(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x_offset,
    int y_offset,
    int alpha,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);
    if( alpha >= 255 )
    {
        ToriDraw2D_BlitSprite(sprite, view_port, x_offset, y_offset, pixel_buffer);
        return;
    }
    if( alpha <= 0 )
        return;

    int const src_w = sprite->width;
    int const src_h = sprite->height;
    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    int const cl = view_port->clip_left;
    int const ct = view_port->clip_top;
    int const cr = view_port->clip_right;
    int const cb = view_port->clip_bottom;
    int const stride = view_port->stride;

    /* Clamp the iteration range once instead of per-pixel clip rejection. */
    int x_begin = 0;
    int x_stop = src_w;
    if( x_offset < cl )
        x_begin = cl - x_offset;
    if( x_offset + x_stop > cr )
        x_stop = cr - x_offset;

    int y_begin = 0;
    int y_stop = src_h;
    if( y_offset < ct )
        y_begin = ct - y_offset;
    if( y_offset + y_stop > cb )
        y_stop = cb - y_offset;

    if( x_begin >= x_stop || y_begin >= y_stop )
        return;

    for( int y = y_begin; y < y_stop; y++ )
    {
        uint32_t const* RESTRICT srow = sprite->pixels_argb + (size_t)y * src_w;
        int* RESTRICT drow = pixel_buffer + (size_t)(y + y_offset) * stride + x_offset;

        for( int x = x_begin; x < x_stop; x++ )
        {
            uint32_t const pixel = srow[x];
            if( pixel == 0 )
                continue;

            sprite_blend_pixel(&drow[x], pixel, alpha);
        }
    }
}

void
ToriDraw2D_BlitSprite_subrect(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x_offset,
    int y_offset,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);
    if( src_w <= 0 || src_h <= 0 )
        return;
    if( src_x < 0 || src_y < 0 || src_x + src_w > sprite->width || src_y + src_h > sprite->height )
        return;
    /* Client.ts Pix8.draw(x,y): destination is (x + cropX, y + cropY) */
    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;
    int stride = view_port->stride;
    int sw = sprite->width;

    /* Clamp the iteration range once instead of testing four clip edges on
     * every pixel - offscreen rows and columns then cost nothing at all. */
    int x_begin = 0;
    int x_stop = src_w;
    if( x_offset < cl )
        x_begin = cl - x_offset;
    if( x_offset + x_stop > cr )
        x_stop = cr - x_offset;

    int y_begin = 0;
    int y_stop = src_h;
    if( y_offset < ct )
        y_begin = ct - y_offset;
    if( y_offset + y_stop > cb )
        y_stop = cb - y_offset;

    if( x_begin >= x_stop || y_begin >= y_stop )
        return;

    for( int y = y_begin; y < y_stop; y++ )
    {
        uint32_t const* RESTRICT srow = sprite->pixels_argb + (size_t)(src_y + y) * sw + src_x;
        int* RESTRICT drow = pixel_buffer + (size_t)(y + y_offset) * stride + x_offset;

        for( int x = x_begin; x < x_stop; x++ )
        {
            uint32_t pixel = srow[x];
            if( pixel == 0 )
                continue;

            drow[x] = (int)pixel;
        }
    }
}

void
ToriDraw2D_BlitSpriteRotatedEx(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int rotation_r2pi2048,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);
    if( dst_w <= 0 || dst_h <= 0 )
        return;

    int src_crop_x = sprite->crop_x;
    int src_crop_y = sprite->crop_y;
    int src_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
    int src_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
    int src_stride = sprite->width;
    int dst_stride = view_port->stride;
    int dst_buffer_height = view_port->clip_bottom;

    rotation_r2pi2048 = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    int sin = ToriDraw_Sin(rotation_r2pi2048);
    int cos = ToriDraw_Cos(rotation_r2pi2048);

    int min_x = dst_x;
    int min_y = dst_y;
    int max_x = dst_x + dst_w;
    int max_y = dst_y + dst_h;

    if( min_x < view_port->clip_left )
        min_x = view_port->clip_left;
    if( max_x > view_port->clip_right )
        max_x = view_port->clip_right;
    if( min_x >= max_x )
        return;
    if( min_y < view_port->clip_top )
        min_y = view_port->clip_top;
    if( max_y > view_port->clip_bottom )
        max_y = view_port->clip_bottom;
    if( min_y >= max_y )
        return;

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            int rel_x = dst_x_abs - dst_x - dst_anchor_x;
            int rel_y = dst_y_abs - dst_y - dst_anchor_y;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            int sx = src_anchor_x + src_rel_x;
            int sy = src_anchor_y + src_rel_y;

            if( sx >= 0 && sx < src_width && sy >= 0 && sy < src_height )
            {
                int bx = src_crop_x + sx;
                int by = src_crop_y + sy;
                uint32_t src_pixel = sprite->pixels_argb[by * src_stride + bx];
                if( src_pixel != 0 )
                    pixel_buffer[dst_y_abs * dst_stride + dst_x_abs] = (int)src_pixel;
            }
        }
    }
}

void
ToriDraw2D_BlitSpriteRotatedMaskedEx(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_Sprite* mask_sprite,
    int mask_keep_opaque,
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int rotation_r2pi2048,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(mask_sprite);
    assert(mask_sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);
    if( dst_w <= 0 || dst_h <= 0 )
        return;

    int src_crop_x = sprite->crop_x;
    int src_crop_y = sprite->crop_y;
    int src_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
    int src_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
    int src_stride = sprite->width;
    int dst_stride = view_port->stride;

    int mask_crop_x = mask_sprite->crop_x;
    int mask_crop_y = mask_sprite->crop_y;
    int mask_ink_w = mask_sprite->width;
    int mask_ink_h = mask_sprite->height;

    rotation_r2pi2048 = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    int sin = ToriDraw_Sin(rotation_r2pi2048);
    int cos = ToriDraw_Cos(rotation_r2pi2048);

    int min_x = dst_x;
    int min_y = dst_y;
    int max_x = dst_x + dst_w;
    int max_y = dst_y + dst_h;

    if( min_x < view_port->clip_left )
        min_x = view_port->clip_left;
    if( max_x > view_port->clip_right )
        max_x = view_port->clip_right;
    if( min_x >= max_x )
        return;
    if( min_y < view_port->clip_top )
        min_y = view_port->clip_top;
    if( max_y > view_port->clip_bottom )
        max_y = view_port->clip_bottom;
    if( min_y >= max_y )
        return;

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            /* Mask samples axis-aligned over the dest box (never rotates).
             * Trimmed masks: pixels are ink-only placed at (crop_x, crop_y), so
             * anything outside the ink is transparent — which under
             * mask_keep_opaque means outside the ink is also outside the
             * window. */
            int mask_bx = dst_x_abs - dst_x - mask_crop_x;
            int mask_by = dst_y_abs - dst_y - mask_crop_y;
            bool mask_opaque =
                mask_bx >= 0 && mask_bx < mask_ink_w && mask_by >= 0 && mask_by < mask_ink_h &&
                mask_sprite->pixels_argb[mask_by * mask_ink_w + mask_bx] != 0;
            if( mask_keep_opaque ? !mask_opaque : mask_opaque )
                continue;

            int rel_x = dst_x_abs - dst_x - dst_anchor_x;
            int rel_y = dst_y_abs - dst_y - dst_anchor_y;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            int sx = src_anchor_x + src_rel_x;
            int sy = src_anchor_y + src_rel_y;

            if( sx >= 0 && sx < src_width && sy >= 0 && sy < src_height )
            {
                int bx = src_crop_x + sx;
                int by = src_crop_y + sy;
                uint32_t src_pixel = sprite->pixels_argb[by * src_stride + bx];
                if( src_pixel != 0 )
                    pixel_buffer[dst_y_abs * dst_stride + dst_x_abs] = (int)src_pixel;
            }
        }
    }
}

void
ToriDraw2D_BlitSpriteTiled(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    int origin_x,
    int origin_y,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);

    int const sw = sprite->width;
    int const sh = sprite->height;
    if( sw <= 0 || sh <= 0 || rect_w <= 0 || rect_h <= 0 )
        return;

    int const cl = view_port->clip_left;
    int const ct = view_port->clip_top;
    int const cr = view_port->clip_right;
    int const cb = view_port->clip_bottom;
    int const stride = view_port->stride;

    int x0 = rect_x;
    int y0 = rect_y;
    int x1 = rect_x + rect_w;
    int y1 = rect_y + rect_h;
    if( x0 < cl )
        x0 = cl;
    if( y0 < ct )
        y0 = ct;
    if( x1 > cr )
        x1 = cr;
    if( y1 > cb )
        y1 = cb;

    for( int y = y0; y < y1; y++ )
    {
        int sy = y - origin_y;
        sy = ((sy % sh) + sh) % sh;
        int const dst_row = y * stride;
        for( int x = x0; x < x1; x++ )
        {
            int sx = x - origin_x;
            sx = ((sx % sw) + sw) % sw;
            uint32_t const pixel = sprite->pixels_argb[sx + sy * sw];
            if( pixel == 0 )
                continue;
            pixel_buffer[dst_row + x] = (int)pixel;
        }
    }
}

void
ToriDraw2D_BlitSpriteRotated(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int anchor_x,
    int anchor_y,
    int width,
    int height,
    int rotation_r2pi2048,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);
    if( width <= 0 )
        width = sprite->width;
    if( height <= 0 )
        height = sprite->height;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;
    int stride = view_port->stride;
    int sw = sprite->width;

    rotation_r2pi2048 = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    int sin = ToriDraw_Sin(rotation_r2pi2048);
    int cos = ToriDraw_Cos(rotation_r2pi2048);
    int sin_zoom = (sin * 1) >> 8;
    int cos_zoom = (cos * 1) >> 8;

    int center_x = (-width / 2);
    int center_y = (-height / 2);
    int left_x = (anchor_x << 16) + center_y * sin_zoom + center_x * cos_zoom;
    int left_y = (anchor_y << 16) + (center_y * cos_zoom - center_x * sin_zoom);
    int left_off = x + y * stride;

    for( int i = 0; i < height; i++ )
    {
        int dst_off = i * stride;
        int dst_x = left_off + dst_off;
        int src_x = left_x + cos_zoom * dst_off;
        int src_y = left_y - sin_zoom * dst_off;

        for( int j = 0; j < width; j++ )
        {
            int sx = src_x >> 16;
            int sy = src_y >> 16;
            if( sx >= 0 && sy >= 0 && sx < sprite->width && sy < sprite->height )
            {
                int dst_px = dst_x % stride;
                int dst_py = dst_x / stride;
                if( dst_px >= cl && dst_px < cr && dst_py >= ct && dst_py < cb )
                {
                    uint32_t pixel = sprite->pixels_argb[sx + sy * sw];
                    if( pixel != 0 )
                        pixel_buffer[dst_x] = (int)pixel;
                }
            }
            src_x += cos_zoom;
            src_y -= sin_zoom;
            dst_x++;
        }

        left_x += sin_zoom;
        left_y += cos_zoom;
        left_off += stride;
    }
}

void
ToriDraw2D_BlitSpriteMasked(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_Sprite* mask_sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(mask_sprite);
    assert(mask_sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);

    x += sprite->crop_x;
    y += sprite->crop_y;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;
    int stride = view_port->stride;
    int sw = sprite->width;
    int mw = mask_sprite->width;
    int mh = mask_sprite->height;
    int draw_w = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
    int draw_h = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;

    for( int row = 0; row < draw_h; row++ )
    {
        int dst_y = y + row;
        if( dst_y < ct || dst_y >= cb )
            continue;
        for( int col = 0; col < draw_w; col++ )
        {
            int dst_x = x + col;
            if( dst_x < cl || dst_x >= cr )
                continue;
            if( col >= mw || row >= mh )
                continue;
            uint32_t mask_px = mask_sprite->pixels_argb[col + row * mw];
            if( mask_px == 0 )
                continue;
            uint32_t pixel = sprite->pixels_argb[col + row * sw];
            if( pixel == 0 )
                continue;
            pixel_buffer[dst_y * stride + dst_x] = (int)pixel;
        }
    }
}

uint32_t*
ToriDraw_SpriteNewGraphicOutline(
    uint32_t const* src,
    int sw,
    int sh,
    int outline,
    int* out_w,
    int* out_h)
{
    if( outline <= 0 || !src )
        return NULL;

    int pad = outline;
    int dw = sw + pad * 2;
    int dh = sh + pad * 2;
    uint32_t* dst = calloc((size_t)dw * (size_t)dh, sizeof(uint32_t));
    if( !dst )
        return NULL;

    for( int y = 0; y < dh; y++ )
    {
        for( int x = 0; x < dw; x++ )
        {
            int sx = x - pad;
            int sy = y - pad;
            if( sx >= 0 && sx < sw && sy >= 0 && sy < sh &&
                toridraw_sprite_pixel_opaque(src[sy * sw + sx]) )
                dst[y * dw + x] = src[sy * sw + sx];
        }
    }

    for( int pass = 0; pass < (outline >= 2 ? 2 : 1); pass++ )
    {
        uint32_t outline_rgb = pass == 0 ? 0xFF000000u : 0xFFFFFFFFu;
        uint32_t* next = calloc((size_t)dw * (size_t)dh, sizeof(uint32_t));
        if( !next )
            break;
        memcpy(next, dst, (size_t)dw * (size_t)dh * sizeof(uint32_t));

        for( int y = 0; y < dh; y++ )
        {
            for( int x = 0; x < dw; x++ )
            {
                if( toridraw_sprite_pixel_opaque(dst[y * dw + x]) )
                    continue;
                bool neighbor = false;
                for( int oy = -1; oy <= 1 && !neighbor; oy++ )
                {
                    for( int ox = -1; ox <= 1; ox++ )
                    {
                        if( ox == 0 && oy == 0 )
                            continue;
                        int nx = x + ox;
                        int ny = y + oy;
                        if( nx < 0 || ny < 0 || nx >= dw || ny >= dh )
                            continue;
                        if( toridraw_sprite_pixel_opaque(dst[ny * dw + nx]) )
                        {
                            neighbor = true;
                            break;
                        }
                    }
                }
                if( neighbor )
                    next[y * dw + x] = outline_rgb;
            }
        }
        free(dst);
        dst = next;
    }

    *out_w = dw;
    *out_h = dh;
    return dst;
}

uint32_t*
ToriDraw_SpriteNewGraphicShadow(
    uint32_t const* src,
    int sw,
    int sh,
    int shadow_colour,
    int* out_w,
    int* out_h)
{
    if( shadow_colour == 0 || !src )
        return NULL;

    int pad = 2;
    int dw = sw + pad;
    int dh = sh + pad;
    uint32_t* dst = calloc((size_t)dw * (size_t)dh, sizeof(uint32_t));
    if( !dst )
        return NULL;

    int sr = (shadow_colour >> 16) & 0xFF;
    int sg = (shadow_colour >> 8) & 0xFF;
    int sb = shadow_colour & 0xFF;
    uint32_t shadow_px = 0xFF000000u | (uint32_t)(sr << 16) | (uint32_t)(sg << 8) | (uint32_t)sb;

    for( int y = 0; y < sh; y++ )
    {
        for( int x = 0; x < sw; x++ )
        {
            if( !toridraw_sprite_pixel_opaque(src[y * sw + x]) )
                continue;
            int dx = x + 1;
            int dy = y + 1;
            if( dx < dw && dy < dh )
                dst[dy * dw + dx] = shadow_px;
        }
    }

    for( int y = 0; y < sh; y++ )
    {
        for( int x = 0; x < sw; x++ )
        {
            uint32_t p = src[y * sw + x];
            if( toridraw_sprite_pixel_opaque(p) )
                dst[y * dw + x] = p;
        }
    }

    *out_w = dw;
    *out_h = dh;
    return dst;
}

void
ToriDraw_SpriteFlipHorizontal(struct ToriDraw_Sprite* sprite)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    if( sprite->width <= 0 || sprite->height <= 0 )
        return;
    int w = sprite->width;
    int h = sprite->height;
    uint32_t* p = sprite->pixels_argb;
    for( int y = 0; y < h; y++ )
        for( int x = 0; x < (w / 2); x++ )
        {
            int a = x + y * w;
            int b = (w - 1 - x) + y * w;
            uint32_t t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
}

void
ToriDraw_SpriteFlipVertical(struct ToriDraw_Sprite* sprite)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    if( sprite->width <= 0 || sprite->height <= 0 )
        return;
    int w = sprite->width;
    int h = sprite->height;
    uint32_t* p = sprite->pixels_argb;
    for( int y = 0; y < (h / 2); y++ )
        for( int x = 0; x < w; x++ )
        {
            int a = x + y * w;
            int b = x + (h - 1 - y) * w;
            uint32_t t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
}

void
ToriDraw_SpriteTransformPixels(
    uint32_t** pixels_argb,
    int* width,
    int* height,
    int hflip,
    int vflip,
    int angle_r2pi65536)
{
    assert(pixels_argb);
    assert(*pixels_argb);
    assert(width);
    assert(height);
    if( *width <= 0 || *height <= 0 )
        return;

    if( hflip || vflip )
    {
        struct ToriDraw_Sprite tmp = {
            .width = *width,
            .height = *height,
            .pixels_argb = *pixels_argb,
        };
        if( hflip )
            ToriDraw_SpriteFlipHorizontal(&tmp);
        if( vflip )
            ToriDraw_SpriteFlipVertical(&tmp);
    }

    if( angle_r2pi65536 == 0 )
        return;

    double rad = ((double)angle_r2pi65536 * 2.0 * 3.141592653589793) /
                 (double)TORIDRAW_SPRITE_ANGLE_SCALE;
    double c = cos(rad);
    double s = sin(rad);
    int src_w = *width;
    int src_h = *height;
    int cx = src_w / 2;
    int cy = src_h / 2;

    int corners[4][2] = {
        { -cx,            -cy            },
        { src_w - 1 - cx, -cy            },
        { -cx,            src_h - 1 - cy },
        { src_w - 1 - cx, src_h - 1 - cy },
    };
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    for( int i = 0; i < 4; i++ )
    {
        int rx = (int)lround((double)corners[i][0] * c - (double)corners[i][1] * s);
        int ry = (int)lround((double)corners[i][0] * s + (double)corners[i][1] * c);
        if( i == 0 )
        {
            min_x = max_x = rx;
            min_y = max_y = ry;
        }
        else
        {
            if( rx < min_x )
                min_x = rx;
            if( rx > max_x )
                max_x = rx;
            if( ry < min_y )
                min_y = ry;
            if( ry > max_y )
                max_y = ry;
        }
    }

    int dst_w = max_x - min_x + 1;
    int dst_h = max_y - min_y + 1;
    if( dst_w <= 0 || dst_h <= 0 )
        return;

    uint32_t* dst = calloc((size_t)dst_w * (size_t)dst_h, sizeof(uint32_t));
    if( !dst )
        return;

    uint32_t const* src = *pixels_argb;
    for( int dy = 0; dy < dst_h; dy++ )
    {
        for( int dx = 0; dx < dst_w; dx++ )
        {
            double lx = (double)(dx + min_x);
            double ly = (double)(dy + min_y);
            int sx = (int)lround(lx * c + ly * s) + cx;
            int sy = (int)lround(-lx * s + ly * c) + cy;
            if( sx >= 0 && sx < src_w && sy >= 0 && sy < src_h )
                dst[dx + dy * dst_w] = src[sx + sy * src_w];
        }
    }

    free(*pixels_argb);
    *pixels_argb = dst;
    *width = dst_w;
    *height = dst_h;
}

void
ToriDraw_SpriteFree(struct ToriDraw_Sprite* sprite)
{
    if( !sprite )
        return;
    free(sprite->pixels_argb);
    free(sprite);
}

int
ToriDraw_SpriteWriteBmpFile(
    struct ToriDraw_Sprite const* sprite,
    char const* path)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(path);
    if( sprite->width <= 0 || sprite->height <= 0 )
        return -1;

    int src_x = 0;
    int src_y = 0;
    int out_w = sprite->width;
    int out_h = sprite->height;

    if( sprite->crop_width > 0 &&
        (sprite->crop_width < sprite->width || sprite->crop_height < sprite->height) )
    {
        src_x = sprite->crop_x;
        src_y = sprite->crop_y;
        out_w = sprite->crop_width;
        out_h = sprite->crop_height;
        if( src_x < 0 )
            src_x = 0;
        if( src_y < 0 )
            src_y = 0;
        if( src_x + out_w > sprite->width )
            out_w = sprite->width - src_x;
        if( src_y + out_h > sprite->height )
            out_h = sprite->height - src_y;
    }

    if( out_w <= 0 || out_h <= 0 )
        return -1;

    if( src_x == 0 && src_y == 0 && out_w == sprite->width && out_h == sprite->height )
    {
        bmp_write_file(path, (int*)sprite->pixels_argb, out_w, out_h);
        return 0;
    }

    int* pixels = malloc((size_t)out_w * (size_t)out_h * sizeof(int));
    if( !pixels )
        return -1;

    int const src_stride = sprite->width;
    for( int y = 0; y < out_h; y++ )
    {
        int const src_row = src_y + y;
        int const dst_row = y * out_w;
        for( int x = 0; x < out_w; x++ )
            pixels[dst_row + x] = (int)sprite->pixels_argb[src_x + x + src_row * src_stride];
    }

    bmp_write_file(path, pixels, out_w, out_h);
    free(pixels);
    return 0;
}
