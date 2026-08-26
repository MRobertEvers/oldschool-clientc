#include "world.h"

#include "entity_facets.h"

#include "features/features.h"

#include "toridraw_scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "log/torirs_log.h"

/* Rev-239 Statics.method6312: radians to the 2048-step yaw unit.  Keep the
 * gamepack's full double precision; the dated Client-TS value maps north/east
 * one yaw unit away from the same cardinals used by route movement. */
#define WORLD_YAW_FROM_RADIANS 325.94932345220167

struct World_MoverInfo
{
    struct World* world;
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

static int anim_step_active(struct WorldEntityFacet_AnimationStep const* step);
static int cycle_seq_preanim_move(struct World* world, int seq_id);
static int cycle_seq_postanim_move(struct World* world, int seq_id);

/*
 * Route-step speed, in draw units per 20ms client cycle.
 *
 * rev-239 class105.method3520 / method3611 -- both movers select the speed
 * with this identical block, which is why it is one function here.
 *
 *   4          walking
 *   2          while still turning toward the step, but only for an entity
 *              free to turn: one locked onto a face target keeps full speed
 *   6 / 8      the queue has run 3 / 4 deep -- the entity is BEHIND where the
 *              server says it stands and closes the gap
 *   8          catching up the cycles a DELAYMOVE seq held it still for
 *   x2         the step arrived as a run step
 *
 * The 6/8 rungs are not decoration and must not be capped back to walking
 * speed: 30 cycles at speed 4 covers 120 of a tile's 128 units, so an entity
 * walking without pause falls 8 units further behind every server tick. The
 * reference lets the queue depth pull it back; a cap leaves it drifting until
 * the queue overflows, which is what read as the player "stuttering" behind a
 * moving target.
 *
 * rev-239 also halves the speed for a CRAWL step (class174.field2475) and
 * gives NPCs whose record carries config opcode 109 the lower 6/8 thresholds
 * (>1 / >2 rather than >2 / >3). Neither is reachable here: no revision this
 * tree speaks emits a crawl step, and the opcode is not decoded.
 */
static int
World_MoverStepSpeed(
    struct World_MoverInfo const* info,
    int route_length,
    bool consume_delay_move)
{
    int move_speed = 4;

    if( info->orientation->yaw != info->orientation->dst_yaw &&
        info->facing->entity_id == WORLD_FACING_ENTITY_NONE && info->facing->turn_speed != 0 )
        move_speed = 2;
    if( route_length > 2 )
        move_speed = 6;
    if( route_length > 3 )
        move_speed = 8;
    if( info->animation->anim_delay_move > 0 && route_length > 1 )
    {
        move_speed = 8;
        /* method3520 consumes a held cycle; method3611 reads the same counter
         * without spending it, because the two run at different rates. */
        if( consume_delay_move )
            info->animation->anim_delay_move--;
    }
    if( info->pathing->route_run[route_length - 1] )
        move_speed <<= 1;

    return move_speed;
}

/*
 * The DELAYMOVE hold -- rev-239 method3520/method3611 both open with it.
 *
 * A primary seq that forbids movement freezes the route where it is and banks
 * a cycle in anim_delay_move for the speed-8 catch-up above.
 */
static bool
World_MoverHeldByAnim(struct World_MoverInfo* info, bool bank_held_cycle)
{
    int seq;

    if( !anim_step_active(&info->animation->primary) || info->animation->primary.delay != 0 )
        return false;

    seq = info->animation->primary.anim_id;
    if( info->animation->preanim_route_length > 0 &&
        cycle_seq_preanim_move(info->world, seq) == 0 )
    {
        if( bank_held_cycle )
            info->animation->anim_delay_move++;
        return true;
    }
    if( info->animation->preanim_route_length == 0 &&
        cycle_seq_postanim_move(info->world, seq) == 0 )
    {
        if( bank_held_cycle )
            info->animation->anim_delay_move++;
        return true;
    }
    return false;
}

/**
 * Per client cycle (20ms): rev-239 class105.method3520.
 *
 * Chooses the facing and the walk/run sequence for the step in progress, and
 * hands back the secondary seq id (-1 = clear). It deliberately does NOT move
 * the entity -- that is World_MoversAdvance, which runs per render frame. The
 * split is the reference's, and it is the whole of "smooth": movement is
 * integrated against real elapsed time, while the decisions that only change
 * once per cycle stay on the cycle clock.
 */
static int
World_UpdateMoverMovementAndAnimation(struct World_MoverInfo* info)
{
    int seqId = info->idle->readyanim;
    int route_length = info->pathing->route_length;
    if( route_length == 0 )
    {
        info->animation->anim_delay_move = 0;
        goto yaw_turn;
    }

    if( World_MoverHeldByAnim(info, /*bank_held_cycle=*/true) )
        return seqId;

    int x = (int)info->draw_position->x;
    int z = (int)info->draw_position->z;
    int dstX = info->pathing->route_x[route_length - 1] * 128 + info->size_x * 64;
    int dstZ = info->pathing->route_z[route_length - 1] * 128 + info->size_z * 64;

    /*
     * Too far to walk: put the entity there.
     *
     * The threshold is the era's. rev-239 tests `max(|dx|, |dz|) > 288` against
     * the *float* position; the 2004 client tests each axis against 256 on the
     * integer one. 256 is a quarter-tile short of covering a two-tile run step
     * taken from a fractional position, which is an ordinary state under the
     * frame-paced mover and nowhere near one under the cycle mover -- so the
     * two constants are not interchangeable and neither is a rounding of the
     * other.
     */
    if( World_MoverModel(info->world) == TORIRS_MOVER_FRAME_DELTA )
    {
        float dx = (float)dstX - info->draw_position->fx;
        float dz = (float)dstZ - info->draw_position->fz;
        float far = fabsf(dx) > fabsf(dz) ? fabsf(dx) : fabsf(dz);

        if( far > 288.0f )
        {
            World_DrawPositionSet(info->draw_position, dstX, dstZ);
            info->grid_position->x = info->pathing->route_x[route_length - 1];
            info->grid_position->z = info->pathing->route_z[route_length - 1];
            return -1;
        }
    }
    else if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        World_DrawPositionSet(info->draw_position, dstX, dstZ);
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

    /* Speed 8 is a run whatever queued the step: an entity closing a queue
     * backlog runs, which is how both references show a walk that has fallen
     * behind (method3520's `var20 >= 8` remap, Client.ts's `moveSpeed >= 8`). */
    int move_speed = World_MoverStepSpeed(info, route_length, /*consume_delay_move=*/true);
    if( move_speed >= 8 && seqId == info->idle->walkanim && info->idle->runanim != -1 )
        seqId = info->idle->runanim;

    /*
     * Classic only: spend the speed here, on the cycle clock, because there is
     * no frame mover under that era to spend it. Under FRAME_DELTA this pass
     * decides and World_MoverAdvance travels, and doing both would move the
     * entity twice.
     */
    if( World_MoverModel(info->world) == TORIRS_MOVER_CYCLE_INTEGER )
    {
        if( x < dstX )
        {
            x += move_speed;
            if( x > dstX )
                x = dstX;
        }
        else if( x > dstX )
        {
            x -= move_speed;
            if( x < dstX )
                x = dstX;
        }
        if( z < dstZ )
        {
            z += move_speed;
            if( z > dstZ )
                z = dstZ;
        }
        else if( z > dstZ )
        {
            z -= move_speed;
            if( z < dstZ )
                z = dstZ;
        }
        World_DrawPositionSet(info->draw_position, x, z);
    }

    /* Under FRAME_DELTA the frame mover retires a step the moment it lands on
     * it, so this only covers the entity that was already standing exactly on
     * its next tile when the step arrived. Under CYCLE_INTEGER it is the one
     * and only retirement. */
    if( x == dstX && z == dstZ )
    {
        /* route_length is a uint8_t: decrementing it at 0 wraps to 255, and the
         * `< 0` clamp this used to carry could never fire. Guard the decrement,
         * the way preanim_route_length below already does. */
        if( info->pathing->route_length > 0 )
            info->pathing->route_length--;
        info->grid_position->x = info->pathing->route_x[0];
        info->grid_position->z = info->pathing->route_z[0];
        if( info->animation->preanim_route_length > 0 )
            info->animation->preanim_route_length--;
    }

yaw_turn:;
    return seqId;
}

/*
 * Per render frame: rev-239 class105.method3611, driven by client.method2324
 * with `elapsed_ns / 2.0e7` -- elapsed time expressed in 20ms client cycles,
 * fractional.
 *
 * The loop is the reference's: walk toward the next queued tile, and when the
 * frame's budget carries the entity *past* it, retire that tile and spend the
 * remainder on the one behind it. That carry is what keeps a route continuous
 * across a frame boundary instead of quantising every step to a whole cycle.
 */
static void
World_MoverAdvance(
    struct World_MoverInfo* info,
    float cycles)
{
    /* Held once, before the loop, exactly as method3611 does: the seq gates the
     * whole frame, not each tile of it. The cycle mover is what banks the held
     * cycle for the catch-up; doing it here as well would bank once per frame. */
    if( World_MoverHeldByAnim(info, /*bank_held_cycle=*/false) )
        return;

    while( info->pathing->route_length > 0 && cycles > 0.0f )
    {
        int route_length = info->pathing->route_length;
        float fx = info->draw_position->fx;
        float fz = info->draw_position->fz;
        int dstX = info->pathing->route_x[route_length - 1] * 128 + info->size_x * 64;
        int dstZ = info->pathing->route_z[route_length - 1] * 128 + info->size_z * 64;
        int move_speed = World_MoverStepSpeed(info, route_length, /*consume_delay_move=*/false);
        float step = (float)move_speed * cycles;
        float leftover = 0.0f;

        if( fx < (float)dstX )
        {
            info->draw_position->fx += step;
            if( info->draw_position->fx > (float)dstX )
            {
                leftover = (info->draw_position->fx - (float)dstX) / (float)move_speed;
                info->draw_position->fx = (float)dstX;
            }
        }
        else if( fx > (float)dstX )
        {
            info->draw_position->fx -= step;
            if( info->draw_position->fx < (float)dstX )
            {
                leftover = ((float)dstX - info->draw_position->fx) / (float)move_speed;
                info->draw_position->fx = (float)dstX;
            }
        }
        if( fz < (float)dstZ )
        {
            info->draw_position->fz += step;
            if( info->draw_position->fz > (float)dstZ )
            {
                float slack = (info->draw_position->fz - (float)dstZ) / (float)move_speed;
                leftover = leftover > slack ? leftover : slack;
                info->draw_position->fz = (float)dstZ;
            }
        }
        else if( fz > (float)dstZ )
        {
            info->draw_position->fz -= step;
            if( info->draw_position->fz < (float)dstZ )
            {
                float slack = ((float)dstZ - info->draw_position->fz) / (float)move_speed;
                leftover = leftover > slack ? leftover : slack;
                info->draw_position->fz = (float)dstZ;
            }
        }

        cycles = leftover;
        info->draw_position->x = (uint32_t)(int)info->draw_position->fx;
        info->draw_position->z = (uint32_t)(int)info->draw_position->fz;

        if( (int)info->draw_position->x == dstX && (int)info->draw_position->z == dstZ )
        {
            info->pathing->route_length--;
            info->grid_position->x = info->pathing->route_x[0];
            info->grid_position->z = info->pathing->route_z[0];
            if( info->animation->preanim_route_length > 0 )
                info->animation->preanim_route_length--;
        }
        else
        {
            /* Did not arrive, so the budget is spent. */
            break;
        }
    }
}

/* Reference entity-facing update, run once per entity per cycle right after
 * the movement step and before entity animation.
 *
 * Facing source precedence follows rev-239 Statics.method6710: a consumable
 * direct angle first, then a pending location, then an entity target.  Only
 * the first eligible source is applied in a cycle.
 * The yaw is then stepped toward dst_yaw by turn_speed, and an entity that is
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
    bool applied = false;

    if( facing->turn_speed == 0 )
        return -1;

    /* class105.method3650 + method3537: a direct angle is one-shot, but mode
     * 0 defers it while walking whereas mode 1 permits it during a route. */
    if( facing->direct_angle >= 0 &&
        (facing->face_during_movement || pathing->route_length == 0 ||
         animation->anim_delay_move > 0) )
    {
        orientation->dst_yaw = (uint16_t)(facing->direct_angle & 0x7ff);
        facing->direct_angle = -1;
        applied = true;
    }

