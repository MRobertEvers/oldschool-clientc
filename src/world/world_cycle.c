#include "world.h"

#include "entity_facets.h"

#include <assert.h>
#include <math.h>

/* Client-TS `(Math.atan2(dstX, dstZ) * 325.949) | 0` — radians to the
 * 2048-step yaw unit, truncated toward zero like the JS `| 0`. */
#define WORLD_YAW_FROM_RADIANS 325.949

struct World_MoverInfo
{
    struct WorldEntityFacet_Pathing* pathing;
    struct WorldEntityFacet_DrawPosition* draw_position;
    struct WorldEntityFacet_GridPosition* grid_position;
    struct WorldEntityFacet_Orientation* orientation;
    struct WorldEntityFacet_IdleAnimations* idle;
    struct WorldEntityFacet_Animation* animation;
    struct WorldEntityFacet_Facing* facing;
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
    /* The turning slow-down only applies to an entity that is actually free
     * to turn: a locked-on target or turnspeed 0 keeps full speed
     * (Client-TS routeMove, `e.faceEntity === -1 && e.turnspeed !== 0`). */
    if( info->orientation->yaw != info->orientation->dst_yaw &&
        info->facing->entity_id == WORLD_FACING_ENTITY_NONE && info->facing->turn_speed != 0 )
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
    return seqId;
}

/* Reference entityFace (Client.ts:3932), run once per entity per cycle right
 * after the movement step and before entityAnim.
 *
 * Three facing sources, applied in order, each only overwriting dst_yaw:
 *   1. faceEntity < 32768 -> npc server slot
 *   2. faceEntity >= 32768 -> player server slot (+ 32768)
 *   3. a pending face-square, but only while the entity is standing still
 *      (route empty) — consumed and cleared either way
 * then the yaw is stepped toward dst_yaw by turn_speed, and an entity that is
 * idle-animating while still mid-turn swaps to its turn/walk seq.
 *
 * Returns the secondary seq id to apply, or -1 to leave it alone. */
static int
World_EntityFace(
    struct World* world,
    struct WorldEntityFacet_Facing* facing,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_Orientation* orientation,
    struct WorldEntityFacet_Pathing const* pathing,
    struct WorldEntityFacet_IdleAnimations const* idle,
    struct WorldEntityFacet_Animation const* animation)
{
    int dst_x;
    int dst_z;

    if( facing->turn_speed == 0 )
        return -1;

    if( facing->entity_id != WORLD_FACING_ENTITY_NONE )
    {
        struct WorldEntityFacet_DrawPosition const* target = NULL;
        if( facing->entity_id < WORLD_FACING_PLAYER_BASE )
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByServerSlot(world, facing->entity_id);
            if( npc )
                target = &npc->draw_position;
        }
        else
        {
            struct WorldEntity_Player* player = World_PlayerGetByServerPid(
                world, facing->entity_id - WORLD_FACING_PLAYER_BASE);
            if( player )
                target = &player->draw_position;
        }
        if( target )
        {
            dst_x = (int)draw_position->x - (int)target->x;
            dst_z = (int)draw_position->z - (int)target->z;
            if( dst_x != 0 || dst_z != 0 )
                orientation->dst_yaw =
                    (uint16_t)(((int)(atan2((double)dst_x, (double)dst_z) *
                                      WORLD_YAW_FROM_RADIANS)) &
                               0x7ff);
        }
    }

    /* faceSquare is in absolute half-tiles; `- base - base` converts the
     * doubled coordinate straight into fine units at 64 per half-tile.
     *
     * The reference also lets the square apply while `animDelayMove > 0` (a
     * seq with PreanimMove/PostanimMove DELAYMOVE holding the route still).
     * torirs does not model DELAYMOVE at all — routeMove never early-returns
     * on it — so that counter would be permanently 0 and the clause is left
     * out rather than faked with a lookalike field. */
    if( (facing->square_x != 0 || facing->square_z != 0) && pathing->route_length == 0 )
    {
        dst_x = (int)draw_position->x -
                (facing->square_x - world->_base_tile_x - world->_base_tile_x) * 64;
        dst_z = (int)draw_position->z -
                (facing->square_z - world->_base_tile_z - world->_base_tile_z) * 64;
        if( dst_x != 0 || dst_z != 0 )
            orientation->dst_yaw =
                (uint16_t)(((int)(atan2((double)dst_x, (double)dst_z) * WORLD_YAW_FROM_RADIANS)) &
                           0x7ff);
        facing->square_x = 0;
        facing->square_z = 0;
    }

    {
        int remaining_yaw = (orientation->dst_yaw - orientation->yaw) & 0x7ff;
        if( remaining_yaw == 0 )
            return -1;

        if( remaining_yaw < facing->turn_speed || remaining_yaw > 2048 - facing->turn_speed )
            orientation->yaw = orientation->dst_yaw;
        else if( remaining_yaw > 1024 )
            orientation->yaw = (uint16_t)(orientation->yaw - facing->turn_speed);
        else
            orientation->yaw = (uint16_t)(orientation->yaw + facing->turn_speed);
        orientation->yaw &= 0x7ff;

        if( animation->secondary.anim_id == (uint16_t)idle->readyanim &&
            orientation->yaw != orientation->dst_yaw )
            return idle->turnanim != -1 ? idle->turnanim : idle->walkanim;
    }
    return -1;
}

