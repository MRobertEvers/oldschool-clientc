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
    int wall_decor;
    int ground_decor;

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

    struct SceneTile* tile;

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
    LOC_TYPE_WALL,
    LOC_TYPE_GROUND_DECOR,
    LOC_TYPE_WALL_DECOR,
};

struct NormalScenery
{
    int model;
};

enum WallSide
{
    WALL_SIDE_EAST = 1 << 0,          // 1
    WALL_SIDE_NORTH = 1 << 1,         // 2
    WALL_SIDE_WEST = 1 << 2,          // 4
    WALL_SIDE_SOUTH = 1 << 3,         // 8
    WALL_CORNER_NORTHEAST = 1 << 4,   // 16
    WALL_CORNER_NORTHWEST = 1 << 5,   // 32
    WALL_CORNER_SOUTHWEST = 1 << 6,   // 64
    WALL_CORNER_SOUTHEAST = 1 << 7,   // 128
    WALL_SIDE_EAST_OFFSET = 1 << 8,   // 256
    WALL_SIDE_NORTH_OFFSET = 1 << 9,  // 512
    WALL_SIDE_WEST_OFFSET = 1 << 10,  // 1024
    WALL_SIDE_SOUTH_OFFSET = 1 << 11, // 2048
};

struct Wall
{
    int model_a;
    int model_b;

    enum WallSide side_a;
    enum WallSide side_b;
};

struct GroundDecor
{
    int model;
};

struct WallDecor
{
    int model;

    enum WallSide side;
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
        struct GroundDecor _ground_decor;
        struct WallDecor _wall_decor;
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

    struct SceneTile* scene_tiles;
    int scene_tiles_length;

    struct ModelCache* _model_cache;
};

struct Scene* scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y);
void scene_free(struct Scene* scene);

#endif