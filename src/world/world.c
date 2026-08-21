#include "world.h"

#include "features/features.h"

#include "entity_pathing.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_PROJECTILE_ANGLE_TO_RAD 0.02454369
#define WORLD_PROJECTILE_ANGLE_TO_RPI2048 325.949

struct World*
World_New(void)
{
    struct World* world = calloc(1, sizeof(struct World));
    assert(world && "Failed to allocate world");
    world->local_pid = -1;
    World_EntityListInit(&world->entities);
    return world;
}

void
World_SetScene(
    struct World* world,
    struct ToriDraw_Scene* scene)
{
    assert(world);
    world->scene = scene;
}

void
World_Free(struct World* world)
{
    if( !world )
        return;
    if( world->heightmap )
        heightmap_free(world->heightmap);
    if( world->minimap )
        minimap_free(world->minimap);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
    {
        if( world->collision_maps[i] )
            collision_map_free(world->collision_maps[i]);
    }
    if( world->cullmap )
        painters_cullmap_free(world->cullmap);
    if( world->painter )
        painter_free(world->painter);
    free(world->tile_flags);
    free(world->obj_raise);
    free(world->tile_last_occupied_cycle);
    free(world->mapscenes);
    free(world->area_sounds);
    World_EntityListFree(&world->entities);
    free(world);
}

void
World_AddMapSceneIcon(
    struct World* world,
    int x,
    int z,
    int level,
    int mapscene,
    int width,
    int length)
{
    struct World_MapSceneIcon* icon;

    if( world->mapscene_count >= world->mapscene_capacity )
    {
        int new_cap = world->mapscene_capacity ? world->mapscene_capacity * 2 : 256;
        struct World_MapSceneIcon* grown = (struct World_MapSceneIcon*)realloc(
            world->mapscenes, (size_t)new_cap * sizeof(*grown));
        if( !grown )
            return;
        world->mapscenes = grown;
        world->mapscene_capacity = new_cap;
    }

    icon = &world->mapscenes[world->mapscene_count++];
    icon->x = x;
    icon->z = z;
    icon->level = level;
    icon->mapscene = mapscene;
    icon->width = width;
    icon->length = length;
}

void
World_AddAreaSound(
    struct World* world,
    const struct World_AreaSound* source)
{
    assert(world);
    assert(source);
    if( world->area_sound_count >= world->area_sound_capacity )
    {
        int new_cap = world->area_sound_capacity ? world->area_sound_capacity * 2 : 64;
        struct World_AreaSound* grown = (struct World_AreaSound*)realloc(
            world->area_sounds, (size_t)new_cap * sizeof(*grown));
        if( !grown )
            return;
        world->area_sounds = grown;
        world->area_sound_capacity = new_cap;
    }

    world->area_sounds[world->area_sound_count++] = *source;
    /* A new emitter is a change the audio layer has to notice, the same as a
     * removal: without the bump it stays bound to the generation it last
     * synchronised with and never acquires this one. */
    world->area_sound_generation++;
}

int
World_RemoveAreaSoundAt(
    struct World* world,
    int x,
    int z,
    int level)
{
    int removed = 0;

    assert(world);
    for( int i = 0; i < world->area_sound_count; )
    {
        struct World_AreaSound* entry = &world->area_sounds[i];
        if( entry->x != x || entry->z != z || entry->level != level )
        {
            i++;
            continue;
        }
        /* Order is not meaningful here -- voices are bound by (loc_id, level,
         * x, z), not by index -- so a swap-with-last beats a memmove. */
        *entry = world->area_sounds[--world->area_sound_count];
        removed++;
    }
    if( removed > 0 )
        world->area_sound_generation++;
    return removed;
}

int
World_TileFlagGet(
    struct World const* world,
    int x,
    int z,
    int level)
{
    if( !world || !world->tile_flags || x < 0 || z < 0 || level < 0 ||
        x >= world->_scene_size || z >= world->_scene_size || level >= WORLD_MAP_TERRAIN_LEVELS )
        return 0;
    return world->tile_flags[x + z * world->_scene_size + level * world->_scene_size * world->_scene_size];
}

/* Same values RSCACHE_FLOFLAG_LINK_BELOW / _VIS_BELOW carry, redeclared so this
 * stays leaf — minimap.h does the same for the two flags its bake reads. */
#define WORLD_TILE_FLAG_LINK_BELOW 0x02
#define WORLD_TILE_FLAG_VIS_BELOW 0x08

/* LINK_BELOW is a property of the whole column and is read at cache level 1,
 * which is why both helpers below ask level 1 whatever level they were given. */
static int
world_column_link_below(
    struct World const* world,
    int x,
    int z)
{
    return (World_TileFlagGet(world, x, z, 1) & WORLD_TILE_FLAG_LINK_BELOW) != 0;
}

int
World_LocPaintLevel(
    struct World const* world,
    int x,
    int z,
    int cache_level)
{
    if( cache_level < 0 || cache_level >= WORLD_MAP_TERRAIN_LEVELS )
        return cache_level;
    if( !world_column_link_below(world, x, z) )
        return cache_level;
    /* The same shuffle painter_tile_copyto performs: 1->0, 2->1, 3->2, 0->3. */
    return cache_level == 0 ? WORLD_MAP_TERRAIN_LEVELS - 1 : cache_level - 1;
}

/*
 * The level a terrain mesh DRAWS at, which is not the level it was authored on.
 *
 * Two flags move a floor off its own plane and this is where they are answered
 * together (reference class112.method4161 / Client-TS getVisBelowLevel):
 * VIS_BELOW drops a tile to level 0 outright, and on a LinkBelow column every
 * plane above 0 is pushed down one by the bridge shuffle. Both leave the mesh
 * on its own plane and change only the level it is culled and picked against —
 * which is why callers that want "where is this geometry" still read the mesh
 * level, and callers asking "can the player standing on level N see or click
 * this" read this.
 *
 * Shared rather than open-coded because the pick guard and the debug readout
 * have to agree with the value the world builder baked into the painter tile
 * (painter_tile_set_draw_level, RSCache_MapFloorVisBelowDrawLevel) — three
 * copies of one rule is how they drift.
 */
int
World_TerrainDrawLevel(
    struct World const* world,
    int x,
    int z,
    int mesh_level)
{
    if( mesh_level < 0 || mesh_level >= WORLD_MAP_TERRAIN_LEVELS )
        return mesh_level;
    if( (World_TileFlagGet(world, x, z, mesh_level) & WORLD_TILE_FLAG_VIS_BELOW) != 0 )
        return 0;
    if( mesh_level > 0 && world_column_link_below(world, x, z) )
        return mesh_level - 1;
    return mesh_level;
}

/*
 * Mesh level -> the level that mesh is walked from.
 *
 * A terrain pick hands back the plane the floor was AUTHORED on, and on a
 * LinkBelow column that is one above the plane the player standing on it walks:
 * the deck is cache level 1, the player is level 0. Anything that takes that
 * hit and speaks the wire — an entity position, a height sample, the tile a
 * plugin draws a marker on — has to come back down first, or it lands a storey
 * out. Feeding a raw mesh level to app_world_height is the loud case: that
 * function adds the bridge's +1 itself, so the double shift samples level 2 and
 * the marker floats 240 units over the deck.
 *
 * VIS_BELOW is not part of this and must not be: it lowers the level a mesh is
 * culled and picked against (World_TerrainDrawLevel) and leaves the geometry —
 * and the player — on their own plane.
 */
int
World_TerrainWalkLevel(
    struct World const* world,
    int x,
    int z,
    int mesh_level)
{
    if( mesh_level <= 0 || mesh_level >= WORLD_MAP_TERRAIN_LEVELS )
        return mesh_level;
    if( !world_column_link_below(world, x, z) )
        return mesh_level;
    return mesh_level - 1;
}

/*
 * Debug readouts of a column, shared so the minimenu row and the loc editor
 * panel cannot describe the same tile differently.
 *
 * Settings: one group per cache level, `[L0|L1|L2|L3]`, letters in bit order —
 * B block, L link-below, R remove-roof, V vis-below, H force-high-detail, `-`
 * for a level whose byte is zero. Spelled rather than hex because the two that
 * move a floor off its own plane are L and V, and "0x0a" does not say that at a
 * glance. The whole column, not one level: LINK_BELOW is read at cache level 1
 * and speaks for every plane, so a readout of the hovered level alone cannot
 * explain the level the tile draws at.
 */
void
World_TileSettingsText(
    struct World* world,
    int x,
    int z,
    char* out,
    int cap)
{
    static char const letters[5] = { 'B', 'L', 'R', 'V', 'H' };
    int used = 0;

    assert(out);
    assert(cap > 0);
    out[0] = '\0';
    for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS && used < cap - 1; level++ )
    {
        unsigned flags = (unsigned)World_TileFlagGet(world, x, z, level);
        int any = 0;

        if( level > 0 && used < cap - 1 )
            out[used++] = '|';
        for( int bit = 0; bit < 5 && used < cap - 1; bit++ )
            if( flags & (1u << bit) )
            {
                out[used++] = letters[bit];
                any = 1;
            }
        if( !any && used < cap - 1 )
            out[used++] = '-';
    }
    out[used] = '\0';
}