    /* FaceSquare is in absolute half-tiles; `- base - base` converts the
     * doubled coordinate straight into fine units at 64 per half-tile.
     * Applies while standing still or held by DELAYMOVE, unless revision-239
     * movement mode 1 explicitly permits facing during a route. */
    if( !applied && (facing->square_x != 0 || facing->square_z != 0) &&
        (facing->face_during_movement || pathing->route_length == 0 ||
         animation->anim_delay_move > 0) )
    {
        dst_x = (int)draw_position->x -
                (facing->square_x - world->_base_tile_x - world->_base_tile_x) * 64;
        dst_z = (int)draw_position->z -
                (facing->square_z - world->_base_tile_z - world->_base_tile_z) * 64;
        if( dst_x != 0 || dst_z != 0 )
            orientation->dst_yaw =
                (uint16_t)(((int)(atan2((double)dst_x, (double)dst_z) *
                                        WORLD_YAW_FROM_RADIANS)) &
                           0x7ff);
        facing->square_x = 0;
        facing->square_z = 0;
        applied = true;
    }

    if( !applied && facing->entity_id != WORLD_FACING_ENTITY_NONE )
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
        else if( facing->fallback_angle >= 0 )
        {
            orientation->dst_yaw = (uint16_t)(facing->fallback_angle & 0x7ff);
        }
    }

    {
        int remaining_yaw = (orientation->dst_yaw - orientation->yaw) & 0x7ff;
        if( remaining_yaw == 0 )
        {
            facing->instant = false;
            return -1;
        }

        if( facing->instant )
        {
            orientation->yaw = orientation->dst_yaw;
            facing->instant = false;
            return -1;
        }

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
cycle_seq_postanim_move(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.postanim_move )
        return world->seq_source.postanim_move(world->seq_source.userdata, seq_id);
    return 0;
}

