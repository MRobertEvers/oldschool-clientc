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

struct SceneModel
{
    int* model_ids;
    struct CacheModel** models;
    int model_count;

    int shape;

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

struct GridTile
{
    // These are only used for normal locs
    int locs[20];
    int locs_length;

    int wall;

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

enum LocType
{
    LOC_TYPE_INVALID,
    LOC_TYPE_SCENERY,
};

struct NormalScenery
{
    int model;
};

struct Wall
{
    int model;
};

struct Loc
{
    enum LocType type;

    int __drawn;

    int size_x;
    int size_y;

    int chunk_pos_x;
    int chunk_pos_y;
    int chunk_pos_level;

    union
    {
        struct NormalScenery _scenery;
        struct Wall _wall;
    };
};

struct Scene
{
    struct GridTile* grid_tiles;
    int grid_tiles_length;

    struct Loc* locs;
    int locs_length;
    int locs_capacity;

    struct SceneModel* models;
    int models_length;
    int models_capacity;

    struct ModelCache* _model_cache;
};

struct Scene* scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y);
void scene_free(struct Scene* scene);

#endif