/*
 * Which cache levels of a column actually carry a terrain mesh, as digits, or
 * `-` for none.
 *
 * "There is no floor here" and "the floor is on a plane you did not expect" are
 * the two states an unclickable or blank patch of ground is in, and they look
 * identical in the viewport. A Theatre of Blood corridor answers `1` while the
 * player stands on level 0; a genuinely floorless tile answers `-`.
 */
void
World_TerrainMeshLevelsText(
    struct World* world,
    int x,
    int z,
    char* out,
    int cap)
{
    int used = 0;

    assert(out);
    assert(cap > 0);
    out[0] = '\0';
    for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS && used < cap - 1; level++ )
        if( World_TerrainElementAt(world, x, z, level) >= 0 )
            out[used++] = (char)('0' + level);
    if( used == 0 && used < cap - 1 )
        out[used++] = '-';
    out[used] = '\0';
}

int
World_LocCacheLevel(
    struct World const* world,
    int x,
    int z,
    int wire_level)
{
    if( wire_level < 0 || wire_level >= WORLD_MAP_TERRAIN_LEVELS - 1 )
        return wire_level;
    if( !world_column_link_below(world, x, z) )
        return wire_level;
    return wire_level + 1;
}

static int
world_obj_raise_idx(
    struct World const* world,
    int x,
    int z,
    int level)
{
    return x + z * world->_scene_size + level * world->_scene_size * world->_scene_size;
}

int
World_ObjRaiseGet(
    struct World const* world,
    int x,
    int z,
    int level)
{
    if( !world || !world->obj_raise || x < 0 || z < 0 || level < 0 ||
        x >= world->_scene_size || z >= world->_scene_size || level >= WORLD_MAP_TERRAIN_LEVELS )
        return 0;
    return world->obj_raise[world_obj_raise_idx(world, x, z, level)];
}

void
World_ObjRaiseSetMax(
    struct World* world,
    int x,
    int z,
    int level,
    int raise)
{
    int idx;
    int16_t cur;

    assert(world);
    assert(world->obj_raise);
    /* Footprints of edge locs can spill past the scene — same skip as Get. */
    if( x < 0 || z < 0 || level < 0 || raise <= 0 ||
        x >= world->_scene_size || z >= world->_scene_size ||
        level >= WORLD_MAP_TERRAIN_LEVELS )
        return;
    idx = world_obj_raise_idx(world, x, z, level);
    cur = world->obj_raise[idx];
    if( raise > cur )
        world->obj_raise[idx] = (int16_t)raise;
}

void
World_SetHeightFn(
    struct World* world,
    World_HeightFn fn,
    void* userdata)
{
    assert(world);
    world->height_fn = fn;
    world->height_userdata = userdata;
}

void
World_SetLoadComplete(
    struct World* world,
    bool complete)
{
    assert(world);
    world->load_complete = complete;
    if( complete )
        world->load_seq++;
}

static void
World_ResetSceneAlloc(
    struct World* world,
    int scene_size)
{
    if( world->heightmap )
        heightmap_free(world->heightmap);
    if( world->minimap )
        minimap_free(world->minimap);
    if( world->cullmap )
    {
        painters_cullmap_free(world->cullmap);
        world->cullmap = NULL;
    }
    if( world->painter )
    {
        painter_free(world->painter);
        world->painter = NULL;
    }
    for( int i = 0; i < COLLISION_LEVELS; i++ )
    {
        if( world->collision_maps[i] )
            collision_map_free(world->collision_maps[i]);
        world->collision_maps[i] = NULL;
    }

    World_TerrainReset(world);
    /* Scenery records mirror the builder's static scene elements, which the
     * rebuild frees and recreates — stale records would alias the new
     * elements' reused ids. Movers/objstacks stay: the rebuild shift
     * relocates them (Client-TS keeps entity slots across a rebuild). */
    World_EntityPoolReset(&world->entities.scenery);
    world->scenery_pick_count = 0;
    /* Pending EntityRemoved must be drained (SceneElementRemove) before a
     * scene reset — wiping the queue here would orphan DYNAMIC elements. */
    assert(world->event_count == 0 && "drain EntityRemoved before World_ResetSceneAlloc");
    world->mapfunc_count = 0;
    world->mapscene_count = 0;
    world->area_sound_count = 0;
    world->area_sound_generation++;
    /* Loc-change records survive the scene reset and are shifted by
     * World_ShiftEntities (Client-TS locChanges / deob field1353). The server
     * re-sends zone state for anything that must reappear visually. */
    world->_scene_size = scene_size;

    world->heightmap = heightmap_new(scene_size + 1, scene_size + 1, WORLD_MAP_TERRAIN_LEVELS);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
        world->collision_maps[i] = collision_map_new(scene_size, scene_size);
    world->minimap = minimap_new(scene_size, scene_size);

    free(world->tile_flags);
    world->tile_flags =
        (uint8_t*)calloc((size_t)(scene_size * scene_size * WORLD_MAP_TERRAIN_LEVELS), 1);

    free(world->obj_raise);
    world->obj_raise =
        (int16_t*)calloc((size_t)(scene_size * scene_size * WORLD_MAP_TERRAIN_LEVELS), sizeof(int16_t));

    /* Per-tile occupancy stamp (reference tileLastOccupiedCycle). calloc's 0
     * can never equal scene_cycle once it starts at 1, so a fresh scene reads
     * as unoccupied without an explicit clear. */
    free(world->tile_last_occupied_cycle);
    world->tile_last_occupied_cycle =
        (int*)calloc((size_t)(scene_size * scene_size), sizeof(int));
    world->scene_cycle = 0;

    world->painter = painter_new(
        scene_size,
        scene_size,
        WORLD_MAP_TERRAIN_LEVELS,
        PAINTER_NEW_CTX_BUCKET | PAINTER_NEW_CTX_WORLD3D);
    /* Default nocull; the client installs a per-frame analytic span cull once
     * the world viewport is known (app_update_painter_cull). TORIRS_PAINTER_NOCULL=1
     * keeps this stub; TORIRS_PAINTER_CULL=baked restores the CPU-baked table. */
    world->cullmap = painters_cullmap_new_nocull();
    if( world->painter )
        painter_set_cullmap(world->painter, world->cullmap);

    int terrain_tile_count = scene_size * scene_size * WORLD_MAP_TERRAIN_LEVELS;
    World_EntityPoolReserve(&world->entities.terrain, terrain_tile_count);
}

void
World_ResetScene(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
    assert(world);

    int zone_padding = scene_size / (2 * 8);
    int zone_sw_x = zone_center_x - zone_padding;
    int zone_sw_z = zone_center_z - zone_padding;
    int zone_ne_x = zone_center_x + zone_padding;
    int zone_ne_z = zone_center_z + zone_padding;
    int world_sw_x = zone_sw_x * 8;
    int world_sw_z = zone_sw_z * 8;

    world->load_complete = false;

    world->_offset_x = world_sw_x % 64;
    world->_offset_z = world_sw_z % 64;
    world->_base_tile_x = zone_sw_x * 8;
    world->_base_tile_z = zone_sw_z * 8;
    world->_chunk_sw_x = zone_sw_x / 8;
    world->_chunk_sw_z = zone_sw_z / 8;
    world->_chunk_ne_x = zone_ne_x / 8;
    world->_chunk_ne_z = zone_ne_z / 8;

    World_ResetSceneAlloc(world, scene_size);
}

void
World_ResetSceneChunkList(
    struct World* world,
    const int* chunks_xz,
    int count)
{
    assert(world);
    assert(chunks_xz);
    assert(count > 0);

    int min_x = chunks_xz[0];
    int min_z = chunks_xz[1];
    int max_x = min_x;
    int max_z = min_z;

    for( int i = 1; i < count; i++ )
    {
        int mapx = chunks_xz[i * 2];
        int mapz = chunks_xz[i * 2 + 1];
        if( mapx < min_x )
            min_x = mapx;
        if( mapx > max_x )
            max_x = mapx;
        if( mapz < min_z )
            min_z = mapz;
        if( mapz > max_z )
            max_z = mapz;
    }

    int span_x = max_x - min_x + 1;
    int span_z = max_z - min_z + 1;
    int span = span_x > span_z ? span_x : span_z;
    int scene_size = span * WORLD_MAP_TERRAIN_X;

    world->load_complete = false;

    world->_offset_x = 0;
    world->_offset_z = 0;
    world->_base_tile_x = min_x * WORLD_MAP_TERRAIN_X;
    world->_base_tile_z = min_z * WORLD_MAP_TERRAIN_Z;
    world->_chunk_sw_x = min_x;
    world->_chunk_sw_z = min_z;
    world->_chunk_ne_x = max_x;
    world->_chunk_ne_z = max_z;

    World_ResetSceneAlloc(world, scene_size);
}

void
World_TerrainReset(struct World* world)
{
    assert(world);
    World_EntityPoolReset(&world->entities.terrain);
}