static int
cycle_seq_stretches(
    struct World* world,
    int seq_id)
{
    if( world->seq_source.stretches )
        return world->seq_source.stretches(world->seq_source.userdata, seq_id);
    return 0;
}

static int
anim_step_active(struct WorldEntityFacet_AnimationStep const* step)
{
    return step->anim_id != (uint16_t)-1 && step->anim_id != 0;
}

/*
 * One frame crossed -> one frame sound, emitted here rather than sampled by the
 * renderer.
 *
 * The reference notifies its listener from inside this loop (deob
 * Statics.method5261, and method4366 for the frame-length branch), which is why
 * a sound survives both a slow frame -- several cycles stepped at once, every
 * frame in between still announced -- and an action animation covering the
 * looping readyanim underneath it, whose sounds keep playing.
 */
static void
World_EmitAnimFrameSound(
    struct World* world,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    int seq_id,
    int frame)
{
    if( !world->anim_sound_sink.frame )
        return;
    world->anim_sound_sink.frame(
        world->anim_sound_sink.userdata,
        seq_id,
        frame,
        (int)draw_position->x,
        (int)draw_position->z);
}

/*
 * ONE STEPPER, BOTH TRACKS -- an exact port of the reference's `method4366`
 * (Statics.java:11987), which is what `method5261` calls for a classic-frame
 * sequence and which the entity update runs over the movement track
 * (`field1504`) and the action track (`field1505`) alike.
 *
 *     int var9 = var1 + var7;                       // advance + carried cycle
 *     while (var9 > var5.field4651[var6]) {         // > this frame's duration
 *         var9 -= var5.field4651[var6];             // CARRY the remainder
 *         var6++;
 *         if ((var4 & 0x2) == 0 && var2 != null) var2.method9642(var5, var6, ..);
 *         if (var6 >= var5.field4649.length) {
 *             var8++; var4 |= 0x1;
 *             var6 -= var5.field4652;               // frame -= frameStep
 *             if (var8 >= var5.field4661) var4 |= 0x2;
 *             if (!(var6 >= 0 && var6 < len)) { var4 |= 0x2; var6 = 0; }
 *             if ((var4 & 0x2) == 0) var2.method9642(var5, var6, ..);
 *         }
 *     }
 *
 * The two tracks used to have two implementations here and only the action one
 * was faithful. The secondary zeroed the accumulator on every frame advance
 * instead of subtracting the frame's duration, and the arithmetic of that is
 * not subtle: `cycle` is pre-incremented and the test is `cycle > duration`, so
 * carrying the remainder costs a 3-cycle frame three cycles in the steady state
 * and dropping it costs four. **Every idle and walk animation in the game ran a
 * third slow**, on a different clock from the action animation covering it, and
 * the error scaled with the frame duration -- a 1-cycle frame ran at half speed.
 * Xarpus is where it is loudest, because his 4-tick spit cadence cuts to the
 * idle twenty times a minute and it is never where the previous cut left it.
 *
 * It also used `if` rather than `while`, so a track could not advance more than
 * one frame per call however many cycles it was handed, and wrapped to frame 0
 * unconditionally rather than through `frameStep`/`maxLoops`.
 */
enum
{
    WORLD_ANIM_STEP_LOOPED = 0x1,
    WORLD_ANIM_STEP_FINISHED = 0x2,
    WORLD_ANIM_STEP_ADVANCED = 0x4,
};

