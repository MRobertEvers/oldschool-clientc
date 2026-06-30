#include "world.h"

#include "collision_map.h"
#include "heightmap.h"
#include "minimap.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct World*
world_new(void)
{
    struct World* world = calloc(1, sizeof(struct World));
    assert(world && "Failed to allocate world");
    World_EntityListInit(&world->entities);
    return world;
}

void
world_free(struct World* world)
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
    World_EntityListFree(&world->entities);
    free(world);
}

static void
world_reset_scene_alloc(
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
    world_terrain_reset(world);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
    {
        if( world->collision_maps[i] )
            collision_map_free(world->collision_maps[i]);
        world->collision_maps[i] = NULL;
    }

    world->_scene_size = scene_size;

    world->heightmap = heightmap_new(scene_size + 1, scene_size + 1, WORLD_MAP_TERRAIN_LEVELS);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
        world->collision_maps[i] = collision_map_new(scene_size, scene_size);
    world->minimap = minimap_new(scene_size, scene_size);
    world->scenery_pick_count = 0;

    world->painter = painter_new(
        scene_size,
        scene_size,
        WORLD_MAP_TERRAIN_LEVELS,
        PAINTER_NEW_CTX_BUCKET | PAINTER_NEW_CTX_WORLD3D);
    world->cullmap = painters_cullmap_new_nocull();
    if( world->painter )
        painter_set_cullmap(world->painter, world->cullmap);

    int terrain_tile_count = scene_size * scene_size * WORLD_MAP_TERRAIN_LEVELS;
    World_EntityPoolReserve(&world->entities.terrain, terrain_tile_count);
}

void
world_terrain_reset(struct World* world)
{
    if( !world )
        return;
    World_EntityPoolReset(&world->entities.terrain);
}

void
world_terrain_set(
    struct World* world,
    int element_id,
    int x,
    int z,
    int level)
{
    if( !world )
        return;

    int idx = world_terrain_tile_idx(world, x, z, level);
    struct World_EntityPool* pool = &world->entities.terrain;
    if( !World_EntityPoolEnsureSlot(pool, idx) )
        return;

    struct WorldEntity_Terrain* terrain = World_EntityPoolGet(pool, idx);
    if( !terrain )
        return;

    terrain->element_id = element_id;
    terrain->grid_position.level = level;
    terrain->grid_position.x = x;
    terrain->grid_position.z = z;
}

void
world_reset_scene(
    struct World* world,
    int zone_center_x,
    int zone_center_z,
    int scene_size)
{
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

    world_reset_scene_alloc(world, scene_size);
}

