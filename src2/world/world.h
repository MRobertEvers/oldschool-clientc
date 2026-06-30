#ifndef WORLD_H
#define WORLD_H

#include "collision_map.h"
#include "osrs/painters.h"
#include "world_entity.h"

#include <stdbool.h>
#include <stdint.h>

struct Heightmap;
struct Minimap;

#define WORLD_MAX_EVENTS 256
#define WORLD_SCENERY_PICK_MAX 4096

struct WorldSceneryPick
{
    int element_id;
    int loc_id;
};

#define WORLD_MAP_TERRAIN_X 64
#define WORLD_MAP_TERRAIN_Z 64
#define WORLD_MAP_TERRAIN_LEVELS 4

static inline int
world_map_tile_coord(
    int x,
    int z,
    int level)
{
    return x + z * WORLD_MAP_TERRAIN_X + level * (WORLD_MAP_TERRAIN_X * WORLD_MAP_TERRAIN_Z);
}

#define WORLD_MAP_TILE_COORD(x, z, level) (world_map_tile_coord(x, z, level))

enum WorldEventKind
{
    WORLD_EVENT_ENTITY_REMOVED = 0,
};

struct WorldEvent
{
    enum WorldEventKind kind;
    int element_id;
};

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

    struct Heightmap* heightmap;
    struct CollisionMap* collision_maps[COLLISION_LEVELS];
    struct Minimap* minimap;

    struct Painter* painter;
    struct PaintersCullMap* cullmap;

    struct World_EntityList entities;

    struct WorldEvent events[WORLD_MAX_EVENTS];
    int event_count;

    struct WorldSceneryPick scenery_picks[WORLD_SCENERY_PICK_MAX];
    int scenery_pick_count;

    bool load_complete;
};

static inline int
world_to_scene_x(
    struct World* world,
    int mapx,
    int chunk_x)
{
    return (chunk_x - world->_offset_x) + (mapx - world->_chunk_sw_x) * WORLD_MAP_TERRAIN_X;
}

static inline int
world_to_scene_z(
    struct World* world,
    int mapz,
    int chunk_z)
{
    return (chunk_z - world->_offset_z) + (mapz - world->_chunk_sw_z) * WORLD_MAP_TERRAIN_Z;
}

struct World*
world_new(void);

void
world_free(struct World* world);

void
world_reset_scene(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
world_reset_scene_chunklist(
    struct World* world,
    const int* chunks_xz,
    int count);

void
world_set_painters_cullmap(
    struct World* world,
    struct PaintersCullMap* cm);

static inline int
world_terrain_tile_idx(
    struct World* world,
    int x,
    int z,
    int level)
{
    return x + z * world->_scene_size + level * world->_scene_size * world->_scene_size;
}

void
world_terrain_set(
    struct World* world,
    int element_id,
    int x,
    int z,
    int level);

void
world_terrain_reset(struct World* world);

int
world_terrain_element_at(
    struct World* world,
    int x,
    int z,
    int level);

int
world_player_spawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    struct WorldEntityFacet_IdleAnimations idle_animations);

void
world_player_despawn(
    struct World* world,
    int idx);

int
world_npc_spawn(
    struct World* world,
    int element_id,
    int npc_id,
    int level,
    int scene_x,
    int scene_z,
    int size,
    struct WorldEntityFacet_IdleAnimations idle_animations);

void
world_npc_despawn(
    struct World* world,
    int idx);

int
world_projectile_spawn(
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
world_projectile_despawn(
    struct World* world,
    int idx);

int
world_spotanim_spawn(
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
world_spotanim_despawn(
    struct World* world,
    int idx);

int
world_events_count(struct World* world);

const struct WorldEvent*
world_events_peek(
    struct World* world,
    int i);

void
world_events_clear(struct World* world);

void
world_cycle(
    struct World* world,
    int cycles_elapsed);

void
world_clear_scenery_picks(struct World* world);

void
world_register_scenery_pick(
    struct World* world,
    int element_id,
    int loc_id);

#endif
