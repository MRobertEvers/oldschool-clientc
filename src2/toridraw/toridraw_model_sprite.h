#ifndef TORIDRAW_MODEL_SPRITE_H
#define TORIDRAW_MODEL_SPRITE_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;
struct ToriDraw_Sprite;

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