void
World_TerrainSet(
    struct World* world,
    int element_id,
    int x,
    int z,
    int level)
{
    assert(world);

    int idx = World_TerrainTileIdx(world, x, z, level);
    struct World_EntityPool* pool = &world->entities.terrain;
    if( !World_EntityPoolEnsureSlot(pool, idx) )
        return;

    struct WorldEntity_Terrain* terrain = World_EntityPoolGet(pool, idx);
    assert(terrain);

    terrain->element_id = element_id;
    terrain->grid_position.level = level;
    terrain->grid_position.x = x;
    terrain->grid_position.z = z;
}

int
World_TerrainElementAt(
    struct World* world,
    int x,
    int z,
    int level)
{
    assert(world);
    if( x < 0 || z < 0 || level < 0 )
        return -1;
    if( x >= world->_scene_size || z >= world->_scene_size || level >= WORLD_MAP_TERRAIN_LEVELS )
        return -1;

    int idx = World_TerrainTileIdx(world, x, z, level);
    struct World_EntityPool* pool = &world->entities.terrain;
    if( idx < 0 || idx >= pool->count || !World_EntityPoolIsActive(pool, idx) )
        return -1;

    struct WorldEntity_Terrain* terrain = World_EntityPoolGet(pool, idx);
    if( !terrain )
        return -1;
    return terrain->element_id;
}

void
World_EmitEvent(
    struct World* world,
    enum WorldEventKind kind,
    int element_id)
{
    if( element_id < 0 )
        return;
    assert(world->event_count < WORLD_MAX_EVENTS && "world event queue full — raise WORLD_MAX_EVENTS");
    world->events[world->event_count++] = (struct World_Event){
        .kind = kind,
        .element_id = element_id,
    };
}

static void
World_EmitEntityRemoved(
    struct World* world,
    int element_id)
{
    World_EmitEvent(world, WorldEventKind_EntityRemoved, element_id);
}

static int
World_ProjectileDstY(
    struct World* world,
    const struct WorldEntity_Projectile* p)
{
    if( world->height_fn )
        return world->height_fn(world->height_userdata, p->dst_x, p->dst_z, p->dst_level) -
               p->end_height;
    return p->h1 - p->end_height;
}

void
World_ProjectileSetTarget(
    struct World* world,
    struct WorldEntity_Projectile* p,
    int cycle)
{
    int const dst_y = World_ProjectileDstY(world, p);
    double const dst_x = (double)p->dst_x;
    double const dst_z = (double)p->dst_z;
    double const dst_y_d = (double)dst_y;

    double dx = dst_x - (double)p->src_x;
    double dz = dst_z - (double)p->src_z;
    double d = sqrt(dx * dx + dz * dz);
    if( d < 1.0 )
        d = 1.0;

    if( !p->launched )
    {
        p->x = (double)p->src_x + (dx * (double)p->startpos) / d;
        p->z = (double)p->src_z + (dz * (double)p->startpos) / d;
        p->y = (double)p->h1;
    }

    double const dt = (double)(p->t2 + 1 - cycle);
    if( dt <= 0.0 )
        return;

    p->vx = (dst_x - p->x) / dt;
    p->vz = (dst_z - p->z) / dt;
    p->velocity = sqrt(p->vx * p->vx + p->vz * p->vz);
    if( !p->launched )
        p->vy = -p->velocity * tan((double)p->angle * WORLD_PROJECTILE_ANGLE_TO_RAD);
    p->ay = ((dst_y_d - p->y - p->vy * dt) * 2.0) / (dt * dt);
}

void
World_ProjectileMove(
    struct WorldEntity_Projectile* p,
    int delta)
{
    if( delta <= 0 )
        return;

    p->launched = true;

    double const delta_d = (double)delta;
    p->x += p->vx * delta_d;
    p->z += p->vz * delta_d;
    p->y += p->vy * delta_d + p->ay * 0.5 * delta_d * delta_d;
    p->vy += p->ay * delta_d;

    p->orientation.yaw =
        ((int)(atan2(p->vx, p->vz) * WORLD_PROJECTILE_ANGLE_TO_RPI2048 + 1024.0)) & 0x7ff;
    p->orientation.pitch =
        ((int)(atan2(p->vy, p->velocity) * WORLD_PROJECTILE_ANGLE_TO_RPI2048)) & 0x7ff;
}

int
World_PlayerSpawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    struct WorldEntityFacet_IdleAnimations idle_animations)
{
    assert(world);
    assert(element_id >= 0);

    struct World_EntityPool* pool = &world->entities.player;
    int idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    *player = (struct WorldEntity_Player){
        .element_id = element_id,
        .grid_position = { .x = scene_x, .z = scene_z, .level = level },
        .draw_position = { .x = (uint32_t)(scene_x * 128 + 64),
                           .z = (uint32_t)(scene_z * 128 + 64),
                           .fx = (float)(scene_x * 128 + 64),
                           .fz = (float)(scene_z * 128 + 64) },
        .orientation = { .yaw = 0, .dst_yaw = 0 },
        .pathing = { .route_length = 0,
                     .route_x = { (uint8_t)scene_x },
                     .route_z = { (uint8_t)scene_z } },
        .idle_animations = idle_animations,
        /* Reference ClientEntity defaults: faceEntity -1, turnspeed 32
         * (players always have the constant; NPCs take NpcType.turnspeed,
         * applied by App_WorldApplyNpcType). */
        .facing = { .entity_id = WORLD_FACING_ENTITY_NONE,
                    .fallback_angle = -1,
                    .direct_angle = -1,
                    .turn_speed = 32 },
        .server_pid = -1,
        .held_left_applied = -1,
        .held_right_applied = -1,
        .loc_merge_id = -1,
        /* 0 is a real healthbar id (the standard bar), so "no bar" has to be
         * spelled rather than left to the pool's zeroing. */
        .combat = { .healthbar_type = -1 },
        /* Reference ClientEntity default: no attached graphic (spotanimId -1). */
        .spotanim = { .id = -1, .frame = -1 },
    };
    return idx;
}

void
World_PlayerDespawn(
    struct World* world,
    int idx)
{
    assert(world);

    struct World_EntityPool* pool = &world->entities.player;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    assert(player);
    World_EmitEntityRemoved(world, player->element_id);
    World_EntityPoolRelease(pool, idx);
}

int
World_NpcSpawn(
    struct World* world,
    int element_id,
    int npc_id,
    int level,
    int scene_x,
    int scene_z,
    int size,
    struct WorldEntityFacet_IdleAnimations idle_animations)
{
    assert(world);
    assert(element_id >= 0);

    struct World_EntityPool* pool = &world->entities.npc;
    int idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    *npc = (struct WorldEntity_NPC){
        .element_id = element_id,
        .grid_position = { .x = scene_x, .z = scene_z, .level = level },
        .draw_position = { .x = (uint32_t)(scene_x * 128 + size * 64),
                           .z = (uint32_t)(scene_z * 128 + size * 64),
                           .fx = (float)(scene_x * 128 + size * 64),
                           .fz = (float)(scene_z * 128 + size * 64) },
        .orientation = { .yaw = 0, .dst_yaw = 0 },
        .pathing = { .route_length = 0,
                     .route_x = { (uint8_t)scene_x },
                     .route_z = { (uint8_t)scene_z } },
        .base_npc_id = npc_id,
        .npc_id = npc_id,
        .size = size,
        /* Default-on: both config flags only ever clear, and a spawn whose
         * npc type has not resolved yet must still draw its dot. */
        .minimap_visible = true,
        .interactable = true,
        .idle_animations = idle_animations,
        .facing = { .entity_id = WORLD_FACING_ENTITY_NONE,
                    .fallback_angle = -1,
                    .direct_angle = -1,
                    .turn_speed = 32 },
        .visible_ops = 0x1f,
        .server_slot = -1,
        /* 0 is a real healthbar id (the standard bar), so "no bar" has to be
         * spelled rather than left to the pool's zeroing. */
        .combat = { .healthbar_type = -1 },
        /* Reference ClientEntity default: no attached graphic (spotanimId -1). */
        .spotanim = { .id = -1, .frame = -1 },
    };
    return idx;
}

void
World_NpcDespawn(
    struct World* world,
    int idx)
{
    assert(world);

    struct World_EntityPool* pool = &world->entities.npc;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    assert(npc);
    World_EmitEntityRemoved(world, npc->element_id);
    World_EntityPoolRelease(pool, idx);
}

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
    int startpos,
    int target)
{
    assert(world);
    assert(element_id >= 0);

    struct World_EntityPool* pool = &world->entities.projectile;
    int idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, idx);
    *p = (struct WorldEntity_Projectile){
        .element_id = element_id,
        .level = level,
        .dst_level = level,
        .src_x = src_x,
        .src_z = src_z,
        .h1 = h1,
        .end_height = end_height,
        .t1 = t1,
        .t2 = t2,
        .angle = angle,
        .startpos = startpos,
        .target = target,
        .dst_x = dst_x,
        .dst_z = dst_z,
        .cycle = 0,
        .launched = false,
    };

    /* Reference MAP_PROJANIM aims at the destination tile once at spawn
     * (before addProjectiles ever sees the projectile), even when a target
     * entity is named — the wire destination is the target's cast-time tile. */
    World_ProjectileSetTarget(world, p, t1);
    return idx;
}

