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

struct GridTile
{
    struct SceneLoc locs[10];
    int loc_spans[10];
    int spans;
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