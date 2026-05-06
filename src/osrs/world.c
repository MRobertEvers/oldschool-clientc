#include "world.h"

#include "blendmap.h"
#include "datatypes/appearances.h"
#include "game.h"
#include "heightmap.h"
#include "platforms/common/mem_format.h"
#include "platforms/common/platform_memory.h"
#include "terrain_shapemap.h"
#include "world_scenebuild.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WORLD_DEBUG_REBUILD_SCENERY_CULL
#define WORLD_DEBUG_REBUILD_SCENERY_CULL 0
#endif

/** Per-chunk loc → painter pipeline; painter span OOB/clip; end histogram. Set -DWORLD_DEBUG_PAINTER_LOC_BUILD=1 to enable. */
#ifndef WORLD_DEBUG_PAINTER_LOC_BUILD
#define WORLD_DEBUG_PAINTER_LOC_BUILD 0
#endif

static void world_player_entity_refresh_scene2_appearance_mesh(struct World* world,
                                                               int player_entity_id);

// clang-format off
#include "world_scenery.u.c"
#include "world_terrain.u.c"
#include "world_sharelight.u.c"
#include "world_cycle.u.c"
// clang-format on

#define CURRENT_LEVEL 0

static void
init_map_build_loc_entity(
    struct MapBuildLocEntity* map_build_loc_entity,
    int entity_id)
{
    memset(map_build_loc_entity, 0, sizeof(struct MapBuildLocEntity));
    map_build_loc_entity->scene_element.element_id = -1;
    map_build_loc_entity->entity_id = entity_id;
    map_build_loc_entity->scene_element_two.element_id = -1;
    map_build_loc_entity->place_size_x = 1;
    map_build_loc_entity->place_size_z = 1;
}

static struct MapBuildLocEntity*
next_map_build_loc_entity(struct World* world)
{
    if( world->active_loc_entity_count < MAX_MAP_BUILD_LOC_ENTITIES )
    {
        int entity_id = world->active_loc_entity_count;
        struct MapBuildLocEntity* loc = world_loc_entity_ensure(world, entity_id);
        init_map_build_loc_entity(loc, entity_id);
        world->active_loc_entities[world->active_loc_entity_count] = entity_id;
        world->active_loc_entity_count++;
        return loc;
    }
    fprintf(stderr, "next_map_build_loc_entity: exceeded MAX_MAP_BUILD_LOC_ENTITIES\n");
    abort();
}

int
world_map_build_loc_entity_allocate_at_scene(
    struct World* world,
    int sx,
    int sz,
    int slevel)
{
    if( world->active_loc_entity_count >= MAX_MAP_BUILD_LOC_ENTITIES )
        return -1;

    int entity_id = world->active_loc_entity_count;
    struct MapBuildLocEntity* loc = world_loc_entity_ensure(world, entity_id);
    init_map_build_loc_entity(loc, entity_id);
    world->active_loc_entities[world->active_loc_entity_count] = entity_id;
    world->active_loc_entity_count++;

    loc->scene_coord.sx = (uint32_t)sx;
    loc->scene_coord.sz = (uint32_t)sz;
    loc->scene_coord.slevel = (uint32_t)slevel;
    loc->loc_type_id = -1;

    return entity_id;
}

/** Slot 0 would otherwise match `entity_id != id` never (0==0) while still all-zero from pool. */
static void
world_prime_map_build_tile_slot0(struct World* world)
{
    struct MapBuildTileEntity* e =
        ENTITY_VEC_ENSURE(world->map_build_tile_entities, struct MapBuildTileEntity, 0);
    memset(e, 0, sizeof(*e));
    e->scene_element.element_id = -1;
    e->entity_id = 0;
}

static void
world_obj_stack_entity_remove_from_active(
    struct World* world,
    int entity_id)
{
    for( int i = 0; i < world->active_obj_stack_entity_count; i++ )
    {
        if( world->active_obj_stack_entities[i] == entity_id )
        {
            world->active_obj_stack_entities[i] =
                world->active_obj_stack_entities[world->active_obj_stack_entity_count - 1];
            world->active_obj_stack_entity_count--;
            return;
        }
    }
}

void
world_cleanup_obj_stack_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->obj_stack_entities) )
        return;

    struct ObjStackEntity* e = world_obj_stack_entity(world, entity_id);
    if( e->alive )
        world_obj_stack_entity_remove_from_active(world, entity_id);
    if( world->scene2 )
        scene2_element_release(world->scene2, e->scene_element.element_id);
    e->scene_element.element_id = -1;
    memset(&e->scene_coord, 0, sizeof(struct EntitySceneCoord));
    e->alive = 0;
    e->level = 0;
    e->world_tile_x = 0;
    e->world_tile_z = 0;
}

int
world_obj_stack_entity_create(struct World* world)
{
    if( !world )
        return -1;

    int32_t n = entity_vec_count(&world->obj_stack_entities);
    for( int32_t i = 0; i < n; i++ )
    {
        struct ObjStackEntity* e = world_obj_stack_entity(world, i);
        if( !e->alive )
        {
            if( world->scene2 )
                scene2_element_release(world->scene2, e->scene_element.element_id);
            e->scene_element.element_id = -1;
            if( world->active_obj_stack_entity_count < MAX_OBJ_STACK_ENTITIES )
                world->active_obj_stack_entities[world->active_obj_stack_entity_count++] =
                    (int32_t)i;
            e->alive = 1;
            e->entity_id = (int)i;
            return (int)i;
        }
    }

    if( n >= MAX_OBJ_STACK_ENTITIES )
        return -1;

    struct ObjStackEntity* e = world_obj_stack_entity_ensure(world, n);
    memset(e, 0, sizeof(*e));
    e->entity_id = (int)n;
    e->scene_element.element_id = -1;
    e->alive = 1;
    if( world->active_obj_stack_entity_count < MAX_OBJ_STACK_ENTITIES )
        world->active_obj_stack_entities[world->active_obj_stack_entity_count++] = (int32_t)n;
    return (int)n;
}

void
world_obj_stacks_ensure(struct World* world)
{
    if( world->obj_stacks )
        return;

    world->obj_stacks =
        (struct ObjStackEntry***)calloc(MAP_TERRAIN_LEVELS, sizeof(struct ObjStackEntry**));
    for( int l = 0; l < MAP_TERRAIN_LEVELS; l++ )
    {
        world->obj_stacks[l] = (struct ObjStackEntry**)calloc(
            ZONE_SCENE_SIZE * ZONE_SCENE_SIZE, sizeof(struct ObjStackEntry*));
    }

    int ntile = MAP_TERRAIN_LEVELS * ZONE_SCENE_SIZE * ZONE_SCENE_SIZE;
    world->obj_stack_entity_by_tile = (int32_t*)malloc(sizeof(int32_t) * (size_t)ntile);
    for( int i = 0; i < ntile; i++ )
        world->obj_stack_entity_by_tile[i] = -1;
}

void
world_obj_stacks_free_all(struct World* world)
{
    if( !world )
        return;

    int nent = entity_vec_count(&world->obj_stack_entities);
    for( int i = 0; i < nent; i++ )
        world_cleanup_obj_stack_entity(world, i);
    world->active_obj_stack_entity_count = 0;

    if( world->obj_stacks )
    {
        for( int l = 0; l < MAP_TERRAIN_LEVELS; l++ )
        {
            if( !world->obj_stacks[l] )
                continue;
            for( int t = 0; t < ZONE_SCENE_SIZE * ZONE_SCENE_SIZE; t++ )
            {
                struct ObjStackEntry* e = world->obj_stacks[l][t];
                while( e )
                {
                    struct ObjStackEntry* next = e->next;
                    free(e);
                    e = next;
                }
                world->obj_stacks[l][t] = NULL;
            }
            free(world->obj_stacks[l]);
            world->obj_stacks[l] = NULL;
        }
        free(world->obj_stacks);
        world->obj_stacks = NULL;
    }

    free(world->obj_stack_entity_by_tile);
    world->obj_stack_entity_by_tile = NULL;
}

struct World*
world_new(
    struct GameCache* gamecache,
    struct Scene2* scene2_shared)
{
    assert(scene2_shared != NULL && "World requires caller-owned Scene2");
    struct World* world = malloc(sizeof(struct World));
    memset(world, 0, sizeof(struct World));
    world->rebuild_prev_batch_id = SCENE2_GPU_BATCH_SLOT_INVALID;
    world->rebuild_current_batch_id = SCENE2_GPU_BATCH_SLOT_INVALID;

    world->scene2 = scene2_shared;
    world->painter = NULL;
    world->collision_map = NULL;
    world->heightmap = NULL;
    world->minimap = NULL;
    world->gamecache = gamecache;

    entity_vec_init(&world->players, sizeof(struct PlayerEntity), MAX_PLAYERS);
    entity_vec_init(&world->npcs, sizeof(struct NPCEntity), MAX_NPCS);
    entity_vec_init(&world->projectiles, sizeof(struct ProjectileEntity), MAX_PROJECTILES);
    entity_vec_init(
        &world->map_build_loc_entities,
        sizeof(struct MapBuildLocEntity),
        MAX_MAP_BUILD_LOC_ENTITIES);
    entity_vec_init(
        &world->map_build_tile_entities,
        sizeof(struct MapBuildTileEntity),
        MAX_MAP_BUILD_TILE_ENTITIES);
    entity_vec_init(
        &world->obj_stack_entities,
        sizeof(struct ObjStackEntity),
        MAX_OBJ_STACK_ENTITIES);

    world_prime_map_build_tile_slot0(world);

    /* Local player is fixed at ACTIVE_PLAYER_SLOT; prime so world_player(world, ...) is in range.
     */
    world_player_ensure(world, ACTIVE_PLAYER_SLOT);

    return world;
}