void
World_ProjectileDespawn(
    struct World* world,
    int idx)
{
    assert(world);

    struct World_EntityPool* pool = &world->entities.projectile;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, idx);
    assert(p);
    World_EmitEntityRemoved(world, p->element_id);
    World_EntityPoolRelease(pool, idx);
}

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
    int lifetime)
{
    assert(world);
    assert(element_id >= 0);

    struct World_EntityPool* pool = &world->entities.spotanim;
    int idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, idx);
    *s = (struct WorldEntity_Spotanim){
        .element_id = element_id,
        .level = level,
        .draw_position = { .x = (uint32_t)scene_x,
                           .z = (uint32_t)scene_z,
                           .y = (uint32_t)y,
                           .fx = (float)scene_x,
                           .fz = (float)scene_z },
        .orientation = { .yaw = (uint16_t)orientation, .dst_yaw = (uint16_t)orientation },
        .idle_cycles = idle_delay,
        .active_cycle = 0,
        .lifetime = lifetime,
        .active = false,
    };

    /*
     * `idle_delay <= 0` (spotanim_map's common case, e.g. Inferno's
     * tzhaar_rock_smash) means "visible this cycle" — and the app layer
     * (app_world_spawn_spotanim_now) already treats it that way: it skips the
     * anim_external park-and-wait entirely and starts the model's sequence
     * synchronously, in the SAME call that spawns this entity, because a
     * zero-delay graphic has no flight/park window to hide the unplayed first
     * frame behind (see that function's own comment).
     *
     * World's bookkeeping used to disagree with what was already on screen:
     * `active` stayed false and `active_cycle` stayed 0 until the *next*
     * `World_CycleUpdateSpotanims` pass flipped them — one whole cycle after
     * the graphic had already started playing. That is invisible today only
     * because the one consumer of WorldEventKind_SpotanimStarted
     * (App_WorldDrainEntityRemoved) is itself gated on `anim_external`, which
     * this path never sets — so the event this emits below is a no-op for it.
     * What the stale flag actually broke is quieter: `active_cycle` is this
     * spotanim's own lifetime clock (World_CycleUpdateSpotanims,
     * `active_cycle >= lifetime` -> despawn), so starting it a cycle late in
     * World's model meant the entity — and the scene element it owns — lived
     * one cycle longer than the graphic it was timed against. Flipping
     * `active` here, at the same instant the app
     * layer starts drawing frame 0, is what makes "when World considers this
     * graphic to have started" and "when the player can see it" the same
     * question for BOTH delay classes, matching how a fresh NPC's primary
     * animation is scored from the cycle it is *set*, not the cycle after.
     */
    if( idle_delay <= 0 )
    {
        s->idle_cycles = 0;
        s->active = true;
        World_EmitEvent(world, WorldEventKind_SpotanimStarted, element_id);
    }
    return idx;
}

void
World_SpotanimDespawn(
    struct World* world,
    int idx)
{
    assert(world);

    struct World_EntityPool* pool = &world->entities.spotanim;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, idx);
    assert(s);
    World_EmitEntityRemoved(world, s->element_id);
    World_EntityPoolRelease(pool, idx);
}

/* ---------------------------------------------- plugin-owned world objects */

int
World_PluginObjectSpawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    int y,
    int orientation,
    int size_x,
    int size_z)
{
    assert(world);
    assert(element_id >= 0);
    assert(size_x > 0);
    assert(size_z > 0);

    struct World_EntityPool* pool = &world->entities.plugin_object;
    int idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    struct WorldEntity_PluginObject* obj = World_EntityPoolGet(pool, idx);
    assert(obj);
    *obj = (struct WorldEntity_PluginObject){
        .element_id = element_id,
        .level = level,
        .draw_position = { .x = (uint32_t)scene_x,
                           .z = (uint32_t)scene_z,
                           .y = (uint32_t)y,
                           .fx = (float)scene_x,
                           .fz = (float)scene_z },
        .orientation = { .yaw = (uint16_t)orientation, .dst_yaw = (uint16_t)orientation },
        .size_x = size_x,
        .size_z = size_z,
        .active = true,
    };
    return idx;
}

void
World_PluginObjectDespawn(
    struct World* world,
    int idx)
{
    assert(world);

    struct World_EntityPool* pool = &world->entities.plugin_object;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    struct WorldEntity_PluginObject* obj = World_EntityPoolGet(pool, idx);
    assert(obj);
    World_EmitEntityRemoved(world, obj->element_id);
    World_EntityPoolRelease(pool, idx);
}

void
World_PluginObjectSetActive(
    struct World* world,
    int idx,
    bool active)
{
    assert(world);

    struct World_EntityPool* pool = &world->entities.plugin_object;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    struct WorldEntity_PluginObject* obj = World_EntityPoolGet(pool, idx);
    assert(obj);
    obj->active = active;
}

void
World_PluginObjectClear(struct World* world)
{
    struct World_EntityPool* pool;
    int next;

    assert(world);

    pool = &world->entities.plugin_object;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = next )
    {
        next = World_EntityPoolNext(pool, i);
        World_PluginObjectDespawn(world, i);
    }
}

/* --- REBUILD_NORMAL scene-base relocation (Client-TS rebuild handler) ---
 *
 * The new scene origin moved by (dx, dz) tiles, so every kept entity's
 * scene-local coordinates move by the negation (routeX -= dx, x -= dx*128).
 * Entities that land outside the scene must stay tracked — the server
 * addresses its PLAYER/NPC_INFO lists by position, so the client cannot drop
 * them — but are parked on tile 255, outside any scene we build, which the
 * painter registration and position-sync bounds checks already skip. The
 * server's next info packet removes them properly. */

static uint8_t
world_shift_route_coord(
    int coord,
    int delta)
{
    int shifted = coord - delta;
    if( shifted < 0 || shifted > 255 )
        return 255;
    return (uint8_t)shifted;
}

static void
world_shift_mover(
    struct WorldEntityFacet_GridPosition* grid,
    struct WorldEntityFacet_DrawPosition* draw,
    struct WorldEntityFacet_Pathing* pathing,
    struct WorldEntityFacet_ExactMove* exact,
    int dx,
    int dz)
{
    for( int j = 0; j < 10; j++ )
    {
        pathing->route_x[j] = world_shift_route_coord(pathing->route_x[j], dx);
        pathing->route_z[j] = world_shift_route_coord(pathing->route_z[j], dz);
    }
    grid->x = pathing->route_x[0];
    grid->z = pathing->route_z[0];

    if( pathing->route_x[0] == 255 || pathing->route_z[0] == 255 )
    {
        /* Parked out-of-scene: pin the draw position to the parked tile so
         * nothing interpolates across the scene if the entity comes back. */
        pathing->route_length = 0;
        draw->x = (uint32_t)(pathing->route_x[0] * 128 + 64);
        draw->z = (uint32_t)(pathing->route_z[0] * 128 + 64);
    }
    else
    {
        int fine_x = (int)draw->x - dx * 128;
        int fine_z = (int)draw->z - dz * 128;
        if( fine_x < 0 )
            fine_x = pathing->route_x[0] * 128 + 64;
        if( fine_z < 0 )
            fine_z = pathing->route_z[0] * 128 + 64;
        draw->x = (uint32_t)fine_x;
        draw->z = (uint32_t)fine_z;
    }

    if( exact && (exact->move_start != 0 || exact->move_end != 0) )
    {
        exact->start_x = world_shift_route_coord(exact->start_x, dx);
        exact->start_z = world_shift_route_coord(exact->start_z, dz);
        exact->end_x = world_shift_route_coord(exact->end_x, dx);
        exact->end_z = world_shift_route_coord(exact->end_z, dz);
    }
}

void
World_ShiftEntities(
    struct World* world,
    int dx,
    int dz)
{
    struct World_EntityPool* pool;

    assert(world);
    if( dx == 0 && dz == 0 )
        return;

    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( player )
            world_shift_mover(
                &player->grid_position,
                &player->draw_position,
                &player->pathing,
                &player->exact_move,
                dx,
                dz);
    }

    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( npc )
            world_shift_mover(
                &npc->grid_position,
                &npc->draw_position,
                &npc->pathing,
                &npc->exact_move,
                dx,
                dz);
    }

    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
        int sx, sz;
        if( !stack )
            continue;
        sx = stack->grid_position.x - dx;
        sz = stack->grid_position.z - dz;
        /* Out-of-scene stacks are deleted by the caller (App layer, which
         * also owns the element reposition); park them at 255 like movers. */
        stack->grid_position.x = (sx < 0 || sx > 255) ? 255 : sx;
        stack->grid_position.z = (sz < 0 || sz > 255) ? 255 : sz;
        World_DrawPositionSet(
            &stack->draw_position, stack->grid_position.x * 128 + 64,
            stack->grid_position.z * 128 + 64);
    }

    /* Loc-change list (Client-TS locChanges / deob field1353): shift and
     * unlink entries that leave the scene. */
    {
        int write = 0;
        int scene = world->_scene_size > 0 ? world->_scene_size : 104;
        for( int i = 0; i < world->loc_change_count; i++ )
        {
            struct World_LocChange* loc = &world->loc_changes[i];
            loc->x -= dx;
            loc->z -= dz;
            if( loc->x < 0 || loc->z < 0 || loc->x >= scene || loc->z >= scene )
                continue;
            if( write != i )
                world->loc_changes[write] = *loc;
            write++;
        }
        world->loc_change_count = write;
    }
}

