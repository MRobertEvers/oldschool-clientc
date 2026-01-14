#ifndef SCENE2_H
#define SCENE2_H

struct SceneElement
{
    int id;
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

    struct SceneElement* elements;
    int elements_length;
    int elements_capacity;
};

struct Scene*
scene_new();
void
scene_free(struct Scene* scene);

struct SceneTerrain*
scene_terrain_new_sized(
    int tile_width_x,
    int tile_width_z);

void
scene_terrain_free(struct SceneTerrain* terrain);

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