void
world_free(struct World* world)
{
    if( !world )
        return;

    for( int i = 0; i < entity_vec_count(&world->map_build_loc_entities); i++ )
        free(world_loc_entity(world, i)->actions);

    for( int i = 0; i < entity_vec_count(&world->npcs); i++ )
    {
        struct NPCEntity* npc = world_npc(world, i);
        free(npc->name);
        free(npc->description);
        free(npc->actions);
    }

    entity_vec_free(&world->players);
    entity_vec_free(&world->npcs);
    entity_vec_free(&world->projectiles);
    entity_vec_free(&world->map_build_loc_entities);
    entity_vec_free(&world->map_build_tile_entities);

    world_obj_stacks_free_all(world);
    entity_vec_free(&world->obj_stack_entities);

    painters_cullmap_free(world->cullmap);
    world->cullmap = NULL;
    painter_free(world->painter);
    collision_map_free(world->collision_map);
    heightmap_free(world->heightmap);
    minimap_free(world->minimap);
    for( int ti = 0; ti < MAP_TERRAIN_LEVELS; ti++ )
    {
        if( world->terrain_va[ti] )
        {
            if( world->scene2 )
            {
                scene2_vertex_array_unregister(world->scene2, world->terrain_va[ti]);
                scene2_face_array_unregister(world->scene2, world->terrain_face_array[ti]);
            }
            else
            {
                dashvertexarray_free(world->terrain_va[ti]);
                if( world->terrain_face_array[ti] )
                    dashfacearray_free(world->terrain_face_array[ti]);
            }
            world->terrain_va[ti] = NULL;
            world->terrain_face_array[ti] = NULL;
        }
        else if( world->terrain_face_array[ti] && !world->scene2 )
        {
            dashfacearray_free(world->terrain_face_array[ti]);
            world->terrain_face_array[ti] = NULL;
        }
    }
    world->scene2 = NULL;
    if( world->decor_buildmap )
        decor_buildmap_free(world->decor_buildmap);
    if( world->lightmap )
        lightmap_free(world->lightmap);
    if( world->shademap )
        shademap2_free(world->shademap);
    if( world->overlaymap )
        overlaymap_free(world->overlaymap);
    if( world->sharelight_map )
        sharelight_map_free(world->sharelight_map);
    if( world->blendmap )
        blendmap_free(world->blendmap);
    if( world->terrain_shapemap )
        terrain_shape_map_free(world->terrain_shapemap);
    free(world->contour_ground_queue);
    world->contour_ground_queue = NULL;
    world->contour_ground_queue_count = 0;
    world->contour_ground_queue_cap = 0;
    free(world);
}

void
world_set_painters_cullmap(
    struct World* world,
    struct PaintersCullMap* cm)
{
    if( !world )
        return;
    if( world->cullmap )
        painters_cullmap_free(world->cullmap);
    world->cullmap = cm;
    if( world->painter )
        painter_set_cullmap(world->painter, cm);
}

static inline enum MinimapWallFlag
orientation_wall_flag(int orientation)
{
    switch( orientation & 0x3 )
    {
    case 0:
        return MINIMAP_WALL_WEST;
    case 1:
        return MINIMAP_WALL_NORTH;
    case 2:
        return MINIMAP_WALL_EAST;
    case 3:
        return MINIMAP_WALL_SOUTH;
    default:
        return MINIMAP_WALL_NONE;
    }
}

static inline enum MinimapWallFlag
orientation_wall_flag_diagonal(int orientation)
{
    switch( orientation & 0x3 )
    {
    case 0:
        return MINIMAP_WALL_NORTHEAST_SOUTHWEST;
    case 1:
        return MINIMAP_WALL_NORTHWEST_SOUTHEAST;
    case 2:
        return MINIMAP_WALL_NORTHEAST_SOUTHWEST;
    case 3:
        return MINIMAP_WALL_NORTHWEST_SOUTHEAST;
    default:
        return MINIMAP_WALL_NONE;
    }
}

static inline int
world_to_scene_x(
    struct World* world,
    int mapx,
    int chunk_x)
{
    return (chunk_x - world->_offset_x) + (mapx - world->_chunk_sw_x) * MAP_TERRAIN_X;
}

static inline int
world_to_scene_z(
    struct World* world,
    int mapz,
    int chunk_z)
{
    return (chunk_z - world->_offset_z) + (mapz - world->_chunk_sw_z) * MAP_TERRAIN_Z;
}

/** Euclidean `v mod 64` in 0..63 (C `%` can be negative when `v` is negative). */
static inline int
world_mod64_euclidean(int v)
{
    int r = v % 64;
    if( r < 0 )
        r += 64;
    return r;
}

struct FlagMap
{
    int* flags;
    int size_x;
    int size_z;
    int levels;
};

struct FlagMap*
flag_map_new(
    int size_x,
    int size_z,
    int levels)
{
    struct FlagMap* flag_map = malloc(sizeof(struct FlagMap));
    flag_map->flags = malloc(sizeof(int) * size_x * size_z * levels);
    memset(flag_map->flags, 0, sizeof(int) * size_x * size_z * levels);
    flag_map->size_x = size_x;
    flag_map->size_z = size_z;
    flag_map->levels = levels;
    return flag_map;
};

void
flag_map_free(struct FlagMap* flag_map)
{
    free(flag_map->flags);
    free(flag_map);
}

static inline int
flag_map_index(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < flag_map->size_x);
    assert(z >= 0 && z < flag_map->size_z);
    assert(level >= 0 && level < flag_map->levels);
    return x + z * flag_map->size_x + level * flag_map->size_x * flag_map->size_z;
}
int
flag_map_get(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level)
{
    return flag_map->flags[flag_map_index(flag_map, x, z, level)];
}

void
flag_map_set(
    struct FlagMap* flag_map,
    int x,
    int z,
    int level,
    int flag)
{
    flag_map->flags[flag_map_index(flag_map, x, z, level)] = flag;
}

#include "platforms/common/platform_memory.h"

static void
world_print_scene2_dashmodel_heap_stats(struct World* world)
{
    struct Scene2* scene2 = world->scene2;
    if( !scene2 )
        return;

    printf("world_buildcachedat_rebuild_centerzone: DashModel heap per scene2 element:\n");
    size_t total = 0;
    long long total_vertices = 0;
    long long total_faces = 0;
    int count = 0;
    for( int i = 0; i < scene2_elements_total(scene2); i++ )
    {
        struct Scene2Element* el = scene2_element_at((struct Scene2*)scene2, i);
        struct DashModel* m = scene2_element_dash_model(el);
        if( !m )
            continue;
        size_t bytes = dashmodel_heap_bytes(m);
        total += bytes;
        total_vertices += dashmodel_vertex_count(m);
        total_faces += dashmodel_face_count(m);
        count++;
    }
}

void
world_rebuild_centerzone_verify_buildcachedat_maps(struct World* world)
{
    struct GameCache* b = world->gamecache;
    int const mx0 = world->_chunk_sw_x;
    int const mz0 = world->_chunk_sw_z;
    int const mx1 = world->_chunk_ne_x;
    int const mz1 = world->_chunk_ne_z;

    for( int mapx = mx0; mapx <= mx1; mapx++ )
    {
        for( int mapz = mz0; mapz <= mz1; mapz++ )
        {
            struct GameCacheTerrainChunk* t = gamecache_get_map_terrain(b, mapx, mapz);
            if( !t )
            {
                fprintf(
                    stderr,
                    "world build: missing map terrain for chunk [%d,%d] (expected all chunks in "
                    "[%d,%d]..[%d,%d] inclusive)\n",
                    mapx,
                    mapz,
                    mx0,
                    mz0,
                    mx1,
                    mz1);
                abort();
            }
            struct GameCacheMapLocs* s = gamecache_get_scenery(b, mapx, mapz);
            if( !s )
            {
                fprintf(
                    stderr,
                    "world build: missing map scenery for chunk [%d,%d] (expected all chunks in "
                    "[%d,%d]..[%d,%d] inclusive)\n",
                    mapx,
                    mapz,
                    mx0,
                    mz0,
                    mx1,
                    mz1);
                abort();
            }
        }
    }
}

void
world_buildcachedat_rebuild_centerzone(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    struct GameCache* gamecache = world->gamecache;

    world_rebuild_centerzone_begin(world, zone_center_x, zone_center_z, scene_size);
    world_rebuild_centerzone_verify_buildcachedat_maps(world);

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
        {
            world_rebuild_centerzone_chunk_terrain(world, mapx, mapz);
        }
    }

    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
        {
            world_rebuild_centerzone_chunk_scenery(world, mapx, mapz);
        }
    }

    world_rebuild_centerzone_end(world);
}

void
world_cleanup_projectile_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->projectiles) )
        return;

    struct ProjectileEntity* p = world_projectile(world, entity_id);
    if( p->scene_element2.element_id != -1 )
    {
        scene2_element_release(world->scene2, p->scene_element2.element_id);
        p->scene_element2.element_id = -1;
    }
    memset(&p->draw_position, 0, sizeof(struct EntityDrawPosition));
    memset(&p->orientation, 0, sizeof(struct EntityOrientation));
    memset(&p->animation, 0, sizeof(struct EntityAnimation));
    memset(&p->visible_level, 0, sizeof(struct EntityVisibleLevel));
    p->alive = false;
}

int
world_projectile_create(struct World* world)
{
    if( !world )
        return -1;

    int32_t n = entity_vec_count(&world->projectiles);
    for( int32_t i = 0; i < n; i++ )
    {
        struct ProjectileEntity* p = world_projectile(world, i);
        if( !p->alive )
        {
            if( world->active_projectile_count < MAX_PROJECTILES )
                world->active_projectiles[world->active_projectile_count++] = (int32_t)i;
            return (int)i;
        }
    }

    if( n >= MAX_PROJECTILES )
        return -1;

    struct ProjectileEntity* p = world_projectile_ensure(world, n);
    assert(!p->alive);
    if( world->active_projectile_count < MAX_PROJECTILES )
        world->active_projectiles[world->active_projectile_count++] = (int32_t)n;
    return (int)n;
}

struct ProjectileEntity*
world_projectile_ensure_scene_element(
    struct World* world,
    int projectile_id,
    int rs_model_id)
{
    struct Scene2Element* element = NULL;
    struct ProjectileEntity* p = world_projectile_ensure(world, projectile_id);

    p->alive = true;
    if( p->scene_element2.element_id == -1 )
    {
        p->scene_element2.element_id = scene2_element_acquire_full(
            world->scene2,
            (int)entity_unified_id(ENTITY_KIND_PROJECTILE, projectile_id),
            SCENE2_ELEMENT_PROJECTILE,
            rs_model_id,
            rs_model_id);

        p->orientation.dst_yaw = 0;

        element = scene2_element_at(world->scene2, p->scene_element2.element_id);
        scene2_element_expect(element, "world_projectile_ensure_scene_element");
        if( !scene2_element_dash_position(element) )
            scene2_element_set_dash_position_ptr(element, dashposition_new());
    }
    return p;
}