void
World_ClearProjectilesAndSpotanims(struct World* world)
{
    struct World_EntityPool* pool;
    int next;

    assert(world);

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = next )
    {
        next = World_EntityPoolNext(pool, i);
        World_ProjectileDespawn(world, i);
    }

    pool = &world->entities.spotanim;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = next )
    {
        next = World_EntityPoolNext(pool, i);
        World_SpotanimDespawn(world, i);
    }
}

static void
World_CopyMenuActions(
    struct WorldEntityFacet_Action dest[5],
    char const src[5][32])
{
    for( int i = 0; i < 5; i++ )
    {
        dest[i].code = (uint16_t)i;
        dest[i].name[0] = '\0';
        if( src && src[i][0] != '\0' )
        {
            strncpy(dest[i].name, src[i], sizeof(dest[i].name) - 1);
            dest[i].name[sizeof(dest[i].name) - 1] = '\0';
        }
    }
}

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
    int shape,
    int angle,
    int force_approach,
    char const* name,
    char const actions[5][32],
    int interactive)
{
    assert(world);
    if( element_id < 0 || loc_id < 0 )
        return -1;

    struct World_EntityPool* pool = &world->entities.scenery;
    int idx = World_EntityPoolAlloc(pool);
    if( idx < 0 )
        return -1;

    struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, idx);
    assert(scenery);
    memset(scenery, 0, sizeof(*scenery));
    scenery->element_id = element_id;
    scenery->loc_id = loc_id;
    scenery->grid_position.x = scene_x;
    scenery->grid_position.z = scene_z;
    scenery->grid_position.level = level;
    scenery->size_x = size_x;
    scenery->size_z = size_z;
    scenery->shape = shape;
    scenery->angle = angle;
    scenery->force_approach = force_approach & 0xf;
    if( name )
    {
        strncpy(scenery->name, name, sizeof(scenery->name) - 1);
        scenery->name[sizeof(scenery->name) - 1] = '\0';
    }
    World_CopyMenuActions(scenery->actions, actions);
    scenery->interactive = interactive ? 1 : 0;
    scenery->painter_wall_ab = -1;
    scenery->painter_ground_decor = 0;
    scenery->painter_wall_side = 0;
    return idx;
}

struct WorldEntity_Scenery*
World_SceneryGetByElementId(
    struct World* world,
    int element_id)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, i);
        if( scenery && scenery->element_id == element_id )
            return scenery;
    }
    return NULL;
}

struct WorldEntity_NPC*
World_NpcGetByServerSlot(
    struct World* world,
    int server_slot)
{
    assert(world);
    if( server_slot < 0 )
        return NULL;
    struct World_EntityPool* pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( npc && npc->server_slot == server_slot )
            return npc;
    }
    return NULL;
}

struct WorldEntity_Player*
World_PlayerGetByServerPid(
    struct World* world,
    int server_pid)
{
    assert(world);
    if( server_pid < 0 )
        return NULL;
    struct World_EntityPool* pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( player && player->server_pid == server_pid )
            return player;
    }
    return NULL;
}

struct WorldEntity_Player*
World_PlayerGetByElementId(
    struct World* world,
    int element_id)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( player && player->element_id == element_id )
            return player;
    }
    return NULL;
}

struct WorldEntity_NPC*
World_NpcGetByElementId(
    struct World* world,
    int element_id,
    int* out_index)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( npc && npc->element_id == element_id )
        {
            if( out_index )
                *out_index = i;
            return npc;
        }
    }
    return NULL;
}

void
World_ClearSceneryPicks(struct World* world)
{
    if( world )
        world->scenery_pick_count = 0;
}

void
World_RegisterSceneryPick(
    struct World* world,
    int element_id,
    int loc_id)
{
    assert(world);
    if( world->scenery_pick_count >= WORLD_SCENERY_PICK_MAX )
        return;

    struct WorldEntity_Scenery* scenery = World_SceneryGetByElementId(world, element_id);
    int scenery_index = -1;
    if( scenery )
    {
        struct World_EntityPool* pool = &world->entities.scenery;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Scenery* s = World_EntityPoolGet(pool, i);
            if( s == scenery )
            {
                scenery_index = i;
                break;
            }
        }
    }

    world->scenery_picks[world->scenery_pick_count++] = (struct World_SceneryPick){
        .element_id = element_id,
        .loc_id = loc_id,
        .scenery_index = scenery_index,
    };
}

static void
World_PathJumpEntity(
    struct WorldEntityFacet_Pathing* pathing,
    struct WorldEntityFacet_DrawPosition* draw,
    struct WorldEntityFacet_GridPosition* grid,
    int size,
    bool force_teleport,
    int x,
    int z)
{
    enum World_PathingJump jump = World_EntityPathingJump(pathing, force_teleport, x, z);
    if( jump == WORLD_PATHING_JUMP_TELEPORT )
    {
        World_EntityDrawPositionSetToTile(draw, x, z, size, size);
        grid->x = x;
        grid->z = z;
    }
}

void
World_PlayerPathPushStep(
    struct World* world,
    int idx,
    int step_type,
    int direction)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    World_EntityPathingPushStep(&player->pathing, step_type, direction);
}

void
World_NpcPathPushStep(
    struct World* world,
    int idx,
    int step_type,
    int direction)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    World_EntityPathingPushStep(&npc->pathing, step_type, direction);
}

void
World_PlayerPathJump(
    struct World* world,
    int idx,
    bool force_teleport,
    int x,
    int z)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    World_PathJumpEntity(
        &player->pathing, &player->draw_position, &player->grid_position, 1, force_teleport, x, z);
}

void
World_PlayerPathJumpCollisionAware(
    struct World* world,
    int idx,
    struct CollisionMap* collision,
    bool force_teleport,
    int x,
    int z,
    int step_type)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    enum World_PathingJump jump = World_EntityPathingJumpCollisionAware(
        &player->pathing, collision, force_teleport, x, z, step_type);
    if( jump == WORLD_PATHING_JUMP_TELEPORT )
    {
        World_EntityDrawPositionSetToTile(&player->draw_position, x, z, 1, 1);
        player->grid_position.x = x;
        player->grid_position.z = z;
    }
}

void
World_NpcPathJump(
    struct World* world,
    int idx,
    bool force_teleport,
    int x,
    int z)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    int size = npc->size > 0 ? npc->size : 1;
    World_PathJumpEntity(
        &npc->pathing, &npc->draw_position, &npc->grid_position, size, force_teleport, x, z);
}

void
World_PlayerFaceEntity(
    struct World* world,
    int idx,
    int entity_id)
{
    World_PlayerFaceEntityDetailed(world, idx, entity_id, -1, false);
}

void
World_PlayerFaceEntityDetailed(
    struct World* world,
    int idx,
    int entity_id,
    int fallback_angle,
    bool instant)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    player->facing.entity_id = entity_id;
    player->facing.fallback_angle = fallback_angle;
    player->facing.instant = instant;
}

void
World_NpcFaceEntity(
    struct World* world,
    int idx,
    int entity_id)
{
    World_NpcFaceEntityDetailed(world, idx, entity_id, -1, false);
}

void
World_NpcFaceEntityDetailed(
    struct World* world,
    int idx,
    int entity_id,
    int fallback_angle,
    bool instant)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    npc->facing.entity_id = entity_id;
    npc->facing.fallback_angle = fallback_angle;
    npc->facing.instant = instant;
}

static void
World_BeginModernFacing(
    struct WorldEntityFacet_Facing* facing,
    int movement_mode)
{
    facing->entity_id = WORLD_FACING_ENTITY_NONE;
    facing->square_x = 0;
    facing->square_z = 0;
    facing->fallback_angle = -1;
    facing->direct_angle = -1;
    facing->instant = false;
    facing->face_during_movement = movement_mode == 1;
}

void
World_PlayerBeginModernFacing(
    struct World* world,
    int idx,
    int movement_mode)
{
    assert(world);
    assert(World_EntityPoolIsActive(&world->entities.player, idx));
    struct WorldEntity_Player* player =
        World_EntityPoolGet(&world->entities.player, idx);
    World_BeginModernFacing(&player->facing, movement_mode);
}

void
World_NpcBeginModernFacing(
    struct World* world,
    int idx,
    int movement_mode)
{
    assert(world);
    assert(World_EntityPoolIsActive(&world->entities.npc, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, idx);
    World_BeginModernFacing(&npc->facing, movement_mode);
}

void
World_PlayerFaceCoord(
    struct World* world,
    int idx,
    int square_x,
    int square_z)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    player->facing.square_x = square_x;
    player->facing.square_z = square_z;
}