static int
World_StepAnimationTrack(
    struct World* world,
    struct WorldEntityFacet_AnimationStep* step,
    int cycles,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    int emit_sounds)
{
    int seq = step->anim_id;
    int count = cycle_seq_frame_count(world, seq);
    int frame;
    int accumulated;
    int loop;
    int flags = 0;

    assert(world);
    assert(step);

    /*
     * A seq whose frames are not resident cannot advance and cannot end, so the
     * entity holds frame 0 of it. That is what a corpse stuck in its death pose
     * is: the animation never played and never finished, and both halves are
     * silent -- `count > 0` simply skips the whole block.
     */
    if( count <= 0 )
    {
        if( getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG("anim: seq %d has no frames; held at frame 0\n", seq);
        return 0;
    }

    frame = step->frame;
    accumulated = step->cycle;
    loop = step->loop;
    /* Reference guard: a frame index past the end of the seq resets both. It
     * happens when a track keeps its frame across a seq change. */
    if( frame >= count )
    {
        frame = 0;
        accumulated = 0;
    }

    accumulated += cycles;
    while( accumulated > cycle_seq_frame_duration(world, seq, frame) )
    {
        accumulated -= cycle_seq_frame_duration(world, seq, frame);
        frame++;
        flags |= WORLD_ANIM_STEP_ADVANCED;
        if( !(flags & WORLD_ANIM_STEP_FINISHED) && emit_sounds )
            World_EmitAnimFrameSound(world, draw_position, seq, frame);
        if( frame >= count )
        {
            int frame_step = cycle_seq_frame_step(world, seq);
            loop++;
            flags |= WORLD_ANIM_STEP_LOOPED;
            frame -= frame_step;
            if( loop >= cycle_seq_max_loops(world, seq) )
                flags |= WORLD_ANIM_STEP_FINISHED;
            if( frame < 0 || frame >= count )
            {
                flags |= WORLD_ANIM_STEP_FINISHED;
                frame = 0;
            }
            /* Looped rather than finished: the frame it looped back onto sounds,
             * exactly as the frames before it did. Xarpus' first wing flap is on
             * frame 1 of a looping readyanim, so a loop that emitted nothing on
             * its way round would drop one flap in three. */
            if( !(flags & WORLD_ANIM_STEP_FINISHED) && emit_sounds )
                World_EmitAnimFrameSound(world, draw_position, seq, frame);
        }
    }

    step->frame = (uint16_t)frame;
    step->cycle = (uint16_t)accumulated;
    step->loop = (uint8_t)(loop > 255 ? 255 : loop);
    return flags;
}

/* One client cycle of frame stepping for an entity's animation tracks +
 * attached graphic (exact port of Client.ts entityAnim, 4000-4075). */
static void
World_StepEntityAnimation(
    struct World* world,
    struct WorldEntityFacet_Animation* anim,
    struct WorldEntityFacet_EntitySpotanim* spot,
    struct WorldEntityFacet_Pathing const* pathing,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_IdleAnimations const* idle)
{
    /* Reference entityAnim clears this at the top every cycle; only an active,
     * un-delayed primary seq below re-asserts it from the seq's stretches flag. */
    anim->needs_forward_draw_padding = 0;

    /*
     * Secondary (idle/walk). The reference steps it first and resets it on the
     * same terms as any other track (`Statics` ~40056):
     *
     *     int var10 = method5261(var1.field1504, 1, field6534, ..);
     *     if ((var10 & 0x2) != 0) method9990(var1.field1504, ..);
     *
     * -- so a movement animation that runs out of loops restarts rather than
     * parking, which is what makes a readyanim loop forever without the wrap
     * being special-cased here.
     */
    if( anim_step_active(&anim->secondary) )
    {
        int flags =
            World_StepAnimationTrack(world, &anim->secondary, 1, draw_position, 1);
        if( flags & WORLD_ANIM_STEP_FINISHED )
        {
            anim->secondary.frame = 0;
            anim->secondary.cycle = 0;
            anim->secondary.loop = 0;
        }
    }

    /* Entity-attached graphic (reference entityAnim, Client.ts 4019-4036): once
     * the start delay elapses, step the frame from the spot's own seq and clear
     * the id when the single loop completes. The app combines the spot into the
     * entity's rendering via a companion scene element pinned to the entity
     * (app_world_sync_entity_spotanims), driven to spot->frame. */
    if( spot && spot->id != -1 && world->cycle >= spot->last_cycle )
    {
        int seq = world->seq_source.spotanim_seq
                      ? world->seq_source.spotanim_seq(world->seq_source.userdata, spot->id)
                      : -1;
        int count = cycle_seq_frame_count(world, seq);

        if( spot->frame < 0 )
            spot->frame = 0;
        spot->cycle++;
        while( count > 0 && spot->frame < count &&
               spot->cycle > cycle_seq_frame_duration(world, seq, spot->frame) )
        {
            spot->cycle -= cycle_seq_frame_duration(world, seq, spot->frame);
            spot->frame++;
        }
        /* Seq resolved and the single loop is done -> graphic ends. Guarded on
         * count > 0 so an id whose seq/model is still loading (count 0) waits
         * rather than expiring instantly (frame 0 >= count 0). */
        if( count > 0 && spot->frame >= count )
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
            int flags = World_StepAnimationTrack(world, &anim->primary, 1, draw_position, 1);
            if( flags & WORLD_ANIM_STEP_FINISHED )
            {
                /*
                 * The reference clears the action track and then, in the same
                 * breath, restarts the idle underneath it (Statics ~40139):
                 *
                 *   if ((var17 & 0x2) != 0) {
                 *       var1.field1505.method9935(..);              // clear
                 *       if (var1.field1504.method9969(..) == var1.field1498)
                 *           if (var1.method2909(..))                // opcode 130
                 *               method9990(var1.field1504, ..);     // frame = 0
                 *   }
                 *
                 * `method9935` is `method9934(-1)`, i.e. seq = -1. The restart
                 * exists because the secondary keeps stepping underneath the
                 * action, so by the time the action ends it sits at an arbitrary
                 * frame and revealing it there is a jump cut.
                 *
                 * TWO of the reference's three conditions are kept. The
                 * secondary has to BE the readyanim -- restarting a walk
                 * animation would stutter the gait, and it is positional -- and
                 * it fires on the FINISH, not on a loop-back.
                 *
                 * THE OPCODE-130 CONDITION IS NOT. `method2909` gates this on
                 * an NpcType flag that 33 records in the rev-239 cache set, and
                 * the flag is the wrong instrument for the question: an action
                 * animation ENDS on the readyanim's loop point whether or not
                 * its record says so, because that is how the clip was authored.
                 * `world_restart_readyanim_under_action` (world.c) states the
                 * matching half at the other end and states the evidence; this
                 * is its mirror, and the two have to agree or a clip is seamless
                 * going in and a jump cut coming out.
                 *
                 * Xarpus is the measurement. Seq 8059 descends out of the spit
                 * at ~73 authored units per frame -- its last three frames top
                 * out at -1045, -972, -900 -- and seq 8058 frame 0 tops out at
                 * -825, which continues that descent exactly. Resuming the
                 * readyanim where the free-run left it instead put frame 26
                 * (-1095) on screen: 195 units of upward snap, against the
                 * direction he was moving. Same reading from the pose diff --
                 * 14.94 to frame 0 against 19.74 to frame 26.
                 *
                 * What it costs elsewhere is a resume point that is fixed rather
                 * than wandering, and the restart at the far end already spent
                 * that: with both in place a readyanim under a repeating attack
                 * shows its first `action length` of cycles, where before it
                 * showed an arbitrary window that moved every time. Neither is
                 * more of the loop than the other; only this one lands on the
                 * frame the clip hands over to. `idle_anim_restart` stays
                 * decoded and carried (ToriRS_Npctype, WorldEntityFacet_
                 * IdleAnimations) because the cache states it and a field that
                 * is read back is worth more than one that was dropped -- it is
                 * simply no longer what decides this.
                 */
                anim->primary.anim_id = (uint16_t)-1;
                anim->primary.frame = 0;
                anim->primary.cycle = 0;
                anim->primary.loop = 0;
                if( idle && anim_step_active(&anim->secondary) &&
                    anim->secondary.anim_id == (uint16_t)idle->readyanim )
                {
                    anim->secondary.frame = 0;
                    anim->secondary.cycle = 0;
                    anim->secondary.loop = 0;
                }
            }
            /* Reference entityAnim (Client.ts:4069): an active, un-delayed
             * primary seq drives the forward draw-padding from its stretches
             * flag. Read from the seq captured at block top so a seq that just
             * finished this cycle still contributes its padding, as upstream. */
            anim->needs_forward_draw_padding = cycle_seq_stretches(world, seq) ? 1 : 0;
        }
        if( anim->primary.delay > 0 )
            anim->primary.delay--;
    }
}

