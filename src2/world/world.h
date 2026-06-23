#ifndef WORLD_H
#define WORLD_H

#include "collision_map.h"
#include "osrs/painters.h"

#include <stdbool.h>
#include <stdint.h>

struct Heightmap;
struct Minimap;

#define WORLD_MAX_PROJECTILES 256

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

struct WorldProjectile
{
    bool alive;
    int element_id;
    int level;
    int pos_x;
    int pos_z;
    int vel_x;
    int vel_z;
    int yaw;
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
    int* terrain_element_ids;

    struct WorldProjectile projectiles[WORLD_MAX_PROJECTILES];
    int projectile_count;

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
world_set_painters_cullmap(
    struct World* world,
    struct PaintersCullMap* cm);

int
world_terrain_element_at(
    struct World* world,
    int x,
    int z,
    int level);

int
world_projectile_spawn(
    struct World* world,
    int element_id,
    int level,
    int pos_x,
    int pos_z,
    int vel_x,
    int vel_z,
    int yaw);

void
world_projectile_despawn(
    struct World* world,
    int idx);

void
world_cycle(
    struct World* world,
    int cycles_elapsed);

#endif
