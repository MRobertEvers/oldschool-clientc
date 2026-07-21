#include "world.h"

#include "entity_facets.h"

#include <assert.h>

struct World_MoverInfo
{
    struct WorldEntityFacet_Pathing* pathing;
    struct WorldEntityFacet_DrawPosition* draw_position;
    struct WorldEntityFacet_GridPosition* grid_position;
    struct WorldEntityFacet_Orientation* orientation;
    struct WorldEntityFacet_IdleAnimations* idle;
    struct WorldEntityFacet_Animation* animation;
    int size_x;
    int size_z;
};

/**
 * Advances pathing draw position one tick and returns the secondary sequence id
 * to use (-1 means clear secondary).
 */
static int
World_UpdateMoverMovementAndAnimation(struct World_MoverInfo* info)
{
    int seqId = info->idle->readyanim;
    int route_length = info->pathing->route_length;
    if( route_length == 0 )
        goto yaw_turn;

    int x = (int)info->draw_position->x;
    int z = (int)info->draw_position->z;
    int dstX = info->pathing->route_x[route_length - 1] * 128 + info->size_x * 64;
    int dstZ = info->pathing->route_z[route_length - 1] * 128 + info->size_z * 64;

    if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        info->draw_position->x = (uint32_t)dstX;
        info->draw_position->z = (uint32_t)dstZ;
        info->grid_position->x = info->pathing->route_x[route_length - 1];
        info->grid_position->z = info->pathing->route_z[route_length - 1];
        return -1;
    }

    if( x < dstX )
    {
        if( z < dstZ )
            info->orientation->dst_yaw = 1280;
        else if( z > dstZ )
            info->orientation->dst_yaw = 1792;
        else
            info->orientation->dst_yaw = 1536;
    }
    else if( x > dstX )
    {
        if( z < dstZ )
            info->orientation->dst_yaw = 768;
        else if( z > dstZ )
            info->orientation->dst_yaw = 256;
        else
            info->orientation->dst_yaw = 512;
    }
    else if( z < dstZ )
        info->orientation->dst_yaw = 1024;
    else
        info->orientation->dst_yaw = 0;

    int deltaYaw = (info->orientation->dst_yaw - info->orientation->yaw) & 0x7ff;
    if( deltaYaw > 1024 )
        deltaYaw -= 2048;

    seqId = info->idle->walkanim_b;
    if( deltaYaw >= -256 && deltaYaw <= 256 )
        seqId = info->idle->walkanim;
    else if( deltaYaw >= 256 && deltaYaw < 768 )
        seqId = info->idle->walkanim_r;
    else if( deltaYaw >= -768 && deltaYaw <= -256 )
        seqId = info->idle->walkanim_l;

    if( seqId == -1 )
        seqId = info->idle->walkanim;

    int moveSpeed = 4;
    if( info->orientation->yaw != info->orientation->dst_yaw )
        moveSpeed = 2;
    if( route_length > 2 )
        moveSpeed = 6;
    if( route_length > 3 )
        moveSpeed = 8;

    if( !info->pathing->route_run[route_length - 1] && moveSpeed > 4 )
        moveSpeed = 4;
    if( info->pathing->route_run[route_length - 1] )
        moveSpeed <<= 1;

    if( info->pathing->route_run[route_length - 1] && moveSpeed >= 8 &&
        seqId == info->idle->walkanim && info->idle->runanim != -1 )
        seqId = info->idle->runanim;

    if( x < dstX )
    {
        info->draw_position->x += (uint32_t)moveSpeed;
        if( (int)info->draw_position->x > dstX )
            info->draw_position->x = (uint32_t)dstX;
    }
    else if( x > dstX )
    {
        info->draw_position->x -= (uint32_t)moveSpeed;
        if( (int)info->draw_position->x < dstX )
            info->draw_position->x = (uint32_t)dstX;
    }
    if( z < dstZ )
    {
        info->draw_position->z += (uint32_t)moveSpeed;
        if( (int)info->draw_position->z > dstZ )
            info->draw_position->z = (uint32_t)dstZ;
    }
    else if( z > dstZ )
    {
        info->draw_position->z -= (uint32_t)moveSpeed;
        if( (int)info->draw_position->z < dstZ )
            info->draw_position->z = (uint32_t)dstZ;
    }

    if( (int)info->draw_position->x == dstX && (int)info->draw_position->z == dstZ )
    {
        info->pathing->route_length--;
        if( info->pathing->route_length < 0 )
            info->pathing->route_length = 0;
        info->grid_position->x = info->pathing->route_x[0];
        info->grid_position->z = info->pathing->route_z[0];
    }