/* Is a server-forced exact move still placing this entity? While one is, it
 * owns the draw position and the route mover must keep off (rev-239
 * method3611's `field1471 >= cycle || field1521 >= cycle` early-out). */
static int
World_ExactMoveActive(
    struct World const* world,
    struct WorldEntityFacet_ExactMove const* exact)
{
    if( exact->move_end == 0 && exact->move_start == 0 )
        return 0;
    return exact->move_start >= world->cycle;
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
    struct WorldEntityFacet_Animation* anim,
    int size)
{
    static const uint16_t k_facing_yaw[4] = { 1024, 1536, 0, 512 };

    if( !World_ExactMoveActive(world, exact) )
    {
        /* Window passed (or never armed). */
        exact->move_end = 0;
        exact->move_start = 0;
        return 0;
    }

    /* Exact move clears the DELAYMOVE hold counter (Client.ts 3765 / 3790). */
    anim->anim_delay_move = 0;

    if( exact->move_end > world->cycle )
    {
        /* Phase 1: glide toward the START tile. */
        int delta = exact->move_end - world->cycle;
        int dst_x = exact->start_x * 128 + size * 64;
        int dst_z = exact->start_z * 128 + size * 64;
        World_DrawPositionSet(
            draw_position, (int)draw_position->x + (dst_x - (int)draw_position->x) / delta,
            (int)draw_position->z + (dst_z - (int)draw_position->z) / delta);
        orientation->dst_yaw = exact->facing_is_yaw
                                   ? (uint16_t)(exact->facing & 0x7ff)
                                   : k_facing_yaw[exact->facing & 3];
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
                World_DrawPositionSet(
                    draw_position, (dx0 * (duration - delta) + dx1 * delta) / duration,
                    (dz0 * (duration - delta) + dz1 * delta) / duration);
            }
        }
        orientation->dst_yaw = exact->facing_is_yaw
                                   ? (uint16_t)(exact->facing & 0x7ff)
                                   : k_facing_yaw[exact->facing & 3];
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
                    .world = world,
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
                world,
                &player->animation,
                &player->spotanim,
                &player->pathing,
                &player->draw_position,
                &player->idle_animations);
            /* Overhead chat expiry (reference Client.ts:3161). */
            if( player->chat.timer > 0 && --player->chat.timer == 0 )
                player->chat.message[0] = '\0';
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
            if( World_UpdateExactMove(
                    world,
                    &npc->exact_move,
                    &npc->draw_position,
                    &npc->orientation,
                    &npc->animation,
                    size) )
            {
                /* Exact move overrides route movement. */
            }
            else
            {
                struct World_MoverInfo info = {
                    .world = world,
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
            }
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
            World_StepEntityAnimation(
                world, &npc->animation, &npc->spotanim, &npc->pathing, &npc->draw_position,
                &npc->idle_animations);
            /* Overhead chat expiry (reference Client.ts:3174). */
            if( npc->chat.timer > 0 && --npc->chat.timer == 0 )
                npc->chat.message[0] = '\0';
        }
    }
}

/* Re-aim a projectile at its target entity's *live* draw position (reference
 * addProjectiles, which looks the entity up fresh every cycle and calls
 * setTarget with the position it holds right now, so the arc bends to follow a
 * target that walked after the cast). Positive wire ids are NPC slots + 1,
 * negative are player slots as -(slot) - 1 — the local player included, since
 * it sits in the player pool under its own server pid.
 *
 * A target that is not currently synced leaves the last aim point standing:
 * that is what the reference's `if (npc)` / `if (player)` guards do, and it
 * matters here because entity slots go briefly unresolved across a rebuild. */
static void
World_ProjectileTrackTarget(
    struct World* world,
    struct WorldEntity_Projectile* proj)
{
    struct WorldEntityFacet_DrawPosition const* dst = NULL;

    if( proj->target == WORLD_PROJECTILE_TARGET_NONE )
        return;

    if( proj->target > 0 )
    {
        struct WorldEntity_NPC* npc = World_NpcGetByServerSlot(world, proj->target - 1);
        if( npc )
            dst = &npc->draw_position;
    }
    else
    {
        struct WorldEntity_Player* player = World_PlayerGetByServerPid(world, -proj->target - 1);
        if( player )
            dst = &player->draw_position;
    }

