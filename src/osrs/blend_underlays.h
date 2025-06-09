#ifndef BLEND_UNDERLAYS_H
#define BLEND_UNDERLAYS_H

#include "scene_tile.h"
#include "tables/config_floortype.h"
#include "tables/maps.h"

#define COLOR_COORD(x, y) ((x) * MAP_TERRAIN_Y + (y))

int* blend_underlays(
    struct MapTerrain* map_terrain,
    struct Underlay* underlays,
    int* underlay_ids,
    int underlays_count,
    int level);

#endif