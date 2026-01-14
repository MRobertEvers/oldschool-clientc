#ifndef SCENE2_H
#define SCENE2_H

#include "graphics/dash.h"

struct SceneElement
{
    int id;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;
};

struct SceneScenery
{
    struct SceneElement* elements;
    int elements_length;
    int elements_capacity;
};

struct SceneTerrainTile
{
    int model_asid;

    struct DashModel* dash_model;

    int sx;
    int sz;
    int slevel;
};

struct SceneTerrain
{
    struct SceneTerrainTile* tiles;
    int tiles_length;
    int tiles_capacity;

    int tile_width_x;
    int tile_width_z;
};

struct Scene
{
    struct SceneTerrain* terrain;
    struct SceneScenery* scenery;
};

struct Scene*
scene_new(
    int tile_width_x,
    int tile_width_z,
    int element_count_hint);
void
scene_free(struct Scene* scene);

int
scene_scenery_push_element_move(
    struct SceneScenery* scenery,
    struct SceneElement* element);

struct SceneTerrainTile*
scene_terrain_tile_at(
    struct SceneTerrain* terrain,
    int sx,
    int sz,
    int slevel);

struct DashModel*
scene_element_model(
    struct Scene* scene,
    int element);

struct DashPosition*
scene_element_position(
    struct Scene* scene,
    int element);

#endif