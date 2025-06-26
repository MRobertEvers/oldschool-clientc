#ifndef SCENE_LOC_H
#define SCENE_LOC_H

#include "cache.h"
#include "tables/maps.h"
#include "tables/model.h"

struct SceneLoc
{
    struct Model** models;
    int model_count;

    int world_x;
    int world_y;
    int world_z;
};

struct SceneLocs
{
    struct SceneLoc* locs;
    int locs_count;
};

struct SceneLocs* scene_locs_new_from_map_locs(
    struct MapTerrain* terrain, struct MapLocs* map_locs, struct Cache* cache);

#endif