void
World_NpcFaceCoord(
    struct World* world,
    int idx,
    int square_x,
    int square_z)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    npc->facing.square_x = square_x;
    npc->facing.square_z = square_z;
}

void
World_PlayerFaceAngle(
    struct World* world,
    int idx,
    int angle,
    bool instant)
{
    struct WorldEntity_Player* player;

    assert(world);
    assert(World_EntityPoolIsActive(&world->entities.player, idx));
    player = World_EntityPoolGet(&world->entities.player, idx);
    player->facing.direct_angle = angle & 0x7ff;
    player->facing.instant = instant;
}

void
World_NpcFaceAngle(
    struct World* world,
    int idx,
    int angle,
    bool instant)
{
    struct WorldEntity_NPC* npc;

    assert(world);
    assert(World_EntityPoolIsActive(&world->entities.npc, idx));
    npc = World_EntityPoolGet(&world->entities.npc, idx);
    npc->facing.direct_angle = angle & 0x7ff;
    npc->facing.instant = instant;
}

static void
World_SetAnimationTrack(
    struct WorldEntityFacet_Animation* animation,
    int animation_id,
    int animation_type)
{
    struct WorldEntityFacet_AnimationStep* step =
        animation_type == WORLD_ANIMATION_TYPE_SECONDARY ? &animation->secondary
                                                         : &animation->primary;
    if( animation_id < 0 )
    {
        step->anim_id = (uint16_t)-1;
        step->frame = 0;
        step->cycle = 0;
        return;
    }
    if( step->anim_id != (uint16_t)animation_id )
    {
        step->anim_id = (uint16_t)animation_id;
        step->frame = 0;
        step->cycle = 0;
    }
}

void
World_PlayerSetAnimation(
    struct World* world,
    int idx,
    int animation_id,
    int animation_type)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    World_SetAnimationTrack(&player->animation, animation_id, animation_type);
}

void
World_NpcSetAnimation(
    struct World* world,
    int idx,
    int animation_id,
    int animation_type)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    World_SetAnimationTrack(&npc->animation, animation_id, animation_type);
}

int
World_EventsCount(struct World* world)
{
    assert(world);
    return world->event_count;
}

const struct World_Event*
World_EventsPeek(
    struct World* world,
    int i)
{
    assert(world);
    if( i < 0 || i >= world->event_count )
        return NULL;
    return &world->events[i];
}

void
World_EventsClear(struct World* world)
{
    assert(world);
    world->event_count = 0;
}

void
World_SetFeatures(
    struct World* world,
    struct ToriRS_FeatureTable const* features)
{
    assert(world);
    world->features = features;
}

int
World_MoverModel(struct World const* world)
{
    assert(world);
    return world->features ? world->features->mover_model : TORIRS_MOVER_CYCLE_INTEGER;
}

void
World_SetSeqSource(
    struct World* world,
    struct World_SeqSource const* source)
{
    assert(world);
    if( source )
        world->seq_source = *source;
    else
        memset(&world->seq_source, 0, sizeof(world->seq_source));
}

void
World_SetAnimSoundSink(
    struct World* world,
    struct World_AnimSoundSink const* sink)
{
    assert(world);
    if( sink )
        world->anim_sound_sink = *sink;
    else
        memset(&world->anim_sound_sink, 0, sizeof(world->anim_sound_sink));
}

/* Seq-source getters with the documented defaults for a NULL source. */
static int
world_seq_priority(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.priority )
        return world->seq_source.priority(world->seq_source.userdata, seq_id);
    return 5;
}

static int
world_seq_duplicate_behavior(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.duplicate_behavior )
        return world->seq_source.duplicate_behavior(world->seq_source.userdata, seq_id);
    return -1;
}

/*
 * The readyanim's loop point is where an action animation is authored to start.
 *
 * A creature's attack clip is not drawn in isolation: the animator builds it as
 * a departure from the ready loop and brings it back, so its first frame is the
 * ready loop's first frame and its last is the frame before that. Xarpus is the
 * case that proves it — seq 8059 frame 0 poses model 35383 IDENTICALLY to seq
 * 8058 frame 0 (bit-identical render), and 8058 is 120 cycles long, which is
 * exactly his four-tick attack cadence. The clip only reads as one motion if
 * the ready loop is at its start when the clip takes over.
 *
 * Nothing kept it there. The ready track free-runs underneath the action (it
 * has to — its frame sounds keep playing, and the reference steps it every
 * cycle regardless), so whatever phase it happened to be in when the fight
 * started is the phase every attack cut in at, forever. Measured on Xarpus
 * before this: the spit began with the ready loop at frame 39 of 52, cycle 87
 * of 120, on every spit of the fight.
 *
 * Restarting it here is invisible at the moment it happens, which is the whole
 * reason it is safe: while an un-delayed action animation is playing over the
 * readyanim, the readyanim is not drawn at all — `getModel` passes null for the
 * movement sequence (reference NPC.getModel; ours is
 * app_world_apply_entity_anim_tracks, which binds a secondary only when it is
 * NOT the readyanim). The reset chooses where the ready loop RESUMES, nothing
 * more, and it resumes at a fixed offset into the loop rather than a drifting
 * one.
 *
 * Three conditions, and each earns its place:
 *
 *   - the secondary has to BE the readyanim. A walk animation is drawn while
 *     an action plays (that is the walkmerge blend), and restarting it would
 *     stutter the gait mid-stride.
 *   - the action has to start driving THIS cycle (`delay == 0`). A delayed
 *     action leaves the readyanim on screen until the delay expires, so a reset
 *     now would be a visible jump.
 *   - it is a fresh action, not a re-application of one already playing — that
 *     branch returns above.
 *
 * This is the same rule the reference states at the far end: NpcType opcode 130
 * restarts the readyanim when an action animation FINISHES (33 npcs in the
 * rev-239 cache carry it; Xarpus is not one of them). Jagex put the restart on
 * the exit and left the entry to chance. The entry is the seam the animation
 * data is authored around, so this client locks that one.
 */
static void
world_restart_readyanim_under_action(
    struct WorldEntityFacet_Animation* animation,
    int readyanim,
    int delay)
{
    if( delay != 0 || readyanim < 0 )
        return;
    if( animation->secondary.anim_id != (uint16_t)readyanim )
        return;
    animation->secondary.frame = 0;
    animation->secondary.cycle = 0;
    animation->secondary.loop = 0;
}

/* Reference readExtendedInfo ANIM application (Client.ts 8401-8430):
 * same-anim RestartMode RESET restarts frame/cycle/loop; RESETLOOP resets
 * only the loop counter; a different anim applies when no primary is playing
 * or its priority is >= the current one's. */
static void
world_apply_primary_animation(
    struct World* world,
    struct WorldEntityFacet_Animation* animation,
    struct WorldEntityFacet_Pathing const* pathing,
    int readyanim,
    int seq_id,
    int delay)
{
    if( seq_id < 0 )
    {
        animation->primary.anim_id = (uint16_t)-1;
        animation->primary.frame = 0;
        animation->primary.cycle = 0;
        animation->primary.delay = 0;
        animation->primary.loop = 0;
        return;
    }

    if( animation->primary.anim_id == (uint16_t)seq_id )
    {
        int restart = world_seq_duplicate_behavior(world, seq_id);
        /*
         * Worth a line because of what it looks like from outside. A seq
         * re-sent while the entity is already on it does NOT restart unless
         * the seq's own replyMode says so (mode 1), which is the reference's
         * rule and is right — but a one-shot death seq parked on its last
         * frame that receives itself again therefore stays parked, and the
         * corpse is reaped showing a pose that never moved. That reads as "it
         * just disappeared". The sender is at fault when that happens, not
         * this branch: see the ToB Matomenos, whose scripted arrival death and
         * engine kill could both play 8097 on one npc.
         */
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(stderr,
                    "anim: seq %d re-applied while already playing (frame %d, restart mode %d)\n",
                    seq_id, (int)animation->primary.frame, restart);
        if( restart == 1 ) /* RestartMode.RESET */
        {
            animation->primary.frame = 0;
            animation->primary.cycle = 0;
            animation->primary.delay = (uint8_t)delay;
            animation->primary.loop = 0;
            animation->preanim_route_length = pathing->route_length;
            return;
        }
        if( restart == 2 ) /* RestartMode.RESETLOOP */
        {
            animation->primary.loop = 0;
            return;
        }
    }

    if( animation->primary.anim_id != (uint16_t)-1 && animation->primary.anim_id != 0 &&
        world_seq_priority(world, seq_id) <
            world_seq_priority(world, animation->primary.anim_id) )
    {
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "anim: seq %d (prio %d) refused by incumbent %d (prio %d)\n",
                seq_id,
                world_seq_priority(world, seq_id),
                (int)animation->primary.anim_id,
                world_seq_priority(world, animation->primary.anim_id));
        return;
    }
    if( getenv("TORIRS_ANIM_DEBUG") )
        fprintf(
            stderr,
            "anim: seq %d (prio %d) applied over %d [idle seq %d frame %d cycle %d]\n",
            seq_id,
            world_seq_priority(world, seq_id),
            (int)animation->primary.anim_id,
            (int)(int16_t)animation->secondary.anim_id,
            (int)animation->secondary.frame,
            (int)animation->secondary.cycle);

    animation->primary.anim_id = (uint16_t)seq_id;
    animation->primary.frame = 0;
    animation->primary.cycle = 0;
    animation->primary.delay = (uint8_t)delay;
    animation->primary.loop = 0;
    animation->preanim_route_length = pathing->route_length;
    world_restart_readyanim_under_action(animation, readyanim, delay);
}

