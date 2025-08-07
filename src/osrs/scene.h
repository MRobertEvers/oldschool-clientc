#ifndef SCENE_H
#define SCENE_H

#include "cache.h"
#include "lighting.h"
#include "scene_cache.h"
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

enum LightingMode
{
    LIGHTING_BLEND,
    LIGHTING_FLAT,
};

struct ModelLighting
{
    enum LightingMode mode;

    int* face_colors_hsl_a;

    // null if mode is LIGHTING_FLAT
    int* face_colors_hsl_b;

    // null if mode is LIGHTING_FLAT
    int* face_colors_hsl_c;
};

/**
 * Shared lighting normals are used for roofs and
 * other locs that represent a single surface.
 *
 * See sharelight.\
 *
 * Sharelight "shares" lighting info between all models of
 * a particular "bitset", the bitset is calculated based on
 * the loc rotation, index, etc.
 *
 * When sharelight is true, normals of abutting locs that
 * share vertexes
 * are merged into a single normal.
 *
 * Consider two roof locs that are next to each other.
 * They are aligned such that the inner edges have coinciding vertexes.
 * Sharelight will take the coinciding vertexes and add the normals together.
 *
 * AB AB
 * CD CD
 *
 * Normal 1 points Up and to the Right,
 * Normal 2 points Up and to the Left.
 *
 * They will be merged into a single normal that points upward only.
 *
 * This removes the influence of the face on the sides of the roof models.
 *
 * world3d_merge_locnormals looks at adjacent locs and merges normals if
 * vertexes coincide.
 *
 *
 * Sharelight forces locs to use the same lighting normals, so their lighting
 * will be the same. When the normals are mutated or merged, they will all be
 * affected.
 */

// Each model needs it's own normals, and then sharelight models
// will have shared normals.
struct ModelNormals
{
    struct LightingNormal* lighting_vertex_normals;
    int lighting_vertex_normals_count;

    struct LightingNormal* lighting_face_normals;
    int lighting_face_normals_count;
};

struct SceneModel
{
    int model_id;
    struct CacheModel* model;

    struct ModelNormals* normals;
    struct ModelNormals* aliasedptr_lighting_normals;
    struct ModelLighting* lighting;

    int light_ambient;
    int light_contrast;

    int region_x;
    int region_y;
    int region_z;

    int offset_x;
    int offset_y;
    int offset_height;

    int _size_x;
    int _size_y;

    int _chunk_pos_x;
    int _chunk_pos_y;
    int _chunk_pos_level;

    // TODO: Remove this
    bool __drawn;
};

enum GridTileFlags
{
    /**
     * The bridge tile is actually a bridge drawn on a different level.
     */
    GRID_TILE_FLAG_BRIDGE = 1 << 0,
};

struct GridTile
{
    // These are only used for normal locs
    int locs[20];
    int locs_length;

    int wall;
    int wall_decor;
    int ground_decor;

    int bridge_tile;

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

    int sharelight;

    int x;
    int z;
    int level;

    int flags;
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
    WALL_SIDE_WEST = 1 << 0,        // 1
    WALL_SIDE_NORTH = 1 << 1,       // 2
    WALL_SIDE_EAST = 1 << 2,        // 4
    WALL_SIDE_SOUTH = 1 << 3,       // 8
    WALL_CORNER_NORTHWEST = 1 << 4, // 16
    WALL_CORNER_NORTHEAST = 1 << 5, // 32
    WALL_CORNER_SOUTHEAST = 1 << 6, // 64
    WALL_CORNER_SOUTHWEST = 1 << 7, // 128
};

struct Wall
{
    int model_a;
    int model_b;

    enum WallSide side_a;
    enum WallSide side_b;

    int wall_width;
};

struct GroundDecor
{
    int model;
};

enum ThroughWallFlags
{
    THROUGHWALL = 0x100,
    // In OS1 and later, this is removed; only present in 2004scape.
    // THROUGHWALL_OUTSIDE = 0x200,
};
struct WallDecor
{
    int model_a;
    int model_b;

    // For throughwall, this specifies which side is the "outside".
    enum WallSide side;

    // In the 2004scape 0x100 is used to indicate an interior decor,
    // and 0x200 to indicate an exterior decor. 0x300 means draw both with same model.
    // In OS1 and deobs, this is removed and 0x100 represents both for throughwall always.
    // Both models are drawn before and after locs.
    // In this case, "side" represents the side of the wall that model_a is facing.
    // model_b faces the opposite side.
    int through_wall_flags;
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

    // See sharelight.
    int sharelight;

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

    int* _shade_map;
    int _shade_map_length;
};

struct Scene* scene_new_from_map(struct Cache* cache, int chunk_x, int chunk_y);
void scene_free(struct Scene* scene);

#endif