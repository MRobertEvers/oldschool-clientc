#ifndef SCENE_H
#define SCENE_H

#include "cache.h"
#include "scene_loc.h"
#include "scene_tile.h"

struct GridTile
{
    struct SceneLoc locs[10];
    int locs_length;

    struct SceneTile tile;

    struct SceneTextures* textures;
    int textures_length;
};

struct Scene
{
    struct GridTile* grid_tiles;
    int grid_tiles_length;
};

struct Scene* scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y);

#endif