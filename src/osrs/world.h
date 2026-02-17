#ifndef WORLD_H
#define WORLD_H

#include "decor_buildmap.h"
#include "game_entity.h"
#include "osrs/blendmap.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/heightmap.h"
#include "osrs/lightmap.h"
#include "osrs/minimap.h"
#include "osrs/overlaymap.h"
#include "osrs/painters.h"
#include "osrs/scene2.h"
#include "osrs/terrain_shapemap.h"

#define MAX_PLAYERS 2048
#define MAX_NPCS 8192

#define MAX_MAP_BUILD_LOC_ENTITIES 16384
#define MAX_MAP_BUILD_TILE_ENTITIES 65535

struct World
{
    struct PlayerEntity players[MAX_PLAYERS];
    struct NPCEntity npcs[MAX_NPCS];
    struct MapBuildLocEntity map_build_loc_entities[MAX_MAP_BUILD_LOC_ENTITIES];
    struct MapBuildTileEntity map_build_tile_entities[MAX_MAP_BUILD_TILE_ENTITIES];

    int map_build_loc_entity_count;

    // Painter
    struct Painter* painter;
    // Collisionmap
    struct CollisionMap* collision_map;
    // Heightmap
    struct Heightmap* heightmap;

    // Minimap
    struct Minimap* minimap;
    // ScenePool
    struct Scene2* scene2;

    // Todo: How to organize, these are only used at build time.
    // Lightmap
    struct Lightmap* lightmap;
    // Blendmap
    struct Blendmap* blendmap;
    // Overlaymap
    struct Overlaymap* overlaymap;
    // Shapemap
    struct TerrainShapeMap* terrain_shapemap;
    // Walloffsetmap
    struct DecorOnWallBuildMap* decor_buildmap;

    int _base_tile_x;
    int _base_tile_z;
    int _chunk_sw_x;
    int _chunk_sw_z;
    int _scene_size;

    int _offset_x;
    int _offset_z;

    struct BuildCacheDat* buildcachedat;
};

struct World*
world_new(struct BuildCacheDat* buildcachedat);

void
world_free(struct World* world);

void
world_buildcachedat_rebuild_centerzone(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

struct MapBuildTileEntity*
world_tile_entity_at(
    struct World* world,
    int x,
    int z,
    int level);

#endif