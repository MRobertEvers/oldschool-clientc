#ifndef BLEND_UNDERLAYS_H
#define BLEND_UNDERLAYS_H

#include "configmap.h"
#include "scene_tile.h"
#include "tables/maps.h"

#define COLOR_COORD(x, z) ((x) * MAP_TERRAIN_Z + (z))

// int* blend_underlays_runelite(
//     struct CacheMapTerrain* map_terrain,
//     struct CacheConfigUnderlay* underlays,
//     int* underlay_ids,
//     int underlays_count,
//     int level);

int* blend_underlays(
    struct CacheMapTerrainIter* terrain, struct ConfigMap* config_underlay_map, int level);

#endif