yaw_turn:;
    int remainingYaw = (info->orientation->dst_yaw - info->orientation->yaw) & 0x7ff;
    if( remainingYaw != 0 )
    {
        if( remainingYaw < 32 || remainingYaw > 2016 )
            info->orientation->yaw = info->orientation->dst_yaw;
        else if( remainingYaw > 1024 )
            info->orientation->yaw -= 32;
        else
            info->orientation->yaw += 32;
        info->orientation->yaw &= 0x7ff;

        if( seqId == info->idle->readyanim &&
            info->orientation->yaw != info->orientation->dst_yaw )
        {
            if( info->idle->turnanim != -1 )
                seqId = info->idle->turnanim;
            else
                seqId = info->idle->walkanim;
        }
    }

    return seqId;
}

static void
World_ApplySecondaryAnim(
    struct WorldEntityFacet_Animation* animation,
    int seqId)
{
    if( seqId == -1 )
    {
        if( animation->secondary.anim_id != (uint16_t)-1 && animation->secondary.anim_id != 0 )
        {
            animation->secondary.anim_id = (uint16_t)-1;
            animation->secondary.frame = 0;
            animation->secondary.cycle = 0;
        }
        return;
    }

    if( animation->secondary.anim_id != (uint16_t)seqId )
    {
        animation->secondary.anim_id = (uint16_t)seqId;
        animation->secondary.frame = 0;
        animation->secondary.cycle = 0;
    }
}

static void
World_CycleUpdatePlayers(
    struct World* world,
    int cycles_elapsed)
{
    struct World_EntityPool* pool = &world->entities.player;
    for( int cycle = 0; cycle < cycles_elapsed; cycle++ )
    {
        for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
             pi = World_EntityPoolNext(pool, pi) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
            if( !player || player->element_id < 0 )
                continue;

            struct World_MoverInfo info = {
                .pathing = &player->pathing,
                .draw_position = &player->draw_position,
                .grid_position = &player->grid_position,
                .orientation = &player->orientation,
                .idle = &player->idle_animations,
                .animation = &player->animation,
                .size_x = 1,
                .size_z = 1,
            };
            int seqId = World_UpdateMoverMovementAndAnimation(&info);
            World_ApplySecondaryAnim(&player->animation, seqId);
        }
    }
}

static void
World_CycleUpdateNpcs(
    struct World* world,
    int cycles_elapsed)
{
    struct World_EntityPool* pool = &world->entities.npc;
    for( int cycle = 0; cycle < cycles_elapsed; cycle++ )
    {
        for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
             ni = World_EntityPoolNext(pool, ni) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
            if( !npc || npc->element_id < 0 )
                continue;

            int size = npc->size > 0 ? npc->size : 1;
            struct World_MoverInfo info = {
                .pathing = &npc->pathing,
                .draw_position = &npc->draw_position,
                .grid_position = &npc->grid_position,
                .orientation = &npc->orientation,
                .idle = &npc->idle_animations,
                .animation = &npc->animation,
                .size_x = size,
                .size_z = size,
            };
            int seqId = World_UpdateMoverMovementAndAnimation(&info);
            World_ApplySecondaryAnim(&npc->animation, seqId);
        }
    }
}

