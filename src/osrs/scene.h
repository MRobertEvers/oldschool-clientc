#ifndef SCENE_H
#define SCENE_H

#include "graphics/dash.h"
#include "osrs/collision_map.h"
#include "rscache/tables/config_sequence.h"
/* MAP_TERRAIN_LEVELS from collision_map.h -> rscache/tables/maps.h */

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
    int _anim_sequence_id; /* last loaded sequence id for entity sync */

    /* Walkmerge: primary + secondary blended. Client.ts maskAnimate. */
    struct DashFrame** dash_frames_secondary;
    int frame_count_secondary;
    int frame_index_secondary;
    int _anim_secondary_sequence_id;
    int* walkmerge; /* from primary sequence */
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

    /* For dynamic elements (NPCs, players): set before push, used for hover tooltip. */
    void* entity_ptr;       /* NPCEntity* or PlayerEntity* */
    int entity_kind;        /* 0=loc, 1=npc, 2=player */
    int entity_npc_type_id; /* when entity_kind==1 */
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

struct SceneTileHeights
{
    int sw_height;
    int se_height;
    int ne_height;
    int nw_height;
    int height_center;
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

    /* Per-level collision maps for pathfinding (BFS). Populated during build_scene_scenery. */
    struct CollisionMap* collision_maps[MAP_TERRAIN_LEVELS];

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
scene_element_animation_new(
    struct SceneElement* element,
    int frame_count_hint);

void
scene_element_animation_free(struct SceneElement* element);

struct SceneElement*
scene_element_new(struct Scene* scene);

void
scene_element_free(struct SceneElement* element);

void
scene_element_animation_push_frame(
    struct SceneElement* element,
    struct DashFrame* dash_frame,
    struct DashFramemap* dash_framemap,
    int length);

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

bool
scene_element_interactable(
    struct Scene* scene,
    int element);

struct DashModel*
scene_element_model(
    struct Scene* scene,
    int element);

void
scene_element_reset(struct SceneElement* element);

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

void
scene_terrain_tile_heights(
    struct Scene* scene,
    int sx,
    int sz,
    int slevel,
    struct SceneTileHeights* tile_heights);

/** Returns height of tile at (sx, sz, slevel), or 0 if out of bounds. */
int
scene_terrain_height_at_tile(
    struct Scene* scene,
    int sx,
    int sz,
    int slevel);

/** Bilinear terrain height at (scene_x, scene_z), 128 units per tile (Client-TS getAvH). */
int
scene_terrain_height_at_interpolated(
    struct Scene* scene,
    int scene_x,
    int scene_z,
    int slevel);

#endif