/* =========================================================================
 * Centerzone rebuild stages: _begin / _chunk / _end
 *
 * world_buildcachedat_rebuild_centerzone is implemented by calling these in
 * sequence (begin, chunk loop, map cache clears, end, clear_map_chunks).
 *
 * Lua can use the same pieces to load and process one map chunk at a time,
 * releasing buildcache assets between chunks to keep peak memory low.
 *
 * Call order (contour ground needs full heightmap before scenery):
 *   world_rebuild_centerzone_begin(world, zonex, zonez, scene_size)
 *   for each (mapx, mapz):
 *       world_rebuild_centerzone_chunk_terrain(world, mapx, mapz)
 *   for each (mapx, mapz):
 *       world_rebuild_centerzone_chunk_scenery(world, mapx, mapz)
 *   world_rebuild_centerzone_end(world)
 *
 * Or world_rebuild_centerzone_chunk (terrain+scenery) per chunk for incremental Lua.
 * =========================================================================*/

/** Release Scene2 elements for players/NPCs so dynamic models are not carried across a GPU batch
 * clear; restored after `scene2_batch_end` via world_restore_scene2_dynamic_entities. */
static void
world_rebuild_clear_scene2_dynamic_entities(struct World* world)
{
    if( !world || !world->scene2 )
        return;

    struct PlayerEntity* lp = world_player(world, ACTIVE_PLAYER_SLOT);
    if( lp->alive && lp->scene_element2.element_id != -1 )
    {
        scene2_element_release(world->scene2, lp->scene_element2.element_id);
        lp->scene_element2.element_id = -1;
    }

    for( int i = 0; i < world->active_player_count; i++ )
    {
        int pid = world->active_players[i];
        if( pid < 0 || pid == ACTIVE_PLAYER_SLOT )
            continue;
        struct PlayerEntity* p = world_player(world, pid);
        if( p->alive && p->scene_element2.element_id != -1 )
        {
            scene2_element_release(world->scene2, p->scene_element2.element_id);
            p->scene_element2.element_id = -1;
        }
    }

    for( int i = 0; i < world->active_npc_count; i++ )
    {
        int nid = world->active_npcs[i];
        if( nid < 0 )
            continue;
        struct NPCEntity* n = world_npc(world, nid);
        if( n->alive && n->scene_element2.element_id != -1 )
        {
            scene2_element_release(world->scene2, n->scene_element2.element_id);
            n->scene_element2.element_id = -1;
        }
    }
}

/** After world geometry batch ends (`batch_active` false): re-bind meshes so MODEL_LOAD uses the
 * non-batched path and matches renderer caches. */
static void
world_restore_scene2_dynamic_entities(struct World* world)
{
    if( !world || !world->scene2 )
        return;

    uint16_t app12[12];
    uint16_t col5[5];

    struct PlayerEntity* lp = world_player(world, ACTIVE_PLAYER_SLOT);
    if( lp->alive )
    {
        world_player_ensure_scene_element(world, ACTIVE_PLAYER_SLOT);
        for( int i = 0; i < 12; i++ )
            app12[i] = (uint16_t)lp->appearance.slots[i];
        for( int i = 0; i < 5; i++ )
            col5[i] = (uint16_t)lp->appearance.colors[i];
        world_scenebuild_player_entity_set_appearance(world, ACTIVE_PLAYER_SLOT, app12, col5);
        if( lp->animation.primary_anim.anim_id != 0 &&
            lp->animation.primary_anim.anim_id != (uint16_t)-1 )
            world_player_entity_set_animation(
                world,
                ACTIVE_PLAYER_SLOT,
                (int)lp->animation.primary_anim.anim_id,
                ANIMATION_TYPE_PRIMARY,
                lp->animation.primary_anim.delay);
        if( lp->animation.secondary_anim.anim_id != 0 &&
            lp->animation.secondary_anim.anim_id != (uint16_t)-1 )
            world_player_entity_set_animation(
                world,
                ACTIVE_PLAYER_SLOT,
                (int)lp->animation.secondary_anim.anim_id,
                ANIMATION_TYPE_SECONDARY,
                0);
    }

    for( int i = 0; i < world->active_player_count; i++ )
    {
        int pid = world->active_players[i];
        if( pid < 0 || pid == ACTIVE_PLAYER_SLOT )
            continue;
        struct PlayerEntity* p = world_player(world, pid);
        if( !p->alive )
            continue;
        world_player_ensure_scene_element(world, pid);
        for( int j = 0; j < 12; j++ )
            app12[j] = (uint16_t)p->appearance.slots[j];
        for( int j = 0; j < 5; j++ )
            col5[j] = (uint16_t)p->appearance.colors[j];
        world_scenebuild_player_entity_set_appearance(world, pid, app12, col5);
        if( p->animation.primary_anim.anim_id != 0 &&
            p->animation.primary_anim.anim_id != (uint16_t)-1 )
            world_player_entity_set_animation(
                world, pid, (int)p->animation.primary_anim.anim_id, ANIMATION_TYPE_PRIMARY,
                p->animation.primary_anim.delay);
        if( p->animation.secondary_anim.anim_id != 0 &&
            p->animation.secondary_anim.anim_id != (uint16_t)-1 )
            world_player_entity_set_animation(
                world, pid, (int)p->animation.secondary_anim.anim_id, ANIMATION_TYPE_SECONDARY,
                0);
    }

    for( int i = 0; i < world->active_npc_count; i++ )
    {
        int nid = world->active_npcs[i];
        if( nid < 0 )
            continue;
        struct NPCEntity* n = world_npc(world, nid);
        if( !n->alive )
            continue;
        world_npc_ensure_scene_element(world, nid);
        world_scenebuild_npc_entity_reload_scene2_model(world, nid);
        if( n->animation.primary_anim.anim_id != 0 &&
            n->animation.primary_anim.anim_id != (uint16_t)-1 )
            world_npc_entity_set_animation(
                world, nid, (int)n->animation.primary_anim.anim_id, ANIMATION_TYPE_PRIMARY);
        if( n->animation.secondary_anim.anim_id != 0 &&
            n->animation.secondary_anim.anim_id != (uint16_t)-1 )
            world_npc_entity_set_animation(
                world, nid, (int)n->animation.secondary_anim.anim_id, ANIMATION_TYPE_SECONDARY);
    }
}

void
world_rebuild_centerzone_begin(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    struct GameCache* gamecache = world->gamecache;

    world->load_complete = false;

    for( int i = 0; i < world->active_projectile_count; i++ )
        world_cleanup_projectile_entity(world, world->active_projectiles[i]);
    world->active_projectile_count = 0;

    world_obj_stacks_free_all(world);

    world_rebuild_clear_scene2_dynamic_entities(world);

    if( world->scene2 )
    {
        if( world->rebuild_prev_batch_id != SCENE2_GPU_BATCH_SLOT_INVALID )
            scene2_batch_clear(world->scene2, world->rebuild_prev_batch_id);
        world->rebuild_prev_batch_id = SCENE2_GPU_BATCH_SLOT_INVALID;
        scene2_gpu_batches_reset(world->scene2);
        world->rebuild_current_batch_id = scene2_batch_begin(world->scene2);
    }

    /* Need (2*padding+1)*8 >= scene_size; smallest padding is ceil((scene_size-8)/16). */
    int zone_padding = scene_size > 8 ? (scene_size - 8 + 15) / 16 : 0;
    int zone_sw_x = zone_center_x - zone_padding;
    int zone_sw_z = zone_center_z - zone_padding;
    int zone_ne_x = zone_center_x + zone_padding;
    int zone_ne_z = zone_center_z + zone_padding;

    int world_sw_x = zone_sw_x * 8;
    int world_sw_z = zone_sw_z * 8;

    world->_offset_x = world_mod64_euclidean(world_sw_x);
    world->_offset_z = world_mod64_euclidean(world_sw_z);

    world->_base_tile_x = zone_sw_x * 8;
    world->_base_tile_z = zone_sw_z * 8;

    /* Tear down existing world state (same as monolithic rebuild). */
    if( world->painter )
        painter_free(world->painter);
    if( world->collision_map )
        collision_map_free(world->collision_map);
    if( world->heightmap )
        heightmap_free(world->heightmap);
    if( world->minimap )
        minimap_free(world->minimap);

    if( world->lightmap )
        lightmap_free(world->lightmap);
    if( world->shademap )
        shademap2_free(world->shademap);
    if( world->blendmap )
        blendmap_free(world->blendmap);
    if( world->overlaymap )
        overlaymap_free(world->overlaymap);
    if( world->terrain_shapemap )
        terrain_shape_map_free(world->terrain_shapemap);
    if( world->decor_buildmap )
        decor_buildmap_free(world->decor_buildmap);
    if( world->sharelight_map )
        sharelight_map_free(world->sharelight_map);

    for( int ti = 0; ti < MAP_TERRAIN_LEVELS; ti++ )
    {
        if( world->terrain_va[ti] )
        {
            if( world->scene2 )
            {
                scene2_vertex_array_unregister(world->scene2, world->terrain_va[ti]);
                scene2_face_array_unregister(world->scene2, world->terrain_face_array[ti]);
            }
            else
            {
                dashvertexarray_free(world->terrain_va[ti]);
                if( world->terrain_face_array[ti] )
                    dashfacearray_free(world->terrain_face_array[ti]);
            }
            world->terrain_va[ti] = NULL;
            world->terrain_face_array[ti] = NULL;
        }
        else if( world->terrain_face_array[ti] && !world->scene2 )
        {
            dashfacearray_free(world->terrain_face_array[ti]);
            world->terrain_face_array[ti] = NULL;
        }
    }

    {
        int prev_scene = world->_scene_size;
        int tile_slots = 0;
        if( prev_scene > 0 )
            tile_slots = prev_scene * prev_scene * MAP_TERRAIN_LEVELS;
        else
            tile_slots = entity_vec_count(&world->map_build_tile_entities);
        if( tile_slots > MAX_MAP_BUILD_TILE_ENTITIES )
            tile_slots = MAX_MAP_BUILD_TILE_ENTITIES;
        for( int i = 0; i < tile_slots; i++ )
        {
            world_cleanup_map_build_tile_entity(world, i);
        }
        entity_vec_free(&world->map_build_tile_entities);
        entity_vec_init(
            &world->map_build_tile_entities,
            sizeof(struct MapBuildTileEntity),
            MAX_MAP_BUILD_TILE_ENTITIES);
        world_prime_map_build_tile_slot0(world);
    }
    /* Release every allocated map-build loc slot (high-water entity_vec count), then reset the vec
     * logical length so it tracks active_loc_entity_count again. Relying only on active_loc_entities[]
     * can miss slots if bookkeeping diverges; stale vec count also skewed contour_ground bounds. */
    {
        int loc_slots = entity_vec_count(&world->map_build_loc_entities);
        for( int i = 0; i < loc_slots; i++ )
            world_cleanup_map_build_loc_entity(world, i);
        world->active_loc_entity_count = 0;
        entity_vec_reset(&world->map_build_loc_entities);
    }

    struct PlatformMemoryInfo mem = { 0 };
    platform_get_memory_info(&mem);
    {
        char mem_line[160];
        mem_format_heap_triple(
            mem_line, sizeof mem_line, mem.heap_used, mem.heap_total, mem.heap_peak);
        printf("Pre-Alloc: Memory info: %s\n", mem_line);
    }

    world->painter = painter_new(
        scene_size,
        scene_size,
        MAP_TERRAIN_LEVELS,
        PAINTER_NEW_CTX_BUCKET | PAINTER_NEW_CTX_WORLD3D);
    painter_set_cullmap(world->painter, world->cullmap);

    world->collision_map = collision_map_new(scene_size, scene_size);
    /* +1: corner posts (matches Client-TS groundh SIZE+1). */
    world->heightmap = heightmap_new(scene_size + 1, scene_size + 1, MAP_TERRAIN_LEVELS);
    world->minimap = minimap_new(scene_size, scene_size);

    /* Allocate blendmap, overlaymap, shapemap, and decor_buildmap now so _chunk_terrain can
     * populate them while terrain data is already loaded.
     * Shademap and sharelight_map are also needed during scenery_add inside _chunk_scenery. */
    world->blendmap = blendmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->overlaymap = overlaymap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->terrain_shapemap = terrain_shape_map_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->decor_buildmap = decor_buildmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->shademap = shademap2_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    world->sharelight_map = sharelight_map_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);

    struct PlatformMemoryInfo mem2 = { 0 };
    platform_get_memory_info(&mem2);
    {
        char mem_line[160];
        mem_format_heap_triple(
            mem_line, sizeof mem_line, mem2.heap_used, mem2.heap_total, mem2.heap_peak);
        printf("Post-Alloc: Memory info: %s\n", mem_line);
    }

    int chunk_sw_x = zone_sw_x / 8;
    int chunk_sw_z = zone_sw_z / 8;
    int chunk_ne_x = zone_ne_x / 8;
    int chunk_ne_z = zone_ne_z / 8;
    world->_chunk_sw_x = chunk_sw_x;
    world->_chunk_sw_z = chunk_sw_z;
    world->_chunk_ne_x = chunk_ne_x;
    world->_chunk_ne_z = chunk_ne_z;
    world->_scene_size = scene_size;

    /* Allocate the flag map used for bridge detection; stored on world until _end. */
    world->_build_flag_map = flag_map_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);

    world->contour_ground_queue_count = 0;

    printf("world_rebuild_centerzone_begin: done\n");

    (void)gamecache;
}

