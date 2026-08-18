#include "entity_pathing.h"

#include "engine/world_builder/collision_map.h"
#include "features/features.h"

#include <stddef.h>
#include <stdint.h>

void
World_EntityAddHitmark(
    uint8_t* damage_values,
    uint8_t* damage_types,
    int* damage_start_cycles,
    int* damage_cycles,
    int loop_cycle,
    int damage_type,
    int damage_value,
    int delay,
    int slot_limit,
    int duration,
    int slot_policy)
{
    int start_cycle = loop_cycle + delay;
    int cursor = 0;
    int chosen = -1;
    int full = 1;

    if( slot_limit <= 0 || slot_limit > WORLD_ENTITY_DAMAGE_SLOTS )
        slot_limit = WORLD_ENTITY_DAMAGE_SLOTS;
    if( duration <= 0 )
        duration = WORLD_HITMARK_DEFAULT_DURATION;

    /* Slot choice follows the reference's `class105.method3560` (rev-239 deob).
     *
     * It does NOT take the lowest free index. It first derives a cursor sitting
     * one past the LAST slot still showing a splat, modulo the limit, then scans
     * from there and wraps:
     *
     *     for (var9 = 0; var9 < size; var9++)
     *         if (list[var9].cycle > now) var8 = (var9 + 1) % var5;
     *         else                        var7 = false;      // a free slot exists
     *
     * then, when not full,
     *
     *     if (var5 > 4) var8 = 0;
     *     for (var17 = 0; var17 < var5; var17++) { var18 = var8; var8 = (var8 + 1) % var5; ... }
     *
     * Scanning from 0 instead — which is what this did — reuses the lowest
     * expired index, so a splat that expires on the left is refilled while a
     * newer one sits to its right, and concurrent splats swap places as they
     * age. Starting past the last live splat keeps a burst laid out in the
     * order it arrived, which is what makes a scythe's 2-3 same-tick hits read
     * left-to-right instead of shuffling.
     *
     * The reference's list GROWS to `var5`; this array is preallocated to
     * WORLD_ENTITY_DAMAGE_SLOTS with "expired" standing in for "not yet
     * created", so the `var18 >= size` append branch has no counterpart here —
     * it cannot fire once the list has reached its cap, which is the steady
     * state this array is always in.
     *
     * The `var5 > 4` reset is kept even though WORLD_ENTITY_DAMAGE_SLOTS caps
     * the limit at 4 today: it is one line, and it is the reference's own
     * handling for the case a server sends a larger maxHitsplats. */
    for( int i = 0; i < slot_limit; i++ )
    {
        if( damage_cycles[i] > loop_cycle )
            cursor = (i + 1) % slot_limit;
        else
            full = 0; /* An expired slot exists, so nothing has to be evicted. */
    }
    if( slot_limit > 4 )
        cursor = 0;

    if( !full )
    {
        for( int i = 0; i < slot_limit; i++ )
        {
            int slot = cursor;

            cursor = (cursor + 1) % slot_limit;
            if( damage_cycles[slot] <= loop_cycle )
            {
                chosen = slot;
                break;
            }
        }
    }
    else
    {
        /* Every slot is live. What happens now is the hitsplat TYPE's decision,
         * not a fixed rule: `field5318` (opcode 12 of the hitsplat config).
         * Discard is its default, which is why a caller that has no config
         * should pass WORLD_HITMARK_POLICY_DISCARD rather than 0 — 0 is a real,
         * different policy. */
        int best = 0;

        if( slot_policy == WORLD_HITMARK_POLICY_DISCARD )
            return;

        for( int i = 0; i < slot_limit; i++ )
        {
            int metric = (slot_policy == WORLD_HITMARK_POLICY_EVICT_SMALLEST)
                             ? (int)damage_values[i]
                             : damage_cycles[i];

            if( i == 0 || metric < best )
            {
                best = metric;
                chosen = i;
            }
        }

        /* EVICT_SMALLEST additionally refuses a hit that would not be an
         * improvement: the reference drops the incoming splat when the smallest
         * one already on screen is at least as large. Without this a stream of
         * 1s would keep displacing a big hit. */
        if( slot_policy == WORLD_HITMARK_POLICY_EVICT_SMALLEST && best >= damage_value )
            return;
    }

    if( chosen < 0 )
        return;

    damage_values[chosen] = (uint8_t)damage_value;
    damage_types[chosen] = (uint8_t)damage_type;
    damage_start_cycles[chosen] = start_cycle;
    /* `duration` is the type's own `field5309` (opcode 9), defaulted by the
     * caller to the reference's pre-loop 70 when the cache has no record. This
     * was hardcoded to 70 before the hitsplat config decoder could read it. */
    damage_cycles[chosen] = start_cycle + duration;
}

struct World_StepCoord
{
    int x;
    int z;
};

