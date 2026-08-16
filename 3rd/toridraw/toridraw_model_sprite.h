#ifndef TORIDRAW_MODEL_SPRITE_H
#define TORIDRAW_MODEL_SPRITE_H

#include "toridraw_types.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;
struct ToriDraw_Sprite;

/** Rigid transform + zoom used by interface model previews (inventory, IF). */
struct ToriDraw_WidgetModelTransform
{
    int var2;
    int var3;
    int var5;
    int var6;
    int var7;
    int var10;
    int var11;
    int var12;
    int var13;
    int var14;
    int var15;
    int var16;
    int var17;
    int zoom2d;
    int zoom3d;
    bool orthographic;
};

void
ToriDraw_WidgetModelTransformInit(
    struct ToriDraw_WidgetModelTransform* xf,
    int zoom,
    int xan,
    int yan,
    int zan,
    int orientation,
    int model_offset,
    int model_center_y,
    bool orthographic,
    bool fixed_zoom);

/** Orthographic sprite extents + blit offsets; perspective path does not need them. */
bool
ToriDraw_WidgetModelBounds(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_WidgetModelTransform const* xf,
    int* out_width,
    int* out_height,
    int* out_dx,
    int* out_dy);

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
 *  out_draw_x/out_draw_y are the canvas top-left of the tight extents raster area.
 *  orthographic: 1 = orthographic projection; 0 = perspective (objRender).
 *  fixed_zoom: 1 = zoom3d uses zoom (drawModel2DAtZoom); 0 = fixed 512 (drawModel2D). */
bool
ToriDraw_RenderModelExtentsAtWidget(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int orientation,
    int model_offset,
    int model_center_y,
    bool orthographic,
    bool fixed_zoom,
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

/**
 * Selected-item outline pass (reference ObjType.getSprite outlineRgb > 0): the
 * value-1 silhouette border, then a solid `outline_argb` ring one pixel outside
 * it — and, unlike ObjIconOutline, no drop shadow. Used for the white
 * (0xFFFFFFFF) highlight on the item armed for "Use".
 */
void
ToriDraw_SpritePostprocessObjIconOutlineColor(
    uint32_t* pixels_argb,
    int width,
    int height,
    uint32_t outline_argb);

#endif