void
world_rebuild_centerzone_chunk_terrain(
    struct World* world,
    int mapx,
    int mapz)
{
    struct GameCache* gamecache = world->gamecache;
    int scene_size = world->_scene_size;
    int offset_x;
    int offset_z;

    /* ---- Heightmap + flag_map ---- */
    {
        struct GameCacheTerrainChunk* map_terrain =
            gamecache_get_map_terrain(gamecache, mapx, mapz);
        assert(map_terrain && "Map terrain must be found");

        for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
        {
            for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
            {
                offset_x = world_to_scene_x(world, mapx, tile_x);
                offset_z = world_to_scene_z(world, mapz, tile_z);

                for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                {
                    int chunk_index = MAP_TILE_COORD(tile_x, tile_z, level);
                    struct GameCacheFloorTile* tile = &map_terrain->tiles[chunk_index];
                    int height = tile->height;

                    if( offset_x >= 0 && offset_z >= 0 && offset_x <= scene_size &&
                        offset_z <= scene_size )
                    {
                        heightmap_set(world->heightmap, offset_x, offset_z, level, height);
                    }

                    if( offset_x >= 0 && offset_z >= 0 && offset_x < scene_size &&
                        offset_z < scene_size )
                    {
                        flag_map_set(
                            world->_build_flag_map, offset_x, offset_z, level, tile->settings);
                        /* Match ClientBuild.finishBuild: trueLevel = level - (mapl[1] has LinkBelow).
                         * C only maintains collision[0]; only apply when trueLevel == 0. */
                        if( (tile->settings & FLOFLAG_BLOCK) != 0 )
                        {
                            int level1_settings =
                                map_terrain->tiles[MAP_TILE_COORD(tile_x, tile_z, 1)].settings;
                            int has_link_below = (level1_settings & FLOFLAG_LINK_BELOW) != 0;
                            int true_level = level - (has_link_below ? 1 : 0);
                            if( true_level == 0 )
                                collision_map_add_floor(world->collision_map, offset_x, offset_z);
                        }
                    }
                }
            }
        }

        /* ---- Blendmap (underlay RGB) ---- */
        for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
        {
            for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
            {
                for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                {
                    offset_x = world_to_scene_x(world, mapx, tile_x);
                    offset_z = world_to_scene_z(world, mapz, tile_z);

                    if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                        offset_z >= scene_size )
                        continue;

                    struct GameCacheFloorTile* tile2 =
                        &map_terrain->tiles[MAP_TILE_COORD(tile_x, tile_z, level)];
                    if( tile2->underlay_id > 0 )
                    {
                        struct GameCacheOverlay* flotype =
                            gamecache_get_flotype(gamecache, tile2->underlay_id - 1);
                        assert(flotype && "Flotype must be found");

                        blendmap_set_underlay_rgb(
                            world->blendmap, offset_x, offset_z, level, flotype->rgb_color);
                    }
                }
            }
        }

        /* ---- Overlaymap + shapemap ---- */
        for( int tile_x = 0; tile_x < MAP_TERRAIN_X; tile_x++ )
        {
            for( int tile_z = 0; tile_z < MAP_TERRAIN_Z; tile_z++ )
            {
                for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
                {
                    offset_x = world_to_scene_x(world, mapx, tile_x);
                    offset_z = world_to_scene_z(world, mapz, tile_z);

                    if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size ||
                        offset_z >= scene_size )
                        continue;

                    struct GameCacheFloorTile* tile2 =
                        &map_terrain->tiles[MAP_TILE_COORD(tile_x, tile_z, level)];

                    int overlay_id = tile2->overlay_id - 1;
                    int underlay_id = tile2->underlay_id - 1;

                    if( overlay_id != -1 )
                    {
                        struct GameCacheOverlay* flotype =
                            gamecache_get_flotype(gamecache, overlay_id);
                        assert(flotype && "Flotype must be found");

                        overlaymap_set_tile_rgb(
                            world->overlaymap, offset_x, offset_z, level, flotype->rgb_color);

                        if( flotype->texture != -1 )
                        {
                            struct DashTexture* dash_texture =
                                scene2_texture_get(world->scene2, flotype->texture);
                            assert(dash_texture && "Dash texture must be found");

                            int average_hsl = dash_texture_average_hsl(dash_texture);
                            overlaymap_set_tile_texture(
                                world->overlaymap,
                                offset_x,
                                offset_z,
                                level,
                                flotype->texture,
                                average_hsl);
                        }

                        if( flotype->secondary_rgb_color > 0 )
                        {
                            overlaymap_set_tile_minimap(
                                world->overlaymap,
                                offset_x,
                                offset_z,
                                level,
                                flotype->secondary_rgb_color);
                        }
                    }

                    if( underlay_id != -1 || overlay_id != -1 )
                    {
                        int shape = 0;
                        int rotation = 0;

                        if( overlay_id != -1 )
                        {
                            shape = tile2->shape + 1;
                            rotation = tile2->rotation;
                        }

                        terrain_shape_map_set_tile(
                            world->terrain_shapemap, offset_x, offset_z, level, shape, rotation);
                    }
                }
            }
        }
    }
}