    if( !dst )
        return;

    /* dst_level stays the projectile's own level: the reference samples the
     * target's height with getAvH(npc.x, npc.z, *proj.level*), not the
     * entity's level. */
    proj->dst_x = (int)dst->x;
    proj->dst_z = (int)dst->z;
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
            World_ProjectileTrackTarget(world, p);
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
            int post_activation_cycles = 0;

            if( cycles_elapsed > 0 )
            {
                int idle_before = s->idle_cycles;

                s->idle_cycles -= cycles_elapsed;
                if( s->idle_cycles <= 0 )
                {
                    s->active = true;
                    s->idle_cycles = 0;
                    /*
                     * How many of this step's cycles landed ON OR AFTER the
                     * delay boundary — the boundary-crossing cycle itself
                     * counts as the first (reference/pre-existing rule: a
                     * single-cycle step that brings idle_cycles to exactly 0
                     * scores active_cycle 1, not 0, so a same-tick delay=0
                     * spotanim is unaffected — see World_SpotanimSpawn's own
                     * immediate activation for that case).
                     *
                     * A multi-cycle catch-up step (several world cycles
                     * settled in one call, e.g. after a stalled frame) can
                     * cross the boundary mid-step. Crediting the WHOLE step
                     * to active_cycle, as this used to, backdates the count
                     * to before the graphic was ever visible and despawns it
                     * up to `idle_before - 1` cycles early; this instead
                     * counts only the cycles from the boundary onward, which
                     * collapses to the single-step rule above whenever
                     * cycles_elapsed == idle_before. */
                    post_activation_cycles = cycles_elapsed - idle_before + 1;
                    if( post_activation_cycles < 1 )
                        post_activation_cycles = 1;
                }
            }
            if( !s->active )
            {
                si = next;
                continue;
            }
            /* First cycle of visibility: the animation starts NOW. Held at
             * frame 0 until here, because a delayed spotanim that stepped
             * through its own delay would surface part-played and then freeze
             * on a dead final frame — the delay is almost always a projectile's
             * flight, so it is routinely longer than the sequence itself. */
            World_EmitEvent(world, WorldEventKind_SpotanimStarted, s->element_id);
            s->active_cycle += post_activation_cycles;
        }
        else if( cycles_elapsed > 0 )
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
 * 60+(size-1)*64 for larger NPCs).
 *
 * The reference World.setSprite rejects the whole sprite when any tile of the
 * padded span is out of [0,scene_size): a large entity straddling the scene
 * edge simply is not drawn. We do the same (return false) instead of clamping
 * the span — clamping can leave sx == scene_size (a size-2 NPC on the last
 * in-scene tile centres its draw position past the edge, so x0 rounds up to
 * scene_size while x1 clamps below it), and that out-of-bounds sx later trips
 * the painter's tile lookup assert on reset/paint. */
bool
World_EntityPainterFootprint(
    int pos_x,
    int pos_z,
    int draw_padding,
    int yaw,
    int forward_padding,
    int scene_size,
    struct World_PainterFootprint* out)
{
    int x0 = pos_x - draw_padding;
    int z0 = pos_z - draw_padding;
    int x1 = pos_x + draw_padding;
    int z1 = pos_z + draw_padding;

    /* Extend the fine-position span one tile forward along yaw before the
     * >>7 to tiles (reference World.addDynamic forwardPadding). Each axis edge
     * moves out when yaw points into that half-turn, so a diagonal facing
     * grows both a +x/-x and a +z/-z edge. */
    if( forward_padding )
    {
        if( yaw > 640 && yaw < 1408 )
            z1 += 128;
        if( yaw > 1152 && yaw < 1920 )
            x1 += 128;
        if( yaw > 1664 || yaw < 384 )
            z0 -= 128;
        if( yaw > 128 && yaw < 896 )
            x0 -= 128;
    }

    x0 /= 128;
    z0 /= 128;
    x1 /= 128;
    z1 /= 128;

    if( x0 < 0 || z0 < 0 || x1 >= scene_size || z1 >= scene_size )
        return false;

    out->sx = x0;
    out->sz = z0;
    out->size_x = x1 - x0 + 1;
    out->size_z = z1 - z0 + 1;
    return true;
}

/* One-entity-per-tile dedup (reference Client.tileLastOccupiedCycle).
 *
 * A stationary size-1 entity sits exactly tile-centred: fine draw coord ==
 * tile*128 + 64, i.e. (draw & 0x7f) == 64 on both axes. The reference records
 * the first such entity to claim a tile in a frame and skips every later one,
 * so a pile of players/NPCs on one square renders as a single model instead of
 * z-fighting. Movers (between tiles) and larger NPCs are never deduped — they
 * always draw. `force` is set for the local player, which the reference never
 * skips (its `i != -1` guard); it still claims the tile so others stacked on it
 * are hidden. Returns true when the caller should skip registering the entity. */
static bool
world_dyn_tile_claim(
    struct World* world, int grid_x, int grid_z, int draw_x, int draw_z, int size, bool force)
{
    if( size != 1 || (draw_x & 0x7f) != 64 || (draw_z & 0x7f) != 64 )
        return false; /* mover / multi-tile: exempt from the dedup entirely */

    int idx = grid_x + grid_z * world->_scene_size;
    if( !force && world->tile_last_occupied_cycle[idx] == world->scene_cycle )
        return true;

    world->tile_last_occupied_cycle[idx] = world->scene_cycle;
    return false;
}

/* Local player's map plane (reference Client.minusedlevel). Movers have no
 * independent plane on the wire — gameDrawMain always addDynamic(minusedlevel). */
static int
world_local_level(struct World* world)
{
    if( world->local_pid < 0 )
        return 0;
    struct World_EntityPool* pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player && player->server_pid == world->local_pid )
            return player->grid_position.level;
    }
    return 0;
}

/* Register one player/NPC element with the painter over its padded footprint
 * (mover span, reference World.addDynamic). */
