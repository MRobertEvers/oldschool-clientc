#include "toridraw_sprite.h"

#include "toridraw_math.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
    if( !pix8 || !palette )
        return NULL;
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
    if( !pix32 || !pix32->pixels )
        return NULL;
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
    if( !pixels_argb || width <= 0 || height <= 0 )
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
    if( !sprite || !view_port || !pixel_buffer )
        return;
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
    if( !sprite || !sprite->pixels_argb || !view_port || !pixel_buffer || src_w <= 0 || src_h <= 0 )
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

    for( int y = 0; y < src_h; y++ )
    {
        int dst_y = y + y_offset;
        if( dst_y < ct || dst_y >= cb )
            continue;
        for( int x = 0; x < src_w; x++ )
        {
            int dst_x = x + x_offset;
            if( dst_x < cl || dst_x >= cr )
                continue;

            int pixel_buffer_index = dst_y * stride + dst_x;
            int sx = src_x + x;
            int sy = src_y + y;
            uint32_t pixel = sprite->pixels_argb[sx + sy * sw];
            if( pixel == 0 )
                continue;

            pixel_buffer[pixel_buffer_index] = (int)pixel;
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
    if( !sprite || !sprite->pixels_argb || !view_port || !pixel_buffer || dst_w <= 0 || dst_h <= 0 )
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
    if( !sprite || !sprite->pixels_argb || !view_port || !pixel_buffer )
        return;

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
    if( !sprite || !sprite->pixels_argb || !view_port || !pixel_buffer )
        return;
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
    if( !sprite || !sprite->pixels_argb || !mask_sprite || !mask_sprite->pixels_argb || !view_port ||
        !pixel_buffer )
        return;

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

void
ToriDraw_SpriteFlipHorizontal(struct ToriDraw_Sprite* sprite)
{
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
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
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
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
ToriDraw_SpriteFree(struct ToriDraw_Sprite* sprite)
{
    if( !sprite )
        return;
    free(sprite->pixels_argb);
    free(sprite);
}
