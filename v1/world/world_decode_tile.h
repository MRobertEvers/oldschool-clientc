#ifndef WORLD_DECODE_TILE_H
#define WORLD_DECODE_TILE_H

#include "toridraw/toridraw_types.h"

#define TERRAIN_OVERLAY_HSL_LIGHTNESS_ONLY -1
#define TERRAIN_OVERLAY_HSL_TRANSPARENT -2
#define TERRAIN_UNDERLAY_HSL_NONE -1

int
terrain_multiply_lightness(
    int hsl,
    int light);

int
terrain_adjust_lightness(
    int hsl,
    int light);

struct ToriDraw_Model*
world_decode_tile(
    int shape,
    int rotation,
    int texture_id,
    int height_sw,
    int height_se,
    int height_ne,
    int height_nw,
    int light_sw,
    int light_se,
    int light_ne,
    int light_nw,
    int underlay_hsl16,
    int overlay_hsl16);

#endif
