#ifndef WORLD_ENTITY_PATHING_H
#define WORLD_ENTITY_PATHING_H

#include "entity_facets.h"

#include <stdbool.h>

#define WORLD_PATHSTEP_WALK 0
#define WORLD_PATHSTEP_RUN 1

struct CollisionMap;

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

/* RuneLite rev-239 method2600 equivalent: RUN traversal pathfinds locally to
 * the reported endpoint and queues intermediate turns before that endpoint. */
enum World_PathingJump
World_EntityPathingJumpCollisionAware(
    struct WorldEntityFacet_Pathing* pathing,
    struct CollisionMap* collision,
    bool force_teleport,
    int x,
    int z,
    int step_type);

void
World_EntityDrawPositionSetToTile(
    struct WorldEntityFacet_DrawPosition* draw_position,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z);

/**
 * Slot-full policy, the hitsplat config's opcode 12 (`field5318` in the rev-239
 * deob's `class420`). The reference's own default is DISCARD; a zeroed value is
 * NOT the default, which is why this is passed rather than defaulted to 0.
 */
enum World_HitmarkSlotPolicy
{
    /** Drop the incoming splat. The reference's default (-1). */
    WORLD_HITMARK_POLICY_DISCARD = -1,
    /** Overwrite the splat with the lowest remaining cycle. */
    WORLD_HITMARK_POLICY_EVICT_OLDEST = 0,
    /** Overwrite the lowest-valued splat, unless it is already >= the new one. */
    WORLD_HITMARK_POLICY_EVICT_SMALLEST = 1,
};

/** The reference's pre-loop default for the splat duration (opcode 9). */
#define WORLD_HITMARK_DEFAULT_DURATION 70

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
    int slot_policy);

#endif
