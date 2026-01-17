#ifndef SCENE2_H
#define SCENE2_H

#include "graphics/dash.h"

struct SceneAnimation
{
    struct DashFrame* dash_frames;
    int frame_count;
    int frame_capacity;

    struct DashFramemap* dash_framemap;
};

struct SceneElement
{
    int id;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;

    struct SceneAnimation* animation;
};

struct SceneScenery
{
    struct SceneElement* elements;
    int elements_length;
    int elements_capacity;

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

struct SceneAnimation*
scene_animation_new(int frame_count_hint);

struct SceneAnimation*
scene_animation_push_frame(
    struct SceneAnimation* animation,
    struct DashFrame* dash_frame,
    struct DashFramemap* dash_framemap);

void
scene_animation_free(struct SceneAnimation* animation);

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