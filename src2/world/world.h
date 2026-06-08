#ifndef WORLD_H
#define WORLD_H

#include "collision_map.h"
#include "decor_buildmap.h"
#include "gamecache/gamecache_types.h"
#include "osrs/painters.h"

#include <stdbool.h>
#include <stdint.h>

struct GameCache;
struct WorldScene;
struct Heightmap;
struct Blendmap;
struct Overlaymap;
struct TerrainShapeMap;
struct Lightmap;
struct SharelightMap;
struct Shademap2;
struct FlagMap;
struct Minimap;

#define WORLD_MAP_TERRAIN_X GAMECACHE_MAP_TERRAIN_X
#define WORLD_MAP_TERRAIN_Z GAMECACHE_MAP_TERRAIN_Z
#define WORLD_MAP_TERRAIN_LEVELS GAMECACHE_MAP_TERRAIN_LEVELS

static inline int
world_map_tile_coord(
    int x,
    int z,
    int level)
{
    return x + z * WORLD_MAP_TERRAIN_X + level * (WORLD_MAP_TERRAIN_X * WORLD_MAP_TERRAIN_Z);
}

#define WORLD_MAP_TILE_COORD(x, z, level) (world_map_tile_coord(x, z, level))

struct ContourGroundQueueEntry
{
    int element_id;
    int loc_id;
    int shape_select;
    int rotation;
    int size_x;
    int size_z;
    int level;
};

struct World
{
    struct GameCache* gamecache;
    struct WorldScene* scene;

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
    struct Blendmap* blendmap;
    struct Overlaymap* overlaymap;
    struct TerrainShapeMap* terrain_shapemap;
    struct DecorBuildMap* decor_buildmap;
    struct Lightmap* lightmap;
    struct SharelightMap* sharelight_map;
    struct Shademap2* shademap;
    struct FlagMap* flag_map;
    struct CollisionMap* collision_maps[COLLISION_LEVELS];
    struct Minimap* minimap;

    struct Painter* painter;
    struct PaintersCullMap* cullmap;
    int* terrain_element_ids;

    struct ContourGroundQueueEntry* contour_ground_queue;
    int contour_ground_queue_count;
    int contour_ground_queue_cap;

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
world_new(
    struct GameCache* gamecache,
    struct WorldScene* scene);

void
world_free(struct World* world);

void
world_rebuild_centerzone(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
world_rebuild_centerzone_begin(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
world_rebuild_centerzone_chunk_terrain(
    struct World* world,
    int mapx,
    int mapz);

void
world_rebuild_centerzone_chunk_scenery(
    struct World* world,
    int mapx,
    int mapz);

void
world_rebuild_centerzone_chunk(
    struct World* world,
    int mapx,
    int mapz);

void
world_rebuild_centerzone_end(struct World* world);

void
world_contour_ground(struct World* world);

void
world_apply_wall_decor_offsets(struct World* world);

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

#endif
