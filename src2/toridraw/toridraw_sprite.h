#ifndef TORIDRAW_SPRITE_H
#define TORIDRAW_SPRITE_H

#include "toridraw_types.h"

#include <stdint.h>

struct ToriDraw_PixPalette
{
    int* palette;
    int palette_count;
};

struct ToriDraw_Pix8
{
    uint8_t* pixels;
    int width;
    int height;

    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct ToriDraw_Pix32
{
    int* pixels;
    int draw_width;
    int draw_height;

    int crop_x;
    int crop_y;
    int stride_x;
    int stride_y;
};

struct ToriDraw_Sprite
{
    uint32_t* pixels_argb;
    int width;
    int height;
    /* Pix8 crop offset: draw(x,y) blits at (x+crop_x, y+crop_y) per Client.ts Pix8.draw */
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromPix8(
    struct ToriDraw_Pix8* pix8,
    struct ToriDraw_PixPalette* palette);

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromPix32(struct ToriDraw_Pix32* pix32);

/** Takes ownership of pixels_argb (heap ARGB, row-major). */
struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromArgbOwned(
    uint32_t* pixels_argb,
    int width,
    int height);

void
ToriDraw_Pix8Free(struct ToriDraw_Pix8* pix8);

void
ToriDraw_PixpaletteFree(struct ToriDraw_PixPalette* palette);

void
ToriDraw2D_BlitSprite(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int* pixel_buffer);

void
ToriDraw2D_BlitSprite_subrect(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* pixel_buffer);

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
    int* pixel_buffer);

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
    int* pixel_buffer);

/** Inverse-map blit: for each destination pixel, sample source with rotation.
 *  Destination bbox is (dst_x, dst_y, dst_w, dst_h) with pivot (dst_anchor_x, dst_anchor_y).
 *  Source pivot is (src_anchor_x, src_anchor_y) within the sprite sub-rectangle. */
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
    int* pixel_buffer);

void
ToriDraw2D_BlitSpriteMasked(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_Sprite* mask_sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int* pixel_buffer);

void
ToriDraw_SpriteFlipHorizontal(struct ToriDraw_Sprite* sprite);

void
ToriDraw_SpriteFlipVertical(struct ToriDraw_Sprite* sprite);

void
ToriDraw_SpriteFree(struct ToriDraw_Sprite* sprite);

#endif