static void
World_CoordStep(
    struct World_StepCoord* step,
    int direction)
{
    int next_x = step->x;
    int next_z = step->z;
    if( direction == 0 )
    {
        next_x--;
        next_z++;
    }
    else if( direction == 1 )
    {
        next_z++;
    }
    else if( direction == 2 )
    {
        next_x++;
        next_z++;
    }
    else if( direction == 3 )
    {
        next_x--;
    }
    else if( direction == 4 )
    {
        next_x++;
    }
    else if( direction == 5 )
    {
        next_x--;
        next_z--;
    }
    else if( direction == 6 )
    {
        next_z--;
    }
    else if( direction == 7 )
    {
        next_x++;
        next_z--;
    }

    step->x = next_x;
    step->z = next_z;
}

void
World_EntityPathingPushXZ(
    struct WorldEntityFacet_Pathing* pathing,
    int x,
    int z,
    int step_type)
{
    if( pathing->route_length < 9 )
        pathing->route_length++;

    for( int i = pathing->route_length; i > 0; i-- )
    {
        pathing->route_x[i] = pathing->route_x[i - 1];
        pathing->route_z[i] = pathing->route_z[i - 1];
        pathing->route_run[i] = pathing->route_run[i - 1];
    }

    pathing->route_x[0] = (uint8_t)x;
    pathing->route_z[0] = (uint8_t)z;
    pathing->route_run[0] = step_type == WORLD_PATHSTEP_RUN ? 1 : 0;
}

void
World_EntityPathingPushStep(
    struct WorldEntityFacet_Pathing* pathing,
    int step_type,
    int direction)
{
    struct World_StepCoord step = { 0 };
    step.x = pathing->route_x[0];
    step.z = pathing->route_z[0];

    World_CoordStep(&step, direction);
    World_EntityPathingPushXZ(pathing, step.x, step.z, step_type);
}

enum World_PathingJump
World_EntityPathingJump(
    struct WorldEntityFacet_Pathing* pathing,
    bool force_teleport,
    int x,
    int z)
{
    if( force_teleport )
    {
        pathing->route_length = 0;
        pathing->route_x[0] = (uint8_t)x;
        pathing->route_z[0] = (uint8_t)z;
        return WORLD_PATHING_JUMP_TELEPORT;
    }

    int dx = x - pathing->route_x[0];
    int dz = z - pathing->route_z[0];
    if( dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8 )
    {
        World_EntityPathingPushXZ(pathing, x, z, WORLD_PATHSTEP_WALK);
        return WORLD_PATHING_JUMP_WALK;
    }

    return World_EntityPathingJump(pathing, true, x, z);
}

enum World_PathingJump
World_EntityPathingJumpCollisionAware(
    struct WorldEntityFacet_Pathing* pathing,
    struct CollisionMap* collision,
    bool force_teleport,
    int x,
    int z,
    int step_type)
{
    int src_x = pathing->route_x[0];
    int src_z = pathing->route_z[0];

    /* RuneLite rev-239 Statics.method3189 -> method2600: RUN traversal is
     * geometry-independent from the GPI WALK/RUN displacement opcode. Before
     * the reported endpoint is queued, run the client pathfinder from the
     * newest queued tile and retain every intermediate turn. The renderer then
     * consumes a continuous run route around the corner instead of either
     * cutting the diagonal or slowing the server tick to a walk. */
    if( !force_teleport && collision && step_type == WORLD_PATHSTEP_RUN )
    {
        int route_x[10];
        int route_z[10];
        /* The endpoint here is a tile the *server* just said the entity stands
         * on, so the fallback is all but unreachable — but method2600 passes
         * findClosest=true, so keep one. The classic ring is what this path has
         * always used; it is not a click, so it does not read the era's
         * ground_click_nearest_model. */
        struct CollisionNearestOpts nearest;
        int route_len;

        collision_nearest_opts_from_model(TORIRS_NEAREST_RING3_STEPS, &nearest);
        route_len = collision_map_try_route(
            collision, src_x, src_z, x, z, &nearest, route_x, route_z, 10, NULL);

        /* try_route is destination-first; actor queues are filled source-first.
         * Exclude route[0], because World_EntityPathingJump queues the reported
         * endpoint below exactly as method3189 does after method2600 returns. */
        for( int i = route_len - 1; i > 0; i-- )
            World_EntityPathingPushXZ(pathing, route_x[i], route_z[i], step_type);
    }

    {
        enum World_PathingJump jump =
            World_EntityPathingJump(pathing, force_teleport, x, z);
        if( jump == WORLD_PATHING_JUMP_WALK )
            pathing->route_run[0] = step_type == WORLD_PATHSTEP_RUN ? 1 : 0;
        return jump;
    }
}

void
World_EntityDrawPositionSetToTile(
    struct WorldEntityFacet_DrawPosition* draw_position,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z)
{
    World_DrawPositionSet(
        draw_position, tile_x * 128 + size_x * 64, tile_z * 128 + size_z * 64);
}