void
world_reset_scene_chunklist(
    struct World* world,
    const int* chunks_xz,
    int count)
{
    assert(world && chunks_xz && count > 0);

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

    world_reset_scene_alloc(world, scene_size);
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

int
world_terrain_element_at(
    struct World* world,
    int x,
    int z,
    int level)
{
    if( !world )
        return -1;
    if( x < 0 || z < 0 || level < 0 )
        return -1;
    if( x >= world->_scene_size || z >= world->_scene_size || level >= WORLD_MAP_TERRAIN_LEVELS )
        return -1;

    int idx = world_terrain_tile_idx(world, x, z, level);
    struct World_EntityPool* pool = &world->entities.terrain;
    if( idx < 0 || idx >= pool->count || !World_EntityPoolIsActive(pool, idx) )
        return -1;

    struct WorldEntity_Terrain* terrain = World_EntityPoolGet(pool, idx);
    if( !terrain )
        return -1;
    return terrain->element_id;
}

#define WORLD_PROJECTILE_ANGLE_TO_RAD 0.02454369
#define WORLD_PROJECTILE_ANGLE_TO_RPI2048 325.949

static int
world_projectile_dst_y(
    struct World* world,
    const struct WorldEntity_Projectile* p)
{
    if( !world || !world->heightmap )
        return 0;
    return heightmap_get_interpolated(world->heightmap, p->dst_x, p->dst_z, p->dst_level) -
           p->end_height;
}

static void
world_projectile_settarget(
    struct WorldEntity_Projectile* p,
    struct World* world,
    int cycle)
{
    int const dst_y = world_projectile_dst_y(world, p);
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

static void
world_projectile_move(
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

    p->orientation.yaw = ((int)(atan2(p->vx, p->vz) * WORLD_PROJECTILE_ANGLE_TO_RPI2048 + 1024.0)) & 0x7ff;
    p->orientation.pitch = ((int)(atan2(p->vy, p->velocity) * WORLD_PROJECTILE_ANGLE_TO_RPI2048)) & 0x7ff;
}

int
world_player_spawn(
    struct World* world,
    int element_id,
    int level,
    int scene_x,
    int scene_z,
    struct WorldEntityFacet_IdleAnimations idle_animations)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Player* player;
    int idx;

    assert(!(!world || element_id < 0));

    pool = &world->entities.player;
    idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    player = World_EntityPoolGet(pool, idx);
    *player = (struct WorldEntity_Player){
        .element_id = element_id,
        .grid_position = { .x = scene_x, .z = scene_z, .level = level },
        .draw_position = { .x = (uint32_t)(scene_x * 128 + 64), .z = (uint32_t)(scene_z * 128 + 64) },
        .orientation = { .yaw = 0 },
        .idle_animations = idle_animations,
        .animation = { 0 },
    };
    return idx;
}

void
world_player_despawn(
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Player* player;

    assert(world);

    pool = &world->entities.player;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    player = World_EntityPoolGet(pool, idx);
    if( !player )
        return;

    if( player->element_id >= 0 && world->event_count < WORLD_MAX_EVENTS )
    {
        world->events[world->event_count++] = (struct WorldEvent){
            .kind = WORLD_EVENT_ENTITY_REMOVED,
            .element_id = player->element_id,
        };
    }

    World_EntityPoolRelease(pool, idx);
}

int
world_npc_spawn(
    struct World* world,
    int element_id,
    int npc_id,
    int level,
    int scene_x,
    int scene_z,
    int size,
    struct WorldEntityFacet_IdleAnimations idle_animations)
{
    struct World_EntityPool* pool;
    struct WorldEntity_NPC* npc;
    int idx;

    assert(!(!world || element_id < 0));

    pool = &world->entities.npc;
    idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    npc = World_EntityPoolGet(pool, idx);
    *npc = (struct WorldEntity_NPC){
        .element_id = element_id,
        .grid_position = { .x = scene_x, .z = scene_z, .level = level },
        .draw_position = { .x = (uint32_t)(scene_x * 128 + 64), .z = (uint32_t)(scene_z * 128 + 64) },
        .orientation = { .yaw = 0 },
        .npc_id = npc_id,
        .size = size,
        .idle_animations = idle_animations,
        .animation = { 0 },
    };
    return idx;
}

void
world_npc_despawn(
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_NPC* npc;

    assert(world);

    pool = &world->entities.npc;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    npc = World_EntityPoolGet(pool, idx);
    if( !npc )
        return;

    if( npc->element_id >= 0 && world->event_count < WORLD_MAX_EVENTS )
    {
        world->events[world->event_count++] = (struct WorldEvent){
            .kind = WORLD_EVENT_ENTITY_REMOVED,
            .element_id = npc->element_id,
        };
    }

    World_EntityPoolRelease(pool, idx);
}

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
    int startpos)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Projectile* p;
    int idx;

    assert(!(!world || element_id < 0));

    pool = &world->entities.projectile;
    idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    p = World_EntityPoolGet(pool, idx);
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
        .dst_x = dst_x,
        .dst_z = dst_z,
        .cycle = 0,
        .launched = false,
    };

    world_projectile_settarget(p, world, t1);
    return idx;
}

#define WORLD_PROJECTILE_PAINTER_PADDING 60

struct WorldPainterFootprint
{
    int sx;
    int sz;
    int size_x;
    int size_z;
};

static void
world_projectile_painter_footprint(
    int pos_x,
    int pos_z,
    int draw_padding,
    int scene_size,
    struct WorldPainterFootprint* out)
{
    int x0 = (pos_x - draw_padding) / 128;
    int z0 = (pos_z - draw_padding) / 128;
    int x1 = (pos_x + draw_padding) / 128;
    int z1 = (pos_z + draw_padding) / 128;

    if( x0 < 0 )
        x0 = 0;
    if( z0 < 0 )
        z0 = 0;
    if( x1 >= scene_size )
        x1 = scene_size - 1;
    if( z1 >= scene_size )
        z1 = scene_size - 1;

    out->sx = x0;
    out->sz = z0;
    out->size_x = x1 - x0 + 1;
    out->size_z = z1 - z0 + 1;
}

void
world_projectile_despawn(
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Projectile* p;

    if( !world )
        return;

    pool = &world->entities.projectile;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    p = World_EntityPoolGet(pool, idx);
    if( !p )
        return;

    if( p->element_id >= 0 && world->event_count < WORLD_MAX_EVENTS )
    {
        world->events[world->event_count++] = (struct WorldEvent){
            .kind = WORLD_EVENT_ENTITY_REMOVED,
            .element_id = p->element_id,
        };
    }

    World_EntityPoolRelease(pool, idx);
}

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
    int lifetime)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Spotanim* s;
    int idx;

    assert(!(!world || element_id < 0));

    pool = &world->entities.spotanim;
    idx = World_EntityPoolAlloc(pool);
    assert(idx >= 0);

    s = World_EntityPoolGet(pool, idx);
    *s = (struct WorldEntity_Spotanim){
        .element_id = element_id,
        .level = level,
        .draw_position = { .x = (uint32_t)scene_x, .z = (uint32_t)scene_z, .y = (uint32_t)y },
        .orientation = { .yaw = (uint16_t)orientation },
        .idle_cycles = idle_delay,
        .active_cycle = 0,
        .lifetime = lifetime,
        .active = false,
    };
    return idx;
}