static void
World_CycleUpdateProjectiles(
    struct World* world,
    int cycles_elapsed)
{
    struct World_EntityPool* pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; )
    {
        int next = World_EntityPoolNext(pool, i);
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);

        if( cycles_elapsed > 0 )
            p->cycle += cycles_elapsed;

        if( p->cycle > p->t2 )
        {
            World_ProjectileDespawn(world, i);
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
            World_ProjectileSetTarget(world, p, p->cycle);
            World_ProjectileMove(p, cycles_elapsed);
        }

        int grid_x = (int)p->x >> 7;
        int grid_z = (int)p->z >> 7;
        if( world->_scene_size > 0 &&
            (grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
             grid_z >= world->_scene_size) )
        {
            World_ProjectileDespawn(world, i);
            i = next;
            continue;
        }

        i = next;
    }
}

static void
World_CycleUpdateSpotanims(
    struct World* world,
    int cycles_elapsed)
{
    struct World_EntityPool* pool = &world->entities.spotanim;
    for( int si = World_EntityPoolHead(pool); si != WORLD_ENTITY_NIL; )
    {
        int next = World_EntityPoolNext(pool, si);
        struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, si);

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
            World_SpotanimDespawn(world, si);
            si = next;
            continue;
        }

        int grid_x = (int)(s->draw_position.x >> 7);
        int grid_z = (int)(s->draw_position.z >> 7);
        if( world->_scene_size > 0 &&
            (grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
             grid_z >= world->_scene_size) )
        {
            World_SpotanimDespawn(world, si);
            si = next;
            continue;
        }

        si = next;
    }
}

#define WORLD_PROJECTILE_PAINTER_PADDING 60

struct World_PainterFootprint
{
    int sx;
    int sz;
    int size_x;
    int size_z;
};

/* A projectile draws between tiles, so it registers over the tile span its
 * padded position covers rather than a single grid cell (v1 parity). */
static void
World_ProjectilePainterFootprint(
    int pos_x,
    int pos_z,
    int draw_padding,
    int scene_size,
    struct World_PainterFootprint* out)
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

/* Dynamic entities re-register with the painter every cycle on top of the
 * static terrain/scenery set; without this pass they never produce
 * PNTR_CMD_ELEMENT commands and never draw (v1 world_cycle). */
static void
World_CycleRegisterPainterDynamics(struct World* world)
{
    struct World_EntityPool* pool;

    painter_reset_to_static(world->painter);

    pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
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

    pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( !npc || npc->element_id < 0 )
            continue;
        int size = npc->size > 0 ? npc->size : 1;
        int grid_x = npc->grid_position.x;
        int grid_z = npc->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        painter_add_normal_scenery(
            world->painter, grid_x, grid_z, npc->grid_position.level, npc->element_id, size, size);
    }

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        if( !p || p->element_id < 0 || p->cycle < p->t1 )
            continue;
        struct World_PainterFootprint footprint;
        World_ProjectilePainterFootprint(
            (int)p->x, (int)p->z, WORLD_PROJECTILE_PAINTER_PADDING, world->_scene_size, &footprint);
        painter_add_normal_scenery(
            world->painter,
            footprint.sx,
            footprint.sz,
            p->level,
            p->element_id,
            footprint.size_x,
            footprint.size_z);
    }

    pool = &world->entities.spotanim;
    for( int si = World_EntityPoolHead(pool); si != WORLD_ENTITY_NIL;
         si = World_EntityPoolNext(pool, si) )
    {
        struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, si);
        if( !s || s->element_id < 0 || !s->active )
            continue;
        int grid_x = (int)(s->draw_position.x >> 7);
        int grid_z = (int)(s->draw_position.z >> 7);
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        painter_add_normal_scenery(world->painter, grid_x, grid_z, s->level, s->element_id, 1, 1);
    }
}

void
World_Cycle(
    struct World* world,
    int cycles_elapsed)
{
    assert(world);
    if( !world->load_complete )
        return;

    World_CycleUpdatePlayers(world, cycles_elapsed);
    World_CycleUpdateNpcs(world, cycles_elapsed);
    World_CycleUpdateProjectiles(world, cycles_elapsed);
    World_CycleUpdateSpotanims(world, cycles_elapsed);

    if( world->painter )
        World_CycleRegisterPainterDynamics(world);
}
