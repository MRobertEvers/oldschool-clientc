#ifndef SCENE_LOC_H
#define SCENE_LOC_H

#include "cache.h"
#include "tables/maps.h"
#include "tables/model.h"

// Todo remove
#include "tables/config_locs.h"

struct SceneLoc
{
    int* model_ids;
    struct CacheModel** models;
    int model_count;

    int region_x;
    int region_y;
    int region_z;

    int orientation;
    int offset_x;
    int offset_y;
    int offset_height;
    int mirrored;

    int chunk_pos_x;
    int chunk_pos_y;
    int chunk_pos_level;

    int size_x;
    int size_y;

    // TODO: Remove this
    bool __drawn;
    struct CacheConfigLocation __loc;
};

struct SceneLocs
{
    struct SceneLoc* locs;
    int locs_count;
};

struct SceneLocs* scene_locs_new_from_map_locs(
    struct CacheMapTerrain* terrain, struct CacheMapLocs* map_locs, struct Cache* cache);

#endif