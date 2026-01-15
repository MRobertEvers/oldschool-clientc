#ifndef SCENE2_H
#define SCENE2_H

#include "graphics/dash.h"

struct SceneElement
{
    int id;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;

    struct DashModelNormals* aliased_lighting_normals;

    bool sharelight;
    uint32_t light_ambient;
    uint32_t light_attenuation;
    uint8_t wall_offset;
    uint8_t size_x;
    uint8_t size_z;

    uint8_t sx;
    uint8_t sz;
    uint8_t slevel;
    uint16_t height_center;
};

struct SceneSceneryTile
{
    int wall_a_element_idx;
    int wall_b_element_idx;
};

struct SceneScenery
{
    struct SceneElement* elements;
    int elements_length;
    int elements_capacity;

    struct SceneSceneryTile* tiles;
    int tiles_length;
    int tiles_capacity;

    int tile_width_x;
    int tile_width_z;
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

    int tile_width_x;
    int tile_width_z;
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

struct SceneSceneryTile*
scene_scenery_tile_at(
    struct SceneScenery* scenery,
    int sx,
    int sz,
    int slevel);

struct SceneTerrainTile*
scene_terrain_tile_at(
    struct SceneTerrain* terrain,
    int sx,
    int sz,
    int slevel);

struct SceneElement*
scene_element_at(
    struct SceneScenery* scenery,
    int element_idx);

struct DashModel*
scene_element_model(
    struct Scene* scene,
    int element);

struct DashPosition*
scene_element_position(
    struct Scene* scene,
    int element);

#endif