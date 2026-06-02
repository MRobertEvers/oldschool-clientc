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
toridraw_sprite_new_from_pix8(
    struct ToriDraw_Pix8* pix8,
    struct ToriDraw_PixPalette* palette);

struct ToriDraw_Sprite*
toridraw_sprite_new_from_pix32(struct ToriDraw_Pix32* pix32);

/** Takes ownership of pixels_argb (heap ARGB, row-major). */
struct ToriDraw_Sprite*
toridraw_sprite_new_from_argb_owned(
    uint32_t* pixels_argb,
    int width,
    int height);

void
toridraw_pix8_free(struct ToriDraw_Pix8* pix8);

void
toridraw_pixpalette_free(struct ToriDraw_PixPalette* palette);

void
toridraw2d_blit_sprite(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int* pixel_buffer);

void
toridraw2d_blit_sprite_subrect(
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
toridraw_sprite_flip_horizontal(struct ToriDraw_Sprite* sprite);

void
toridraw_sprite_flip_vertical(struct ToriDraw_Sprite* sprite);

void
toridraw_sprite_free(struct ToriDraw_Sprite* sprite);

#endif
