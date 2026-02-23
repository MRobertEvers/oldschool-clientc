#ifndef WORLD_H
#define WORLD_H

#include "decor_buildmap.h"
#include "game_entity.h"
#include "osrs/blendmap.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/datatypes/player_appearance.h"
#include "osrs/heightmap.h"
#include "osrs/lightmap.h"
#include "osrs/minimap.h"
#include "osrs/overlaymap.h"
#include "osrs/painters.h"
#include "osrs/scene2.h"
#include "osrs/shademap.h"
#include "osrs/sharelight_map.h"
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

    int active_players[MAX_PLAYERS];
    int active_npcs[MAX_NPCS];
    int active_loc_entities[MAX_MAP_BUILD_LOC_ENTITIES];
    int active_player_count;
    int active_npc_count;
    int active_loc_entity_count;

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
    // Shademap
    struct Shademap2* shademap;
    // Blendmap
    struct Blendmap* blendmap;
    // Overlaymap
    struct Overlaymap* overlaymap;
    // Shapemap
    struct TerrainShapeMap* terrain_shapemap;
    // Walloffsetmap
    struct DecorOnWallBuildMap* decor_buildmap;
    // Sharelight Element Map
    struct SharelightMap* sharelight_map;

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

void
world_cleanup_map_build_loc_entity(
    struct World* world,
    int entity_id);

void
world_cleanup_map_build_tile_entity(
    struct World* world,
    int entity_id);

void
world_cleanup_player_entity(
    struct World* world,
    int entity_id);

void
world_cleanup_npc_entity(
    struct World* world,
    int entity_id);

void
world_player_entity_set_appearance(
    struct World* world,
    int player_entity_id,
    struct PlayerAppearance* appearance);

#define ANIMATION_TYPE_PRIMARY 0
#define ANIMATION_TYPE_SECONDARY 1

void
world_player_entity_set_animation(
    struct World* world,
    int player_entity_id,
    int animation_id,
    int animation_type);

void
world_npc_entity_set_animation(
    struct World* world,
    int npc_entity_id,
    int animation_id,
    int animation_type);

struct PassiveAnimationInfo
{
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
};

void
world_player_entity_set_passive_animations(
    struct World* world,
    int player_entity_id,
    struct PassiveAnimationInfo* passive_animation_info);

void
world_npc_entity_set_passive_animations(
    struct World* world,
    int npc_entity_id,
    struct PassiveAnimationInfo* passive_animation_info);

void
world_player_entity_path_push_step(
    struct World* world,
    int player_entity_id,
    int step_type,
    int direction);

void
world_npc_entity_path_push_step(
    struct World* world,
    int npc_entity_id,
    int step_type,
    int direction);

void
world_player_entity_path_jump_relative_to_active(
    struct World* world,
    int player_entity_id,
    bool force_teleport,
    int dx,
    int dz);

void
world_npc_entity_path_jump_relative_to_active(
    struct World* world,
    int npc_entity_id,
    bool force_teleport,
    int dx,
    int dz);

void
world_player_entity_path_jump(
    struct World* world,
    int player_entity_id,
    bool force_teleport,
    int x,
    int z);

void
world_npc_entity_path_jump(
    struct World* world,
    int npc_entity_id,
    bool force_teleport,
    int x,
    int z);

void
world_player_face_entity(
    struct World* world,
    int player_entity_id,
    int entity_id);

void
world_npc_face_entity(
    struct World* world,
    int npc_entity_id,
    int entity_id);

void
world_player_face_coord(
    struct World* world,
    int player_entity_id,
    int x,
    int z);

void
world_npc_face_coord(
    struct World* world,
    int npc_entity_id,
    int x,
    int z);

void
world_npc_set_size(
    struct World* world,
    int npc_entity_id,
    int size_x,
    int size_z);

struct NPCEntity*
world_npc_ensure_scene_element(
    struct World* world,
    int npc_id);

struct PlayerEntity*
world_player_ensure_scene_element(
    struct World* world,
    int player_id);

void
world_cycle(
    struct World* world,
    int cycles_elapsed);

#endif