#include "game_entity.h"

void
entity_add_hitmark(
    int* damage_values,
    int* damage_types,
    int* damage_cycles,
    int loop_cycle,
    int damage_type,
    int damage_value)
{
    for( int i = 0; i < ENTITY_DAMAGE_SLOTS; i++ )
    {
        if( damage_cycles[i] <= loop_cycle )
        {
            damage_values[i] = damage_value;
            damage_types[i] = damage_type;
            damage_cycles[i] = loop_cycle + 70;
            return;
        }
    }
}

struct StepCoord
{
    int x;
    int z;
};

static inline void
coord_step(
    struct StepCoord* step,
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
entity_pathing_push_xz(
    struct EntityPathing* pathing,
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

    /**
     * This is the authoritative position of the npc.
     * Always in route[0]
     */
    pathing->route_x[0] = x;
    pathing->route_z[0] = z;
    pathing->route_run[0] = step_type == PATHSTEP_RUN ? 1 : 0;
}

void
entity_pathing_push_step(
    struct EntityPathing* pathing,
    int step_type,
    int direction)
{
    struct StepCoord step = { 0 };
    step.x = pathing->route_x[0];
    step.z = pathing->route_z[0];

    coord_step(&step, direction);

    entity_pathing_push_xz(pathing, step.x, step.z, step_type);
}

enum PathingJump
entity_pathing_jump(
    struct EntityPathing* pathing,
    bool force_teleport,
    int x,
    int z)
{
    if( force_teleport )
    {
        pathing->route_length = 0;
        pathing->route_x[0] = x;
        pathing->route_z[0] = z;
        return PATHING_JUMP_TELEPORT;
    }
    else
    {
        // Try to move to the destination without teleporting
        int dx = x - pathing->route_x[0];
        int dz = z - pathing->route_z[0];
        if( dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8 )
        {
            // Close enough to step to it.
            entity_pathing_push_xz(pathing, x, z, PATHSTEP_WALK);
            return PATHING_JUMP_WALK;
        }
        else
        {
            // Too far away to step to it.
            // Teleport
            return entity_pathing_jump(pathing, true, x, z);
        }
    }
}

void
entity_draw_position_set_to_tile(
    struct EntityDrawPosition* draw_position,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z)
{
    draw_position->x = tile_x * 128 + size_x * 64;
    draw_position->z = tile_z * 128 + size_z * 64;
}