void
world_rebuild_centerzone_chunk_scenery(
    struct World* world,
    int mapx,
    int mapz)
{
    struct GameCache* gamecache = world->gamecache;
    int scene_size = world->_scene_size;
    int offset_x;
    int offset_z;
    struct GameCacheMapLoc* map_tile = NULL;
    struct GameCacheLoc* config_loc = NULL;
#if WORLD_DEBUG_REBUILD_SCENERY_CULL
    int dbg_entity_culled = 0;
    int dbg_entity_max_ox = INT_MIN;
    int dbg_entity_max_oz = INT_MIN;
#endif
#if WORLD_DEBUG_PAINTER_LOC_BUILD
    int dbg_painter_chunk_locs = 0;
    int dbg_painter_chunk_in_scene = 0;
#endif

    /* ---- Collision map ---- */
    {
        struct GameCacheMapLocs* map_locs = gamecache_get_scenery(gamecache, mapx, mapz);
        assert(map_locs && "Map scenery must be found");
        struct GameCacheTerrainChunk* map_terrain_col =
            gamecache_get_map_terrain(gamecache, mapx, mapz);
        assert(map_terrain_col && "Map terrain must be found for collision level (LinkBelow)");
#if WORLD_DEBUG_PAINTER_LOC_BUILD
        dbg_painter_chunk_locs = map_locs->locs_count;
#endif

        for( int i = 0; i < map_locs->locs_count; i++ )
        {
            map_tile = &map_locs->locs[i];

            offset_x = world_to_scene_x(world, mapx, map_tile->chunk_pos_x);
            offset_z = world_to_scene_z(world, mapz, map_tile->chunk_pos_z);

            if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size || offset_z >= scene_size )
                continue;

            /* ClientBuild.loadLocations: currentLevel = level - (mapl[1][stx][stz] & LinkBelow).
             * We only maintain world->collision_map for CURRENT_LEVEL (ground pathfinding). */
            {
                int l1_settings = map_terrain_col
                                      ->tiles[MAP_TILE_COORD(
                                          map_tile->chunk_pos_x, map_tile->chunk_pos_z, 1)]
                                      .settings;
                int collision_level = map_tile->chunk_pos_level -
                                      (((l1_settings & FLOFLAG_LINK_BELOW) != 0) ? 1 : 0);
                if( collision_level != CURRENT_LEVEL )
                    continue;
            }

            config_loc = gamecache_get_config_loc(gamecache, map_tile->loc_id);
            assert(config_loc && "Config location must be found");

            int size_x = config_loc->size_x;
            int size_z = config_loc->size_z;
            /* Match scenebuilder / Client: wall+loc angle is low 2 bits only. */
            int angle = map_tile->orientation & 0x3;
            int blockrange = config_loc->blocks_projectiles ? 1 : 0;

            switch( map_tile->shape_select )
            {
            case LOC_SHAPE_FLOOR_DECORATION:
            {
                /* Client: blockGround only when loc.blockwalk && loc.active (is_interactive). */
                if( config_loc->blocks_walk != 0 && config_loc->is_interactive )
                    collision_map_add_floor(world->collision_map, offset_x, offset_z);
                break;
            }
            case LOC_SHAPE_WALL_SINGLE_SIDE:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_wall(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        LOC_SHAPE_WALL_SINGLE_SIDE,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_WALL_TRI_CORNER:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_wall(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        LOC_SHAPE_WALL_TRI_CORNER,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_WALL_TWO_SIDES:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_wall(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        LOC_SHAPE_WALL_TWO_SIDES,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_WALL_RECT_CORNER:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_wall(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        LOC_SHAPE_WALL_RECT_CORNER,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_WALL_DIAGONAL:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_loc(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        size_x,
                        size_z,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_SCENERY:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_loc(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        size_x,
                        size_z,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_SCENERY_DIAGIONAL:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_loc(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        size_x,
                        size_z,
                        angle,
                        blockrange);
                break;
            }
            case LOC_SHAPE_ROOF_SLOPED:
            case LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
            case LOC_SHAPE_ROOF_FLAT:
            case LOC_SHAPE_ROOF_SLOPED_OVERHANG:
            case LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
            {
                if( config_loc->blocks_walk != 0 )
                    collision_map_add_loc(
                        world->collision_map,
                        offset_x,
                        offset_z,
                        size_x,
                        size_z,
                        angle,
                        blockrange);
                break;
            }
            default:
                break;
            }
        }

        /* ---- Minimap walls ---- */
        for( int i = 0; i < map_locs->locs_count; i++ )
        {
            map_tile = &map_locs->locs[i];
            offset_x = world_to_scene_x(world, mapx, map_tile->chunk_pos_x);
            offset_z = world_to_scene_z(world, mapz, map_tile->chunk_pos_z);

            if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size || offset_z >= scene_size )
                continue;

            config_loc = gamecache_get_config_loc(gamecache, map_tile->loc_id);

            int level = map_tile->chunk_pos_level;
            if( config_loc->map_scene_id != -1 )
                continue;

            if( level != CURRENT_LEVEL )
                continue;

            switch( map_tile->shape_select )
            {
            case LOC_SHAPE_WALL_SINGLE_SIDE:
            {
                minimap_add_tile_wall(
                    world->minimap,
                    offset_x,
                    offset_z,
                    orientation_wall_flag(map_tile->orientation));
                break;
            }
            case LOC_SHAPE_WALL_TRI_CORNER:
                break;
            case LOC_SHAPE_WALL_TWO_SIDES:
            {
                minimap_add_tile_wall(
                    world->minimap,
                    offset_x,
                    offset_z,
                    orientation_wall_flag(map_tile->orientation));

                int next_orientation = (map_tile->orientation + 1) & 0x3;

                minimap_add_tile_wall(
                    world->minimap, offset_x, offset_z, orientation_wall_flag(next_orientation));
                break;
            }
            case LOC_SHAPE_WALL_RECT_CORNER:
                break;
            case LOC_SHAPE_WALL_DIAGONAL:
            {
                minimap_add_tile_wall(
                    world->minimap,
                    offset_x,
                    offset_z,
                    orientation_wall_flag_diagonal(map_tile->orientation));
                break;
            }
            case LOC_SHAPE_SCENERY:
            case LOC_SHAPE_SCENERY_DIAGIONAL:
            case LOC_SHAPE_ROOF_SLOPED:
            case LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
            case LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
            case LOC_SHAPE_ROOF_FLAT:
            case LOC_SHAPE_ROOF_SLOPED_OVERHANG:
                break;
            }
        }

        /* ---- Scenery entities (scenery_add) ---- */
        for( int i = 0; i < map_locs->locs_count; i++ )
        {
            map_tile = &map_locs->locs[i];
            config_loc = gamecache_get_config_loc(gamecache, map_tile->loc_id);
            assert(config_loc && "Config location must be found");

            offset_x = world_to_scene_x(world, mapx, map_tile->chunk_pos_x);
            offset_z = world_to_scene_z(world, mapz, map_tile->chunk_pos_z);

#if WORLD_DEBUG_REBUILD_SCENERY_CULL
            if( offset_x >= 0 && offset_z >= 0 && offset_x < scene_size && offset_z < scene_size )
            {
                if( offset_x > dbg_entity_max_ox )
                    dbg_entity_max_ox = offset_x;
                if( offset_z > dbg_entity_max_oz )
                    dbg_entity_max_oz = offset_z;
            }
            else
                dbg_entity_culled++;
#endif
            if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size || offset_z >= scene_size )
                continue;

#if WORLD_DEBUG_PAINTER_LOC_BUILD
            dbg_painter_chunk_in_scene++;
#endif
            struct MapBuildLocEntity* entity = next_map_build_loc_entity(world);
            entity->scene_coord.sx = offset_x;
            entity->scene_coord.sz = offset_z;
            entity->scene_coord.slevel = map_tile->chunk_pos_level;
            entity->loc_type_id = map_tile->loc_id;
            entity->shape_select = (uint8_t)map_tile->shape_select;
            entity->orientation = (uint8_t)map_tile->orientation;
            scenery_add(world, entity, map_tile, config_loc);
        }

#if WORLD_DEBUG_REBUILD_SCENERY_CULL
        printf(
            "[scenery_cull] map=(%d,%d) scene_size=%d entity_culled=%d max_inbounds_ox=%d max_inbounds_oz=%d\n",
            mapx,
            mapz,
            scene_size,
            dbg_entity_culled,
            dbg_entity_max_ox == INT_MIN ? -1 : dbg_entity_max_ox,
            dbg_entity_max_oz == INT_MIN ? -1 : dbg_entity_max_oz);
#endif
#if WORLD_DEBUG_PAINTER_LOC_BUILD
        printf(
            "[painter_loc_chunk] map=(%d,%d) locs_in_map=%d passed_scene_tile_bounds=%d "
            "scene_size=%d world_off=(%d,%d)\n",
            mapx,
            mapz,
            dbg_painter_chunk_locs,
            dbg_painter_chunk_in_scene,
            scene_size,
            world->_offset_x,
            world->_offset_z);
#endif
    }
}

void
world_rebuild_centerzone_chunk(
    struct World* world,
    int mapx,
    int mapz)
{
    world_rebuild_centerzone_chunk_terrain(world, mapx, mapz);
    world_rebuild_centerzone_chunk_scenery(world, mapx, mapz);
}

void
world_rebuild_centerzone_end(struct World* world)
{
    struct GameCache* gamecache = world->gamecache;
    int scene_size = world->_scene_size;

    world_contour_ground(world);

    /* ---- Decor wall-offset pass (scene-local, not chunk-indexed) ---- */
    struct DecorElementsOnWall* elements = NULL;
    struct Scene2Element* scene_element = NULL;
    for( int sx = 0; sx < scene_size; sx++ )
    {
        for( int sz = 0; sz < scene_size; sz++ )
        {
            for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
            {
                int wall_width =
                    decor_buildmap_get_wall_offset(world->decor_buildmap, sx, sz, level);

                elements = decor_buildmap_get_elements(world->decor_buildmap, sx, sz, level);

                for( int i = 0; i < elements->count; i++ )
                {
                    int element_id = elements->element_id[i];
                    int displacement_kind = elements->displacement_kind[i];
                    int orientation = elements->orientation[i];
                    scene_element = scene2_element_at(world->scene2, element_id);
                    scene2_element_expect(scene_element, "decor_buildmap wall offset");

                    bool diagonal = false;
                    int offset = 0;
                    switch( displacement_kind )
                    {
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET:
                        offset += wall_width;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET:
                        offset += (wall_width / 16) * 8 + (45);
                        diagonal = true;
                        break;
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT:
                        offset += 0;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL:
                        offset += 45;
                        diagonal = true;
                        break;
                    }

                    calculate_wall_decor_offset(
                        scene2_element_dash_position(scene_element), orientation, offset, diagonal);
                }
            }
        }
    }

    printf("Alive Decor Buildmap\n");

    /* ---- Bridge adjustment (LinkBelow on cache level 1) ---- */
    int bridge_flags = 0;
    struct PaintersTile bridge_tile_tmp = { 0 };
    for( int x = 0; x < scene_size; x++ )
    {
        for( int z = 0; z < scene_size; z++ )
        {
            bridge_flags = flag_map_get(world->_build_flag_map, x, z, 1);

            if( (bridge_flags & FLOFLAG_LINK_BELOW) != 0 )
            {
                bridge_tile_tmp = *painter_tile_at(world->painter, x, z, 0);

                for( int level = 0; level < painter_max_levels(world->painter) - 1; level++ )
                {
                    painter_tile_copyto(world->painter, x, z, level + 1, x, z, level);
                }

                *painter_tile_at(world->painter, x, z, 3) = bridge_tile_tmp;
                /* grid_level becomes 3; terrain_level stays from bridge_tile_tmp (old grid 0). */
                painters_tile_set_grid_level(painter_tile_at(world->painter, x, z, 3), 3);
                painter_tile_set_bridge(world->painter, x, z, 0, x, z, 3);
            }
        }
    }

    /*
     * Painter draw level (slevel) from cache flags, matching ClientBuild.getVisBelowLevel +
     * pushDown source mapping: for each painter grid level g, content came from cache level
     * src (inverse of push-down when LinkBelow on level 1).
     */
#ifndef WORLD_DEBUG_VIS_BELOW_DRAW
#define WORLD_DEBUG_VIS_BELOW_DRAW 0
#endif
#if WORLD_DEBUG_VIS_BELOW_DRAW
    int world_dbg_vis_below_samples = 0;
#endif
    for( int x = 0; x < scene_size; x++ )
    {
        for( int z = 0; z < scene_size; z++ )
        {
            int link_l1 = (flag_map_get(world->_build_flag_map, x, z, 1) & FLOFLAG_LINK_BELOW) != 0;
            for( int g = 0; g < painter_max_levels(world->painter); g++ )
            {
                int src = link_l1 ? (g < 3 ? g + 1 : 0) : g;
                uint8_t st = (uint8_t)flag_map_get(world->_build_flag_map, x, z, src);
                int draw = map_floor_vis_below_draw_level(st, src, link_l1);
                painter_tile_set_draw_level(world->painter, x, z, g, draw);
#if WORLD_DEBUG_VIS_BELOW_DRAW
                if( world_dbg_vis_below_samples < 200 &&
                    (draw != g || (st & FLOFLAG_VIS_BELOW) != 0 || link_l1 != 0) )
                {
                    printf(
                        "[vis_below] sx=%d sz=%d grid=%d src_cache=%d settings=0x%02x "
                        "draw_level=%d "
                        "link_l1=%d\n",
                        x,
                        z,
                        g,
                        src,
                        (unsigned)st,
                        draw,
                        link_l1);
                    world_dbg_vis_below_samples++;
                }
#endif
            }
        }
    }

    printf("Alive Bridges\n");

    flag_map_free(world->_build_flag_map);
    world->_build_flag_map = NULL;

    painter_mark_static_count(world->painter);

#if WORLD_DEBUG_PAINTER_LOC_BUILD
    {
        int const n = painter_element_count(world->painter);
        int k_scenery = 0;
        int k_wall_a = 0;
        int k_wall_b = 0;
        int k_gdecor = 0;
        int k_wdecor = 0;
        int k_gobj = 0;
        int k_ground = 0;
        for( int i = 0; i < n; i++ )
        {
            struct PaintersElement* el = painter_element_at(world->painter, i);
            switch( el->kind )
            {
            case PNTRELEM_SCENERY:
                k_scenery++;
                break;
            case PNTRELEM_WALL_A:
                k_wall_a++;
                break;
            case PNTRELEM_WALL_B:
                k_wall_b++;
                break;
            case PNTRELEM_GROUND_DECOR:
                k_gdecor++;
                break;
            case PNTRELEM_WALL_DECOR:
                k_wdecor++;
                break;
            case PNTRELEM_GROUND_OBJECT:
                k_gobj++;
                break;
            case PNTRELEM_GROUND:
                k_ground++;
                break;
            default:
                break;
            }
        }
        printf(
            "[painter_loc_end] scene=%d chunks=[%d,%d]..[%d,%d] map_build_locs=%d "
            "painter_elements=%d kind: scenery=%d wall_a=%d wall_b=%d gdecor=%d wdecor=%d gobj=%d "
            "ground=%d\n",
            scene_size,
            world->_chunk_sw_x,
            world->_chunk_sw_z,
            world->_chunk_ne_x,
            world->_chunk_ne_z,
            world->active_loc_entity_count,
            n,
            k_scenery,
            k_wall_a,
            k_wall_b,
            k_gdecor,
            k_wdecor,
            k_gobj,
            k_ground);
    }
#endif

    /* ---- Allocate build-only structures needed for lighting ---- */
    world->lightmap = lightmap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS);
    /* shademap and sharelight_map are already allocated in _begin (needed by scenery_add). */

    printf("Alive Lightmap\n");

    lightmap_build(world->lightmap, world->heightmap);

    printf("Alive Blendmap\n");

    blendmap_build(world->blendmap);

    /* ---- Terrain geometry ---- */
#if WORLD_BUILD_TERRAIN_VA
    build_scene_terrain_va(world);
#else
    build_scene_terrain(world);
#endif

    overlaymap_free(world->overlaymap);
    world->overlaymap = NULL;
    decor_buildmap_free(world->decor_buildmap);
    world->decor_buildmap = NULL;

    world_build_lighting(world);

    lightmap_free(world->lightmap);
    world->lightmap = NULL;
    shademap2_free(world->shademap);
    world->shademap = NULL;
    blendmap_free(world->blendmap);
    world->blendmap = NULL;
    terrain_shape_map_free(world->terrain_shapemap);
    world->terrain_shapemap = NULL;
    sharelight_map_free(world->sharelight_map);
    world->sharelight_map = NULL;

    world_print_scene2_dashmodel_heap_stats(world);

    if( world->scene2 )
    {
        scene2_batch_end(world->scene2);
        world->rebuild_prev_batch_id = world->rebuild_current_batch_id;
    }

    world_restore_scene2_dynamic_entities(world);

    world->load_complete = true;
}

void
world_cleanup_map_build_loc_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= MAX_MAP_BUILD_LOC_ENTITIES )
        return;
    if( entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;

    struct MapBuildLocEntity* entity = world_loc_entity(world, entity_id);

    scene2_element_release(world->scene2, entity->scene_element.element_id);
    scene2_element_release(world->scene2, entity->scene_element_two.element_id);
    entity->scene_element.element_id = -1;
    entity->scene_element_two.element_id = -1;
    memset(&entity->scene_coord, 0, sizeof(struct EntitySceneCoord));
    free(entity->actions);
    entity->actions = NULL;
    entity->action_count = 0;
}