void
world_spotanim_despawn(
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_Spotanim* s;

    if( !world )
        return;

    pool = &world->entities.spotanim;
    if( !World_EntityPoolIsActive(pool, idx) )
        return;

    s = World_EntityPoolGet(pool, idx);
    if( !s )
        return;

    if( s->element_id >= 0 && world->event_count < WORLD_MAX_EVENTS )
    {
        world->events[world->event_count++] = (struct WorldEvent){
            .kind = WORLD_EVENT_ENTITY_REMOVED,
            .element_id = s->element_id,
        };
    }

    World_EntityPoolRelease(pool, idx);
}

int
world_events_count(struct World* world)
{
    if( !world )
        return 0;
    return world->event_count;
}

const struct WorldEvent*
world_events_peek(
    struct World* world,
    int i)
{
    if( !world || i < 0 || i >= world->event_count )
        return NULL;
    return &world->events[i];
}

void
world_events_clear(struct World* world)
{
    if( !world )
        return;
    world->event_count = 0;
}

void
world_cycle(
    struct World* world,
    int cycles_elapsed)
{
    if( !world || !world->painter || !world->load_complete )
        return;

    painter_reset_to_static(world->painter);

    struct World_EntityPool* player_pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(player_pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(player_pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(player_pool, pi);
        if( !player || player->element_id < 0 )
            continue;

        int grid_x = player->grid_position.x;
        int grid_z = player->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;

        painter_add_normal_scenery(
            world->painter, grid_x, grid_z, player->grid_position.level, player->element_id, 1, 1);
    }

    struct World_EntityPool* npc_pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(npc_pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(npc_pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(npc_pool, ni);
        if( !npc || npc->element_id < 0 )
            continue;

        int grid_x = npc->grid_position.x;
        int grid_z = npc->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;

        painter_add_normal_scenery(
            world->painter,
            grid_x,
            grid_z,
            npc->grid_position.level,
            npc->element_id,
            npc->size,
            npc->size);
    }

    struct World_EntityPool* pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; )
    {
        int next = World_EntityPoolNext(pool, i);
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);

        if( cycles_elapsed > 0 )
            p->cycle += cycles_elapsed;

        if( p->cycle > p->t2 )
        {
            world_projectile_despawn(world, i);
            i = next;
            continue;
        }

        if( p->cycle < p->t1 )
        {
            i = next;
            continue;
        }

        if( cycles_elapsed > 0 )
        {
            world_projectile_settarget(p, world, p->cycle);
            world_projectile_move(p, cycles_elapsed);
        }

        int pos_x = (int)p->x;
        int pos_z = (int)p->z;
        int grid_x = pos_x >> 7;
        int grid_z = pos_z >> 7;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
        {
            world_projectile_despawn(world, i);
            i = next;
            continue;
        }

        struct WorldPainterFootprint footprint;
        world_projectile_painter_footprint(
            pos_x, pos_z, WORLD_PROJECTILE_PAINTER_PADDING, world->_scene_size, &footprint);

        painter_add_normal_scenery(
            world->painter,
            footprint.sx,
            footprint.sz,
            p->level,
            p->element_id,
            footprint.size_x,
            footprint.size_z);

        i = next;
    }

    struct World_EntityPool* spotanim_pool = &world->entities.spotanim;
    for( int si = World_EntityPoolHead(spotanim_pool); si != WORLD_ENTITY_NIL; )
    {
        int next = World_EntityPoolNext(spotanim_pool, si);
        struct WorldEntity_Spotanim* s = World_EntityPoolGet(spotanim_pool, si);

        if( !s->active )
        {
            if( cycles_elapsed > 0 )
            {
                s->idle_cycles -= cycles_elapsed;
                if( s->idle_cycles <= 0 )
                    s->active = true;
            }
            if( !s->active )
            {
                si = next;
                continue;
            }
        }

        if( cycles_elapsed > 0 )
            s->active_cycle += cycles_elapsed;

        if( s->active_cycle >= s->lifetime )
        {
            world_spotanim_despawn(world, si);
            si = next;
            continue;
        }

        int grid_x = (int)(s->draw_position.x >> 7);
        int grid_z = (int)(s->draw_position.z >> 7);
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
        {
            world_spotanim_despawn(world, si);
            si = next;
            continue;
        }

        painter_add_normal_scenery(world->painter, grid_x, grid_z, s->level, s->element_id, 1, 1);

        si = next;
    }
}

void
world_clear_scenery_picks(struct World* world)
{
    if( world )
        world->scenery_pick_count = 0;
}

void
world_register_scenery_pick(
    struct World* world,
    int element_id,
    int loc_id)
{
    if( !world || element_id < 0 || loc_id < 0 )
        return;
    if( world->scenery_pick_count >= WORLD_SCENERY_PICK_MAX )
        return;

    struct WorldSceneryPick* pick = &world->scenery_picks[world->scenery_pick_count++];
    pick->element_id = element_id;
    pick->loc_id = loc_id;
}
