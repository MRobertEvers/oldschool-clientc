#ifndef WORLD_H
#define WORLD_H

#include "cache.h"
#include "scene.h"

// CacheData -> World -> Scene

/**
 * Store the transforms required for each model.
 *
 * The renderer will need to transform the model.
 *
 * rs-map-viewer getLocModelData
 */
#define WORLD_MODEL_MAX_COUNT 5
struct WorldModel
{
    int models[WORLD_MODEL_MAX_COUNT];
    int model_count;

    int offset_x;
    int offset_y;
    int offset_z;

    int yaw_r2pi2048;
    int yaw_orientation; // 0-3
};

// A wall might have two models. E.g. if it's an L shap.
struct Wall
{
    int _loc_id;

    int wmodel;

    // Used to mask which side of the tile that the wall is on.
    int wall_type;
};

struct WallDecor
{
    int _loc_id;
    int orientation;

    int wmodel;
    int wall_type;

    int offset_x;
    int offset_y;
};

struct GroundDecor
{
    int _loc_id;
    int orientation;

    int wmodel;
};

struct NormalScenery
{
    int _loc_id;

    int wmodel;

    int span_x;
    int span_z;
};

// MapFloor -> Floor  identical.
struct Floor
{
    int height;
    int attr_opcode;
    int settings;
    int overlay_id;
    int shape;
    int rotation;
    int underlay_id;
};

#define WORLD_NORMAL_MAX_COUNT 5

struct WorldTile
{
    int x;
    int z;
    int level;

    // Index in the World Bridge array.
    int bridge;

    // Index in the World Floor array.
    int floor;

    int wall;

    int wall_decor;
    int ground_decor;

    // Only used for world3d_add_loc/world3d_add_loc2
    // which is only called for
    // shape == CENTREPIECE_STRAIGHT || shape == CENTREPIECE_DIAGONAL
    // shape >= ROOF_STRAIGHT
    // shape == WALL_DIAGONAL
    //
    // rs-map-viewer getLocModelData
    // type === LocModelType.NORMAL || type === LocModelType.NORMAL_DIAGIONAL
    int scenery[WORLD_NORMAL_MAX_COUNT];
    int scenery_length;
};

enum ElementKind
{
    ELEMENT_KIND_NONE,
    ELEMENT_KIND_GROUND,
    ELEMENT_KIND_LOC,
    ELEMENT_KIND_WALL,
    ELEMENT_KIND_WALL_DECOR,
    ELEMENT_KIND_GROUND_DECOR,
    ELEMENT_KIND_NORMAL,
};

struct WorldElement
{
    enum ElementKind kind;

    union
    {
        struct Wall _wall;
        struct WallDecor _wall_decor;
        struct GroundDecor _ground_decor;
        struct NormalScenery _normal;
    };
};

struct World
{
    struct WorldTile* bridges;
    int bridge_count;

    struct WorldTile* tiles;
    int tile_count;

    struct WorldElement* elements;
    int element_count;
    int element_capacity;

    int x_width;
    int z_width;

    struct Floor* floors;
    int floor_count;
    int floor_capacity;

    struct WorldModel* world_models;
    int world_model_count;
    int world_model_capacity;
};

struct World* world_new_from_cache(struct Cache* cache, int chunk_x, int chunk_y);
void world_free(struct World* world);

#endif