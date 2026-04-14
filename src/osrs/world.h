#ifndef WORLD_H
#define WORLD_H

#include "decor_buildmap.h"
#include "entity_vec.h"
#include "game_entity.h"
#include "graphics/dash.h"
#include "graphics/dashmap.h"
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

#include <stdbool.h>
#include <string.h>

/* Forward declaration: defined in world.c (local struct used for bridging during build). */
struct FlagMap;

#define MAX_PLAYERS 2048
#define MAX_NPCS 8192

#define MAX_MAP_BUILD_LOC_ENTITIES (16384 >> 1)
#define MAX_MAP_BUILD_TILE_ENTITIES (50000)

/** Nonzero: shared vertex-array terrain (build_scene_terrain_va). Zero: per-tile models
 * (build_scene_terrain). */
#ifndef WORLD_BUILD_TERRAIN_VA
#define WORLD_BUILD_TERRAIN_VA 1
#endif

struct World
{
    struct EntityVec players;
    struct EntityVec npcs;
    struct EntityVec map_build_loc_entities;
    struct EntityVec map_build_tile_entities;

    int32_t active_players[MAX_PLAYERS];
    int32_t active_npcs[MAX_NPCS];
    int32_t active_loc_entities[MAX_MAP_BUILD_LOC_ENTITIES];
    int active_player_count;
    int active_npc_count;
    int active_loc_entity_count;

    // Painter
    struct Painter* painter;
    /** Precomputed frustum visibility; built once, reused across zone rebuilds. */
    struct PaintersCullMap* cullmap;
    int cullmap_near_clip_z;
    int cullmap_screen_width;
    int cullmap_screen_height;
    // Collisionmap
    struct CollisionMap* collision_map;
    // Heightmap
    struct Heightmap* heightmap;

    // Minimap
    struct Minimap* minimap;
    /** Scene2 is owned by the caller (e.g. GGame); world never frees it. */
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
    struct DecorBuildMap* decor_buildmap;
    // Sharelight Element Map
    struct SharelightMap* sharelight_map;

    int _base_tile_x;
    int _base_tile_z;
    int _chunk_sw_x;
    int _chunk_sw_z;
    int _chunk_ne_x;
    int _chunk_ne_z;
    int _scene_size;

    int _offset_x;
    int _offset_z;

    /** Temporary bridge-flag map; allocated in world_rebuild_centerzone_begin, freed in _end. */
    struct FlagMap* _build_flag_map;

    struct BuildCacheDat* buildcachedat;

    /** Flyweight model cache: maps uint64_t bitset key -> DashModelFlyWeight*.
     *  Shared geometry is stored once per unique (loc_id, shape_select, rotation) tuple.
     *  Cleared at the start of each world rebuild. */
    struct DashMap* flyweight_hmap;

    /** Terrain: pointers to shared geometry per level. With scene2, Scene2 owns the arrays
     * (register/unregister); these are non-owning. Without scene2, world frees them. */
    struct DashVertexArray* terrain_va[MAP_TERRAIN_LEVELS];

    struct DashFaceArray* terrain_face_array[MAP_TERRAIN_LEVELS];
};

struct World*
world_new(
    struct BuildCacheDat* buildcachedat,
    struct Scene2* scene2_shared);

void
world_free(struct World* world);

/** Replace painters cullmap; frees any previous map. World owns cm. Updates painter if present. */
void
world_set_painters_cullmap(
    struct World* world,
    struct PaintersCullMap* cm);

void
world_buildcachedat_rebuild_centerzone(
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
world_rebuild_centerzone_chunk(
    struct World* world,
    int mapx,
    int mapz);

void
world_rebuild_centerzone_end(struct World* world);

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

void
world_map_build_loc_entity_set_animation(
    struct World* world,
    int map_build_loc_entity_id,
    int animation_id);

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

void
world_map_build_loc_entity_push_action(
    struct World* world,
    int map_build_loc_entity_id,
    int code,
    char* action);

static inline struct PlayerEntity*
world_player(
    struct World* world,
    int id)
{
    return ENTITY_VEC_AT(world->players, struct PlayerEntity, id);
}

static inline struct PlayerEntity*
world_player_ensure(
    struct World* world,
    int id)
{
    struct PlayerEntity* p = ENTITY_VEC_ENSURE(world->players, struct PlayerEntity, id);
    if( !p->alive )
        p->scene_element2.element_id = -1;
    return p;
}

static inline struct NPCEntity*
world_npc(
    struct World* world,
    int id)
{
    return ENTITY_VEC_AT(world->npcs, struct NPCEntity, id);
}

static inline struct NPCEntity*
world_npc_ensure(
    struct World* world,
    int id)
{
    struct NPCEntity* n = ENTITY_VEC_ENSURE(world->npcs, struct NPCEntity, id);
    if( !n->alive )
        n->scene_element2.element_id = -1;
    return n;
}

static inline struct MapBuildLocEntity*
world_loc_entity(
    struct World* world,
    int id)
{
    return ENTITY_VEC_AT(world->map_build_loc_entities, struct MapBuildLocEntity, id);
}

static inline struct MapBuildLocEntity*
world_loc_entity_ensure(
    struct World* world,
    int id)
{
    return ENTITY_VEC_ENSURE(world->map_build_loc_entities, struct MapBuildLocEntity, id);
}

static inline struct MapBuildTileEntity*
world_map_build_tile_entity(
    struct World* world,
    int id)
{
    struct MapBuildTileEntity* e =
        ENTITY_VEC_ENSURE(world->map_build_tile_entities, struct MapBuildTileEntity, id);
    if( e->entity_id != id )
    {
        memset(e, 0, sizeof(*e));
        e->scene_element.element_id = -1;
        e->entity_id = id;
    }
    return e;
}

#endif