void
World_PlayerSetPrimaryAnimation(
    struct World* world,
    int idx,
    int seq_id,
    int delay)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    world_apply_primary_animation(
        world,
        &player->animation,
        &player->pathing,
        player->idle_animations.readyanim,
        seq_id,
        delay);
}

void
World_NpcSetPrimaryAnimation(
    struct World* world,
    int idx,
    int seq_id,
    int delay)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    world_apply_primary_animation(
        world,
        &npc->animation,
        &npc->pathing,
        npc->idle_animations.readyanim,
        seq_id,
        delay);
}

void
World_PlayerSetExactMove(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing)
{
    World_PlayerSetExactMoveDetailed(
        world, idx, start_x, start_z, end_x, end_z, start_cycle_delta,
        end_cycle_delta, facing, false);
}

void
World_PlayerSetExactMoveDetailed(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing,
    bool facing_is_yaw)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);

    player->exact_move.start_x = (uint8_t)start_x;
    player->exact_move.start_z = (uint8_t)start_z;
    player->exact_move.end_x = (uint8_t)end_x;
    player->exact_move.end_z = (uint8_t)end_z;
    player->exact_move.move_start = world->cycle + start_cycle_delta;
    player->exact_move.move_end = world->cycle + end_cycle_delta;
    player->exact_move.facing = (uint16_t)facing;
    player->exact_move.facing_is_yaw = facing_is_yaw;

    /* Reference abortRoute(). */
    player->pathing.route_length = 0;
}

void
World_NpcSetExactMove(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing)
{
    World_NpcSetExactMoveDetailed(
        world, idx, start_x, start_z, end_x, end_z, start_cycle_delta,
        end_cycle_delta, facing, false);
}

void
World_NpcSetExactMoveDetailed(
    struct World* world,
    int idx,
    int start_x,
    int start_z,
    int end_x,
    int end_z,
    int start_cycle_delta,
    int end_cycle_delta,
    int facing,
    bool facing_is_yaw)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);

    npc->exact_move.start_x = (uint8_t)start_x;
    npc->exact_move.start_z = (uint8_t)start_z;
    npc->exact_move.end_x = (uint8_t)end_x;
    npc->exact_move.end_z = (uint8_t)end_z;
    npc->exact_move.move_start = world->cycle + start_cycle_delta;
    npc->exact_move.move_end = world->cycle + end_cycle_delta;
    npc->exact_move.facing = (uint16_t)facing;
    npc->exact_move.facing_is_yaw = facing_is_yaw;
    npc->pathing.route_length = 0;
}

void
World_LocChangePush(
    struct World* world,
    int level,
    int layer,
    int x,
    int z,
    int old_type,
    int old_angle,
    int old_shape,
    int new_type,
    int new_angle,
    int new_shape,
    int start_time,
    int end_time)
{
    struct World_LocChange* loc;

    assert(world);
    if( world->loc_change_count >= WORLD_LOC_CHANGE_MAX )
        return;
    loc = &world->loc_changes[world->loc_change_count++];
    loc->level = level;
    loc->layer = layer;
    loc->x = x;
    loc->z = z;
    loc->old_type = old_type;
    loc->old_angle = old_angle;
    loc->old_shape = old_shape;
    loc->new_type = new_type;
    loc->new_angle = new_angle;
    loc->new_shape = new_shape;
    loc->start_time = start_time;
    loc->end_time = end_time;
}

void
World_LocChangesClear(struct World* world)
{
    assert(world);
    world->loc_change_count = 0;
}

void
World_LocChangesTick(
    struct World* world,
    int cycles_elapsed,
    void (*apply)(void* user, int level, int x, int z, int loc_id, int shape, int angle),
    void* user)
{
    int write;

    assert(world);
    assert(apply);
    if( cycles_elapsed <= 0 || world->loc_change_count <= 0 )
        return;

    for( int c = 0; c < cycles_elapsed; c++ )
    {
        write = 0;
        for( int i = 0; i < world->loc_change_count; i++ )
        {
            struct World_LocChange loc = world->loc_changes[i];

            /* Permanent records (end_time < 0): keep, never countdown. */
            if( loc.end_time < 0 )
            {
                world->loc_changes[write++] = loc;
                continue;
            }

            if( loc.end_time > 0 )
                loc.end_time--;

            if( loc.end_time != 0 )
            {
                if( loc.start_time > 0 )
                    loc.start_time--;
                if( loc.start_time == 0 )
                {
                    apply(user, loc.level, loc.x, loc.z, loc.new_type, loc.new_shape,
                          loc.new_angle);
                    loc.start_time = -1;
                    if( loc.old_type == loc.new_type && loc.old_type == -1 )
                        continue;
                    if( loc.old_type == loc.new_type && loc.old_angle == loc.new_angle &&
                        loc.old_shape == loc.new_shape )
                        continue;
                }
                world->loc_changes[write++] = loc;
            }
            else
            {
                apply(user, loc.level, loc.x, loc.z, loc.old_type, loc.old_shape,
                      loc.old_angle);
                /* Drop the entry (restored). */
            }
        }
        world->loc_change_count = write;
    }
}

static void
world_set_entity_spotanim(
    struct World* world,
    struct WorldEntityFacet_EntitySpotanim* spot,
    int spotanim_id,
    int height,
    int cycle_delay)
{
    spot->id = spotanim_id;
    spot->height = height;
    spot->last_cycle = world->cycle + cycle_delay;
    spot->frame = cycle_delay > 0 ? -1 : 0;
    spot->cycle = 0;
}

void
World_PlayerSetSpotanim(
    struct World* world,
    int idx,
    int spotanim_id,
    int height,
    int cycle_delay)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    world_set_entity_spotanim(world, &player->spotanim, spotanim_id, height, cycle_delay);
}

void
World_NpcSetSpotanim(
    struct World* world,
    int idx,
    int spotanim_id,
    int height,
    int cycle_delay)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    world_set_entity_spotanim(world, &npc->spotanim, spotanim_id, height, cycle_delay);
}

void
World_NpcSetType(
    struct World* world,
    int idx,
    int npc_id,
    int size,
    struct WorldEntityFacet_IdleAnimations const* idle)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);

    npc->npc_id = npc_id;
    npc->size = size > 0 ? size : 1;
    if( idle )
        npc->idle_animations = *idle;
    /*
     * A transmog does NOT touch the transient animation.
     *
     * This used to clear `primary`, on a comment claiming that was reference
     * ClientEntity behaviour. It is not: `Client.ts`'s CHANGETYPE branch writes
     * type, size, turnspeed, the four walk anims and readyanim, and never goes
     * near `primaryAnim` (the only paths that clear it are RESET_ANIMS, the
     * ABORTANIM postanim rule, and the sequence running out). Clearing it here
     * silently destroyed any one-shot that shared a tick with a retype — and
     * since the wire writes the SEQUENCE block BEFORE the TRANSFORMATION block
     * of the same packet, `npc_anim(X)` followed by `npc_changetype(Y)` was
     * *guaranteed* to be that case, not merely at risk of it.
     *
     * The visible bug was the Queen Black Dragon having no death: content plays
     * her 9-second return-to-sleep and retypes her to the sleeping form, and
     * she snapped straight to the sleeping idle instead. It is the same defect
     * behind every transforming boss and every summoning familiar losing an
     * animation across a form change.
     *
     * Nothing has to be re-bound for this to render. The world tick is the
     * authority on the frame, and `app_world_apply_entity_anim_tracks` rebinds
     * (seq, frame) onto whatever element the npc currently owns every frame —
     * including the freshly-built model the retype just installed.
     */
}

void
World_PlayerSetAppearance(
    struct World* world,
    int idx,
    int const slots[12],
    int const identkit[12],
    int const colors[5],
    struct WorldEntityFacet_IdleAnimations const* idle,
    char const* name,
    int combat_level,
    int gender)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);

    if( slots )
        memcpy(player->appearance.slots, slots, sizeof(player->appearance.slots));
    if( identkit )
        memcpy(player->appearance.identkit, identkit, sizeof(player->appearance.identkit));
    else if( slots )
        memcpy(player->appearance.identkit, slots, sizeof(player->appearance.identkit));
    if( colors )
        memcpy(player->appearance.colors, colors, sizeof(player->appearance.colors));
    if( idle )
        player->idle_animations = *idle;
    if( name )
    {
        strncpy(player->name, name, sizeof(player->name) - 1);
        player->name[sizeof(player->name) - 1] = '\0';
    }
    player->combat_level = combat_level;
    player->gender = gender;
}

