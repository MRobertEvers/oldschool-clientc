#ifndef OSRS_TABLES_FLOOR_HEIGHT_FIXUP_H
#define OSRS_TABLES_FLOOR_HEIGHT_FIXUP_H

#include "maps.h"

void fixup_terrain_tile(
    struct CacheMapTerrain* map_terrain, int world_scene_origin_x, int world_scene_origin_y);

#endif // OSRS_TABLES_FLOOR_HEIGHT_FIXUP_H