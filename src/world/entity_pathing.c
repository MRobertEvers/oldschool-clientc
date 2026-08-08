#include "entity_pathing.h"

#include "engine/world_builder/collision_map.h"

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
    int slot_limit)
{
    int start_cycle = loop_cycle + delay;

    if( slot_limit <= 0 || slot_limit > WORLD_ENTITY_DAMAGE_SLOTS )
        slot_limit = WORLD_ENTITY_DAMAGE_SLOTS;
    for( int i = 0; i < slot_limit; i++ )
    {
        if( damage_cycles[i] <= loop_cycle )
        {
            damage_values[i] = (uint8_t)damage_value;
            damage_types[i] = (uint8_t)damage_type;
            damage_start_cycles[i] = start_cycle;
            damage_cycles[i] = start_cycle + 70;
            return;
        }
    }
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
        int route_len = collision_map_try_route(
            collision, src_x, src_z, x, z, true, route_x, route_z, 10, NULL);

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
    draw_position->x = (uint32_t)(tile_x * 128 + size_x * 64);
    draw_position->z = (uint32_t)(tile_z * 128 + size_z * 64);
}