void
world_loc_entity_reload_model(
    struct World* world,
    int entity_id,
    int new_loc_id,
    int new_shape,
    int new_angle)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;

    struct MapBuildLocEntity* entity = world_loc_entity(world, entity_id);

    struct GameCacheLoc* config_loc =
        gamecache_get_config_loc(world->gamecache, new_loc_id);
    if( !config_loc )
        return;

    bool animated = config_loc->seq_id != -1;

    /* After LOC_DEL the Scene2 slots were released; acquire before loading models. */
    if( entity->scene_element.element_id < 0 )
        entity->scene_element.element_id =
            scenery_element_acquire(world, entity_id, animated);
    if( entity->scene_element.element_id < 0 )
        return;

    bool needs_secondary = ( new_shape == LOC_SHAPE_WALL_TWO_SIDES ||
                               new_shape == LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE );

    if( needs_secondary )
    {
        if( entity->scene_element_two.element_id < 0 )
            entity->scene_element_two.element_id =
                scenery_element_acquire(world, entity_id, animated);
        if( entity->scene_element_two.element_id < 0 )
            return;
    }
    else if( entity->scene_element_two.element_id >= 0 )
    {
        scene2_element_release(world->scene2, entity->scene_element_two.element_id);
        entity->scene_element_two.element_id = -1;
        memset(&entity->animation_two, 0, sizeof(entity->animation_two));
    }

    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    if( new_angle == 1 || new_angle == 3 )
    {
        int tmp = size_x;
        size_x = size_z;
        size_z = tmp;
    }

    struct GameCacheMapLoc map_tile = { 0 };
    map_tile.loc_id = new_loc_id;
    map_tile.shape_select = new_shape;
    map_tile.orientation = new_angle;

    if( new_shape == LOC_SHAPE_WALL_TWO_SIDES )
    {
        int orientation = new_angle & 0x3;
        int rotation = orientation + 4;
        int next_rotation = ( rotation + 1 ) & 0x3;

        world_load_scenery_model(
            world,
            entity,
            &map_tile,
            &entity->scene_coord,
            &entity->scene_element,
            LOC_SHAPE_WALL_TWO_SIDES,
            rotation,
            1,
            1,
            config_loc);
        world_load_scenery_model(
            world,
            entity,
            &map_tile,
            &entity->scene_coord,
            &entity->scene_element_two,
            LOC_SHAPE_WALL_TWO_SIDES,
            next_rotation,
            1,
            1,
            config_loc);
        scenery_element_position_init(
            world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);
        scenery_element_position_init(
            world, entity, &entity->scene_coord, &entity->scene_element_two, 1, 1);
        world_map_build_loc_entity_set_animation(world, entity_id, config_loc->seq_id);
        if( world->painter )
            painter_sync_map_build_loc_elements(
                world->painter,
                entity->scene_element.element_id,
                entity->scene_element_two.element_id,
                new_shape,
                new_angle);
        return;
    }

    if( new_shape == LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE )
    {
        int outside_rotation = config_loc->seq_id != -1 ? 0 : map_tile.orientation;
        int outside_orientation = map_tile.orientation;
        int inside_rotation = config_loc->seq_id != -1 ? 0 : ( outside_orientation + 2 ) & 0x3;
        int inside_orientation = ( outside_orientation + 2 ) & 0x3;

        world_load_scenery_model(
            world,
            entity,
            &map_tile,
            &entity->scene_coord,
            &entity->scene_element,
            LOC_SHAPE_WALL_DECOR_INSIDE,
            outside_rotation,
            1,
            1,
            config_loc);
        world_load_scenery_model(
            world,
            entity,
            &map_tile,
            &entity->scene_coord,
            &entity->scene_element_two,
            LOC_SHAPE_WALL_DECOR_INSIDE,
            inside_rotation,
            1,
            1,
            config_loc);
        world_map_build_loc_entity_set_animation(world, entity_id, config_loc->seq_id);
        scenery_element_position_init(
            world, entity, &entity->scene_coord, &entity->scene_element, 1, 1);
        scenery_element_position_init(
            world, entity, &entity->scene_coord, &entity->scene_element_two, 1, 1);

        struct Scene2Element* scene_element =
            scene2_element_at(world->scene2, entity->scene_element.element_id);
        if( scene_element )
        {
            struct DashPosition* dp = scene2_element_dash_position(scene_element);
            if( dp )
            {
                dp->yaw += WALL_DECOR_YAW_ADJUST;
                if( config_loc->seq_id != -1 )
                    dp->yaw += 512 * outside_orientation;
                dp->yaw %= 2048;
            }
        }
        scene_element =
            scene2_element_at(world->scene2, entity->scene_element_two.element_id);
        if( scene_element )
        {
            struct DashPosition* dp = scene2_element_dash_position(scene_element);
            if( dp )
            {
                dp->yaw += WALL_DECOR_YAW_ADJUST;
                if( config_loc->seq_id != -1 )
                    dp->yaw += 512 * inside_orientation;
                dp->yaw %= 2048;
            }
        }
        if( world->painter )
            painter_sync_map_build_loc_elements(
                world->painter,
                entity->scene_element.element_id,
                entity->scene_element_two.element_id,
                new_shape,
                new_angle);
        return;
    }

    int load_rotation = new_angle;
    if( ( new_shape == LOC_SHAPE_SCENERY || new_shape == LOC_SHAPE_SCENERY_DIAGIONAL ) &&
         config_loc->seq_id != -1 )
        load_rotation = 0;

    /* scenery_add_normal always loads models under LOC_SHAPE_SCENERY and applies diagonal yaw (+256)
     * when map_tile.shape_select is LOC_SHAPE_SCENERY_DIAGIONAL; shape 11 has no separate row in
     * config_loc->shapes — using 11 here fails model lookup. */
    int model_shape = new_shape;
    if( new_shape == LOC_SHAPE_SCENERY || new_shape == LOC_SHAPE_SCENERY_DIAGIONAL )
        model_shape = LOC_SHAPE_SCENERY;

    world_load_scenery_model(
        world,
        entity,
        &map_tile,
        &entity->scene_coord,
        &entity->scene_element,
        model_shape,
        load_rotation,
        size_x,
        size_z,
        config_loc);

    if( new_shape == LOC_SHAPE_SCENERY || new_shape == LOC_SHAPE_SCENERY_DIAGIONAL )
        world_map_build_loc_entity_set_animation(world, entity_id, config_loc->seq_id);

    scenery_element_position_init(
        world,
        entity,
        &entity->scene_coord,
        &entity->scene_element,
        size_x,
        size_z);

    struct Scene2Element* se = scene2_element_at(world->scene2, entity->scene_element.element_id);
    if( se )
    {
        struct DashPosition* dp = scene2_element_dash_position(se);
        if( dp )
        {
            int yaw = 0;
            if( new_shape == 11 )
                yaw += 256;
            if( config_loc->seq_id != -1 )
                yaw += 512 * new_angle;
            else
                yaw = 512 * new_angle;
            dp->yaw = yaw % 2048;
        }
    }

    if( world->painter )
        painter_sync_map_build_loc_elements(
            world->painter,
            entity->scene_element.element_id,
            entity->scene_element_two.element_id,
            new_shape,
            new_angle);
}

