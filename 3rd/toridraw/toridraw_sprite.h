#ifndef TORIDRAW_SPRITE_H
#define TORIDRAW_SPRITE_H

#include "toridraw_types.h"

#include <stdint.h>

/** RS UI widget spriteAngle / CS2 ANGLE_2D: 65536 = one full turn (not 0-2048). */
#define TORIDRAW_SPRITE_ANGLE_SCALE 65536

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

/** @see ToriDraw_SpriteAlphaClass. */
enum
{
    TORIDRAW_SPRITE_ALPHA_UNKNOWN = 0,
    TORIDRAW_SPRITE_ALPHA_MIXED,
    TORIDRAW_SPRITE_ALPHA_ALL_OPAQUE,
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
    /* Cached "is every pixel a==255", and the buffer it was computed for.
     * @see ToriDraw_SpriteAlphaClass. */
    unsigned char alpha_class;
    uint32_t const* alpha_class_src;
    /*
     * What the alpha byte MEANS, stated by whoever made the pixels.
     *
     * 1: a channel. The producer wrote real coverage -- the cache decoders
     *    (0xFF for a palette colour, 0 for the transparent index), the
     *    inkwell baker, anything with soft edges -- and a coloured pixel with
     *    alpha 0 is transparent. This is what the software blit assumes for
     *    every sprite (its test is `alpha != 0`).
     * 0: a convention, the legacy default. The client drew this pixmap into
     *    an opaque framebuffer and never wrote alpha at all, so a consumer
     *    that needs coverage (a GPU upload) derives it from the colour key:
     *    black is transparent, everything else opaque.
     *
     * A per-pixel guess cannot tell the two apart -- alpha 0 on a yellow
     * pixel is padding in one and paint in the other -- which is how the
     * touch marker came to draw as a yellow rectangle. So it is declared.
     */
    unsigned char alpha_channel;
};

/**
 * Is every pixel of this sprite fully opaque? Computed once, then cached.
 *
 * The blit already walks runs of a==255 and hands each to memcpy, which is the
 * right shape for an icon. But it re-derives that structure on EVERY draw, and
 * the derivation is a scalar load/shift/compare per pixel that runs ahead of a
 * vectorised copy. For chrome — panels, backgrounds, the gameframe borders,
 * which are the large sprites and so most of the blitted area — the answer is
 * always "all of it", and the scan is pure overhead paid fifty times a second
 * to re-learn a property of a static asset.
 *
 * Keyed on the pixel pointer, not just computed once: the outline and shadow
 * builders hand a sprite a different buffer, and a stale "all opaque" on a
 * sprite that grew a transparent border would blit its surround as black.
 */
unsigned char
ToriDraw_SpriteAlphaClass(struct ToriDraw_Sprite* sprite);

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
    toripixel_t* pixel_buffer);

void
ToriDraw2D_BlitSpriteAlpha(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int alpha,
    toripixel_t* pixel_buffer);

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
    toripixel_t* pixel_buffer);

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
    toripixel_t* pixel_buffer);

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
    toripixel_t* pixel_buffer);

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
    toripixel_t* pixel_buffer);

/** BlitSpriteRotatedEx plus an axis-aligned mask sampled over the dest box
 *  (the mask never rotates with the content).
 *
 *  `mask_keep_opaque` picks which side of the mask is the window: 0 = content
 *  shows where the mask is transparent (an OldSchool corner cover, opaque
 *  outside the hole), 1 = where it is opaque (a rev-634 stencil, opaque inside
 *  the window). The two eras ship opposite art in the same widget field. */
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
    toripixel_t* pixel_buffer);

void
ToriDraw2D_BlitSpriteMasked(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_Sprite* mask_sprite,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    toripixel_t* pixel_buffer);

/** RS UI graphic outline (CC_SETOUTLINE / deob SpritePixels.method9420).
 *  Same size as src; outline>=1 black edge, outline>=2 also white. Owned. */
uint32_t*
ToriDraw_SpriteNewGraphicOutline(
    uint32_t const* src,
    int sw,
    int sh,
    int outline,
    int* out_w,
    int* out_h);

/** RS UI graphic shadow (CC_SETGRAPHICSHADOW). Returns new owned buffer or NULL. */
uint32_t*
ToriDraw_SpriteNewGraphicShadow(
    uint32_t const* src,
    int sw,
    int sh,
    int shadow_colour,
    int* out_w,
    int* out_h);

void
ToriDraw_SpriteFlipHorizontal(struct ToriDraw_Sprite* sprite);

void
ToriDraw_SpriteFlipVertical(struct ToriDraw_Sprite* sprite);

/** In-place horizontal/vertical flip and/or rotation of an owned ARGB pixel buffer.
 *  On rotation, replaces *pixels_argb (frees the old buffer) and updates width and height.
 *  angle_r2pi65536 uses TORIDRAW_SPRITE_ANGLE_SCALE (65536 = full turn), not 0-2048. */
void
ToriDraw_SpriteTransformPixels(
    uint32_t** pixels_argb,
    int* width,
    int* height,
    int hflip,
    int vflip,
    int angle_r2pi65536);

void
ToriDraw_SpriteFree(struct ToriDraw_Sprite* sprite);

/** Write sprite ARGB pixels to a 32-bit BMP. Exports the embedded crop rect when set. */
int
ToriDraw_SpriteWriteBmpFile(
    struct ToriDraw_Sprite const* sprite,
    char const* path);

#endif
