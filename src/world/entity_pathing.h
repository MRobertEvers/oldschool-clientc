#ifndef WORLD_ENTITY_PATHING_H
#define WORLD_ENTITY_PATHING_H

#include "entity_facets.h"

#include <stdbool.h>

#define WORLD_PATHSTEP_WALK 0
#define WORLD_PATHSTEP_RUN 1

enum World_PathingJump
{
    WORLD_PATHING_JUMP_TELEPORT,
    WORLD_PATHING_JUMP_WALK,
};

void
World_EntityPathingPushXZ(
    struct WorldEntityFacet_Pathing* pathing,
    int x,
    int z,
    int step_type);

void
World_EntityPathingPushStep(
    struct WorldEntityFacet_Pathing* pathing,
    int step_type,
    int direction);

enum World_PathingJump
World_EntityPathingJump(
    struct WorldEntityFacet_Pathing* pathing,
    bool force_teleport,
    int x,
    int z);

void
World_EntityDrawPositionSetToTile(
    struct WorldEntityFacet_DrawPosition* draw_position,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z);

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
    int slot_limit);

#endif
