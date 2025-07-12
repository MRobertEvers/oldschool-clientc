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

    int model_id;
    int wall_type;

    int offset_x;
    int offset_y;
};

struct GroundDecor
{
    int _loc_id;
    int orientation;

    int model_id;
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

    struct Floor* floor;
    // A wall might have two models. E.g. if it's an L shap.
    struct Wall* wall;

    struct Bridge* bridge;

    // Only used for world3d_add_loc/world3d_add_loc2
    // which is only called for
    // shape == CENTREPIECE_STRAIGHT || shape == CENTREPIECE_DIAGONAL
    // shape >= ROOF_STRAIGHT
    // shape == WALL_DIAGONAL
    //
    struct NormalScenery* normals[WORLD_NORMAL_MAX_COUNT];
    int normals_length;

    // bridge underlay or
    // bridge overlay
    // bridge wall
    // bridge locs

    // Underlay or
    // Overlay
    // Wall (don't know if far or near yet.)
    // Wall decoration
    // ground decoration

    // objs
};

struct World
{
    struct WorldTile* bridges;
    int bridge_count;

    struct WorldTile* tiles;
    int tile_count;

    int x_width;
    int z_width;

    struct WorldModel* world_models;
    int world_model_count;
    int world_model_capacity;
};

struct World* world_new_from_cache(struct Cache* cache, int chunk_x, int chunk_y);
void world_free(struct World* world);

#endif