void
world_cleanup_map_build_tile_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id == -1 )
        return;

    assert(entity_id >= 0 && entity_id < MAX_MAP_BUILD_TILE_ENTITIES);
    if( entity_id >= entity_vec_count(&world->map_build_tile_entities) )
        return;

    struct MapBuildTileEntity* entity =
        (struct MapBuildTileEntity*)entity_vec_at(&world->map_build_tile_entities, entity_id);
    if( entity->entity_id != entity_id )
        return;
    if( entity->scene_element.element_id == -1 )
        return;

    scene2_element_release(world->scene2, entity->scene_element.element_id);
    entity->scene_element.element_id = -1;
    memset(&entity->scene_coord, 0, sizeof(struct EntitySceneCoord));
}

void
world_cleanup_player_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->players) )
        return;

    struct PlayerEntity* player = world_player(world, entity_id);
    scene2_element_release(world->scene2, player->scene_element2.element_id);
    player->scene_element2.element_id = -1;
    memset(&player->pathing, 0, sizeof(struct EntityPathing));
    memset(&player->appearance, 0, sizeof(struct PlayerAppearanceSlots));
    memset(&player->draw_position, 0, sizeof(struct EntityDrawPosition));
    memset(&player->orientation, 0, sizeof(struct EntityOrientation));
    memset(&player->animation, 0, sizeof(struct EntityAnimation));
    player->alive = false;
}

void
world_cleanup_npc_entity(
    struct World* world,
    int entity_id)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->npcs) )
        return;

    struct NPCEntity* npc = world_npc(world, entity_id);
    free(npc->name);
    npc->name = NULL;
    free(npc->description);
    npc->description = NULL;
    free(npc->actions);
    npc->actions = NULL;
    npc->action_count = 0;
    scene2_element_release(world->scene2, npc->scene_element2.element_id);
    npc->scene_element2.element_id = -1;
    memset(&npc->pathing, 0, sizeof(struct EntityPathing));
    memset(&npc->draw_position, 0, sizeof(struct EntityDrawPosition));
    memset(&npc->orientation, 0, sizeof(struct EntityOrientation));
    memset(&npc->animation, 0, sizeof(struct EntityAnimation));
    npc->config_npc_id = -1;
    npc->minimap_hidden = false;
    npc->alive = false;
}

void
world_player_entity_set_appearance(
    struct World* world,
    int player_entity_id,
    struct PlayerAppearance* appearance)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    struct Scene2Element* element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    scene2_element_expect(element, "world_player_entity_set_appearance");

    world_scenebuild_player_entity_set_appearance(
        world, player_entity_id, appearance->appearance, appearance->color);

    for( int i = 0; i < 12; i++ )
        player->appearance.slots[i] = appearance->appearance[i];
    for( int i = 0; i < 5; i++ )
        player->appearance.colors[i] = appearance->color[i];

    struct PassiveAnimationInfo passive_animations = {
        .readyanim = appearance->readyanim,
        .walkanim = appearance->walkanim,
        .turnanim = appearance->turnanim,
        .runanim = appearance->runanim,
        .walkanim_b = appearance->walkanim_b,
        .walkanim_r = appearance->walkanim_r,
        .walkanim_l = appearance->walkanim_l,
    };

    world_player_entity_set_passive_animations(world, player_entity_id, &passive_animations);

    strncpy(player->name.name, appearance->name, sizeof(player->name.name));
    player->visible_level.level = appearance->combat_level;
}

static void
world_player_entity_refresh_scene2_appearance_mesh(struct World* world, int player_entity_id)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    if( !player->alive || player->scene_element2.element_id == -1 )
        return;
    uint16_t app12[12];
    uint16_t col5[5];
    for( int i = 0; i < 12; i++ )
        app12[i] = (uint16_t)player->appearance.slots[i];
    for( int i = 0; i < 5; i++ )
        col5[i] = (uint16_t)player->appearance.colors[i];
    world_scenebuild_player_entity_set_appearance(world, player_entity_id, app12, col5);
}

static void
load_scene_animation(
    struct World* world,
    struct Scene2Element* element,
    int animation_id,
    int animation_type)
{
    if( animation_id == -1 )
        return;

    struct GameCacheAnimframe* animframe = NULL;
    struct GameCacheSequence* sequence =
        gamecache_get_sequence(world->gamecache, animation_id);
    if( !sequence )
        return;

    for( int i = 0; i < sequence->frame_count; i++ )
    {
        // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
        //     first 2 bytes are the sequence ID,
        //     the second 2 bytes are the frame file ID
        int frame_id = sequence->frames[i];

        animframe = gamecache_get_animframe(world->gamecache, frame_id);
        if( !animframe )
            return;

        if( !scene2_element_dash_framemap(element) )
        {
            scene2_element_set_framemap(element, dashframemap_new_from_gamecache_animframe(animframe));
        }

        // From Client-TS 245.2
        int length = sequence->delay[i];
        if( length == 0 )
            length = animframe->delay;

        struct DashFrame* dash_frame = dashframe_new_from_gamecache_animframe(animframe);
        if( animation_type == ANIMATION_TYPE_PRIMARY )
        {
            scene2_element_push_animation_frame(
                world->scene2, element, animation_id, i, dash_frame, length);
        }
        else
        {
            scene2_element_push_secondary_animation_frame(
                world->scene2, element, animation_id, i, dash_frame, length);
        }
    }
}

/** Client.ts `RestartMode` for `SeqType.duplicatebehaviour`. */
#define WORLD_SEQ_RESTART_RESET      1
#define WORLD_SEQ_RESTART_RESETLOOP  2

static bool
world_player_primary_track_active(uint16_t anim_id)
{
    return anim_id != 0 && anim_id != (uint16_t)-1;
}

static bool
world_player_seq_held_equal(
    struct GameCacheSequence const* a,
    struct GameCacheSequence const* b)
{
    if( !a && !b )
        return true;
    if( !a || !b )
        return false;
    return a->replaceheldleft == b->replaceheldleft && a->replaceheldright == b->replaceheldright;
}

static void
world_player_sync_primary_pose_from_entity(struct World* world, int player_entity_id)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    if( !scene_element )
        return;
    struct EntityAnimation* animation = &player->animation;
    struct Scene2Frames* primary = scene2_element_primary_frames(scene_element);

    if( world_player_primary_track_active(animation->primary_anim.anim_id) && primary &&
        primary->count > 0 )
    {
        int frame = animation->primary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->primary_anim.anim_id);
        scene2_element_set_active_animation_index(scene_element, 0);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
    }
}

void
world_map_build_loc_entity_set_animation(
    struct World* world,
    int map_build_loc_entity_id,
    int animation_id)
{
    if( animation_id == -1 )
        return;
    if( map_build_loc_entity_id < 0 ||
        map_build_loc_entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;

    struct Scene2Element* element = NULL;
    struct MapBuildLocEntity* map_build_loc_entity =
        world_loc_entity(world, map_build_loc_entity_id);

    if( map_build_loc_entity->scene_element.element_id != -1 )
    {
        element = scene2_element_at(world->scene2, map_build_loc_entity->scene_element.element_id);
        scene2_element_expect(element, "world_map_build_loc_entity_set_animation primary");
        load_scene_animation(world, element, animation_id, ANIMATION_TYPE_PRIMARY);

        memset(&map_build_loc_entity->animation, 0, sizeof(struct EntityAnimation));
        map_build_loc_entity->animation.primary_anim.anim_id = animation_id;
    }

    if( map_build_loc_entity->scene_element_two.element_id != -1 )
    {
        element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element_two.element_id);
        scene2_element_expect(element, "world_map_build_loc_entity_set_animation secondary");

        load_scene_animation(world, element, animation_id, ANIMATION_TYPE_PRIMARY);

        memset(&map_build_loc_entity->animation_two, 0, sizeof(struct EntityAnimation));
        map_build_loc_entity->animation_two.primary_anim.anim_id = animation_id;
    }
}

