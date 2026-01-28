#ifndef SCENE2_H
#define SCENE2_H

#include "graphics/dash.h"
#include "rscache/tables/config_sequence.h"

struct SceneAction
{
    struct SceneAction* next;

    char action[32];
};

struct SceneAnimation
{
    struct DashFrame** dash_frames;
    int* frame_lengths;
    int frame_count;
    int frame_capacity;

    struct CacheConfigSequence* config_sequence;

    struct DashFramemap* dash_framemap;

    int frame_index;
    int cycle;
};

struct SceneElement
{
    int id;

    bool interactable;

    struct DashModel* dash_model;
    struct DashPosition* dash_position;

    int tile_sx;
    int tile_sz;
    int tile_slevel;

    struct SceneAnimation* animation;
    struct SceneAction* actions;

    struct CacheConfigLocation* config_loc;

    char _dbg_name[64];
    int model_ids[10];
    int config_loc_id;
};

struct SceneScenery
{
    // Static elements
    struct SceneElement* elements;
    int elements_length;
    int elements_capacity;

    struct SceneElement* dynamic_elements;
    int dynamic_elements_length;
    int dynamic_elements_capacity;

    int tile_width_x;
    int tile_width_z;
};

struct SceneTerrainTile
{
    int model_asid;

    struct DashModel* dash_model;

    int height;

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

int
scene_scenery_push_dynamic_element_move(
    struct SceneScenery* scenery,
    struct SceneElement* element);

void
scene_scenery_reset_dynamic_elements(struct SceneScenery* scenery);

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

int
scene_push_element_move(
    struct Scene* scene,
    struct SceneElement* element);

bool
scene_element_interactable(
    struct Scene* scene,
    int element);

struct DashModel*
scene_element_model(
    struct Scene* scene,
    int element);

struct DashPosition*
scene_element_position(
    struct Scene* scene,
    int element);

char*
scene_element_name(
    struct Scene* scene,
    int element);

int
scene_terrain_height_center(
    struct Scene* scene,
    int sx,
    int sz,
    int slevel);

#endif