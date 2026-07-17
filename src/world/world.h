#ifndef WORLD_H
#define WORLD_H

#include "world_entity.h"

#include "engine/world_builder/collision_map.h"
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/minimap.h"
#include "painters/painters.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_MAX_EVENTS 256
#define WORLD_SCENERY_PICK_MAX 4096

#define WORLD_MAP_TERRAIN_X 64
#define WORLD_MAP_TERRAIN_Z 64
#define WORLD_MAP_TERRAIN_LEVELS 4

enum WorldEventKind
{
    WorldEventKind_EntityRemoved = 0,
};

struct World_Event
{
    enum WorldEventKind kind;
    int element_id;
};

struct World_SceneryPick
{
    int element_id;
    int loc_id;
    int scenery_index;
};

typedef int (*World_HeightFn)(
    void* userdata,
    int world_x,
    int world_z,
    int level);

struct World
{
    int _base_tile_x;
    int _base_tile_z;
    int _chunk_sw_x;
    int _chunk_sw_z;
    int _chunk_ne_x;
    int _chunk_ne_z;
    int _offset_x;
    int _offset_z;
    int _scene_size;

    bool load_complete;

    struct World_EntityList entities;

    struct World_Event events[WORLD_MAX_EVENTS];
    int event_count;

    struct World_SceneryPick scenery_picks[WORLD_SCENERY_PICK_MAX];
    int scenery_pick_count;

    World_HeightFn height_fn;
    void* height_userdata;

    struct Heightmap* heightmap;
    struct CollisionMap* collision_maps[COLLISION_LEVELS];
    struct Minimap* minimap;
    struct Painter* painter;
    struct PaintersCullMap* cullmap;
};

static inline int
World_MapTileCoord(
    int x,
    int z,
    int level)
{
    return x + z * WORLD_MAP_TERRAIN_X + level * (WORLD_MAP_TERRAIN_X * WORLD_MAP_TERRAIN_Z);
}

static inline int
World_ToSceneX(
    struct World* world,
    int mapx,
    int chunk_x)
{
    return (chunk_x - world->_offset_x) + (mapx - world->_chunk_sw_x) * WORLD_MAP_TERRAIN_X;
}

static inline int
World_ToSceneZ(
    struct World* world,
    int mapz,
    int chunk_z)
{
    return (chunk_z - world->_offset_z) + (mapz - world->_chunk_sw_z) * WORLD_MAP_TERRAIN_Z;
}

static inline int
World_TerrainTileIdx(
    struct World* world,
    int x,
    int z,
    int level)
{
    return x + z * world->_scene_size + level * world->_scene_size * world->_scene_size;
}

struct World*
World_New(void);

void
World_Free(struct World* world);

void
World_SetHeightFn(
    struct World* world,
    World_HeightFn fn,
    void* userdata);

void
World_ResetScene(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
World_ResetSceneChunkList(
    struct World* world,
    const int* chunks_xz,
    int count);

void
World_SetLoadComplete(
    struct World* world,
    bool complete);

void
World_TerrainSet(
    struct World* world,
    int element_id,
    int x,
    int z,
    int level);

void
World_TerrainReset(struct World* world);

int
World_TerrainElementAt(
    struct World* world,
    int x,
    int z,
    int level);

int
World_PlayerSpawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    struct WorldEntityFacet_IdleAnimations idle_animations);

void
World_PlayerDespawn(
    struct World* world,
    int idx);

int
World_NpcSpawn(
    struct World* world,
    int element_id,
    int npc_id,
    int level,
    int scene_x,
    int scene_z,
    int size,
    struct WorldEntityFacet_IdleAnimations idle_animations);

void
World_NpcDespawn(
    struct World* world,
    int idx);

int
World_ProjectileSpawn(
    struct World* world,
    int element_id,
    int level,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int h1,
    int end_height,
    int t1,
    int t2,
    int angle,
    int startpos);

void
World_ProjectileDespawn(
    struct World* world,
    int idx);

int
World_SpotanimSpawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    int y,
    int orientation,
    int idle_delay,
    int lifetime);

void
World_SpotanimDespawn(
    struct World* world,
    int idx);

int
World_SceneryRegister(
    struct World* world,
    int element_id,
    int loc_id,
    int scene_x,
    int scene_z,
    int level,
    int size_x,
    int size_z,
    char const* name,
    char const actions[5][32]);

struct WorldEntity_Scenery*
World_SceneryGetByElementId(
    struct World* world,
    int element_id);

void
World_ClearSceneryPicks(struct World* world);

void
World_RegisterSceneryPick(
    struct World* world,
    int element_id,
    int loc_id);

void
World_PlayerPathPushStep(
    struct World* world,
    int idx,
    int step_type,
    int direction);

void
World_NpcPathPushStep(
    struct World* world,
    int idx,
    int step_type,
    int direction);

void
World_PlayerPathJump(
    struct World* world,
    int idx,
    bool force_teleport,
    int x,
    int z);

void
World_NpcPathJump(
    struct World* world,
    int idx,
    bool force_teleport,
    int x,
    int z);

void
World_PlayerFaceEntity(
    struct World* world,
    int idx,
    int entity_id);

void
World_NpcFaceEntity(
    struct World* world,
    int idx,
    int entity_id);

void
World_PlayerFaceCoord(
    struct World* world,
    int idx,
    int x,
    int z);

void
World_NpcFaceCoord(
    struct World* world,
    int idx,
    int x,
    int z);

void
World_PlayerSetAnimation(
    struct World* world,
    int idx,
    int animation_id,
    int animation_type);

void
World_NpcSetAnimation(
    struct World* world,
    int idx,
    int animation_id,
    int animation_type);

#define WORLD_ANIMATION_TYPE_PRIMARY 0
#define WORLD_ANIMATION_TYPE_SECONDARY 1

int
World_EventsCount(struct World* world);

const struct World_Event*
World_EventsPeek(
    struct World* world,
    int i);

void
World_EventsClear(struct World* world);

void
World_Cycle(
    struct World* world,
    int cycles_elapsed);

/* Shared with world_cycle.c */
void
World_ProjectileSetTarget(
    struct World* world,
    struct WorldEntity_Projectile* p,
    int cycle);

void
World_ProjectileMove(
    struct WorldEntity_Projectile* p,
    int delta);

#endif