void
World_PlayerAddHitmark(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health)
{
    World_PlayerAddHitmarkTimed(
        world,
        idx,
        damage_type,
        damage,
        health,
        total_health,
        0,
        WORLD_ENTITY_DAMAGE_SLOTS,
        WORLD_HITMARK_DEFAULT_DURATION,
        WORLD_HITMARK_POLICY_DISCARD);
}

void
World_PlayerAddHitmarkTimed(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health,
    int delay,
    int slot_limit,
    int duration,
    int slot_policy)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    World_EntityAddHitmark(
        player->combat.damage_values,
        player->combat.damage_types,
        player->combat.damage_start_cycles,
        player->combat.damage_cycles,
        world->cycle,
        damage_type,
        damage,
        delay,
        slot_limit,
        duration,
        slot_policy);
    player->combat.health = health;
    player->combat.total_health = total_health;
    player->combat.combat_cycle = world->cycle + 400;
}

/*
 * Record one HEADBAR block.
 *
 * The fills are fractions of the healthbar TYPE's own width and mean nothing
 * without it, so nothing here interprets them -- the caller resolves the type
 * (it is the side that can reach the config table) and hands over the already
 * computed expiry. World's job is to remember what the server said until the
 * overlay build reads it back.
 */
void
World_PlayerSetHealthbar(
    struct World* world,
    int idx,
    struct WorldEntity_Headbar bar)
{
    struct WorldEntity_Player* player;

    assert(world);
    assert(bar.type >= 0);
    if( !World_EntityPoolIsActive(&world->entities.player, idx) )
        return;
    player = World_EntityPoolGet(&world->entities.player, idx);
    World_EntityApplyHeadbar(&player->combat, bar);
}

void
World_PlayerClearHealthbar(struct World* world, int idx)
{
    struct WorldEntity_Player* player;

    assert(world);
    if( !World_EntityPoolIsActive(&world->entities.player, idx) )
        return;
    player = World_EntityPoolGet(&world->entities.player, idx);
    player->combat.healthbar_type = -1;
}

int
World_ObjStackAdd(
    struct World* world,
    int element_id,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count,
    char const* name,
    char const actions[5][32])
{
    struct World_EntityPool* pool;
    struct WorldEntity_ObjStack* stack;
    int idx;

    assert(world);
    pool = &world->entities.obj_stack;
    idx = World_EntityPoolAlloc(pool);
    if( idx < 0 )
        return -1;
    stack = World_EntityPoolGet(pool, idx);
    memset(stack, 0, sizeof(*stack));
    stack->element_id = element_id;
    stack->grid_position.x = scene_x;
    stack->grid_position.z = scene_z;
    stack->grid_position.level = level;
    World_DrawPositionSet(&stack->draw_position, scene_x * 128 + 64, scene_z * 128 + 64);
    stack->obj_id = obj_id;
    stack->count = count;
    if( name )
    {
        strncpy(stack->name, name, sizeof(stack->name) - 1);
        stack->name[sizeof(stack->name) - 1] = '\0';
    }
    World_CopyMenuActions(stack->actions, actions);
    return idx;
}

int
World_ObjStackFind(
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int obj_id)
{
    struct World_EntityPool* pool;

    assert(world);
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
        if( !stack )
            continue;
        if( stack->grid_position.x == scene_x && stack->grid_position.z == scene_z &&
            stack->grid_position.level == level &&
            (obj_id < 0 || stack->obj_id == obj_id) )
            return i;
    }
    return -1;
}

struct WorldEntity_ObjStack*
World_ObjStackGetByElementId(
    struct World* world,
    int element_id)
{
    struct World_EntityPool* pool;

    assert(world);
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
        if( stack && stack->element_id == element_id )
            return stack;
    }
    return NULL;
}

void
World_ObjStackDel(
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_ObjStack* stack;

    assert(world);
    pool = &world->entities.obj_stack;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;
    stack = World_EntityPoolGet(pool, idx);
    World_EmitEntityRemoved(world, stack->element_id);
    World_EntityPoolRelease(pool, idx);
}

void
World_ObjStackSetCount(
    struct World* world,
    int idx,
    int count)
{
    struct World_EntityPool* pool;
    struct WorldEntity_ObjStack* stack;

    assert(world);
    pool = &world->entities.obj_stack;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;
    stack = World_EntityPoolGet(pool, idx);
    stack->count = count;
}

/* Client-TS LOC_SHAPE_TO_LAYER (LocShape.ts): shapes 0-3 = WALL, 4-8 =
 * WALL_DECOR, 9-21 = GROUND (scenery/centrepiece), 22 = GROUND_DECOR. A door is
 * a wall (shape 0-3) and lives in the WALL layer; the zone-packet mutations must
 * only touch the loc in that layer, not whatever else shares the tile. Returns
 * -1 for an out-of-range shape (treated as "match any layer"). */
int
World_LocShapeToLayer(int shape)
{
    if( shape < 0 )
        return -1;
    if( shape <= 3 )
        return 0; /* WALL */
    if( shape <= 8 )
        return 1; /* WALL_DECOR */
    if( shape <= 21 )
        return 2; /* GROUND */
    if( shape == 22 )
        return 3; /* GROUND_DECOR */
    return -1;
}

int
World_SceneryFindAt(
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape)
{
    struct World_EntityPool* pool;
    int want_layer = World_LocShapeToLayer(loc_shape);

    assert(world);
    pool = &world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, i);
        if( !scenery )
            continue;
        if( scenery->grid_position.x != scene_x || scenery->grid_position.z != scene_z ||
            scenery->grid_position.level != level )
            continue;
        /* Match the reference locChangeCreate key: (level, x, z, layer). When the
         * caller passes a real shape (>= 0), only the loc in the same layer is a
         * hit — so a door (WALL) never mutates a centrepiece/floor-decor on the
         * same tile. loc_shape < 0 keeps the old "first on tile" behaviour. */
        if( want_layer >= 0 && World_LocShapeToLayer(scenery->shape) != want_layer )
            continue;
        return i;
    }
    return -1;
}

void
World_SceneryRemove(
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Scenery* scenery;

    assert(world);
    pool = &world->entities.scenery;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;
    scenery = World_EntityPoolGet(pool, idx);
    World_EmitEntityRemoved(world, scenery->element_id);
    World_EntityPoolRelease(pool, idx);
}

void
World_NpcAddHitmark(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health)
{
    World_NpcAddHitmarkTimed(
        world,
        idx,
        damage_type,
        damage,
        health,
        total_health,
        0,
        WORLD_ENTITY_DAMAGE_SLOTS,
        WORLD_HITMARK_DEFAULT_DURATION,
        WORLD_HITMARK_POLICY_DISCARD);
}

void
World_NpcAddHitmarkTimed(
    struct World* world,
    int idx,
    int damage_type,
    int damage,
    int health,
    int total_health,
    int delay,
    int slot_limit,
    int duration,
    int slot_policy)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    World_EntityAddHitmark(
        npc->combat.damage_values,
        npc->combat.damage_types,
        npc->combat.damage_start_cycles,
        npc->combat.damage_cycles,
        world->cycle,
        damage_type,
        damage,
        delay,
        slot_limit,
        duration,
        slot_policy);
    npc->combat.health = health;
    npc->combat.total_health = total_health;
    npc->combat.combat_cycle = world->cycle + 400;
}

void
World_NpcSetHealthbar(
    struct World* world,
    int idx,
    struct WorldEntity_Headbar bar)
{
    struct WorldEntity_NPC* npc;

    assert(world);
    assert(bar.type >= 0);
    if( !World_EntityPoolIsActive(&world->entities.npc, idx) )
        return;
    npc = World_EntityPoolGet(&world->entities.npc, idx);
    World_EntityApplyHeadbar(&npc->combat, bar);
}

void
World_NpcClearHealthbar(struct World* world, int idx)
{
    struct WorldEntity_NPC* npc;

    assert(world);
    if( !World_EntityPoolIsActive(&world->entities.npc, idx) )
        return;
    npc = World_EntityPoolGet(&world->entities.npc, idx);
    npc->combat.healthbar_type = -1;
}

/* Reference chatTimer = 150 on every new message (Client.ts:8166); the per-
 * cycle decrement in world_cycle clears the message at 0. */
static void
world_entity_set_chat(
    struct WorldEntityFacet_Chat* chat,
    char const* message,
    int colour,
    int effect)
{
    if( !message || message[0] == '\0' )
    {
        chat->message[0] = '\0';
        chat->timer = 0;
        return;
    }
    strncpy(chat->message, message, sizeof(chat->message) - 1);
    chat->message[sizeof(chat->message) - 1] = '\0';
    chat->colour = colour;
    chat->effect = effect;
    chat->timer = 150;
}

void
World_PlayerSetChat(
    struct World* world,
    int idx,
    char const* message,
    int colour,
    int effect)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    world_entity_set_chat(&player->chat, message, colour, effect);
}

void
World_NpcSetChat(
    struct World* world,
    int idx,
    char const* message,
    int colour,
    int effect)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    world_entity_set_chat(&npc->chat, message, colour, effect);
}