void
world_player_entity_set_animation(
    struct World* world,
    int player_entity_id,
    int animation_id,
    int animation_type,
    uint8_t primary_start_delay)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    if( player->scene_element2.element_id == -1 )
        return;

    struct Scene2Element* element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    scene2_element_expect(element, "world_player_entity_set_animation");

    if( animation_type != ANIMATION_TYPE_PRIMARY )
    {
        scene2_element_clear_secondary_animation(element);
        load_scene_animation(world, element, animation_id, animation_type);
        memset(&player->animation.secondary_anim, 0, sizeof(struct EntityAnimationStep));
        player->animation.secondary_anim.anim_id =
            animation_id < 0 ? (uint16_t)-1 : (uint16_t)animation_id;
        return;
    }

    /* Primary: mirror Client.ts getPlayerExtendedDecode ANIM (priority, duplicate behaviour). */
    if( animation_id < 0 )
    {
        scene2_element_clear_animation(element);
        memset(&player->animation.primary_anim, 0, sizeof(struct EntityAnimationStep));
        player->animation.primary_anim.anim_id = (uint16_t)-1;
        world_player_entity_refresh_scene2_appearance_mesh(world, player_entity_id);
        return;
    }

    struct GameCacheSequence* new_seq = gamecache_get_sequence(world->gamecache, animation_id);
    if( !new_seq )
        return;

    uint16_t cur_id = player->animation.primary_anim.anim_id;
    bool cur_on = world_player_primary_track_active(cur_id);

    struct GameCacheSequence* old_seq = NULL;
    if( cur_on )
        old_seq = gamecache_get_sequence(world->gamecache, (int)cur_id);

    if( old_seq && new_seq->priority < old_seq->priority )
        return;

    /* Same sequence id already playing: do not clear/reload frames (avoids mesh/anim jitter). */
    if( cur_on && (int)cur_id == animation_id )
    {
        player->animation.primary_anim.loop = 0;

        int mode = new_seq->duplicate_behavior;
        if( mode == WORLD_SEQ_RESTART_RESET )
        {
            uint8_t prev_delay = player->animation.primary_anim.delay;
            player->animation.primary_anim.frame = 0;
            player->animation.primary_anim.cycle = 0;
            player->animation.primary_anim.delay = primary_start_delay;
            player->animation.primary_anim.anim_id = (uint16_t)animation_id;
            world_player_sync_primary_pose_from_entity(world, player_entity_id);
            if( prev_delay != primary_start_delay )
                world_player_entity_refresh_scene2_appearance_mesh(world, player_entity_id);
        }
        /* RESETLOOP and other modes: Client only clears primaryAnimLoop (done above). */
        return;
    }

    scene2_element_clear_animation(element);

    /* Update entity state before any mesh refresh so player_appearance_model
       sees the new sequence's held overrides (replaceheldleft/right). */
    memset(&player->animation.primary_anim, 0, sizeof(struct EntityAnimationStep));
    player->animation.primary_anim.anim_id = (uint16_t)animation_id;
    player->animation.primary_anim.delay = primary_start_delay;

    /* Refresh mesh before pushing frames so GPU uploads target the final DashModel. */
    if( !world_player_seq_held_equal(old_seq, new_seq) )
        world_player_entity_refresh_scene2_appearance_mesh(world, player_entity_id);

    load_scene_animation(world, element, animation_id, ANIMATION_TYPE_PRIMARY);

    /* Immediately sync scene2 active anim/frame so element_step_animation never
       sees primary_frames populated but active_anim_id still at the old value. */
    world_player_sync_primary_pose_from_entity(world, player_entity_id);
}

void
world_npc_entity_set_animation(
    struct World* world,
    int npc_entity_id,
    int animation_id,
    int animation_type)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);

    if( npc->scene_element2.element_id == -1 )
        return;

    struct Scene2Element* element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    scene2_element_expect(element, "world_npc_entity_set_animation");

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        scene2_element_clear_animation(element);
    }
    else
    {
        scene2_element_clear_secondary_animation(element);
    }

    load_scene_animation(world, element, animation_id, animation_type);

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        memset(&npc->animation.primary_anim, 0, sizeof(struct EntityAnimationStep));
        npc->animation.primary_anim.anim_id = animation_id;
    }
    else
    {
        memset(&npc->animation.secondary_anim, 0, sizeof(struct EntityAnimationStep));
        npc->animation.secondary_anim.anim_id = animation_id;
    }
}

void
world_projectile_entity_set_animation(
    struct World* world,
    int projectile_entity_id,
    int animation_id,
    int animation_type)
{
    struct ProjectileEntity* p = world_projectile(world, projectile_entity_id);

    if( p->scene_element2.element_id == -1 )
        return;

    struct Scene2Element* element = scene2_element_at(world->scene2, p->scene_element2.element_id);
    scene2_element_expect(element, "world_projectile_entity_set_animation");

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        scene2_element_clear_animation(element);
    }
    else
    {
        scene2_element_clear_secondary_animation(element);
    }

    load_scene_animation(world, element, animation_id, animation_type);

    if( animation_type == ANIMATION_TYPE_PRIMARY )
    {
        memset(&p->animation.primary_anim, 0, sizeof(struct EntityAnimationStep));
        p->animation.primary_anim.anim_id = animation_id;
    }
    else
    {
        memset(&p->animation.secondary_anim, 0, sizeof(struct EntityAnimationStep));
        p->animation.secondary_anim.anim_id = animation_id;
    }
}

void
world_player_entity_set_passive_animations(
    struct World* world,
    int player_entity_id,
    struct PassiveAnimationInfo* passive_animation_info)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    player->animation.readyanim = passive_animation_info->readyanim;
    player->animation.walkanim = passive_animation_info->walkanim;
    player->animation.turnanim = passive_animation_info->turnanim;
    player->animation.runanim = passive_animation_info->runanim;
    player->animation.walkanim_b = passive_animation_info->walkanim_b;
    player->animation.walkanim_r = passive_animation_info->walkanim_r;
    player->animation.walkanim_l = passive_animation_info->walkanim_l;
}

void
world_npc_entity_set_passive_animations(
    struct World* world,
    int npc_entity_id,
    struct PassiveAnimationInfo* passive_animation_info)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    npc->animation.readyanim = passive_animation_info->readyanim;
    npc->animation.walkanim = passive_animation_info->walkanim;
    npc->animation.turnanim = passive_animation_info->turnanim;
    npc->animation.runanim = passive_animation_info->runanim;
    npc->animation.walkanim_b = passive_animation_info->walkanim_b;
    npc->animation.walkanim_r = passive_animation_info->walkanim_r;
    npc->animation.walkanim_l = passive_animation_info->walkanim_l;
}

void
world_npc_entity_path_push_moveto(
    struct World* world,
    int npc_entity_id,
    int x,
    int z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
}

void
world_player_entity_path_push_step(
    struct World* world,
    int player_entity_id,
    int step_type,
    int direction)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    entity_pathing_push_step(&player->pathing, step_type, direction);
}

void
world_npc_entity_path_push_step(
    struct World* world,
    int npc_entity_id,
    int step_type,
    int direction)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);

    entity_pathing_push_step(&npc->pathing, step_type, direction);
}

void
world_player_entity_path_jump_relative_to_active(
    struct World* world,
    int player_entity_id,
    bool force_teleport,
    int dx,
    int dz)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    struct PlayerEntity* active_player = world_player(world, ACTIVE_PLAYER_SLOT);

    int x = active_player->pathing.route_x[0] + dx;
    int z = active_player->pathing.route_z[0] + dz;

    enum PathingJump jump = entity_pathing_jump(&player->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&player->draw_position, x, z, 1, 1);
}

void
world_npc_entity_path_jump_relative_to_active(
    struct World* world,
    int npc_entity_id,
    bool force_teleport,
    int dx,
    int dz)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    struct PlayerEntity* active_player = world_player(world, ACTIVE_PLAYER_SLOT);

    int x = active_player->pathing.route_x[0] + dx;
    int z = active_player->pathing.route_z[0] + dz;

    enum PathingJump jump = entity_pathing_jump(&npc->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&npc->draw_position, x, z, 1, 1);
}

void
world_player_entity_path_jump(
    struct World* world,
    int player_entity_id,
    bool force_teleport,
    int x,
    int z)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);

    enum PathingJump jump = entity_pathing_jump(&player->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&player->draw_position, x, z, 1, 1);
}

void
world_npc_entity_path_jump(
    struct World* world,
    int npc_entity_id,
    bool force_teleport,
    int x,
    int z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);

    enum PathingJump jump = entity_pathing_jump(&npc->pathing, force_teleport, x, z);
    if( jump == PATHING_JUMP_TELEPORT )
        entity_draw_position_set_to_tile(&npc->draw_position, x, z, npc->size.x, npc->size.z);
}

void
world_player_face_entity(
    struct World* world,
    int player_entity_id,
    int entity_id)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
}

void
world_npc_face_entity(
    struct World* world,
    int npc_entity_id,
    int entity_id)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
}

void
world_player_face_coord(
    struct World* world,
    int player_entity_id,
    int tile_x,
    int tile_z)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
}

void
world_npc_face_coord(
    struct World* world,
    int npc_entity_id,
    int tile_x,
    int tile_z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
}

void
world_npc_set_size(
    struct World* world,
    int npc_entity_id,
    int size_x,
    int size_z)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    npc->size.x = size_x;
    npc->size.z = size_z;
}

struct NPCEntity*
world_npc_ensure_scene_element(
    struct World* world,
    int npc_id)
{
    struct Scene2Element* element = NULL;
    struct NPCEntity* npc = world_npc_ensure(world, npc_id);

    npc->alive = true;
    if( npc->scene_element2.element_id == -1 )
    {
        npc->scene_element2.element_id = scene2_element_acquire_full(
            world->scene2,
            (int)entity_unified_id(ENTITY_KIND_NPC, npc_id),
            SCENE2_ELEMENT_NPC,
            0,
            0);

        npc->orientation.dst_yaw = 0;

        element = scene2_element_at(world->scene2, npc->scene_element2.element_id);
        scene2_element_expect(element, "world_npc_ensure_scene_element");
        if( !scene2_element_dash_position(element) )
            scene2_element_set_dash_position_ptr(element, dashposition_new());
    }
    return npc;
}

struct PlayerEntity*
world_player_ensure_scene_element(
    struct World* world,
    int player_id)
{
    struct Scene2Element* element = NULL;
    struct PlayerEntity* player = world_player_ensure(world, player_id);

    player->alive = true;
    if( player->scene_element2.element_id == -1 )
    {
        player->scene_element2.element_id = scene2_element_acquire_full(
            world->scene2,
            (int)entity_unified_id(ENTITY_KIND_PLAYER, player_id),
            SCENE2_ELEMENT_PLAYER,
            0,
            0);

        // player->orientation.face_entity = -1;
        player->orientation.dst_yaw = 0;

        element = scene2_element_at(world->scene2, player->scene_element2.element_id);
        scene2_element_expect(element, "world_player_ensure_scene_element");
        if( !scene2_element_dash_position(element) )
            scene2_element_set_dash_position_ptr(element, dashposition_new());
    }

    return player;
}

void
world_map_build_loc_entity_push_action(
    struct World* world,
    int map_build_loc_entity_id,
    int code,
    char* action)
{
    if( map_build_loc_entity_id < 0 ||
        map_build_loc_entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;
    struct MapBuildLocEntity* entity = world_loc_entity(world, map_build_loc_entity_id);
    assert(entity->action_count < 10);
    if( !entity->actions )
        entity->actions = (struct EntityAction*)calloc(10, sizeof(struct EntityAction));
    entity->actions[entity->action_count].code = code;
    strncpy(
        entity->actions[entity->action_count].name,
        action,
        sizeof(entity->actions[entity->action_count].name));
    entity->action_count++;
}