/* Seq-source getters with defaults for a NULL source / unknown seq. */
static int
cycle_seq_frame_count(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.frame_count )
        return world->seq_source.frame_count(world->seq_source.userdata, seq_id);
    return 0;
}

static int
cycle_seq_frame_duration(
    struct World* world,
    int seq_id,
    int frame)
{
    int duration = 1;
    if( world->seq_source.frame_duration )
        duration = world->seq_source.frame_duration(world->seq_source.userdata, seq_id, frame);
    return duration > 0 ? duration : 1;
}

static int
cycle_seq_frame_step(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.frame_step )
        return world->seq_source.frame_step(world->seq_source.userdata, seq_id);
    return 0;
}

static int
cycle_seq_max_loops(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.max_loops )
        return world->seq_source.max_loops(world->seq_source.userdata, seq_id);
    return 99;
}

static int
cycle_seq_preanim_move(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.preanim_move )
        return world->seq_source.preanim_move(world->seq_source.userdata, seq_id);
    return 0;
}

static int
anim_step_active(struct WorldEntityFacet_AnimationStep const* step)
{
    return step->anim_id != (uint16_t)-1 && step->anim_id != 0;
}

/* One client cycle of frame stepping for an entity's animation tracks +
 * attached graphic (exact port of Client.ts entityAnim, 4000-4075). */
static void
World_StepEntityAnimation(
    struct World* world,
    struct WorldEntityFacet_Animation* anim,
    struct WorldEntityFacet_EntitySpotanim* spot,
    struct WorldEntityFacet_Pathing const* pathing)
{
    /* Secondary (idle/walk) loops forever. */
    if( anim_step_active(&anim->secondary) )
    {
        int seq = anim->secondary.anim_id;
        int count = cycle_seq_frame_count(world, seq);
        if( count > 0 )
        {
            anim->secondary.cycle++;
            if( anim->secondary.frame < count &&
                anim->secondary.cycle > cycle_seq_frame_duration(world, seq, anim->secondary.frame) )
            {
                anim->secondary.cycle = 0;
                anim->secondary.frame++;
            }
            if( anim->secondary.frame >= count )
            {
                anim->secondary.frame = 0;
                anim->secondary.cycle = 0;
            }
        }
    }

    /* Entity graphic. Spotanim configs are not decoded yet, so the seq-source
     * cannot time the frames; hold the state and expire it after a generous
     * fixed window (visualization is a flagged follow-on). */
    if( spot && spot->id != -1 && world->cycle >= spot->last_cycle )
    {
        if( spot->frame < 0 )
            spot->frame = 0;
        spot->cycle++;
        if( world->cycle - spot->last_cycle > 200 )
            spot->id = -1;
    }

