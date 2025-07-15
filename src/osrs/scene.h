#ifndef SCENE_H
#define SCENE_H

#include "cache.h"
#include "scene_cache.h"
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

struct NormalScenery
{
    // TODO:
    int model;

    // Additional yaw from the orientation.
    int yaw_r2pi2048;

    int span_x;
    int span_z;

    int offset_x;
    int offset_z;
};

struct Floor
{};

struct Wall
{
    // Used to mask which side of the tile that the wall is on.
    int rotatation_type;

    // TODO:
    int model;
};

struct GridTile
{
    // These are only used for normal locs
    int locs[20];
    int locs_length;

    struct Wall* wall;

    // Contains directions for which tiles are waiting for us to draw.
    // This is determined by locs that are larger than 1x1.
    // E.g. If a table is 3x1, then the spans for each tile will be:
    // (Assuming the table is going west-east direction)
    // WEST SIDE
    //     SPAN_FLAG_EAST,
    //     SPAN_FLAG_EAST | SPAN_FLAG_WEST,
    //     SPAN_FLAG_WEST
    // EAST SIDE
    //
    // For a 2x2 table, the spans will be:
    // WEST SIDE  <->  EAST SIDE
    //     [SPAN_FLAG_EAST | SPAN_FLAG_SOUTH,    SPAN_FLAG_WEST | SPAN_FLAG_SOUTH]
    //     [SPAN_FLAG_EAST | SPAN_FLAG_NORTH,    SPAN_FLAG_WEST | SPAN_FLAG_NORTH]
    //
    //
    // As the underlays are drawn diagonally inwards from the corner, once each of the
    // underlays is drawn, the loc on top is drawn.
    // The spans are used to determine which tiles are waiting for us to draw.
    int spans;

    struct SceneTile tile;

    struct SceneTextures* textures;
    int textures_length;

    int x;
    int z;
    int level;
};

struct Scene
{
    struct SceneLocs* locs;

    struct GridTile* grid_tiles;
    int grid_tiles_length;

    struct ModelCache* _model_cache;
};

struct Scene* scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y);
void scene_free(struct Scene* scene);

#endif