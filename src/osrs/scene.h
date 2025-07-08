#ifndef SCENE_H
#define SCENE_H

#include "cache.h"
#include "scene_loc.h"
#include "scene_tile.h"

/**
 * Tells the renderer to defer drawing locs until
 * the underlay is drawn for tiles in the direction
 * of the span.
 */
enum SpanFlag
{
    SPAN_FLAG_WEST = 1 << 0,
    SPAN_FLAG_NORTH = 1 << 1,
    SPAN_FLAG_EAST = 1 << 2,
    SPAN_FLAG_SOUTH = 1 << 3,
};

struct SceneLocReference
{
    int loc_index;
    int tile_x;
    int tile_y;
    int tile_z;
};

struct GridTile
{
    int locs[20];
    int locs_length;

    // Contains directions for which tiles are waiting for us to draw.
    int spans;

    struct SceneTile tile;

    struct SceneTextures* textures;
    int textures_length;
};

struct Scene
{
    struct SceneLocs* locs;

    struct GridTile* grid_tiles;
    int grid_tiles_length;
};

struct Scene* scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y);

#endif