    /* Primary (transient/action). */
    if( anim_step_active(&anim->primary) )
    {
        int seq = anim->primary.anim_id;

        /* PreanimMove.DELAYANIM: pause the action while route movement is
         * pending (Client.ts 4039-4045). */
        if( anim->primary.delay <= 1 && pathing && pathing->route_length > 0 &&
            anim->preanim_route_length > 0 && cycle_seq_preanim_move(world, seq) == 1 )
        {
            anim->primary.delay = 1;
            return;
        }

        if( anim->primary.delay == 0 )
        {
            int count = cycle_seq_frame_count(world, seq);
            if( count > 0 )
            {
                anim->primary.cycle++;
                while( anim->primary.frame < count &&
                       anim->primary.cycle >
                           cycle_seq_frame_duration(world, seq, anim->primary.frame) )
                {
                    anim->primary.cycle = (uint16_t)(
                        anim->primary.cycle -
                        cycle_seq_frame_duration(world, seq, anim->primary.frame));
                    anim->primary.frame++;

                    if( anim->primary.frame >= count )
                    {
                        int stepped = (int)anim->primary.frame - cycle_seq_frame_step(world, seq);
                        anim->primary.loop++;
                        if( anim->primary.loop >= cycle_seq_max_loops(world, seq) ||
                            stepped < 0 || stepped >= count )
                        {
                            anim->primary.anim_id = (uint16_t)-1;
                            anim->primary.frame = 0;
                            anim->primary.cycle = 0;
                            anim->primary.loop = 0;
                            break;
                        }
                        anim->primary.frame = (uint16_t)stepped;
                    }
                }
            }
        }
        if( anim->primary.delay > 0 )
            anim->primary.delay--;
    }
}

/* Reference exactMove1/exactMove2 (Client.ts 3757-3803): server-forced
 * interpolation overriding route movement while the window is active.
 * Returns 1 while active (route movement must be skipped). */
static int
World_UpdateExactMove(
    struct World* world,
    struct WorldEntityFacet_ExactMove* exact,
    struct WorldEntityFacet_DrawPosition* draw_position,
    struct WorldEntityFacet_Orientation* orientation,
    struct WorldEntityFacet_Animation const* anim,
    int size)
{
    static const uint16_t k_facing_yaw[4] = { 1024, 1536, 0, 512 };

    if( exact->move_end == 0 && exact->move_start == 0 )
        return 0;
    if( exact->move_start < world->cycle )
    {
        /* Window passed. */
        exact->move_end = 0;
        exact->move_start = 0;
        return 0;
    }

    if( exact->move_end > world->cycle )
    {
        /* Phase 1: glide toward the START tile. */
        int delta = exact->move_end - world->cycle;
        int dst_x = exact->start_x * 128 + size * 64;
        int dst_z = exact->start_z * 128 + size * 64;
        draw_position->x = (uint32_t)((int)draw_position->x + (dst_x - (int)draw_position->x) / delta);
        draw_position->z = (uint32_t)((int)draw_position->z + (dst_z - (int)draw_position->z) / delta);
        orientation->dst_yaw = k_facing_yaw[exact->facing & 3];
    }
    else
    {
        /* Phase 2: interpolate start -> end over the window, gated on the
         * primary anim frame boundary (Client.ts exactMove2). */
        int on_boundary = exact->move_start == world->cycle ||
                          !anim_step_active(&anim->primary) || anim->primary.delay != 0 ||
                          anim->primary.cycle + 1 >
                              cycle_seq_frame_duration(
                                  world, anim->primary.anim_id, anim->primary.frame);
        if( on_boundary )
        {
            int duration = exact->move_start - exact->move_end;
            int delta = world->cycle - exact->move_end;
            int dx0 = exact->start_x * 128 + size * 64;
            int dz0 = exact->start_z * 128 + size * 64;
            int dx1 = exact->end_x * 128 + size * 64;
            int dz1 = exact->end_z * 128 + size * 64;
            if( duration > 0 )
            {
                draw_position->x = (uint32_t)((dx0 * (duration - delta) + dx1 * delta) / duration);
                draw_position->z = (uint32_t)((dz0 * (duration - delta) + dz1 * delta) / duration);
            }
        }
        orientation->dst_yaw = k_facing_yaw[exact->facing & 3];
        orientation->yaw = orientation->dst_yaw;
    }
    return 1;
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

            if( World_UpdateExactMove(
                    world,
                    &player->exact_move,
                    &player->draw_position,
                    &player->orientation,
                    &player->animation,
                    1) )
            {
                /* Exact move overrides route movement; the secondary anim
                 * keeps whatever it was (reference skips routeMove). */
            }
            else
            {
                struct World_MoverInfo info = {
                    .pathing = &player->pathing,
                    .draw_position = &player->draw_position,
                    .grid_position = &player->grid_position,
                    .orientation = &player->orientation,
                    .idle = &player->idle_animations,
                    .animation = &player->animation,
                    .facing = &player->facing,
                    .size_x = 1,
                    .size_z = 1,
                };
                int seqId = World_UpdateMoverMovementAndAnimation(&info);
                World_ApplySecondaryAnim(&player->animation, seqId);
            }
            /* Reference moveEntity order: move (route or exact) -> entityFace
             * -> entityAnim. entityFace runs on both move branches. */
            {
                int face_seq = World_EntityFace(
                    world,
                    &player->facing,
                    &player->draw_position,
                    &player->orientation,
                    &player->pathing,
                    &player->idle_animations,
                    &player->animation);
                if( face_seq != -1 )
                    World_ApplySecondaryAnim(&player->animation, face_seq);
            }
            World_StepEntityAnimation(
                world, &player->animation, &player->spotanim, &player->pathing);
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
                .facing = &npc->facing,
                .size_x = size,
                .size_z = size,
            };
            int seqId = World_UpdateMoverMovementAndAnimation(&info);
            World_ApplySecondaryAnim(&npc->animation, seqId);
            {
                int face_seq = World_EntityFace(
                    world,
                    &npc->facing,
                    &npc->draw_position,
                    &npc->orientation,
                    &npc->pathing,
                    &npc->idle_animations,
                    &npc->animation);
                if( face_seq != -1 )
                    World_ApplySecondaryAnim(&npc->animation, face_seq);
            }
            World_StepEntityAnimation(world, &npc->animation, &npc->spotanim, &npc->pathing);
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
#define WORLD_MOVER_PAINTER_PADDING 60

