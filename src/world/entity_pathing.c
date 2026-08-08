#include "entity_pathing.h"

#include "engine/world_builder/collision_map.h"

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
    int dx = x - src_x;
    int dz = z - src_z;

    if( !force_teleport && collision && (dx == -1 || dx == 1) &&
        (dz == -1 || dz == 1) &&
        !collision_map_can_travel(collision, src_x, src_z, dx, dz, 1, 0) )
    {
        int x_first =
            collision_map_can_travel(collision, src_x, src_z, dx, 0, 1, 0) &&
            collision_map_can_travel(collision, src_x + dx, src_z, 0, dz, 1, 0);
        int z_first =
            collision_map_can_travel(collision, src_x, src_z, 0, dz, 1, 0) &&
            collision_map_can_travel(collision, src_x, src_z + dz, dx, 0, 1, 0);

        /* The authoritative mover did reach the diagonal endpoint. If either
         * L-shaped route explains it, retain that corner tile in the local
         * queue instead of interpolating straight through the obstruction. */
        if( x_first )
            World_EntityPathingPushXZ(pathing, src_x + dx, src_z, step_type);
        else if( z_first )
            World_EntityPathingPushXZ(pathing, src_x, src_z + dz, step_type);
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