static void
world_dyn_register_mover(
    struct World* world,
    int element_id,
    int grid_level,
    int draw_x,
    int draw_z,
    int padding,
    int yaw,
    int forward_padding)
{
    struct World_PainterFootprint footprint;
    if( !World_EntityPainterFootprint(
            draw_x, draw_z, padding, yaw, forward_padding, world->_scene_size, &footprint) )
        return; /* padded span pokes off the scene edge: reference draws nothing */
    painter_add_normal_scenery(
        world->painter,
        footprint.sx,
        footprint.sz,
        grid_level,
        element_id,
        footprint.size_x,
        footprint.size_z,
        (world->scene ? ToriDraw_SceneElementOcclusionHeight(world->scene, element_id) : 0));
}

/* Register every player in the pool except the local one (registered first by
 * the caller). `only_local` flips the sense: register just the local player.
 * `local_level` is reference minusedlevel — all movers stamp onto that plane. */
static void
world_dyn_register_players(struct World* world, bool only_local, int local_level)
{
    struct World_EntityPool* pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( !player || player->element_id < 0 )
            continue;
        bool is_local = world->local_pid >= 0 && player->server_pid == world->local_pid;
        if( is_local != only_local )
            continue;
        int grid_x = player->grid_position.x;
        int grid_z = player->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        if( world_dyn_tile_claim(
                world,
                grid_x,
                grid_z,
                (int)player->draw_position.x,
                (int)player->draw_position.z,
                1,
                is_local) )
            continue;
        world_dyn_register_mover(
            world,
            player->element_id,
            local_level,
            (int)player->draw_position.x,
            (int)player->draw_position.z,
            WORLD_MOVER_PAINTER_PADDING,
            player->orientation.yaw,
            player->animation.needs_forward_draw_padding);
    }
}

/* Register NPCs of one draw tier: `alwaysontop` NPCs first (added ahead of
 * other players in the reference), then the rest. */
static void
world_dyn_register_npcs(struct World* world, bool alwaysontop, int local_level)
{
    struct World_EntityPool* pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( !npc || npc->multinpc_hidden || npc->element_id < 0 )
            continue;
        if( npc->alwaysontop != alwaysontop )
            continue;
        int size = npc->size > 0 ? npc->size : 1;
        int grid_x = npc->grid_position.x;
        int grid_z = npc->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        if( world_dyn_tile_claim(
                world,
                grid_x,
                grid_z,
                (int)npc->draw_position.x,
                (int)npc->draw_position.z,
                size,
                false) )
            continue;
        world_dyn_register_mover(
            world,
            npc->element_id,
            local_level,
            (int)npc->draw_position.x,
            (int)npc->draw_position.z,
            WORLD_MOVER_PAINTER_PADDING + (size - 1) * 64,
            npc->orientation.yaw,
            npc->animation.needs_forward_draw_padding);
    }
}

/* Dynamic entities re-register with the painter every cycle on top of the
 * static terrain/scenery set; without this pass they never produce
 * PNTR_CMD_ELEMENT commands and never draw (v1 world_cycle).
 *
 * Registration order = reference gameDrawMain (Client.ts:4409): ground objects
 * (part of the static tile in the reference, so behind everything dynamic),
 * then self, alwaysontop NPCs, other players, normal NPCs, projectiles,
 * spotanims. Same-tile ties draw in insertion order (painter walks
 * scenery_head->next), so earlier tiers sit behind later ones; combined with
 * the tile-claim dedup this reproduces which single entity wins a stacked
 * tile. */