/* A mover draws between tiles, so it registers over the tile span its
 * padded fine position covers rather than a single grid cell (Client-TS
 * World.addDynamic: (x±padding)>>7; padding 60 for size-1 movers,
 * 60+(size-1)*64 for larger NPCs). */
void
World_EntityPainterFootprint(
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
        struct World_PainterFootprint footprint;
        World_EntityPainterFootprint(
            (int)player->draw_position.x,
            (int)player->draw_position.z,
            WORLD_MOVER_PAINTER_PADDING,
            world->_scene_size,
            &footprint);
        painter_add_normal_scenery(
            world->painter,
            footprint.sx,
            footprint.sz,
            player->grid_position.level,
            player->element_id,
            footprint.size_x,
            footprint.size_z);
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
        struct World_PainterFootprint footprint;
        World_EntityPainterFootprint(
            (int)npc->draw_position.x,
            (int)npc->draw_position.z,
            WORLD_MOVER_PAINTER_PADDING + (size - 1) * 64,
            world->_scene_size,
            &footprint);
        painter_add_normal_scenery(
            world->painter,
            footprint.sx,
            footprint.sz,
            npc->grid_position.level,
            npc->element_id,
            footprint.size_x,
            footprint.size_z);
    }

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        if( !p || p->element_id < 0 || p->cycle < p->t1 )
            continue;
        struct World_PainterFootprint footprint;
        World_EntityPainterFootprint(
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

    pool = &world->entities.obj_stack;
    for( int oi = World_EntityPoolHead(pool); oi != WORLD_ENTITY_NIL;
         oi = World_EntityPoolNext(pool, oi) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, oi);
        if( !stack || stack->element_id < 0 )
            continue;
        int grid_x = stack->grid_position.x;
        int grid_z = stack->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        painter_add_normal_scenery(
            world->painter, grid_x, grid_z, stack->grid_position.level, stack->element_id, 1, 1);
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

    world->cycle += cycles_elapsed;

    World_CycleUpdatePlayers(world, cycles_elapsed);
    World_CycleUpdateNpcs(world, cycles_elapsed);
    World_CycleUpdateProjectiles(world, cycles_elapsed);
    World_CycleUpdateSpotanims(world, cycles_elapsed);

    if( world->painter )
        World_CycleRegisterPainterDynamics(world);
}
