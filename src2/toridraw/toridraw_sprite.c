#include "toridraw_sprite.h"

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