static void
World_CycleRegisterPainterDynamics(struct World* world)
{
    struct World_EntityPool* pool;
    int local_level = world_local_level(world);

    painter_reset_to_static(world->painter);
    world->scene_cycle++;

    /* Runtime-spawned locs (zone LOC_ADD_CHANGE, e.g. an open door). The painter
     * static set is baked at build time, so these live in the scenery pool but
     * must be re-registered every cycle. Registered before the dynamics below so
     * they draw with the scenery (behind players/NPCs), matching a normal loc. */
    pool = &world->entities.scenery;
    for( int si = World_EntityPoolHead(pool); si != WORLD_ENTITY_NIL;
         si = World_EntityPoolNext(pool, si) )
    {
        struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, si);
        if( !sc || !sc->runtime_spawn || sc->element_id < 0 )
            continue;
        int grid_x = sc->grid_position.x;
        int grid_z = sc->grid_position.z;
        int paint_level;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        /* The scenery pool holds cache levels; the painter wants the level the
         * build's bridge push-down parked that tile on, which is the one the
         * static locs beside this one ended up in. */
        paint_level = World_LocPaintLevel(world, grid_x, grid_z, sc->grid_position.level);
        /* Wall locs re-register with their recorded wallside so the painter
         * orders them like a built wall (drawn on the correct side of the
         * tile); the removal path released the tile slot and reset_to_static
         * frees dynamic wall slots each frame, so the exclusive-slot claim is
         * safe. Everything else draws as normal scenery. */
        if( sc->painter_wall_ab >= 0 )
            painter_add_wall(
                world->painter, grid_x, grid_z, paint_level, sc->element_id,
                sc->painter_wall_ab, sc->painter_wall_side);
        /* Ground decor (shape 22) belongs in the tile's exclusive decor slot,
         * not in its scenery chain. The slot is emitted in the tile's BASE
         * step, ahead of every scenery element whose footprint covers the
         * tile — which is the whole difference for a puddle spawned under a
         * 5x5 boss: as scenery it sorted against him and won on any tile
         * nearer the camera than his anchor. */
        else if( sc->painter_ground_decor )
            painter_add_ground_decor_dynamic(
                world->painter, grid_x, grid_z, paint_level, sc->element_id);
        else
            painter_add_normal_scenery(
                world->painter,
                grid_x,
                grid_z,
                paint_level,
                sc->element_id,
                sc->size_x > 0 ? sc->size_x : 1,
                sc->size_z > 0 ? sc->size_z : 1,
                (world->scene ? ToriDraw_SceneElementOcclusionHeight(world->scene, sc->element_id) : 0));
    }

    /* Ground items render below entities (reference tile.groundObject draws in
     * the tile's base step, before any dynamic sprite). Only the local plane —
     * reference keeps objStacks[minusedlevel] only. Registered first so
     * insertion-order ties keep them behind stacked players/NPCs. */
    pool = &world->entities.obj_stack;
    for( int oi = World_EntityPoolHead(pool); oi != WORLD_ENTITY_NIL;
         oi = World_EntityPoolNext(pool, oi) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, oi);
        if( !stack || stack->element_id < 0 )
            continue;
        if( stack->grid_position.level != local_level )
            continue;
        int grid_x = stack->grid_position.x;
        int grid_z = stack->grid_position.z;
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        painter_add_normal_scenery_ex(
            world->painter,
            grid_x,
            grid_z,
            local_level,
            stack->element_id,
            1,
            1,
            (world->scene ? ToriDraw_SceneElementOcclusionHeight(world->scene, stack->element_id) : 0),
            World_ObjRaiseGet(world, grid_x, grid_z, local_level) > 0 ? (uint8_t)PNTR_SCENERY_RAISED
                                                                     : 0);
    }

    world_dyn_register_players(world, /*only_local=*/true, local_level);
    world_dyn_register_npcs(world, /*alwaysontop=*/true, local_level);
    world_dyn_register_players(world, /*only_local=*/false, local_level);
    world_dyn_register_npcs(world, /*alwaysontop=*/false, local_level);

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        /* Reference addProjectiles: unlink when level !== minusedlevel. */
        if( !p || p->element_id < 0 || p->cycle < p->t1 || p->level != local_level )
            continue;
        struct World_PainterFootprint footprint;
        if( !World_EntityPainterFootprint(
                (int)p->x,
                (int)p->z,
                WORLD_PROJECTILE_PAINTER_PADDING,
                /*yaw=*/0,
                /*forward_padding=*/0, /* reference addDynamic passes false for projectiles */
                world->_scene_size,
                &footprint) )
            continue; /* off-edge padded span: reference draws nothing */
        painter_add_normal_scenery(
            world->painter,
            footprint.sx,
            footprint.sz,
            local_level,
            p->element_id,
            footprint.size_x,
            footprint.size_z,
            (world->scene ? ToriDraw_SceneElementOcclusionHeight(world->scene, p->element_id) : 0));
    }

    pool = &world->entities.spotanim;
    for( int si = World_EntityPoolHead(pool); si != WORLD_ENTITY_NIL;
         si = World_EntityPoolNext(pool, si) )
    {
        struct WorldEntity_Spotanim* s = World_EntityPoolGet(pool, si);
        /* Reference addMapAnim: unlink when level !== minusedlevel. */
        if( !s || s->element_id < 0 || !s->active || s->level != local_level )
            continue;
        int grid_x = (int)(s->draw_position.x >> 7);
        int grid_z = (int)(s->draw_position.z >> 7);
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        painter_add_normal_scenery(
            world->painter,
            grid_x,
            grid_z,
            local_level,
            s->element_id,
            1,
            1,
            (world->scene ? ToriDraw_SceneElementOcclusionHeight(world->scene, s->element_id) : 0));
    }

    /* Plugin-owned objects, last, so a plugin's marker sits in front of a
     * graphic sharing its tile rather than behind one -- the same reason
     * spotanims are registered after projectiles. They are ordinary scenery to
     * the painter, which is the whole point of putting them here instead of on
     * the overlay: they sort against the world and are hidden by what stands
     * in front of them.
     *
     * Level-gated like every other dynamic: the reference draws nothing from a
     * plane the camera is not on. */
    pool = &world->entities.plugin_object;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_PluginObject* obj = World_EntityPoolGet(pool, pi);
        if( !obj || obj->element_id < 0 || !obj->active || obj->level != local_level )
            continue;
        int grid_x = (int)(obj->draw_position.x >> 7);
        int grid_z = (int)(obj->draw_position.z >> 7);
        if( grid_x < 0 || grid_z < 0 || grid_x >= world->_scene_size ||
            grid_z >= world->_scene_size )
            continue;
        painter_add_normal_scenery(
            world->painter,
            grid_x,
            grid_z,
            local_level,
            obj->element_id,
            obj->size_x > 0 ? obj->size_x : 1,
            obj->size_z > 0 ? obj->size_z : 1,
            (world->scene ? ToriDraw_SceneElementOcclusionHeight(world->scene, obj->element_id)
                          : 0));
    }
}

/*
 * Move every actor by `cycles` worth of route -- rev-239 client.method1894,
 * called from method2324 once per rendered frame with the wall-clock delta
 * expressed in 20ms client cycles.
 *
 * Separate from World_Cycle on purpose. The cycle clock is a 20ms grid and a
 * frame almost never lands on it; integrating movement on the grid throws away
 * the remainder every frame, which the frame pacer's own comment in app.c
 * already describes as "a visible hitch even though no frame was late". Here
 * the remainder is the whole point.
 *
 * An entity inside an exact-move window is skipped: the server is placing it
 * explicitly and World_UpdateExactMove owns its position (method3611's
 * `field1471 >= cycle || field1521 >= cycle` early-out).
 */
void
World_MoversAdvance(
    struct World* world,
    float cycles)
{
    assert(world);
    if( !world->load_complete || cycles <= 0.0f )
        return;
    /* Classic spends the speed inside World_Cycle instead; see the enum. */
    if( World_MoverModel(world) != TORIRS_MOVER_FRAME_DELTA )
        return;

    {
        struct World_EntityPool* pool = &world->entities.player;
        for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
             pi = World_EntityPoolNext(pool, pi) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
            if( !player || player->element_id < 0 )
                continue;
            if( World_ExactMoveActive(world, &player->exact_move) )
                continue;
            {
                struct World_MoverInfo info = {
                    .world = world,
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
                World_MoverAdvance(&info, cycles);
            }
        }
    }

    {
        struct World_EntityPool* pool = &world->entities.npc;
        for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
             ni = World_EntityPoolNext(pool, ni) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
            int size;
            if( !npc || npc->element_id < 0 )
                continue;
            if( World_ExactMoveActive(world, &npc->exact_move) )
                continue;
            size = npc->size > 0 ? npc->size : 1;
            {
                struct World_MoverInfo info = {
                    .world = world,
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
                World_MoverAdvance(&info, cycles);
            }
        }
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
