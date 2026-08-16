#ifndef DASH_MINIMAP_H
#define DASH_MINIMAP_H

#include "osrs/minimap.h"

/** Raster minimap tile commands into an ARGB buffer using Dash 2D helpers (4 px / tile). */
void
dash_minimap_raster_tile_commands(
    struct Minimap* minimap,
    struct MinimapRenderCommandBuffer* tile_commands,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int* dst_pixels,
    int dst_stride,
    int dst_width,
    int dst_height);

#endif
