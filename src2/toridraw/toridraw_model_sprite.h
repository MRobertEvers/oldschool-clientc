#ifndef TORIDRAW_MODEL_SPRITE_H
#define TORIDRAW_MODEL_SPRITE_H

#include "toridraw_types.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;
struct ToriDraw_Sprite;

/** Result of extents model raster: tight sprite plus center-relative blit origin. */
struct ToriDraw_ModelExtentsRaster
{
    struct ToriDraw_Sprite* sprite;
    int offset_x;
    int offset_y;
};

/** Rasterize a model into projected extents (tight bbox + border), not widget WxH.
 *  offset_x/offset_y are the sprite origin relative to the widget center (official IF3 model blit). */
struct ToriDraw_ModelExtentsRaster
ToriDraw_SpriteNewFromModelRasterExtents(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int offset_x,
    int offset_y,
    bool orthographic,
    bool postprocess_outline);

/** Rasterize widget-model extents directly into a canvas buffer via RenderModel2/3.
 *  out_draw_x/out_draw_y are the canvas top-left of the tight extents raster area. */
bool
ToriDraw_RenderModelExtentsAtWidget(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int offset_x,
    int offset_y,
    bool orthographic,
    toripixel_t* pixels,
    int stride,
    int canvas_w,
    int canvas_h,
    int widget_x,
    int widget_y,
    int widget_w,
    int widget_h,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom,
    int* out_draw_x,
    int* out_draw_y,
    int* out_width,
    int* out_height);

/** Rasterize a model into a new ToriDraw_Sprite (heap-owned ARGB). Uses widget preview Y placement. */
struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromModelRaster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int width,
    int height,
    bool postprocess_outline);

/** Rasterize a model for RS inventory/obj icons (offsets, roll, obj-icon Y placement). */
struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromObjIconRaster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int xof,
    int yof,
    int width,
    int height,
    bool postprocess_outline);

/** RS inventory/obj icon 1-pixel outline pass on raw ARGB buffer. */
void
ToriDraw_SpritePostprocessObjIconOutline(
    uint32_t* pixels_argb,
    int width,
    int height);

#endif
