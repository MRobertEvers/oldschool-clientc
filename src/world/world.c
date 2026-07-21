#include "world.h"

#include "entity_pathing.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_PROJECTILE_ANGLE_TO_RAD 0.02454369
#define WORLD_PROJECTILE_ANGLE_TO_RPI2048 325.949

struct World*
World_New(void)
{
    struct World* world = calloc(1, sizeof(struct World));
    assert(world && "Failed to allocate world");
    World_EntityListInit(&world->entities);
    return world;
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
    World_EntityListFree(&world->entities);
    free(world);
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
    world->scenery_pick_count = 0;
    world->event_count = 0;
    world->_scene_size = scene_size;

    world->heightmap = heightmap_new(scene_size + 1, scene_size + 1, WORLD_MAP_TERRAIN_LEVELS);
    for( int i = 0; i < COLLISION_LEVELS; i++ )
        world->collision_maps[i] = collision_map_new(scene_size, scene_size);
    world->minimap = minimap_new(scene_size, scene_size);

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

static void
World_EmitEntityRemoved(
    struct World* world,
    int element_id)
{
    if( element_id < 0 || world->event_count >= WORLD_MAX_EVENTS )
        return;
    world->events[world->event_count++] = (struct World_Event){
        .kind = WorldEventKind_EntityRemoved,
        .element_id = element_id,
    };
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
                           .z = (uint32_t)(scene_z * 128 + 64) },
        .orientation = { .yaw = 0, .dst_yaw = 0 },
        .pathing = { .route_length = 0,
                     .route_x = { (uint8_t)scene_x },
                     .route_z = { (uint8_t)scene_z } },
        .idle_animations = idle_animations,
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
                           .z = (uint32_t)(scene_z * 128 + size * 64) },
        .orientation = { .yaw = 0, .dst_yaw = 0 },
        .pathing = { .route_length = 0,
                     .route_x = { (uint8_t)scene_x },
                     .route_z = { (uint8_t)scene_z } },
        .npc_id = npc_id,
        .size = size,
        .idle_animations = idle_animations,
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
    int startpos)
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
        .dst_x = dst_x,
        .dst_z = dst_z,
        .cycle = 0,
        .launched = false,
    };

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
        .draw_position = { .x = (uint32_t)scene_x, .z = (uint32_t)scene_z, .y = (uint32_t)y },
        .orientation = { .yaw = (uint16_t)orientation, .dst_yaw = (uint16_t)orientation },
        .idle_cycles = idle_delay,
        .active_cycle = 0,
        .lifetime = lifetime,
        .active = false,
    };
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
    char const* name,
    char const actions[5][32])
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
    if( name )
    {
        strncpy(scenery->name, name, sizeof(scenery->name) - 1);
        scenery->name[sizeof(scenery->name) - 1] = '\0';
    }
    World_CopyMenuActions(scenery->actions, actions);
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
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    player->facing.mode = WORLD_FACING_ENTITY_ID;
    player->facing.u.entity_id = entity_id;
}

void
World_NpcFaceEntity(
    struct World* world,
    int idx,
    int entity_id)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    npc->facing.mode = WORLD_FACING_ENTITY_ID;
    npc->facing.u.entity_id = entity_id;
}

void
World_PlayerFaceCoord(
    struct World* world,
    int idx,
    int x,
    int z)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.player;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_Player* player = World_EntityPoolGet(pool, idx);
    player->facing.mode = WORLD_FACING_GRID_COORDS;
    player->facing.u.grid_coords.x = (uint16_t)x;
    player->facing.u.grid_coords.z = (uint16_t)z;
}

void
World_NpcFaceCoord(
    struct World* world,
    int idx,
    int x,
    int z)
{
    assert(world);
    struct World_EntityPool* pool = &world->entities.npc;
    assert(World_EntityPoolIsActive(pool, idx));
    struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, idx);
    npc->facing.mode = WORLD_FACING_GRID_COORDS;
    npc->facing.u.grid_coords.x = (uint16_t)x;
    npc->facing.u.grid_coords.z = (uint16_t)z;
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
    if( !world )
        return 0;
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
